// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 clock management
 *
 * Copyright (C) 2025
 *
 * Clock tree:
 *   Crystal (29.4912 MHz) -> PLL (ND, FS) -> System Clock (176.9 MHz)
 *     -> CPU clock  = sys_clk / 2 = 88.5 MHz
 *     -> AHB clock  = sys_clk / 4 = 44.2 MHz
 *     -> BBus clock = sys_clk / 8 = 22.1 MHz
 */

#include <asm/io.h>

#define NS9360_SYS_BASE		0xA0900000
#define NS9360_SYS_PLL		0x0188

#define CRYSTAL_FREQ		29491200	/* 29.4912 MHz */

/*
 * Read the PLL register and calculate the system clock frequency.
 *
 * SYS_PLL register (0xA0900188):
 *   ND = bits [20:16] -> multiply by (ND+1)
 *   FS = bits [24:23] -> divide by 2^FS
 *
 * Result: crystal * (ND+1) / (1 << FS) = CONFIG_SYS_CLK_FREQ
 *
 * For HPE iPDU: ND=11, FS=1 -> 29.4912 * 12 / 2 = 176.9472 MHz
 */
unsigned long ns9360_get_sys_clk(void)
{
	u32 pll_reg;
	unsigned int nd, fs;

	pll_reg = readl(NS9360_SYS_BASE + NS9360_SYS_PLL);
	nd = (pll_reg >> 16) & 0x1f;
	fs = (pll_reg >> 23) & 0x3;

	return (unsigned long)CRYSTAL_FREQ * (nd + 1) / (1 << fs);
}

unsigned long ns9360_get_cpu_clk(void)
{
	return ns9360_get_sys_clk() / 2;
}

unsigned long ns9360_get_ahb_clk(void)
{
	return ns9360_get_sys_clk() / 4;
}

unsigned long ns9360_get_bbus_clk(void)
{
	return ns9360_get_sys_clk() / 8;
}
