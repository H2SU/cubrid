#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <new>
#include <cstdint>
#include <malloc.h>
#include <cassert>
#include "memmon.hpp"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

// -----------------------------
// glibc 내부 함수 포인터 초기화
extern "C" {
  void *__libc_malloc (size_t);
  void *__libc_calloc (size_t, size_t);
  void *__libc_realloc (void *, size_t);
  void __libc_free (void *);
}
static void * (*real_malloc) (size_t) = __libc_malloc;
static void * (*real_calloc) (size_t, size_t) = __libc_calloc;
static void * (*real_realloc) (void *, size_t) = __libc_realloc;
static void (*real_free) (void *) = __libc_free;

// 재진입 방지를 위한 thread_local 변수
thread_local bool malloc_in_hook = false;
thread_local bool calloc_in_hook = false;
thread_local bool realloc_in_hook = false;
thread_local bool free_in_hook = false;

// -----------------------------
// RealAllocator: TBB map 내부 할당 시 후킹 영향을 받지 않도록 함
template <typename T>
struct RealAllocator
{
  using value_type = T;
  RealAllocator() noexcept {}
  template <class U>
  RealAllocator (const RealAllocator<U> &) noexcept {}
  T *allocate (std::size_t n)
  {
    void *p = real_malloc (n * sizeof (T));
    if (!p)
      {
	throw std::bad_alloc();
      }
    return static_cast<T *> (p);
  }
  void deallocate (T *p, std::size_t) noexcept
  {
    real_free (p);
  }
};

template <class T, class U>
bool operator== (const RealAllocator<T> &, const RealAllocator<U> &)
{
  return true;
}
template <class T, class U>
bool operator!= (const RealAllocator<T> &, const RealAllocator<U> &)
{
  return false;
}

