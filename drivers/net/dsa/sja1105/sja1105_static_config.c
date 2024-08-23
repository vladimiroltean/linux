// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2016-2018 NXP
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105_static_config.h"
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

/* Convenience wrappers over the generic packing functions. These take into
 * account the SJA1105 memory layout quirks and provide some level of
 * programmer protection against incorrect API use. The errors are not expected
 * to occur durring runtime, therefore printing and swallowing them here is
 * appropriate instead of clutterring up higher-level code.
 */
void sja1105_pack(void *buf, u64 val, int start, int end, size_t len)
{
	int rc = pack(buf, val, start, end, len, QUIRK_LSW32_IS_FIRST);

	if (likely(!rc))
		return;

	if (rc == -EINVAL) {
		pr_err("Start bit (%d) expected to be larger than end (%d)\n",
		       start, end);
	} else if (rc == -ERANGE) {
		pr_err("Field %d-%d too large for 64 bits!\n",
		       start, end);
	}
	dump_stack();
}

void sja1105_unpack(const void *buf, u64 *val, int start, int end, size_t len)
{
	int rc = unpack(buf, val, start, end, len, QUIRK_LSW32_IS_FIRST);

	if (likely(!rc))
		return;

	if (rc == -EINVAL)
		pr_err("Start bit (%d) expected to be larger than end (%d)\n",
		       start, end);
	else if (rc == -ERANGE)
		pr_err("Field %d-%d too large for 64 bits!\n",
		       start, end);
	dump_stack();
}

void sja1105_packing(void *buf, u64 *val, int start, int end,
		     size_t len, enum packing_op op)
{
	if (op == PACK)
		sja1105_pack(buf, *val, start, end, len);
	else
		sja1105_unpack(buf, val, start, end, len);
}

/* Little-endian Ethernet CRC32 of data packed as big-endian u32 words */
u32 sja1105_crc32(const void *buf, size_t len)
{
	unsigned int i;
	u64 word;
	u32 crc;

	/* seed */
	crc = ~0;
	for (i = 0; i < len; i += 4) {
		sja1105_unpack(buf + i, &word, 31, 0, 4);
		crc = crc32_le(crc, (u8 *)&word, 4);
	}
	return ~crc;
}

static const struct packed_field sja1105et_avb_params_entry_fields[] = {
	PACKED_FIELD(95, 48, struct sja1105_avb_params_entry, destmeta),
	PACKED_FIELD(47, 0, struct sja1105_avb_params_entry, srcmeta),
};

static void sja1105et_avb_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_2(sja1105et_avb_params_entry_fields,
			      SJA1105ET_SIZE_AVB_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105ET_SIZE_AVB_PARAMS_ENTRY, entry_ptr,
			    sja1105et_avb_params_entry_fields);
}

static void sja1105et_avb_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105ET_SIZE_AVB_PARAMS_ENTRY, entry_ptr,
			      sja1105et_avb_params_entry_fields);
}

static const struct packed_field sja1105pqrs_avb_params_entry_fields[] = {
	PACKED_FIELD(126, 126, struct sja1105_avb_params_entry, cas_master),
	PACKED_FIELD(125, 78, struct sja1105_avb_params_entry, destmeta),
	PACKED_FIELD(77, 30, struct sja1105_avb_params_entry, srcmeta),
};

void sja1105pqrs_avb_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_3(sja1105pqrs_avb_params_entry_fields,
			      SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY, entry_ptr,
			    sja1105pqrs_avb_params_entry_fields);
}

void sja1105pqrs_avb_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY, entry_ptr,
			      sja1105pqrs_avb_params_entry_fields);
}

static const struct packed_field sja1105et_general_params_entry_fields[] = {
	PACKED_FIELD(319, 319, struct sja1105_general_params_entry, vllupformat),
	PACKED_FIELD(318, 318, struct sja1105_general_params_entry, mirr_ptacu),
	PACKED_FIELD(317, 315, struct sja1105_general_params_entry, switchid),
	PACKED_FIELD(314, 312, struct sja1105_general_params_entry, hostprio),
	PACKED_FIELD(311, 264, struct sja1105_general_params_entry, mac_fltres1),
	PACKED_FIELD(263, 216, struct sja1105_general_params_entry, mac_fltres0),
	PACKED_FIELD(215, 168, struct sja1105_general_params_entry, mac_flt1),
	PACKED_FIELD(167, 120, struct sja1105_general_params_entry, mac_flt0),
	PACKED_FIELD(119, 119, struct sja1105_general_params_entry, incl_srcpt1),
	PACKED_FIELD(118, 118, struct sja1105_general_params_entry, incl_srcpt0),
	PACKED_FIELD(117, 117, struct sja1105_general_params_entry, send_meta1),
	PACKED_FIELD(116, 116, struct sja1105_general_params_entry, send_meta0),
	PACKED_FIELD(115, 113, struct sja1105_general_params_entry, casc_port),
	PACKED_FIELD(112, 110, struct sja1105_general_params_entry, host_port),
	PACKED_FIELD(109, 107, struct sja1105_general_params_entry, mirr_port),
	PACKED_FIELD(106, 75, struct sja1105_general_params_entry, vlmarker),
	PACKED_FIELD(74, 43, struct sja1105_general_params_entry, vlmask),
	PACKED_FIELD(42, 27, struct sja1105_general_params_entry, tpid),
	PACKED_FIELD(26, 26, struct sja1105_general_params_entry, ignore2stf),
	PACKED_FIELD(25, 10, struct sja1105_general_params_entry, tpid2),
};

static void sja1105et_general_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_20(sja1105et_general_params_entry_fields,
			       SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			    sja1105et_general_params_entry_fields);
}

static void sja1105et_general_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			      sja1105et_general_params_entry_fields);
}

/* TPID and TPID2 are intentionally reversed so that semantic
 * compatibility with E/T is kept.
 */
static const struct packed_field sja1105pqrs_general_params_entry_fields[] = {
	PACKED_FIELD(351, 351, struct sja1105_general_params_entry, vllupformat),
	PACKED_FIELD(350, 350, struct sja1105_general_params_entry, mirr_ptacu),
	PACKED_FIELD(349, 347, struct sja1105_general_params_entry, switchid),
	PACKED_FIELD(346, 344, struct sja1105_general_params_entry, hostprio),
	PACKED_FIELD(343, 296, struct sja1105_general_params_entry, mac_fltres1),
	PACKED_FIELD(295, 248, struct sja1105_general_params_entry, mac_fltres0),
	PACKED_FIELD(247, 200, struct sja1105_general_params_entry, mac_flt1),
	PACKED_FIELD(199, 152, struct sja1105_general_params_entry, mac_flt0),
	PACKED_FIELD(151, 151, struct sja1105_general_params_entry, incl_srcpt1),
	PACKED_FIELD(150, 150, struct sja1105_general_params_entry, incl_srcpt0),
	PACKED_FIELD(149, 149, struct sja1105_general_params_entry, send_meta1),
	PACKED_FIELD(148, 148, struct sja1105_general_params_entry, send_meta0),
	PACKED_FIELD(147, 145, struct sja1105_general_params_entry, casc_port),
	PACKED_FIELD(144, 142, struct sja1105_general_params_entry, host_port),
	PACKED_FIELD(141, 139, struct sja1105_general_params_entry, mirr_port),
	PACKED_FIELD(138, 107, struct sja1105_general_params_entry, vlmarker),
	PACKED_FIELD(106, 75, struct sja1105_general_params_entry, vlmask),
	PACKED_FIELD(74, 59, struct sja1105_general_params_entry, tpid2),
	PACKED_FIELD(58, 58, struct sja1105_general_params_entry, ignore2stf),
	PACKED_FIELD(57, 42, struct sja1105_general_params_entry, tpid),
	PACKED_FIELD(41, 41, struct sja1105_general_params_entry, queue_ts),
	PACKED_FIELD(40, 29, struct sja1105_general_params_entry, egrmirrvid),
	PACKED_FIELD(28, 26, struct sja1105_general_params_entry, egrmirrpcp),
	PACKED_FIELD(25, 25, struct sja1105_general_params_entry, egrmirrdei),
	PACKED_FIELD(24, 22, struct sja1105_general_params_entry, replay_port),
};

void sja1105pqrs_general_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_25(sja1105pqrs_general_params_entry_fields,
			       SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			    sja1105pqrs_general_params_entry_fields);
}

void sja1105pqrs_general_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			      sja1105pqrs_general_params_entry_fields);
}

static const struct packed_field sja1110_general_params_entry_fields[] = {
	PACKED_FIELD(447, 447, struct sja1105_general_params_entry, vllupformat),
	PACKED_FIELD(446, 446, struct sja1105_general_params_entry, mirr_ptacu),
	PACKED_FIELD(445, 442, struct sja1105_general_params_entry, switchid),
	PACKED_FIELD(441, 439, struct sja1105_general_params_entry, hostprio),
	PACKED_FIELD(438, 391, struct sja1105_general_params_entry, mac_fltres1),
	PACKED_FIELD(390, 343, struct sja1105_general_params_entry, mac_fltres0),
	PACKED_FIELD(342, 295, struct sja1105_general_params_entry, mac_flt1),
	PACKED_FIELD(294, 247, struct sja1105_general_params_entry, mac_flt0),
	PACKED_FIELD(246, 246, struct sja1105_general_params_entry, incl_srcpt1),
	PACKED_FIELD(245, 245, struct sja1105_general_params_entry, incl_srcpt0),
	PACKED_FIELD(244, 244, struct sja1105_general_params_entry, send_meta1),
	PACKED_FIELD(243, 243, struct sja1105_general_params_entry, send_meta0),
	PACKED_FIELD(242, 232, struct sja1105_general_params_entry, casc_port),
	PACKED_FIELD(231, 228, struct sja1105_general_params_entry, host_port),
	PACKED_FIELD(227, 224, struct sja1105_general_params_entry, mirr_port),
	PACKED_FIELD(223, 192, struct sja1105_general_params_entry, vlmarker),
	PACKED_FIELD(191, 160, struct sja1105_general_params_entry, vlmask),
	PACKED_FIELD(159, 144, struct sja1105_general_params_entry, tpid2),
	PACKED_FIELD(143, 143, struct sja1105_general_params_entry, ignore2stf),
	PACKED_FIELD(142, 127, struct sja1105_general_params_entry, tpid),
	PACKED_FIELD(126, 126, struct sja1105_general_params_entry, queue_ts),
	PACKED_FIELD(125, 114, struct sja1105_general_params_entry, egrmirrvid),
	PACKED_FIELD(113, 111, struct sja1105_general_params_entry, egrmirrpcp),
	PACKED_FIELD(110, 110, struct sja1105_general_params_entry, egrmirrdei),
	PACKED_FIELD(109, 106, struct sja1105_general_params_entry, replay_port),
	PACKED_FIELD(70, 67, struct sja1105_general_params_entry, tdmaconfigidx),
	PACKED_FIELD(64, 49, struct sja1105_general_params_entry, header_type),
	PACKED_FIELD(16, 16, struct sja1105_general_params_entry, tte_en),
};

void sja1110_general_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_28(sja1110_general_params_entry_fields,
			       SJA1110_SIZE_GENERAL_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			    sja1110_general_params_entry_fields);
}

void sja1110_general_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_GENERAL_PARAMS_ENTRY, entry_ptr,
			      sja1110_general_params_entry_fields);
}

static const struct packed_field sja1105_l2_forwarding_params_entry_fields[] = {
	PACKED_FIELD(95, 93, struct sja1105_l2_forwarding_params_entry, max_dynp),
	PACKED_FIELD(92, 83, struct sja1105_l2_forwarding_params_entry, part_spc[7]),
	PACKED_FIELD(82, 73, struct sja1105_l2_forwarding_params_entry, part_spc[6]),
	PACKED_FIELD(72, 63, struct sja1105_l2_forwarding_params_entry, part_spc[5]),
	PACKED_FIELD(62, 53, struct sja1105_l2_forwarding_params_entry, part_spc[4]),
	PACKED_FIELD(52, 43, struct sja1105_l2_forwarding_params_entry, part_spc[3]),
	PACKED_FIELD(42, 33, struct sja1105_l2_forwarding_params_entry, part_spc[2]),
	PACKED_FIELD(32, 23, struct sja1105_l2_forwarding_params_entry, part_spc[1]),
	PACKED_FIELD(22, 13, struct sja1105_l2_forwarding_params_entry, part_spc[0]),
};

static void
sja1105_l2_forwarding_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_9(sja1105_l2_forwarding_params_entry_fields,
			      SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
			    entry_ptr, sja1105_l2_forwarding_params_entry_fields);
}

static void
sja1105_l2_forwarding_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
			      entry_ptr, sja1105_l2_forwarding_params_entry_fields);
}

