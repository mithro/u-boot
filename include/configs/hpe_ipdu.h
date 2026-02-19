/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * HPE Intelligent Modular PDU (AF531A) - Board Configuration
 *
 * NS9360B SoC (ARM926EJ-S, boots BE then switches to LE, 176.9 MHz)
 * 32 MB SDRAM, 16 MB NOR Flash (2x 8 MB), Ethernet
 */

#ifndef __HPE_IPDU_H
#define __HPE_IPDU_H

/*
 * Clock frequencies
 *
 * CONFIG_SYS_CLK_FREQ is set via Kconfig (176947200 Hz).
 * It equals crystal * (ND+1) / (1 << FS) per reference cc9c.h.
 * This is the post-FS PLL output (system clock), NOT the raw PLL.
 */
#define CRYSTAL_FREQ		29491200	/* 29.4912 MHz */
#define CPU_CLK_FREQ		(CONFIG_SYS_CLK_FREQ / 2)	/* 88.5 MHz */
#define AHB_CLK_FREQ		(CONFIG_SYS_CLK_FREQ / 4)	/* 44.2 MHz */
#define BBUS_CLK_FREQ		(CONFIG_SYS_CLK_FREQ / 8)	/* 22.1 MHz */

/* Memory layout */
#define CONFIG_SYS_SDRAM_BASE	0x00000000
#define CONFIG_SYS_SDRAM_SIZE	0x02000000	/* 32 MB */

/*
 * Initial RAM for the stack before full SDRAM init.
 * Use top of SDRAM as init RAM area (SDRAM is initialized in lowlevel_init).
 */
#define CFG_SYS_INIT_RAM_ADDR	CONFIG_SYS_SDRAM_BASE
#define CFG_SYS_INIT_RAM_SIZE	CONFIG_SYS_SDRAM_SIZE

/* NOR Flash - CFG_SYS_ prefix for modern U-Boot CFI driver */
#define CFG_SYS_FLASH_BASE		0x40000000

/* Serial console */
#define CONFIG_CONS_INDEX	1	/* Port A */

/* Ethernet */
#define NS9360_ETH_PHY_ADDRESS	0x0001

/* Boot configuration */
#define CONFIG_EXTRA_ENV_SETTINGS \
	"bootcmd_flash=bootm 0x40050000\0" \
	"bootcmd_tftp=tftp 0x200000 uImage; bootm 0x200000\0" \
	"bootargs=console=ttyNS1,115200 root=/dev/mtdblock3 rootfstype=jffs2\0"

#endif /* __HPE_IPDU_H */