// -----------------------------
// AtomicSize 래퍼 (원자적 업데이트를 지원)
struct AtomicSize
{
  std::atomic<size_t> value;
  AtomicSize() : value (0) {}
  AtomicSize (size_t v) : value (v) {}
  AtomicSize (const AtomicSize &other) : value (other.value.load (std::memory_order_relaxed)) {}
  AtomicSize &operator= (const AtomicSize &other)
  {
    value.store (other.value.load (std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }
  AtomicSize &operator+= (size_t delta)
  {
    value.fetch_add (delta, std::memory_order_relaxed);
    return *this;
  }
  AtomicSize &operator-= (size_t delta)
  {
    value.fetch_sub (delta, std::memory_order_relaxed);
    return *this;
  }
  operator size_t() const
  {
    return value.load (std::memory_order_relaxed);
  }
};

// -----------------------------
// TBB map 타입 및 lazy 초기화 함수
using MemStatMap = tbb::concurrent_unordered_map<
		   uintptr_t,
		   AtomicSize,
		   std::hash<uintptr_t>,
		   std::equal_to<uintptr_t>,
		   RealAllocator<std::pair<const uintptr_t, AtomicSize>>
		   >;

// rehash를 한 번만 호출하도록 lambda 초기화를 사용
MemStatMap &get_mem_stat_map()
{
  static MemStatMap instance;
  static bool initialized = []()
  {
    instance.rehash (MEMMON_MAP_RESERVE_SIZE);
    return true;
  }
  ();
  (void)initialized; // 사용하지 않는 변수 경고 제거
  return instance;
}

// -----------------------------
// 메타정보 관련 함수
char *memmon_get_metainfo_pos (void *ptr, size_t size)
{
  return reinterpret_cast<char *> (ptr) + size - METAINFO_SIZE;
}

size_t memmon_get_allocated_size (void *ptr)
{
  size_t allocated_size = malloc_usable_size (ptr);
  if (allocated_size <= METAINFO_SIZE)
    {
      return allocated_size;
    }
  const MEMMON_METAINFO *metainfo = reinterpret_cast<const MEMMON_METAINFO *> (memmon_get_metainfo_pos (ptr,
				    allocated_size));
  if (metainfo->magic_number == MAGIC_NUMBER)
    {
      allocated_size = metainfo->allocated_size - METAINFO_SIZE;
    }
  return allocated_size;
}

void *memmon_get_initial_mem_call_addr (void)
{
  int trace_count = 0;
  Dl_info dl_info;
  void *return_addr[MAX_TRACE];
  void *func_addr_p = nullptr;
  char max_check_length = 255;
  size_t len = 0;

  trace_count = backtrace (return_addr, MAX_TRACE);
  for (int i = 0; i < trace_count; i++)
    {
      if (dladdr (return_addr[i], &dl_info) == 0)
	{
	  continue;
	}

      len = strnlen (dl_info.dli_fname, max_check_length);
      if (len == max_check_length)
	{
	  continue;
	}

      if (strstr (dl_info.dli_fname, "libcubrid.") == NULL)
	{
	  continue;
	}

      // printf ("dli.fname: %s, dli.fbase: %p, dli.sname: %s, dli.saddr: %p\n",
	    //   dl_info.dli_fname, dl_info.dli_fbase, dl_info.dli_sname, dl_info.dli_saddr);

      if (dl_info.dli_fbase >= reinterpret_cast<const void *> (0x40000000))
	{
	  func_addr_p = reinterpret_cast<void *> (reinterpret_cast<size_t> (reinterpret_cast<const char *>
						  (return_addr[i])) - reinterpret_cast<size_t> (dl_info.dli_fbase));
	}
      else
	{
	  func_addr_p = return_addr[i];
	}

      break;
    }

  return func_addr_p;
}

void add_stat (void *ptr, const size_t size)
{
  assert (size > 0);
  MEMMON_METAINFO *metainfo = reinterpret_cast<MEMMON_METAINFO *> (memmon_get_metainfo_pos (ptr, size));

  metainfo->magic_number = MAGIC_NUMBER;
  metainfo->allocated_size = size;
  metainfo->caller_addr = reinterpret_cast<uintptr_t> (memmon_get_initial_mem_call_addr());

  auto &map_ref = get_mem_stat_map();

  // emplace를 시도하고, 실패하면 기존 값에 누적
  auto result = map_ref.emplace (metainfo->caller_addr, AtomicSize (metainfo->allocated_size));
  if (!result.second)
    {
      result.first->second += metainfo->allocated_size;
      // printf ("[add_stat] Updated existing entry: Key 0x%016lx, New Value %zu\n",
      //   static_cast<unsigned long> (metainfo->caller_addr),
      //   static_cast<size_t> (result.first->second));
    }
}

#if 0
void sub_stat (void *ptr)
{
  if (!ptr)
    {
      return;
    }
  size_t size = malloc_usable_size (ptr);
  if (size <= METAINFO_SIZE)
    {
      return;
    }
  MEMMON_METAINFO *metainfo = reinterpret_cast<MEMMON_METAINFO *> (memmon_get_metainfo_pos (ptr, size));
  if (metainfo->magic_number != MAGIC_NUMBER)
    {
      return;
    }
  assert (metainfo->allocated_size == size);
  auto &map_ref = get_mem_stat_map();
  auto it = map_ref.find (metainfo->caller_addr);
  if (it != map_ref.end())
    {
      it->second -= metainfo->allocated_size;
    }
  metainfo->magic_number = 0;
}
#else
void sub_stat (void *ptr)
{
  if (!ptr)
    {
      return;
    }
  size_t size = malloc_usable_size (ptr);
  if (size <= METAINFO_SIZE)
    {
      return;
    }
  MEMMON_METAINFO *metainfo = reinterpret_cast<MEMMON_METAINFO *> (memmon_get_metainfo_pos (ptr, size));
  if (metainfo->magic_number != MAGIC_NUMBER)
    {
      return;
    }
  assert (metainfo->allocated_size == size);
  auto &map_ref = get_mem_stat_map();
  auto result = map_ref.find (metainfo->caller_addr);
  if (result != map_ref.end())
    {
      // printf ("[sub_stat] Before removal: Key 0x%016lx, Value: %zu\n",
      //   static_cast<unsigned long> (metainfo->caller_addr), static_cast<size_t> (it->second));
      result->second -= metainfo->allocated_size;
    }
  metainfo->magic_number = 0;
}
#endif

// -----------------------------
// 메모리 후킹 함수들
extern "C" {

  void *malloc (size_t size)
  {
    void *ptr = nullptr;

    if (malloc_in_hook)
      {
	return real_malloc (size);
      }

    malloc_in_hook = true;

    if (memmon_get_initial_mem_call_addr() != nullptr)
      {
	size_t total_size = size + METAINFO_SIZE;

	ptr = real_malloc (total_size);
	if (!ptr)
	  {
	    malloc_in_hook = false;
	    return nullptr;
	  }

	add_stat (ptr, total_size);
	// printf("\t[hooked] malloc (ptr:%p, size:%zu, total_size:%zu)\n", ptr, size, total_size);
      }
    else
      {
	ptr = real_malloc (size);
      }

    malloc_in_hook = false;
    return ptr;
  }

  void *calloc (size_t nmemb, size_t size)
  {
    void *ptr = nullptr;

    if (calloc_in_hook)
      {
	return real_calloc (nmemb, size);
      }

    calloc_in_hook = true;

    if (memmon_get_initial_mem_call_addr() != nullptr)
      {
	size_t user_size = nmemb * size;
	size_t total_size = user_size + METAINFO_SIZE;

	ptr = real_calloc (1, total_size);
	if (!ptr)
	  {
	    calloc_in_hook = false;
	    return nullptr;
	  }

	add_stat (ptr, total_size);
	// printf("\t[hooked] calloc (ptr:%p, nmemb:%zu, size:%zu, total_size:%zu)\n", ptr, nmemb, size, total_size);
      }
    else
      {
	ptr = real_calloc (nmemb, size);
      }

    calloc_in_hook = false;

    return ptr;
  }

  void *realloc (void *ptr, size_t size)
  {
    void *new_ptr = nullptr;

    if (realloc_in_hook)
      {
	return real_realloc (ptr, size);
      }

    realloc_in_hook = true;

    if (memmon_get_initial_mem_call_addr() != nullptr)
      {
	size_t total_size = size + METAINFO_SIZE;

	if (ptr == nullptr)
	  {
	    realloc_in_hook = false;
	    return malloc (size);
	  }

	if (size == 0)
	  {
	    free (ptr);
	    realloc_in_hook = false;
	    return nullptr;
	  }
	else
	  {
	    new_ptr = malloc (size);
	    if (new_ptr != nullptr)
	      {
		size_t old_size = memmon_get_allocated_size (ptr);
		size_t copy_size = (old_size < size) ? old_size : size;
		memcpy (new_ptr, ptr, copy_size);
		free (ptr);
	      }
	  }

	// printf("\t[hooked] realloc (ptr:%p, new_ptr:%p, size:%zu, total_size:%zu)\n", ptr, new_ptr, size, total_size);
      }
    else
      {
	new_ptr = real_realloc (ptr, size);
      }

    realloc_in_hook = false;
    return new_ptr;
  }

  void free (void *ptr)
  {
    if (!ptr)
      {
	return;
      }

    if (free_in_hook)
      {
	real_free (ptr);
	return;
      }

    free_in_hook = true;
    sub_stat (ptr);
    // printf("\t[hooked] free (ptr:%p, size:%zu)\n", ptr, memmon_get_allocated_size(ptr));
    real_free (ptr);
    ptr = nullptr;
    free_in_hook = false;
  }
}

// -----------------------------
// operator new / delete 후킹
// void *operator new (std::size_t size)
// {
//   void *ptr = malloc (size);
//   if (!ptr)
//     {
//       throw std::bad_alloc();
//     }
//   return ptr;
// }

// void *operator new[] (std::size_t size)
// {
//   void *ptr = malloc (size);
//   if (!ptr)
//     {
//       throw std::bad_alloc();
//     }
//   return ptr;
// }

// void operator delete (void *ptr) noexcept
// {
//   if (ptr)
//     {
//       free (ptr);
//     }
// }

// void operator delete[] (void *ptr) noexcept
// {
//   if (ptr)
//     {
//       free (ptr);
//     }
// }

// void operator delete (void *ptr, std::size_t) noexcept
// {
//   if (ptr)
//     {
//       free (ptr);
//     }
// }

// void operator delete[] (void *ptr, std::size_t) noexcept
// {
//   if (ptr)
//     {
//       free (ptr);
//     }
// }

// trim_function_name: 경로 문자열에서 마지막에서 두 번째 '/'부터 시작하는 부분을 반환하고,
//                    문자열 끝의 개행 문자('\n')도 제거한다.
// 예) "/home/heexoo/workspace/cubrid/src/transaction/log_compress.c:206\n" → "/src/transaction/log_compress.c:206"
static const char *trim_function_name (const char *path)
{
  // 1) 문자열 끝에 '\n'이 있으면 제거 (path는 수정 가능한 버퍼라고 가정)
  size_t len = strlen (path);
  if (len > 0 && path[len - 1] == '\n')
    {
      // const_cast로 const를 제거한 뒤 '\0'을 삽입
      // (path가 실제로 수정 가능한 배열이라는 전제)
      const_cast<char *> (path)[len - 1] = '\0';
    }

  // 2) 마지막 슬래시 위치 찾기
  const char *last_slash = strrchr (path, '/');
  if (!last_slash)
    {
      // 슬래시가 없다면 원본 반환
      return path;
    }

  // 3) 마지막 슬래시 앞쪽에서 다시 마지막으로 등장하는 슬래시 찾기 (두 번째 마지막 슬래시)
  const char *p = path;
  const char *second_last = nullptr;
  while ((p = strchr (p, '/')) != nullptr && p < last_slash)
    {
      second_last = p;
      p++; // 다음 위치부터 검색
    }

  // 4) 두 번째 마지막 슬래시가 있다면 그 위치부터 반환, 없으면 원본 반환
  return (second_last) ? second_last : path;
}

const char *er_resolve_function_name (const void *address, char *buffer)
{
  FILE *output = nullptr;
  char cmd_line[BUFFER_SIZE] = {0};
  const char *func_name_p = nullptr;
  const char *fixed_lib = "/home/heexoo/CUBRID/lib/libcubrid.so.11.4";

  snprintf (cmd_line, sizeof (cmd_line), "addr2line -e %s %p 2> /dev/null", fixed_lib, address);

  // unsetenv ("LD_PRELOAD"); // LD_PRELOAD 끄기

  output = popen (cmd_line, "r");
  if (output == nullptr)
    {
      return nullptr;
    }

  func_name_p = fgets (buffer, BUFFER_SIZE - 1, output);
  pclose (output);

  // printf ("\tfunc_name_p : %s\n", func_name_p);
  // printf ("\tbuffer : %s\n", buffer);
  if (!func_name_p || !func_name_p[0])
    {
      return nullptr;
    }

  // return trim_function_name (buffer);
  return buffer;
}



// -----------------------------
// 파일명을 "memory_dump_YYYYMMDD_HHMMSS.txt" 형식으로 생성하여 파일을 여는 함수
FILE *open_dump_file (void)
{
  char filename[256] = {0};
  time_t now = time (nullptr);
  struct tm tm_now;
  localtime_r (&now, &tm_now);
  strftime (filename, sizeof (filename), "memory_dump_%Y%m%d_%H%M%S.txt", &tm_now);
  FILE *fp = fopen (filename, "w");
  if (!fp)
    {
      perror ("fopen");
    }
  return fp;
}

// -----------------------------
// dump_memory_usage 함수를 C 방식으로 내보내기
#ifdef __cplusplus
extern "C" {
#endif

void dump_memory_usage (void)
{
  auto &map_ref = get_mem_stat_map();
  tbb::concurrent_unordered_map<std::string, size_t> func_usage;
  size_t total_usage = 0;

  // 각 항목마다 caller address를 함수 이름으로 해석 후 누적
  for (const auto &entry : map_ref)
    {
      char buffer[BUFFER_SIZE] = {0};
      if (reinterpret_cast<const void *> (entry.first) == NULL)
	{
	  continue;
	}
      const char *func_name = er_resolve_function_name (reinterpret_cast<const void *> (entry.first), buffer);
      if (!func_name || !func_name[0])
	{
	  continue;
	}
      // std::string으로 변환하여 key로 사용
      std::string key (func_name);
      size_t usage = entry.second; // AtomicSize가 size_t로 변환됨

      // 누적합산
      func_usage[key] += usage;
      total_usage += usage;

      // 디버깅 출력
      // printf ("[dump] Caller Address: 0x%016lx -> Func: %s, Value: %zu\n", entry.first, key.c_str(), usage);
    }

  // map의 결과를 벡터로 옮겨서 정렬 (메모리 사용량 내림차순)
  std::vector<std::pair<std::string, size_t>> stats (func_usage.begin(), func_usage.end());
  std::sort (stats.begin(), stats.end(), [] (const std::pair<std::string, size_t> &a,
	     const std::pair<std::string, size_t> &b)
  {
    return a.second > b.second;
  });

  FILE *outfile_fp = open_dump_file();
  if (!outfile_fp)
    {
      return;
    }


  // 3) 헤더 출력
  fprintf (outfile_fp, "==================== Memory Usage Dump ====================\n");
  fprintf (outfile_fp, "Total Memory Usage: %lu Bytes (%d MB)\n\n", total_usage, (int)total_usage/1024/1024);

  // 컬럼 폭을 보기 좋게 조정 (예: 함수 이름 최대 50자로)
  fprintf (outfile_fp, "%-60s | %-15s | %s\n",
	   "Function Name", "Memory Usage", "Ratio (%)");
  fprintf (outfile_fp, "---------------------------------------------------------------------\n");

  // 4) 실제 데이터 출력
  for (const auto &stat : stats)
    {
      double ratio = (total_usage > 0)
		     ? static_cast<double> (stat.second) / total_usage * 100.0
		     : 0.0;

      // %-50s: 왼쪽 정렬, 50칸 확보
      // %-15zu: 왼쪽 정렬, 15칸 확보
      // %7.2f%%: 소수점 둘째 자리까지, 총 7칸 폭
      fprintf (outfile_fp, "%-60s | %-15zu | %7.2f%%\n",
	       stat.first.c_str(), stat.second, ratio);
    }

  fprintf (outfile_fp, "=====================================================================\n");
  fflush (outfile_fp);
  fclose (outfile_fp);
  printf ("Memory usage dump written to file.\n");
}


#ifdef __cplusplus
}
#endif