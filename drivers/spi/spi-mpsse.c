// SPDX-License-Identifier: GPL-2.0
/*
 * FTDI MPSSE SPI Protocol Driver
 * Uses the exported API from gpio-mpsse.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/usb/mpsse.h>

struct spi_mpsse_priv {
	struct mpsse_device *mpsse;
	struct spi_controller *ctrl;
	struct device *dev;

	/* Standard GPIO descriptors for the bus lines */
	struct gpio_desc *sck;
	struct gpio_desc *mosi;
	struct gpio_desc *miso;

	/* Buffers for SPI transfer command composition */
	u8 cmd_buf[512];
	u8 rx_buf[512];

	u32 last_speed_hz;
};

/* Scavenged logic from spi-ftdi-mpsse.c: ftdi_spi_txrx_byte_cmd */
static inline u8 ftdi_spi_txrx_byte_cmd(struct spi_device *spi)
{
	u8 mode = spi->mode & (SPI_CPOL | SPI_CPHA);
	u8 cmd = 0;

	if (spi->mode & SPI_LSB_FIRST) {
		switch (mode) {
		case SPI_MODE_0:
		case SPI_MODE_1:
			cmd = MPSSE_TXF_RXR_BYTES_LSB;
			break;
		case SPI_MODE_2:
		case SPI_MODE_3:
			cmd = MPSSE_TXR_RXF_BYTES_LSB;
			break;
		}
	} else {
		switch (mode) {
		case SPI_MODE_0:
		case SPI_MODE_1:
			cmd = MPSSE_TXF_RXR_BYTES_MSB;
			break;
		case SPI_MODE_2:
		case SPI_MODE_3:
			cmd = MPSSE_TXR_RXF_BYTES_MSB;
			break;
		}
	}
	return cmd;
}

static inline u8 ftdi_spi_tx_only_cmd(struct spi_device *spi)
{
	u8 mode = spi->mode & (SPI_CPOL | SPI_CPHA);
	if (spi->mode & SPI_LSB_FIRST)
		return (mode <= SPI_MODE_1) ? MPSSE_TX_BYTES_FE_LSB : MPSSE_TX_BYTES_RE_LSB;
	else
		return (mode <= SPI_MODE_1) ? MPSSE_TX_BYTES_FE_MSB : MPSSE_TX_BYTES_RE_MSB;
}

static inline u8 ftdi_spi_rx_only_cmd(struct spi_device *spi)
{
	u8 mode = spi->mode & (SPI_CPOL | SPI_CPHA);
	if (spi->mode & SPI_LSB_FIRST)
		return (mode <= SPI_MODE_1) ? MPSSE_RX_BYTES_RE_LSB : MPSSE_RX_BYTES_FE_LSB;
	else
		return (mode <= SPI_MODE_1) ? MPSSE_RX_BYTES_RE_MSB : MPSSE_RX_BYTES_FE_MSB;
}

static int spi_mpsse_set_loopback(struct mpsse_device *mpsse, bool enable)
{
	u8 cmd = enable ? MPSSE_LOOPBACK_ON : MPSSE_LOOPBACK_OFF;
	return mpsse_write_data(mpsse, &cmd, 1);
}

static int spi_mpsse_transfer_one(struct spi_controller *ctrl,
				  struct spi_device *spi,
				  struct spi_transfer *t)
{
	struct spi_mpsse_priv *priv = spi_controller_get_devdata(ctrl);
	struct mpsse_device *mpsse = priv->mpsse;
	const u8 *tx_buf = t->tx_buf;
	size_t remaining = t->len;
	u8 *rx_buf = t->rx_buf;
	int initial_sck;
	size_t stride;
	int ret = 0;
	u8 cmd;

	/* 1. Configure SCLK Idle State (CPOL) using standard API */
	/* This ensures the pin is physically in the right state before MPSSE takes over */
	initial_sck = (spi->mode & SPI_CPOL) ? 1 : 0;
	gpiod_set_value_cansleep(priv->sck, initial_sck);

	mpsse_lock(mpsse);

	if (t->rx_buf) {
		ret = mpsse_flush(mpsse);
		if (ret) {
			dev_err(&spi->dev, "Failed to flush RX FIFO: %pe\n",
				ERR_PTR(ret));
			goto out;
		}
	}

	if (priv->last_speed_hz != t->speed_hz) {
		ret = mpsse_set_clock(mpsse, t->speed_hz);
		if (ret) {
			dev_err(&spi->dev,
				"Failed to set SPI frequency to %d Hz: %pe\n",
				t->speed_hz, ERR_PTR(ret));
			return ret;
		}
		priv->last_speed_hz = t->speed_hz;
	}

	/* Implement Loopback if requested */
	if (spi->mode & SPI_LOOP) {
		ret = spi_mpsse_set_loopback(mpsse, true);
		if (ret) {
			dev_err(&spi->dev, "Failed to set SPI loopback: %pe\n",
				ERR_PTR(ret));
			goto out;
		}
	}

