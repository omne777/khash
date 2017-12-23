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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <linux/list.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/hash.h>
#include <linux/rculist.h>

#include "khash.h"
#include "khash_internal.h"

khash_item_t *
khash_item_new(khash_key_t hash, void *value, gfp_t flags)
{
	khash_item_t *item = NULL;

	item = kzalloc(sizeof(khash_item_t), flags);
	if (!item)
		return (NULL);

	item->hash = hash;
	item->value = value;

	return (item);
}
EXPORT_SYMBOL(khash_item_new);

void
khash_item_del(khash_item_t *item)
{
	if (unlikely(!item))
		return;

	kfree(item);
}
EXPORT_SYMBOL(khash_item_del);

__always_inline static void *
khash_item_value_get(khash_item_t *item)
{
	if (unlikely(!item))
		return (NULL);

	return (item->value);
}

__always_inline static int
khash_item_key_get(khash_item_t *item, khash_key_t *key)
{
	if (unlikely(!item || !key))
		return (-1);

	*key = item->hash;

	return (0);
}

__always_inline static int
khash_item_value_set(khash_item_t *item, void *value)
{
	if (unlikely(!item))
		return (-1);

	item->value = value;

	return (0);
}

#define khash_min(val, bits)							\
	(sizeof(val) <= 4 ? hash_32(val, bits) : hash_long(val, bits))

#define khash_add_rcu(hashtable, bits_num, node, key)					\
	hlist_add_head_rcu(node, &hashtable[khash_min(key, bits_num)])

static inline void
khash_del_rcu(struct hlist_node *node)
{
	hlist_del_init_rcu(node);
}

#define khash_for_each_possible_rcu(name, bit_num, obj, member, key) \
	hlist_for_each_entry_rcu(obj, &name[khash_min(key, bit_num)], member)

#define khash_for_each_rcu(name, bkt, bkt_num, obj, member) \
	for ((bkt) = 0, obj = NULL; !obj && (bkt) < bkt_num; (bkt)++)      \
		hlist_for_each_entry_rcu(obj, &name[bkt], member)

#define KHASH_BUCKET_LOOKUP(__kh__, _hh_, __item, __f__)                     \
	do {                                                                      \
		khash_for_each_possible_rcu((__kh__)->ht, (__kh__)->bits_num, __item, hh, (_hh_)->key) {  \
			if (khash_key_match(&(__item)->hash, (_hh_))) {                   \
				__f__++;                                                      \
				break;                                                        \
			}                                                                 \
		}                                                                     \
	} while (0)

#define KHASH_FOREACH(__kh__, __idx__, __item__, __func__, __data__)        \
	do {                                                                    \
		khash_for_each_rcu((__kh__)->ht, __idx__, (__kh__)->bck_num, __item__, hh) {           \
			if (__func__((__item__)->hash, (__item__)->value, __data__))    \
				break;                                                      \
		}                                                                   \
	} while (0)


__always_inline static khash_item_t *
__khash_lookup2(khash_t *kh, khash_key_t *hash)
{
	khash_item_t *item = NULL;
	uint8_t found = 0;

	KHASH_BUCKET_LOOKUP(kh, hash, item, found);
	if (!found)
		return (NULL);

	return (item);
}

__always_inline static uint32_t
khash_hash_idx_get(khash_t *kh, khash_key_t *hash)
{
	return (khash_min(hash->key, kh->bits_num));
}

uint64_t
khash_footprint(khash_t *kh)
{
	if (unlikely(!kh))
		return (0);

	return (sizeof(*kh) +
			sizeof(*kh->ht_count) * kh->bck_num +
			sizeof(*kh->ht) * kh->bck_num);
}
EXPORT_SYMBOL(khash_footprint);

uint64_t
khash_entry_footprint(void)
{
	return (sizeof(khash_item_t));
}
EXPORT_SYMBOL(khash_entry_footprint);

