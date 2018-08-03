#include "log.h"
#include <psp2kern/io/fcntl.h>

extern int ksceIoMkdir(const char *, int);

void log_reset()
{
	ksceIoMkdir(LOG_PATH, 6);

	SceUID fd = ksceIoOpen(LOG_FILE,
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
	if (fd < 0)
		return;

	ksceIoClose(fd);
}

void log_write(const char *buffer, size_t length)
{
	SceUID fd = ksceIoOpen(LOG_FILE,
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 6);
	if (fd < 0)
		return;

	ksceIoWrite(fd, buffer, length);
	ksceIoClose(fd);
}
