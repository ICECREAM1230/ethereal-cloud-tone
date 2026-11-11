// Copyright (c) 2026 ICECREAM1230
// ble_midi.c — MIDI over Bluetooth LE (peripheral)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Implements MIDI 1.0 over BLE (the MMA / Bluetooth SIG service):
// primary service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 carrying one
// "MIDI data I/O" characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3
// (read, write, write-without-response, notify). Centrals with BLE
// MIDI support — macOS Audio MIDI Setup, Android 13+, iOS — discover
// "FPGA_Bluetooth" and treat it like any wired MIDI port.
//
// The GATT callback copies ATT payloads and a connection epoch into a ring.
// ble_rx preserves MIDI message meaning, restoring status after Common only
// when required, and submits the byte stream to raw source 2.
// No BLE music is parsed into semantic events and no BLE callback performs SPI.
// Source loss invalidates queued epochs and flushes only BLE through raw_midi.

#include "ble_midi.h"
#include "status_led.h"

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// NimBLE's NVS-backed bond store (no public header for this in IDF;
// the bundled bleprph example declares it the same way)
void ble_store_config_init(void);

#include "ble_midi_unframer.h"
#include "midi_feedback_guard.h"
#include "midi_feedback_transport.h"
#include "raw_midi.h"

#define TAG "ble_midi"

// Standard MIDI 1.0 over BLE service (03B80E5A-EDE8-4B33-A751-6CE34EC4C700)
// and its MIDI data I/O characteristic (7772E5DB-3868-4112-A1A9-F2669D106BF3).
// BLE_UUID128_INIT takes the bytes in on-air (little-endian) order, i.e.
// the printed UUID reversed.
#define MIDI_SVC_UUID BLE_UUID128_INIT(0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, \
                                       0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03)
#define MIDI_IO_UUID BLE_UUID128_INIT(0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, \
                                      0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77)

// Largest possible packet: the negotiated ATT MTU (we prefer 517).
#define BLE_MIDI_MAX_PKT 517

static const ble_uuid128_t gatt_svr_svc_midi_uuid = MIDI_SVC_UUID;
static const ble_uuid128_t gatt_svr_chr_midi_io_uuid = MIDI_IO_UUID;
static uint16_t gatt_svr_chr_midi_io_handle;
static uint16_t s_feedback_conn=BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_feedback_mtu=BLE_ATT_MTU_DFLT;
static bool s_feedback_subscribed;
static uint64_t s_feedback_sync_epoch;
static struct ble_npl_event s_feedback_notify_event;
static SemaphoreHandle_t s_feedback_notify_done;
static SemaphoreHandle_t s_feedback_notify_mutex;
static midi_feedback_transport_t s_feedback_notify;
static web_control_state_t s_feedback_owner_state;

// Advertising is one-shot: the stack stops it when a connection
// forms, and the GAP callback restarts it on failure/disconnect.
static bool s_advertising;
static int s_conn_count;

// One physical connection is configured. A 64-bit epoch is also the raw
// session, so disconnect/loss never reuses a queued producer identity.
#define BLE_RAW_SOURCE 2u
static portMUX_TYPE s_ble_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_ble_epoch = 1;
static bool s_ble_live;
static uint64_t s_bound_epoch, s_unframer_epoch;
static ble_midi_unframer_t s_unframer;

// NOSPLIT preserves ATT boundaries. Only ble_rx owns the unframer.
#define BLE_RX_RB_SIZE   4096
#define BLE_RX_TASK_STACK 3072
#define BLE_RX_TASK_PRIO  4
static RingbufHandle_t s_rx_rb;
static uint32_t s_rx_drops;

static uint64_t ble_raw_snapshot(void)
{
    portENTER_CRITICAL(&s_ble_lock);
    uint64_t epoch = s_ble_live ? s_ble_epoch : 0;
    portEXIT_CRITICAL(&s_ble_lock);
    return epoch;
}

// Caller holds s_ble_lock. raw_midi functions only queue work; the SPI owner
// never acquires this lock. Serializing source flush with submit closes the
// disconnect-after-check race without parsing or doing SPI in the callback.
static void ble_raw_invalidate_locked(void)
{
    if (++s_ble_epoch == 0) ++s_ble_epoch;
    raw_midi_flush(BLE_RAW_SOURCE);
}

