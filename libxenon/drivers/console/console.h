#ifndef __console_console_h
#define __console_console_h

#include <xetypes.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t console_color[2];
extern uint32_t console_oldbg, console_oldfg;
    
#define CONSOLE_COLOR_RED 0x0000FF00
#define CONSOLE_COLOR_BLUE 0xD8444E00
#define CONSOLE_COLOR_GREEN 0x00800000
#define CONSOLE_COLOR_BLACK 0x00000000
#define CONSOLE_COLOR_WHITE 0xFFFFFF00
#define CONSOLE_COLOR_GREY 0xC0C0C000
#define CONSOLE_COLOR_BROWN 0x00339900
#define CONSOLE_COLOR_PURPLE 0xFF009900
#define CONSOLE_COLOR_YELLOW 0x00FFFF00
#define CONSOLE_COLOR_ORANGE 0x0066FF00
#define CONSOLE_COLOR_PINK 0xFF66FF00

#define CONSOLE_SUCCESS CONSOLE_COLOR_GREEN
#define CONSOLE_WARN CONSOLE_COLOR_YELLOW
#define CONSOLE_ERR CONSOLE_COLOR_ORANGE

#define PRINT_COL(bg, fg, s, ...) { \
        console_oldbg = console_color[0]; console_oldfg = console_color[1]; \
        console_set_colors(bg,fg); \
        printf(s, ##__VA_ARGS__); \
        console_set_colors(console_oldbg,console_oldfg); }

#define PRINT_SUCCESS(s, ...) PRINT_COL(console_color[0],CONSOLE_SUCCESS, s, ##__VA_ARGS__)
#define PRINT_WARN(s, ...) PRINT_COL(console_color[0],CONSOLE_WARN, s, ##__VA_ARGS__)
#define PRINT_ERR(s, ...) PRINT_COL(console_color[0],CONSOLE_ERR, s, ##__VA_ARGS__)

void console_set_colors(unsigned int background, unsigned int foreground); // can be called before init
void console_get_dimensions(unsigned int * width,unsigned int * height);
void console_putch(const char c);
void console_clrscr();
void console_clrline();
void console_clear_to_eol(int start_pos);

void console_init(void);
void console_open(void);
void console_close(void);
void console_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b);
void console_pset_right(int x, int y, unsigned char r, unsigned char g, unsigned char b);

int console_get_cursor_x(void);
int console_get_cursor_y(void);

#ifdef __cplusplus
};
#endif

#endif
