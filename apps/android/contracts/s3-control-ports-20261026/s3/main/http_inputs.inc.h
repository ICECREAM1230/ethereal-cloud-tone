// Private to control_api.c. Both API versions serialize the same owner snapshot;
// HTTP handlers never probe SPI or infer readiness from a received request.
static int format_inputs(char *body, size_t capacity, const web_input_state_t *s)
{
    return snprintf(body,capacity,
        ",\"inputs\":{\"version\":%" PRIu32 ",\"available\":%s,\"initialized\":%s,"
        "\"enabled\":%s,\"ready\":%s,\"fault\":%" PRIu32 ",\"capabilities_known\":%s,"
        "\"independent_usb\":%s,\"usb_configured\":%s,\"usb_error\":%s,"
        "\"stop_requested\":%" PRIu32 ",\"stop_completed\":%" PRIu32 ","
        "\"stop_fault\":%" PRIu32 ",\"stop_pending\":%s}}",
        s->version,s->available?"true":"false",s->initialized?"true":"false",
        s->enabled?"true":"false",s->ready?"true":"false",s->fault,
        s->capabilities_known?"true":"false",s->independent_usb?"true":"false",
        s->usb_configured?"true":"false",s->usb_error?"true":"false",
        s->stop_requested,s->stop_completed,s->stop_fault,s->stop_pending?"true":"false");
}