static const struct packed_field sja1110_l2_forwarding_params_entry_fields[] = {
	PACKED_FIELD(95, 93, struct sja1105_l2_forwarding_params_entry, max_dynp),
	PACKED_FIELD(92, 82, struct sja1105_l2_forwarding_params_entry, part_spc[7]),
	PACKED_FIELD(81, 71, struct sja1105_l2_forwarding_params_entry, part_spc[6]),
	PACKED_FIELD(70, 60, struct sja1105_l2_forwarding_params_entry, part_spc[5]),
	PACKED_FIELD(59, 49, struct sja1105_l2_forwarding_params_entry, part_spc[4]),
	PACKED_FIELD(48, 38, struct sja1105_l2_forwarding_params_entry, part_spc[3]),
	PACKED_FIELD(37, 27, struct sja1105_l2_forwarding_params_entry, part_spc[2]),
	PACKED_FIELD(26, 16, struct sja1105_l2_forwarding_params_entry, part_spc[1]),
	PACKED_FIELD(15, 5, struct sja1105_l2_forwarding_params_entry, part_spc[0]),
};

void sja1110_l2_forwarding_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_9(sja1110_l2_forwarding_params_entry_fields,
			      SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
			    entry_ptr, sja1110_l2_forwarding_params_entry_fields);
}

void sja1110_l2_forwarding_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
			      entry_ptr, sja1110_l2_forwarding_params_entry_fields);
}

static const struct packed_field sja1105_l2_forwarding_entry_fields[] = {
	PACKED_FIELD(63, 59, struct sja1105_l2_forwarding_entry, bc_domain),
	PACKED_FIELD(58, 54, struct sja1105_l2_forwarding_entry, reach_port),
	PACKED_FIELD(53, 49, struct sja1105_l2_forwarding_entry, fl_domain),
	PACKED_FIELD(48, 46, struct sja1105_l2_forwarding_entry, vlan_pmap[7]),
	PACKED_FIELD(45, 43, struct sja1105_l2_forwarding_entry, vlan_pmap[6]),
	PACKED_FIELD(42, 40, struct sja1105_l2_forwarding_entry, vlan_pmap[5]),
	PACKED_FIELD(39, 37, struct sja1105_l2_forwarding_entry, vlan_pmap[4]),
	PACKED_FIELD(36, 34, struct sja1105_l2_forwarding_entry, vlan_pmap[3]),
	PACKED_FIELD(33, 31, struct sja1105_l2_forwarding_entry, vlan_pmap[2]),
	PACKED_FIELD(30, 28, struct sja1105_l2_forwarding_entry, vlan_pmap[1]),
	PACKED_FIELD(27, 25, struct sja1105_l2_forwarding_entry, vlan_pmap[0]),
};

void sja1105_l2_forwarding_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_11(sja1105_l2_forwarding_entry_fields,
			       SJA1105_SIZE_L2_FORWARDING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
			    entry_ptr, sja1105_l2_forwarding_entry_fields);
}

void sja1105_l2_forwarding_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
			      entry_ptr, sja1105_l2_forwarding_entry_fields);
}

static const struct packed_field sja1110_l2_forwarding_entry_type1_fields[] = {
	PACKED_FIELD(63, 61, struct sja1105_l2_forwarding_entry, vlan_pmap[10]),
	PACKED_FIELD(60, 58, struct sja1105_l2_forwarding_entry, vlan_pmap[9]),
	PACKED_FIELD(57, 55, struct sja1105_l2_forwarding_entry, vlan_pmap[8]),
	PACKED_FIELD(54, 52, struct sja1105_l2_forwarding_entry, vlan_pmap[7]),
	PACKED_FIELD(51, 49, struct sja1105_l2_forwarding_entry, vlan_pmap[6]),
	PACKED_FIELD(48, 46, struct sja1105_l2_forwarding_entry, vlan_pmap[5]),
	PACKED_FIELD(45, 43, struct sja1105_l2_forwarding_entry, vlan_pmap[4]),
	PACKED_FIELD(42, 40, struct sja1105_l2_forwarding_entry, vlan_pmap[3]),
	PACKED_FIELD(39, 37, struct sja1105_l2_forwarding_entry, vlan_pmap[2]),
	PACKED_FIELD(36, 34, struct sja1105_l2_forwarding_entry, vlan_pmap[1]),
	PACKED_FIELD(33, 31, struct sja1105_l2_forwarding_entry, vlan_pmap[0]),
};

static const struct packed_field sja1110_l2_forwarding_entry_type2_fields[] = {
	PACKED_FIELD(63, 53, struct sja1105_l2_forwarding_entry, bc_domain),
	PACKED_FIELD(52, 42, struct sja1105_l2_forwarding_entry, reach_port),
	PACKED_FIELD(41, 31, struct sja1105_l2_forwarding_entry, fl_domain),
};

void sja1110_l2_forwarding_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_l2_forwarding_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_11(sja1110_l2_forwarding_entry_type1_fields,
			       SJA1105_SIZE_L2_FORWARDING_ENTRY);
	CHECK_PACKED_FIELDS_3(sja1110_l2_forwarding_entry_type2_fields,
			      SJA1105_SIZE_L2_FORWARDING_ENTRY);

	if (entry->type_egrpcp2outputq) {
		sja1105_pack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
				    entry_ptr, sja1110_l2_forwarding_entry_type1_fields);
	} else {
		sja1105_pack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
				    entry_ptr, sja1110_l2_forwarding_entry_type2_fields);
	}
}

void sja1110_l2_forwarding_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_l2_forwarding_entry *entry = entry_ptr;

	if (entry->type_egrpcp2outputq) {
		sja1105_unpack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
				      entry_ptr, sja1110_l2_forwarding_entry_type1_fields);
	} else {
		sja1105_unpack_fields(buf, SJA1105_SIZE_L2_FORWARDING_ENTRY,
				      entry_ptr, sja1110_l2_forwarding_entry_type2_fields);
	}
}

static const struct packed_field sja1105et_l2_lookup_params_entry_fields[] = {
	PACKED_FIELD(31, 17, struct sja1105_l2_lookup_params_entry, maxage),
	PACKED_FIELD(16, 14, struct sja1105_l2_lookup_params_entry, dyn_tbsz),
	PACKED_FIELD(13, 6, struct sja1105_l2_lookup_params_entry, poly),
	PACKED_FIELD(5, 5, struct sja1105_l2_lookup_params_entry, shared_learn),
	PACKED_FIELD(4, 4, struct sja1105_l2_lookup_params_entry, no_enf_hostprt),
	PACKED_FIELD(3, 3, struct sja1105_l2_lookup_params_entry, no_mgmt_learn),
};

static void sja1105et_l2_lookup_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_6(sja1105et_l2_lookup_params_entry_fields,
			      SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			    entry_ptr, sja1105et_l2_lookup_params_entry_fields);
}

static void sja1105et_l2_lookup_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			      entry_ptr, sja1105et_l2_lookup_params_entry_fields);
}

static const struct packed_field sja1105pqrs_l2_lookup_params_entry_fields[] = {
	PACKED_FIELD(112, 102, struct sja1105_l2_lookup_params_entry, maxaddrp[4]),
	PACKED_FIELD(101, 91, struct sja1105_l2_lookup_params_entry, maxaddrp[3]),
	PACKED_FIELD(90, 80, struct sja1105_l2_lookup_params_entry, maxaddrp[2]),
	PACKED_FIELD(79, 69, struct sja1105_l2_lookup_params_entry, maxaddrp[1]),
	PACKED_FIELD(68, 58, struct sja1105_l2_lookup_params_entry, maxaddrp[0]),
	PACKED_FIELD(57, 43, struct sja1105_l2_lookup_params_entry, maxage),
	PACKED_FIELD(42, 33, struct sja1105_l2_lookup_params_entry, start_dynspc),
	PACKED_FIELD(32, 28, struct sja1105_l2_lookup_params_entry, drpnolearn),
	PACKED_FIELD(27, 27, struct sja1105_l2_lookup_params_entry, shared_learn),
	PACKED_FIELD(26, 26, struct sja1105_l2_lookup_params_entry, no_enf_hostprt),
	PACKED_FIELD(25, 25, struct sja1105_l2_lookup_params_entry, no_mgmt_learn),
	PACKED_FIELD(24, 24, struct sja1105_l2_lookup_params_entry, use_static),
	PACKED_FIELD(23, 23, struct sja1105_l2_lookup_params_entry, owr_dyn),
	PACKED_FIELD(22, 22, struct sja1105_l2_lookup_params_entry, learn_once),
};

void sja1105pqrs_l2_lookup_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_14(sja1105pqrs_l2_lookup_params_entry_fields,
			       SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			    entry_ptr, sja1105pqrs_l2_lookup_params_entry_fields);
}

void sja1105pqrs_l2_lookup_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			      entry_ptr, sja1105pqrs_l2_lookup_params_entry_fields);
}

static const struct packed_field sja1110_l2_lookup_params_entry_fields[] = {
	PACKED_FIELD(190, 180, struct sja1105_l2_lookup_params_entry, maxaddrp[10]),
	PACKED_FIELD(179, 169, struct sja1105_l2_lookup_params_entry, maxaddrp[9]),
	PACKED_FIELD(168, 158, struct sja1105_l2_lookup_params_entry, maxaddrp[8]),
	PACKED_FIELD(157, 147, struct sja1105_l2_lookup_params_entry, maxaddrp[7]),
	PACKED_FIELD(146, 136, struct sja1105_l2_lookup_params_entry, maxaddrp[6]),
	PACKED_FIELD(135, 125, struct sja1105_l2_lookup_params_entry, maxaddrp[5]),
	PACKED_FIELD(124, 114, struct sja1105_l2_lookup_params_entry, maxaddrp[4]),
	PACKED_FIELD(113, 103, struct sja1105_l2_lookup_params_entry, maxaddrp[3]),
	PACKED_FIELD(102, 92, struct sja1105_l2_lookup_params_entry, maxaddrp[2]),
	PACKED_FIELD(91, 81, struct sja1105_l2_lookup_params_entry, maxaddrp[1]),
	PACKED_FIELD(80, 70, struct sja1105_l2_lookup_params_entry, maxaddrp[0]),
	PACKED_FIELD(69, 55, struct sja1105_l2_lookup_params_entry, maxage),
	PACKED_FIELD(54, 45, struct sja1105_l2_lookup_params_entry, start_dynspc),
	PACKED_FIELD(44, 34, struct sja1105_l2_lookup_params_entry, drpnolearn),
	PACKED_FIELD(33, 33, struct sja1105_l2_lookup_params_entry, shared_learn),
	PACKED_FIELD(32, 32, struct sja1105_l2_lookup_params_entry, no_enf_hostprt),
	PACKED_FIELD(31, 31, struct sja1105_l2_lookup_params_entry, no_mgmt_learn),
	PACKED_FIELD(30, 30, struct sja1105_l2_lookup_params_entry, use_static),
	PACKED_FIELD(29, 29, struct sja1105_l2_lookup_params_entry, owr_dyn),
	PACKED_FIELD(28, 28, struct sja1105_l2_lookup_params_entry, learn_once),
};

void sja1110_l2_lookup_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_20(sja1110_l2_lookup_params_entry_fields,
			       SJA1110_SIZE_L2_LOOKUP_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			    entry_ptr, sja1110_l2_lookup_params_entry_fields);
}

void sja1110_l2_lookup_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_L2_LOOKUP_PARAMS_ENTRY,
			      entry_ptr, sja1110_l2_lookup_params_entry_fields);
}

static const struct packed_field sja1105et_l2_lookup_entry_fields[] = {
	PACKED_FIELD(95, 84, struct sja1105_l2_lookup_entry, vlanid),
	PACKED_FIELD(83, 36, struct sja1105_l2_lookup_entry, macaddr),
	PACKED_FIELD(35, 31, struct sja1105_l2_lookup_entry, destports),
	PACKED_FIELD(30, 30, struct sja1105_l2_lookup_entry, enfport),
	PACKED_FIELD(29, 20, struct sja1105_l2_lookup_entry, index),
};

void sja1105et_l2_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_5(sja1105et_l2_lookup_entry_fields,
			      SJA1105ET_SIZE_L2_LOOKUP_ENTRY);

	sja1105_pack_fields(buf, SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
			    entry_ptr, sja1105et_l2_lookup_entry_fields);
}

void sja1105et_l2_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
			      entry_ptr, sja1105et_l2_lookup_entry_fields);
}

