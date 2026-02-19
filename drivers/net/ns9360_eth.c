// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 Ethernet MAC driver (driver model)
 *
 * Copyright (C) 2025
 *
 * Adapted from the reference NS9750 Ethernet driver for modern U-Boot DM_ETH.
 *
 * The NS9360 has an integrated 10/100 Ethernet MAC with:
 *   - MII interface to external PHY (ICS1893AFLF on HPE iPDU)
 *   - 4-channel RX DMA (we use channel A only)
 *   - Single TX DMA channel
 *   - Integrated MDIO management interface
 *   - MAC address filter with promiscuous mode
 */

#include <dm.h>
#include <malloc.h>
#include <net.h>
#include <miiphy.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm/device_compat.h>

/* ─── Register offsets from Ethernet base (0xA0600000) ─── */

/* Engine global control/status */
#define EGCR1		0x0000
#define EGCR2		0x0004
#define EGSR		0x0008

/* MAC registers */
#define MAC1		0x0400
#define MAC2		0x0404
#define IPGT		0x0408
#define IPGR		0x040C
#define SUPP		0x0418

/* MII management */
#define MCFG		0x0420
#define MCMD		0x0424
#define MADR		0x0428
#define MWTD		0x042C
#define MRDD		0x0430
#define MIND		0x0434

/* MAC address */
#define SA1		0x0440
#define SA2		0x0444
#define SA3		0x0448

/* Address filter */
#define SAFR		0x0500

/* DMA / buffer descriptor pointers */
#define RXAPTR		0x0A00
#define RXBPTR		0x0A04
#define RXCPTR		0x0A08
#define RXDPTR		0x0A0C
#define EINTR		0x0A10
#define EINTREN		0x0A14
#define TXPTR		0x0A18
#define RXFREE		0x0A3C

/* ─── Bit definitions ─── */

/* EGCR1 */
#define EGCR1_ERX		BIT(31)
#define EGCR1_ERXDMA		BIT(30)
#define EGCR1_ERXSHT		BIT(28)
#define EGCR1_ETX		BIT(23)
#define EGCR1_ETXDMA		BIT(22)
#define EGCR1_ERXINIT		BIT(19)
#define EGCR1_MAC_HRST		BIT(9)

/* EGCR2 */
#define EGCR2_TCLER		BIT(3)
#define EGCR2_STEN		BIT(0)

/* EGSR */
#define EGSR_RXINIT		BIT(19)

/* MAC1 */
#define MAC1_RXEN		BIT(0)
#define MAC1_RPEMCSR		BIT(11)
#define MAC1_RPERFUN		BIT(10)
#define MAC1_RPEMCST		BIT(9)
#define MAC1_RPETFUN		BIT(8)

/* MAC2 */
#define MAC2_FULLD		BIT(0)
#define MAC2_CRCEN		BIT(4)
#define MAC2_PADEN		BIT(5)

/* SUPP */
#define SUPP_RPERMII		BIT(9)

/* MCMD */
#define MCMD_READ		BIT(0)

/* MIND */
#define MIND_BUSY		BIT(0)
#define MIND_NVALID		BIT(2)

/* SAFR */
#define SAFR_PRO		BIT(3)
#define SAFR_BROAD		BIT(0)

/* EINTR status bits */
#define EINTR_RXDONEA		BIT(22)
#define EINTR_TXDONE		BIT(2)
#define EINTR_TXERR		BIT(1)

/* ─── Buffer descriptor format ─── */

/* RX buffer descriptor: 4 x 32-bit words */
struct ns9360_rx_desc {
	u32 src;	/* Buffer address */
	u32 len;	/* Buffer length (bits 10:0) */
	u32 reserved;
	u32 flags;	/* Status/control: [31] wrap, [30] int, [29] enable, [28] full */
};

#define RX_FLAG_WRAP	BIT(31)
#define RX_FLAG_INT	BIT(30)
#define RX_FLAG_EN	BIT(29)
#define RX_FLAG_FULL	BIT(28)

