// SPDX-License-Identifier: GPL-2.0
/* Clause 73 autonegotiation library for the MoreThanIP AN/LT block
 *
 * Copyright 2023-2026 NXP
 *
 * This implements &struct phylink_anlt_ops on top of the autonegotiation (MMD
 * 7) registers of the block, for 1000Base-KX. There is no link training, no
 * FEC and no access to the SerDes or to the PCS: the owner of the PCS gives
 * the library a way to access the AN registers (&struct mtip_anlt_access) and
 * reads the link bit of the PCS itself.
 *
 * Next Pages: IEEE 802.3 73.6.9 makes them mandatory, and says that a device
 * with no page to send shall answer the Next Pages of its partner with Null
 * message pages. This library has no page to send and never sets NP in its
 * base page. It leaves the extended next page control bit of the AN control
 * register (7.0.13) clear, and then, according to the reference manual of the
 * block, it answers the Next Pages of the partner with Null pages by itself:
 * the transmit registers (7.22 to 7.24) hold a Null page from reset, and are
 * not writable. So software sends nothing. The Next Page of the partner can
 * still be read (7.25 to 7.27), and is logged. ASSUMPTION: this was not tested
 * with a partner that has Next Pages (the Broadcom WarpCore sets NP in its
 * base page); if the negotiation does not complete with one, this is the
 * place to look, and the library says so in the message of the timeout.
 *
 * Locking: every operation, and the work that polls the block, take
 * mtip_anlt::lock. None of them takes a lock of phylink or waits for phylink:
 * the only call into phylink is phylink_anlt_change(), which just queues the
 * state resolution. The lock is the outermost of the library, the accessor
 * methods are called with it held.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/linkmode.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pcs/pcs-mtip-anlt.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* The registers are numbered as in IEEE 802.3 Clause 45, MMD 7 */
#define MTIP_AN_CTRL			MDIO_CTRL1		/* 7.0 */
#define MTIP_AN_STAT			MDIO_STAT1		/* 7.1 */
#define MTIP_AN_ADV			MDIO_AN_ADVERTISE	/* 7.16-18 */
#define MTIP_AN_LPA			MDIO_AN_LPA		/* 7.19-21 */
#define MTIP_AN_LP_XNP			25			/* 7.25-27 */

#define MTIP_AN_STAT_PDFAULT		BIT(9)	/* 7.1.9, latched high */

#define MTIP_NUM_PAGE_WORDS		3

/* The message code of a Next Page (IEEE 802.3 Annex 73A.1), 7.25 bits 10:0 */
#define MTIP_NP_MESSAGE_CODE		GENMASK(10, 0)

/* The AN state machine takes about 90 ms on the wire to complete (measured on
 * LS2088A against two kinds of partner), so a second is generous.
 */
#define MTIP_AN_PAGE_TIMEOUT		HZ
#define MTIP_POLL_INTERVAL		(HZ / 4)

/**
 * struct mtip_anlt - one autonegotiation block
 * @anlt: the facet handed out to phylink
 * @dev: device used for messages
 * @access: how to reach the AN registers
 * @ctx: argument of the methods of @access
 * @lock: serialises every operation and the poll work, and protects the
 *	members below
 * @work_off: @work is disabled: it starts that way, and anlt_disable() disables
 *	it, so that nothing can queue it until the next anlt_config()
 * @work: polls AN_STAT, the only reader of that register (page received is
 *	read-to-clear, link is latched low)
 * @configured: the registers of the block hold a configuration from us, which
 *	anlt_disable() has to undo. When false, no register is accessed
 * @an_enabled: the negotiation is running (the Autoneg bit was set)
 * @adv_valid: @adv holds what was last written to the block
 * @adv: the base page we advertise (7.16 to 7.18), without the nonce
 * @nonce: the transmitted nonce of the base page
 * @last_restart: when the negotiation was last (re)started, in jiffies
 * @lpa: link partner base page, valid if @lpa_valid
 * @lpa_valid: a base page was received since the last restart
 * @lp_np: the partner has Next Pages
 * @up: AN complete and AN_STAT link set
 * @complete: AN complete
 * @pardet: AN complete without any page from a partner: the partner does not
 *	run Clause 73 (parallel detection)
 * @timeout_logged: AN has not completed in time after a page, and we said so
 * @xnp_logged: the first Next Page of the partner was logged
 */
