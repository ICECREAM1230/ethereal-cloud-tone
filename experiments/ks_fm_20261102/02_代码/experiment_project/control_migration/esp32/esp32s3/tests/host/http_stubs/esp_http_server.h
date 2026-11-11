#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Only the synchronous HTTP SDK boundary is simulated. The handlers and parser
// under test are the production wifi_web.c and web_control.c.
typedef struct {
    size_t content_len, received, chunk, response_len, send_limit;
    const char *control_header;
    const char *accept_encoding;
    const char *query;
    const unsigned char *input;
    bool receive_fails;
    char status[64], type[80], cache[32], csp[256], nosniff[32], encoding[16], vary[32];
    char response[256 * 1024]; // Host capture for the 167,896-byte inline product page.
} httpd_req_t;
typedef void *httpd_handle_t;
typedef struct {
    unsigned task_priority, stack_size, max_open_sockets;
    bool lru_purge_enable;
    unsigned recv_wait_timeout, send_wait_timeout;
    unsigned max_uri_handlers;
} httpd_config_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *req);
} httpd_uri_t;
#define HTTPD_RESP_USE_STRLEN (-1)
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
esp_err_t httpd_resp_send_chunk(httpd_req_t *, const char *, int);
size_t httpd_req_get_url_query_len(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *, char *, size_t);
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_stop(httpd_handle_t);
