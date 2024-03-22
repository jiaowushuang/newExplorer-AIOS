/*
 * Copyright (c) 2008-2012 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <limits.h>
#include <stdint.h>
#include <stddef.h>


typedef int status_t;

typedef uintptr_t range_t;
typedef uintptr_t range_transform_t;
typedef uintptr_t range_base_t;
typedef uintptr_t word_t;

typedef int spin_lock_saved_state_t;
typedef int spin_lock_t;

#define POINTER_TO_UINT(x) ((uintptr_t)(x))
#define UINT_TO_POINTER(x) ((void *)(uintptr_t)(x))
#define POINTER_TO_INT(x) ((intptr_t)(x))
#define INT_TO_POINTER(x) ((void *)(intptr_t)(x))

typedef signed long int ssize_t;

static inline long fls(size_t x)
{
	return x ? sizeof(x) * 8 - builtin_clz_sizet(x) : 0;
}

#define ROUND_UP(x, n) (((x) + (n)-(1UL)) & ~((n)-(1UL)))
#define ROUND_DOWN(x, n) ((x) & ~((n)-(1UL)))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))