struct mtip_anlt {
	struct phylink_anlt anlt;
	struct device *dev;
	const struct mtip_anlt_access *access;
	void *ctx;
	struct mutex lock;	/* serializes every operation and the poll work */
	struct delayed_work work;
	bool work_off;
	bool configured;
	bool an_enabled;
	bool adv_valid;
	u16 adv[MTIP_NUM_PAGE_WORDS];
	u8 nonce;
	unsigned long last_restart;
	u16 lpa[MTIP_NUM_PAGE_WORDS];
	bool lpa_valid;
	bool lp_np;
	bool up;
	bool complete;
	bool pardet;
	bool timeout_logged;
	bool xnp_logged;
};

static struct mtip_anlt *to_mtip_anlt(struct phylink_anlt *anlt)
{
	return container_of(anlt, struct mtip_anlt, anlt);
}

static int mtip_anlt_read(struct mtip_anlt *mtip, int reg)
{
	return mtip->access->read(mtip->ctx, reg);
}

static int mtip_anlt_write(struct mtip_anlt *mtip, int reg, u16 val)
{
	return mtip->access->write(mtip->ctx, reg, val);
}

static int mtip_anlt_modify(struct mtip_anlt *mtip, int reg, u16 mask, u16 set)
{
	return mtip->access->modify(mtip->ctx, reg, mask, set);
}

static int mtip_anlt_read_words(struct mtip_anlt *mtip, int reg, u16 *words)
{
	int i, val;

	for (i = 0; i < MTIP_NUM_PAGE_WORDS; i++) {
		val = mtip_anlt_read(mtip, reg + i);
		if (val < 0)
			return val;

		words[i] = val;
	}

	return 0;
}

static int mtip_anlt_write_words(struct mtip_anlt *mtip, int reg,
				 const u16 *words)
{
	int i, err;

	for (i = 0; i < MTIP_NUM_PAGE_WORDS; i++) {
		err = mtip_anlt_write(mtip, reg + i, words[i]);
		if (err)
			return err;
	}

	return 0;
}

static void mtip_anlt_reset_cache(struct mtip_anlt *mtip)
{
	memset(mtip->lpa, 0, sizeof(mtip->lpa));
	mtip->lpa_valid = false;
	mtip->lp_np = false;
	mtip->xnp_logged = false;
	mtip->up = false;
	mtip->complete = false;
	mtip->pardet = false;
	mtip->timeout_logged = false;
}

/* The base page we advertise is built from the @advertising link modes (the
 * KX bit, and the Pause and Asym_Pause bits, 7.16 bits 10 and 11) plus the
 * selector field of IEEE 802.3 Annex 28A. The nonce is added when it is
 * written, and the acknowledge and echoed nonce fields are driven by the
 * block. Whatever else is in @advertising is not ours to advertise.
 */
static void mtip_anlt_base_page(const unsigned long *advertising, u16 *words)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(modes) = {};
	int bit;

	for_each_set_bit(bit, advertising, __ETHTOOL_LINK_MODE_MASK_NBITS) {
		if (bit == ETHTOOL_LINK_MODE_1000baseKX_Full_BIT ||
		    bit == ETHTOOL_LINK_MODE_Pause_BIT ||
		    bit == ETHTOOL_LINK_MODE_Asym_Pause_BIT)
			linkmode_set_bit(bit, modes);
	}

	mii_c73_linkmode_to_adv(modes, words);
	words[0] |= FIELD_PREP(MDIO_AN_C73_0_S_MASK, 1);
}

static int mtip_anlt_write_adv(struct mtip_anlt *mtip)
{
	u16 words[MTIP_NUM_PAGE_WORDS];
	int err;

	memcpy(words, mtip->adv, sizeof(words));
	words[1] |= FIELD_PREP(MDIO_AN_C73_1_T_MASK, mtip->nonce);

	err = mtip_anlt_write_words(mtip, MTIP_AN_ADV, words);
	if (err)
		mtip->adv_valid = false;

	return err;
}

