/*
 *  drivers/usb/notify/usb_notify_sysfs.c
 *
 * Copyright (C) 2015 Samsung, Inc.
 * Author: Dongrak Shin <dongrak.shin@samsung.com>
 *
*/

 /* usb notify layer v2.0 */

#define pr_fmt(fmt) "usb_notify: " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/usb.h>
#include <linux/usb_notify.h>
#include <linux/string.h>
#include "usb_notify_sysfs.h"

const char USB_HW_Param_Print[USB_CCIC_HW_PARAM_MAX][MAX_HWPARAM_STRING] = {
	{"CC_WATER"},
	{"CC_DRY"},
	{"CC_I2C"},
	{"CC_OVC"},
	{"CC_OTG"},
	{"CC_DP"},
	{"CC_VR"},
	{"H_SUPER"},
	{"H_HIGH"},
	{"H_FULL"},
	{"H_LOW"},
	{"C_SUPER"},
	{"C_HIGH"},
	{"H_AUDIO"},
	{"H_COMM"},
	{"H_HID"},
	{"H_PHYSIC"},
	{"H_IMAGE"},
	{"H_PRINTER"},
	{"H_STORAGE"},
	{"H_HUB"},
	{"H_CDC"},
	{"H_CSCID"},
	{"H_CONTENT"},
	{"H_VIDEO"},
	{"H_WIRE"},
	{"H_MISC"},
	{"H_APP"},
	{"H_VENDOR"},
	{"CC_WTIME"},
	{"CC_WVBUS"},
};
struct notify_data {
	struct class *usb_notify_class;
	atomic_t device_count;
};

static struct notify_data usb_notify_data;

#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
int get_ccic_water_count(void);
int get_ccic_dry_count(void);
int get_usb310_count(void);
int get_usb210_count(void);
unsigned long get_waterDet_duration(void);
int get_waterChg_count(void);
#endif

static int is_valid_cmd(char *cur_cmd, char *prev_cmd)
{
	pr_info("%s : current state=%s, previous state=%s\n",
		__func__, cur_cmd, prev_cmd);

	if (!strcmp(cur_cmd, "ON") ||
			!strncmp(cur_cmd, "ON_ALL_", 7)) {
		if (!strcmp(prev_cmd, "ON") ||
				!strncmp(prev_cmd, "ON_ALL_", 7)) {
			goto ignore;
		} else if (!strncmp(prev_cmd, "ON_HOST_", 8)) {
			goto all;
		} else if (!strncmp(prev_cmd, "ON_CLIENT_", 10)) {
			goto all;
		} else if (!strcmp(prev_cmd, "OFF")) {
			goto all;
		} else {
			goto invalid;
		}
	} else if (!strcmp(cur_cmd, "OFF")) {
		if (!strcmp(prev_cmd, "ON") ||
				!strncmp(prev_cmd, "ON_ALL_", 7)) {
			goto off;
		} else if (!strncmp(prev_cmd, "ON_HOST_", 8)) {
			goto off;
		} else if (!strncmp(prev_cmd, "ON_CLIENT_", 10)) {
			goto off;
		} else if (!strcmp(prev_cmd, "OFF")) {
			goto ignore;
		} else {
			goto invalid;
		}
	} else if (!strncmp(cur_cmd, "ON_HOST_", 8)) {
		if (!strcmp(prev_cmd, "ON") ||
				!strncmp(prev_cmd, "ON_ALL_", 7)) {
			goto host;
		} else if (!strncmp(prev_cmd, "ON_HOST_", 8)) {
			goto ignore;
		} else if (!strncmp(prev_cmd, "ON_CLIENT_", 10)) {
			goto host;
		} else if (!strcmp(prev_cmd, "OFF")) {
			goto host;
		} else {
			goto invalid;
		}
	} else if (!strncmp(cur_cmd, "ON_CLIENT_", 10)) {
		if (!strcmp(prev_cmd, "ON") ||
				!strncmp(prev_cmd, "ON_ALL_", 7)) {
			goto client;
		} else if (!strncmp(prev_cmd, "ON_HOST_", 8)) {
			goto client;
		} else if (!strncmp(prev_cmd, "ON_CLIENT_", 10)) {
			goto ignore;
		} else if (!strcmp(prev_cmd, "OFF")) {
			goto client;
		} else {
			goto invalid;
		}
	} else {
		goto invalid;
	}
host:
	pr_info("%s cmd=%s is accepted.\n", __func__, cur_cmd);
	return NOTIFY_BLOCK_TYPE_HOST;
client:
	pr_info("%s cmd=%s is accepted.\n", __func__, cur_cmd);
	return NOTIFY_BLOCK_TYPE_CLIENT;
all:
	pr_info("%s cmd=%s is accepted.\n", __func__, cur_cmd);
	return NOTIFY_BLOCK_TYPE_ALL;
off:
	pr_info("%s cmd=%s is accepted.\n", __func__, cur_cmd);
	return NOTIFY_BLOCK_TYPE_NONE;
ignore:
	pr_err("%s cmd=%s is ignored but saved.\n", __func__, cur_cmd);
	return -EEXIST;
invalid:
	pr_err("%s cmd=%s is invalid.\n", __func__, cur_cmd);
	return -EINVAL;
}

