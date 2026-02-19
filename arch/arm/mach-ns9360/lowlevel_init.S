/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NS9360 low-level initialization
 *
 * Copyright (C) 2025
 *
 * Initializes the memory controller, SDRAM timing, and performs
 * LR/IP register relocation from the flash mirror address (0x0)
 * to the real flash address (0x40000000).
 *
 * Adapted from the Digi CC9C platform.S reference implementation.
 *
 * Memory map at this point:
 *   0x00000000 - Flash mirror (CS0 flash aliased here at reset)
 *   0x40000000 - CS0 NOR flash (8 MB, boot device)
 *   0x50000000 - CS1 NOR flash (8 MB, secondary)
 *   0xA0700000 - Memory controller module
 *   0xA0900000 - System control module
 *
 * After this code runs:
 *   0x00000000 - SDRAM (32 MB)
 *   0x40000000 - CS0 NOR flash (8 MB)
 */

/* Register base addresses */
#define MEM_BASE		0xA0700000
#define SYS_BASE		0xA0900000
#define PHYS_FLASH		0x40000000

/* Memory controller registers (offsets from MEM_BASE) */
#define MEM_CTRL		0x0000
#define MEM_CFG			0x0008
#define MEM_DYN_CTRL		0x0020
#define MEM_DYN_REFRESH		0x0024
#define MEM_DYN_READ_CFG	0x0028
#define MEM_DYN_TRP		0x0030
#define MEM_DYN_TRAS		0x0034
#define MEM_DYN_TSREX		0x0038
#define MEM_DYN_TAPR		0x003C
#define MEM_DYN_TDAL		0x0040
#define MEM_DYN_TWR		0x0044
#define MEM_DYN_TRC		0x0048
#define MEM_DYN_TRFC		0x004C
#define MEM_DYN_TXSR		0x0050
#define MEM_DYN_TRRD		0x0054
#define MEM_DYN_TMRD		0x0058
#define MEM_DYN_CFG0		0x0100	/* CS4 dynamic config */
#define MEM_DYN_RAS_CAS0	0x0104	/* CS4 RAS/CAS latency */

/* MEM_CTRL bits */
#define MEM_CTRL_E		0x01	/* Enable memory controller */

/* MEM_DYN_CTRL bits */
#define DYN_CTRL_CE		0x01	/* Clock enable */
#define DYN_CTRL_BIT1		0x02	/* Must always be set to 1 */
#define DYN_CTRL_I_NORMAL	0x00	/* Normal operation */
#define DYN_CTRL_I_MODE		0x80	/* Mode register set */
#define DYN_CTRL_I_PALL		0x100	/* Precharge all */
#define DYN_CTRL_I_NOP		0x180	/* NOP */

/* MEM_DYN_CFG bits */
#define DYN_CFG_AM		0x4000	/* Address mapping enable */
#define DYN_CFG_BDMC		0x80000	/* Buffer disable mem clock */

/* MEM_DYN_RAS_CAS bits */
#define RAS_CAS_CAS2		0x200	/* CAS latency 2 */
#define RAS_CAS_RAS3		0x03	/* RAS latency 3 */

/* System control registers (offsets from SYS_BASE) */
#define SYS_AHB_TIMEOUT		0x0014
#define SYS_AHB_MON		0x0020

/*
 * SDRAM configuration for IS42S32800D-7BLI (32 MB)
 *   Organisation: 8M x 32 bit x 4 banks
 *   CAS latency: 2
 *   CAS latency address for mode register set: 0x22000
 *     (encodes CAS=2 in the address lines read during MODE command)
 *
 * Timing values in AHB clock cycles (~22.6 ns @ 44.2 MHz):
 *   tRP  = 1, tRAS = 4, tAPR = 1, tDAL = 5, tWR  = 1
 *   tRC  = 6, tRFC = 6, tRRD = 1, tMRD = 1
 */

/*
 * Address mapping for 32 MB (8M x 32) SDRAM on CS4:
 * Per NS9360 hardware reference, AM field selects row/column config.
 * Value 0x4680 = 8Mx32 (256Mbit), 4 banks, 13 row, 9 col
 */
#define SDRAM_DYN_CFG_VAL	0x4680
#define CAS_LATENCY_ADDR	0x00022000

.globl lowlevel_init

