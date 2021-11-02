/*
*  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*/
#include <linux/init.h>
#include <linux/module.h>
#include "adsp.h"
#define VENDOR "AMS"
#ifdef CONFIG_SUPPORT_TMD4904_FACTORY
#define CHIP_ID "TMD4904"
#else
#define CHIP_ID "TMD4903"
#endif

#define RAWDATA_TIMER_MS 200
#define RAWDATA_TIMER_MARGIN_MS	20
#define PROX_AVG_COUNT	40
#define PROX_ALERT_THRESHOLD 200

struct tmd4903_prox_data {
	struct hrtimer prox_timer;
	struct work_struct work_prox;
	struct workqueue_struct *prox_wq;
	int min;
	int max;
	int avg;
	int val;
	short avgwork_check;
	short avgtimer_enabled;
	short bBarcodeEnabled;
};
static struct tmd4903_prox_data *pdata;

static ssize_t prox_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VENDOR);
}

static ssize_t prox_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", CHIP_ID);
}

static ssize_t prox_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);

	if (pdata->avgwork_check == 1)
		return snprintf(buf, PAGE_SIZE, "%d\n", pdata->val);

	if (adsp_start_raw_data(ADSP_FACTORY_PROX) == false)
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data->sensor_data[ADSP_FACTORY_PROX].prox);
}

static ssize_t prox_raw_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);

	if (pdata->avgwork_check == 1)
		return snprintf(buf, PAGE_SIZE, "%d\n", pdata->val);

	if (adsp_start_raw_data(ADSP_FACTORY_PROX) == false)
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data->sensor_data[ADSP_FACTORY_PROX].prox);
}

static ssize_t prox_avg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n", pdata->min,
		pdata->avg, pdata->max);
}

static ssize_t prox_avg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int new_value;

	if (sysfs_streq(buf, "0"))
		new_value = 0;
	else
		new_value = 1;

	if (new_value == pdata->avgtimer_enabled)
		return size;

	if (new_value == 0) {
		pdata->avgtimer_enabled = 0;
		hrtimer_cancel(&pdata->prox_timer);
		cancel_work_sync(&pdata->work_prox);
	} else {
		pdata->avgtimer_enabled = 1;
		hrtimer_start(&pdata->prox_timer,
			ns_to_ktime(2000 * NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	}

	return size;
}

static void prox_work_func(struct work_struct *work)
{
	int min = 0, max = 0, avg = 0;
	int i;

	pdata->avgwork_check = 1;
	for (i = 0; i < PROX_AVG_COUNT; i++) {
		msleep(20);
		if (adsp_start_raw_data(ADSP_FACTORY_PROX) == false)
			continue;
		pdata->val = adsp_get_sensor_data(ADSP_FACTORY_PROX);

		avg += pdata->val;

		if (!i)
			min = pdata->val;
		else if (pdata->val < min)
			min = pdata->val;

		if (pdata->val > max)
			max = pdata->val;
	}
	avg /= PROX_AVG_COUNT;

	pdata->min = min;
	pdata->avg = avg;
	pdata->max = max;
	pdata->avgwork_check = 0;
}

static enum hrtimer_restart prox_timer_func(struct hrtimer *timer)
{
	queue_work(pdata->prox_wq, &pdata->work_prox);
	hrtimer_forward_now(&pdata->prox_timer,
		ns_to_ktime(2000 * NSEC_PER_MSEC));
	return HRTIMER_RESTART;
}


static ssize_t prox_cancel_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d,%d,%d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].offset,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].offset,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);
}

static ssize_t prox_cancel_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;
	int enable = 0;

	if (kstrtoint(buf, 10, &enable)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}
	if (enable > 0)
		enable = 1;

	message.sensor_type = ADSP_FACTORY_PROX;
	message.param1 = enable;
	msleep(RAWDATA_TIMER_MS + RAWDATA_TIMER_MARGIN_MS);
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_CALIB_STORE_DATA, 0, 0);

	while (!(data->calib_store_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	if (data->sensor_calib_result[ADSP_FACTORY_PROX].result < 0)
		pr_err("[FACTORY] %s: failed\n", __func__);

	data->calib_store_ready_flag |= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: result(%d)\n", __func__,
		data->sensor_calib_result[ADSP_FACTORY_PROX].result);

	return size;
}