static unsigned long long int strtoull(char *ptr, char **end, int base)
{
	unsigned long long ret = 0;

	if (base > 36)
		goto out;

	while (*ptr) {
		int digit;

		if (*ptr >= '0' && *ptr <= '9' && *ptr < '0' + base)
			digit = *ptr - '0';
		else if (*ptr >= 'A' && *ptr < 'A' + base - 10)
			digit = *ptr - 'A' + 10;
		else if (*ptr >= 'a' && *ptr < 'a' + base - 10)
			digit = *ptr - 'a' + 10;
		else
			break;

		ret *= base;
		ret += digit;
		ptr++;
	}

out:
	if (end)
		*end = (char *)ptr;

	return ret;
}

static ssize_t disable_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);

	pr_info("read disable_state %s\n", udev->disable_state_cmd);
	return sprintf(buf, "%s\n", udev->disable_state_cmd);
}

static ssize_t disable_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);

	char *disable;
	int sret, param = -EINVAL;
	size_t ret = -ENOMEM;

	if (size > MAX_DISABLE_STR_LEN) {
		pr_err("%s size(%zu) is too long.\n", __func__, size);
		goto error;
	}

	disable = kzalloc(size+1, GFP_KERNEL);
	if (!disable)
		goto error;

	sret = sscanf(buf, "%s", disable);
	if (sret != 1)
		goto error1;

	if (udev->set_disable) {
		param = is_valid_cmd(disable, udev->disable_state_cmd);
		if (param == -EINVAL) {
			ret = param;
		} else {
			if (param != -EEXIST)
				udev->set_disable(udev, param);
			strncpy(udev->disable_state_cmd,
				disable, sizeof(udev->disable_state_cmd)-1);
			ret = size;
		}
	} else
		pr_err("set_disable func is NULL\n");
error1:
	kfree(disable);
error:
	return ret;
}

static ssize_t support_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;
	char *support;

	if (n->unsupport_host || !IS_ENABLED(CONFIG_USB_HOST_NOTIFY))
		support = "CLIENT";
	else
		support = "ALL";

	pr_info("read support %s\n", support);
	return snprintf(buf,  sizeof(support)+1, "%s\n", support);
}

static ssize_t otg_speed_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;
	char *speed;

	switch(n->speed)
	{
		case USB_SPEED_SUPER:
			speed = "SUPER";
		break;
		case USB_SPEED_HIGH:
			speed = "HIGH";
		break;
		case USB_SPEED_FULL:
			speed = "FULL";
		break;
		case USB_SPEED_LOW:
			speed = "LOW";
		break;
		default:
			speed = "UNKNOWN";
		break;
	}
	pr_info("%s : read otg speed %s\n", __func__, speed);
	return snprintf(buf,  sizeof(speed)+1, "%s\n", speed);
}

static ssize_t usb_hw_param_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;
	int index, ret = 0;

#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	n->hw_param[USB_CCIC_WATER_INT_COUNT] += get_ccic_water_count();
	n->hw_param[USB_CCIC_DRY_INT_COUNT] += get_ccic_dry_count();
	n->hw_param[USB_CLIENT_SUPER_SPEED_COUNT] += get_usb310_count();
	n->hw_param[USB_CLIENT_HIGH_SPEED_COUNT] += get_usb210_count();
	n->hw_param[USB_CCIC_WATER_TIME_DURATION] += get_waterDet_duration();
	n->hw_param[USB_CCIC_WATER_VBUS_COUNT] += get_waterChg_count();
#endif
	for (index = 0; index < USB_CCIC_HW_PARAM_MAX-1; index++) {
		ret += sprintf(buf + ret, "%llu ", n->hw_param[index]);
	}
	ret += sprintf(buf + ret, "%llu\n", n->hw_param[index]);
	pr_info("%s - ret : %d\n", __func__, ret);

	return ret;
}

