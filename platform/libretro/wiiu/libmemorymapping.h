#ifndef LIBMEMORYMAPPING_H
#define LIBMEMORYMAPPING_H

#include <stdint.h>

void *wiiu_alloc_mappedmemory(uint32_t size, int32_t alignment);
void wiiu_free_mappedmemory(void *mem);
void wiiu_deinit_mappedmemory(void);

#endif // LIBMEMORYMAPPING_H
