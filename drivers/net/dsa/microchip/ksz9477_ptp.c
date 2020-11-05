// SPDX-License-Identifier: GPL-2.0
/* Microchip KSZ9477 switch driver PTP routines
 *
 * Author: Christian Eggers <ceggers@arri.de>
 *
 * Copyright (c) 2020 ARRI Lighting
 */

#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/sysfs.h>

#include "ksz_common.h"
#include "ksz9477_reg.h"

#include "ksz9477_ptp.h"

#define KSZ_PTP_INC_NS 40  /* HW clock is incremented every 40 ns (by 40) */
#define KSZ_PTP_SUBNS_BITS 32  /* Number of bits in sub-nanoseconds counter */

/* Shared register access routines (Trigger Output Unit) */

static int ksz9477_ptp_tou_reset(struct ksz_device *dev, unsigned int unit)
{
	u32 ctrl_stat, data;
	int ret;

	/* Reset trigger unit (clears TRIGGER_EN, but not GPIOSTATx) */
	ret = ksz_read32(dev, REG_PTP_CTRL_STAT__4, &ctrl_stat);
	if (ret)
		return ret;

	ctrl_stat |= TRIG_RESET;

	ret = ksz_write32(dev, REG_PTP_CTRL_STAT__4, ctrl_stat);
	if (ret)
		return ret;

	/* Clear DONE */
	data = 1 << (unit + TRIG_DONE_S);
	ret = ksz_write32(dev, REG_PTP_TRIG_STATUS__4, data);
	if (ret)
		return ret;

	/* Clear IRQ */
	data = 1 << (unit + TRIG_INT_S);
	ret = ksz_write32(dev, REG_PTP_INT_STATUS__4, data);
	if (ret)
		return ret;

	/* Clear reset and set GPIO direction */
	ctrl_stat &= ~TRIG_ENABLE;  /* clear cached bit :-) */
	ctrl_stat &= ~TRIG_RESET;

	ret = ksz_write32(dev, REG_PTP_CTRL_STAT__4, ctrl_stat);
	if (ret)
		return ret;

	return 0;
}

static int ksz9477_ptp_tou_cycle_width_set(struct ksz_device *dev, u32 width_ns)
{
	int ret;

	ret = ksz_write32(dev, REG_TRIG_CYCLE_WIDTH, width_ns);
	if (ret)
		return ret;

	return 0;
}

static int ksz9477_ptp_tou_cycle_count_set(struct ksz_device *dev, u16 count)
{
	u32 data;
	int ret;

	ret = ksz_read32(dev, REG_TRIG_CYCLE_CNT, &data);
	if (ret)
		return ret;

	data &= ~(TRIG_CYCLE_CNT_M << TRIG_CYCLE_CNT_S);
	data |= (count & TRIG_CYCLE_CNT_M) << TRIG_CYCLE_CNT_S;

	ret = ksz_write32(dev, REG_TRIG_CYCLE_CNT, data);
	if (ret)
		return ret;

	return 0;
}

static int ksz9477_ptp_tou_pulse_verify(u64 pulse_ns)
{
	u32 data;

	if (pulse_ns & 0x3)
		return -EINVAL;

	data = (pulse_ns / 8);
	if (data != (data & TRIG_PULSE_WIDTH_M))
		return -ERANGE;

	return 0;
}

static int ksz9477_ptp_tou_pulse_set(struct ksz_device *dev, u32 pulse_ns)
{
	u32 data;

	data = (pulse_ns / 8);

	return ksz_write32(dev, REG_TRIG_PULSE_WIDTH__4, data);
}

static int ksz9477_ptp_tou_target_time_set(struct ksz_device *dev, struct timespec64 const *ts)
{
	int ret;

	/* Hardware has only 32 bit */
	if ((ts->tv_sec & 0xffffffff) != ts->tv_sec)
		return -EINVAL;

	ret = ksz_write32(dev, REG_TRIG_TARGET_NANOSEC, ts->tv_nsec);
	if (ret)
		return ret;

	ret = ksz_write32(dev, REG_TRIG_TARGET_SEC, ts->tv_sec);
	if (ret)
		return ret;

	return 0;
}