static void ble_raw_set_live(bool live)
{
    portENTER_CRITICAL(&s_ble_lock);
    s_ble_live = live;
    ble_raw_invalidate_locked();
    portEXIT_CRITICAL(&s_ble_lock);
}

static void ble_raw_loss(uint64_t epoch)
{
    portENTER_CRITICAL(&s_ble_lock);
    if (epoch && s_ble_live && s_ble_epoch == epoch)
        ble_raw_invalidate_locked();
    portEXIT_CRITICAL(&s_ble_lock);
}

// RX-only: validate the entire ATT packet before binding/submitting bytes.
// The raw driver queues each source's bind flush ahead of its first new byte.
static void process_ble_midi_pkt(uint64_t epoch, const uint8_t *pkt, size_t n)
{
    if (!epoch || ble_raw_snapshot() != epoch) return;
    if (s_unframer_epoch != epoch) {
        ble_midi_unframer_reset(&s_unframer);
        s_unframer_epoch = epoch;
    }
    // Every restored status replaces a mandatory timestamp; unframer output
    // is at most ATT length - 1 (516 bytes at our maximum packet size).
    uint8_t bytes[BLE_MIDI_MAX_PKT];
    size_t length = 0;
    if (!ble_midi_unframer_feed_packet(&s_unframer, pkt, n, bytes,
                                       sizeof(bytes), &length)) {
        ble_raw_loss(epoch);
        return;
    }
    portENTER_CRITICAL(&s_ble_lock);
    if (s_ble_live && s_ble_epoch == epoch) {
        if (s_bound_epoch != epoch) {
            raw_midi_bind(BLE_RAW_SOURCE, epoch);
            s_bound_epoch = epoch;
        }
        if (!raw_midi_submit(BLE_RAW_SOURCE, epoch, bytes, length, 0))
            ble_raw_invalidate_locked();
    }
    portEXIT_CRITICAL(&s_ble_lock);
}

// Drain the ingress ring buffer, with the standard single-core yield
// guard (#70/#83): after ~2 ms of unbroken work, give IDLE a tick.
static void ble_rx_task(void *arg)
{
    int64_t busy_since = esp_timer_get_time();
    while (1) {
        size_t n = 0;
        uint8_t *pkt = xRingbufferReceive(s_rx_rb, &n, pdMS_TO_TICKS(100));
        if (pkt == NULL) {
            busy_since = esp_timer_get_time();   // we blocked
            continue;
        }
        if (n >= sizeof(uint64_t)) {
            uint64_t epoch;
            memcpy(&epoch, pkt, sizeof(epoch));
            process_ble_midi_pkt(epoch, pkt + sizeof(epoch), n - sizeof(epoch));
        }
        vRingbufferReturnItem(s_rx_rb, pkt);

        portENTER_CRITICAL(&s_ble_lock);
        uint32_t drops = s_rx_drops;
        s_rx_drops = 0;
        portEXIT_CRITICAL(&s_ble_lock);
        if (drops)
            ESP_LOGW(TAG, "rx ring full, dropped %" PRIu32 " packets", drops);
        int64_t now = esp_timer_get_time();
        if (now - busy_since > 2000) {
            vTaskDelay(1);
            busy_since = esp_timer_get_time();
        }
    }
}