static const struct packed_field sja1105pqrs_l2_lookup_entry_type1_fields[] = {
	PACKED_FIELD(159, 159, struct sja1105_l2_lookup_entry, tsreg),
	PACKED_FIELD(158, 147, struct sja1105_l2_lookup_entry, mirrvlan),
	PACKED_FIELD(146, 146, struct sja1105_l2_lookup_entry, takets),
	PACKED_FIELD(145, 145, struct sja1105_l2_lookup_entry, mirr),
	PACKED_FIELD(144, 144, struct sja1105_l2_lookup_entry, retag),
	PACKED_FIELD(143, 143, struct sja1105_l2_lookup_entry, mask_iotag),
	PACKED_FIELD(142, 131, struct sja1105_l2_lookup_entry, mask_vlanid),
	PACKED_FIELD(130, 83, struct sja1105_l2_lookup_entry, mask_macaddr),
	PACKED_FIELD(82, 82, struct sja1105_l2_lookup_entry, iotag),
	PACKED_FIELD(81, 70, struct sja1105_l2_lookup_entry, vlanid),
	PACKED_FIELD(69, 22, struct sja1105_l2_lookup_entry, macaddr),
	PACKED_FIELD(21, 17, struct sja1105_l2_lookup_entry, destports),
	PACKED_FIELD(16, 16, struct sja1105_l2_lookup_entry, enfport),
	PACKED_FIELD(15, 6, struct sja1105_l2_lookup_entry, index),
};

static const struct packed_field sja1105pqrs_l2_lookup_entry_type2_fields[] = {
	PACKED_FIELD(159, 159, struct sja1105_l2_lookup_entry, touched),
	PACKED_FIELD(158, 144, struct sja1105_l2_lookup_entry, age),
	PACKED_FIELD(143, 143, struct sja1105_l2_lookup_entry, mask_iotag),
	PACKED_FIELD(142, 131, struct sja1105_l2_lookup_entry, mask_vlanid),
	PACKED_FIELD(130, 83, struct sja1105_l2_lookup_entry, mask_macaddr),
	PACKED_FIELD(82, 82, struct sja1105_l2_lookup_entry, iotag),
	PACKED_FIELD(81, 70, struct sja1105_l2_lookup_entry, vlanid),
	PACKED_FIELD(69, 22, struct sja1105_l2_lookup_entry, macaddr),
	PACKED_FIELD(21, 17, struct sja1105_l2_lookup_entry, destports),
	PACKED_FIELD(16, 16, struct sja1105_l2_lookup_entry, enfport),
	PACKED_FIELD(15, 6, struct sja1105_l2_lookup_entry, index),
};

void sja1105pqrs_l2_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_l2_lookup_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_14(sja1105pqrs_l2_lookup_entry_type1_fields,
			       SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY);
	CHECK_PACKED_FIELDS_11(sja1105pqrs_l2_lookup_entry_type2_fields,
			       SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY);

	if (entry->lockeds) {
		sja1105_pack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
				    entry_ptr, sja1105pqrs_l2_lookup_entry_type1_fields);
	} else {
		sja1105_pack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
				    entry_ptr, sja1105pqrs_l2_lookup_entry_type2_fields);
	}
}

void sja1105pqrs_l2_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_l2_lookup_entry *entry = entry_ptr;

	if (entry->lockeds) {
		sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
				      entry_ptr, sja1105pqrs_l2_lookup_entry_type1_fields);
	} else {
		sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
				      entry_ptr, sja1105pqrs_l2_lookup_entry_type2_fields);
	}
}

static const struct packed_field sja1110_l2_lookup_entry_type1_fields[] = {
	PACKED_FIELD(168, 168, struct sja1105_l2_lookup_entry, trap),
	PACKED_FIELD(167, 156, struct sja1105_l2_lookup_entry, mirrvlan),
	PACKED_FIELD(155, 155, struct sja1105_l2_lookup_entry, takets),
	PACKED_FIELD(154, 154, struct sja1105_l2_lookup_entry, mirr),
	PACKED_FIELD(153, 153, struct sja1105_l2_lookup_entry, retag),
	PACKED_FIELD(152, 152, struct sja1105_l2_lookup_entry, mask_iotag),
	PACKED_FIELD(151, 140, struct sja1105_l2_lookup_entry, mask_vlanid),
	PACKED_FIELD(139, 92, struct sja1105_l2_lookup_entry, mask_macaddr),
	PACKED_FIELD(91, 88, struct sja1105_l2_lookup_entry, mask_srcport),
	PACKED_FIELD(87, 87, struct sja1105_l2_lookup_entry, iotag),
	PACKED_FIELD(86, 75, struct sja1105_l2_lookup_entry, vlanid),
	PACKED_FIELD(74, 27, struct sja1105_l2_lookup_entry, macaddr),
	PACKED_FIELD(26, 23, struct sja1105_l2_lookup_entry, srcport),
	PACKED_FIELD(22, 12, struct sja1105_l2_lookup_entry, destports),
	PACKED_FIELD(11, 11, struct sja1105_l2_lookup_entry, enfport),
	PACKED_FIELD(10, 1, struct sja1105_l2_lookup_entry, index),
};

static const struct packed_field sja1110_l2_lookup_entry_type2_fields[] = {
	PACKED_FIELD(168, 168, struct sja1105_l2_lookup_entry, touched),
	PACKED_FIELD(167, 153, struct sja1105_l2_lookup_entry, age),
	PACKED_FIELD(152, 152, struct sja1105_l2_lookup_entry, mask_iotag),
	PACKED_FIELD(151, 140, struct sja1105_l2_lookup_entry, mask_vlanid),
	PACKED_FIELD(139, 92, struct sja1105_l2_lookup_entry, mask_macaddr),
	PACKED_FIELD(91, 88, struct sja1105_l2_lookup_entry, mask_srcport),
	PACKED_FIELD(87, 87, struct sja1105_l2_lookup_entry, iotag),
	PACKED_FIELD(86, 75, struct sja1105_l2_lookup_entry, vlanid),
	PACKED_FIELD(74, 27, struct sja1105_l2_lookup_entry, macaddr),
	PACKED_FIELD(26, 23, struct sja1105_l2_lookup_entry, srcport),
	PACKED_FIELD(22, 12, struct sja1105_l2_lookup_entry, destports),
	PACKED_FIELD(11, 11, struct sja1105_l2_lookup_entry, enfport),
	PACKED_FIELD(10, 1, struct sja1105_l2_lookup_entry, index),
};

void sja1110_l2_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_l2_lookup_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_16(sja1110_l2_lookup_entry_type1_fields,
			       SJA1110_SIZE_L2_LOOKUP_ENTRY);
	CHECK_PACKED_FIELDS_13(sja1110_l2_lookup_entry_type2_fields,
			       SJA1110_SIZE_L2_LOOKUP_ENTRY);

	if (entry->lockeds) {
		sja1105_pack_fields(buf, SJA1110_SIZE_L2_LOOKUP_ENTRY,
				    entry_ptr, sja1110_l2_lookup_entry_type1_fields);
	} else {
		sja1105_pack_fields(buf, SJA1110_SIZE_L2_LOOKUP_ENTRY,
				    entry_ptr, sja1110_l2_lookup_entry_type2_fields);
	}
}

void sja1110_l2_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_l2_lookup_entry *entry = entry_ptr;

	if (entry->lockeds) {
		sja1105_unpack_fields(buf, SJA1110_SIZE_L2_LOOKUP_ENTRY,
				      entry_ptr, sja1110_l2_lookup_entry_type1_fields);
	} else {
		sja1105_unpack_fields(buf, SJA1110_SIZE_L2_LOOKUP_ENTRY,
				      entry_ptr, sja1110_l2_lookup_entry_type2_fields);
	}
}

static const struct packed_field sja1105_l2_policing_entry_fields[] = {
	PACKED_FIELD(63, 58, struct sja1105_l2_policing_entry, sharindx),
	PACKED_FIELD(57, 42, struct sja1105_l2_policing_entry, smax),
	PACKED_FIELD(41, 26, struct sja1105_l2_policing_entry, rate),
	PACKED_FIELD(25, 15, struct sja1105_l2_policing_entry, maxlen),
	PACKED_FIELD(14, 12, struct sja1105_l2_policing_entry, partition),
};

static void sja1105_l2_policing_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_5(sja1105_l2_policing_entry_fields,
			      SJA1105_SIZE_L2_POLICING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_L2_POLICING_ENTRY,
			    entry_ptr, sja1105_l2_policing_entry_fields);
}

static void sja1105_l2_policing_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_L2_POLICING_ENTRY,
			      entry_ptr, sja1105_l2_policing_entry_fields);
}

static const struct packed_field sja1110_l2_policing_entry_fields[] = {
	PACKED_FIELD(63, 57, struct sja1105_l2_policing_entry, sharindx),
	PACKED_FIELD(56, 39, struct sja1105_l2_policing_entry, smax),
	PACKED_FIELD(38, 21, struct sja1105_l2_policing_entry, rate),
	PACKED_FIELD(20, 10, struct sja1105_l2_policing_entry, maxlen),
	PACKED_FIELD(9, 7, struct sja1105_l2_policing_entry, partition),
};

void sja1110_l2_policing_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_5(sja1110_l2_policing_entry_fields,
			      SJA1105_SIZE_L2_POLICING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_L2_POLICING_ENTRY,
			    entry_ptr, sja1110_l2_policing_entry_fields);
}

void sja1110_l2_policing_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_L2_POLICING_ENTRY,
			      entry_ptr, sja1110_l2_policing_entry_fields);
}

static const struct packed_field sja1105et_mac_config_entry_fields[] = {
	PACKED_FIELD(223, 215, struct sja1105_mac_config_entry, top[7]),
	PACKED_FIELD(214, 206, struct sja1105_mac_config_entry, base[7]),
	PACKED_FIELD(205, 205, struct sja1105_mac_config_entry, enabled[7]),
	PACKED_FIELD(204, 196, struct sja1105_mac_config_entry, top[6]),
	PACKED_FIELD(195, 187, struct sja1105_mac_config_entry, base[6]),
	PACKED_FIELD(186, 186, struct sja1105_mac_config_entry, enabled[6]),
	PACKED_FIELD(185, 177, struct sja1105_mac_config_entry, top[5]),
	PACKED_FIELD(176, 168, struct sja1105_mac_config_entry, base[5]),
	PACKED_FIELD(167, 167, struct sja1105_mac_config_entry, enabled[5]),
	PACKED_FIELD(166, 158, struct sja1105_mac_config_entry, top[4]),
	PACKED_FIELD(157, 149, struct sja1105_mac_config_entry, base[4]),
	PACKED_FIELD(148, 148, struct sja1105_mac_config_entry, enabled[4]),
	PACKED_FIELD(147, 139, struct sja1105_mac_config_entry, top[3]),
	PACKED_FIELD(138, 130, struct sja1105_mac_config_entry, base[3]),
	PACKED_FIELD(129, 129, struct sja1105_mac_config_entry, enabled[3]),
	PACKED_FIELD(128, 120, struct sja1105_mac_config_entry, top[2]),
	PACKED_FIELD(119, 111, struct sja1105_mac_config_entry, base[2]),
	PACKED_FIELD(110, 110, struct sja1105_mac_config_entry, enabled[2]),
	PACKED_FIELD(109, 101, struct sja1105_mac_config_entry, top[1]),
	PACKED_FIELD(100, 92, struct sja1105_mac_config_entry, base[1]),
	PACKED_FIELD(91, 91, struct sja1105_mac_config_entry, enabled[1]),
	PACKED_FIELD(90, 82, struct sja1105_mac_config_entry, top[0]),
	PACKED_FIELD(81, 73, struct sja1105_mac_config_entry, base[0]),
	PACKED_FIELD(72, 72, struct sja1105_mac_config_entry, enabled[0]),
	PACKED_FIELD(71, 67, struct sja1105_mac_config_entry, ifg),
	PACKED_FIELD(66, 65, struct sja1105_mac_config_entry, speed),
	PACKED_FIELD(64, 49, struct sja1105_mac_config_entry, tp_delin),
	PACKED_FIELD(48, 33, struct sja1105_mac_config_entry, tp_delout),
	PACKED_FIELD(32, 25, struct sja1105_mac_config_entry, maxage),
	PACKED_FIELD(24, 22, struct sja1105_mac_config_entry, vlanprio),
	PACKED_FIELD(21, 10, struct sja1105_mac_config_entry, vlanid),
	PACKED_FIELD(9, 9, struct sja1105_mac_config_entry, ing_mirr),
	PACKED_FIELD(8, 8, struct sja1105_mac_config_entry, egr_mirr),
	PACKED_FIELD(7, 7, struct sja1105_mac_config_entry, drpnona664),
	PACKED_FIELD(6, 6, struct sja1105_mac_config_entry, drpdtag),
	PACKED_FIELD(5, 5, struct sja1105_mac_config_entry, drpuntag),
	PACKED_FIELD(4, 4, struct sja1105_mac_config_entry, retag),
	PACKED_FIELD(3, 3, struct sja1105_mac_config_entry, dyn_learn),
	PACKED_FIELD(2, 2, struct sja1105_mac_config_entry, egress),
	PACKED_FIELD(1, 1, struct sja1105_mac_config_entry, ingress),
};

static void sja1105et_mac_config_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_40(sja1105et_mac_config_entry_fields,
			       SJA1105ET_SIZE_MAC_CONFIG_ENTRY);

	sja1105_pack_fields(buf, SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
			    entry_ptr, sja1105et_mac_config_entry_fields);
}