khash_t *
khash_init(uint32_t bck_size)
{
	khash_t *khash = NULL;
	u32 i = 0;

	khash = vzalloc(sizeof(khash_t));
	if (unlikely(!khash))
		goto exit_failure;

	/* Round the buckets number to the nearest multiple of 2, up to 2^24 */
	khash->bits_num = 0;
	while (((1 << khash->bits_num) < bck_size) && khash->bits_num < 24) {
		khash->bits_num++;
	}
	khash->bck_num = 1 << khash->bits_num;

	khash->ht = vzalloc(sizeof(*khash->ht) * khash->bck_num);
	if (unlikely(!khash->ht))
		goto exit_failure;

	khash->ht_count = vzalloc(sizeof(*khash->ht_count) * khash->bck_num);
	if (unlikely(!khash->ht_count))
		goto exit_failure;

	for (i = 0; i < khash->bck_num; i++)
		INIT_HLIST_HEAD(&khash->ht[i]);

	return (khash);

exit_failure:

	khash_term(khash);

	return (NULL);
}
EXPORT_SYMBOL(khash_init);

__always_inline void
__khash_rementry(khash_t *khash, khash_item_t *item)
{
	khash->ht_count[khash_hash_idx_get(khash, &item->hash)]--;
	khash->count--;

	khash_del_rcu(&item->hh);

	kfree_rcu(item, rcu);
}

void
khash_flush(khash_t *kh)
{
	khash_item_t *item = NULL;
	int idx;

	if (!kh)
		return;

	khash_for_each_rcu(kh->ht, idx, kh->bck_num, item, hh)
		__khash_rementry(kh, item);
}
EXPORT_SYMBOL(khash_flush);

void
khash_term(khash_t *kh)
{
	if (unlikely(!kh))
		return;

	khash_flush(kh);

	if (kh->ht)
		vfree(kh->ht);

	if (kh->ht_count)
		vfree(kh->ht_count);

	vfree(kh);
}
EXPORT_SYMBOL(khash_term);

int
khash_rementry(khash_t *khash, khash_key_t hash, void **retval)
{
	khash_item_t *item = NULL;
	void *value = NULL;

	if (!khash)
		goto khash_rementry_fail;

	item = __khash_lookup2(khash, &hash);
	if (!item)
		goto khash_rementry_fail;

	value = item->value;

	__khash_rementry(khash, item);

	if (retval)
		*retval = value;
	return (0);

khash_rementry_fail:
	if (retval)
		*retval = NULL;
	return (-1);
}
EXPORT_SYMBOL(khash_rementry);

int
khash_add_item(khash_t *khash, khash_item_t *item)
{
	khash_item_t *old_item = NULL;

	if (!khash || !item)
		return (-1);

	old_item = __khash_lookup2(khash, &item->hash);
	if (old_item)
		return (-1);

	khash_add_rcu(khash->ht, khash->bits_num, &item->hh, item->hash.key);

	khash->ht_count[khash_hash_idx_get(khash, &item->hash)]++;
	khash->count++;

	return (0);
}
EXPORT_SYMBOL(khash_add_item);

int
khash_addentry(khash_t *khash, khash_key_t hash, void *value, gfp_t flags)
{
	khash_item_t *item = NULL;

	if (unlikely(!khash))
		return (-1);

	item = khash_item_new(hash, value, flags);
	if (unlikely(!item))
		return (-1);

	if (khash_add_item(khash, item) < 0) {
		kfree(item);
		return (-1);
	}

	return (0);
}
EXPORT_SYMBOL(khash_addentry);

int
khash_lookup(khash_t *khash, khash_key_t hash, void **retval)
{
	khash_item_t *item = NULL;

	if (unlikely(!khash))
		goto khash_lookup_fail;

	item = __khash_lookup2(khash, &hash);
	if (!item)
		goto khash_lookup_fail;

	if (retval)
		*retval = (rcu_dereference(item))->value;

	return (0);

khash_lookup_fail:
	if (retval)
		*retval = NULL;

	return (-1);
}
EXPORT_SYMBOL(khash_lookup);

