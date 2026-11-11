#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
struct fake_queue { unsigned len, size, head, count; unsigned char *data; };
QueueHandle_t xQueueCreate(unsigned len, unsigned size) {
    QueueHandle_t q = calloc(1, sizeof(*q));
    q->len = len; q->size = size; q->data = calloc(len, size); return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait) {
    (void)wait; if (q->count == q->len) return pdFALSE;
    memcpy(q->data + ((q->head + q->count) % q->len) * q->size, item, q->size);
    q->count++; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) {
    (void)wait; if (!q->count) return pdFALSE;
    memcpy(item, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->len; q->count--; return pdTRUE;
}