static void sja1105et_mac_config_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
			      entry_ptr, sja1105et_mac_config_entry_fields);
}

static const struct packed_field sja1105pqrs_mac_config_entry_fields[] = {
	PACKED_FIELD(255, 247, struct sja1105_mac_config_entry, top[7]),
	PACKED_FIELD(246, 238, struct sja1105_mac_config_entry, base[7]),
	PACKED_FIELD(237, 237, struct sja1105_mac_config_entry, enabled[7]),
	PACKED_FIELD(236, 228, struct sja1105_mac_config_entry, top[6]),
	PACKED_FIELD(227, 219, struct sja1105_mac_config_entry, base[6]),
	PACKED_FIELD(218, 218, struct sja1105_mac_config_entry, enabled[6]),
	PACKED_FIELD(217, 209, struct sja1105_mac_config_entry, top[5]),
	PACKED_FIELD(208, 200, struct sja1105_mac_config_entry, base[5]),
	PACKED_FIELD(199, 199, struct sja1105_mac_config_entry, enabled[5]),
	PACKED_FIELD(198, 190, struct sja1105_mac_config_entry, top[4]),
	PACKED_FIELD(189, 181, struct sja1105_mac_config_entry, base[4]),
	PACKED_FIELD(180, 180, struct sja1105_mac_config_entry, enabled[4]),
	PACKED_FIELD(179, 171, struct sja1105_mac_config_entry, top[3]),
	PACKED_FIELD(170, 162, struct sja1105_mac_config_entry, base[3]),
	PACKED_FIELD(161, 161, struct sja1105_mac_config_entry, enabled[3]),
	PACKED_FIELD(160, 152, struct sja1105_mac_config_entry, top[2]),
	PACKED_FIELD(151, 143, struct sja1105_mac_config_entry, base[2]),
	PACKED_FIELD(142, 142, struct sja1105_mac_config_entry, enabled[2]),
	PACKED_FIELD(141, 133, struct sja1105_mac_config_entry, top[1]),
	PACKED_FIELD(132, 124, struct sja1105_mac_config_entry, base[1]),
	PACKED_FIELD(123, 123, struct sja1105_mac_config_entry, enabled[1]),
	PACKED_FIELD(122, 114, struct sja1105_mac_config_entry, top[0]),
	PACKED_FIELD(113, 105, struct sja1105_mac_config_entry, base[0]),
	PACKED_FIELD(104, 104, struct sja1105_mac_config_entry, enabled[0]),
	PACKED_FIELD(103, 99, struct sja1105_mac_config_entry, ifg),
	PACKED_FIELD(98, 97, struct sja1105_mac_config_entry, speed),
	PACKED_FIELD(96, 81, struct sja1105_mac_config_entry, tp_delin),
	PACKED_FIELD(80, 65, struct sja1105_mac_config_entry, tp_delout),
	PACKED_FIELD(64, 57, struct sja1105_mac_config_entry, maxage),
	PACKED_FIELD(56, 54, struct sja1105_mac_config_entry, vlanprio),
	PACKED_FIELD(53, 42, struct sja1105_mac_config_entry, vlanid),
	PACKED_FIELD(41, 41, struct sja1105_mac_config_entry, ing_mirr),
	PACKED_FIELD(40, 40, struct sja1105_mac_config_entry, egr_mirr),
	PACKED_FIELD(39, 39, struct sja1105_mac_config_entry, drpnona664),
	PACKED_FIELD(38, 38, struct sja1105_mac_config_entry, drpdtag),
	PACKED_FIELD(35, 35, struct sja1105_mac_config_entry, drpuntag),
	PACKED_FIELD(34, 34, struct sja1105_mac_config_entry, retag),
	PACKED_FIELD(33, 33, struct sja1105_mac_config_entry, dyn_learn),
	PACKED_FIELD(32, 32, struct sja1105_mac_config_entry, egress),
	PACKED_FIELD(31, 31, struct sja1105_mac_config_entry, ingress),
};

void sja1105pqrs_mac_config_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_40(sja1105pqrs_mac_config_entry_fields,
			       SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY);

	sja1105_pack_fields(buf, SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
			    entry_ptr, sja1105pqrs_mac_config_entry_fields);
}

void sja1105pqrs_mac_config_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
			      entry_ptr, sja1105pqrs_mac_config_entry_fields);
}

static const struct packed_field sja1110_mac_config_entry_fields[] = {
	PACKED_FIELD(255, 247, struct sja1105_mac_config_entry, top[7]),
	PACKED_FIELD(246, 238, struct sja1105_mac_config_entry, base[7]),
	PACKED_FIELD(237, 237, struct sja1105_mac_config_entry, enabled[7]),
	PACKED_FIELD(236, 228, struct sja1105_mac_config_entry, top[6]),
	PACKED_FIELD(227, 219, struct sja1105_mac_config_entry, base[6]),
	PACKED_FIELD(218, 218, struct sja1105_mac_config_entry, enabled[6]),
	PACKED_FIELD(217, 209, struct sja1105_mac_config_entry, top[5]),
	PACKED_FIELD(208, 200, struct sja1105_mac_config_entry, base[5]),
	PACKED_FIELD(199, 199, struct sja1105_mac_config_entry, enabled[5]),
	PACKED_FIELD(198, 190, struct sja1105_mac_config_entry, top[4]),
	PACKED_FIELD(189, 181, struct sja1105_mac_config_entry, base[4]),
	PACKED_FIELD(180, 180, struct sja1105_mac_config_entry, enabled[4]),
	PACKED_FIELD(179, 171, struct sja1105_mac_config_entry, top[3]),
	PACKED_FIELD(170, 162, struct sja1105_mac_config_entry, base[3]),
	PACKED_FIELD(161, 161, struct sja1105_mac_config_entry, enabled[3]),
	PACKED_FIELD(160, 152, struct sja1105_mac_config_entry, top[2]),
	PACKED_FIELD(151, 143, struct sja1105_mac_config_entry, base[2]),
	PACKED_FIELD(142, 142, struct sja1105_mac_config_entry, enabled[2]),
	PACKED_FIELD(141, 133, struct sja1105_mac_config_entry, top[1]),
	PACKED_FIELD(132, 124, struct sja1105_mac_config_entry, base[1]),
	PACKED_FIELD(123, 123, struct sja1105_mac_config_entry, enabled[1]),
	PACKED_FIELD(122, 114, struct sja1105_mac_config_entry, top[0]),
	PACKED_FIELD(113, 105, struct sja1105_mac_config_entry, base[0]),
	PACKED_FIELD(104, 104, struct sja1105_mac_config_entry, enabled[0]),
	PACKED_FIELD(98, 96, struct sja1105_mac_config_entry, speed),
	PACKED_FIELD(95, 80, struct sja1105_mac_config_entry, tp_delin),
	PACKED_FIELD(79, 64, struct sja1105_mac_config_entry, tp_delout),
	PACKED_FIELD(63, 56, struct sja1105_mac_config_entry, maxage),
	PACKED_FIELD(55, 53, struct sja1105_mac_config_entry, vlanprio),
	PACKED_FIELD(52, 41, struct sja1105_mac_config_entry, vlanid),
	PACKED_FIELD(40, 40, struct sja1105_mac_config_entry, ing_mirr),
	PACKED_FIELD(39, 39, struct sja1105_mac_config_entry, egr_mirr),
	PACKED_FIELD(38, 38, struct sja1105_mac_config_entry, drpnona664),
	PACKED_FIELD(37, 37, struct sja1105_mac_config_entry, drpdtag),
	PACKED_FIELD(34, 34, struct sja1105_mac_config_entry, drpuntag),
	PACKED_FIELD(33, 33, struct sja1105_mac_config_entry, retag),
	PACKED_FIELD(32, 32, struct sja1105_mac_config_entry, dyn_learn),
	PACKED_FIELD(31, 31, struct sja1105_mac_config_entry, egress),
	PACKED_FIELD(30, 30, struct sja1105_mac_config_entry, ingress),
	PACKED_FIELD(10, 5, struct sja1105_mac_config_entry, ifg),
};

void sja1110_mac_config_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_40(sja1110_mac_config_entry_fields,
			       SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY);

	sja1105_pack_fields(buf, SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
			    entry_ptr, sja1110_mac_config_entry_fields);
}

void sja1110_mac_config_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
			      entry_ptr, sja1110_mac_config_entry_fields);
}

static const struct packed_field sja1105_schedule_entry_points_params_entry_fields[] = {
	PACKED_FIELD(31, 30, struct sja1105_schedule_entry_points_params_entry, clksrc),
	PACKED_FIELD(29, 27, struct sja1105_schedule_entry_points_params_entry, actsubsch),
};

static void
sja1105_schedule_entry_points_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_2(sja1105_schedule_entry_points_params_entry_fields,
			      SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
			    entry_ptr, sja1105_schedule_entry_points_params_entry_fields);
}

static void
sja1105_schedule_entry_points_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
			      entry_ptr, sja1105_schedule_entry_points_params_entry_fields);
}

static const struct packed_field sja1105_schedule_entry_points_entry_fields[] = {
	PACKED_FIELD(31, 29, struct sja1105_schedule_entry_points_entry, subschindx),
	PACKED_FIELD(28, 11, struct sja1105_schedule_entry_points_entry, delta),
	PACKED_FIELD(10, 1, struct sja1105_schedule_entry_points_entry, address),
};

static void
sja1105_schedule_entry_points_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_3(sja1105_schedule_entry_points_entry_fields,
			      SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
			    entry_ptr, sja1105_schedule_entry_points_entry_fields);
}

static void
sja1105_schedule_entry_points_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
			      entry_ptr, sja1105_schedule_entry_points_entry_fields);
}

static const struct packed_field sja1110_schedule_entry_points_entry_fields[] = {
	PACKED_FIELD(63, 61, struct sja1105_schedule_entry_points_entry, subschindx),
	PACKED_FIELD(60, 43, struct sja1105_schedule_entry_points_entry, delta),
	PACKED_FIELD(42, 31, struct sja1105_schedule_entry_points_entry, address),
};

static void
sja1110_schedule_entry_points_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_3(sja1110_schedule_entry_points_entry_fields,
			      SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
			    entry_ptr, sja1110_schedule_entry_points_entry_fields);
}

static void
sja1110_schedule_entry_points_entry_unpack(const void *buf, void *entry_ptr)
{
	CHECK_PACKED_FIELDS_3(sja1110_schedule_entry_points_entry_fields,
			      SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY);

	sja1105_unpack_fields(buf, SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
			      entry_ptr, sja1110_schedule_entry_points_entry_fields);
}

static const struct packed_field sja1105_schedule_params_entry_fields[] = {
	PACKED_FIELD(95, 86, struct sja1105_schedule_params_entry, subscheind[7]),
	PACKED_FIELD(85, 76, struct sja1105_schedule_params_entry, subscheind[6]),
	PACKED_FIELD(75, 66, struct sja1105_schedule_params_entry, subscheind[5]),
	PACKED_FIELD(65, 56, struct sja1105_schedule_params_entry, subscheind[4]),
	PACKED_FIELD(55, 46, struct sja1105_schedule_params_entry, subscheind[3]),
	PACKED_FIELD(45, 36, struct sja1105_schedule_params_entry, subscheind[2]),
	PACKED_FIELD(35, 26, struct sja1105_schedule_params_entry, subscheind[1]),
	PACKED_FIELD(25, 16, struct sja1105_schedule_params_entry, subscheind[0]),
};

static void sja1105_schedule_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_8(sja1105_schedule_params_entry_fields,
			      SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
			    entry_ptr, sja1105_schedule_params_entry_fields);
}

static void sja1105_schedule_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
			      entry_ptr, sja1105_schedule_params_entry_fields);
}

static const struct packed_field sja1110_schedule_params_entry_fields[] = {
	PACKED_FIELD(95, 84, struct sja1105_schedule_params_entry, subscheind[7]),
	PACKED_FIELD(83, 72, struct sja1105_schedule_params_entry, subscheind[6]),
	PACKED_FIELD(71, 60, struct sja1105_schedule_params_entry, subscheind[5]),
	PACKED_FIELD(59, 48, struct sja1105_schedule_params_entry, subscheind[4]),
	PACKED_FIELD(47, 36, struct sja1105_schedule_params_entry, subscheind[3]),
	PACKED_FIELD(35, 24, struct sja1105_schedule_params_entry, subscheind[2]),
	PACKED_FIELD(23, 12, struct sja1105_schedule_params_entry, subscheind[1]),
	PACKED_FIELD(11, 0, struct sja1105_schedule_params_entry, subscheind[0]),
};

static void sja1110_schedule_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_8(sja1110_schedule_params_entry_fields,
			      SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
			    entry_ptr, sja1110_schedule_params_entry_fields);
}

static void sja1110_schedule_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
			      entry_ptr, sja1110_schedule_params_entry_fields);
}