static void mtip_anlt_enable_work(struct mtip_anlt *mtip)
{
	if (mtip->work_off) {
		enable_delayed_work(&mtip->work);
		mtip->work_off = false;
	}
}

/* Start the negotiation over. The block is first stopped, so that the status
 * bits that latch can be discarded without losing a real event, and the pages
 * we send are loaded while it is quiet. Called with the lock held.
 *
 * The downstream driver resets the block here (7.0.15) instead of clearing
 * AN_EN. This one does not, since the reset is not documented to keep the
 * advertisement and the effect of the reset on the PCS link is not known. The
 * AN_EN 1 -> 0 -> 1 sequence was measured on LS2088A to resume the negotiation
 * in about 90 ms.
 */
static int mtip_anlt_do_restart(struct mtip_anlt *mtip)
{
	int err;

	/* The block needs a nonce that is arbitrary but not the one of its
	 * partner, and resets to 0x0f on every device.
	 */
	do {
		mtip->nonce = get_random_u8() & GENMASK(4, 0);
	} while (!mtip->nonce || mtip->nonce == 0x0f);

	err = mtip_anlt_modify(mtip, MTIP_AN_CTRL, MDIO_AN_CTRL1_ENABLE, 0);
	if (err)
		return err;

	/* Discard the status of the previous negotiation: the work is the
	 * only reader of AN_STAT, and we hold the lock that it needs.
	 */
	err = mtip_anlt_read(mtip, MTIP_AN_STAT);
	if (err < 0)
		return err;

	err = mtip_anlt_write_adv(mtip);
	if (err)
		return err;

	mtip_anlt_reset_cache(mtip);

	err = mtip_anlt_modify(mtip, MTIP_AN_CTRL,
			       MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART,
			       MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
	if (err)
		return err;

	mtip->last_restart = jiffies;
	mod_delayed_work(system_percpu_wq, &mtip->work, 0);

	return 0;
}

static void mtip_anlt_get_caps(struct phylink_anlt *anlt,
			       unsigned long *interfaces,
			       unsigned long *linkmodes)
{
	__set_bit(PHY_INTERFACE_MODE_1000BASEKX, interfaces);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, linkmodes);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, linkmodes);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, linkmodes);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT, linkmodes);
}

/* Autoneg off: the block is stopped, and the link is then whatever the PCS
 * says it is. The PCS is not reset and nothing is written if the block is
 * already in this state, so that a link that is up stays up.
 */
static int mtip_anlt_config_forced(struct mtip_anlt *mtip)
{
	int err;

	mtip_anlt_enable_work(mtip);

	err = mtip_anlt_modify(mtip, MTIP_AN_CTRL, MDIO_AN_CTRL1_ENABLE, 0);
	if (err)
		return err;

	mtip->configured = true;
	mtip->an_enabled = false;
	mtip_anlt_reset_cache(mtip);

	return 0;
}

static int mtip_anlt_config(struct phylink_anlt *anlt,
			    phy_interface_t interface,
			    const unsigned long *advertising)
{
	struct mtip_anlt *mtip = to_mtip_anlt(anlt);
	bool changed = false;
	u16 adv[MTIP_NUM_PAGE_WORDS];
	int err;

	if (interface != PHY_INTERFACE_MODE_1000BASEKX)
		return -EINVAL;

	mutex_lock(&mtip->lock);

	if (!linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, advertising)) {
		err = mtip_anlt_config_forced(mtip);
		goto out_unlock;
	}

	mtip_anlt_base_page(advertising, adv);
	if (!mtip->adv_valid || memcmp(adv, mtip->adv, sizeof(adv))) {
		memcpy(mtip->adv, adv, sizeof(adv));
		changed = true;
	}

	if (!mtip->an_enabled) {
		/* From nothing, or from the forced mode: start the
		 * negotiation here, the caller has nothing to restart.
		 */
		mtip_anlt_enable_work(mtip);
		mtip->configured = true;
		err = mtip_anlt_do_restart(mtip);
		if (err) {
			mtip->adv_valid = false;
			goto out_unlock;
		}

		mtip->adv_valid = true;
		mtip->an_enabled = true;
		err = 0;
		goto out_unlock;
	}

	if (changed) {
		/* The new advertisement is sent by the next restart */
		err = mtip_anlt_write_adv(mtip);
		if (err)
			goto out_unlock;

		mtip->adv_valid = true;
	}

	err = changed;

