// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 clock driver (driver model)
 *
 * Copyright (C) 2025
 *
 * Clock tree:
 *   Crystal (29.4912 MHz) -> PLL -> System Clock (176.9 MHz)
 *     -> CPU clock  = sys_clk / 2 = 88.5 MHz
 *     -> AHB clock  = sys_clk / 4 = 44.2 MHz
 *     -> BBus clock = sys_clk / 8 = 22.1 MHz
 *
 * PLL register (SYS_PLL @ 0xA0900188):
 *   ND = bits [20:16] -> multiply by (ND+1)
 *   FS = bits [24:23] -> divide by 2^FS
 *   sys_clk = crystal * (ND+1) / (1 << FS)
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>

#define NS9360_SYS_BASE		0xA0900000
#define NS9360_SYS_PLL		0x0188

#define CRYSTAL_FREQ		29491200UL	/* 29.4912 MHz */

struct ns9360_clk_priv {
	unsigned long sys_clk;
};

static unsigned long ns9360_read_sys_clk(void)
{
	u32 pll_reg;
	unsigned int nd, fs;

	pll_reg = readl(NS9360_SYS_BASE + NS9360_SYS_PLL);
	nd = (pll_reg >> 16) & 0x1f;
	fs = (pll_reg >> 23) & 0x3;

	return CRYSTAL_FREQ * (nd + 1) / (1 << fs);
}

static ulong ns9360_clk_get_rate(struct clk *clk)
{
	struct ns9360_clk_priv *priv = dev_get_priv(clk->dev);

	/*
	 * Clock IDs (matching device tree fixed-factor-clock convention):
	 * The PLL outputs the system clock. Derived clocks divide by 2/4/8.
	 * For this simple SoC, we return the PLL rate for any clock request
	 * and let the DT fixed-factor-clock nodes handle the division.
	 */
	return priv->sys_clk;
}

static int ns9360_clk_enable(struct clk *clk)
{
	/* Clocks are always enabled on NS9360 */
	return 0;
}

static int ns9360_clk_disable(struct clk *clk)
{
	/* Cannot disable clocks on NS9360 */
	return 0;
}

static struct clk_ops ns9360_clk_ops = {
	.get_rate	= ns9360_clk_get_rate,
	.enable		= ns9360_clk_enable,
	.disable	= ns9360_clk_disable,
};

static int ns9360_clk_probe(struct udevice *dev)
{
	struct ns9360_clk_priv *priv = dev_get_priv(dev);

	priv->sys_clk = ns9360_read_sys_clk();

	return 0;
}

static const struct udevice_id ns9360_clk_ids[] = {
	{ .compatible = "digi,ns9360-pll" },
	{ }
};

U_BOOT_DRIVER(ns9360_clk) = {
	.name		= "ns9360_clk",
	.id		= UCLASS_CLK,
	.of_match	= ns9360_clk_ids,
	.probe		= ns9360_clk_probe,
	.ops		= &ns9360_clk_ops,
	.priv_auto	= sizeof(struct ns9360_clk_priv),
};