static int ksz9477_ptp_tou_start(struct ksz_device *dev, u32 *ctrl_stat_)
{
	u32 ctrl_stat;
	int ret;

	ret = ksz_read32(dev, REG_PTP_CTRL_STAT__4, &ctrl_stat);
	if (ret)
		return ret;

	ctrl_stat |= TRIG_ENABLE;

	ret = ksz_write32(dev, REG_PTP_CTRL_STAT__4, ctrl_stat);
	if (ret)
		return ret;

	if (ctrl_stat_)
		*ctrl_stat_ = ctrl_stat;

	return 0;
}

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

static int ksz9477_ptp_restart_perout(struct ksz_device *dev);

static int ksz9477_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
	struct timespec64 delta64 = ns_to_timespec64(delta);
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

	switch (dev->ptp_tou_mode) {
	case KSZ_PTP_TOU_IDLE:
		break;

	case KSZ_PTP_TOU_PEROUT:
		dev_info(dev->dev, "Restarting periodic output signal\n");

		ret = ksz9477_ptp_restart_perout(dev);
		if (ret)
			goto error_return;

		break;
	}

	spin_lock_bh(&ptp_shared->ptp_clock_lock);
	ptp_shared->ptp_clock_time = timespec64_add(ptp_shared->ptp_clock_time,
						    delta64);
	spin_unlock_bh(&ptp_shared->ptp_clock_lock);

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
	struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
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

	switch (dev->ptp_tou_mode) {
	case KSZ_PTP_TOU_IDLE:
		break;

	case KSZ_PTP_TOU_PEROUT:
		dev_info(dev->dev, "Restarting periodic output signal\n");

		ret = ksz9477_ptp_restart_perout(dev);
		if (ret)
			goto error_return;

		break;
	}

	spin_lock_bh(&ptp_shared->ptp_clock_lock);
	ptp_shared->ptp_clock_time = *ts;
	spin_unlock_bh(&ptp_shared->ptp_clock_lock);

error_return:
	mutex_unlock(&dev->ptp_mutex);
	return ret;
}

static int ksz9477_ptp_configure_perout(struct ksz_device *dev,
					u32 cycle_width_ns, u16 cycle_count,
					u32 pulse_width_ns,
					struct timespec64 const *target_time)
{
	u32 trig_ctrl;
	int ret;

	/* Enable notify, set rising edge, set periodic pattern */
	trig_ctrl = TRIG_NOTIFY | (TRIG_POS_PERIOD << TRIG_PATTERN_S);
	ret = ksz_write32(dev, REG_TRIG_CTRL__4, trig_ctrl);
	if (ret)
		return ret;

	ret = ksz9477_ptp_tou_cycle_width_set(dev, cycle_width_ns);
	if (ret)
		return ret;

	ksz9477_ptp_tou_cycle_count_set(dev,  cycle_count);
	if (ret)
		return ret;

	ret = ksz9477_ptp_tou_pulse_set(dev, pulse_width_ns);
	if (ret)
		return ret;

	ret = ksz9477_ptp_tou_target_time_set(dev, target_time);
	if (ret)
		return ret;

	return 0;
}

#define KSZ9477_PEROUT_VALID_FLAGS ( \
	PTP_PEROUT_DUTY_CYCLE \
)

