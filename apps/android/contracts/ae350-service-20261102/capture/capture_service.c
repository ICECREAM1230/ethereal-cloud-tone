// App-owned extension of the unchanged firmware SDK-boundary test harness.
// All adoption/fault observations below are an explicit HOST OWNER MODEL.
// They are not an execution of voice_alloc.c, FPGA adoption, or audio proof.
#define HTTP_V2_TEST_MAIN original_http_v2_entry
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN

static const control_api_peer_t service_peer = {CONTROL_API_BLE_CONTROL, 1001};
static FILE *service_trace;
static const char *service_directory;

typedef struct { char bytes[32768]; size_t length; } service_buffer_t;
static esp_err_t service_write(void *context, const char *bytes, size_t length)
{
    service_buffer_t *buffer = context;
    assert(buffer->length + length < sizeof buffer->bytes);
    memcpy(buffer->bytes + buffer->length, bytes, length);
    buffer->length += length; buffer->bytes[buffer->length] = 0;
    return ESP_OK;
}
static void service_call(const char *name, const char *path, const char *body,
                         unsigned expected, const char *observation)
{
    const unsigned before = publication_count;
    control_api_request_t input = {.path=path, .body=body,
        .body_length=body ? strlen(body) : 0, .peer=service_peer,
        .control_authorized=true};
    control_api_response_t response = {0};
    assert(control_api_prepare(&input, &response) == ESP_OK);
    if (response.code != expected)
        fprintf(stderr, "%s expected %u got %u\n", name, expected, response.code);
    assert(response.code == expected);
    service_buffer_t first = {0}, second = {0};
    control_api_sink_t sink = {&first, service_write};
    assert(control_api_emit(&response, &sink) == ESP_OK);
    sink.context = &second;
    assert(control_api_emit(&response, &sink) == ESP_OK);
    assert(first.length == second.length && !memcmp(first.bytes, second.bytes, first.length));
    assert(publication_count == before + (expected == 201 || expected == 202));
    char file[4096];
    assert(snprintf(file, sizeof file, "%s/%s.json", service_directory, name) > 0);
    FILE *output = fopen(file, "wb"); assert(output);
    assert(fwrite(first.bytes, 1, first.length, output) == first.length && !fclose(output));
    assert(fprintf(service_trace, "%s\t%u\t%s\t%s\t%s\t%s\t%u\n", name,
        response.code, path, body ? "string" : "null", body ? body : "",
        observation, publication_count - before) > 0);
    control_api_release(&response);
}
static void service_state(const char *name, const char *observation)
{ service_call(name, "/api/v2/state", NULL, 200, observation); }
static void service_command(const char *name, uint32_t token, uint32_t sequence,
                            unsigned op, uint32_t a, uint32_t b, uint32_t c,
                            unsigned expected)
{
    char body[128];
    assert(snprintf(body, sizeof body, "%" PRIu32 " %" PRIu32 " 9 %u %" PRIu32
        " %" PRIu32 " %" PRIu32, token, sequence, op, a, b, c) > 0);
    service_call(name, "/api/v2/command", body, expected, "real-core-receipt");
}
static void service_adopt(web_control_state_t *owner, unsigned client,
                          uint32_t token, uint32_t sequence, bool parameter)
{
    const evt_t *event = &published[publication_count-1];
    assert(event->kind == EVT_WEB_V2 && event->web_v2.sequence == sequence);
    owner->token[client] = token; owner->sequence[client] = sequence;
    if (parameter) owner->parameter_sequence[client] = sequence;
    owner->v2_result_sequence[client] = sequence;
    owner->v2_result_code[client] = FM_RESULT_OK;
    owner->v2_result_context[client] = owner->context_epoch; owner->revision++;
    control_source_activity_t activity = event->web_v2.op == FM_HTTP_OPCODE_KEYS_UPDATE
        ? (event->web_v2.a ? CONTROL_SOURCE_KEYS : CONTROL_SOURCE_IDLE) : CONTROL_SOURCE_OUTPUT;
    control_source_complete(event->source, token, sequence, activity, true);
    web_control_store(owner);
}

