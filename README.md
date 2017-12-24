# KHASH

## Copyright

Versions up to 1.4.X, Copyright (C) 2016-2017 - Athonet s.r.l. - All Rights Reserved<br/>
Changes from version 1.4.1 to 2.0.X, Copyright (C) 2017-2018 - Paolo Missiaggia - All Rights Reserved

Authors:<br/>
Paolo Missiaggia, <paolo.missiaggia@athonet.com><br/>
Paolo Missiaggia, <paolo.ratm@gmail.com>

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as published by
the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

## About

An ultra fast hash table in kernel space

* RCU locks capable
* Keys maximum length 160 bits
* Memory footprint: 28 Bytes + buckets_number * 12 Bytes + 72 Bytes for each indexed element
* Current version does not support inflate or deflete operations