static int ksz9477_ptp_enable_perout(struct ksz_device *dev,
				     struct ptp_perout_request const *perout_request,
				     int on)
{
	u64 cycle_width_ns;
	u64 pulse_width_ns;
	u32 gpio_stat0;
	int ret;

	if (perout_request->flags & ~KSZ9477_PEROUT_VALID_FLAGS)
		return -EINVAL;

	if (dev->ptp_tou_mode != KSZ_PTP_TOU_PEROUT &&
	    dev->ptp_tou_mode != KSZ_PTP_TOU_IDLE)
		return -EBUSY;

	ret = ksz9477_ptp_tou_reset(dev, 0);
	if (ret)
		return ret;

	if (!on) {
		dev->ptp_tou_mode = KSZ_PTP_TOU_IDLE;
		return 0;  /* success */
	}

	dev->ptp_perout_target_time_first.tv_sec  = perout_request->start.sec;
	dev->ptp_perout_target_time_first.tv_nsec = perout_request->start.nsec;

	dev->ptp_perout_period.tv_sec = perout_request->period.sec;
	dev->ptp_perout_period.tv_nsec = perout_request->period.nsec;

	cycle_width_ns = timespec64_to_ns(&dev->ptp_perout_period);
	if ((cycle_width_ns & GENMASK(31, 0)) != cycle_width_ns)
		return -EINVAL;

	if (perout_request->flags & PTP_PEROUT_DUTY_CYCLE)
		pulse_width_ns = perout_request->on.sec * NSEC_PER_SEC +
				 perout_request->on.nsec;

	else
		/* Use a duty cycle of 50%. Maximum pulse width supported by the
		 * hardware is a little bit more than 125 ms.
		 */
		pulse_width_ns = min_t(u64,
				       (perout_request->period.sec * NSEC_PER_SEC
					+ perout_request->period.nsec) / 2
				       / 8 * 8,
				       125000000LL);

	ret = ksz9477_ptp_tou_pulse_verify(pulse_width_ns);
	if (ret)
		return ret;

	dev->ptp_perout_pulse_width_ns = pulse_width_ns;

	ret = ksz9477_ptp_configure_perout(dev, cycle_width_ns,
					   dev->ptp_perout_cycle_count,
					   dev->ptp_perout_pulse_width_ns,
					   &dev->ptp_perout_target_time_first);
	if (ret)
		return ret;

	/* Activate trigger unit */
	ret = ksz9477_ptp_tou_start(dev, NULL);
	if (ret)
		return ret;

	/* Check error flag:
	 * - the ACTIVE flag is NOT cleared an error!
	 */
	ret = ksz_read32(dev, REG_PTP_TRIG_STATUS__4, &gpio_stat0);
	if (ret)
		return ret;

	if (gpio_stat0 & (1 << (0 + TRIG_ERROR_S))) {
		dev_err(dev->dev, "%s: Trigger unit0 error!\n", __func__);
		ret = -EIO;
		/* Unit will be reset on next access */
		return ret;
	}

	dev->ptp_tou_mode = KSZ_PTP_TOU_PEROUT;
	return 0;
}

