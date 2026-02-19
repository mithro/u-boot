// SPDX-License-Identifier: GPL-2.0+
/*
 * Digi NS9360 GPIO driver (driver model)
 *
 * Copyright (C) 2025
 *
 * The NS9360 has 73 GPIOs (0-72) in two blocks:
 *   Block 1: GPIO 0-55,  config regs at BBUS + 0x10 + (gpio/8)*4
 *   Block 2: GPIO 56-72, config regs at BBUS + 0x100 + ((gpio-56)/8)*4
 *
 * Each GPIO uses 4 bits in a configuration register:
 *   [1:0] = function select (0-2: peripheral, 3: GPIO mode)
 *   [2]   = invert
 *   [3]   = direction (0: input, 1: output) - only when func=3
 *
 * Output control registers:
 *   GCTRL1 (0x30): GPIO 0-31
 *   GCTRL2 (0x34): GPIO 32-63
 *   GCTRL3 (0x120): GPIO 64-72
 *
 * Input status registers:
 *   GSTAT1 (0x40): GPIO 0-31
 *   GSTAT2 (0x44): GPIO 32-63
 *   GSTAT3 (0x130): GPIO 64-72
 */

#include <dm.h>
#include <asm/gpio.h>
#include <asm/io.h>

/* Register offsets from BBus base (mapped via DT reg property) */
#define GPIO_CFG_BASE		0x00	/* GPIO 0-55 config (relative to reg base 0x10) */
#define GPIO_CTRL1		0x20	/* Output control GPIO 0-31 (relative to 0x10) */
#define GPIO_CTRL2		0x24	/* Output control GPIO 32-63 */
#define GPIO_STAT1		0x30	/* Input status GPIO 0-31 */
#define GPIO_STAT2		0x34	/* Input status GPIO 32-63 */

/* Block 2 offsets (relative to DT reg base for GPIO 56-72) */
#define GPIO_CFG_B2_BASE	0xF0	/* 0x100 - 0x10 = 0xF0 relative */
#define GPIO_CTRL3		0x110	/* 0x120 - 0x10 = 0x110 relative */
#define GPIO_STAT3		0x120	/* 0x130 - 0x10 = 0x120 relative */

#define NS9360_GPIO_COUNT	73
#define GPIO_FUNC_GPIO		3	/* Function select value for GPIO mode */

struct ns9360_gpio_priv {
	void __iomem *base;
};

static void ns9360_gpio_get_cfg_reg(unsigned int gpio,
				     unsigned int *offset, unsigned int *shift)
{
	if (gpio <= 55) {
		*offset = GPIO_CFG_BASE + (gpio / 8) * 4;
		*shift = (gpio % 8) * 4;
	} else {
		*offset = GPIO_CFG_B2_BASE + ((gpio - 56) / 8) * 4;
		*shift = ((gpio - 56) % 8) * 4;
	}
}

static void ns9360_gpio_get_ctrl_stat(unsigned int gpio,
				       unsigned int *ctrl_off,
				       unsigned int *stat_off,
				       unsigned int *bit)
{
	if (gpio < 32) {
		*ctrl_off = GPIO_CTRL1;
		*stat_off = GPIO_STAT1;
		*bit = gpio;
	} else if (gpio < 64) {
		*ctrl_off = GPIO_CTRL2;
		*stat_off = GPIO_STAT2;
		*bit = gpio - 32;
	} else {
		*ctrl_off = GPIO_CTRL3;
		*stat_off = GPIO_STAT3;
		*bit = gpio - 64;
	}
}

static int ns9360_gpio_direction_input(struct udevice *dev, unsigned int gpio)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	unsigned int cfg_off, shift;
	u32 val;

	ns9360_gpio_get_cfg_reg(gpio, &cfg_off, &shift);

	val = readl(priv->base + cfg_off);
	val &= ~(0xf << shift);
	/* func=3 (GPIO mode), direction=0 (input) */
	val |= (GPIO_FUNC_GPIO << shift);
	writel(val, priv->base + cfg_off);

	return 0;
}

static int ns9360_gpio_direction_output(struct udevice *dev, unsigned int gpio,
					int value)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	unsigned int cfg_off, shift;
	unsigned int ctrl_off, stat_off, bit;
	u32 val;

	/* Set output value first */
	ns9360_gpio_get_ctrl_stat(gpio, &ctrl_off, &stat_off, &bit);
	val = readl(priv->base + ctrl_off);
	if (value)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);
	writel(val, priv->base + ctrl_off);

	/* Then set direction to output */
	ns9360_gpio_get_cfg_reg(gpio, &cfg_off, &shift);
	val = readl(priv->base + cfg_off);
	val &= ~(0xf << shift);
	/* func=3 (GPIO mode), direction=1 (output) */
	val |= ((GPIO_FUNC_GPIO | (1 << 3)) << shift);
	writel(val, priv->base + cfg_off);

	return 0;
}

static int ns9360_gpio_get_value(struct udevice *dev, unsigned int gpio)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	unsigned int ctrl_off, stat_off, bit;

	ns9360_gpio_get_ctrl_stat(gpio, &ctrl_off, &stat_off, &bit);
	return (readl(priv->base + stat_off) >> bit) & 1;
}

static int ns9360_gpio_set_value(struct udevice *dev, unsigned int gpio,
				 int value)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	unsigned int ctrl_off, stat_off, bit;
	u32 val;

	ns9360_gpio_get_ctrl_stat(gpio, &ctrl_off, &stat_off, &bit);
	val = readl(priv->base + ctrl_off);
	if (value)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);
	writel(val, priv->base + ctrl_off);

	return 0;
}

static int ns9360_gpio_get_function(struct udevice *dev, unsigned int gpio)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	unsigned int cfg_off, shift;
	u32 val, func, dir;

	ns9360_gpio_get_cfg_reg(gpio, &cfg_off, &shift);
	val = (readl(priv->base + cfg_off) >> shift) & 0xf;

	func = val & 0x3;
	dir = (val >> 3) & 1;

	if (func != GPIO_FUNC_GPIO)
		return GPIOF_FUNC;	/* Assigned to peripheral */

	return dir ? GPIOF_OUTPUT : GPIOF_INPUT;
}

static int ns9360_gpio_probe(struct udevice *dev)
{
	struct ns9360_gpio_priv *priv = dev_get_priv(dev);
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	fdt_addr_t addr;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base = (void __iomem *)addr;

	uc_priv->bank_name = "ns9360";
	uc_priv->gpio_count = NS9360_GPIO_COUNT;

	return 0;
}

static const struct dm_gpio_ops ns9360_gpio_ops = {
	.direction_input	= ns9360_gpio_direction_input,
	.direction_output	= ns9360_gpio_direction_output,
	.get_value		= ns9360_gpio_get_value,
	.set_value		= ns9360_gpio_set_value,
	.get_function		= ns9360_gpio_get_function,
};

static const struct udevice_id ns9360_gpio_ids[] = {
	{ .compatible = "digi,ns9360-gpio" },
	{ }
};

U_BOOT_DRIVER(ns9360_gpio) = {
	.name		= "ns9360_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= ns9360_gpio_ids,
	.probe		= ns9360_gpio_probe,
	.ops		= &ns9360_gpio_ops,
	.priv_auto	= sizeof(struct ns9360_gpio_priv),
};
