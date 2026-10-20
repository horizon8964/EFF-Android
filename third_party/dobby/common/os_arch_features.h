#pragma once

#include <sys/types.h>
#include <stddef.h>
#include "pac_kit.h"

#include "PlatformUnifiedInterface/platform.h"
#if defined(ANDROID)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace features {

template <typename T> inline T arm_thumb_fix_addr(T &addr) {
#if defined(__arm__) || defined(__aarch64__)
  addr = (T)((uintptr_t)addr & ~1);
#endif
  return addr;
}

namespace apple {
template <typename T> inline T arm64e_pac_strip(T &addr) {
  return pac_strip(addr);
}

template <typename T> inline T arm64e_pac_sign(T &addr) {
  return pac_sign(addr);
}

template <typename T> inline T arm64e_pac_strip_and_sign(T &addr) {
  return pac_strip_and_sign(addr);
}
} // namespace apple

namespace android {
inline void make_memory_readable(void *address, size_t size) {
#if defined(ANDROID)
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0 || size == 0) return;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  const uintptr_t page = start - start % static_cast<size_t>(page_size);
  const size_t extent = (start - page + size + page_size - 1) / page_size * page_size;
  mprotect(reinterpret_cast<void*>(page), extent, PROT_READ | PROT_EXEC);
#endif
}
} // namespace android
} // namespace features