static int ksz9477_ptp_restart_perout(struct ksz_device *dev)
{
	s64 now_ns, first_ns, period_ns, next_ns;
	struct timespec64 now;
	unsigned int count;
	int ret;

	ret = _ksz9477_ptp_gettime(dev, &now);
	if (ret)
		return ret;

	now_ns = timespec64_to_ns(&now);
	first_ns = timespec64_to_ns(&dev->ptp_perout_target_time_first);

	/* Calculate next perout event based on start time and period */
	period_ns = timespec64_to_ns(&dev->ptp_perout_period);

	if (first_ns < now_ns) {
		count = div_u64(now_ns - first_ns, period_ns);
		next_ns = first_ns + count * period_ns;
	} else {
		next_ns = first_ns;
	}

	/* Ensure 100 ms guard time prior next event */
	while (next_ns < now_ns + 100000000)
		next_ns += period_ns;

	/* Restart periodic output signal */
	{
		struct timespec64 next = ns_to_timespec64(next_ns);
		struct ptp_perout_request perout_request = {
			.start = {
				.sec  = next.tv_sec,
				.nsec = next.tv_nsec
			},
			.period = {
				.sec  = dev->ptp_perout_period.tv_sec,
				.nsec = dev->ptp_perout_period.tv_nsec
			},
			.index = 0,
			.flags = 0,  /* keep current values */
		};
		ret = ksz9477_ptp_enable_perout(dev, &perout_request, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int ksz9477_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *req, int on)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	int ret;

	switch (req->type) {
	case PTP_CLK_REQ_PEROUT: {
		struct ptp_perout_request const *perout_request = &req->perout;

		mutex_lock(&dev->ptp_mutex);
		ret = ksz9477_ptp_enable_perout(dev, perout_request, on);
		mutex_unlock(&dev->ptp_mutex);
		return ret;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static long ksz9477_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct ksz_device *dev = container_of(ptp, struct ksz_device, ptp_caps);
	struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
	struct timespec64 ts;

	mutex_lock(&dev->ptp_mutex);
	_ksz9477_ptp_gettime(dev, &ts);
	mutex_unlock(&dev->ptp_mutex);

	spin_lock_bh(&ptp_shared->ptp_clock_lock);
	ptp_shared->ptp_clock_time = ts;
	spin_unlock_bh(&ptp_shared->ptp_clock_lock);

	return HZ;  /* reschedule in 1 second */
}

static int ksz9477_ptp_start_clock(struct ksz_device *dev)
{
	struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
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

	spin_lock_bh(&ptp_shared->ptp_clock_lock);
	ptp_shared->ptp_clock_time.tv_sec = 0;
	ptp_shared->ptp_clock_time.tv_nsec = 0;
	spin_unlock_bh(&ptp_shared->ptp_clock_lock);

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

/* Time stamping support */

static int ksz9477_ptp_enable_mode(struct ksz_device *dev, bool enable)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_MSG_CONF1, &data);
	if (ret)
		return ret;

	/* Setting the PTP_802_1AS bit disables forwarding of PDelay_Req and
	 * PDelay_Resp messages. These messages must not be forwarded in
	 * Boundary Clock mode.
	 */
	if (enable)
		data |= PTP_ENABLE | PTP_802_1AS;
	else
		data &= ~PTP_ENABLE;

	ret = ksz_write16(dev, REG_PTP_MSG_CONF1, data);
	if (ret)
		return ret;

	if (enable) {
		/* Schedule cyclic call of ksz_ptp_do_aux_work() */
		ret = ptp_schedule_worker(dev->ptp_clock, 0);
		if (ret)
			goto error_disable_mode;
	} else {
		ptp_cancel_worker_sync(dev->ptp_clock);
	}

	return 0;

error_disable_mode:
	ksz_write16(dev, REG_PTP_MSG_CONF1, data & ~PTP_ENABLE);
	return ret;
}

static int ksz9477_ptp_enable_port_ptp_interrupts(struct ksz_device *dev,
						  int port, bool enable)
{
	u32 addr = PORT_CTRL_ADDR(port, REG_PORT_INT_MASK);
	u8 data;
	int ret;

	ret = ksz_read8(dev, addr, &data);
	if (ret)
		return ret;

	/* PORT_PTP_INT bit is active low */
	if (enable)
		data &= ~PORT_PTP_INT;
	else
		data |= PORT_PTP_INT;

	return ksz_write8(dev, addr, data);
}

static int ksz9477_ptp_enable_port_egress_interrupts(struct ksz_device *dev,
						     int port, bool enable)
{
	u32 addr = PORT_CTRL_ADDR(port, REG_PTP_PORT_TX_INT_ENABLE__2);
	u16 data;
	int ret;

	ret = ksz_read16(dev, addr, &data);
	if (ret)
		return ret;

	/* PTP_PORT_XDELAY_REQ_INT is high active */
	if (enable)
		data |= PTP_PORT_XDELAY_REQ_INT;
	else
		data &= PTP_PORT_XDELAY_REQ_INT;

	return ksz_write16(dev, addr, data);
}

static void ksz9477_ptp_txtstamp_skb(struct ksz_device *dev,
				     struct ksz_port *prt, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps hwtstamps = {};
	int ret;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	/* timeout must include tstamp latency, IRQ latency and time for
	 * reading the time stamp via I2C.
	 */
	ret = wait_for_completion_timeout(&prt->tstamp_completion,
					  msecs_to_jiffies(100));
	if (!ret) {
		dev_err(dev->dev, "timeout waiting for time stamp\n");
		return;
	}
	hwtstamps.hwtstamp = prt->tstamp_xdelay;
	skb_complete_tx_timestamp(skb, &hwtstamps);
}

#define work_to_port(work) \
		container_of((work), struct ksz_port_ptp_shared, xmit_work)
#define ptp_shared_to_ksz_port(t) \
		container_of((t), struct ksz_port, ptp_shared)
#define ptp_shared_to_ksz_device(t) \
		container_of((t), struct ksz_device, ptp_shared)

/* Deferred work is necessary for time stamped PDelay_Req messages. This cannot
 * be done from atomic context as we have to wait for the hardware interrupt.
 */
static void ksz9477_port_deferred_xmit(struct kthread_work *work)
{
	struct ksz_port_ptp_shared *prt_ptp_shared = work_to_port(work);
	struct ksz_port *prt = ptp_shared_to_ksz_port(prt_ptp_shared);
	struct ksz_device_ptp_shared *ptp_shared = prt_ptp_shared->dev;
	struct ksz_device *dev = ptp_shared_to_ksz_device(ptp_shared);
	int port = prt - dev->ports;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&prt_ptp_shared->xmit_queue)) != NULL) {
		struct sk_buff *clone = DSA_SKB_CB(skb)->clone;

		reinit_completion(&prt->tstamp_completion);

		/* Transfer skb to the host port. */
		dsa_enqueue_skb(skb, dsa_to_port(dev->ds, port)->slave);

		ksz9477_ptp_txtstamp_skb(dev, prt, clone);
	}
}