/* TX buffer descriptor: 4 x 32-bit words */
struct ns9360_tx_desc {
	u32 src;	/* Buffer address */
	u32 len;	/* Packet length (bits 9:0) */
	u32 reserved;
	u32 flags;	/* [31] wrap, [30] int, [29] last, [28] full */
};

#define TX_FLAG_WRAP	BIT(31)
#define TX_FLAG_INT	BIT(30)
#define TX_FLAG_LAST	BIT(29)
#define TX_FLAG_FULL	BIT(28)

/* ─── Driver configuration ─── */

#define NS9360_RX_NUM		4	/* Number of RX descriptors */
#define NS9360_MAX_FRAME	1522
#define NS9360_MII_TIMEOUT	10000	/* MII poll iterations */
#define NS9360_TX_TIMEOUT	5000	/* TX timeout in ms */
#define NS9360_PHY_ADDR		1	/* Default PHY address */

struct ns9360_eth_priv {
	void __iomem *base;
	struct ns9360_rx_desc rx_desc[NS9360_RX_NUM] __aligned(16);
	struct ns9360_tx_desc tx_desc __aligned(16);
	u8 rx_buf[NS9360_RX_NUM][NS9360_MAX_FRAME] __aligned(4);
	int rx_idx;
	struct mii_dev *bus;
	int phy_addr;
};

/* ─── Register access helpers ─── */

