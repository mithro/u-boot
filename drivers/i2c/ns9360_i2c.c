// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 I2C master driver (driver model)
 *
 * Copyright (C) 2025
 *
 * Adapted from the reference NS9750 I2C driver (ns9750_i2c.c).
 *
 * The NS9360 I2C controller uses a command-based interface:
 *   - Write commands/data to TX register
 *   - Poll RX register for status/data
 *   - Commands: M_WRITE, M_READ, M_NOP, M_STOP
 *
 * Register map (from I2C base):
 *   0x00: DATA_TX (write) / DATA_RX (read)
 *   0x04: MASTER  (master device address)
 *   0x08: SLAVE   (slave device address)
 *   0x0C: CFG     (clock divider, config)
 */

#include <dm.h>
#include <i2c.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm/device_compat.h>

/* Register offsets */
#define NS_I2C_DATA		0x00	/* TX (write) / RX (read) */
#define NS_I2C_MASTER		0x04	/* Master device address */
#define NS_I2C_SLAVE		0x08	/* Slave device address */
#define NS_I2C_CFG		0x0C	/* Configuration */

/* TX register command/data fields */
#define TX_CMD_M_NOP		0x0000
#define TX_CMD_M_READ		0x0400
#define TX_CMD_M_WRITE		0x0500
#define TX_CMD_M_STOP		0x0600
#define TX_VAL			BIT(13)	/* Value valid */
#define TX_DATA_MASK		0x00FF

/* RX register status fields */
#define RX_IRQCD_MASK		0x0F00
#define RX_IRQCD_NO_IRQ		0x0000
#define RX_IRQCD_M_ARBIT	0x0100	/* Arbitration loss */
#define RX_IRQCD_M_NO_ACK	0x0200	/* No ACK from slave */
#define RX_IRQCD_M_TX_DATA	0x0300	/* TX data acknowledged */
#define RX_IRQCD_M_RX_DATA	0x0400	/* RX data ready */
#define RX_IRQCD_M_CMD_ACK	0x0500	/* Command acknowledged */
#define RX_DATA_MASK		0x00FF

/* Master address register */
#define MASTER_MDA_MASK		0x07FE
#define MASTER_MDA_SHIFT	1

/* CFG register fields */
#define CFG_IRQD		BIT(15)	/* IRQ disable */
#define CFG_CLREF_MASK		0x01FF
#define CFG_SFW_SHIFT		9
#define CFG_SFW_MASK		0x1E00

/* Timeouts */
#define I2C_TIMEOUT_MS		1000
#define I2C_POLL_US		100

struct ns9360_i2c_priv {
	void __iomem *base;
	unsigned long clk_rate;	/* BBus/CPU clock for divider calc */
};