// ── GATT access callback ───────────────────────────────────────────
// Writes carry one BLE MIDI packet (possibly several MIDI messages
// under running status). COPY into the ring buffer and return — never
// parse in the NimBLE host task (#83).
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0; // reads (and CCCD bookkeeping) pass through
    }

    uint64_t epoch = ble_raw_snapshot();
    if (!epoch) return 0;
    struct os_mbuf *om = ctxt->om;
    uint16_t n = OS_MBUF_PKTLEN(om);
    struct {uint64_t epoch; uint8_t bytes[BLE_MIDI_MAX_PKT];} packet;
    if (n < 2 || n > sizeof(packet.bytes)) {
        ble_raw_loss(epoch);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    packet.epoch = epoch;
    if (os_mbuf_copydata(om, 0, n, packet.bytes) != 0) {
        ble_raw_loss(epoch);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (xRingbufferSend(s_rx_rb, &packet, sizeof(packet.epoch) + n, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_ble_lock);
        s_rx_drops++;
        portEXIT_CRITICAL(&s_ble_lock);
        ble_raw_loss(epoch);
    }
    return 0;
}

// Issue #67 notes:
// - NO manual CCCD descriptor: NimBLE auto-creates the CCCD for any
//   NOTIFY characteristic. The hand-declared one here produced TWO
//   CCCDs on the characteristic — a GATT violation some centrals
//   punish with a disconnect.
// - Encryption is SUPPORTED, not REQUIRED (issue #79). The SM config
//   in ble_midi_init() lets Apple pair+encrypt by its own BLE-MIDI
//   policy — that (not a characteristic flag) is what fixed the
//   macOS reason-531 drops. Adding _ENC flags to REQUIRE encryption
//   then broke Android, which connects without pairing (BLE MIDI
//   Engineer Lite: connects but writes rejected, no MIDI). Plain
//   READ/WRITE serves both: Apple still encrypts, Android works.
static const struct ble_gatt_chr_def gatt_svr_chrs[] = {
    {
        .uuid = &gatt_svr_chr_midi_io_uuid.u,
        .access_cb = gatt_svr_chr_access,
        .flags = BLE_GATT_CHR_F_READ |
                 BLE_GATT_CHR_F_WRITE |
                 BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &gatt_svr_chr_midi_io_handle,
    },
    { 0, }
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_midi_uuid.u,
        .characteristics = gatt_svr_chrs,
    },
    { 0, }
};

// ── Advertising ────────────────────────────────────────────────────

static int ble_midi_gap_event(struct ble_gap_event *event, void *arg);

// Log the parameters actually in effect on a connection — the
// supervision timeout is what a reason-520 (0x08) drop hinges on
// (issue #67). Units per HCI: interval 1.25 ms, timeout 10 ms.
static void log_conn_params(uint16_t conn_handle)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn_handle, &d) != 0)
        return;
    ESP_LOGI(TAG, "conn params in effect: itvl=%u (%u.%02u ms) latency=%u "
                  "timeout=%u (%u ms)",
             d.conn_itvl, (d.conn_itvl * 125) / 100, (d.conn_itvl * 125) % 100,
             d.conn_latency, d.supervision_timeout, d.supervision_timeout * 10);
}

// Request our preferred connection parameters. Apple-friendly:
// 15-30 ms interval, no latency, 6 s supervision (generous, so a
// brief RF hiccup can't trip a reason-520 timeout). MUST run AFTER
// encryption — requesting it in the CONNECT event collided with
// macOS's own pairing procedure (observed status=554 = HCI 0x2A,
// different transaction collision) and was rejected (issue #67).
static void request_conn_params(uint16_t conn_handle)
{
    struct ble_gap_upd_params up = {
        .itvl_min = 12, .itvl_max = 24,
        .latency = 0, .supervision_timeout = 600,
    };
    ble_gap_update_params(conn_handle, &up);
}

static const char s_ble_name[] = "FPGA_Bluetooth";
_Static_assert(sizeof(s_ble_name) + 1 <= 31, "BLE name exceeds scan response");

static void ble_midi_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&gatt_svr_svc_midi_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: rc=%d", rc);
        status_led_set_flag(STATUS_BLE_READY, false);
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }

    // Legacy ADV: flags (3) + MIDI UUID (18) = 21 bytes. Full name
    // belongs in scan response: 2 + 14 = 16 bytes (ADV limit is 31).
    memset(&fields, 0, sizeof fields);
    fields.name = (uint8_t *)s_ble_name;
    fields.name_len = sizeof(s_ble_name) - 1;
    fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response fields: rc=%d", rc);
        status_led_set_flag(STATUS_BLE_READY, false);
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_midi_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start: rc=%d", rc);
        status_led_set_flag(STATUS_BLE_READY, false);
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }
    s_advertising = true;
    status_led_set_flag(STATUS_BLE_FAULT, false);
    status_led_set_flag(STATUS_BLE_READY, true);
}