static inline u32 eth_readl(struct ns9360_eth_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static inline void eth_writel(struct ns9360_eth_priv *priv, u32 val, u32 reg)
{
	writel(val, priv->base + reg);
}

/* ─── MII/MDIO ─── */

static int ns9360_mii_wait(struct ns9360_eth_priv *priv)
{
	int timeout = NS9360_MII_TIMEOUT;

	while ((eth_readl(priv, MIND) & (MIND_BUSY | MIND_NVALID)) && timeout--)
		udelay(1);

	return timeout > 0 ? 0 : -ETIMEDOUT;
}

static int ns9360_mdio_read(struct mii_dev *bus, int addr, int devad, int reg)
{
	struct ns9360_eth_priv *priv = bus->priv;
	u16 val;

	eth_writel(priv, (addr << 8) | (reg & 0x1f), MADR);
	eth_writel(priv, MCMD_READ, MCMD);

	if (ns9360_mii_wait(priv)) {
		eth_writel(priv, 0, MCMD);
		return -ETIMEDOUT;
	}

	val = eth_readl(priv, MRDD) & 0xffff;
	eth_writel(priv, 0, MCMD);

	return val;
}

static int ns9360_mdio_write(struct mii_dev *bus, int addr, int devad,
			     int reg, u16 val)
{
	struct ns9360_eth_priv *priv = bus->priv;

	eth_writel(priv, (addr << 8) | (reg & 0x1f), MADR);
	eth_writel(priv, val, MWTD);

	return ns9360_mii_wait(priv);
}

/* ─── MAC reset ─── */

static void ns9360_eth_reset(struct ns9360_eth_priv *priv)
{
	u32 val;

	/* MAC hard reset */
	val = eth_readl(priv, EGCR1);
	val |= EGCR1_ERX | EGCR1_ETX | EGCR1_MAC_HRST;
	eth_writel(priv, val, EGCR1);
	udelay(10);
	val &= ~EGCR1_MAC_HRST;
	eth_writel(priv, val, EGCR1);

	/* Reset MAC1 sub-modules */
	eth_writel(priv, MAC1_RPEMCSR | MAC1_RPERFUN | MAC1_RPEMCST | MAC1_RPETFUN,
		   MAC1);
	udelay(5);
	eth_writel(priv, 0, MAC1);
}

/* ─── PHY init ─── */

static int ns9360_phy_init(struct ns9360_eth_priv *priv)
{
	u16 status;
	int timeout;

	/*
	 * Configure MDIO clock: AHB clock (~44 MHz) / 20 = 2.2 MHz
	 * MCFG clock select bits [4:2]:
	 *   0x14 = divide by 20
	 */
	eth_writel(priv, 0x14, MCFG);
	udelay(100);

	/* Reset PHY via MII register 0, bit 15 */
	ns9360_mdio_write(priv->bus, priv->phy_addr, MDIO_DEVAD_NONE,
			  MII_BMCR, BMCR_RESET);
	udelay(3000);

	/* Wait for reset to complete */
	timeout = 1000;
	while (timeout--) {
		status = ns9360_mdio_read(priv->bus, priv->phy_addr,
					  MDIO_DEVAD_NONE, MII_BMCR);
		if (!(status & BMCR_RESET))
			break;
		udelay(100);
	}

	if (timeout <= 0) {
		debug("NS9360 ETH: PHY reset timeout\n");
		return -ETIMEDOUT;
	}

	/* Enable auto-negotiation */
	ns9360_mdio_write(priv->bus, priv->phy_addr, MDIO_DEVAD_NONE,
			  MII_ADVERTISE,
			  ADVERTISE_100FULL | ADVERTISE_100HALF |
			  ADVERTISE_10FULL | ADVERTISE_10HALF |
			  ADVERTISE_CSMA);

	ns9360_mdio_write(priv->bus, priv->phy_addr, MDIO_DEVAD_NONE,
			  MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

	/* Wait for link (up to 5 seconds) */
	timeout = 50;
	while (timeout--) {
		status = ns9360_mdio_read(priv->bus, priv->phy_addr,
					  MDIO_DEVAD_NONE, MII_BMSR);
		if (status & BMSR_LSTATUS)
			break;
		mdelay(100);
	}

	if (timeout <= 0)
		debug("NS9360 ETH: link timeout (continuing anyway)\n");

	/* Configure MAC for link speed/duplex */
	if (status & BMSR_LSTATUS) {
		u16 lpa = ns9360_mdio_read(priv->bus, priv->phy_addr,
					    MDIO_DEVAD_NONE, MII_LPA);
		u32 mac2 = eth_readl(priv, MAC2) & ~MAC2_FULLD;
		u32 ipgt = 0x12; /* half duplex default */

		if ((lpa & LPA_100FULL) || (lpa & LPA_10FULL)) {
			mac2 |= MAC2_FULLD;
			ipgt = 0x15;
		}
		eth_writel(priv, mac2, MAC2);
		eth_writel(priv, ipgt, IPGT);
	}

	return 0;
}

/* ─── DM_ETH operations ─── */

static int ns9360_eth_start(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct eth_pdata *pdata = dev_get_plat(dev);
	int i;

	/* Reset the MAC */
	ns9360_eth_reset(priv);

	/* Configure MAC2: CRC + padding + full duplex */
	eth_writel(priv, MAC2_CRCEN | MAC2_PADEN | MAC2_FULLD, MAC2);

	/* Set address filter to promiscuous for bring-up */
	eth_writel(priv, SAFR_PRO | SAFR_BROAD, SAFR);

	/* Load MAC address: SA registers use swapped byte pairs */
	eth_writel(priv, (pdata->enetaddr[5] << 8) | pdata->enetaddr[4], SA1);
	eth_writel(priv, (pdata->enetaddr[3] << 8) | pdata->enetaddr[2], SA2);
	eth_writel(priv, (pdata->enetaddr[1] << 8) | pdata->enetaddr[0], SA3);

	/* MII mode (not RMII) */
	eth_writel(priv, eth_readl(priv, SUPP) & ~SUPP_RPERMII, SUPP);

	/* Initialize PHY */
	ns9360_phy_init(priv);

	/* Set up RX descriptors */
	priv->rx_idx = 0;
	for (i = 0; i < NS9360_RX_NUM; i++) {
		priv->rx_desc[i].src = (u32)(ulong)priv->rx_buf[i];
		priv->rx_desc[i].len = NS9360_MAX_FRAME;
		priv->rx_desc[i].reserved = 0;
		priv->rx_desc[i].flags = RX_FLAG_INT | RX_FLAG_EN;
		if (i == NS9360_RX_NUM - 1)
			priv->rx_desc[i].flags |= RX_FLAG_WRAP;
	}

	/* Point RX channel A to our descriptors, disable B/C/D */
	eth_writel(priv, (u32)(ulong)priv->rx_desc, RXAPTR);
	eth_writel(priv, 0, RXBPTR);
	eth_writel(priv, 0, RXCPTR);
	eth_writel(priv, 0, RXDPTR);
	udelay(1);

	/* Clear TX pointer */
	eth_writel(priv, 0, TXPTR);

	/* ERXINIT handshake: enable RX init */
	eth_writel(priv, EGCR1_ERX | EGCR1_ETX | EGCR1_ERXINIT, EGCR1);
	udelay(5);

	/* Wait for RX init completion */
	i = 1000;
	while (!(eth_readl(priv, EGSR) & EGSR_RXINIT) && i--)
		udelay(1);

	/* Acknowledge and clear ERXINIT */
	eth_writel(priv, EGSR_RXINIT, EGSR);
	eth_writel(priv, eth_readl(priv, EGCR1) & ~EGCR1_ERXINIT, EGCR1);

	/* Enable statistics */
	eth_writel(priv, EGCR2_STEN, EGCR2);

	/* Enable MAC receive */
	eth_writel(priv, MAC1_RXEN, MAC1);

	/* Enable full DMA operation */
	eth_writel(priv, EGCR1_ERX | EGCR1_ETX | EGCR1_ERXDMA | EGCR1_ETXDMA |
		   EGCR1_ERXSHT, EGCR1);

	/* Clear any pending interrupts */
	udelay(100);
	eth_writel(priv, eth_readl(priv, EINTR), EINTR);

	return 0;
}

static void ns9360_eth_stop(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);

	/* Disable RX/TX and DMA */
	eth_writel(priv, 0, EGCR1);
	eth_writel(priv, 0, MAC1);
}

static int ns9360_eth_send(struct udevice *dev, void *packet, int length)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct ns9360_tx_desc *txd = &priv->tx_desc;
	ulong start;

	/* Acknowledge any old TX status */
	eth_writel(priv, eth_readl(priv, EINTR) & (EINTR_TXDONE | EINTR_TXERR),
		   EINTR);

	/* Set up TX descriptor */
	txd->src = (u32)(ulong)packet;
	txd->len = length;
	txd->reserved = 0;
	txd->flags = TX_FLAG_WRAP | TX_FLAG_INT | TX_FLAG_LAST | TX_FLAG_FULL;

	/* Point TX to our descriptor */
	eth_writel(priv, (u32)(ulong)txd, TXPTR);

	/* Trigger transmission by toggling TCLER */
	eth_writel(priv, eth_readl(priv, EGCR2) & ~EGCR2_TCLER, EGCR2);
	eth_writel(priv, eth_readl(priv, EGCR2) | EGCR2_TCLER, EGCR2);

	/* Wait for completion */
	start = get_timer(0);
	while (!(eth_readl(priv, EINTR) & (EINTR_TXDONE | EINTR_TXERR))) {
		if (get_timer(start) > NS9360_TX_TIMEOUT) {
			debug("NS9360 ETH: TX timeout\n");
			return -ETIMEDOUT;
		}
	}

	if (eth_readl(priv, EINTR) & EINTR_TXERR) {
		eth_writel(priv, EINTR_TXERR, EINTR);
		debug("NS9360 ETH: TX error\n");
		return -EIO;
	}

	eth_writel(priv, EINTR_TXDONE, EINTR);
	return 0;
}