out_unlock:
	mutex_unlock(&mtip->lock);

	return err;
}

static void mtip_anlt_restart(struct phylink_anlt *anlt)
{
	struct mtip_anlt *mtip = to_mtip_anlt(anlt);
	int err;

	mutex_lock(&mtip->lock);

	if (mtip->configured && mtip->an_enabled) {
		err = mtip_anlt_do_restart(mtip);
		if (err)
			dev_err(mtip->dev, "Failed to restart autoneg: %pe\n",
				ERR_PTR(err));
	}

	mutex_unlock(&mtip->lock);
}

/* Called from the work, with the lock held. Digests AN_STAT, and the base
 * page of the partner if there is a new one, into the state served by
 * anlt_get_state(). Returns whether phylink has to look at the link again.
 */
static bool mtip_anlt_process_stat(struct mtip_anlt *mtip, u16 stat)
{
	bool up = (stat & MDIO_AN_STAT1_COMPLETE) && (stat & MDIO_STAT1_LSTATUS);
	bool complete = !!(stat & MDIO_AN_STAT1_COMPLETE);
	bool changed = false;
	bool pardet;
	u16 lpa[MTIP_NUM_PAGE_WORDS];
	int err;

	/* The page received bit is read-to-clear: if reading the page failed,
	 * try again as long as the partner says it has one
	 */
	if ((stat & MDIO_AN_STAT1_PAGE) ||
	    (complete && !mtip->lpa_valid && (stat & MDIO_AN_STAT1_LPABLE))) {
		err = mtip_anlt_read_words(mtip, MTIP_AN_LPA, lpa);
		if (err) {
			dev_err_ratelimited(mtip->dev,
					    "Failed to read the partner page: %pe\n",
					    ERR_PTR(err));
			return false;
		}

		if (!mtip->lpa_valid || memcmp(lpa, mtip->lpa, sizeof(lpa))) {
			memcpy(mtip->lpa, lpa, sizeof(lpa));
			mtip->lpa_valid = true;
			changed = true;
		}

		mtip->lp_np = !!(lpa[0] & MDIO_AN_C73_0_NP);
	}

	/* The block says that autonegotiation is complete, but no page was
	 * ever received and the partner is not able of autonegotiation: this
	 * is the parallel detection of Clause 73, a partner that just sends
	 * 1000Base-X code words. There is nothing to resolve, and the link is
	 * up if the PCS says so. AN_STAT bit 6 is read-to-clear, so it is the
	 * flag of the received base page, and not the bit, that counts.
	 */
	pardet = complete && !mtip->lpa_valid &&
		 !(stat & (MDIO_AN_STAT1_LPABLE | MTIP_AN_STAT_PDFAULT));

	if (complete && mtip->lp_np && !mtip->xnp_logged) {
		u16 xnp[MTIP_NUM_PAGE_WORDS];

		mtip->xnp_logged = true;
		if (!mtip_anlt_read_words(mtip, MTIP_AN_LP_XNP, xnp))
			dev_info(mtip->dev,
				 "Partner Next Page %04x:%04x:%04x, message code 0x%x\n",
				 xnp[2], xnp[1], xnp[0],
				 (u32)FIELD_GET(MTIP_NP_MESSAGE_CODE, xnp[0]));
	}

	if (!complete && mtip->lpa_valid && !mtip->timeout_logged &&
	    time_after(jiffies, mtip->last_restart + MTIP_AN_PAGE_TIMEOUT)) {
		mtip->timeout_logged = true;
		dev_warn(mtip->dev,
			 "Autoneg not complete %u ms after the base page (partner has Next Pages: %d, AN_STAT 0x%04x, partner page %04x:%04x:%04x)\n",
			 jiffies_to_msecs(jiffies - mtip->last_restart),
			 mtip->lp_np, stat, mtip->lpa[2], mtip->lpa[1],
			 mtip->lpa[0]);
	}

	if (mtip->up != up || mtip->complete != complete ||
	    mtip->pardet != pardet)
		changed = true;

	/* The link was up and the block says it is not any more: have it
	 * exchange pages again, like the downstream driver does when the PCS
	 * link drops. HYPOTHESIS: needed because the block does not go back to
	 * sending pages by itself while the partner is quiet.
	 */
	if (mtip->up && !up && mtip->an_enabled) {
		err = mtip_anlt_do_restart(mtip);
		if (err)
			dev_err_ratelimited(mtip->dev,
					    "Failed to restart autoneg: %pe\n",
					    ERR_PTR(err));
		return true;
	}

	mtip->up = up;
	mtip->complete = complete;
	mtip->pardet = pardet;

	return changed;
}

