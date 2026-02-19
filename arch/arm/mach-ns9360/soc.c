// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 SoC support
 *
 * Copyright (C) 2025
 */

#include <init.h>
#include <irq_func.h>
#include <asm/io.h>

/* System control base address */
#define NS9360_SYS_BASE		0xA0900000
#define NS9360_SYS_MISC	0x0184
#define NS9360_SYS_PLL		0x0188

/* BBus base address */
#define NS9360_BBUS_BASE	0x90600000
#define NS9360_BBUS_MASTER_RESET 0x00

/* GPIO configuration register base offsets from BBUS */
#define NS9360_GPIO_CFG_BASE	0x10

/*
 * Set a GPIO pin's function and direction.
 *
 * Each GPIO uses 4 bits in a configuration register:
 *   [1:0] = function select (0-2: peripheral, 3: GPIO mode)
 *   [2]   = invert
 *   [3]   = direction (0: input, 1: output) - only when func=3
 *
 * GPIOs 0-55:  config at BBUS + 0x10 + (gpio/8)*4, bits (gpio%8)*4
 * GPIOs 56-72: config at BBUS + 0x100 + ((gpio-56)/8)*4, bits ((gpio-56)%8)*4
 */
void ns9360_gpio_configure(unsigned int gpio, unsigned int func, int output)
{
	u32 reg_addr;
	u32 val;
	unsigned int shift;

	if (gpio <= 55) {
		reg_addr = NS9360_BBUS_BASE + NS9360_GPIO_CFG_BASE +
			   (gpio / 8) * 4;
		shift = (gpio % 8) * 4;
	} else if (gpio <= 72) {
		reg_addr = NS9360_BBUS_BASE + 0x100 +
			   ((gpio - 56) / 8) * 4;
		shift = ((gpio - 56) % 8) * 4;
	} else {
		return;
	}

	val = readl(reg_addr);
	val &= ~(0xf << shift);
	val |= (func & 0x3) << shift;
	if (output)
		val |= (1 << (shift + 3));
	writel(val, reg_addr);
}

/*
 * Deassert BBus master reset - this enables all BBus peripherals
 * (serial ports, GPIO, DMA, etc.). Must be called before any
 * BBus peripheral access.
 */
void ns9360_bbus_init(void)
{
	writel(0, NS9360_BBUS_BASE + NS9360_BBUS_MASTER_RESET);
}

/*
 * Print CPU information at boot
 */
int print_cpuinfo(void)
{
	u32 pll_reg;
	unsigned int nd, fs;
	unsigned long sys_clk;

	pll_reg = readl(NS9360_SYS_BASE + NS9360_SYS_PLL);
	nd = (pll_reg >> 16) & 0x1f;
	fs = (pll_reg >> 23) & 0x3;
	sys_clk = 29491200UL * (nd + 1) / (1 << fs);

	printf("CPU:   NS9360 ARM926EJ-S @ %lu MHz\n", sys_clk / 1000000);
	printf("       PLL: ND=%u FS=%u, SysClk=%lu Hz\n", nd, fs, sys_clk);

	return 0;
}

/*
 * Reset the CPU by writing to the system misc register
 */
void reset_cpu(void)
{
	/*
	 * The NS9360 can be reset by triggering the watchdog or
	 * writing specific bits. For now, use an infinite loop
	 * as a placeholder — real reset needs hardware watchdog.
	 */
	printf("Resetting...\n");

	/* Disable interrupts and loop forever */
	disable_interrupts();
	while (1)
		;
}
