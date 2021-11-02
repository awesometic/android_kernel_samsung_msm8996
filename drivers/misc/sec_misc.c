/*
 * driver/misc/sec_misc.c
 *
 * driver supporting miscellaneous functions for Samsung P-series device
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2011 All Right Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/blkdev.h>
#include <linux/sec_class.h>

struct device *sec_misc_dev;

#define QFPROM_CORR_PTE_ROW0_LSB_ID				0x00074130

#if defined(CONFIG_SEC_FACTORY)
static char *cpr_iddq_info;

static ssize_t msm_apc_avs_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
		pr_debug("msm cpr_iddq_info : %s\n", cpr_iddq_info);

		return scnprintf(buf, PAGE_SIZE, "%s\n", cpr_iddq_info);
}

static DEVICE_ATTR(msm_apc_avs_info, S_IRUGO | S_IWUSR ,
		msm_apc_avs_info_show, NULL);
#endif

static ssize_t msm_feature_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
		uint32_t feature_id = 0;
		void *feature_id_addr;

		feature_id_addr = ioremap_nocache(QFPROM_CORR_PTE_ROW0_LSB_ID,SZ_4K);
		if (!feature_id_addr) {
			pr_err("could not map FEATURE_ID address\n");
			return scnprintf(buf, PAGE_SIZE, "could not map FEATURE_ID address\n");
		}

		feature_id = readl_relaxed(feature_id_addr);
		iounmap(feature_id_addr);

		if (!feature_id) {
			return scnprintf(buf, PAGE_SIZE, "Feature ID is not supported!!\n");
		}

		feature_id = (feature_id >> 20) & 0xff;
		pr_debug("FEATURE_ID : 0x%08x\n",feature_id);

		return scnprintf(buf, PAGE_SIZE, "%02d\n",feature_id);
}

static DEVICE_ATTR(msm_feature_id, S_IRUGO, msm_feature_id_show, NULL);
/*  End of Feature ID */

static struct device_attribute *sec_misc_attrs[] = {
	&dev_attr_msm_feature_id,
#if defined(CONFIG_SEC_FACTORY)
	&dev_attr_msm_apc_avs_info,
#endif
};

static const struct file_operations sec_misc_fops = {
	.owner = THIS_MODULE,
};

static struct miscdevice sec_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sec_misc",
	.fops = &sec_misc_fops,
};

#if defined(CONFIG_SEC_FACTORY)
static int __init cpr_iddq_info_setup(char *str)
{
	cpr_iddq_info = str;
	return 1;
}
__setup("QCOM.CPR_IDDQ_INFO=", cpr_iddq_info_setup);
#endif

static int __init sec_misc_init(void)
{
	int ret = 0;
	int i;

	ret = misc_register(&sec_misc_device);
	if (ret < 0) {
		pr_err("misc_register failed!\n");
		goto failed_register_misc;
	}

	sec_misc_dev = device_create(sec_class, NULL, 0, NULL, "sec_misc");
	if (IS_ERR(sec_misc_dev)) {
		pr_err("failed to create device!\n");
		ret = -ENODEV;
		goto failed_create_device;
	}

	for (i = 0; i < ARRAY_SIZE(sec_misc_attrs) ; i++) {
		ret = device_create_file(sec_misc_dev, sec_misc_attrs[i]);
		if (ret < 0) {
			pr_err("failed to create device file - %d\n",i);
			goto failed_create_device_file;
		}
	}

	return 0;

failed_create_device_file:
	if (i) {
		for (--i; i >= 0; i--)
			device_remove_file(sec_misc_dev, sec_misc_attrs[i]);
	}
failed_create_device:
	misc_deregister(&sec_misc_device);
failed_register_misc:
	return ret;
}

module_init(sec_misc_init);

/* Module information */
MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Samsung misc. driver");
MODULE_LICENSE("GPL");
