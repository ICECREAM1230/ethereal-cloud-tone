#ifndef MENU_H
#define MENU_H
#include <stdint.h>
#define MENU_COLS 40u
#define MENU_ROWS 20u
#define MENU_CHARS (MENU_COLS * MENU_ROWS)
typedef struct { uint32_t selected, gain, cutoff, mode, heartbeat; } menu_t;
void menu_init(menu_t *m);
void menu_key(menu_t *m, unsigned key);
void menu_render(const menu_t *m, char screen[MENU_CHARS], uint32_t ack,
                 uint32_t sample, unsigned link_ok);
#endif