static int ksz9477_ptp_port_init(struct ksz_device *dev, int port)
{
	struct ksz_port *prt = &dev->ports[port];
	struct ksz_port_ptp_shared *ptp_shared = &prt->ptp_shared;
	struct dsa_port *dp = dsa_to_port(dev->ds, port);
	int ret;

	if (port == dev->cpu_port)
		return 0;

	/* Set rx and tx latency to 0 (will be handled by user space) */
	ret = ksz_write16(dev, PORT_CTRL_ADDR(port, REG_PTP_PORT_RX_DELAY__2),
			  0);
	if (ret)
		return ret;

	ret = ksz_write16(dev, PORT_CTRL_ADDR(port, REG_PTP_PORT_TX_DELAY__2),
			  0);
	if (ret)
		return ret;

	ret = ksz9477_ptp_enable_port_ptp_interrupts(dev, port, true);
	if (ret)
		return ret;

	ret = ksz9477_ptp_enable_port_egress_interrupts(dev, port, true);
	if (ret)
		goto error_disable_port_ptp_interrupts;

	/* ksz_port::ptp_shared is used in tagging driver */
	ptp_shared->dev = &dev->ptp_shared;
	dp->priv = ptp_shared;

	/* PDelay_Req messages require deferred transmit as the time
	 * stamp unit provides no sequenceId or similar.  So we must
	 * wait for the time stamp interrupt.
	 */
	init_completion(&prt->tstamp_completion);
	kthread_init_work(&ptp_shared->xmit_work,
			  ksz9477_port_deferred_xmit);
	ptp_shared->xmit_worker = kthread_create_worker(0, "%s_xmit",
							dp->slave->name);
	if (IS_ERR(ptp_shared->xmit_worker)) {
		ret = PTR_ERR(ptp_shared->xmit_worker);
		dev_err(dev->dev,
			"failed to create deferred xmit thread: %d\n", ret);
		goto error_disable_port_egress_interrupts;
	}
	skb_queue_head_init(&ptp_shared->xmit_queue);

	return 0;

error_disable_port_egress_interrupts:
	ksz9477_ptp_enable_port_egress_interrupts(dev, port, false);
error_disable_port_ptp_interrupts:
	ksz9477_ptp_enable_port_ptp_interrupts(dev, port, false);
	return ret;
}

static void ksz9477_ptp_port_deinit(struct ksz_device *dev, int port)
{
	struct ksz_port_ptp_shared *ptp_shared = &dev->ports[port].ptp_shared;

	if (port == dev->cpu_port)
		return;

	kthread_destroy_worker(ptp_shared->xmit_worker);
	ksz9477_ptp_enable_port_egress_interrupts(dev, port, false);
	ksz9477_ptp_enable_port_ptp_interrupts(dev, port, false);
}

static int ksz9477_ptp_ports_init(struct ksz_device *dev)
{
	int port;
	int ret;

	for (port = 0; port < dev->port_cnt; port++) {
		ret = ksz9477_ptp_port_init(dev, port);
		if (ret)
			goto error_deinit;
	}

	return 0;

error_deinit:
	while (port-- > 0)
		ksz9477_ptp_port_deinit(dev, port);
	return ret;
}

static void ksz9477_ptp_ports_deinit(struct ksz_device *dev)
{
	int port;

	for (port = 0; port < dev->port_cnt; port++)
		ksz9477_ptp_port_deinit(dev, port);
}

/* device attributes */

enum ksz9477_ptp_tcmode {
	KSZ9477_PTP_TCMODE_E2E,
	KSZ9477_PTP_TCMODE_P2P,
};

