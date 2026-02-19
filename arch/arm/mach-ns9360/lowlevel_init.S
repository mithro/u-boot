/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NS9360 low-level initialization
 *
 * Initializes SDRAM controller, memory controller, and performs
 * the LR/IP relocation from flash mirror (0x0) to real flash
 * address (0x40000000).
 *
 * Will be fully implemented in a later phase. This stub allows
 * the build to succeed.
 */

.globl lowlevel_init

lowlevel_init:
	/* Return to caller - stub for now */
	mov	pc, lr
