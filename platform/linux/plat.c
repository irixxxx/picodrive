/*
 * PicoDrive
 * (C) notaz, 2006-2010
 * (C) irixxxx, 2019-2024
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 *
 * Platform functions that don't depend on the SDL frontend in emu.c.
 */

#include <unistd.h>
#ifndef __MINGW32__
#include <sys/mman.h> // MAP_JIT
#endif
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#endif

#include "../libpicofe/plat.h"

#include <pico/pico_types.h>
#include <pico/pico.h>

void plat_wait_till_us(unsigned int us_to)
{
	unsigned int now;

	now = plat_get_ticks_us();

	while ((signed int)(us_to - now) > 512)
	{
		usleep(1024);
		now = plat_get_ticks_us();
	}
}

void *plat_mem_get_for_drc(size_t size)
{
#ifdef MAP_JIT
	// newer versions of OSX, IOS or TvOS need this
	return plat_mmap(0, size, 1, 0);
#else
	return NULL;
#endif
}

void plat_update_volume(int has_changed, int is_up)
{
}

void plat_debug_cat(char *str)
{
}

#if defined(__APPLE__) && defined(__aarch64__)

/* Apple silicon gives a page either write or execute permission, and only a
 * MAP_JIT mapping can be both at different times.  The recompilers write a
 * block into tcache and then run it from there, so the buffer has to be
 * switched before each use.  MAP_JIT pages start out write-protected. */
int plat_drc_jit_set_exec(void *ptr, size_t size)
{
	(void)ptr;
	(void)size;
	/* the mapping is executable as soon as it's write-protected */
	return 0;
}

void plat_drc_jit_protect(int writable)
{
	pthread_jit_write_protect_np(!writable);
}

#else

int plat_drc_jit_set_exec(void *ptr, size_t size)
{
	return plat_mem_set_exec(ptr, size);
}

void plat_drc_jit_protect(int writable)
{
	(void)writable;
}

#endif