static int ksz9477_ptp_tcmode_get(struct ksz_device *dev, enum ksz9477_ptp_tcmode *tcmode)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_MSG_CONF1, &data);
	if (ret)
		return ret;

	*tcmode = (data & PTP_TC_P2P) ? KSZ9477_PTP_TCMODE_P2P : KSZ9477_PTP_TCMODE_E2E;

	return 0;
}

static int ksz9477_ptp_tcmode_set(struct ksz_device *dev,
				  enum ksz9477_ptp_tcmode tcmode)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_MSG_CONF1, &data);
	if (ret)
		return ret;

	if (tcmode == KSZ9477_PTP_TCMODE_P2P)
		data |= PTP_TC_P2P;
	else
		data &= ~PTP_TC_P2P;

	return ksz_write16(dev, REG_PTP_MSG_CONF1, data);
}

static ssize_t tcmode_show(struct device *dev, struct device_attribute *attr __always_unused,
			   char *buf)
{
	struct ksz_device *ksz = dev_get_drvdata(dev);
	enum ksz9477_ptp_tcmode tcmode;
	int ret = ksz9477_ptp_tcmode_get(ksz, &tcmode);

	if (ret)
		return ret;

	return sprintf(buf, "%s\n", tcmode == KSZ9477_PTP_TCMODE_P2P ? "P2P" : "E2E");
}

static ssize_t tcmode_store(struct device *dev, struct device_attribute *attr __always_unused,
			    char const *buf, size_t count)
{
	struct ksz_device *ksz = dev_get_drvdata(dev);
	int ret;

	if (strcasecmp(buf, "E2E") == 0)
		ret = ksz9477_ptp_tcmode_set(ksz, KSZ9477_PTP_TCMODE_E2E);
	else if (strcasecmp(buf, "P2P") == 0)
		ret = ksz9477_ptp_tcmode_set(ksz, KSZ9477_PTP_TCMODE_P2P);
	else
		return -EINVAL;

	return ret ? ret : (ssize_t)count;
}

static DEVICE_ATTR_RW(tcmode);

enum ksz9477_ptp_ocmode {
	KSZ9477_PTP_OCMODE_SLAVE,
	KSZ9477_PTP_OCMODE_MASTER,
};

static int ksz9477_ptp_ocmode_get(struct ksz_device *dev, enum ksz9477_ptp_ocmode *ocmode)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_MSG_CONF1, &data);
	if (ret)
		return ret;

	*ocmode = (data & PTP_MASTER) ? KSZ9477_PTP_OCMODE_MASTER : KSZ9477_PTP_OCMODE_SLAVE;

	return 0;
}

static int ksz9477_ptp_ocmode_set(struct ksz_device *dev,
				  enum ksz9477_ptp_ocmode ocmode)
{
	u16 data;
	int ret;

	ret = ksz_read16(dev, REG_PTP_MSG_CONF1, &data);
	if (ret)
		return ret;

	if (ocmode == KSZ9477_PTP_OCMODE_MASTER)
		data |= PTP_MASTER;
	else
		data &= ~PTP_MASTER;

	return ksz_write16(dev, REG_PTP_MSG_CONF1, data);
}

static ssize_t ocmode_show(struct device *dev, struct device_attribute *attr __always_unused,
			   char *buf)
{
	struct ksz_device *ksz = dev_get_drvdata(dev);
	enum ksz9477_ptp_ocmode ocmode;
	int ret = ksz9477_ptp_ocmode_get(ksz, &ocmode);

	if (ret)
		return ret;

	return sprintf(buf, "%s\n", ocmode == KSZ9477_PTP_OCMODE_MASTER ? "master" : "slave");
}

static ssize_t ocmode_store(struct device *dev, struct device_attribute *attr __always_unused,
			    char const *buf, size_t count)
{
	struct ksz_device *ksz = dev_get_drvdata(dev);
	int ret;

	if (strcasecmp(buf, "master") == 0)
		ret = ksz9477_ptp_ocmode_set(ksz, KSZ9477_PTP_OCMODE_MASTER);
	else if (strcasecmp(buf, "slave") == 0)
		ret = ksz9477_ptp_ocmode_set(ksz, KSZ9477_PTP_OCMODE_SLAVE);
	else
		return -EINVAL;

	return ret ? ret : (ssize_t)count;
}