int main(int argc, char **argv)
{
    assert(argc == 2); service_directory = argv[1];
    char trace_file[4096];
    assert(snprintf(trace_file, sizeof trace_file, "%s/requests.tsv", service_directory) > 0);
    service_trace = fopen(trace_file, "wb"); assert(service_trace);
    assert(fputs("name\tcode\tpath\tbody_kind\tbody\tobservation\tpublications\n", service_trace) >= 0);
    web_control_state_t owner = {.ready=true, .revision=1, .context_epoch=9,
        .ks_decay_ms=3000, .sub_patch_required=true, .sub_patch_supported=true,
        .sub_patch_valid=true};
    patch_default(&owner.patch); owner.engine.available=owner.engine.valid=true;
    owner.cpu_mode=(engine_cpu_mode_state_t){.supported=true, .valid=true,
        .ready=true, .channel_routes=true};
    owner.inputs=(web_input_state_t){.version=1, .available=true, .initialized=true,
        .enabled=true, .ready=true, .capabilities_known=true, .independent_usb=true,
        .usb_configured=true};
    for (unsigned i=0; i<WEB_CLIENTS; i++) owner.midi_channel[i]=1;
    web_control_store(&owner);
    service_call("capabilities", "/api/v2/capabilities", NULL, 200, "host-owner-initial");
    service_call("session-get-rejected", "/api/v2/session", NULL, 404, "null-means-get");
    service_call("state-post-rejected", "/api/v2/state", "", 404, "empty-means-post");
    service_call("session", "/api/v2/session", "", 201, "empty-means-post");
    assert(publication_count == 1);
    unsigned client=published[0].web.client; uint32_t token=published[0].web.token;
    owner.token[client]=token; web_control_store(&owner);
    service_state("state-initial-client-ks", "host-owner-session-adopted");

    service_command("clear-received", token, 1, FM_HTTP_OPCODE_KEYS_UPDATE, 0, 100, 0, 202);
    service_adopt(&owner, client, token, 1, false);
    service_state("clear-adopted", "host-owner-adopted");
    service_command("select-sub-received", token, 2, FM_HTTP_OPCODE_ENGINE_SELECT, 0, 0, 0, 202);
    service_state("select-sub-pending", "host-owner-not-yet-adopted");
    owner.midi_channel[client]=0; service_adopt(&owner, client, token, 2, true);
    service_state("select-sub-adopted", "host-owner-adopted");
    service_command("keys-on-received", token, 3, FM_HTTP_OPCODE_KEYS_UPDATE, 1, 100, 0, 202);
    service_state("keys-on-pending", "host-owner-not-yet-adopted");
    owner.keys[client]=1; service_adopt(&owner, client, token, 3, false);
    service_state("keys-on-adopted", "host-owner-adopted");
    service_command("keys-off-received", token, 4, FM_HTTP_OPCODE_KEYS_UPDATE, 0, 100, 0, 202);
    owner.keys[client]=0; service_adopt(&owner, client, token, 4, false);
    service_state("keys-off-adopted", "host-owner-adopted");
    service_command("cc7-received", token, 5, FM_HTTP_OPCODE_CC, 7, 96, 0, 202);
    service_state("cc7-pending", "host-owner-not-yet-adopted");
    owner.patch.volume=192; service_adopt(&owner, client, token, 5, true);
    service_state("cc7-adopted", "host-owner-shared-patch-adopted");
    service_command("select-ks-received", token, 6, FM_HTTP_OPCODE_ENGINE_SELECT, 1, 0, 0, 202);
    service_state("select-ks-pending", "host-owner-not-yet-adopted");
    owner.midi_channel[client]=1; service_adopt(&owner, client, token, 6, true);
    service_state("select-ks-adopted", "host-owner-adopted");
    service_command("ks-decay-received", token, 7, FM_HTTP_OPCODE_KS_DECAY, 4200, 0, 0, 202);
    owner.ks_decay_ms=4200; service_adopt(&owner, client, token, 7, true);
    service_state("ks-decay-adopted", "host-owner-global-ks-decay-adopted");
    service_command("select-fm-received", token, 8, FM_HTTP_OPCODE_ENGINE_SELECT, 2, 0, 0, 202);
    service_state("select-fm-pending", "host-owner-not-yet-adopted");
    owner.midi_channel[client]=2; service_adopt(&owner, client, token, 8, true);
    service_state("select-fm-adopted", "host-owner-adopted-native-fm-still-unavailable");
    service_command("fm-native-parameter-rejected", token, 9, FM_HTTP_OPCODE_FM_PARAMETER,
        18, UINT32_C(0xffff0000), 31, 503);

    owner.cpu_mode.ready=false; owner.cpu_mode.busy=true; web_control_store(&owner);
    service_state("state-cpu-not-ready", "host-owner-ready-false-busy-true");
    service_command("cpu-not-ready-select-received", token, 9, FM_HTTP_OPCODE_ENGINE_SELECT, 1, 0, 0, 202);
    owner.midi_channel[client]=1; service_adopt(&owner, client, token, 9, true);
    service_state("cpu-not-ready-select-adopted", "host-owner-valid-no-fault-adoption");
    owner.cpu_mode.busy=false; owner.cpu_mode.fault=true; owner.mode_recovering=true;
    web_control_store(&owner);
    service_state("state-cpu-fault", "host-owner-fault");
    service_command("cpu-fault-select-rejected", token, 10, FM_HTTP_OPCODE_ENGINE_SELECT, 0, 0, 0, 503);
    owner.cpu_mode.fault=false; owner.cpu_mode.valid=false; web_control_store(&owner);
    service_state("state-cpu-invalid", "host-owner-invalid");
    service_command("cpu-invalid-select-rejected", token, 10, FM_HTTP_OPCODE_ENGINE_SELECT, 0, 0, 0, 503);
    owner.cpu_mode.valid=true; owner.cpu_mode.ready=true; owner.mode_recovering=false;
    owner.sub_patch_valid=false; web_control_store(&owner);
    service_state("state-sub-patch-invalid", "host-owner-sub-patch-invalid");
    service_command("cc7-sub-patch-invalid-rejected", token, 10, FM_HTTP_OPCODE_CC, 7, 96, 0, 503);
    owner.sub_patch_valid=true; owner.ready=false; web_control_store(&owner);
    service_state("state-owner-unavailable", "host-owner-unavailable");
    service_command("owner-unavailable-keys-rejected", token, 10, FM_HTTP_OPCODE_KEYS_UPDATE, 1, 100, 0, 503);
    owner.ready=true; owner.inputs.ready=false; owner.inputs.stop_pending=true;
    owner.inputs.stop_requested=4; owner.inputs.stop_completed=3; web_control_store(&owner);
    service_state("state-input-stop-pending", "host-owner-input-stop-pending");
    owner.inputs.ready=true; owner.inputs.stop_pending=false; owner.inputs.stop_completed=4;
    web_control_store(&owner);
    service_state("state-ready-recovered", "host-owner-ready-recovered");
    service_command("final-clear-received", token, 10, FM_HTTP_OPCODE_KEYS_UPDATE, 0, 100, 0, 202);
    service_adopt(&owner, client, token, 10, false);
    service_state("final-clear-adopted", "host-owner-adopted");
    assert(owner.requested_engine==0 && owner.applied_engine==0 &&
        owner.cpu_mode.desired==0 && owner.cpu_mode.applied==0);
    assert(!fclose(service_trace));
    puts("AE350 service capture PASS: actual prepare/emit, explicit HOST OWNER MODEL, global SUB0 retained; no FPGA/audio claim");
    return 0;
}
