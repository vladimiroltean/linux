// SPDX-License-Identifier: GPL-2.0
/* Microchip KSZ9477 switch driver PTP routines
 *
 * Author: Christian Eggers <ceggers@arri.de>
 *
 * Copyright (c) 2020 ARRI Lighting
 */

#include <linux/ptp_clock_kernel.h>

#include "ksz_common.h"
#include "ksz9477_reg.h"

#include "ksz9477_ptp.h"

#define KSZ_PTP_INC_NS 40  /* HW clock is incremented every 40 ns (by 40) */
#define KSZ_PTP_SUBNS_BITS 32  /* Number of bits in sub-nanoseconds counter */

/* Posix clock support */

static int ksz9477_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	u16 data16;
	int ret;

	mutex_lock(&dev->ptp_mutex);

	if (scaled_ppm) {
		s64 ppb, adj;
		u32 data32;

		/* basic calculation:
		 * s32 ppb = scaled_ppm_to_ppb(scaled_ppm);
		 * s64 adj = div_s64(((s64)ppb * KSZ_PTP_INC_NS) << KSZ_PTP_SUBNS_BITS,
		 *                   NSEC_PER_SEC);
		 */

		/* More precise calculation (avoids shifting out precision).
		 * See scaled_ppm_to_ppb() in ptp_clock.c for details.
		 */
		ppb = 1 + scaled_ppm;
		ppb *= 125;
		ppb *= KSZ_PTP_INC_NS;
		ppb <<= KSZ_PTP_SUBNS_BITS - 13;
		adj = div_s64(ppb, NSEC_PER_SEC);

		data32 = abs(adj);
		data32 &= BIT_MASK(30) - 1;
		if (adj >= 0)
			data32 |= PTP_RATE_DIR;

		ret = ksz_write32(dev, REG_PTP_SUBNANOSEC_RATE, data32);
		if (ret)
			goto error_return;
	}

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		goto error_return;

	if (scaled_ppm)
		data16 |= PTP_CLK_ADJ_ENABLE;
	else
		data16 &= ~PTP_CLK_ADJ_ENABLE;

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);
	if (ret)
		goto error_return;

error_return:
	mutex_unlock(&dev->ptp_mutex);
	return ret;
}

static int ksz9477_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	s32 sec, nsec;
	u16 data16;
	int ret;

	mutex_lock(&dev->ptp_mutex);

	/* Do not use ns_to_timespec64(), both sec and nsec are subtracted by
	 * hardware.
	 */
	sec = div_s64_rem(delta, NSEC_PER_SEC, &nsec);

	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, abs(nsec));
	if (ret)
		goto error_return;

	/* Contradictory to the data sheet, seconds are also considered.  */
	ret = ksz_write32(dev, REG_PTP_RTC_SEC, abs(sec));
	if (ret)
		goto error_return;

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		goto error_return;

	data16 |= PTP_STEP_ADJ;
	if (delta < 0)
		data16 &= ~PTP_STEP_DIR;  /* 0: subtract */
	else
		data16 |= PTP_STEP_DIR;   /* 1: add */

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);
	if (ret)
		goto error_return;

error_return:
	mutex_unlock(&dev->ptp_mutex);
	return ret;
}

static int _ksz9477_ptp_gettime(struct ksz_device *dev, struct timespec64 *ts)
{
	u32 nanoseconds;
	u32 seconds;
	u16 data16;
	u8 phase;
	int ret;

	/* Copy current PTP clock into shadow registers */
	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		return ret;

	data16 |= PTP_READ_TIME;

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);
	if (ret)
		return ret;

	/* Read from shadow registers */
	ret = ksz_read8(dev, REG_PTP_RTC_SUB_NANOSEC__2, &phase);
	if (ret)
		return ret;
	ret = ksz_read32(dev, REG_PTP_RTC_NANOSEC, &nanoseconds);
	if (ret)
		return ret;
	ret = ksz_read32(dev, REG_PTP_RTC_SEC, &seconds);
	if (ret)
		return ret;

	ts->tv_sec = seconds;
	ts->tv_nsec = nanoseconds + phase * 8;

	return 0;
}

