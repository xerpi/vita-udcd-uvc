/*
 * Copyright (c) 2015 Sergi Granell (xerpi)
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <psp2kern/types.h>

int console_init();
int console_fini();
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