static const struct packed_field sja1105_schedule_entry_fields[] = {
	PACKED_FIELD(63, 54, struct sja1105_schedule_entry, winstindex),
	PACKED_FIELD(53, 53, struct sja1105_schedule_entry, winend),
	PACKED_FIELD(52, 52, struct sja1105_schedule_entry, winst),
	PACKED_FIELD(51, 47, struct sja1105_schedule_entry, destports),
	PACKED_FIELD(46, 46, struct sja1105_schedule_entry, setvalid),
	PACKED_FIELD(45, 45, struct sja1105_schedule_entry, txen),
	PACKED_FIELD(44, 44, struct sja1105_schedule_entry, resmedia_en),
	PACKED_FIELD(43, 36, struct sja1105_schedule_entry, resmedia),
	PACKED_FIELD(35, 26, struct sja1105_schedule_entry, vlindex),
	PACKED_FIELD(25, 8, struct sja1105_schedule_entry, delta),
};

static void sja1105_schedule_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_10(sja1105_schedule_entry_fields,
			       SJA1105_SIZE_SCHEDULE_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY, entry_ptr,
			    sja1105_schedule_entry_fields);
}

static void sja1105_schedule_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_SCHEDULE_ENTRY, entry_ptr,
			      sja1105_schedule_entry_fields);
}

static const struct packed_field sja1110_schedule_entry_fields[] = {
	PACKED_FIELD(95, 84, struct sja1105_schedule_entry, winstindex),
	PACKED_FIELD(83, 83, struct sja1105_schedule_entry, winend),
	PACKED_FIELD(82, 82, struct sja1105_schedule_entry, winst),
	PACKED_FIELD(81, 71, struct sja1105_schedule_entry, destports),
	PACKED_FIELD(70, 70, struct sja1105_schedule_entry, setvalid),
	PACKED_FIELD(69, 69, struct sja1105_schedule_entry, txen),
	PACKED_FIELD(68, 68, struct sja1105_schedule_entry, resmedia_en),
	PACKED_FIELD(67, 60, struct sja1105_schedule_entry, resmedia),
	PACKED_FIELD(59, 48, struct sja1105_schedule_entry, vlindex),
	PACKED_FIELD(47, 30, struct sja1105_schedule_entry, delta),
};

static void sja1110_schedule_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_10(sja1110_schedule_entry_fields,
			       SJA1110_SIZE_SCHEDULE_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_SCHEDULE_ENTRY, entry_ptr,
			    sja1110_schedule_entry_fields);
}

static void sja1110_schedule_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_SCHEDULE_ENTRY, entry_ptr,
			      sja1110_schedule_entry_fields);
}

static const struct packed_field sja1105_vl_forwarding_params_entry_fields[] = {
	PACKED_FIELD(95, 86, struct sja1105_vl_forwarding_params_entry, partspc[7]),
	PACKED_FIELD(85, 76, struct sja1105_vl_forwarding_params_entry, partspc[6]),
	PACKED_FIELD(75, 66, struct sja1105_vl_forwarding_params_entry, partspc[5]),
	PACKED_FIELD(65, 56, struct sja1105_vl_forwarding_params_entry, partspc[4]),
	PACKED_FIELD(55, 46, struct sja1105_vl_forwarding_params_entry, partspc[3]),
	PACKED_FIELD(45, 36, struct sja1105_vl_forwarding_params_entry, partspc[2]),
	PACKED_FIELD(35, 26, struct sja1105_vl_forwarding_params_entry, partspc[1]),
	PACKED_FIELD(25, 16, struct sja1105_vl_forwarding_params_entry, partspc[0]),
	PACKED_FIELD(15, 15, struct sja1105_vl_forwarding_params_entry, debugen),
};

static void
sja1105_vl_forwarding_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_9(sja1105_vl_forwarding_params_entry_fields,
			      SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
			    entry_ptr, sja1105_vl_forwarding_params_entry_fields);
}

static void
sja1105_vl_forwarding_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
			      entry_ptr, sja1105_vl_forwarding_params_entry_fields);
}

static const struct packed_field sja1110_vl_forwarding_params_entry_fields[] = {
	PACKED_FIELD(95, 85, struct sja1105_vl_forwarding_params_entry, partspc[7]),
	PACKED_FIELD(84, 74, struct sja1105_vl_forwarding_params_entry, partspc[6]),
	PACKED_FIELD(73, 63, struct sja1105_vl_forwarding_params_entry, partspc[5]),
	PACKED_FIELD(62, 52, struct sja1105_vl_forwarding_params_entry, partspc[4]),
	PACKED_FIELD(51, 41, struct sja1105_vl_forwarding_params_entry, partspc[3]),
	PACKED_FIELD(40, 30, struct sja1105_vl_forwarding_params_entry, partspc[2]),
	PACKED_FIELD(29, 19, struct sja1105_vl_forwarding_params_entry, partspc[1]),
	PACKED_FIELD(18, 8, struct sja1105_vl_forwarding_params_entry, partspc[0]),
	PACKED_FIELD(7, 7, struct sja1105_vl_forwarding_params_entry, debugen),
};

static void
sja1110_vl_forwarding_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_9(sja1110_vl_forwarding_params_entry_fields,
			      SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
			    entry_ptr, sja1110_vl_forwarding_params_entry_fields);
}

static void
sja1110_vl_forwarding_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
			      entry_ptr, sja1110_vl_forwarding_params_entry_fields);
}

static const struct packed_field sja1105_vl_forwarding_entry_fields[] = {
	PACKED_FIELD(31, 31, struct sja1105_vl_forwarding_entry, type),
	PACKED_FIELD(30, 28, struct sja1105_vl_forwarding_entry, priority),
	PACKED_FIELD(27, 25, struct sja1105_vl_forwarding_entry, partition),
	PACKED_FIELD(24, 20, struct sja1105_vl_forwarding_entry, destports),
};

static void sja1105_vl_forwarding_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_4(sja1105_vl_forwarding_entry_fields,
			      SJA1105_SIZE_VL_FORWARDING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_FORWARDING_ENTRY, entry_ptr,
			    sja1105_vl_forwarding_entry_fields);
}

static void sja1105_vl_forwarding_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_FORWARDING_ENTRY, entry_ptr,
			      sja1105_vl_forwarding_entry_fields);
}

static const struct packed_field sja1110_vl_forwarding_entry_fields[] = {
	PACKED_FIELD(31, 31, struct sja1105_vl_forwarding_entry, type),
	PACKED_FIELD(30, 28, struct sja1105_vl_forwarding_entry, priority),
	PACKED_FIELD(27, 25, struct sja1105_vl_forwarding_entry, partition),
	PACKED_FIELD(24, 14, struct sja1105_vl_forwarding_entry, destports),
};

static void sja1110_vl_forwarding_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_4(sja1110_vl_forwarding_entry_fields,
			      SJA1105_SIZE_VL_FORWARDING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_FORWARDING_ENTRY, entry_ptr,
			    sja1110_vl_forwarding_entry_fields);
}

static void sja1110_vl_forwarding_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_FORWARDING_ENTRY, entry_ptr,
			      sja1110_vl_forwarding_entry_fields);
}

static const struct packed_field sja1105_vl_lookup_entry_type1_fields[] = {
	PACKED_FIELD(95, 91, struct sja1105_vl_lookup_entry, destports),
	PACKED_FIELD(90, 90, struct sja1105_vl_lookup_entry, iscritical),
	PACKED_FIELD(89, 42, struct sja1105_vl_lookup_entry, macaddr),
	PACKED_FIELD(41, 30, struct sja1105_vl_lookup_entry, vlanid),
	PACKED_FIELD(29, 27, struct sja1105_vl_lookup_entry, port),
	PACKED_FIELD(26, 24, struct sja1105_vl_lookup_entry, vlanprior),
};

static const struct packed_field sja1105_vl_lookup_entry_type2_fields[] = {
	PACKED_FIELD(95, 91, struct sja1105_vl_lookup_entry, egrmirr),
	PACKED_FIELD(90, 90, struct sja1105_vl_lookup_entry, ingrmirr),
	PACKED_FIELD(57, 42, struct sja1105_vl_lookup_entry, vlid),
	PACKED_FIELD(29, 27, struct sja1105_vl_lookup_entry, port),
};

void sja1105_vl_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_vl_lookup_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_6(sja1105_vl_lookup_entry_type1_fields,
			      SJA1105_SIZE_VL_LOOKUP_ENTRY);
	CHECK_PACKED_FIELDS_4(sja1105_vl_lookup_entry_type2_fields,
			      SJA1105_SIZE_VL_LOOKUP_ENTRY);

	if (entry->format == SJA1105_VL_FORMAT_PSFP) {
		/* Interpreting vllupformat as 0 */
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				    sja1105_vl_lookup_entry_type1_fields);
	} else {
		/* Interpreting vllupformat as 1 */
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				    sja1105_vl_lookup_entry_type2_fields);
	}
}

void sja1105_vl_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_vl_lookup_entry *entry = entry_ptr;

	if (entry->format == SJA1105_VL_FORMAT_PSFP) {
		/* Interpreting vllupformat as 0 */
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				      sja1105_vl_lookup_entry_type1_fields);
	} else {
		/* Interpreting vllupformat as 1 */
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				      sja1105_vl_lookup_entry_type2_fields);
	}
}

static const struct packed_field sja1110_vl_lookup_entry_type1_fields[] = {
	PACKED_FIELD(94, 84, struct sja1105_vl_lookup_entry, destports),
	PACKED_FIELD(83, 83, struct sja1105_vl_lookup_entry, iscritical),
	PACKED_FIELD(82, 35, struct sja1105_vl_lookup_entry, macaddr),
	PACKED_FIELD(34, 23, struct sja1105_vl_lookup_entry, vlanid),
	PACKED_FIELD(22, 19, struct sja1105_vl_lookup_entry, port),
	PACKED_FIELD(18, 16, struct sja1105_vl_lookup_entry, vlanprior),
};

static const struct packed_field sja1110_vl_lookup_entry_type2_fields[] = {
	PACKED_FIELD(94, 84, struct sja1105_vl_lookup_entry, egrmirr),
	PACKED_FIELD(83, 83, struct sja1105_vl_lookup_entry, ingrmirr),
	PACKED_FIELD(50, 35, struct sja1105_vl_lookup_entry, vlid),
	PACKED_FIELD(22, 19, struct sja1105_vl_lookup_entry, port),
};

void sja1110_vl_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_vl_lookup_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_6(sja1110_vl_lookup_entry_type1_fields,
			      SJA1105_SIZE_VL_LOOKUP_ENTRY);
	CHECK_PACKED_FIELDS_4(sja1110_vl_lookup_entry_type2_fields,
			      SJA1105_SIZE_VL_LOOKUP_ENTRY);

	if (entry->format == SJA1105_VL_FORMAT_PSFP) {
		/* Interpreting vllupformat as 0 */
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				    sja1110_vl_lookup_entry_type1_fields);
	} else {
		/* Interpreting vllupformat as 1 */
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				    sja1110_vl_lookup_entry_type2_fields);
	}
}

void sja1110_vl_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_vl_lookup_entry *entry = entry_ptr;

	if (entry->format == SJA1105_VL_FORMAT_PSFP) {
		/* Interpreting vllupformat as 0 */
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				      sja1110_vl_lookup_entry_type1_fields);
	} else {
		/* Interpreting vllupformat as 1 */
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_LOOKUP_ENTRY, entry_ptr,
				      sja1110_vl_lookup_entry_type2_fields);
	}
}

static const struct packed_field sja1105_vl_policing_entry_base_fields[] = {
	PACKED_FIELD(63, 63, struct sja1105_vl_policing_entry, type),
	PACKED_FIELD(62, 52, struct sja1105_vl_policing_entry, maxlen),
	PACKED_FIELD(51, 42, struct sja1105_vl_policing_entry, sharindx),
};

static const struct packed_field sja1105_vl_policing_entry_extra_fields[] = {
	PACKED_FIELD(41, 28, struct sja1105_vl_policing_entry, bag),
	PACKED_FIELD(27, 18, struct sja1105_vl_policing_entry, jitter),
};

static void sja1105_vl_policing_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_vl_policing_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_3(sja1105_vl_policing_entry_base_fields,
			      SJA1105_SIZE_VL_POLICING_ENTRY);
	CHECK_PACKED_FIELDS_2(sja1105_vl_policing_entry_extra_fields,
			      SJA1105_SIZE_VL_POLICING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
			    sja1105_vl_policing_entry_base_fields);

	if (entry->type == 0) {
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
				    sja1105_vl_policing_entry_extra_fields);
	}
}

static void sja1105_vl_policing_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_vl_policing_entry *entry = entry_ptr;

	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY,
			      entry_ptr, sja1105_vl_policing_entry_base_fields);

	if (entry->type == 0) {
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
				      sja1105_vl_policing_entry_extra_fields);
	}
}