static void mtip_anlt_poll(struct work_struct *work)
{
	struct mtip_anlt *mtip = container_of(work, struct mtip_anlt,
					      work.work);
	bool changed = false;
	int stat;

	mutex_lock(&mtip->lock);

	if (!mtip->configured || !mtip->an_enabled)
		goto out_unlock;

	stat = mtip_anlt_read(mtip, MTIP_AN_STAT);
	if (stat < 0)
		dev_err_ratelimited(mtip->dev, "Failed to read AN_STAT: %pe\n",
				    ERR_PTR(stat));
	else
		changed = mtip_anlt_process_stat(mtip, stat);

	if (changed)
		phylink_anlt_change(&mtip->anlt, mtip->up);

	schedule_delayed_work(&mtip->work, MTIP_POLL_INTERVAL);

out_unlock:
	mutex_unlock(&mtip->lock);
}

/* Complements the state that phylink got from the PCS. Does not read any
 * register: AN_STAT has a single reader, the work, so this serves the result
 * of its last run, and the PCS driver is the only one to read the link bit of
 * the PCS.
 *
 * When the negotiation is stopped, by anlt_config() with Autoneg off, the link
 * is the one of the PCS, forced to 1000Base-KX full duplex. When the block was
 * never configured, or after anlt_disable(), the state is not touched.
 */
static void mtip_anlt_get_state(struct phylink_anlt *anlt,
				struct phylink_link_state *state)
{
	struct mtip_anlt *mtip = to_mtip_anlt(anlt);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(kx) = {};
	bool tx_pause, rx_pause;

	mutex_lock(&mtip->lock);

	if (!mtip->configured)
		goto out_unlock;

	state->speed = SPEED_1000;
	state->duplex = DUPLEX_FULL;

	if (!mtip->an_enabled)
		goto out_unlock;

	state->an_complete = mtip->complete;
	if (!mtip->up)
		state->link = false;

	if (mtip->pardet) {
		state->pause = MLO_PAUSE_NONE;
		goto out_unlock;
	}

	if (!mtip->lpa_valid) {
		state->link = false;
		goto out_unlock;
	}

	mii_c73_mod_linkmode(state->lp_advertising, mtip->lpa);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, state->lp_advertising);

	/* No 1000Base-KX in common, whatever the block says */
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT, kx);
	linkmode_and(kx, kx, state->advertising);
	linkmode_and(kx, kx, state->lp_advertising);
	if (linkmode_empty(kx))
		state->link = false;

	state->pause = MLO_PAUSE_NONE;
	linkmode_resolve_pause(state->advertising, state->lp_advertising,
			       &tx_pause, &rx_pause);
	if (tx_pause)
		state->pause |= MLO_PAUSE_TX;
	if (rx_pause)
		state->pause |= MLO_PAUSE_RX;

out_unlock:
	mutex_unlock(&mtip->lock);
}

/* Stops the block and the work, and returns only after the work has finished.
 * Can be called at any time and any number of times, and does not access the
 * block if it was not configured. The work is disabled rather than cancelled,
 * so that an operation that races with this one cannot queue it again, and it
 * is enabled again by the next anlt_config().
 *
 * What anlt_get_state() returns afterwards is a harmless state: it leaves what
 * the PCS reported alone, so the link is the one of the PCS.
 */
