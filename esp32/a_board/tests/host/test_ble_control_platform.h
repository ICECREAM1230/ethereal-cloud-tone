#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define MYNEWT_VAL(x) 1
typedef int BaseType_t;
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(x) pthread_mutex_lock(x)
#define portEXIT_CRITICAL(x) pthread_mutex_unlock(x)
typedef struct fake_queue *QueueHandle_t;
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;bool available;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef void *TaskHandle_t;
QueueHandle_t xQueueCreate(unsigned count,size_t item_size);
int xQueueSend(QueueHandle_t queue,const void *item,unsigned timeout);
int xQueueReceive(QueueHandle_t queue,void *item,unsigned timeout);
void vQueueDelete(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
int xSemaphoreTake(SemaphoreHandle_t semaphore,unsigned timeout);
void xSemaphoreGive(SemaphoreHandle_t semaphore);
int xTaskCreate(void (*function)(void*),const char *name,unsigned stack,void *argument,
                unsigned priority,TaskHandle_t *handle);
void vTaskDelay(unsigned ticks);
_Noreturn void vTaskDelete(TaskHandle_t task);
int xTimerPendFunctionCall(void (*function)(void*,uint32_t),void *argument,uint32_t value,unsigned timeout);
int64_t esp_timer_get_time(void);

#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_ATT_MTU_DFLT 23
#define BLE_HS_ENOTCONN 7
#define BLE_HS_ENOMEM 6
#define BLE_HS_ETIMEOUT 13
#define BLE_HS_EAGAIN 1
#define BLE_HS_EBUSY 15
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_WRITE_NO_RSP 4
#define BLE_GATT_CHR_F_NOTIFY 16
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_SUBSCRIBE 14
#define BLE_GAP_EVENT_MTU 15
#define BLE_GAP_EVENT_NOTIFY_TX 13
typedef struct {uint8_t type;} ble_uuid_t;
typedef struct {ble_uuid_t u;uint8_t value[16];} ble_uuid128_t;
#define BLE_UUID128_INIT(...) {.u={128},.value={__VA_ARGS__}}
struct os_mbuf {size_t length;bool heap;uint8_t bytes[514];};
#define OS_MBUF_PKTLEN(o) ((o)->length)
struct ble_gatt_access_ctxt {unsigned op;struct os_mbuf *om;};
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;
    int (*access_cb)(uint16_t,uint16_t,struct ble_gatt_access_ctxt*,void*);
    unsigned flags;
    uint16_t *val_handle;
};
struct ble_gatt_svc_def {unsigned type;const ble_uuid_t *uuid;const struct ble_gatt_chr_def *characteristics;};
struct ble_gap_event {
    int type;
    union {
        struct {int status;uint16_t conn_handle;} connect;
        struct {struct {uint16_t conn_handle;} conn;} disconnect;
        struct {uint16_t conn_handle,value;} mtu;
        struct {uint16_t conn_handle,attr_handle;bool cur_notify;} subscribe;
        struct {uint16_t conn_handle,attr_handle;int status;bool indication;} notify_tx;
    };
};
struct ble_npl_event {void (*function)(struct ble_npl_event*);void *argument;bool queued;};
void ble_npl_event_init(struct ble_npl_event *event,void (*function)(struct ble_npl_event*),void *argument);
void ble_npl_eventq_put(void *queue,struct ble_npl_event *event);
void *nimble_port_get_dflt_eventq(void);
int os_msys_num_free(void);
int os_mbuf_copydata(const struct os_mbuf *mbuf,unsigned offset,unsigned length,void *output);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *bytes,uint16_t length);
void os_mbuf_free_chain(struct os_mbuf *mbuf);
int ble_gatts_notify_custom(uint16_t connection,uint16_t attribute,struct os_mbuf *mbuf);
int ble_gap_terminate(uint16_t connection,unsigned reason);