static const struct packed_field sja1110_vl_policing_entry_base_fields[] = {
	PACKED_FIELD(63, 63, struct sja1105_vl_policing_entry, type),
	PACKED_FIELD(62, 52, struct sja1105_vl_policing_entry, maxlen),
	PACKED_FIELD(51, 40, struct sja1105_vl_policing_entry, sharindx),
};

void sja1110_vl_policing_entry_pack(void *buf, const void *entry_ptr)
{
	const struct sja1105_vl_policing_entry *entry = entry_ptr;

	CHECK_PACKED_FIELDS_3(sja1110_vl_policing_entry_base_fields,
			      SJA1105_SIZE_VL_POLICING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
			    sja1105_vl_policing_entry_extra_fields);

	if (entry->type == 0) {
		sja1105_pack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
				    sja1105_vl_policing_entry_extra_fields);
	}
}

void sja1110_vl_policing_entry_unpack(const void *buf, void *entry_ptr)
{
	const struct sja1105_vl_policing_entry *entry = entry_ptr;

	sja1105_unpack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
			      sja1105_vl_policing_entry_extra_fields);

	if (entry->type == 0) {
		sja1105_unpack_fields(buf, SJA1105_SIZE_VL_POLICING_ENTRY, entry_ptr,
				      sja1105_vl_policing_entry_extra_fields);
	}
}

static const struct packed_field sja1105_vlan_lookup_entry_fields[] = {
	PACKED_FIELD(63, 59, struct sja1105_vlan_lookup_entry, ving_mirr),
	PACKED_FIELD(58, 54, struct sja1105_vlan_lookup_entry, vegr_mirr),
	PACKED_FIELD(53, 49, struct sja1105_vlan_lookup_entry, vmemb_port),
	PACKED_FIELD(48, 44, struct sja1105_vlan_lookup_entry, vlan_bc),
	PACKED_FIELD(43, 39, struct sja1105_vlan_lookup_entry, tag_port),
	PACKED_FIELD(38, 27, struct sja1105_vlan_lookup_entry, vlanid),
};

void sja1105_vlan_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_6(sja1105_vlan_lookup_entry_fields,
			      SJA1105_SIZE_VLAN_LOOKUP_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_VLAN_LOOKUP_ENTRY, entry_ptr,
			    sja1105_vlan_lookup_entry_fields);
}

void sja1105_vlan_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_VLAN_LOOKUP_ENTRY, entry_ptr,
			      sja1105_vlan_lookup_entry_fields);
}

static const struct packed_field sja1110_vlan_lookup_entry_fields[] = {
	PACKED_FIELD(95, 85, struct sja1105_vlan_lookup_entry, ving_mirr),
	PACKED_FIELD(84, 74, struct sja1105_vlan_lookup_entry, vegr_mirr),
	PACKED_FIELD(73, 63, struct sja1105_vlan_lookup_entry, vmemb_port),
	PACKED_FIELD(62, 52, struct sja1105_vlan_lookup_entry, vlan_bc),
	PACKED_FIELD(51, 41, struct sja1105_vlan_lookup_entry, tag_port),
	PACKED_FIELD(40, 39, struct sja1105_vlan_lookup_entry, type_entry),
	PACKED_FIELD(38, 27, struct sja1105_vlan_lookup_entry, vlanid),
};

void sja1110_vlan_lookup_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_7(sja1110_vlan_lookup_entry_fields,
			      SJA1110_SIZE_VLAN_LOOKUP_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_VLAN_LOOKUP_ENTRY, entry_ptr,
			    sja1110_vlan_lookup_entry_fields);
}

void sja1110_vlan_lookup_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_VLAN_LOOKUP_ENTRY, entry_ptr,
			      sja1110_vlan_lookup_entry_fields);
}

static const struct packed_field sja1105_xmii_params_entry_fields[] = {
	PACKED_FIELD(31, 31, struct sja1105_xmii_params_entry, phy_mac[4]),
	PACKED_FIELD(30, 29, struct sja1105_xmii_params_entry, xmii_mode[4]),
	PACKED_FIELD(28, 28, struct sja1105_xmii_params_entry, phy_mac[3]),
	PACKED_FIELD(27, 26, struct sja1105_xmii_params_entry, xmii_mode[3]),
	PACKED_FIELD(25, 25, struct sja1105_xmii_params_entry, phy_mac[2]),
	PACKED_FIELD(24, 23, struct sja1105_xmii_params_entry, xmii_mode[2]),
	PACKED_FIELD(22, 22, struct sja1105_xmii_params_entry, phy_mac[1]),
	PACKED_FIELD(21, 20, struct sja1105_xmii_params_entry, xmii_mode[1]),
	PACKED_FIELD(19, 19, struct sja1105_xmii_params_entry, phy_mac[0]),
	PACKED_FIELD(18, 17, struct sja1105_xmii_params_entry, xmii_mode[0]),
};

static void sja1105_xmii_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_10(sja1105_xmii_params_entry_fields,
			       SJA1105_SIZE_XMII_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_XMII_PARAMS_ENTRY, entry_ptr,
			    sja1105_xmii_params_entry_fields);
}

static void sja1105_xmii_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_XMII_PARAMS_ENTRY, entry_ptr,
			      sja1105_xmii_params_entry_fields);
}

static const struct packed_field sja1110_xmii_params_entry_fields[] = {
	PACKED_FIELD(63, 63, struct sja1105_xmii_params_entry, special[10]),
	PACKED_FIELD(62, 62, struct sja1105_xmii_params_entry, phy_mac[10]),
	PACKED_FIELD(61, 60, struct sja1105_xmii_params_entry, xmii_mode[10]),
	PACKED_FIELD(59, 59, struct sja1105_xmii_params_entry, special[9]),
	PACKED_FIELD(58, 58, struct sja1105_xmii_params_entry, phy_mac[9]),
	PACKED_FIELD(57, 56, struct sja1105_xmii_params_entry, xmii_mode[9]),
	PACKED_FIELD(55, 55, struct sja1105_xmii_params_entry, special[8]),
	PACKED_FIELD(54, 54, struct sja1105_xmii_params_entry, phy_mac[8]),
	PACKED_FIELD(53, 52, struct sja1105_xmii_params_entry, xmii_mode[8]),
	PACKED_FIELD(51, 51, struct sja1105_xmii_params_entry, special[7]),
	PACKED_FIELD(50, 50, struct sja1105_xmii_params_entry, phy_mac[7]),
	PACKED_FIELD(49, 48, struct sja1105_xmii_params_entry, xmii_mode[7]),
	PACKED_FIELD(47, 47, struct sja1105_xmii_params_entry, special[6]),
	PACKED_FIELD(46, 46, struct sja1105_xmii_params_entry, phy_mac[6]),
	PACKED_FIELD(45, 44, struct sja1105_xmii_params_entry, xmii_mode[6]),
	PACKED_FIELD(43, 43, struct sja1105_xmii_params_entry, special[5]),
	PACKED_FIELD(42, 42, struct sja1105_xmii_params_entry, phy_mac[5]),
	PACKED_FIELD(41, 40, struct sja1105_xmii_params_entry, xmii_mode[5]),
	PACKED_FIELD(39, 39, struct sja1105_xmii_params_entry, special[4]),
	PACKED_FIELD(38, 38, struct sja1105_xmii_params_entry, phy_mac[4]),
	PACKED_FIELD(37, 36, struct sja1105_xmii_params_entry, xmii_mode[4]),
	PACKED_FIELD(35, 35, struct sja1105_xmii_params_entry, special[3]),
	PACKED_FIELD(34, 34, struct sja1105_xmii_params_entry, phy_mac[3]),
	PACKED_FIELD(33, 32, struct sja1105_xmii_params_entry, xmii_mode[3]),
	PACKED_FIELD(31, 31, struct sja1105_xmii_params_entry, special[2]),
	PACKED_FIELD(30, 30, struct sja1105_xmii_params_entry, phy_mac[2]),
	PACKED_FIELD(29, 28, struct sja1105_xmii_params_entry, xmii_mode[2]),
	PACKED_FIELD(27, 27, struct sja1105_xmii_params_entry, special[1]),
	PACKED_FIELD(26, 26, struct sja1105_xmii_params_entry, phy_mac[1]),
	PACKED_FIELD(25, 24, struct sja1105_xmii_params_entry, xmii_mode[1]),
	PACKED_FIELD(23, 23, struct sja1105_xmii_params_entry, special[0]),
	PACKED_FIELD(22, 22, struct sja1105_xmii_params_entry, phy_mac[0]),
	PACKED_FIELD(21, 20, struct sja1105_xmii_params_entry, xmii_mode[0]),
};

void sja1110_xmii_params_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_33(sja1110_xmii_params_entry_fields,
			       SJA1110_SIZE_XMII_PARAMS_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_XMII_PARAMS_ENTRY, entry_ptr,
			    sja1110_xmii_params_entry_fields);
}

void sja1110_xmii_params_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_XMII_PARAMS_ENTRY, entry_ptr,
			      sja1110_xmii_params_entry_fields);
}

static const struct packed_field sja1105_retagging_entry_fields[] = {
	PACKED_FIELD(63, 59, struct sja1105_retagging_entry, egr_port),
	PACKED_FIELD(58, 54, struct sja1105_retagging_entry, ing_port),
	PACKED_FIELD(53, 42, struct sja1105_retagging_entry, vlan_ing),
	PACKED_FIELD(41, 30, struct sja1105_retagging_entry, vlan_egr),
	PACKED_FIELD(29, 29, struct sja1105_retagging_entry, do_not_learn),
	PACKED_FIELD(28, 28, struct sja1105_retagging_entry, use_dest_ports),
	PACKED_FIELD(27, 23, struct sja1105_retagging_entry, destports),
};

void sja1105_retagging_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_7(sja1105_retagging_entry_fields,
			      SJA1105_SIZE_RETAGGING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_RETAGGING_ENTRY, entry_ptr,
			    sja1105_retagging_entry_fields);
}

void sja1105_retagging_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_RETAGGING_ENTRY, entry_ptr,
			      sja1105_retagging_entry_fields);
}

static const struct packed_field sja1110_retagging_entry_fields[] = {
	PACKED_FIELD(63, 53, struct sja1105_retagging_entry, egr_port),
	PACKED_FIELD(52, 42, struct sja1105_retagging_entry, ing_port),
	PACKED_FIELD(41, 30, struct sja1105_retagging_entry, vlan_ing),
	PACKED_FIELD(29, 18, struct sja1105_retagging_entry, vlan_egr),
	PACKED_FIELD(17, 17, struct sja1105_retagging_entry, do_not_learn),
	PACKED_FIELD(16, 16, struct sja1105_retagging_entry, use_dest_ports),
	PACKED_FIELD(15, 5, struct sja1105_retagging_entry, destports),
};

void sja1110_retagging_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_7(sja1110_retagging_entry_fields,
			      SJA1105_SIZE_RETAGGING_ENTRY);

	sja1105_pack_fields(buf, SJA1105_SIZE_RETAGGING_ENTRY, entry_ptr,
			    sja1110_retagging_entry_fields);
}

void sja1110_retagging_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_RETAGGING_ENTRY, entry_ptr,
			      sja1110_retagging_entry_fields);
}

static const struct packed_field sja1110_pcp_remapping_entry_fields[] = {
	PACKED_FIELD(31, 29, struct sja1110_pcp_remapping_entry, egrpcp[7]),
	PACKED_FIELD(28, 26, struct sja1110_pcp_remapping_entry, egrpcp[6]),
	PACKED_FIELD(25, 23, struct sja1110_pcp_remapping_entry, egrpcp[5]),
	PACKED_FIELD(22, 20, struct sja1110_pcp_remapping_entry, egrpcp[4]),
	PACKED_FIELD(19, 17, struct sja1110_pcp_remapping_entry, egrpcp[3]),
	PACKED_FIELD(16, 14, struct sja1110_pcp_remapping_entry, egrpcp[2]),
	PACKED_FIELD(13, 11, struct sja1110_pcp_remapping_entry, egrpcp[1]),
	PACKED_FIELD(10, 8, struct sja1110_pcp_remapping_entry, egrpcp[0]),
};

static void sja1110_pcp_remapping_entry_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_8(sja1110_pcp_remapping_entry_fields,
			      SJA1110_SIZE_PCP_REMAPPING_ENTRY);

	sja1105_pack_fields(buf, SJA1110_SIZE_PCP_REMAPPING_ENTRY, entry_ptr,
			    sja1110_pcp_remapping_entry_fields);
}

static void sja1110_pcp_remapping_entry_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1110_SIZE_PCP_REMAPPING_ENTRY, entry_ptr,
			      sja1110_pcp_remapping_entry_fields);
}

static const struct packed_field sja1105_table_header_fields[] = {
	PACKED_FIELD(95, 64, struct sja1105_table_header, crc),
	PACKED_FIELD(55, 32, struct sja1105_table_header, len),
	PACKED_FIELD(31, 24, struct sja1105_table_header, block_id),
};

