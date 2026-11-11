// Two simple selectors. HTTP never touches the SPI peripheral.
static esp_err_t audio_routes_get(httpd_req_t *req)
{
    engine_audio_routes_state_t state;engine_link_audio_routes_state(&state);
    char body[360];
    int n=snprintf(body,sizeof(body),
        "{\"version\":1,\"available\":%s,\"valid\":%s,\"ready\":%s,\"busy\":%s,\"pending\":%s,\"error\":%s,\"pt8211\":%" PRIu32 ",\"pcm_a\":%" PRIu32 ",\"pcm_b\":3,\"sequence\":%" PRIu32 ",\"generation\":%" PRIu32 "}",
        state.available?"true":"false",state.valid?"true":"false",state.ready?"true":"false",
        state.busy?"true":"false",state.pending?"true":"false",state.error?"true":"false",
        state.pt8211,state.pcm_a,state.sequence,state.generation);
    if(n<0 || (size_t)n>=sizeof(body))return reply(req,"500 Internal Server Error","{\"error\":\"state_too_large\"}");
    return reply(req,"200 OK",body);
}
static esp_err_t audio_routes_post(httpd_req_t *req)
{
    if(!same_origin_command(req))return reply(req,"403 Forbidden","{\"error\":\"header_required\"}");
    if(req->content_len!=3)return reply(req,"400 Bad Request","{\"error\":\"invalid_audio_routes\"}");
    char body[3];size_t received=0;
    while(received<sizeof(body)) {
        int n=httpd_req_recv(req,body+received,sizeof(body)-received);
        if(n<=0)return ESP_FAIL;
        received+=(size_t)n;
    }
    if(body[0]<'0' || body[0]>'3' || body[1]!=' ' || body[2]<'0' || body[2]>'3')
        return reply(req,"400 Bad Request","{\"error\":\"invalid_audio_routes\"}");
    if(!engine_link_audio_routes_request((uint32_t)(body[0]-'0'),(uint32_t)(body[2]-'0')))
        return reply(req,"409 Conflict","{\"error\":\"audio_routes_not_ready\"}");
    return reply(req,"202 Accepted","{\"stage\":\"received\",\"confirmation\":\"pending_audio_frame\"}");
}