lowlevel_init:
	/*
	 * Save lr and ip. We need to adjust them later when jumping
	 * from the flash mirror (0x0) to the real flash address.
	 */
	mov	r10, lr
	mov	r11, ip

	/* Load base addresses */
	ldr	r0, =MEM_BASE
	ldr	r7, =SYS_BASE

	/*
	 * Phase 1: Enable memory controller and set SDRAM timing.
	 *
	 * The memory controller must be enabled before any SDRAM
	 * registers can be written.
	 */
	mov	r1, #MEM_CTRL_E
	str	r1, [r0, #MEM_CTRL]

	/* Set little-endian memory configuration (bit 0 = 0 for LE) */
	mov	r1, #0
	str	r1, [r0, #MEM_CFG]

	/* SDRAM timing parameters */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_TRP]		/* tRP = 1 */
	mov	r1, #4
	str	r1, [r0, #MEM_DYN_TRAS]	/* tRAS = 4 */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_TAPR]	/* tAPR = 1 */
	mov	r1, #5
	str	r1, [r0, #MEM_DYN_TDAL]	/* tDAL = 5 */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_TWR]		/* tWR = 1 */
	mov	r1, #6
	str	r1, [r0, #MEM_DYN_TRC]		/* tRC = 6 */
	str	r1, [r0, #MEM_DYN_TRFC]	/* tRFC = 6 */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_TRRD]	/* tRRD = 1 */
	str	r1, [r0, #MEM_DYN_TMRD]	/* tMRD = 1 */

	/* Read configuration delay */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_READ_CFG]

	/* Initial refresh rate (low value for stability during init) */
	mov	r1, #6
	str	r1, [r0, #MEM_DYN_REFRESH]

	/*
	 * Phase 2: Issue PALL (precharge all) command
	 * DYN_CTRL = I_PALL | BIT1 | CE
	 */
	ldr	r1, =(DYN_CTRL_I_PALL | DYN_CTRL_BIT1 | DYN_CTRL_CE)
	str	r1, [r0, #MEM_DYN_CTRL]

	/* Set minimum refresh for initial settling */
	mov	r1, #1
	str	r1, [r0, #MEM_DYN_REFRESH]

	/*
	 * Phase 3: Wait ~80 AHB cycles for SDRAM to settle
	 */
	mov	r1, #0x50
1:	subs	r1, r1, #1
	bne	1b

	/*
	 * Phase 4: Increase refresh rate and configure CS4
	 */
	mov	r1, #0x30
	str	r1, [r0, #MEM_DYN_REFRESH]

	/* CS4 dynamic memory configuration: 8Mx32, address mapping */
	ldr	r1, =(SDRAM_DYN_CFG_VAL | DYN_CFG_AM)
	str	r1, [r0, #MEM_DYN_CFG0]

	/* CS4 RAS/CAS latency: CAS=2, RAS=3 */
	ldr	r1, =(RAS_CAS_CAS2 | RAS_CAS_RAS3)
	str	r1, [r0, #MEM_DYN_RAS_CAS0]

	/*
	 * Phase 5: Issue MODE command and perform CAS latency read
	 * DYN_CTRL = I_MODE | BIT1 | CE
	 */
	ldr	r1, =(DYN_CTRL_I_MODE | DYN_CTRL_BIT1 | DYN_CTRL_CE)
	str	r1, [r0, #MEM_DYN_CTRL]

	/* Dummy read at CAS latency address to set SDRAM mode register */
	ldr	r3, =CAS_LATENCY_ADDR
	ldr	r3, [r3]

	/*
	 * Phase 6: Switch to NORMAL operation mode
	 * DYN_CTRL = I_NORMAL | BIT1 | CE
	 */
	ldr	r1, =(DYN_CTRL_I_NORMAL | DYN_CTRL_BIT1 | DYN_CTRL_CE)
	str	r1, [r0, #MEM_DYN_CTRL]

	/* Enable BDMC (buffer disable memory clock when idle) */
	ldr	r1, =(SDRAM_DYN_CFG_VAL | DYN_CFG_AM | DYN_CFG_BDMC)
	str	r1, [r0, #MEM_DYN_CFG0]

	/*
	 * Phase 7: Set up AHB arbiter/monitor
	 */
	ldr	r1, =0x01000100
	str	r1, [r7, #SYS_AHB_TIMEOUT]
	mov	r1, #0x05			/* BMTC_GEN_IRQ | BATC_GEN_IRQ */
	str	r1, [r7, #SYS_AHB_MON]

	/*
	 * Phase 8: Relocate LR and IP registers
	 *
	 * At reset, flash (0x40000000) is mirrored to 0x00000000.
	 * Now that SDRAM is mapped at 0x0, we need to adjust return
	 * addresses from the mirror range to the real flash range.
	 *
	 * If we were called from an address < 0x40000000, add the
	 * flash base offset. If already in the flash range, no change.
	 */
	ldr	r1, =PHYS_FLASH
	cmp	r10, r1
	addlo	r10, r10, r1
	cmp	r11, r1
	addlo	r11, r11, r1

	/* Restore lr and ip, return */
	mov	lr, r10
	mov	ip, r11
	mov	pc, lr