void sja1105_table_header_pack(void *buf, const void *entry_ptr)
{
	CHECK_PACKED_FIELDS_3(sja1105_table_header_fields,
			      SJA1105_SIZE_TABLE_HEADER);

	sja1105_pack_fields(buf, SJA1105_SIZE_TABLE_HEADER, entry_ptr,
			    sja1105_table_header_fields);
}

void sja1105_table_header_unpack(const void *buf, void *entry_ptr)
{
	sja1105_unpack_fields(buf, SJA1105_SIZE_TABLE_HEADER, entry_ptr,
			      sja1105_table_header_fields);
}

/* WARNING: the *hdr pointer is really non-const, because it is
 * modifying the CRC of the header for a 2-stage packing operation
 */
void
sja1105_table_header_pack_with_crc(void *buf, struct sja1105_table_header *hdr)
{
	/* First pack the table as-is, then calculate the CRC, and
	 * finally put the proper CRC into the packed buffer
	 */
	memset(buf, 0, SJA1105_SIZE_TABLE_HEADER);
	sja1105_table_header_pack(buf, hdr);
	hdr->crc = sja1105_crc32(buf, SJA1105_SIZE_TABLE_HEADER - 4);
	sja1105_pack(buf + SJA1105_SIZE_TABLE_HEADER - 4, hdr->crc, 31, 0, 4);
}

static void sja1105_table_write_crc(u8 *table_start, u8 *crc_ptr)
{
	u64 computed_crc;
	int len_bytes;

	len_bytes = (uintptr_t)(crc_ptr - table_start);
	computed_crc = sja1105_crc32(table_start, len_bytes);
	sja1105_pack(crc_ptr, computed_crc, 31, 0, 4);
}

/* The block IDs that the switches support are unfortunately sparse, so keep a
 * mapping table to "block indices" and translate back and forth so that we
 * don't waste useless memory in struct sja1105_static_config.
 * Also, since the block id comes from essentially untrusted input (unpacking
 * the static config from userspace) it has to be sanitized (range-checked)
 * before blindly indexing kernel memory with the blk_idx.
 */
static u64 blk_id_map[BLK_IDX_MAX] = {
	[BLK_IDX_SCHEDULE] = BLKID_SCHEDULE,
	[BLK_IDX_SCHEDULE_ENTRY_POINTS] = BLKID_SCHEDULE_ENTRY_POINTS,
	[BLK_IDX_VL_LOOKUP] = BLKID_VL_LOOKUP,
	[BLK_IDX_VL_POLICING] = BLKID_VL_POLICING,
	[BLK_IDX_VL_FORWARDING] = BLKID_VL_FORWARDING,
	[BLK_IDX_L2_LOOKUP] = BLKID_L2_LOOKUP,
	[BLK_IDX_L2_POLICING] = BLKID_L2_POLICING,
	[BLK_IDX_VLAN_LOOKUP] = BLKID_VLAN_LOOKUP,
	[BLK_IDX_L2_FORWARDING] = BLKID_L2_FORWARDING,
	[BLK_IDX_MAC_CONFIG] = BLKID_MAC_CONFIG,
	[BLK_IDX_SCHEDULE_PARAMS] = BLKID_SCHEDULE_PARAMS,
	[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS] = BLKID_SCHEDULE_ENTRY_POINTS_PARAMS,
	[BLK_IDX_VL_FORWARDING_PARAMS] = BLKID_VL_FORWARDING_PARAMS,
	[BLK_IDX_L2_LOOKUP_PARAMS] = BLKID_L2_LOOKUP_PARAMS,
	[BLK_IDX_L2_FORWARDING_PARAMS] = BLKID_L2_FORWARDING_PARAMS,
	[BLK_IDX_AVB_PARAMS] = BLKID_AVB_PARAMS,
	[BLK_IDX_GENERAL_PARAMS] = BLKID_GENERAL_PARAMS,
	[BLK_IDX_RETAGGING] = BLKID_RETAGGING,
	[BLK_IDX_XMII_PARAMS] = BLKID_XMII_PARAMS,
	[BLK_IDX_PCP_REMAPPING] = BLKID_PCP_REMAPPING,
};

const char *sja1105_static_config_error_msg[] = {
	[SJA1105_CONFIG_OK] = "",
	[SJA1105_TTETHERNET_NOT_SUPPORTED] =
		"schedule-table present, but TTEthernet is "
		"only supported on T and Q/S",
	[SJA1105_INCORRECT_TTETHERNET_CONFIGURATION] =
		"schedule-table present, but one of "
		"schedule-entry-points-table, schedule-parameters-table or "
		"schedule-entry-points-parameters table is empty",
	[SJA1105_INCORRECT_VIRTUAL_LINK_CONFIGURATION] =
		"vl-lookup-table present, but one of vl-policing-table, "
		"vl-forwarding-table or vl-forwarding-parameters-table is empty",
	[SJA1105_MISSING_L2_POLICING_TABLE] =
		"l2-policing-table needs to have at least one entry",
	[SJA1105_MISSING_L2_FORWARDING_TABLE] =
		"l2-forwarding-table is either missing or incomplete",
	[SJA1105_MISSING_L2_FORWARDING_PARAMS_TABLE] =
		"l2-forwarding-parameters-table is missing",
	[SJA1105_MISSING_GENERAL_PARAMS_TABLE] =
		"general-parameters-table is missing",
	[SJA1105_MISSING_VLAN_TABLE] =
		"vlan-lookup-table needs to have at least the default untagged VLAN",
	[SJA1105_MISSING_XMII_TABLE] =
		"xmii-table is missing",
	[SJA1105_MISSING_MAC_TABLE] =
		"mac-configuration-table needs to contain an entry for each port",
	[SJA1105_OVERCOMMITTED_FRAME_MEMORY] =
		"Not allowed to overcommit frame memory. L2 memory partitions "
		"and VL memory partitions share the same space. The sum of all "
		"16 memory partitions is not allowed to be larger than 929 "
		"128-byte blocks (or 910 with retagging). Please adjust "
		"l2-forwarding-parameters-table.part_spc and/or "
		"vl-forwarding-parameters-table.partspc.",
};

static sja1105_config_valid_t
static_config_check_memory_size(const struct sja1105_table *tables, int max_mem)
{
	const struct sja1105_l2_forwarding_params_entry *l2_fwd_params;
	const struct sja1105_vl_forwarding_params_entry *vl_fwd_params;
	int i, mem = 0;

	l2_fwd_params = tables[BLK_IDX_L2_FORWARDING_PARAMS].entries;

	for (i = 0; i < 8; i++)
		mem += l2_fwd_params->part_spc[i];

	if (tables[BLK_IDX_VL_FORWARDING_PARAMS].entry_count) {
		vl_fwd_params = tables[BLK_IDX_VL_FORWARDING_PARAMS].entries;
		for (i = 0; i < 8; i++)
			mem += vl_fwd_params->partspc[i];
	}

	if (tables[BLK_IDX_RETAGGING].entry_count)
		max_mem -= SJA1105_FRAME_MEMORY_RETAGGING_OVERHEAD;

	if (mem > max_mem)
		return SJA1105_OVERCOMMITTED_FRAME_MEMORY;

	return SJA1105_CONFIG_OK;
}

sja1105_config_valid_t
sja1105_static_config_check_valid(const struct sja1105_static_config *config,
				  int max_mem)
{
	const struct sja1105_table *tables = config->tables;
#define IS_FULL(blk_idx) \
	(tables[blk_idx].entry_count == tables[blk_idx].ops->max_entry_count)

	if (tables[BLK_IDX_SCHEDULE].entry_count) {
		if (!tables[BLK_IDX_SCHEDULE].ops->max_entry_count)
			return SJA1105_TTETHERNET_NOT_SUPPORTED;

		if (tables[BLK_IDX_SCHEDULE_ENTRY_POINTS].entry_count == 0)
			return SJA1105_INCORRECT_TTETHERNET_CONFIGURATION;

		if (!IS_FULL(BLK_IDX_SCHEDULE_PARAMS))
			return SJA1105_INCORRECT_TTETHERNET_CONFIGURATION;

		if (!IS_FULL(BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS))
			return SJA1105_INCORRECT_TTETHERNET_CONFIGURATION;
	}
	if (tables[BLK_IDX_VL_LOOKUP].entry_count) {
		struct sja1105_vl_lookup_entry *vl_lookup;
		bool has_critical_links = false;
		int i;

		vl_lookup = tables[BLK_IDX_VL_LOOKUP].entries;

		for (i = 0; i < tables[BLK_IDX_VL_LOOKUP].entry_count; i++) {
			if (vl_lookup[i].iscritical) {
				has_critical_links = true;
				break;
			}
		}

		if (tables[BLK_IDX_VL_POLICING].entry_count == 0 &&
		    has_critical_links)
			return SJA1105_INCORRECT_VIRTUAL_LINK_CONFIGURATION;

		if (tables[BLK_IDX_VL_FORWARDING].entry_count == 0 &&
		    has_critical_links)
			return SJA1105_INCORRECT_VIRTUAL_LINK_CONFIGURATION;

		if (tables[BLK_IDX_VL_FORWARDING_PARAMS].entry_count == 0 &&
		    has_critical_links)
			return SJA1105_INCORRECT_VIRTUAL_LINK_CONFIGURATION;
	}

	if (tables[BLK_IDX_L2_POLICING].entry_count == 0)
		return SJA1105_MISSING_L2_POLICING_TABLE;

	if (tables[BLK_IDX_VLAN_LOOKUP].entry_count == 0)
		return SJA1105_MISSING_VLAN_TABLE;

	if (!IS_FULL(BLK_IDX_L2_FORWARDING))
		return SJA1105_MISSING_L2_FORWARDING_TABLE;

	if (!IS_FULL(BLK_IDX_MAC_CONFIG))
		return SJA1105_MISSING_MAC_TABLE;

	if (!IS_FULL(BLK_IDX_L2_FORWARDING_PARAMS))
		return SJA1105_MISSING_L2_FORWARDING_PARAMS_TABLE;

	if (!IS_FULL(BLK_IDX_GENERAL_PARAMS))
		return SJA1105_MISSING_GENERAL_PARAMS_TABLE;

	if (!IS_FULL(BLK_IDX_XMII_PARAMS))
		return SJA1105_MISSING_XMII_TABLE;

	return static_config_check_memory_size(tables, max_mem);
#undef IS_FULL
}

void
sja1105_static_config_pack(void *buf, struct sja1105_static_config *config)
{
	struct sja1105_table_header header = {0};
	enum sja1105_blk_idx i;
	char *p = buf;
	int j;

	sja1105_pack(p, config->device_id, 31, 0, 4);
	p += SJA1105_SIZE_DEVICE_ID;

	for (i = 0; i < BLK_IDX_MAX; i++) {
		const struct sja1105_table *table;
		char *table_start;

		table = &config->tables[i];
		if (!table->entry_count)
			continue;

		header.block_id = blk_id_map[i];
		header.len = table->entry_count *
			     table->ops->packed_entry_size / 4;
		sja1105_table_header_pack_with_crc(p, &header);
		p += SJA1105_SIZE_TABLE_HEADER;
		table_start = p;
		for (j = 0; j < table->entry_count; j++) {
			u8 *entry_ptr = table->entries;

			entry_ptr += j * table->ops->unpacked_entry_size;
			memset(p, 0, table->ops->packed_entry_size);
			table->ops->pack(p, entry_ptr);
			p += table->ops->packed_entry_size;
		}
		sja1105_table_write_crc(table_start, p);
		p += 4;
	}
	/* Final header:
	 * Block ID does not matter
	 * Length of 0 marks that header is final
	 * CRC will be replaced on-the-fly on "config upload"
	 */
	header.block_id = 0;
	header.len = 0;
	header.crc = 0xDEADBEEF;
	memset(p, 0, SJA1105_SIZE_TABLE_HEADER);
	sja1105_table_header_pack(p, &header);
}

size_t
sja1105_static_config_get_length(const struct sja1105_static_config *config)
{
	unsigned int sum;
	unsigned int header_count;
	enum sja1105_blk_idx i;

	/* Ending header */
	header_count = 1;
	sum = SJA1105_SIZE_DEVICE_ID;

	/* Tables (headers and entries) */
	for (i = 0; i < BLK_IDX_MAX; i++) {
		const struct sja1105_table *table;

		table = &config->tables[i];
		if (table->entry_count)
			header_count++;

		sum += table->ops->packed_entry_size * table->entry_count;
	}
	/* Headers have an additional CRC at the end */
	sum += header_count * (SJA1105_SIZE_TABLE_HEADER + 4);
	/* Last header does not have an extra CRC because there is no data */
	sum -= 4;

	return sum;
}

/* Compatibility matrices */