static int ksz9477_ptp_gettime(struct ptp_clock_info *ptp,
			       struct timespec64 *ts)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	int ret;

	mutex_lock(&dev->ptp_mutex);
	ret = _ksz9477_ptp_gettime(dev, ts);
	mutex_unlock(&dev->ptp_mutex);

	return ret;
}

static int ksz9477_ptp_settime(struct ptp_clock_info *ptp,
			       struct timespec64 const *ts)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	u16 data16;
	int ret;

	mutex_lock(&dev->ptp_mutex);

	/* Write to shadow registers */

	/* clock phase */
	ret = ksz_read16(dev, REG_PTP_RTC_SUB_NANOSEC__2, &data16);
	if (ret)
		goto error_return;

	data16 &= ~PTP_RTC_SUB_NANOSEC_M;

	ret = ksz_write16(dev, REG_PTP_RTC_SUB_NANOSEC__2, data16);
	if (ret)
		goto error_return;

	/* nanoseconds */
	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, ts->tv_nsec);
	if (ret)
		goto error_return;

	/* seconds */
	ret = ksz_write32(dev, REG_PTP_RTC_SEC, ts->tv_sec);
	if (ret)
		goto error_return;

	/* Load PTP clock from shadow registers */
	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		goto error_return;

	data16 |= PTP_LOAD_TIME;

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);
	if (ret)
		goto error_return;

error_return:
	mutex_unlock(&dev->ptp_mutex);
	return ret;
}

static int ksz9477_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *req, int on)
{
	return -EOPNOTSUPP;
}

static int ksz9477_ptp_start_clock(struct ksz_device *dev)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data);
	if (ret)
		return ret;

	/* Perform PTP clock reset */
	data |= PTP_CLK_RESET;
	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data);
	if (ret)
		return ret;
	data &= ~PTP_CLK_RESET;

	/* Enable PTP clock */
	data |= PTP_CLK_ENABLE;
	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data);
	if (ret)
		return ret;

	return 0;
}

static int ksz9477_ptp_stop_clock(struct ksz_device *dev)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data);
	if (ret)
		return ret;

	/* Disable PTP clock */
	data &= ~PTP_CLK_ENABLE;
	return ksz_write16(dev, REG_PTP_CLK_CTRL, data);
}

int ksz9477_ptp_init(struct ksz_device *dev)
{
	int ret;

	mutex_init(&dev->ptp_mutex);

	/* PTP clock properties */

	dev->ptp_caps.owner = THIS_MODULE;
	snprintf(dev->ptp_caps.name, sizeof(dev->ptp_caps.name),
		 dev_name(dev->dev));

	/* Sub-nanoseconds-adj,max * sub-nanoseconds / 40ns * 1ns
	 * = (2^30-1) * (2 ^ 32) / 40 ns * 1 ns = 6249999
	 */
	dev->ptp_caps.max_adj     = 6249999;
	dev->ptp_caps.n_alarm     = 0;
	dev->ptp_caps.n_ext_ts    = 0;  /* currently not implemented */
	dev->ptp_caps.n_per_out   = 0;
	dev->ptp_caps.pps         = 0;
	dev->ptp_caps.adjfine     = ksz9477_ptp_adjfine;
	dev->ptp_caps.adjtime     = ksz9477_ptp_adjtime;
	dev->ptp_caps.gettime64   = ksz9477_ptp_gettime;
	dev->ptp_caps.settime64   = ksz9477_ptp_settime;
	dev->ptp_caps.enable      = ksz9477_ptp_enable;

	/* Start hardware counter (will overflow after 136 years) */
	ret = ksz9477_ptp_start_clock(dev);
	if (ret)
		return ret;

	dev->ptp_clock = ptp_clock_register(&dev->ptp_caps, dev->dev);
	if (IS_ERR(dev->ptp_clock)) {
		ret = PTR_ERR(dev->ptp_clock);
		goto error_stop_clock;
	}

	return 0;

error_stop_clock:
	ksz9477_ptp_stop_clock(dev);
	return ret;
}

void ksz9477_ptp_deinit(struct ksz_device *dev)
{
	ptp_clock_unregister(dev->ptp_clock);
	ksz9477_ptp_stop_clock(dev);
}
