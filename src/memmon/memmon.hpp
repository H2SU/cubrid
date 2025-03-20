/*
 * memory_monitor_sr.hpp - Declaration of APIs and structures, classes
 *                         for memory monitoring module
 */

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
 
 extern char *memmon_get_metainfo_pos(void *ptr, size_t size);
 extern size_t memmon_get_allocated_size(void *ptr);
 extern void *memmon_get_initial_mem_call_addr(void);
 extern void add_stat(void *ptr, const size_t size);
 extern void sub_stat(void *ptr);
 extern void dump_memory_usage(void);
 extern FILE *open_dump_file(void);
 extern const char *er_resolve_function_name (const void *address, char *buffer);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif // !WINDOWS
 