static ssize_t prox_thresh_high_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d,%d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);

#ifdef CONFIG_SUPPORT_TMD4904_FACTORY
	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi);
#else
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);
#endif
}

static ssize_t prox_thresh_high_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct msg_big_data message;
	int thd = 0;

	if (kstrtoint(buf, 10, &thd)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	message.sensor_type = ADSP_FACTORY_PROX;
	message.msg_size = 4;
	message.int_msg[0] = thd;
	msleep(RAWDATA_TIMER_MS + RAWDATA_TIMER_MARGIN_MS);
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_THD_HI_STORE_DATA, 0, 0);

	return size;
}

static ssize_t prox_thresh_low_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d,%d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);

#ifdef CONFIG_SUPPORT_TMD4904_FACTORY
	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);
#else
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threLo);
#endif
}

static ssize_t prox_thresh_low_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct msg_big_data message;
	int thd = 0;

	if (kstrtoint(buf, 10, &thd)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	message.sensor_type = ADSP_FACTORY_PROX;
	message.msg_size = 4;
	message.int_msg[0] = thd;
	msleep(RAWDATA_TIMER_MS + RAWDATA_TIMER_MARGIN_MS);
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_THD_LO_STORE_DATA, 0, 0);

	return size;
}

static ssize_t barcode_emul_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", pdata->bBarcodeEnabled);
}

static ssize_t barcode_emul_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int iRet;
	int64_t dEnable;
	iRet = kstrtoll(buf, 10, &dEnable);
	if (iRet < 0)
		return iRet;
	if (dEnable)
		pdata->bBarcodeEnabled = 1;
	else
		pdata->bBarcodeEnabled = 0;
	return size;
}

static ssize_t prox_cancel_pass_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].cal_done);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].cal_done);
}

static ssize_t prox_default_trim_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);

#ifdef CONFIG_SUPPORT_PROX_AUTO_CAL
	pr_info("[FACTORY] %s: %d\n", __func__,
		data->sensor_data[ADSP_FACTORY_PROX].offset);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data->sensor_data[ADSP_FACTORY_PROX].offset);
#else
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message), NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].trim);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].trim);
#endif
}

#ifdef CONFIG_SUPPORT_PROX_AUTO_CAL
static ssize_t prox_thresh_detect_high_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message),
		NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d,%d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectLo);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectHi);
}

static ssize_t prox_thresh_detect_high_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct msg_big_data message;
	int thd = 0;

	if (kstrtoint(buf, 10, &thd)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	message.sensor_type = ADSP_FACTORY_PROX;
	message.msg_size = 4;
	message.int_msg[0] = thd;
	msleep(RAWDATA_TIMER_MS + RAWDATA_TIMER_MARGIN_MS);
	adsp_unicast(&message, sizeof(message),
		NETLINK_MESSAGE_THD_DETECT_HI_STORE_DATA, 0, 0);

	return size;
}

static ssize_t prox_thresh_detect_low_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	struct msg_data message;

	message.sensor_type = ADSP_FACTORY_PROX;
	adsp_unicast(&message, sizeof(message),
		NETLINK_MESSAGE_GET_CALIB_DATA, 0, 0);

	while (!(data->calib_ready_flag & 1 << ADSP_FACTORY_PROX))
		msleep(20);

	data->calib_ready_flag &= 0 << ADSP_FACTORY_PROX;

	pr_info("[FACTORY] %s: %d,%d\n", __func__,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectHi,
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectLo);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->sensor_calib_data[ADSP_FACTORY_PROX].threDetectLo);
}