/* SJA1105E: First generation, no TTEthernet */
const struct sja1105_table_ops sja1105e_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105et_l2_lookup_entry_pack,
		.unpack = sja1105et_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105et_mac_config_entry_pack,
		.unpack = sja1105et_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105et_l2_lookup_params_entry_pack,
		.unpack = sja1105et_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105et_avb_params_entry_pack,
		.unpack = sja1105et_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105et_general_params_entry_pack,
		.unpack = sja1105et_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105T: First generation, TTEthernet */
const struct sja1105_table_ops sja1105t_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_SCHEDULE] = {
		.pack = sja1105_schedule_entry_pack,
		.unpack = sja1105_schedule_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS] = {
		.pack = sja1105_schedule_entry_points_entry_pack,
		.unpack = sja1105_schedule_entry_points_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_COUNT,
	},
	[BLK_IDX_VL_LOOKUP] = {
		.pack = sja1105_vl_lookup_entry_pack,
		.unpack = sja1105_vl_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VL_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_LOOKUP_COUNT,
	},
	[BLK_IDX_VL_POLICING] = {
		.pack = sja1105_vl_policing_entry_pack,
		.unpack = sja1105_vl_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_policing_entry),
		.packed_entry_size = SJA1105_SIZE_VL_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_POLICING_COUNT,
	},
	[BLK_IDX_VL_FORWARDING] = {
		.pack = sja1105_vl_forwarding_entry_pack,
		.unpack = sja1105_vl_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_COUNT,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105et_l2_lookup_entry_pack,
		.unpack = sja1105et_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105et_mac_config_entry_pack,
		.unpack = sja1105et_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_SCHEDULE_PARAMS] = {
		.pack = sja1105_schedule_params_entry_pack,
		.unpack = sja1105_schedule_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_PARAMS_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS] = {
		.pack = sja1105_schedule_entry_points_params_entry_pack,
		.unpack = sja1105_schedule_entry_points_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT,
	},
	[BLK_IDX_VL_FORWARDING_PARAMS] = {
		.pack = sja1105_vl_forwarding_params_entry_pack,
		.unpack = sja1105_vl_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105et_l2_lookup_params_entry_pack,
		.unpack = sja1105et_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105et_avb_params_entry_pack,
		.unpack = sja1105et_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105et_general_params_entry_pack,
		.unpack = sja1105et_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105P: Second generation, no TTEthernet, no SGMII */
const struct sja1105_table_ops sja1105p_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105pqrs_l2_lookup_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105pqrs_mac_config_entry_pack,
		.unpack = sja1105pqrs_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105pqrs_l2_lookup_params_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105pqrs_avb_params_entry_pack,
		.unpack = sja1105pqrs_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105pqrs_general_params_entry_pack,
		.unpack = sja1105pqrs_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105Q: Second generation, TTEthernet, no SGMII */
const struct sja1105_table_ops sja1105q_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_SCHEDULE] = {
		.pack = sja1105_schedule_entry_pack,
		.unpack = sja1105_schedule_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS] = {
		.pack = sja1105_schedule_entry_points_entry_pack,
		.unpack = sja1105_schedule_entry_points_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_COUNT,
	},
	[BLK_IDX_VL_LOOKUP] = {
		.pack = sja1105_vl_lookup_entry_pack,
		.unpack = sja1105_vl_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VL_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_LOOKUP_COUNT,
	},
	[BLK_IDX_VL_POLICING] = {
		.pack = sja1105_vl_policing_entry_pack,
		.unpack = sja1105_vl_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_policing_entry),
		.packed_entry_size = SJA1105_SIZE_VL_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_POLICING_COUNT,
	},
	[BLK_IDX_VL_FORWARDING] = {
		.pack = sja1105_vl_forwarding_entry_pack,
		.unpack = sja1105_vl_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_COUNT,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105pqrs_l2_lookup_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105pqrs_mac_config_entry_pack,
		.unpack = sja1105pqrs_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_SCHEDULE_PARAMS] = {
		.pack = sja1105_schedule_params_entry_pack,
		.unpack = sja1105_schedule_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_PARAMS_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS] = {
		.pack = sja1105_schedule_entry_points_params_entry_pack,
		.unpack = sja1105_schedule_entry_points_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT,
	},
	[BLK_IDX_VL_FORWARDING_PARAMS] = {
		.pack = sja1105_vl_forwarding_params_entry_pack,
		.unpack = sja1105_vl_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105pqrs_l2_lookup_params_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105pqrs_avb_params_entry_pack,
		.unpack = sja1105pqrs_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105pqrs_general_params_entry_pack,
		.unpack = sja1105pqrs_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105R: Second generation, no TTEthernet, SGMII */
const struct sja1105_table_ops sja1105r_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105pqrs_l2_lookup_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105pqrs_mac_config_entry_pack,
		.unpack = sja1105pqrs_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105pqrs_l2_lookup_params_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105pqrs_avb_params_entry_pack,
		.unpack = sja1105pqrs_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105pqrs_general_params_entry_pack,
		.unpack = sja1105pqrs_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105S: Second generation, TTEthernet, SGMII */
const struct sja1105_table_ops sja1105s_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_SCHEDULE] = {
		.pack = sja1105_schedule_entry_pack,
		.unpack = sja1105_schedule_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS] = {
		.pack = sja1105_schedule_entry_points_entry_pack,
		.unpack = sja1105_schedule_entry_points_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_COUNT,
	},
	[BLK_IDX_VL_LOOKUP] = {
		.pack = sja1105_vl_lookup_entry_pack,
		.unpack = sja1105_vl_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VL_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_LOOKUP_COUNT,
	},
	[BLK_IDX_VL_POLICING] = {
		.pack = sja1105_vl_policing_entry_pack,
		.unpack = sja1105_vl_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_policing_entry),
		.packed_entry_size = SJA1105_SIZE_VL_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_POLICING_COUNT,
	},
	[BLK_IDX_VL_FORWARDING] = {
		.pack = sja1105_vl_forwarding_entry_pack,
		.unpack = sja1105_vl_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_COUNT,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1105pqrs_l2_lookup_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1105_l2_policing_entry_pack,
		.unpack = sja1105_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1105_vlan_lookup_entry_pack,
		.unpack = sja1105_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1105_l2_forwarding_entry_pack,
		.unpack = sja1105_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1105pqrs_mac_config_entry_pack,
		.unpack = sja1105pqrs_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_SCHEDULE_PARAMS] = {
		.pack = sja1105_schedule_params_entry_pack,
		.unpack = sja1105_schedule_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_PARAMS_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS] = {
		.pack = sja1105_schedule_entry_points_params_entry_pack,
		.unpack = sja1105_schedule_entry_points_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT,
	},
	[BLK_IDX_VL_FORWARDING_PARAMS] = {
		.pack = sja1105_vl_forwarding_params_entry_pack,
		.unpack = sja1105_vl_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1105pqrs_l2_lookup_params_entry_pack,
		.unpack = sja1105pqrs_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1105_l2_forwarding_params_entry_pack,
		.unpack = sja1105_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105pqrs_avb_params_entry_pack,
		.unpack = sja1105pqrs_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1105pqrs_general_params_entry_pack,
		.unpack = sja1105pqrs_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1105_retagging_entry_pack,
		.unpack = sja1105_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1105_xmii_params_entry_pack,
		.unpack = sja1105_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1110A: Third generation */
const struct sja1105_table_ops sja1110_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_SCHEDULE] = {
		.pack = sja1110_schedule_entry_pack,
		.unpack = sja1110_schedule_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry),
		.packed_entry_size = SJA1110_SIZE_SCHEDULE_ENTRY,
		.max_entry_count = SJA1110_MAX_SCHEDULE_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS] = {
		.pack = sja1110_schedule_entry_points_entry_pack,
		.unpack = sja1110_schedule_entry_points_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_entry),
		.packed_entry_size = SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_COUNT,
	},
	[BLK_IDX_VL_LOOKUP] = {
		.pack = sja1110_vl_lookup_entry_pack,
		.unpack = sja1110_vl_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VL_LOOKUP_ENTRY,
		.max_entry_count = SJA1110_MAX_VL_LOOKUP_COUNT,
	},
	[BLK_IDX_VL_POLICING] = {
		.pack = sja1110_vl_policing_entry_pack,
		.unpack = sja1110_vl_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_policing_entry),
		.packed_entry_size = SJA1105_SIZE_VL_POLICING_ENTRY,
		.max_entry_count = SJA1110_MAX_VL_POLICING_COUNT,
	},
	[BLK_IDX_VL_FORWARDING] = {
		.pack = sja1110_vl_forwarding_entry_pack,
		.unpack = sja1110_vl_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_ENTRY,
		.max_entry_count = SJA1110_MAX_VL_FORWARDING_COUNT,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.pack = sja1110_l2_lookup_entry_pack,
		.unpack = sja1110_l2_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1110_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.pack = sja1110_l2_policing_entry_pack,
		.unpack = sja1110_l2_policing_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1110_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.pack = sja1110_vlan_lookup_entry_pack,
		.unpack = sja1110_vlan_lookup_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1110_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.pack = sja1110_l2_forwarding_entry_pack,
		.unpack = sja1110_l2_forwarding_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1110_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.pack = sja1110_mac_config_entry_pack,
		.unpack = sja1110_mac_config_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1110_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_SCHEDULE_PARAMS] = {
		.pack = sja1110_schedule_params_entry_pack,
		.unpack = sja1110_schedule_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_PARAMS_COUNT,
	},
	[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS] = {
		.pack = sja1105_schedule_entry_points_params_entry_pack,
		.unpack = sja1105_schedule_entry_points_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_schedule_entry_points_params_entry),
		.packed_entry_size = SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT,
	},
	[BLK_IDX_VL_FORWARDING_PARAMS] = {
		.pack = sja1110_vl_forwarding_params_entry_pack,
		.unpack = sja1110_vl_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_vl_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_VL_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.pack = sja1110_l2_lookup_params_entry_pack,
		.unpack = sja1110_l2_lookup_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1110_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.pack = sja1110_l2_forwarding_params_entry_pack,
		.unpack = sja1110_l2_forwarding_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.pack = sja1105pqrs_avb_params_entry_pack,
		.unpack = sja1105pqrs_avb_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.pack = sja1110_general_params_entry_pack,
		.unpack = sja1110_general_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1110_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_RETAGGING] = {
		.pack = sja1110_retagging_entry_pack,
		.unpack = sja1110_retagging_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_retagging_entry),
		.packed_entry_size = SJA1105_SIZE_RETAGGING_ENTRY,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.pack = sja1110_xmii_params_entry_pack,
		.unpack = sja1110_xmii_params_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1110_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
	[BLK_IDX_PCP_REMAPPING] = {
		.pack = sja1110_pcp_remapping_entry_pack,
		.unpack = sja1110_pcp_remapping_entry_unpack,
		.unpacked_entry_size = sizeof(struct sja1110_pcp_remapping_entry),
		.packed_entry_size = SJA1110_SIZE_PCP_REMAPPING_ENTRY,
		.max_entry_count = SJA1110_MAX_PCP_REMAPPING_COUNT,
	},
};

int sja1105_static_config_init(struct sja1105_static_config *config,
			       const struct sja1105_table_ops *static_ops,
			       u64 device_id)
{
	enum sja1105_blk_idx i;

	*config = (struct sja1105_static_config) {0};

	/* Transfer static_ops array from priv into per-table ops
	 * for handier access
	 */
	for (i = 0; i < BLK_IDX_MAX; i++)
		config->tables[i].ops = &static_ops[i];

	config->device_id = device_id;
	return 0;
}

void sja1105_static_config_free(struct sja1105_static_config *config)
{
	enum sja1105_blk_idx i;

	for (i = 0; i < BLK_IDX_MAX; i++) {
		if (config->tables[i].entry_count) {
			kfree(config->tables[i].entries);
			config->tables[i].entry_count = 0;
		}
	}
}

int sja1105_table_delete_entry(struct sja1105_table *table, int i)
{
	size_t entry_size = table->ops->unpacked_entry_size;
	u8 *entries = table->entries;

	if (i > table->entry_count)
		return -ERANGE;

	memmove(entries + i * entry_size, entries + (i + 1) * entry_size,
		(table->entry_count - i) * entry_size);

	table->entry_count--;

	return 0;
}

/* No pointers to table->entries should be kept when this is called. */
int sja1105_table_resize(struct sja1105_table *table, size_t new_count)
{
	size_t entry_size = table->ops->unpacked_entry_size;
	void *new_entries, *old_entries = table->entries;

	if (new_count > table->ops->max_entry_count)
		return -ERANGE;

	new_entries = kcalloc(new_count, entry_size, GFP_KERNEL);
	if (!new_entries)
		return -ENOMEM;

	memcpy(new_entries, old_entries, min(new_count, table->entry_count) *
		entry_size);

	table->entries = new_entries;
	table->entry_count = new_count;
	kfree(old_entries);
	return 0;
}