static int ble_midi_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_advertising = false;
        if (event->connect.status != 0) {
            status_led_set_flag(STATUS_BLE_READY, false);
            ESP_LOGW(TAG, "connect failed, re-advertising");
            ble_midi_advertise();
        } else {
            ble_raw_set_live(true);
            portENTER_CRITICAL(&s_ble_lock);
            s_feedback_conn=event->connect.conn_handle;
            s_feedback_mtu=BLE_ATT_MTU_DFLT;s_feedback_subscribed=false;
            s_feedback_sync_epoch=0;
            portEXIT_CRITICAL(&s_ble_lock);
            s_conn_count++;
            status_led_set_flag(STATUS_BLE_CONNECTED, true);
            status_led_set_flag(STATUS_BLE_READY, true);
            status_led_set_flag(STATUS_BLE_FAULT, false);
            ESP_LOGI(TAG, "MIDI connection established");
            log_conn_params(event->connect.conn_handle);
            // params requested AFTER encryption (see ENC_CHANGE) to
            // avoid the pairing-time collision (issue #67)
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        // Invalidate queued ATT packets and silence only raw source 2.
        portENTER_CRITICAL(&s_ble_lock);
        s_feedback_conn=BLE_HS_CONN_HANDLE_NONE;s_feedback_subscribed=false;
        s_feedback_sync_epoch=0;
        portEXIT_CRITICAL(&s_ble_lock);
        ble_raw_set_live(false);
        if (s_conn_count > 0) {
            s_conn_count--;
        }
        status_led_set_flag(STATUS_BLE_CONNECTED, s_conn_count > 0);
        status_led_set_flag(STATUS_BLE_READY, false);
        // reason = 512 + HCI code; 531 = 0x13 remote user terminated
        ESP_LOGI(TAG, "disconnected (reason=%d / HCI 0x%02x), advertising",
                 event->disconnect.reason, event->disconnect.reason - 512);
        ble_midi_advertise();
        return 0;
    }

    // A central re-pairing while it holds a stale bond (our side lost
    // or rotated keys): NimBLE's default REJECTS it, which the
    // central answers by disconnecting (issue #67). Delete our stale
    // peer entry and let the pairing retry.
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        ESP_LOGI(TAG, "stale bond replaced, retrying pairing");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption %s (status=%d)",
                 event->enc_change.status == 0 ? "established" : "FAILED",
                 event->enc_change.status);
        if (event->enc_change.status == 0)
            request_conn_params(event->enc_change.conn_handle);  // now valid
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "conn params updated (status=%d)",
                 event->conn_update.status);
        log_conn_params(event->conn_update.conn_handle);
        return 0;

    case BLE_GAP_EVENT_MTU:
        portENTER_CRITICAL(&s_ble_lock);
        if(event->mtu.conn_handle==s_feedback_conn)s_feedback_mtu=event->mtu.value;
        portEXIT_CRITICAL(&s_ble_lock);
        ESP_LOGI(TAG, "MTU %u", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if(event->subscribe.attr_handle==gatt_svr_chr_midi_io_handle) {
            portENTER_CRITICAL(&s_ble_lock);
            if(event->subscribe.conn_handle==s_feedback_conn) {
                s_feedback_subscribed=event->subscribe.cur_notify;
                s_feedback_sync_epoch=s_feedback_subscribed?s_ble_epoch:0;
            }
            portEXIT_CRITICAL(&s_ble_lock);
            ESP_LOGI(TAG,"notifications %s",event->subscribe.cur_notify?"subscribed":"off");
        }
        return 0;

    default:
        return 0;
    }
}

// ── Host lifecycle ─────────────────────────────────────────────────

static void ble_midi_on_reset(int reason)
{
    ble_raw_set_live(false);
    s_advertising = false;
    s_conn_count = 0;
    portENTER_CRITICAL(&s_ble_lock);
    s_feedback_conn=BLE_HS_CONN_HANDLE_NONE;s_feedback_subscribed=false;
    s_feedback_sync_epoch=0;
    portEXIT_CRITICAL(&s_ble_lock);
    status_led_set_flag(STATUS_BLE_READY, false);
    status_led_set_flag(STATUS_BLE_CONNECTED, false);
    status_led_set_flag(STATUS_BLE_FAULT, true);
    ESP_LOGW(TAG, "host reset: reason=%d", reason);
}

bool ble_midi_feedback_link(uint64_t *epoch,size_t *packet_max)
{
    portENTER_CRITICAL(&s_ble_lock);
    bool ready=s_ble_live && s_feedback_subscribed &&
        s_feedback_conn!=BLE_HS_CONN_HANDLE_NONE;
    if(ready){*epoch=s_ble_epoch;*packet_max=s_feedback_mtu>3?s_feedback_mtu-3:0;}
    portEXIT_CRITICAL(&s_ble_lock);return ready;
}

