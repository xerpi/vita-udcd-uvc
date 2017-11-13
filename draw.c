/*
 * Copyright (c) 2015 Sergi Granell (xerpi)
 */

#include <stdio.h>
#include <string.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/display.h>
#include "draw.h"
#include "utils.h"
#include "log.h"

extern const unsigned char msx_font[];

static int console_x = 16;
static int console_y = 16;

static SceDisplayFrameBuf fb;
static SceUID fb_uid = -1;
static int fb_initialized = 0;

int map_framebuffer()
{
	const unsigned int fb_size = ALIGN(4 * SCREEN_PITCH * SCREEN_H, 256 * 1024);
	int ret;
	void *fb_addr;

	fb_uid = ksceKernelAllocMemBlock("fb", 0x40404006 , fb_size, NULL);
	if (fb_uid < 0)
		return fb_uid;

	ret = ksceKernelGetMemBlockBase(fb_uid, &fb_addr);
	if (ret < 0)
		return ret;

	/*LOG("Framebuffer uid: 0x%08X\n", fb_uid);
	LOG("Framebuffer vaddr: 0x%08X\n", (uintptr_t)fb_addr);
	LOG("Framebuffer size: 0x%08X\n", fb_size);*/

	memset(&fb, 0, sizeof(fb));
	fb.size        = sizeof(fb);
	fb.base        = fb_addr;
	fb.pitch       = SCREEN_PITCH;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width       = SCREEN_W;
	fb.height      = SCREEN_H;

	fb_initialized = 1;

	clear_screen();

	return ksceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void unmap_framebuffer(void)
{
	if (fb_uid >= 0)
		ksceKernelFreeMemBlock(fb_uid);

	fb_initialized = 0;
}

typedef unsigned int u32;
void fill_fb(void *addr, unsigned int color)
{
	int i, j;

	if (!fb_initialized)
		return;

	for (i = 0; i < SCREEN_H; i++) {
		for (j = 0; j < SCREEN_PITCH; j++) {
			*(u32 *)((char *)addr + i * SCREEN_PITCH * 4 + j * 4) = color;
		}
	}
}

void clear_screen()
{
	if (!fb_initialized)
		return;

	memset(fb.base, 0x00, SCREEN_PITCH * SCREEN_H * 4);
	//fill_fb(fb.base, RGBA8(0x80, 0x80, 0x80, 0xFF));
	ksceKernelCpuDcacheWritebackRange(fb.base, SCREEN_PITCH * SCREEN_H * 4);
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
	if (!fb_initialized)
		return;

	uint32_t *p = &((uint32_t *)fb.base)[x + y * fb.pitch];
	*p = color;
	ksceKernelCpuDcacheWritebackRange(p, sizeof(*p));
}

void draw_rectangle(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
	if (!fb_initialized)
		return;

	int i, j;
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			draw_pixel(x + j, y + i, color);
		}
	}
}

void draw_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color)
{
	if (!fb_initialized)
		return;

	int r2 = radius * radius;
	int area = r2 << 2;
	int rr = radius << 1;

	int i;
	for (i = 0; i < area; i++) {
		int tx = (i % rr) - radius;
		int ty = (i / rr) - radius;

		if (tx * tx + ty * ty <= r2) {
			draw_pixel(x + tx, y + ty, color);
		}
	}
}

void font_draw_char(int x, int y, uint32_t color, char c)
{
	if (!fb_initialized)
		return;

	unsigned char *font = (unsigned char *)(msx_font + (c - (uint32_t)' ') * 8);
	int i, j, pos_x, pos_y;
	for (i = 0; i < 8; ++i) {
		pos_y = y + i*2;
		for (j = 0; j < 8; ++j) {
			pos_x = x + j*2;
			if ((*font & (128 >> j))) {
				draw_pixel(pos_x + 0, pos_y + 0, color);
				draw_pixel(pos_x + 1, pos_y + 0, color);
				draw_pixel(pos_x + 0, pos_y + 1, color);
				draw_pixel(pos_x + 1, pos_y + 1, color);
			}
		}
		++font;
	}
}

void font_draw_string(int x, int y, uint32_t color, const char *string)
{
	if (!fb_initialized)
		return;

	if (string == NULL) return;

	int startx = x;
	const char *s = string;

	while (*s) {
		if (*s == '\n') {
			x = startx;
			y += 16;
		} else if (*s == ' ') {
			x += 16;
		} else if(*s == '\t') {
			x += 16*4;
		} else {
			font_draw_char(x, y, color, *s);
			x += 16;
		}
		++s;
	}
}

void console_print(const char *s)
{
	if (!fb_initialized)
		return;

	if (!s)
		return;

	for (; *s; s++) {
		if (*s == '\n') {
			console_x = 16;
			console_y += 16;
			draw_rectangle(0, console_y, SCREEN_W, 16, BLACK);
		} else if (*s == ' ') {
			console_x += 16;
		} else if (*s == '\t') {
			console_x += 16 * 4;
		} else {
			font_draw_char(console_x, console_y, WHITE, *s);
			console_x += 16;
		}

		if (console_x > SCREEN_W)
			console_x = 16;

		if (console_y + 16 > SCREEN_H) {
			console_y = 16;
			draw_rectangle(0, console_y, SCREEN_W, 16, BLACK);
		}
	}
}

int console_get_y(void)
{
	return console_y;
}

void console_set_y(int y)
{
	console_y = y;
	draw_rectangle(0, console_y, SCREEN_W, 16, BLACK);
}
