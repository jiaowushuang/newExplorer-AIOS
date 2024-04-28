/*
 * Copyright (c) 2008-2012 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef int status_t;

typedef uintptr_t range_t;
typedef uintptr_t range_transform_t;
typedef uintptr_t range_base_t;
typedef uintptr_t word_t;
typedef intptr_t sword_t;
typedef int spin_lock_saved_state_t;
typedef int spin_lock_t;

#define POINTER_TO_UINT(x) ((uintptr_t)(x))
#define UINT_TO_POINTER(x) ((void *)(uintptr_t)(x))
#define POINTER_TO_INT(x) ((intptr_t)(x))
#define INT_TO_POINTER(x) ((void *)(intptr_t)(x))

typedef signed long int ssize_t;

#define BITS_PER_LONG 64
/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word) {
  int num = 0;

#if BITS_PER_LONG == 64
  if ((word & 0xffffffff) == 0) {
    num += 32;
    word >>= 32;
  }
#endif
  if ((word & 0xffff) == 0) {
    num += 16;
    word >>= 16;
  }
  if ((word & 0xff) == 0) {
    num += 8;
    word >>= 8;
  }
  if ((word & 0xf) == 0) {
    num += 4;
    word >>= 4;
  }
  if ((word & 0x3) == 0) {
    num += 2;
    word >>= 2;
  }
  if ((word & 0x1) == 0) num += 1;
  return num;
}

static inline unsigned long __fls(unsigned long word) {
  int num = BITS_PER_LONG - 1;

#if BITS_PER_LONG == 64
  if (!(word & (~0ul << 32))) {
    num -= 32;
    word <<= 32;
  }
#endif
  if (!(word & (~0ul << (BITS_PER_LONG - 16)))) {
    num -= 16;
    word <<= 16;
  }
  if (!(word & (~0ul << (BITS_PER_LONG - 8)))) {
    num -= 8;
    word <<= 8;
  }
  if (!(word & (~0ul << (BITS_PER_LONG - 4)))) {
    num -= 4;
    word <<= 4;
  }
  if (!(word & (~0ul << (BITS_PER_LONG - 2)))) {
    num -= 2;
    word <<= 2;
  }
  if (!(word & (~0ul << (BITS_PER_LONG - 1)))) num -= 1;
  return num;
}

static inline long fls(unsigned long x) {
  return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}

#if BITS_PER_LONG == 32
static inline int fls64(unsigned long x) {
  __u32 h = x >> 32;

  if (h) return fls(h) + 32;
  return fls(x);
}
#elif BITS_PER_LONG == 64
static inline int fls64(unsigned long x) {
  if (x == 0) return 0;
  return __fls(x) + 1;
}
#else
#error BITS_PER_LONG not 32 or 64
#endif

#define ffz(x) __ffs(~(x))

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

/*
 * Include this here because some architectures need generic_ffs/fls64 in
 * scope
 */

static inline void set_bit(int nr, unsigned long *addr) {
  addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}

static inline void clear_bit(int nr, unsigned long *addr) {
  addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline bool is_set_bit(int nr, unsigned long *addr) {
	return addr[nr / BITS_PER_LONG] & (1UL << (nr % BITS_PER_LONG));
}


static inline unsigned long find_first_bit(const unsigned long *addr,
                                           unsigned long size) {
  unsigned long idx;

  for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
    if (addr[idx]) return MIN(idx * BITS_PER_LONG + __ffs(addr[idx]), size);
  }

  return size;
}

static inline unsigned long _find_next_bit(const unsigned long *addr,
                                           unsigned long nbits,
                                           unsigned long start,
                                           unsigned long invert) {
  unsigned long tmp;

  if (!nbits || start >= nbits) return nbits;

  tmp = addr[start / BITS_PER_LONG] ^ invert;

  /* Handle 1st word. */
  tmp &= BITMAP_FIRST_WORD_MASK(start);
  start = ROUND_DOWN(start, BITS_PER_LONG);

  while (!tmp) {
    start += BITS_PER_LONG;
    if (start >= nbits) return nbits;

    tmp = addr[start / BITS_PER_LONG] ^ invert;
  }

  return MIN(start + __ffs(tmp), nbits);
}

static inline unsigned long find_first_zero_bit(const unsigned long *addr,
                                                unsigned long size) {
  unsigned long idx;

  for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
    if (addr[idx] != ~0UL)
      return MIN(idx * BITS_PER_LONG + ffz(addr[idx]), size);
  }

  return size;
}

static inline unsigned long find_next_bit(const unsigned long *addr,
                                          unsigned long size,
                                          unsigned long offset) {
  return _find_next_bit(addr, size, offset, 0UL);
}

static inline unsigned long find_next_zero_bit(const unsigned long *addr,
                                               unsigned long size,
                                               unsigned long offset) {
  return _find_next_bit(addr, size, offset, ~0UL);
}

#define for_each_set_bit(bit, addr, size)                      \
  for ((bit) = find_first_bit((addr), (size)); (bit) < (size); \
       (bit) = find_next_bit((addr), (size), (bit) + 1))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size)                       \
  for ((bit) = find_next_bit((addr), (size), (bit)); (bit) < (size); \
       (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size)                         \
  for ((bit) = find_first_zero_bit((addr), (size)); (bit) < (size); \
       (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size)                          \
  for ((bit) = find_next_zero_bit((addr), (size), (bit)); (bit) < (size); \
       (bit) = find_next_zero_bit((addr), (size), (bit) + 1))
#define ROUND_UP(x, n) (((x) + (n) - (1UL)) & ~((n) - (1UL)))
#define ROUND_DOWN(x, n) ((x) & ~((n) - (1UL)))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))