static int ns9360_eth_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct ns9360_rx_desc *rxd;
	u32 status;
	int len;

	/* Check for RX completion */
	status = eth_readl(priv, EINTR) & EINTR_RXDONEA;
	if (!status)
		return -EAGAIN;

	/* Acknowledge */
	eth_writel(priv, status, EINTR);

	rxd = &priv->rx_desc[priv->rx_idx];

	/* Check if this descriptor has data */
	if (!(rxd->flags & RX_FLAG_FULL))
		return -EAGAIN;

	/* Get packet length (minus 4-byte CRC) */
	len = (rxd->len & 0x7ff) - 4;
	if (len <= 0)
		return -EAGAIN;

	*packetp = priv->rx_buf[priv->rx_idx];
	return len;
}

static int ns9360_eth_free_pkt(struct udevice *dev, uchar *packet, int length)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct ns9360_rx_desc *rxd = &priv->rx_desc[priv->rx_idx];

	/* Reset descriptor for reuse */
	rxd->len = NS9360_MAX_FRAME;
	rxd->flags &= ~RX_FLAG_FULL;

	/* Tell DMA this buffer is available */
	eth_writel(priv, eth_readl(priv, RXFREE) | 0x1, RXFREE);

	priv->rx_idx = (priv->rx_idx + 1) % NS9360_RX_NUM;
	return 0;
}