static DEVICE_ATTR_RW(ocmode);

static struct attribute *ksz9477_ptp_attrs[] = {
	&dev_attr_tcmode.attr,
	&dev_attr_ocmode.attr,
	NULL,
};

static struct attribute_group ksz9477_ptp_attrgrp = {
	.attrs = ksz9477_ptp_attrs,
};

int ksz9477_ptp_init(struct ksz_device *dev)
{
	int ret;

	mutex_init(&dev->ptp_mutex);
	spin_lock_init(&dev->ptp_shared.ptp_clock_lock);

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
	dev->ptp_caps.n_per_out   = 1;
	dev->ptp_caps.pps         = 0;
	dev->ptp_caps.adjfine     = ksz9477_ptp_adjfine;
	dev->ptp_caps.adjtime     = ksz9477_ptp_adjtime;
	dev->ptp_caps.gettime64   = ksz9477_ptp_gettime;
	dev->ptp_caps.settime64   = ksz9477_ptp_settime;
	dev->ptp_caps.enable      = ksz9477_ptp_enable;
	dev->ptp_caps.do_aux_work = ksz9477_ptp_do_aux_work;

	/* Start hardware counter (will overflow after 136 years) */
	ret = ksz9477_ptp_start_clock(dev);
	if (ret)
		return ret;

	dev->ptp_clock = ptp_clock_register(&dev->ptp_caps, dev->dev);
	if (IS_ERR(dev->ptp_clock)) {
		ret = PTR_ERR(dev->ptp_clock);
		goto error_stop_clock;
	}

	/* Init switch ports */
	ret = ksz9477_ptp_ports_init(dev);
	if (ret)
		goto error_unregister_clock;

	/* Init attributes */
	ret = sysfs_create_group(&dev->dev->kobj, &ksz9477_ptp_attrgrp);
	if (ret)
		goto error_ports_deinit;

	return 0;

error_ports_deinit:
	ksz9477_ptp_ports_deinit(dev);
error_unregister_clock:
	ptp_clock_unregister(dev->ptp_clock);
error_stop_clock:
	ksz9477_ptp_stop_clock(dev);
	return ret;
}

void ksz9477_ptp_deinit(struct ksz_device *dev)
{
	sysfs_remove_group(&dev->dev->kobj, &ksz9477_ptp_attrgrp);
	ksz9477_ptp_ports_deinit(dev);
	ksz9477_ptp_enable_mode(dev, false);
	ptp_clock_unregister(dev->ptp_clock);
	ksz9477_ptp_stop_clock(dev);
}

