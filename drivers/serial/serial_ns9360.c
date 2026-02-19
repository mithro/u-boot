// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 UART serial driver (driver model)
 *
 * Copyright (C) 2025
 *
 * NS9360 serial port with 4 channels. Each channel has a 64-byte
 * TX and RX FIFO. Baud rate is derived from the BBus clock
 * (CONFIG_SYS_CLK_FREQ / 8 = 22.1184 MHz).
 *
 * Reference: Digi U-Boot 1.1.4 drivers/ns9750_serial.c
 */

#include <dm.h>
#include <serial.h>
#include <asm/io.h>

/* Register offsets from channel base */
#define NS9360_CTRL_A		0x00
#define NS9360_CTRL_B		0x04
#define NS9360_STAT_A		0x08
#define NS9360_BITRATE		0x0C
#define NS9360_FIFO		0x10
#define NS9360_RX_CHAR_TMR	0x18

/* CTRL_A bits */
#define CTRL_A_CE		BIT(31)		/* Channel Enable */
#define CTRL_A_WLS_8		(3 << 24)	/* 8-bit word length */
#define CTRL_A_STOP		BIT(26)		/* 2 stop bits (0 = 1 stop bit) */

/* STAT_A bits */
#define STAT_A_TRDY		BIT(3)		/* TX Ready */
#define STAT_A_RRDY		BIT(11)		/* RX Ready (data available) */

/* BITRATE bits */
#define BITRATE_EBIT		BIT(31)		/* Enable baud rate generator */
#define BITRATE_TMODE		BIT(30)		/* Transmitter mode */
#define BITRATE_CLKMUX_BCLK	(1 << 24)	/* Use BBus clock */
#define BITRATE_TCDR_16		(2 << 19)	/* TX clock prescaler /16 */
#define BITRATE_RCDR_16		(4 << 16)	/* RX clock prescaler /16 */
#define BITRATE_N_MASK		0x7FFF		/* Divisor N mask */

struct ns9360_serial_priv {
	void __iomem *base;
	unsigned long clk_rate;		/* BBus clock frequency */
};

static int ns9360_serial_setbrg(struct udevice *dev, int baudrate)
{
	struct ns9360_serial_priv *priv = dev_get_priv(dev);
	unsigned int n;
	u32 val;

	/*
	 * Baud rate calculation:
	 *   BBus clock = CONFIG_SYS_CLK_FREQ / 8 = 22,118,400 Hz
	 *   N = (bbus_clk / (baudrate * 16)) - 1
	 *   For 115200: N = (22118400 / (115200 * 16)) - 1 = 11
	 */
	n = (priv->clk_rate / (baudrate * 16)) - 1;

	val = BITRATE_EBIT | BITRATE_TMODE | BITRATE_CLKMUX_BCLK |
	      BITRATE_TCDR_16 | BITRATE_RCDR_16 | (n & BITRATE_N_MASK);

	writel(val, priv->base + NS9360_BITRATE);

	return 0;
}

static int ns9360_serial_putc(struct udevice *dev, const char ch)
{
	struct ns9360_serial_priv *priv = dev_get_priv(dev);

	/* Wait for TX ready */
	if (!(readl(priv->base + NS9360_STAT_A) & STAT_A_TRDY))
		return -EAGAIN;

	writel(ch, priv->base + NS9360_FIFO);
	return 0;
}

static int ns9360_serial_getc(struct udevice *dev)
{
	struct ns9360_serial_priv *priv = dev_get_priv(dev);

	/* Check for RX data available */
	if (!(readl(priv->base + NS9360_STAT_A) & STAT_A_RRDY))
		return -EAGAIN;

	return readl(priv->base + NS9360_FIFO) & 0xFF;
}

static int ns9360_serial_pending(struct udevice *dev, bool input)
{
	struct ns9360_serial_priv *priv = dev_get_priv(dev);
	u32 stat = readl(priv->base + NS9360_STAT_A);

	if (input)
		return (stat & STAT_A_RRDY) ? 1 : 0;
	else
		return (stat & STAT_A_TRDY) ? 0 : 1;
}

static int ns9360_serial_probe(struct udevice *dev)
{
	struct ns9360_serial_priv *priv = dev_get_priv(dev);
	fdt_addr_t addr;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base = (void __iomem *)addr;

	/* BBus clock = CONFIG_SYS_CLK_FREQ / 8 */
	priv->clk_rate = CONFIG_SYS_CLK_FREQ / 8;

	/*
	 * Enable the serial channel: 8N1 (8 data bits, no parity, 1 stop bit)
	 * CE=1, WLS=8bit
	 */
	writel(CTRL_A_CE | CTRL_A_WLS_8, priv->base + NS9360_CTRL_A);

	/*
	 * Set RX gap timer. In normal PLL mode, value of 0 with TRUN=1
	 * (bit 31 set) is used.
	 */
	writel(BIT(31), priv->base + NS9360_RX_CHAR_TMR);

	return 0;
}

static const struct dm_serial_ops ns9360_serial_ops = {
	.putc	= ns9360_serial_putc,
	.getc	= ns9360_serial_getc,
	.pending = ns9360_serial_pending,
	.setbrg	= ns9360_serial_setbrg,
};

static const struct udevice_id ns9360_serial_ids[] = {
	{ .compatible = "digi,ns9360-uart" },
	{ }
};

U_BOOT_DRIVER(ns9360_serial) = {
	.name	= "ns9360_serial",
	.id	= UCLASS_SERIAL,
	.of_match = ns9360_serial_ids,
	.probe	= ns9360_serial_probe,
	.ops	= &ns9360_serial_ops,
	.priv_auto = sizeof(struct ns9360_serial_priv),
};