static int ns9360_eth_write_hwaddr(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct eth_pdata *pdata = dev_get_plat(dev);

	eth_writel(priv, (pdata->enetaddr[5] << 8) | pdata->enetaddr[4], SA1);
	eth_writel(priv, (pdata->enetaddr[3] << 8) | pdata->enetaddr[2], SA2);
	eth_writel(priv, (pdata->enetaddr[1] << 8) | pdata->enetaddr[0], SA3);

	return 0;
}

static int ns9360_eth_of_to_plat(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);
	struct eth_pdata *pdata = dev_get_plat(dev);

	pdata->iobase = dev_read_addr(dev);
	if (pdata->iobase == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base = (void __iomem *)pdata->iobase;
	priv->phy_addr = NS9360_PHY_ADDR;

	return 0;
}

static int ns9360_eth_probe(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);

	/* Register MDIO bus */
	priv->bus = mdio_alloc();
	if (!priv->bus)
		return -ENOMEM;

	priv->bus->read = ns9360_mdio_read;
	priv->bus->write = ns9360_mdio_write;
	priv->bus->priv = priv;
	snprintf(priv->bus->name, sizeof(priv->bus->name), "ns9360_mdio");

	return mdio_register(priv->bus);
}

static int ns9360_eth_remove(struct udevice *dev)
{
	struct ns9360_eth_priv *priv = dev_get_priv(dev);

	if (priv->bus)
		mdio_unregister(priv->bus);

	return 0;
}

static const struct eth_ops ns9360_eth_ops = {
	.start		= ns9360_eth_start,
	.stop		= ns9360_eth_stop,
	.send		= ns9360_eth_send,
	.recv		= ns9360_eth_recv,
	.free_pkt	= ns9360_eth_free_pkt,
	.write_hwaddr	= ns9360_eth_write_hwaddr,
};

static const struct udevice_id ns9360_eth_ids[] = {
	{ .compatible = "digi,ns9360-eth" },
	{ }
};

U_BOOT_DRIVER(ns9360_eth) = {
	.name		= "ns9360_eth",
	.id		= UCLASS_ETH,
	.of_match	= ns9360_eth_ids,
	.of_to_plat	= ns9360_eth_of_to_plat,
	.probe		= ns9360_eth_probe,
	.remove		= ns9360_eth_remove,
	.ops		= &ns9360_eth_ops,
	.priv_auto	= sizeof(struct ns9360_eth_priv),
	.plat_auto	= sizeof(struct eth_pdata),
};