static inline u32 i2c_readl(struct ns9360_i2c_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static inline void i2c_writel(struct ns9360_i2c_priv *priv, u32 val, u32 reg)
{
	writel(val, priv->base + reg);
}

/*
 * Send a command to the I2C TX register and wait for acknowledgment.
 * Returns 0 on success, negative on error.
 */
static int ns9360_i2c_send_cmd(struct ns9360_i2c_priv *priv, u16 cmd)
{
	u32 status;
	int timeout = I2C_TIMEOUT_MS * (1000 / I2C_POLL_US);

	i2c_writel(priv, cmd, NS_I2C_DATA);

	while (timeout--) {
		status = i2c_readl(priv, NS_I2C_DATA);
		switch (status & RX_IRQCD_MASK) {
		case RX_IRQCD_M_TX_DATA:
		case RX_IRQCD_M_CMD_ACK:
		case RX_IRQCD_M_RX_DATA:
			return status & RX_DATA_MASK;
		case RX_IRQCD_M_NO_ACK:
			return -EREMOTEIO;
		case RX_IRQCD_M_ARBIT:
			return -EAGAIN;
		case RX_IRQCD_NO_IRQ:
			break;
		default:
			return -EIO;
		}
		udelay(I2C_POLL_US);
	}

	return -ETIMEDOUT;
}

static int ns9360_i2c_xfer(struct udevice *dev, struct i2c_msg *msg, int nmsgs)
{
	struct ns9360_i2c_priv *priv = dev_get_priv(dev);
	int i, j, ret;

	for (i = 0; i < nmsgs; i++) {
		/* Set slave address */
		i2c_writel(priv,
			   (msg[i].addr << MASTER_MDA_SHIFT) & MASTER_MDA_MASK,
			   NS_I2C_MASTER);

		if (msg[i].flags & I2C_M_RD) {
			/* Read transaction */
			ret = ns9360_i2c_send_cmd(priv, TX_CMD_M_READ);
			if (ret < 0)
				goto stop;

			for (j = 0; j < msg[i].len; j++) {
				if (j == 0 && (ret >= 0)) {
					/* First byte already in return value */
					msg[i].buf[j] = ret & RX_DATA_MASK;
				}
				if (j < msg[i].len - 1) {
					ret = ns9360_i2c_send_cmd(priv,
								  TX_CMD_M_NOP);
					if (ret < 0)
						goto stop;
					msg[i].buf[j + 1] = ret & RX_DATA_MASK;
				}
			}
		} else {
			/* Write transaction: first byte with M_WRITE cmd */
			ret = ns9360_i2c_send_cmd(priv,
						  TX_CMD_M_WRITE | TX_VAL |
						  (msg[i].buf[0] & TX_DATA_MASK));
			if (ret < 0)
				goto stop;

			/* Remaining bytes with M_NOP */
			for (j = 1; j < msg[i].len; j++) {
				ret = ns9360_i2c_send_cmd(priv,
							  TX_CMD_M_NOP | TX_VAL |
							  (msg[i].buf[j] & TX_DATA_MASK));
				if (ret < 0)
					goto stop;
			}
		}
	}

stop:
	/* Always send STOP */
	ns9360_i2c_send_cmd(priv, TX_CMD_M_STOP);

	return (ret < 0) ? ret : 0;
}

static int ns9360_i2c_probe_chip(struct udevice *dev, uint chip_addr,
				 uint chip_flags)
{
	struct ns9360_i2c_priv *priv = dev_get_priv(dev);
	int ret;

	/* Set slave address */
	i2c_writel(priv,
		   (chip_addr << MASTER_MDA_SHIFT) & MASTER_MDA_MASK,
		   NS_I2C_MASTER);

	/* Try a read command to see if device ACKs */
	ret = ns9360_i2c_send_cmd(priv, TX_CMD_M_READ);
	ns9360_i2c_send_cmd(priv, TX_CMD_M_STOP);

	return (ret < 0) ? ret : 0;
}

static int ns9360_i2c_set_bus_speed(struct udevice *dev, unsigned int speed)
{
	struct ns9360_i2c_priv *priv = dev_get_priv(dev);
	u32 clref, cfg;

	/*
	 * I2C clock formula (from NS9750 Hardware Reference):
	 *   I2C_clk = CPU_clk / (4 * (2 * CLREF + 4))
	 *
	 * Solving for CLREF:
	 *   CLREF = (CPU_clk / (4 * speed) - 4) / 2
	 *
	 * Using BBus clock (CPU/8) as the reference:
	 *   CLREF = (bbus_clk / speed - 4) / 2
	 */
	if (speed == 0)
		return -EINVAL;

	clref = ((priv->clk_rate / speed) + 4) / 2;
	clref &= CFG_CLREF_MASK;

	cfg = clref | ((1 << CFG_SFW_SHIFT) & CFG_SFW_MASK);
	i2c_writel(priv, cfg, NS_I2C_CFG);

	return 0;
}

static int ns9360_i2c_probe(struct udevice *dev)
{
	struct ns9360_i2c_priv *priv = dev_get_priv(dev);
	fdt_addr_t addr;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base = (void __iomem *)addr;

	/*
	 * Default clock rate assumption: BBus clock ~22 MHz.
	 * This is CONFIG_SYS_CLK_FREQ / 8 for the NS9360.
	 */
	priv->clk_rate = CONFIG_SYS_CLK_FREQ / 8;

	/* Initialize with 100 kHz default */
	ns9360_i2c_set_bus_speed(dev, 100000);

	return 0;
}

static const struct dm_i2c_ops ns9360_i2c_ops = {
	.xfer		= ns9360_i2c_xfer,
	.probe_chip	= ns9360_i2c_probe_chip,
	.set_bus_speed	= ns9360_i2c_set_bus_speed,
};

static const struct udevice_id ns9360_i2c_ids[] = {
	{ .compatible = "digi,ns9360-i2c" },
	{ }
};

U_BOOT_DRIVER(ns9360_i2c) = {
	.name		= "ns9360_i2c",
	.id		= UCLASS_I2C,
	.of_match	= ns9360_i2c_ids,
	.probe		= ns9360_i2c_probe,
	.ops		= &ns9360_i2c_ops,
	.priv_auto	= sizeof(struct ns9360_i2c_priv),
};