int
khash_lookup2(khash_t *khash, khash_key_t *hash, void **retval)
{
	khash_item_t *item = NULL;

	if (unlikely(!khash))
		goto khash_lookup_fail;

	item = __khash_lookup2(khash, hash);
	if (!item)
		goto khash_lookup_fail;

	if (retval)
		*retval = (rcu_dereference(item))->value;

	return (0);

khash_lookup_fail:
	if (retval)
		*retval = NULL;

	return (-1);
}
EXPORT_SYMBOL(khash_lookup2);

int
khash_size(khash_t *khash)
{
	if (unlikely(!khash))
		return (-1);

	return (khash->count);
}
EXPORT_SYMBOL(khash_size);

__always_inline u32
khash_bck_size_get(khash_t *kh)
{
	return (kh->bck_num);
}

__always_inline struct hlist_head *
khash_bck_get(khash_t *kh, uint32_t idx)
{
	return (&kh->ht[idx]);
}

void
khash_foreach(khash_t *khash, khfunc func, void *data)
{
	khash_item_t *item = NULL;
	int idx;

	if (unlikely(!khash || !func))
		return;

	KHASH_FOREACH(khash, idx, item, func, data);
}
EXPORT_SYMBOL(khash_foreach);

__always_inline static uint64_t
sqrt_u64(uint64_t a)
{
	uint64_t max = ((uint64_t) 1) << 32;
	uint64_t min = 0;
	uint64_t sqt = 0;
	uint64_t sq = 0;

	while (1) {
		if (max <= (1 + min))
			return min;

		sqt = min + (max - min) / 2;
		sq = sqt * sqt;

		if (sq == a)
			return sqt;

		if (sq > a)
			max = sqt;
		else
			min = sqt;
	}

	return (0);
}

__always_inline static int64_t
sqrt_s64(int64_t a)
{
	return ((int64_t)sqrt_u64(a));
}

int
khash_stats_get(khash_t *khash, khash_stats_t *stats)
{
	uint64_t i, variance = 0, tmp;

	if (unlikely(!khash || !stats || !khash->bck_num))
		return (-1);

	memset(stats, 0, sizeof(khash_stats_t));
	stats->min = 1000000;

	stats->count = khash->count;

	for (i = 0; i < khash->bck_num; i++) {
		tmp = khash->ht_count[i];
		if (tmp < stats->min) {
			stats->min = tmp;
			stats->min_counter = 1;
		} else if (tmp == stats->min) {
			stats->min_counter++;
		}

		if (tmp > stats->max) {
			stats->max = tmp;
			stats->max_counter = 1;
		} else if (tmp == stats->max) {
			stats->max_counter++;
		}

		stats->statistical_mode[tmp]++;

		tmp *= PRECISION;
		stats->mean += tmp;
		variance += tmp * tmp;
	}

	stats->mean /= khash->bck_num;
	variance = (variance / khash->bck_num) - (stats->mean * stats->mean);
	stats->std_dev = sqrt_u64(variance);

	stats->mean /= PRECISION;
	stats->std_dev /= PRECISION;

	tmp = (MAX_STATISTICAL_MODE < stats->max)?MAX_STATISTICAL_MODE:stats->max;
	for(i = 0; i < tmp; i++) {
		if (stats->statistical_mode[i] > stats->stat_mode_counter) {
			stats->stat_mode = i;
			stats->stat_mode_counter = stats->statistical_mode[i];
		}
	}

	for (i = 0; i < MAX_STATISTICAL_MODE; i++)
		stats->x_axis[i] = i;

	stats->bucket_number = khash->bck_num;

	return (0);
}
EXPORT_SYMBOL(khash_stats_get);

static int
foreach_test(khash_key_t hash, void *value, void *user_data)
{
	printk("[KHASH] Key %llu %llu %llu\n",
			hash.__key._64[0], hash.__key._64[1], hash.__key._64[2]);

	return (0);
}

