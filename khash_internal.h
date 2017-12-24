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

#ifndef KHASH_INTERNAL_H
#define KHASH_INTERNAL_H

/* Hash table size MUST be a power of 2 */
#define KHASH_BCK_SIZE_16    (1 << 4)
#define KHASH_BCK_SIZE_1k    (1 << 10)
#define KHASH_BCK_SIZE_512k  (1 << 19)

struct khash_t {
	uint32_t          count;
	uint32_t          bck_num;
	uint32_t          bits_num;  /* Keep structure 4 bytes alligned */
	uint32_t          *ht_count; /* Array of bck_size length */
	struct hlist_head *ht;       /* Array of bck_size length */
};

#endif