static ssize_t prox_thresh_detect_low_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct msg_big_data message;
	int thd = 0;

	if (kstrtoint(buf, 10, &thd)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	message.sensor_type = ADSP_FACTORY_PROX;
	message.msg_size = 4;
	message.int_msg[0] = thd;
	msleep(RAWDATA_TIMER_MS + RAWDATA_TIMER_MARGIN_MS);
	adsp_unicast(&message, sizeof(message),
		NETLINK_MESSAGE_THD_DETECT_LO_STORE_DATA, 0, 0);

	return size;
}
#endif

static ssize_t prox_alert_thresh_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", PROX_ALERT_THRESHOLD);
}

static DEVICE_ATTR(vendor, S_IRUGO, prox_vendor_show, NULL);
static DEVICE_ATTR(name, S_IRUGO, prox_name_show, NULL);
static DEVICE_ATTR(state, S_IRUGO, prox_state_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO, prox_raw_data_show, NULL);
static DEVICE_ATTR(prox_avg, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_avg_show, prox_avg_store);
static DEVICE_ATTR(prox_cal, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_cancel_show, prox_cancel_store);
static DEVICE_ATTR(thresh_high, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_thresh_high_show, prox_thresh_high_store);
static DEVICE_ATTR(thresh_low, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_thresh_low_show, prox_thresh_low_store);
static DEVICE_ATTR(barcode_emul_en, S_IRUGO | S_IWUSR | S_IWGRP,
	barcode_emul_enable_show, barcode_emul_enable_store);
static DEVICE_ATTR(prox_offset_pass, S_IRUGO, prox_cancel_pass_show, NULL);
static DEVICE_ATTR(prox_trim, S_IRUGO, prox_default_trim_show, NULL);

#ifdef CONFIG_SUPPORT_PROX_AUTO_CAL
static DEVICE_ATTR(thresh_detect_high, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_thresh_detect_high_show, prox_thresh_detect_high_store);
static DEVICE_ATTR(thresh_detect_low, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_thresh_detect_low_show, prox_thresh_detect_low_store);
#endif
static DEVICE_ATTR(prox_alert_thresh, S_IRUGO, prox_alert_thresh_show, NULL);

static struct device_attribute *prox_attrs[] = {
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_state,
	&dev_attr_raw_data,
	&dev_attr_prox_avg,
	&dev_attr_prox_cal,
	&dev_attr_thresh_high,
	&dev_attr_thresh_low,
	&dev_attr_barcode_emul_en,
	&dev_attr_prox_offset_pass,
	&dev_attr_prox_trim,
#ifdef CONFIG_SUPPORT_PROX_AUTO_CAL
	&dev_attr_thresh_detect_high,
	&dev_attr_thresh_detect_low,
#endif
	&dev_attr_prox_alert_thresh,
	NULL,
};

static int __init tmd490x_prox_factory_init(void)
{
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	adsp_factory_register(ADSP_FACTORY_PROX, prox_attrs);
	pr_info("[FACTORY] %s\n", __func__);

	hrtimer_init(&pdata->prox_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pdata->prox_timer.function = prox_timer_func;
	pdata->prox_wq = create_singlethread_workqueue("prox_wq");

 	/* this is the thread function we run on the work queue */
	INIT_WORK(&pdata->work_prox, prox_work_func);

	pdata->avgwork_check = 0;
	pdata->avgtimer_enabled = 0;
	pdata->avg = 0;
	pdata->min = 0;
	pdata->max = 0;
	return 0;
}

static void __exit tmd490x_prox_factory_exit(void)
{
	if (pdata->avgtimer_enabled == 1) {
		hrtimer_cancel(&pdata->prox_timer);
		cancel_work_sync(&pdata->work_prox);
	}
	destroy_workqueue(pdata->prox_wq);
	adsp_factory_unregister(ADSP_FACTORY_PROX);
	kfree(pdata);
	pr_info("[FACTORY] %s\n", __func__);
}
module_init(tmd490x_prox_factory_init);
module_exit(tmd490x_prox_factory_exit);