static ssize_t usb_hw_param_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;

	int index = 0;
	size_t ret = -ENOMEM;
	char *token, *str = (char *)buf;

	if (size > MAX_HWPARAM_STR_LEN) {
		pr_err("%s size(%zu) is too long.\n", __func__, size);
		goto error;
	}
	ret = size;

	if (size < USB_CCIC_HW_PARAM_MAX) {
		pr_err("%s efs file is not created correctly.\n", __func__);
		goto error;
	}

	for (index = 0; index < USB_CCIC_HW_PARAM_MAX; index++) {
		token = strsep(&str, " ");
		if(token)
			n->hw_param[index] += strtoull(token, NULL, 10);
		pr_info("%s - hw_param[%d] : %llu\n",
			__func__, index, n->hw_param[index]);
	}
error:
	return ret;
}

static ssize_t hw_param_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;
	int index, ret = 0;

	for (index = 0; index < USB_CCIC_HW_PARAM_MAX-1; index++) {
		ret += sprintf(buf + ret, "\"%s\":\"%llu\",",
			USB_HW_Param_Print[index], n->hw_param[index]);
	}
	ret += sprintf(buf + ret, "\"%s\":\"%llu\"\n",
			USB_HW_Param_Print[index], n->hw_param[index]);
	pr_info("%s - ret : %d\n", __func__, ret);

	return ret;
}

static ssize_t hw_param_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct usb_notify_dev *udev = (struct usb_notify_dev *)
		dev_get_drvdata(dev);
	struct otg_notify *n = udev->o_notify;

	int index = 0;
	size_t ret = -ENOMEM;
	char *str = (char *)buf;

	if (size > 2) {
		pr_err("%s size(%zu) is too long.\n", __func__, size);
		goto error;
	}
	ret = size;
	pr_info("%s : %s\n", __func__, str);
	if(!strncmp(str, "c", 1))
		for (index = 0; index < USB_CCIC_HW_PARAM_MAX; index++) {
			n->hw_param[index] = 0;
		}
error:
	return ret;
}

static DEVICE_ATTR(disable, 0664, disable_show, disable_store);
static DEVICE_ATTR(support, 0444, support_show, NULL);
static DEVICE_ATTR(otg_speed, 0444, otg_speed_show, NULL);
static DEVICE_ATTR(usb_hw_param, 0664, usb_hw_param_show, usb_hw_param_store);
static DEVICE_ATTR(hw_param, 0664, hw_param_show, hw_param_store);

static struct attribute *usb_notify_attrs[] = {
	&dev_attr_disable.attr,
	&dev_attr_support.attr,
	&dev_attr_otg_speed.attr,
	&dev_attr_usb_hw_param.attr,
	&dev_attr_hw_param.attr,
	NULL,
};

static struct attribute_group usb_notify_attr_grp = {
	.attrs = usb_notify_attrs,
};

static int create_usb_notify_class(void)
{
	if (!usb_notify_data.usb_notify_class) {
		usb_notify_data.usb_notify_class
			= class_create(THIS_MODULE, "usb_notify");
		if (IS_ERR(usb_notify_data.usb_notify_class))
			return PTR_ERR(usb_notify_data.usb_notify_class);
		atomic_set(&usb_notify_data.device_count, 0);
	}

	return 0;
}

int usb_notify_dev_register(struct usb_notify_dev *udev)
{
	int ret;

	if (!usb_notify_data.usb_notify_class) {
		ret = create_usb_notify_class();
		if (ret < 0)
			return ret;
	}

	udev->index = atomic_inc_return(&usb_notify_data.device_count);
	udev->dev = device_create(usb_notify_data.usb_notify_class, NULL,
		MKDEV(0, udev->index), NULL, udev->name);
	if (IS_ERR(udev->dev))
		return PTR_ERR(udev->dev);

	udev->disable_state = 0;
	strncpy(udev->disable_state_cmd, "OFF",
			sizeof(udev->disable_state_cmd)-1);
	ret = sysfs_create_group(&udev->dev->kobj, &usb_notify_attr_grp);
	if (ret < 0) {
		device_destroy(usb_notify_data.usb_notify_class,
				MKDEV(0, udev->index));
		return ret;
	}

	dev_set_drvdata(udev->dev, udev);
	return 0;
}
EXPORT_SYMBOL_GPL(usb_notify_dev_register);

void usb_notify_dev_unregister(struct usb_notify_dev *udev)
{
	sysfs_remove_group(&udev->dev->kobj, &usb_notify_attr_grp);
	device_destroy(usb_notify_data.usb_notify_class, MKDEV(0, udev->index));
	dev_set_drvdata(udev->dev, NULL);
}
EXPORT_SYMBOL_GPL(usb_notify_dev_unregister);

int usb_notify_class_init(void)
{
	return create_usb_notify_class();
}
EXPORT_SYMBOL_GPL(usb_notify_class_init);

void usb_notify_class_exit(void)
{
	class_destroy(usb_notify_data.usb_notify_class);
}
EXPORT_SYMBOL_GPL(usb_notify_class_exit);