static void ble_midi_feedback_notify_event(struct ble_npl_event *event)
{
    (void)event;
    xSemaphoreTake(s_feedback_notify_mutex, portMAX_DELAY);
    uint32_t token = s_feedback_notify.token;
    bool running = midi_feedback_transport_mark_running(&s_feedback_notify,
                                                         token);
    xSemaphoreGive(s_feedback_notify_mutex);
    int result = BLE_HS_EINVAL;

    // GAP connect/disconnect/subscribe callbacks and this event all execute on
    // the default NimBLE host queue. The final identity check therefore cannot
    // race a disconnect plus same-handle reconnect.
    portENTER_CRITICAL(&s_ble_lock);
    bool current = running &&
        midi_feedback_link_guard_matches(s_feedback_notify.epoch,
                                         s_feedback_notify.conn,
                                         s_ble_epoch, s_feedback_conn,
                                         s_ble_live, s_feedback_subscribed);
    portEXIT_CRITICAL(&s_ble_lock);

    if (current) {
        web_control_read(&s_feedback_owner_state);
        current = midi_feedback_owner_state_matches(
            &s_feedback_notify.owner, &s_feedback_owner_state);
        if (current) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(
                s_feedback_notify.packet,
                (uint16_t)s_feedback_notify.length);
            result = om ? ble_gatts_notify_custom(
                s_feedback_notify.conn, gatt_svr_chr_midi_io_handle, om) :
                BLE_HS_ENOMEM;
        }
    }

    xSemaphoreTake(s_feedback_notify_mutex, portMAX_DELAY);
    midi_feedback_transport_complete(&s_feedback_notify, token, result);
    xSemaphoreGive(s_feedback_notify_mutex);
    xSemaphoreGive(s_feedback_notify_done);
}

bool ble_midi_feedback_notify(uint64_t epoch,const uint8_t *packet,size_t length,
                              const midi_feedback_owner_guard_t *owner)
{
    if (!s_feedback_notify_done || !s_feedback_notify_mutex || !owner)
        return false;
    // The semaphore is only a wake hint. Request phase and token own results.
    xSemaphoreTake(s_feedback_notify_done, 0);

    portENTER_CRITICAL(&s_ble_lock);
    bool ready = s_feedback_conn != BLE_HS_CONN_HANDLE_NONE &&
        epoch == s_ble_epoch && s_ble_live && s_feedback_subscribed;
    uint16_t conn = s_feedback_conn;
    portEXIT_CRITICAL(&s_ble_lock);
    if (!ready)
        return false;

    uint32_t token = 0;
    xSemaphoreTake(s_feedback_notify_mutex, portMAX_DELAY);
    midi_feedback_transport_action_t action = midi_feedback_transport_begin(
        &s_feedback_notify, epoch, conn, packet, length, owner, &token);
    xSemaphoreGive(s_feedback_notify_mutex);
    if (action == MIDI_FEEDBACK_TRANSPORT_SENT)
        return true;
    if (action == MIDI_FEEDBACK_TRANSPORT_FAILED ||
        action == MIDI_FEEDBACK_TRANSPORT_BUSY)
        return false;
    if (action == MIDI_FEEDBACK_TRANSPORT_ENQUEUE)
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
                           &s_feedback_notify_event);
    if (xSemaphoreTake(s_feedback_notify_done, pdMS_TO_TICKS(50)) != pdTRUE)
        return false;

    xSemaphoreTake(s_feedback_notify_mutex, portMAX_DELAY);
    action = midi_feedback_transport_begin(&s_feedback_notify, epoch, conn,
                                            packet, length, owner, &token);
    xSemaphoreGive(s_feedback_notify_mutex);
    return action == MIDI_FEEDBACK_TRANSPORT_SENT;
}

bool ble_midi_feedback_sync_pending(uint64_t *epoch)
{
    portENTER_CRITICAL(&s_ble_lock);
    bool pending = s_feedback_sync_epoch != 0;
    if (pending)
        *epoch = s_feedback_sync_epoch;
    portEXIT_CRITICAL(&s_ble_lock);
    return pending;
}
void ble_midi_feedback_sync_complete(uint64_t epoch)
{
    portENTER_CRITICAL(&s_ble_lock);
    if (s_feedback_sync_epoch == epoch)
        s_feedback_sync_epoch = 0;
    portEXIT_CRITICAL(&s_ble_lock);
}

