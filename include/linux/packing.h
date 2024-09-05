/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2016-2018 NXP
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _LINUX_PACKING_H
#define _LINUX_PACKING_H

#include <linux/types.h>
#include <linux/bitops.h>

#define QUIRK_MSB_ON_THE_RIGHT	BIT(0)
#define QUIRK_LITTLE_ENDIAN	BIT(1)
#define QUIRK_LSW32_IS_FIRST	BIT(2)

int pack(void *pbuf, u64 uval, size_t startbit, size_t endbit, size_t pbuflen,
	 u8 quirks);

int unpack(const void *pbuf, u64 *uval, size_t startbit, size_t endbit,
	   size_t pbuflen, u8 quirks);

struct packed_field {
	u16 startbit;
	u16 endbit;
	u16 offset;
	u16 size;
};

#define PACKED_FIELD(start, end, struct_name, struct_field) \
	(struct packed_field) { \
		.startbit = (start), \
		.endbit = (end), \
		.offset = offsetof(struct_name, struct_field), \
		.size = sizeof_field(struct_name, struct_field), \
	}

#define CHECK_PACKED_FIELD(field, pbuflen) \
	({ typeof (field) __f = (field); typeof (pbuflen) __len = (pbuflen); \
	BUILD_BUG_ON(__f.startbit < __f.endbit); \
	BUILD_BUG_ON(__f.startbit >= BITS_PER_BYTE * __len); \
	BUILD_BUG_ON(__f.startbit - __f.endbit >= BITS_PER_BYTE * __f.size); \
	BUILD_BUG_ON(__f.size != 1 && __f.size != 2 && __f.size != 4 && __f.size != 8); })

#define CHECK_PACKED_FIELD_OVERLAP(field1, field2) \
	({ typeof (field1) _f1 = (field1); typeof (field2) _f2 = (field2); \
	BUILD_BUG_ON(max(_f1.endbit, _f2.endbit) <=  min(_f1.startbit, _f2.startbit)); })

#include <generated/packing-checks.h>

void pack_fields(void *pbuf, size_t pbuflen, const void *ustruct,
		 const struct packed_field *fields, size_t num_fields,
		 u8 quirks);

void unpack_fields(const void *pbuf, size_t pbuflen, void *ustruct,
		   const struct packed_field *fields, size_t num_fields,
		   u8 quirks);

#endif
