// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 timer support
 *
 * Copyright (C) 2025
 *
 * Uses Timer 0 of the NS9360 system control module as a free-running
 * counter. The timer counts down from the reload value. We use the AHB
 * clock (CONFIG_SYS_CLK_FREQ / 4) as the timer clock source.
 *
 * Timer registers (SYS_BASE = 0xA0900000):
 *   SYS_TRC(n) = 0x0044 + n*4  - Timer Reload Count
 *   SYS_TR(n)  = 0x0084 + n*4  - Timer Read (current value)
 *   SYS_TC(n)  = 0x0190 + n*4  - Timer Control
 *   SYS_TIS    = 0x0170        - Timer Interrupt Status
 */

#include <init.h>
#include <time.h>
#include <asm/global_data.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

#define NS9360_SYS_BASE		0xA0900000

/* Timer register offsets */
#define SYS_TRC(n)		(0x0044 + (n) * 4)
#define SYS_TR(n)		(0x0084 + (n) * 4)
#define SYS_TC(n)		(0x0190 + (n) * 4)
#define SYS_TIS			0x0170

/*
 * Timer Control register bits:
 *   bit 15 = Timer Enable
 *   bit 14 = 32-bit mode (0) vs 16-bit (1)  -- we want 32-bit = 0
 *   bit 13 = Reload Enable (auto-reload on underflow)
 *   bit 12 = Interrupt Enable
 *   bits [3:2] = Clock source: 00 = AHB clock
 *   bit 1  = Debug halt
 *   bit 0  = Up(1)/Down(0)
 */
#define TC_ENABLE		BIT(15)
#define TC_RELOAD_EN		BIT(13)
#define TC_CLK_AHB		(0 << 2)
#define TC_DOWN			0

/* Use Timer 0 */
#define TIMER_NUM		0
#define TIMER_RELOAD_VAL	0xFFFFFFFF

/* Timer clock = AHB clock = CONFIG_SYS_CLK_FREQ / 4 */
#define TIMER_CLK_HZ		(CONFIG_SYS_CLK_FREQ / 4)

static unsigned long long timestamp;
static unsigned long lastdec;

int timer_init(void)
{
	void __iomem *base = (void __iomem *)NS9360_SYS_BASE;

	/* Set reload value to max (free-running 32-bit countdown) */
	writel(TIMER_RELOAD_VAL, base + SYS_TRC(TIMER_NUM));

	/* Enable timer: 32-bit, AHB clock, countdown, auto-reload */
	writel(TC_ENABLE | TC_RELOAD_EN | TC_CLK_AHB | TC_DOWN,
	       base + SYS_TC(TIMER_NUM));

	/* Reset our software timestamp */
	lastdec = TIMER_RELOAD_VAL;
	timestamp = 0;

	return 0;
}

/*
 * Read the countdown timer and convert to an incrementing counter.
 * The hardware counts down from TIMER_RELOAD_VAL to 0, so we
 * invert it.
 */
unsigned long timer_read_counter(void)
{
	void __iomem *base = (void __iomem *)NS9360_SYS_BASE;
	unsigned long now = readl(base + SYS_TR(TIMER_NUM));

	/* Convert countdown to count-up */
	return TIMER_RELOAD_VAL - now;
}

ulong get_tbclk(void)
{
	return TIMER_CLK_HZ;
}