static int
khash_self_test(uint32_t bck_size)
{
	static khash_t *k = NULL;
	khash_key_t key = {};
	u64 p0[2] = {177, 277};
	u64 p1 = 77;
	u32 p2 = 7;
	void *res = NULL;

	printk("[KHASH] Start self test\n");

	k = khash_init(bck_size);
	if (!k) {
		printk(KERN_INFO "[KHASH] ERROR - Test, hash allocation failure\n");
		goto exit_failure;
	}

	printk("[KHASH] Allocate hash table with %u buckets [using %u bits]\n",
			k->bck_num, k->bits_num);

	__khash_hash_u160(&key, p0, p2);
	if (khash_addentry(k, key, p0, GFP_ATOMIC) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u128 add failed\n");
		goto exit_failure;
	}

	__khash_hash_u128(&key, p0);
	if (khash_addentry(k, key, p0, GFP_ATOMIC) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u128 add failed\n");
		goto exit_failure;
	}

	__khash_hash_u64(&key, p1);
	if (khash_addentry(k, key, &p1, GFP_ATOMIC) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u64 add failed\n");
		goto exit_failure;
	}

	__khash_hash_u32(&key, p2);
	if (khash_addentry(k, key, &p2, GFP_ATOMIC) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u32 add failed\n");
		goto exit_failure;
	}

	printk("[KHASH] Added 4 entries, iterate and print hash keys\n");
	khash_foreach(k, foreach_test, NULL);

	__khash_hash_u160(&key, p0, p2);
	if (khash_lookup2(k, &key, &res) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u128 lookup failed\n");
		goto exit_failure;
	}

	__khash_hash_u128(&key, p0);
	if (khash_lookup2(k, &key, &res) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u128 lookup failed\n");
		goto exit_failure;
	}

	__khash_hash_u64(&key, p1);
	if (khash_lookup2(k, &key, &res) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u64 lookup failed\n");
		goto exit_failure;
	}

	__khash_hash_u32(&key, p2);
	if (khash_lookup2(k, &key, &res) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u32 lookup failed\n");
		goto exit_failure;
	}

	__khash_hash_u64(&key, p1);
	if (khash_rementry(k, key, NULL) < 0) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u64 remove failed\n");
		goto exit_failure;
	}

	__khash_hash_u64(&key, p1);
	if (!khash_lookup2(k, &key, &res)) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u64 present after remove\n");
		goto exit_failure;
	}

	printk("[KHASH] Removed 1 entry, iterate and print hash keys\n");
	khash_foreach(k, foreach_test, NULL);

	khash_flush(k);

	__khash_hash_u128(&key, p0);
	if (!khash_lookup2(k, &key, &res)) {
		printk(KERN_INFO "[KHASH] ERROR - Test, u128 present after flush\n");
		goto exit_failure;
	}

	printk("[KHASH] Flushed hash table, iterate and print hash keys\n");
	khash_foreach(k, foreach_test, NULL);

	khash_term(k);

	printk(KERN_INFO "[KHASH] Test succeed\n");

	return (0);

exit_failure:

	printk(KERN_INFO "[KHASH] ERROR - Test failed\n");

	if (k) {
		khash_flush(k);
		khash_term(k);
	}

	return (-1);
}

int
khash_init_module(void)
{
	printk(KERN_INFO "[%s] module loaded\n", KHASH_VERSION_STR);

	if (khash_self_test(15) < 0) {
		printk(KERN_INFO "[%s] module unloaded\n", KHASH_VERSION_STR);
		return (-ENOMEM);
	}

	if (khash_self_test(555000) < 0) {
		printk(KERN_INFO "[%s] module unloaded\n", KHASH_VERSION_STR);
		return (-ENOMEM);
	}

	return (0);
}

void
khash_exit_module(void)
{
	printk(KERN_INFO "[%s] module unloaded\n", KHASH_VERSION_STR);
}

module_init(khash_init_module);
module_exit(khash_exit_module);

MODULE_AUTHOR("Paolo Missiaggia <paolo.missiaggia@athonet.com>");
MODULE_DESCRIPTION("Kernel ultrafast HASH tables");
MODULE_VERSION(KHASH_VERSION_STR);

MODULE_LICENSE("GPL");