static void mtip_anlt_disable(struct phylink_anlt *anlt)
{
	struct mtip_anlt *mtip = to_mtip_anlt(anlt);
	bool disable_work;
	int err;

	mutex_lock(&mtip->lock);

	if (mtip->configured && mtip->an_enabled) {
		err = mtip_anlt_modify(mtip, MTIP_AN_CTRL,
				       MDIO_AN_CTRL1_ENABLE, 0);
		if (err)
			dev_dbg(mtip->dev, "Failed to stop autoneg: %pe\n",
				ERR_PTR(err));
	}

	mtip->configured = false;
	mtip->an_enabled = false;
	mtip->adv_valid = false;
	mtip_anlt_reset_cache(mtip);

	disable_work = !mtip->work_off;
	mtip->work_off = true;

	mutex_unlock(&mtip->lock);

	/* The work takes the lock, so this cannot be done with it held. The
	 * disable count is raised once, by whoever finds the work enabled.
	 */
	if (disable_work)
		disable_delayed_work_sync(&mtip->work);
	else
		cancel_delayed_work_sync(&mtip->work);
}

static const struct phylink_anlt_ops mtip_anlt_ops = {
	.anlt_get_caps = mtip_anlt_get_caps,
	.anlt_config = mtip_anlt_config,
	.anlt_restart = mtip_anlt_restart,
	.anlt_get_state = mtip_anlt_get_state,
	.anlt_disable = mtip_anlt_disable,
};

/**
 * mtip_anlt_create() - create the object of an autonegotiation block
 * @dev: device for messages, which must outlive the object
 * @access: how to reach the AN registers, which must outlive the object
 * @ctx: argument to the methods of @access
 *
 * Does not access the block, which may not be visible yet (on Layerscape it
 * is only there when the SerDes lane runs 1000Base-KX).
 *
 * Return: the object, or an ERR_PTR().
 */
struct mtip_anlt *mtip_anlt_create(struct device *dev,
				   const struct mtip_anlt_access *access,
				   void *ctx)
{
	struct mtip_anlt *mtip;

	if (!access || !access->read || !access->write || !access->modify)
		return ERR_PTR(-EINVAL);

	mtip = kzalloc_obj(*mtip);
	if (!mtip)
		return ERR_PTR(-ENOMEM);

	mtip->anlt.ops = &mtip_anlt_ops;
	mtip->dev = dev;
	mtip->access = access;
	mtip->ctx = ctx;
	mutex_init(&mtip->lock);
	INIT_DELAYED_WORK(&mtip->work, mtip_anlt_poll);
	disable_delayed_work(&mtip->work);
	mtip->work_off = true;

	return mtip;
}
EXPORT_SYMBOL_GPL(mtip_anlt_create);

/**
 * mtip_anlt_destroy() - free the object of an autonegotiation block
 * @mtip: the object, which was returned by mtip_anlt_create()
 *
 * Stops the block and the work if that was not done yet. Must be called after
 * phylink_destroy() of the phylink instance the facet was passed to.
 */
void mtip_anlt_destroy(struct mtip_anlt *mtip)
{
	mtip_anlt_disable(&mtip->anlt);
	mutex_destroy(&mtip->lock);
	kfree(mtip);
}
EXPORT_SYMBOL_GPL(mtip_anlt_destroy);

/**
 * mtip_anlt_to_phylink() - the facet to put in &phylink_config.anlt
 * @mtip: the object, which was returned by mtip_anlt_create()
 */
struct phylink_anlt *mtip_anlt_to_phylink(struct mtip_anlt *mtip)
{
	return &mtip->anlt;
}
EXPORT_SYMBOL_GPL(mtip_anlt_to_phylink);

MODULE_AUTHOR("Vladimir Oltean <vladimir.oltean@nxp.com>");
MODULE_DESCRIPTION("MoreThanIP Clause 73 autonegotiation library");
MODULE_LICENSE("GPL");
