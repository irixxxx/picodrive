#include "libmemorymapping.h"

#include <coreinit/dynload.h>
#include <stdbool.h>
#include <stdint.h>

// Aroma libmappedmemory - Maschell promises he won't break ABI here
static void* (**MEMAllocFromMappedMemoryEx)(uint32_t size, int32_t alignment);
static void* (**MEMFreeToMappedMemory)(void *ptr);
static OSDynLoad_Module libmappedmemory;

static bool wiiu_init_libmappedmemory() {
  OSDynLoad_Error err;

  // already ran?
  if (MEMAllocFromMappedMemoryEx)
    return true;

  err = OSDynLoad_Acquire("homebrew_memorymapping", &libmappedmemory);
  if (err != OS_DYNLOAD_OK)
    return false;

  err = OSDynLoad_FindExport(libmappedmemory, OS_DYNLOAD_EXPORT_DATA, "MEMAllocFromMappedMemoryEx", (void**)&MEMAllocFromMappedMemoryEx);
  if (err != OS_DYNLOAD_OK)
    return false;

  err = OSDynLoad_FindExport(libmappedmemory, OS_DYNLOAD_EXPORT_DATA, "MEMFreeToMappedMemory", (void**)&MEMFreeToMappedMemory);
  if (err != OS_DYNLOAD_OK)
    return false;

  return true;
}

void *wiiu_alloc_mappedmemory(uint32_t size, int32_t alignment) {
  if (!wiiu_init_libmappedmemory()) return NULL;

  return (*MEMAllocFromMappedMemoryEx)(size, alignment);
}

void wiiu_free_mappedmemory(void *mem) {
  if (!wiiu_init_libmappedmemory()) return;

  (*MEMFreeToMappedMemory)(mem);
}

void wiiu_deinit_mappedmemory(void) {
  OSDynLoad_Release(libmappedmemory);
  MEMAllocFromMappedMemoryEx = NULL;
  MEMFreeToMappedMemory = NULL;
}