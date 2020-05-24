/*
 * Copyright (c) 2015 Sergi Granell (xerpi)
 */

#include <stdio.h>
#include <string.h>
#include <psp2kern/kernel/threadmgr.h>
#include "draw.h"
#include "console.h"

static int console_x = 16;
static int console_y = 16;
static int64_t mutex[8];

int console_init()
{
	return ksceKernelInitializeFastMutex(&mutex, "console_mutex", 0, 0);
}

int console_fini()
{
	return ksceKernelDeleteFastMutex(&mutex);
}

void console_print(const char *s)
{
	if (!s)
		return;

	ksceKernelLockFastMutex(&mutex);

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

	ksceKernelUnlockFastMutex(&mutex);
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
