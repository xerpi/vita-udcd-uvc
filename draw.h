/*
 * Copyright (c) 2015 Sergi Granell (xerpi)
 */

#ifndef DRAW_H
#define DRAW_H

#include <psp2/types.h>

#define RGBA8(r, g, b, a)      ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | (((r)&0xFF)<<0))

#define SCREEN_PITCH 1024
#define SCREEN_W 960
#define SCREEN_H 544

#define RED   RGBA8(255, 0,   0,   255)
#define GREEN RGBA8(0,   255, 0,   255)
#define BLUE  RGBA8(0,   0,   255, 255)
#define CYAN  RGBA8(0,   255, 255, 255)
#define LIME  RGBA8(50,  205, 50,  255)
#define PURP  RGBA8(147, 112, 219, 255)
#define WHITE RGBA8(255, 255, 255, 255)
#define BLACK RGBA8(0,   0,   0,   255)

int map_framebuffer(void);
void unmap_framebuffer(void);
void clear_screen();
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rectangle(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color);

void font_draw_char(int x, int y, uint32_t color, char c);
void font_draw_string(int x, int y, uint32_t color, const char *string);

#define font_draw_stringf(x, y, color, s, ...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), s, ##__VA_ARGS__); \
		font_draw_string(x, y, color, buffer); \
	} while (0)

void console_print(const char *s);
int console_get_y(void);
void console_set_y(int y);

#define console_printf(s, ...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), s, ##__VA_ARGS__); \
		console_print(buffer); \
	} while (0)

#endif