	while (remaining) {
		/* Max stride is buffer size minus command overhead (3 bytes) */
		stride = min_t(size_t, remaining, sizeof(priv->cmd_buf) - 3);

		if (tx_buf && rx_buf) {
			/* Bidirectional */
			cmd = ftdi_spi_txrx_byte_cmd(spi);
			priv->cmd_buf[0] = cmd;
			priv->cmd_buf[1] = (stride - 1) & 0xFF;
			priv->cmd_buf[2] = ((stride - 1) >> 8) & 0xFF;
			memcpy(&priv->cmd_buf[3], tx_buf, stride);

			ret = mpsse_write_data(mpsse, priv->cmd_buf, stride + 3);
			if (ret) {
				dev_err(&spi->dev,
					"Failed to transfer SPI TX buffer: %pe\n",
					ERR_PTR(ret));
				goto out_loop;
			}

			ret = mpsse_read_data(mpsse, rx_buf, stride);
			if (ret) {
				dev_err(&spi->dev,
					"Failed to transfer SPI RX buffer: %pe\n",
					ERR_PTR(ret));
				goto out_loop;
			}

			tx_buf += stride;
			rx_buf += stride;

		} else if (tx_buf) {
			/* Write Only */
			cmd = ftdi_spi_tx_only_cmd(spi);
			priv->cmd_buf[0] = cmd;
			priv->cmd_buf[1] = (stride - 1) & 0xFF;
			priv->cmd_buf[2] = ((stride - 1) >> 8) & 0xFF;
			memcpy(&priv->cmd_buf[3], tx_buf, stride);

			ret = mpsse_write_data(mpsse, priv->cmd_buf, stride + 3);
			if (ret) {
				dev_err(&spi->dev,
					"Failed to transfer SPI TX buffer: %pe\n",
					ERR_PTR(ret));
				goto out_loop;
			}

			tx_buf += stride;

		} else if (rx_buf) {
			/* Read Only */
			cmd = ftdi_spi_rx_only_cmd(spi);
			priv->cmd_buf[0] = cmd;
			priv->cmd_buf[1] = (stride - 1) & 0xFF;
			priv->cmd_buf[2] = ((stride - 1) >> 8) & 0xFF;
			/* SEND_IMMEDIATE scavenged from spi-ftdi-mpsse.c to force flush */
			priv->cmd_buf[3] = MPSSE_SEND_IMMEDIATE;

			ret = mpsse_write_data(mpsse, priv->cmd_buf, 4);
			if (ret) {
				dev_err(&spi->dev,
					"Failed to force flushing: %pe\n",
					ERR_PTR(ret));
				goto out_loop;
			}

			ret = mpsse_read_data(mpsse, rx_buf, stride);
			if (ret) {
				dev_err(&spi->dev,
					"Failed to transfer SPI RX buffer: %pe\n",
					ERR_PTR(ret));
				goto out_loop;
			}

			rx_buf += stride;
		}

		remaining -= stride;
	}

out_loop:
	/* Disable loopback if it was enabled */
	if (spi->mode & SPI_LOOP) {
		ret = spi_mpsse_set_loopback(mpsse, false);
		if (ret)
			dev_warn(&spi->dev,
				 "Failed to restore SPI loopback: %pe\n",
				 ERR_PTR(ret));
	}

out:
	mpsse_unlock(mpsse);
	return ret;
}

static int spi_mpsse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	struct spi_mpsse_priv *priv;
	struct mpsse_device *mpsse;
	int ret;

	/* 1. Find the MPSSE provider via Phandle */
	mpsse = mpsse_get_by_phandle(dev, "ftdi,mpsse-handle");
	if (IS_ERR(mpsse)) {
		return PTR_ERR(mpsse);
	}

	/* 2. Allocate SPI Controller */
	ctrl = devm_spi_alloc_host(dev, sizeof(*priv));
	if (!ctrl) {
		mpsse_put(mpsse);
		return -ENOMEM;
	}

	priv = spi_controller_get_devdata(ctrl);
	priv->ctrl = ctrl;
	priv->dev = dev;
	priv->mpsse = mpsse;

	/* * Request the Hardwired pins as GPIOs to configure initial direction.
	 * We use GPIOD_OUT_LOW as default for SCLK/MOSI to ensure they don't float.
	 */
	priv->sck = devm_gpiod_get(dev, "sck", GPIOD_OUT_LOW);
	if (IS_ERR(priv->sck))
		return PTR_ERR(priv->sck);

	priv->mosi = devm_gpiod_get(dev, "mosi", GPIOD_OUT_LOW);
	if (IS_ERR(priv->mosi))
		return PTR_ERR(priv->mosi);

	priv->miso = devm_gpiod_get(dev, "miso", GPIOD_IN);
	if (IS_ERR(priv->miso))
		return PTR_ERR(priv->miso);

	/* 3. Configure Controller */
	ctrl->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST | SPI_LOOP;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->transfer_one = spi_mpsse_transfer_one;
	ctrl->use_gpio_descriptors = true;
	ctrl->num_chipselect = 4;
	device_set_node(&ctrl->dev, dev_fwnode(dev));

	/* 4. Register */
	ret = devm_spi_register_controller(dev, ctrl);
	if (ret) {
		dev_err(dev, "Failed to register controller: %pe\n", ERR_PTR(ret));
		mpsse_put(mpsse);
		return ret;
	}

	dev_info(dev, "Registered SPI controller successfully\n");

	return 0;
}

static void spi_mpsse_remove(struct platform_device *pdev)
{
	struct spi_controller *ctrl = platform_get_drvdata(pdev);
	struct spi_mpsse_priv *priv = spi_controller_get_devdata(ctrl);

	mpsse_put(priv->mpsse);
}

static const struct of_device_id spi_mpsse_of_match[] = {
	{ .compatible = "ftdi,spi-mpsse", },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_mpsse_of_match);

static struct platform_driver spi_mpsse_driver = {
	.probe = spi_mpsse_probe,
	.remove = spi_mpsse_remove,
	.driver = {
		.name = "spi-mpsse",
		.of_match_table = spi_mpsse_of_match,
	},
};
module_platform_driver(spi_mpsse_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Original: Anatolij Gustschin <agust@denx.de>");
MODULE_DESCRIPTION("FTDI MPSSE SPI controller driver");
