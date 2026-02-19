// SPDX-License-Identifier: GPL-2.0+
/*
 * HPE Intelligent Modular PDU (AF531A) board support
 *
 * Copyright (C) 2025
 *
 * Board: HPE iPDU with Digi NS9360B SoC
 * - 32 MB SDRAM (ISSI IS42S32800D-7BLI)
 * - 16 MB NOR Flash (2x Macronix MX29LV640EBXEI)
 * - ICS1893AFLF Ethernet PHY
 * - Debug UART on Port A (GPIO 8/9), 115200 8N1
 */

#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

/* NS9360 register bases */
#define NS9360_BBUS_BASE	0x90600000
#define NS9360_SYS_BASE		0xA0900000
#define NS9360_MEM_BASE		0xA0700000

/* BBus registers */
#define NS9360_BBUS_MASTER_RESET 0x00

/* System control - chip select registers */
#define NS9360_SYS_CS_STATIC_BASE(n)	(0x01F0 + (n) * 8)
#define NS9360_SYS_CS_STATIC_MASK(n)	(0x01F4 + (n) * 8)

/* Memory controller - static memory registers */
#define NS9360_MEM_STAT_CFG(n)		(0x0200 + (n) * 0x20)
#define NS9360_MEM_STAT_WAIT_WEN(n)	(0x0204 + (n) * 0x20)
#define NS9360_MEM_STAT_WAIT_OEN(n)	(0x0208 + (n) * 0x20)
#define NS9360_MEM_STAT_RD(n)		(0x020C + (n) * 0x20)
#define NS9360_MEM_STAT_PAGE(n)		(0x0210 + (n) * 0x20)
#define NS9360_MEM_STAT_WR(n)		(0x0214 + (n) * 0x20)
#define NS9360_MEM_STAT_TURN(n)		(0x0218 + (n) * 0x20)

/* Static memory config bits */
#define MEM_STAT_CFG_MW_16	(1 << 0)	/* 16-bit bus width */
#define MEM_STAT_CFG_PB		(1 << 3)	/* Page burst enable */

/* External declarations */
extern void ns9360_bbus_init(void);
extern void ns9360_gpio_configure(unsigned int gpio, unsigned int func,
				  int output);

int board_init(void)
{
	/*
	 * Deassert BBus master reset to enable all BBus peripherals
	 * (serial, GPIO, DMA, etc.)
	 */
	ns9360_bbus_init();

	/*
	 * Configure GPIO 65 as output (BOOTMUX control).
	 * This selects the boot source configuration.
	 * func=3 means GPIO mode, output=1.
	 */
	ns9360_gpio_configure(65, 3, 1);

	/*
	 * Set up CS1 static memory controller for secondary NOR flash
	 * at 0x50000000. CS0 (boot flash at 0x40000000) is automatically
	 * configured by hardware at reset.
	 */
	writel(0x50000000, NS9360_SYS_BASE + NS9360_SYS_CS_STATIC_BASE(1));
	writel(0xFF000001, NS9360_SYS_BASE + NS9360_SYS_CS_STATIC_MASK(1));

	writel(MEM_STAT_CFG_MW_16 | MEM_STAT_CFG_PB,
	       NS9360_MEM_BASE + NS9360_MEM_STAT_CFG(1));
	writel(0x2, NS9360_MEM_BASE + NS9360_MEM_STAT_WAIT_WEN(1));
	writel(0x2, NS9360_MEM_BASE + NS9360_MEM_STAT_WAIT_OEN(1));
	writel(0x6, NS9360_MEM_BASE + NS9360_MEM_STAT_RD(1));
	writel(0x6, NS9360_MEM_BASE + NS9360_MEM_STAT_WR(1));

	/*
	 * Configure UART GPIO pins for debug console (Port A):
	 *   GPIO 8 = TxD (function 0, output)
	 *   GPIO 9 = RxD (function 0, input)
	 */
	ns9360_gpio_configure(8, 0, 1);   /* TxD - func 0, output */
	ns9360_gpio_configure(9, 0, 0);   /* RxD - func 0, input */

	return 0;
}

int dram_init(void)
{
	/* 32 MB SDRAM at 0x00000000 */
	gd->ram_size = SZ_32M;
	return 0;
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size = SZ_32M;
	return 0;
}
