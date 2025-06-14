#if !defined(WINDOWS)
#include <cstdio>
#include <atomic>
#include <cassert>
#include <malloc.h>
#include "concurrent_unordered_map.h"

#define MAX_TRACE 64
#define MEMMON_MAX_NAME_LENGTH 255
#define BUFFER_SIZE MEMMON_MAX_NAME_LENGTH
#define METAINFO_SIZE sizeof(MEMMON_METAINFO)
#define MAGIC_NUMBER 0x12345678
#define MEMMON_MAP_RESERVE_SIZE 8192

#define ALIGNMENT 8
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
// METAINFO_SIZE를 8바이트 배수로 올림한 값을 사용합니다.
#define ALIGNED_METAINFO_SIZE ALIGN_UP(METAINFO_SIZE, ALIGNMENT)

typedef struct memmon_metainfo MEMMON_METAINFO;
struct memmon_metainfo
{
  int magic_number;
  uint64_t allocated_size;
  uintptr_t caller_addr;
};

#ifdef __cplusplus
extern "C" {
#endif

extern int memmon_dump_memory_usage (void);
extern int memmon_disabled_force (void);

const char *memmon_resolve_function_name (const void *addr, char *buffer);

#ifdef __cplusplus
}
#endif

#endif // !WINDOWS