static void ble_midi_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGW(TAG, "public address unavailable (rc=%d), falling back to random", rc);
        rc = ble_hs_util_ensure_addr(1);
        if (rc != 0) {
            ESP_LOGE(TAG, "no usable address (rc=%d) - not advertising", rc);
            status_led_set_flag(STATUS_BLE_FAULT, true);
            return;
        }
    }
    ble_midi_advertise();
}

static void ble_midi_host_task(void *param)
{
    nimble_port_run(); // blocks for the life of the stack
    ble_raw_set_live(false);
    status_led_set_flag(STATUS_BLE_READY, false);
    status_led_set_flag(STATUS_BLE_CONNECTED, false);
    status_led_set_flag(STATUS_BLE_FAULT, true);
    nimble_port_freertos_deinit();
}

// Console key 'p': print stack state and re-trigger advertising.
// NimBLE APIs are safe to call from any task (they queue to the host
// task), so this can run in the shared serial-console dispatcher.
void ble_midi_console_status(void)
{
    bool synced = ble_hs_is_enabled();
    ESP_LOGI(TAG, "BLE: host %s, advertising=%s, connections=%d",
             synced ? "synced" : "NOT synced",
             s_advertising ? "yes" : "no",
             s_conn_count);
    if (synced && !s_advertising && !s_conn_count) {
        ble_midi_advertise();
    }
}

void ble_midi_init(void)
{
    ble_midi_unframer_init(&s_unframer);

    // Ingress decoupling (#83): ring buffer + drain task, so the
    // NimBLE never parses or publishes semantic events.
    s_rx_rb = xRingbufferCreate(BLE_RX_RB_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (s_rx_rb == NULL) {
        ESP_LOGE(TAG, "failed to create rx ring buffer");
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }
    if (xTaskCreate(ble_rx_task, "ble_rx", BLE_RX_TASK_STACK, NULL,
                    BLE_RX_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create ble_rx task");
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }

    // nimble_port_init() is the full bring-up: controller enable, HCI,
    // and the NPL primitives (mutex, event queue) the host lock needs.
    // It MUST run before any service registration - ble_gatts_add_svcs
    // takes ble_hs_lock, and without this call the lock's mutex does
    // not exist yet (load access fault in xQueueSemaphoreTake, seen on
    // silicon). The bundled bleprph example calls it in app_main for
    // exactly this reason; nimble_port_freertos_init() below only
    // spawns the host task.
    ESP_ERROR_CHECK(nimble_port_init());

    // Persistent mailbox storage makes a bounded worker timeout safe: the
    // queued host event never references a caller stack or temporary mbuf.
    s_feedback_notify_done = xSemaphoreCreateBinary();
    s_feedback_notify_mutex = xSemaphoreCreateMutex();
    if (s_feedback_notify_done == NULL || s_feedback_notify_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create feedback notify mailbox");
        status_led_set_flag(STATUS_BLE_FAULT, true);
        return;
    }
    ble_npl_event_init(&s_feedback_notify_event,
                       ble_midi_feedback_notify_event, NULL);

    // Order matters: the GAP/GATT services must exist before their
    // settings are touched (the bundled bleprph example does exactly
    // this sequence).
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(s_ble_name));

    ble_hs_cfg.reset_cb = ble_midi_on_reset;
    ble_hs_cfg.sync_cb = ble_midi_on_sync;

    // Security manager (issue #67): Apple BLE-MIDI requires an
    // encrypted link. Just-Works pairing (no IO), bonded, LE Secure
    // Connections, encryption + identity keys both ways; bonds
    // persist in NVS (CONFIG_BT_NIMBLE_NVS_PERSIST=y +
    // ble_store_config_init below) so reconnects after a reboot can
    // re-establish encryption instead of being dropped.
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();

    // nimble_port_freertos_init() in this IDF version wraps
    // esp_nimble_enable(): controller + host bring-up inside, void
    // return - errors surface as abort/log from the stack.
    nimble_port_freertos_init(ble_midi_host_task);

    ESP_LOGI(TAG, "BLE MIDI peripheral up, advertising as %s", s_ble_name);
}