irqreturn_t ksz9477_ptp_port_interrupt(struct ksz_device *dev, int port)
{
	u32 addr = PORT_CTRL_ADDR(port, REG_PTP_PORT_TX_INT_STATUS__2);
	struct ksz_port *prt = &dev->ports[port];
	u16 data;
	int ret;

	ret = ksz_read16(dev, addr, &data);
	if (ret)
		return IRQ_NONE;

	if (data & PTP_PORT_XDELAY_REQ_INT) {
		/* Timestamp for Pdelay_Req / Delay_Req */
		struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
		u32 tstamp_raw;
		ktime_t tstamp;

		/* In contrast to the KSZ9563R data sheet, the format of the
		 * port time stamp registers is also 2 bit seconds + 30 bit
		 * nanoseconds (same as in the tail tags).
		 */
		ret = ksz_read32(dev,
				 PORT_CTRL_ADDR(port, REG_PTP_PORT_XDELAY_TS),
				 &tstamp_raw);
		if (ret)
			return IRQ_NONE;

		tstamp = ksz9477_decode_tstamp(tstamp_raw);
		prt->tstamp_xdelay = ksz9477_tstamp_reconstruct(ptp_shared,
								tstamp);
		complete(&prt->tstamp_completion);
	}

	/* Clear interrupt(s) (W1C) */
	ret = ksz_write16(dev, addr, data);
	if (ret)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

/* DSA PTP operations */

int ksz9477_ptp_get_ts_info(struct dsa_switch *ds, int port,
			    struct ethtool_ts_info *ts)
{
	struct ksz_device *dev = ds->priv;

	ts->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
			      SOF_TIMESTAMPING_RX_HARDWARE |
			      SOF_TIMESTAMPING_RAW_HARDWARE;

	ts->phc_index = ptp_clock_index(dev->ptp_clock);

	ts->tx_types = BIT(HWTSTAMP_TX_OFF) |
		       BIT(HWTSTAMP_TX_ONESTEP_P2P);

	ts->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

static int ksz9477_set_hwtstamp_config(struct ksz_device *dev, int port,
				       struct hwtstamp_config *config)
{
	struct ksz_device_ptp_shared *ptp_shared = &dev->ptp_shared;
	struct ksz_port *prt = &dev->ports[port];
	bool on = true;

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		prt->hwts_tx_en = false;
		break;
	case HWTSTAMP_TX_ONESTEP_P2P:
		prt->hwts_tx_en = true;
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		on = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		on = false;
		return -ERANGE;
	}

	if (on != test_bit(KSZ9477_HWTS_EN, &ptp_shared->state)) {
		int ret = 0;

		clear_bit(KSZ9477_HWTS_EN, &ptp_shared->state);

		ret = ksz9477_ptp_enable_mode(dev, on);
		if (ret) {
			dev_err(dev->dev,
				"Failed to change timestamping: %d\n", ret);
			return ret;
		}
		if (on)
			set_bit(KSZ9477_HWTS_EN, &ptp_shared->state);
	}

	return 0;
}

int ksz9477_ptp_port_hwtstamp_get(struct dsa_switch *ds, int port,
				  struct ifreq *ifr)
{
	struct ksz_device *dev = ds->priv;
	unsigned long bytes_copied;

	bytes_copied = copy_to_user(ifr->ifr_data, &dev->tstamp_config,
				    sizeof(dev->tstamp_config));

	return bytes_copied ? -EFAULT : 0;
}

int ksz9477_ptp_port_hwtstamp_set(struct dsa_switch *ds, int port,
				  struct ifreq *ifr)
{
	struct ksz_device *dev = ds->priv;
	struct hwtstamp_config config;
	unsigned long bytes_copied;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	mutex_lock(&dev->ptp_mutex);

	err = ksz9477_set_hwtstamp_config(dev, port, &config);
	if (err) {
		mutex_unlock(&dev->ptp_mutex);
		return err;
	}

	/* Save the chosen configuration to be returned later. */
	memcpy(&dev->tstamp_config, &config, sizeof(config));
	bytes_copied = copy_to_user(ifr->ifr_data, &config, sizeof(config));

	mutex_unlock(&dev->ptp_mutex);

	return bytes_copied ? -EFAULT : 0;
}

bool ksz9477_ptp_port_txtstamp(struct dsa_switch *ds, int port,
			       struct sk_buff *clone, unsigned int type)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *prt = &dev->ports[port];
	struct ptp_header *hdr;
	u8 ptp_msg_type;

	/* Should already been tested in dsa_skb_tx_timestamp()? */
	if (!(skb_shinfo(clone)->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	if (!prt->hwts_tx_en)
		return false;

	hdr = ptp_parse_header(clone, type);
	if (!hdr)
		return false;

	ptp_msg_type = ptp_get_msgtype(hdr, type);
	switch (ptp_msg_type) {
	/* As the KSZ9563 always performs one step time stamping, only the time
	 * stamp for Delay_Req and Pdelay_Req are reported to the application
	 * via socket error queue. Time stamps for Sync and Pdelay_resp will be
	 * applied directly to the outgoing message (e.g. correction field), but
	 * will NOT be reported to the socket.
	 */
	case PTP_MSGTYPE_DELAY_REQ:
	case PTP_MSGTYPE_PDELAY_REQ:
	case PTP_MSGTYPE_PDELAY_RESP:
		break;
	default:
		return false;
	}

	/* ptp_type will be reused in ksz9477_xmit_timestamp(). ptp_msg_type
	 * will be reused in ksz9477_defer_xmit(). For PDelay_Resp, the cloned
	 * skb will not be passed to skb_complete_tx_timestamp() and has to be
	 * freed manually in ksz9477_defer_xmit().
	 */
	KSZ9477_SKB_CB(clone)->ptp_type = type;
	KSZ9477_SKB_CB(clone)->ptp_msg_type = ptp_msg_type;

	return true;  /* keep cloned skb */
}
