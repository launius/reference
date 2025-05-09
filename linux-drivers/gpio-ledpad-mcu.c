/*
 * Copyright (c) 2024 Yunjae Lim <launius@gmail.com>
 *
 * LED-Pad MCU GPIO Driver.
 *
 * This driver maps MCU registers to the Linux sysfs interface
 * using regmap to control LEDs and receive keypad inputs.
 *
 * This driver has been updated to include:
 *   - using kernel interrupts to notify sysfs for reading keypad inputs.
 *   - setting RW group permissions on sysfs attributes, allowing applications to control LEDs.
 */

/*
 * 1. Device tree configurations
 *

&i2c1 {
	status = "okay";
	pinctrl-0 = <&i2c1_z_pins>;
	pinctrl-names = "default";

	ledpad_mcu: ledpad_mcu@12 {
		compatible = "manufacturer,model-ledpad-mcu";
		reg = <0x12>;
		gpio-controller;
		#gpio-cells = <2>;
		interrupt-controller;
		#interrupt-cells = <2>;
		irq-gpios = <&gpio1 RK_PB3 GPIO_ACTIVE_HIGH>;
		interrupt-parent = <&gpio1>;
		interrupts = <11 IRQ_TYPE_EDGE_RISING>;
	};
};

 *
 * 2. Build configurations
 *

+++ b/drivers/gpio/Kconfig
+config GPIO_LEDPAD_MCU
+       tristate "LED-Pad MCU GPIO support"
+       depends on MFD_DEVICE_MODEL_MCU
+       help
+          Say Y here to support LED-Pad MCU GPIOs
+

+++ b/drivers/gpio/Makefile
+obj-$(CONFIG_GPIO_LEDPAD_MCU)     += gpio-ledpad-mcu.o

 *
 * 3. Changes in parent driver
 *

+++ b/drivers/mfd/device-model-mcu.c
#define MCU_DEVICE_MODEL_MAX_REG					0x50

static const struct regmap_config device_model_mcu_regmap_config = {
	.reg_bits	= 8,
	.reg_stride	= 1,
	.val_bits	= 8,
	.max_register	= MCU_DEVICE_MODEL_MAX_REG,
	.volatile_reg	= device_model_mcu_reg_volatile,
	.writeable_reg	= device_model_mcu_reg_writeable,
	.cache_type	= REGCACHE_RBTREE,
};

static struct mfd_cell device_model_ledpad_mcu_cells[] = {
+       { .name = "model-mcu-gpio", },
};

static struct device_model_mcu_conf device_model_ledpad_mcu_conf_data = {
	.cells = device_model_ledpad_mcu_cells,
	.count = ARRAY_SIZE(device_model_ledpad_mcu_cells),
	.mcu_type = LEDPAD_MCU,
};

static const struct of_device_id device_model_mcu_of_match[] = {
	{ .compatible = "manufacturer,model-ledpad-mcu", .data = &device_model_ledpad_mcu_conf_data },
	{},
};

static int device_model_mcu_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	data->regmap = devm_regmap_init_i2c(client, &device_model_mcu_regmap_config);

 *
 * 4. Load driver module
 *

root@system:~# lsmod
Module                  Size  Used by
gpio_ledpad_mcu         16384  0

root@system:~# dmesg |grep -i device-model
[    1.934368] ledpad-mcu-gpio model-mcu-gpio: Registered LED-Pad MCU GPIO driver

 *
 * 5. Read/Write MCU registers via sysfs
 *

root@system:/sys/devices/platform/soc/ffd00000.bus/ffd1e000.i2c/i2c-2/2-0012/model-mcu-gpio# ls -l
total 0
lrwxrwxrwx 1 root root    0 Nov 21 01:09 driver -> ../../../../../../../../bus/platform/drivers/ledpad-mcu-gpio
-rw-r--r-- 1 root root 4096 Nov 21 01:09 driver_override
-r--r--r-- 1 root root 4096 Nov 21 01:09 modalias
drwxr-xr-x 2 root root    0 Nov 21 01:08 power
drwxr-xr-x 2 root root    0 Nov 21 01:08 gpio_ledpad
lrwxrwxrwx 1 root root    0 Nov 21 01:09 subsystem -> ../../../../../../../../bus/platform
-rw-r--r-- 1 root root 4096 Jan  1  1970 uevent

root@system:/sys/devices/platform/soc/ffd00000.bus/ffd1e000.i2c/i2c-2/2-0012/model-mcu-gpio# ls -l gpio_ledpad/
total 0
-rw-r--r-- 1 root root 4096 Nov 21 01:11 keypad_reg
-r--r--r-- 1 root root 4096 Nov 21 01:11 enc_in0
-r--r--r-- 1 root root 4096 Nov 21 01:11 enc_in1
-r--r--r-- 1 root root 4096 Nov 21 01:11 enc_in2
-rw-r--r-- 1 root root 4096 Nov 21 01:11 enc_sw_reg
-rw-r--r-- 1 root root 4096 Nov 21 01:11 input_irq
-rw-r--r-- 1 root root 4096 Nov 21 01:11 led_brightness
-rw-r--r-- 1 root root 4096 Nov 21 01:11 led_color
-rw-r--r-- 1 root root 4096 Nov 21 01:11 led_enc_pos0
-rw-r--r-- 1 root root 4096 Nov 21 01:11 led_enc_pos1
-rw-r--r-- 1 root root 4096 Nov 21 01:11 led_enc_pos2

root@system:/sys/devices/platform/soc/ffd00000.bus/ffd1e000.i2c/i2c-2/2-0012/model-mcu-gpio# cat gpio_ledpad/keypad_reg
64 (8 keypads' ON/OFF status with 8bits, e.g. 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80)
root@system:/sys/devices/platform/soc/ffd00000.bus/ffd1e000.i2c/i2c-2/2-0012/model-mcu-gpio# echo 257 > gpio_ledpad/led_color (divided by total number of colors)

 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/device-model-mcu.h>

#define DEV_ATTR_RW_PERM (S_IRUGO | S_IWUSR | S_IWGRP)
#define DEV_ATTR_WO_PERM (S_IWUSR | S_IWGRP)
#define DEV_ATTR_RO_PERM (S_IRUSR | S_IRGRP)

#define MCU_LEDPAD_KEYPAD_REG			0x07
#define MCU_LEDPAD_ENC_SW_REG			0x08
#define MCU_LEDPAD_ENC_IN				0x09
#define MCU_LEDPAD_LED_COLOR			0x0C
#define MCU_LEDPAD_LED_BRIGHTNESS		0x16
#define MCU_LEDPAD_LED_ENC_POS			0x17

struct ledpad_mcu_gpio_priv {
	struct gpio_chip chip;
	int irq;
	struct device_model_mcu *mcu;
};

struct ledpad_mcu_gpio_map {
	unsigned int reg;
	unsigned int bit;
};

const struct ledpad_mcu_gpio_map gpios_map[] = {
	{MCU_LEDPAD_KEYPAD_REG, 0},
	{MCU_LEDPAD_KEYPAD_REG, 1},
	{MCU_LEDPAD_KEYPAD_REG, 2},
	{MCU_LEDPAD_KEYPAD_REG, 3},
	{MCU_LEDPAD_KEYPAD_REG, 4},
	{MCU_LEDPAD_KEYPAD_REG, 5},
	{MCU_LEDPAD_KEYPAD_REG, 6},
	{MCU_LEDPAD_KEYPAD_REG, 7},
	{MCU_LEDPAD_ENC_SW_REG, 0},
	{MCU_LEDPAD_ENC_SW_REG, 1},
	{MCU_LEDPAD_ENC_SW_REG, 2},
	{MCU_LEDPAD_ENC_SW_REG, 3},
	{MCU_LEDPAD_ENC_SW_REG, 4},
	{MCU_LEDPAD_ENC_SW_REG, 5},
	{MCU_LEDPAD_ENC_SW_REG, 6},
	{MCU_LEDPAD_ENC_SW_REG, 7},
};

#define STORE_FUNC(_name, _reg) \
static ssize_t \
_name##_store(struct device *dev, struct device_attribute *attr, \
		 const char *buf, size_t count) \
{ \
	int ret; \
	ret = ledpad_mcu_reg_set(dev, buf, _reg); \
	if (ret != 0) \
		return ret; \
	return count; \
}

#define SHOW_FUNC(_name, _reg) \
static ssize_t \
_name##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return ledpad_mcu_reg_get(dev, buf, _reg); \
}

#define ENC_IN_SHOW(_index) \
static ssize_t \
enc_in##_index##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return ledpad_mcu_reg_get(dev, buf, MCU_LEDPAD_ENC_IN+_index); \
}

#define LED_ENC_POS_STORE(_index) \
static ssize_t \
led_enc_pos##_index##_store(struct device *dev, struct device_attribute *attr, \
		 const char *buf, size_t count) { \
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev); \
	int ret; \
	long data; \
	\
	ret = kstrtol(buf, 10, &data); \
	if (ret != 0) \
		return ret; \
	\
	ret = regmap_write(priv->mcu->regmap, MCU_LEDPAD_LED_ENC_POS+_index, (unsigned int)data); \
	if (ret != 0) \
		return ret; \
	\
	return count; \
}

#define LED_ENC_POS_SHOW(_index) \
static ssize_t \
led_enc_pos##_index##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return ledpad_mcu_reg_get(dev, buf, MCU_LEDPAD_LED_ENC_POS+_index); \
}

static int ledpad_mcu_reg_get(struct device *dev, char *buf, unsigned int reg)
{
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->mcu->regmap, reg, &val);
	if (ret)
		return ret;

//	printk(KERN_INFO "%s: reg %d val %#x\n", __func__, reg, val);

	return snprintf(buf, 4, "%u\n", val);
}

static int ledpad_mcu_reg_set(struct device *dev, const char *buf, unsigned int reg)
{
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev);
	int ret;
	long data;

	ret = kstrtol(buf, 10, &data);
	if (ret != 0)
		return ret;

	return regmap_write(priv->mcu->regmap, reg, data);
}

static ssize_t
input_irq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev);

	return snprintf(buf, 4, "%d\n", priv->irq);
}

static ssize_t
led_color_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev);
	int ret;
	long data;
	const int num_of_clr = 256;

	ret = kstrtol(buf, 10, &data);
	if (ret != 0)
		return ret;

	ret = regmap_write(priv->mcu->regmap, MCU_LEDPAD_LED_COLOR + data/num_of_clr, data%num_of_clr);
	if (ret != 0)
		return ret;

	return count;
}
static ssize_t
led_color_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return ledpad_mcu_reg_get(dev, buf, MCU_LEDPAD_LED_COLOR);
}

static ssize_t
led_brightness_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct ledpad_mcu_gpio_priv *priv = dev_get_drvdata(dev);
	int ret;
	long data;

	ret = kstrtol(buf, 10, &data);
	if (ret != 0)
		return ret;

	ret = regmap_write(priv->mcu->regmap, MCU_LEDPAD_LED_BRIGHTNESS, (unsigned int)data);
	if (ret != 0)
		return ret;

	return count;
}
static ssize_t
led_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return ledpad_mcu_reg_get(dev, buf, MCU_LEDPAD_LED_BRIGHTNESS);
}

STORE_FUNC(keypad_reg, MCU_LEDPAD_KEYPAD_REG)
SHOW_FUNC(keypad_reg, MCU_LEDPAD_KEYPAD_REG)

STORE_FUNC(enc_sw_reg, MCU_LEDPAD_ENC_SW_REG)
SHOW_FUNC(enc_sw_reg, MCU_LEDPAD_ENC_SW_REG)

ENC_IN_SHOW(0)
ENC_IN_SHOW(1)
ENC_IN_SHOW(2)

LED_ENC_POS_STORE(0)
LED_ENC_POS_SHOW(0)
LED_ENC_POS_STORE(1)
LED_ENC_POS_SHOW(1)
LED_ENC_POS_STORE(2)
LED_ENC_POS_SHOW(2)

static DEVICE_ATTR_RW(keypad_reg);
static DEVICE_ATTR_RW(enc_sw_reg);
static DEVICE_ATTR_RO(enc_in0);
static DEVICE_ATTR_RO(enc_in1);
static DEVICE_ATTR_RO(enc_in2);
static DEVICE_ATTR_RO(input_irq);
static DEVICE_ATTR(led_color, DEV_ATTR_RW_PERM, led_color_show, led_color_store);
static DEVICE_ATTR(led_brightness, DEV_ATTR_RW_PERM, led_brightness_show, led_brightness_store);
static DEVICE_ATTR(led_enc_pos0, DEV_ATTR_RW_PERM, led_enc_pos0_show, led_enc_pos0_store);
static DEVICE_ATTR(led_enc_pos1, DEV_ATTR_RW_PERM, led_enc_pos1_show, led_enc_pos1_store);
static DEVICE_ATTR(led_enc_pos2, DEV_ATTR_RW_PERM, led_enc_pos2_show, led_enc_pos2_store);

static struct attribute *gpio_ledpad_attrs[] = {
	&dev_attr_keypad_reg.attr,
	&dev_attr_enc_sw_reg.attr,
	&dev_attr_enc_in0.attr,
	&dev_attr_enc_in1.attr,
	&dev_attr_enc_in2.attr,
	&dev_attr_input_irq.attr,
	&dev_attr_led_color.attr,
	&dev_attr_led_brightness.attr,
	&dev_attr_led_enc_pos0.attr,
	&dev_attr_led_enc_pos1.attr,
	&dev_attr_led_enc_pos2.attr,
	NULL,
};

static struct attribute_group gpio_ledpad_attr_group = {
	.attrs = gpio_ledpad_attrs,
	.name = "gpio_ledpad"
};

static int ledpad_mcu_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ledpad_mcu_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int reg, bit = 0;
	unsigned int val;
	int ret;

	reg = gpios_map[offset].reg;
	bit = gpios_map[offset].bit;

	ret = regmap_read(priv->mcu->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & BIT(bit));
}

static void ledpad_mcu_gpio_set(struct gpio_chip *chip, unsigned int offset, int val)
{
	struct ledpad_mcu_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int reg, bit = 0;

	reg = gpios_map[offset].reg;
	bit = gpios_map[offset].bit;
	
	if (val > 0)
		regmap_update_bits(priv->mcu->regmap, reg, BIT(bit), BIT(bit));
	else
		regmap_update_bits(priv->mcu->regmap, reg, BIT(bit), 0);
}

static irqreturn_t ledpad_mcu_gpio_irq(int irq, void *data)
{
	struct device *dev = data;
	char *attr = "input_irq";

	sysfs_notify(&dev->kobj, "ledpad_mcu_gpio", attr);

	return IRQ_HANDLED;
}

static int ledpad_mcu_gpio_probe(struct platform_device *pdev)
{
	struct device_model_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct ledpad_mcu_gpio_priv *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mcu = mcu;
	priv->chip.parent = pdev->dev.parent;
	priv->chip.owner = THIS_MODULE;
	priv->chip.label = dev_name(pdev->dev.parent);
	priv->chip.base = -1;
	priv->chip.ngpio = ARRAY_SIZE(gpios_map);
	priv->chip.get = ledpad_mcu_gpio_get;
	priv->chip.direction_input = NULL;
 	priv->chip.set = ledpad_mcu_gpio_set;
	priv->chip.direction_output = NULL;

	priv->irq = irq_of_parse_and_map(dev->parent->of_node, 0);
	if (!priv->irq)
		dev_err(dev, "irqs are not available\n");

	if (priv->irq > 0) {
		err = devm_request_threaded_irq(&pdev->dev, priv->irq, NULL,
					ledpad_mcu_gpio_irq, IRQF_ONESHOT, "ledpad-mcu-gpio", &pdev->dev);
		if (err)
			dev_err(&pdev->dev, "unable to get irq: %d\n", err);
	}

	platform_set_drvdata(pdev, priv);

	err = sysfs_create_group(&dev->kobj, &gpio_ledpad_attr_group);
	if (err)
		return err;

	dev_info(&pdev->dev, "Registered LED-Pad MCU GPIO driver with IRQ %d\n", priv->irq);

	return devm_gpiochip_add_data(&pdev->dev, &priv->chip, priv);
}

static const struct platform_device_id ledpad_mcu_gpio_id_table[] = {
	{ .name = "model-mcu-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(platform, ledpad_mcu_gpio_id_table);

static struct platform_driver ledpad_mcu_gpio_driver = {
	.driver	= {
		.name = "ledpad-mcu-gpio",
		.owner = THIS_MODULE,
	},
	.id_table = ledpad_mcu_gpio_id_table,
	.probe	= ledpad_mcu_gpio_probe,
};
module_platform_driver(ledpad_mcu_gpio_driver);

MODULE_AUTHOR("Yunjae Lim <launius@gmail.com>");
MODULE_DESCRIPTION("LED-Pad MCU GPIO driver");
MODULE_LICENSE("GPL");
