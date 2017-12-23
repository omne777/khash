/*
 * KHASH
 * An ultra fast hash table in kernel space based on hashtable.h
 * Copyright (C) 2016-2017 - Athonet s.r.l. - All Rights Reserved
 *
 * Authors:
 *         Paolo Missiaggia, <paolo.Missiaggia@athonet.com>
 *         Paolo Missiaggia, <paolo.ratm@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef KHASH_H
#define KHASH_H

#define KHASH_NAME  "KHASH"
#define KHASH_MAJOR 2
#define KHASH_MINOR 0
#define KHASH_PATCH 0
#define KHASH_STR_H(x) #x
#define KHASH_STR(x) KHASH_STR_H(x)
#define KHASH_VERSION(a, b, c) (((a) << 24) + ((b) << 16) + (c))
#define KHASH_VERSION_STR \
		KHASH_NAME ": " KHASH_STR(KHASH_MAJOR) "." \
		KHASH_STR(KHASH_MINOR) "." KHASH_STR(KHASH_PATCH)

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#include "hashtable.h"
#else
#include <linux/hashtable.h>
#endif

#include <linux/jhash.h>

#define KHASH_GOLDEN_RATIO_32 0x61C88647
#define KHASH_GOLDEN_RATIO_64 0x61C8864680B583EBull

#ifndef HAVE_ARCH__HASH_32
#define __khash_32 __khash_32_generic
#endif
static inline u32
__khash_32_generic(u32 val)
{
	return val * KHASH_GOLDEN_RATIO_32;
}

#ifndef HAVE_ARCH_HASH_32
#define khash_32 khash_32_generic
#endif
static inline u32
khash_32_generic(u32 val, unsigned int bits)
{
	/* High bits are more random, so use them. */
	return __khash_32(val) >> (32 - bits);
}

#ifndef HAVE_ARCH_HASH_64
#define khash_64 khash_64_generic
#endif
static __always_inline u64
khash_64_generic(u64 val)
{
#if BITS_PER_LONG == 64
	/* 64x64-bit multiply is efficient on all 64-bit processors */
	return val * KHASH_GOLDEN_RATIO_64;
#else
	/* Hash 64 bits using only 32x32-bit multiply. */
	return khash_32((u32)val ^ __khash_32(val >> 32), 0);
#endif
}

/*
 * HASH KEY manipulation API - Consolidated
 */

#define _64WORDS_NUM (3)
typedef struct {
	union {
		u8   _8[8 * _64WORDS_NUM];
		u16 _16[4 * _64WORDS_NUM];
		u32 _32[2 * _64WORDS_NUM];
		u64 _64[_64WORDS_NUM];
	} __key;
	u64 key;
} khash_key_t;

__always_inline static void
__khash_key_hash(khash_key_t *key, u8 lkey)
{
	int i;

	for (i = 0; i < lkey; i++)
		key->key ^= khash_64(key->__key._64[i]);
}

__always_inline static khash_key_t *
khash_hash_u32(khash_key_t *key, u32 _u32)
{
	memset(key, 0, sizeof(*key));

	key->__key._64[0] = _u32;
	__khash_key_hash(key, 1);

	return (key);
}

__always_inline static khash_key_t *
khash_hash_u64(khash_key_t *key, u64 _u64)
{
	memset(key, 0, sizeof(*key));

	memcpy(key->__key._64, &_u64, 8);
	__khash_key_hash(key, 1);

	return (key);
}

__always_inline static khash_key_t *
khash_hash_u128(khash_key_t *key, u64 _u64[2])
{
	memset(key, 0, sizeof(*key));

	memcpy(key->__key._64, _u64, 16);
	__khash_key_hash(key, 2);

	return (key);
}

__always_inline static khash_key_t *
khash_hash_u160(khash_key_t *key, u64 _u64[2], u32 _u32)
{
	memset(key, 0, sizeof(*key));

	memcpy(key->__key._64, _u64, 8);
	key->__key._64[2] = _u32;
	__khash_key_hash(key, 3);

	return (key);
}

/*
 * KHASH manipulation API - Consolidated
 */
typedef struct khash_t khash_t;

khash_t *khash_init(uint32_t bck_size);
void     khash_term(khash_t *khash);
void     khash_flush(khash_t *khash);

int      khash_size(khash_t *khash);
int      khash_addentry(khash_t *khash, khash_key_t hash, void *val, gfp_t gfp);
int      khash_rementry(khash_t *khash, khash_key_t hash, void **retval);
int      khash_lookup(khash_t *khash, khash_key_t *hash, void **retval);

typedef  int(*khfunc)(khash_key_t hash, void *value, void *user_data);
void     khash_foreach(khash_t *khash, khfunc func, void *data);

typedef struct {
	loff_t p;
	void *value;
} khash_proc_iter_t;

__always_inline static int
khash_proc_interator(khash_key_t hash, void *value, void *user_data)
{
	khash_proc_iter_t *iter = (khash_proc_iter_t *)user_data;

	if (iter->p--) {
		iter->value = NULL;
		return (0);
	}

	iter->value = value;
	return (1);
}

/*
 * Stats API - Experimental!!! Don't relay on them, they could be removed.
 */
#define PRECISION 1000
#define MAX_STATISTICAL_MODE 25
typedef struct {
	uint32_t count;
	uint64_t mean;
	uint64_t std_dev;
	uint64_t min;
	uint64_t min_counter;
	uint64_t max;
	uint64_t max_counter;
	uint64_t stat_mode;
	uint64_t stat_mode_counter;
	uint32_t bucket_number;
	uint32_t x_axis[MAX_STATISTICAL_MODE];
	uint32_t statistical_mode[MAX_STATISTICAL_MODE];
} khash_stats_t;

int khash_stats_get(khash_t *khash, khash_stats_t *stats);
uint64_t khash_footprint(khash_t *kh);
uint64_t khash_entry_footprint(void);

#endif
