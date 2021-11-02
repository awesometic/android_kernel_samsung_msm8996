/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include "synaptics_i2c_rmi.h"

#define I2C_BURST_LIMIT 255
#define XFER_MSGS_LIMIT 1

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_stop_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_start_device(struct synaptics_rmi4_data *rmi4_data);

static void synaptics_rmi4_f51_finger_timer(unsigned long data);

static ssize_t synaptics_rmi4_f51_enables_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f51_enables_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static int synaptics_rmi4_input_open(struct input_dev *dev);
static void synaptics_rmi4_input_close(struct input_dev *dev);

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static DEVICE_ATTR(proximity_enables, (S_IRUGO | S_IWUSR | S_IWGRP),
			synaptics_rmi4_f51_enables_show, synaptics_rmi4_f51_enables_store);
static DEVICE_ATTR(reset, S_IWUSR | S_IWGRP, NULL, synaptics_rmi4_f01_reset_store);
static DEVICE_ATTR(productinfo, S_IRUGO, synaptics_rmi4_f01_productinfo_show, NULL);
static DEVICE_ATTR(buildid, S_IRUGO, synaptics_rmi4_f01_buildid_show, NULL);
static DEVICE_ATTR(flashprog, S_IRUGO, synaptics_rmi4_f01_flashprog_show, NULL);
static DEVICE_ATTR(0dbutton, (S_IRUGO | S_IWUSR | S_IWGRP), synaptics_rmi4_0dbutton_show, synaptics_rmi4_0dbutton_store);

static struct attribute *attrs[] = {
	&dev_attr_proximity_enables.attr,
	&dev_attr_reset.attr,
	&dev_attr_productinfo.attr,
	&dev_attr_buildid.attr,
	&dev_attr_flashprog.attr,
	&dev_attr_0dbutton.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.name = "rmii2c",
	.attrs = attrs,
};

static ssize_t synaptics_rmi4_f51_enables_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n",
			f51->proximity_enables);
}

static ssize_t synaptics_rmi4_f51_enables_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!f51)
		return -ENODEV;

	if (kstrtoint(buf, 10, &input))
		return -EINVAL;

	f51->proximity_enables = (unsigned char)input;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->proximity_enables_addr,
			&f51->proximity_enables,
			sizeof(f51->proximity_enables));
	if (retval < 0) {
		input_err(true, dev, "%s: Failed to write proximity enables, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int reset;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (kstrtoint(buf, 10, &reset))
		return -EINVAL;

	if (reset != 1)
		return -EINVAL;

	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		input_err(true, dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			(rmi4_data->rmi4_mod_info.product_info[0]),
			(rmi4_data->rmi4_mod_info.product_info[1]));
}

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int build_id;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	build_id = (unsigned int)rmi->build_id[0] +
		(unsigned int)rmi->build_id[1] * 0x100 +
		(unsigned int)rmi->build_id[2] * 0x10000;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			build_id);
}

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct synaptics_rmi4_f01_device_status device_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			device_status.data,
			sizeof(device_status.data));
	if (retval < 0) {
		input_err(true, dev,
				"%s: Failed to read device status, error = %d\n",
				__func__, retval);
		return retval;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n",
			device_status.flash_prog);
}

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->button_0d_enabled);
}

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	unsigned char ii;
	unsigned char intr_enable;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (kstrtoint(buf, 10, &input))
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	if (rmi4_data->button_0d_enabled == input)
		return count;

	if (list_empty(&rmi->support_fn_list))
		return -ENODEV;

	list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
		if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
			ii = fhandler->intr_reg_num;

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}

			if (input == 1)
				intr_enable |= fhandler->intr_mask;
			else
				intr_enable &= ~fhandler->intr_mask;

			retval = synaptics_rmi4_i2c_write(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
		}
	}

	rmi4_data->button_0d_enabled = input;

	return count;
}

static void reset_func(struct work_struct *work)
{
	int retval;
	int old_awake;
	struct synaptics_rmi4_data *rmi4_data = container_of(work, struct synaptics_rmi4_data ,work_reset.work);
	input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Executing Reset by workqueue \n",__func__);

	old_awake = rmi4_data->stay_awake;
	rmi4_data->stay_awake = true;
	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
						"%s: Failed to issue reset command, error = %d\n",
						__func__, retval);
	}
	rmi4_data->stay_awake = old_awake;
	msleep(SYNAPTICS_HW_RESET_TIME);
	rmi4_data->resetting = false;
}

/**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */
static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	unsigned char buf_page[PAGE_SELECT_LEN];
	unsigned char page;
#ifdef I2C_BURST_LIMIT
	unsigned char ii;
	unsigned char rd_msgs = ((length - 1) / I2C_BURST_LIMIT) + 1;
#else
	unsigned char rd_msgs = 1;
#endif
	unsigned char index = 0;
	unsigned char xfer_msgs;
	unsigned char remaining_msgs;
	unsigned short data_offset = 0;
	unsigned short remaining_length = length;
	struct i2c_msg msg[rd_msgs + 2];

	msg[0].addr = rmi4_data->i2c_client->addr;
	msg[0].flags = 0;
	msg[0].len = PAGE_SELECT_LEN;
	msg[0].buf = buf_page;

	msg[1].addr = rmi4_data->i2c_client->addr;
	msg[1].flags = 0;
	msg[1].len = 1;
	msg[1].buf = &buf;

#ifdef I2C_BURST_LIMIT
	for (ii = 0; ii < (rd_msgs - 1); ii++) {
		msg[ii + 2].addr = rmi4_data->i2c_client->addr;
		msg[ii + 2].flags = I2C_M_RD;
		msg[ii + 2].len = I2C_BURST_LIMIT;
		msg[ii + 2].buf = &data[data_offset];
		data_offset += I2C_BURST_LIMIT;
		remaining_length -= I2C_BURST_LIMIT;
	}
#endif

	msg[rd_msgs + 1].addr = rmi4_data->i2c_client->addr;
	msg[rd_msgs + 1].flags = I2C_M_RD;
	msg[rd_msgs + 1].len = remaining_length;
	msg[rd_msgs + 1].buf = &data[data_offset];

	page = ((addr >> 8) & MASK_8BIT);
	buf_page[0] = MASK_8BIT;
	buf_page[1] = page;

	buf = addr & MASK_8BIT;

	remaining_msgs = rd_msgs + 2;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	if (rmi4_data->touch_stopped) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		retval = 0;
		goto exit;
	}

	while (remaining_msgs) {
#ifdef XFER_MSGS_LIMIT
		if (remaining_msgs > XFER_MSGS_LIMIT)
			xfer_msgs = XFER_MSGS_LIMIT;
		else
			xfer_msgs = remaining_msgs;
#else
		xfer_msgs = remaining_msgs;
#endif
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			if (i2c_transfer(rmi4_data->i2c_client->adapter, &msg[index], xfer_msgs) == xfer_msgs)
				break;
			input_err(true, &rmi4_data->i2c_client->dev, "%s: I2C retry %d\n",
					__func__, retry + 1);
			msleep(20);
		}

		if (retry == SYN_I2C_RETRY_TIMES) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: I2C read over retry limit\n", __func__);
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: rd_msgs = %d, remaining_msgs = %d\n",
					__func__, rd_msgs, remaining_msgs);
			if ((rmi4_data->work_reset.work.func != NULL) &&
					(rmi4_data->resetting == false)) {
				input_err(true, &rmi4_data->i2c_client->dev,
						"%s: reset triggered\n", __func__);
				rmi4_data->resetting = true;
				schedule_delayed_work(&rmi4_data->work_reset, 2);
			}
			retval = -EIO;
			goto exit;
		}
		remaining_msgs -= xfer_msgs;
		index += xfer_msgs;
	}

	retval = length;

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

/**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char *buf;
	unsigned char buf_page[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_msg msg[2];

	buf = kzalloc(length + 1, GFP_KERNEL);
	if (!buf) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for buffer\n",
				__func__);
		return -ENOMEM;
	}

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	page = ((addr >> 8) & MASK_8BIT);
	buf_page[0] = MASK_8BIT;
	buf_page[1] = page;

	msg[0].addr = rmi4_data->i2c_client->addr;
	msg[0].flags = 0;
	msg[0].len = PAGE_SELECT_LEN;
	msg[0].buf = buf_page;

	msg[1].addr = rmi4_data->i2c_client->addr;
	msg[1].flags = 0;
	msg[1].len = length + 1;
	msg[1].buf = buf;

	if (rmi4_data->touch_stopped) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Sensor stopped\n",
				__func__);
		retval = 0;
		goto exit;
	}

	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));
	kfree(buf);

	return retval;
}


/* Below function acess the register manually. so please keep caution when use this.
 * I do not recommend to use this function except special case....
 * mode[0 / 1] 0: read, 1: write
 */
int synaptics_rmi4_access_register(struct synaptics_rmi4_data *rmi4_data,
				bool mode, unsigned short address, int length, unsigned char *value)
{
	int retval = 0, ii = 0;
	unsigned char data[10] = {0, };
	char temp[70] = {0, };
	char t_temp[7] = {0, };

	if (mode == SYNAPTICS_ACCESS_WRITE) {
		retval = synaptics_rmi4_i2c_write(rmi4_data,
				address, value, length);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to write 0x%X ~ %d byte\n",
				__func__, address, length);
			goto out;
		}

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				address, data, length);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read 0x%04X\n",
					__func__, address);
			goto out;
		}
		snprintf(temp, 1, ":");
		while (ii < length) {
			snprintf(t_temp, 7, "0x%X, ", data[ii]);
			strcat(temp, t_temp);
			ii++;
		}
	} else {
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				address, data, length);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read 0x%04X\n",
				__func__, address);
			goto out;
		}
		memcpy(value, data, length);

		snprintf(temp, 1, ":");
		while (ii < length) {
			snprintf(t_temp, 7, "0x%X, ", data[ii]);
			strcat(temp, t_temp);
			ii++;
		}
	}
	input_info(true, &rmi4_data->i2c_client->dev, "%s: [%c]0x%04X, length:%d, data: %s\n",
		__func__, mode ? 'W' : 'R', address, length, temp);

out:
	return retval;
}

void synaptics_rmi4_f12_abs_report_print_log(struct synaptics_rmi4_data *rmi4_data)
{
	int ii;

	for (ii = 0; ii < MAX_NUMBER_OF_FINGERS ; ii++) {
		switch (rmi4_data->finger[ii].print_type) {
		case TOUCH_PRESS:
		case TOUCH_CHANGE:

			input_info(true, &rmi4_data->i2c_client->dev, "[%d][%s] 0x%02x T[%d] M[%d]"
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
			", x = %d, y = %d, wx = %d, wy = %d, z = %d, p = %d\n",
			ii, rmi4_data->finger[ii].print_type == TOUCH_PRESS ? "P" : "C",
			rmi4_data->finger[ii].finger_status,
			rmi4_data->finger[ii].tool_type, rmi4_data->finger[ii].mcount,
			rmi4_data->finger[ii].x, rmi4_data->finger[ii].y,
			rmi4_data->finger[ii].wx, rmi4_data->finger[ii].wy, rmi4_data->finger[ii].z,
			rmi4_data->finger[ii].custom & 0x01);
#else
			"\n", ii, rmi4_data->finger[ii].print_type == TOUCH_PRESS ? "P" : "C",
			rmi4_data->finger[ii].finger_status, rmi4_data->finger[ii].tool_type,
			rmi4_data->finger[ii].mcount);
#endif
			break;
		case TOUCH_RELEASE:
		case TOUCH_RELEASE_FORCED:
			input_info(true, &rmi4_data->i2c_client->dev,
				"[%d][%s] 0x%02x M[%d], Ver[%02X%02X%02X%02X, 0x%02X, 0x%02X/0x%02X/0x%02X]\n",
				ii, rmi4_data->finger[ii].print_type == TOUCH_RELEASE ? "R" : "RA",
				rmi4_data->finger[ii].finger_status, rmi4_data->finger[ii].mcount,
				rmi4_data->fw_release_date_of_ic >> 8 & 0xFF,
				rmi4_data->fw_release_date_of_ic & 0xFF, rmi4_data->ic_revision_of_ic,
				rmi4_data->fw_version_of_ic, rmi4_data->f12.feature_enable,
				rmi4_data->f51 == NULL ? 0 : rmi4_data->f51->proximity_enables,
				rmi4_data->f51 == NULL ? 0 : rmi4_data->f51->general_control,
				rmi4_data->f51 == NULL ? 0 : rmi4_data->f51->general_control_2);
			rmi4_data->finger[ii].mcount = 0;
			rmi4_data->finger[ii].state = 0;

			break;
		case TOUCH_NONE:
			break;
		default:
			input_info(true, &rmi4_data->i2c_client->dev,"%s : Unkonwn Type!", __func__);
			break;
		}

		rmi4_data->finger[ii].print_type = TOUCH_NONE;
	}
}

static void synaptics_rmi4_release_all_finger(struct synaptics_rmi4_data *rmi4_data)
{
	unsigned char ii;

	if (!rmi4_data->tsp_probe) {
		input_info(true, &rmi4_data->i2c_client->dev, "%s: it is not probed or f51 is null.\n",
				__func__);
		return;
	}

	for (ii = 0; ii < rmi4_data->num_of_fingers; ii++) {
		input_mt_slot(rmi4_data->input_dev, ii);
		if (rmi4_data->finger[ii].state) {
			rmi4_data->finger[ii].print_type = TOUCH_RELEASE_FORCED;
			input_report_abs(rmi4_data->input_dev, ABS_MT_CUSTOM, 0);
#ifdef CONFIG_SEC_FACTORY
			input_report_abs(rmi4_data->input_dev,ABS_MT_PRESSURE, 0);
#endif
		}
		input_mt_report_slot_state(rmi4_data->input_dev, MT_TOOL_FINGER, 0);
	}

	input_report_key(rmi4_data->input_dev, BTN_TOUCH, 0);
	input_report_key(rmi4_data->input_dev, BTN_TOOL_FINGER, 0);
#ifdef GLOVE_MODE
	input_report_switch(rmi4_data->input_dev, SW_GLOVE, false);
	rmi4_data->touchkey_glove_mode_status = false;
#endif
	input_sync(rmi4_data->input_dev);

	synaptics_rmi4_f12_abs_report_print_log(rmi4_data);

	rmi4_data->fingers_on_2d = false;
	rmi4_data->f51_finger = false;
	if ((rmi4_data->f51 != NULL) && (rmi4_data->f51->edge_swipe_data.palm))
		rmi4_data->f51->edge_swipe_data.palm = 0;
}

void synpatics_rmi4_release_all_event(struct synaptics_rmi4_data *rmi4_data, unsigned char type)
{
	if (type & RELEASE_TYPE_FINGER)
		synaptics_rmi4_release_all_finger(rmi4_data);
}

/**
 * synaptics_rmi4_f11_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $11
 * finger data has been detected.
 *
 * This function reads the Function $11 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f11_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char reg_index;
	unsigned char finger;
	unsigned char fingers_supported;
	unsigned char num_of_finger_status_regs;
	unsigned char finger_shift;
	unsigned char finger_status;
	unsigned char data_reg_blk_size;
	unsigned char finger_status_reg[3];
	unsigned char data[F11_STD_DATA_LEN];
	unsigned short data_addr;
	unsigned short data_offset;
	int x;
	int y;
	int wx;
	int wy;

	/*
	 * The number of finger status registers is determined by the
	 * maximum number of fingers supported - 2 bits per finger. So
	 * the number of finger status registers to read is:
	 * register_count = ceil(max_num_of_fingers / 4)
	 */

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return 0;
	}

	fingers_supported = fhandler->num_of_data_points;
	num_of_finger_status_regs = (fingers_supported + 3) / 4;
	data_addr = fhandler->full_addr.data_base;
	data_reg_blk_size = fhandler->size_of_data_register_block;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			finger_status_reg,
			num_of_finger_status_regs);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return 0;
	}

	for (finger = 0; finger < fingers_supported; finger++) {
		reg_index = finger / 4;
		finger_shift = (finger % 4) * 2;
		finger_status = (finger_status_reg[reg_index] >> finger_shift)
			& MASK_2BIT;

		/*
		 * Each 2-bit finger status field represents the following:
		 * 00 = finger not present
		 * 01 = finger present and data accurate
		 * 10 = finger present but data may be inaccurate
		 * 11 = reserved
		 */
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status ? true : false);

		if (finger_status) {
			data_offset = data_addr +
				num_of_finger_status_regs +
				(finger * data_reg_blk_size);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					data_offset,
					data,
					data_reg_blk_size);
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
					__func__, __LINE__, retval);
				return 0;
			}

			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wx = (data[3] & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;

			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));

			input_dbg(false, &rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y, wx, wy);

			touch_count++;
		}
	}
	input_sync(rmi4_data->input_dev);

	return touch_count;
}

/**
 * synaptics_rmi4_f12_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $12
 * finger data has been detected.
 *
 * This function reads the Function $12 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f12_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0;
	unsigned char finger;
	unsigned char fingers_to_process;
	unsigned char finger_status;
	unsigned char size_of_2d_data;
	unsigned short data_addr;
	int x;
	int y;
	int wx;
	int wy;
	int x_y_tmp;
	int z = 0;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_finger_data *data;
	struct synaptics_rmi4_f12_finger_data *finger_data;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	unsigned char tool_type = MT_TOOL_FINGER;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return 0;
	}
	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return 0;
	}

	fingers_to_process = fhandler->num_of_data_points;
	data_addr = fhandler->full_addr.data_base;
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	/* Determine the total number of fingers to process */
	if (extra_data->data15_size) {
		unsigned short object_attention = 0;

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				data_addr + extra_data->data15_offset,
				extra_data->data15_data,
				extra_data->data15_size);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return 0;
		}

		/* Object_attention : [000000xx xxxxxxxxx] : "x" represent that finger is on or not
		 * Get the highest finger number to read finger data efficently.
		 */
		object_attention = extra_data->data15_data[0];
		if (extra_data->data15_size > 1)
			object_attention |= (extra_data->data15_data[1] & MASK_3BIT) << 8;

		for (; fingers_to_process > 0; fingers_to_process--) {
			if (object_attention & (1 << (fingers_to_process - 1)))
				break;
		}

		input_dbg(false, &rmi4_data->i2c_client->dev, "fingers[%d, %d] object_attention[0x%02x]\n",
			fingers_to_process, rmi4_data->fingers_already_present, object_attention);
	}

	fingers_to_process = max(fingers_to_process, rmi4_data->fingers_already_present);

	if (!fingers_to_process) {
		synaptics_rmi4_release_all_finger(rmi4_data);
		return 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr + extra_data->data1_offset,
			(unsigned char *)fhandler->data,
			fingers_to_process * size_of_2d_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return 0;
	}
	data = (struct synaptics_rmi4_f12_finger_data *)fhandler->data;

	for (finger = 0; finger < fingers_to_process; finger++) {
		finger_data = data + finger;
		finger_status = (finger_data->object_type_and_status & MASK_3BIT);
		x = (finger_data->x_msb << 8) | (finger_data->x_lsb);
		y = (finger_data->y_msb << 8) | (finger_data->y_lsb);
		if ((x == INVALID_X) && (y == INVALID_Y))
			finger_status = OBJECT_NOT_PRESENT;


		if ((x > rmi4_data->sensor_max_x) || (y > rmi4_data->sensor_max_y) || finger_status == OBJECT_UNCLASSIFIED) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"[%d] 0x%02x [%d:%d], Ver[%02X%02X%02X%02X]\n",
					finger, finger_status, x, y,
					rmi4_data->fw_release_date_of_ic >> 8 & 0xFF,
					rmi4_data->fw_release_date_of_ic & 0xFF,
					rmi4_data->ic_revision_of_ic, rmi4_data->fw_version_of_ic);
			finger_status = OBJECT_NOT_PRESENT;
		}

		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev, tool_type, finger_status ? true : false);

		rmi4_data->finger[finger].tool_type = tool_type;
		rmi4_data->finger[finger].finger_status= finger_status;

		if (finger_status != OBJECT_NOT_PRESENT) {
			rmi4_data->fingers_already_present = finger + 1;
#ifdef GLOVE_MODE
			if ((finger_status == OBJECT_GLOVE || finger_status == OBJECT_PASSIVE_STYLUS)
					&& !rmi4_data->touchkey_glove_mode_status) {
				rmi4_data->touchkey_glove_mode_status = true;
				input_report_switch(rmi4_data->input_dev, SW_GLOVE, true);
			}
			if ((finger_status != OBJECT_GLOVE && finger_status != OBJECT_PASSIVE_STYLUS)
					&& rmi4_data->touchkey_glove_mode_status) {
				rmi4_data->touchkey_glove_mode_status = false;
				input_report_switch(rmi4_data->input_dev, SW_GLOVE, false);
			}
#endif
			wx = finger_data->wx;
			wy = finger_data->wy;
			z = finger_data->z;

			if (z <= 0)
				z = 1;

			if (rmi4_data->use_brush)
				rmi4_data->finger[finger].custom = (z & 0xFF) << 1;
			else 
				rmi4_data->finger[finger].custom = 0x3F << 1;

			if (f51) {
				if (f51->proximity_controls & HAS_EDGE_SWIPE) {
					u8 palm_flag = f51->edge_swipe_data.palm & 0x01;
					if (finger_status == OBJECT_PALM) {
						wx = f51->edge_swipe_data.wx;
						wy = f51->edge_swipe_data.wy;
						palm_flag = 1;
					}
					rmi4_data->finger[finger].custom |= palm_flag;
				}
			}
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_CUSTOM, rmi4_data->finger[finger].custom);

			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;
			if (rmi4_data->board->x_y_chnage) {
				x_y_tmp = x;
				x = y;
				y = x_y_tmp;
			}

			if (rmi4_data->board->x_offset) {
				x = x - rmi4_data->board->x_offset;
			}

			rmi4_data->finger[finger].x = x;
			rmi4_data->finger[finger].y = y;
			rmi4_data->finger[finger].wx = wx;
			rmi4_data->finger[finger].wy = wy;
			rmi4_data->finger[finger].z = z;

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, finger_status == OBJECT_HOVER ? 0 : 1);
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#ifdef CONFIG_SEC_FACTORY
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_PRESSURE, z);
#endif

			if (rmi4_data->finger[finger].state) {
				rmi4_data->finger[finger].mcount++;
				if (rmi4_data->finger[finger].state != finger_status){
					rmi4_data->finger[finger].print_type = TOUCH_CHANGE;
				}
			} else {
				rmi4_data->finger[finger].print_type = TOUCH_PRESS;
			}
			touch_count++;

		} else {
			if (rmi4_data->finger[finger].state) {
				rmi4_data->finger[finger].print_type = TOUCH_RELEASE;
			}
		}
		rmi4_data->finger[finger].state = finger_status;
	}

	/* Clear BTN_TOUCH when All touch are released  */
	if (touch_count == 0) {
		input_report_key(rmi4_data->input_dev, BTN_TOUCH, 0);
		rmi4_data->fingers_already_present = 0;
	}
	input_sync(rmi4_data->input_dev);

	synaptics_rmi4_f12_abs_report_print_log(rmi4_data);

	return touch_count;
}

static void synaptics_rmi4_f1a_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0;
	unsigned char button;
	unsigned char index;
	unsigned char shift;
	unsigned char status;
	unsigned char *data;
	unsigned short data_addr;
	struct synaptics_rmi4_f1a_handle *f1a;
	static unsigned char do_once = 1;
	static bool current_status[MAX_NUMBER_OF_BUTTONS];

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return;
	}

	data_addr = fhandler->full_addr.data_base;
	f1a = fhandler->data;

	if (do_once) {
		memset(current_status, 0, sizeof(current_status));
		do_once = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read button data registers\n",
				__func__);
		return;
	}

	data = f1a->button_data_buffer;

	for (button = 0; button < f1a->valid_button_count; button++) {
		index = button / 8;
		shift = button % 8;
		status = ((data[index] >> shift) & MASK_1BIT);

		if (current_status[button] == status)
			continue;
		else
			current_status[button] = status;

		input_dbg(false, &rmi4_data->i2c_client->dev,
				"%s: Button %d (code %d) ->%d\n",
				__func__, button,
				f1a->button_map[button],
				status);
		touch_count++;
		input_report_key(rmi4_data->input_dev,
				f1a->button_map[button],
				status);
	}

	if (touch_count)
		input_sync(rmi4_data->input_dev);

	return;
}

static int synaptics_rmi4_f51_edge_swipe(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval = 0;
	struct synaptics_rmi4_f51_data *data;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!fhandler || !rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler or f51 is not exist\n", __func__);
		return -ENXIO;
	}

	if (!(f51->general_control & EDGE_SWIPE_EN)) {
		input_info(true, &rmi4_data->i2c_client->dev, "%s : Edge swipe report is disabled. General control[0x%02x]\n",
			__func__, f51->general_control);
		goto out;
	}

	data = (struct synaptics_rmi4_f51_data *)fhandler->data;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			f51->edge_swipe_data_addr,
			data->edge_swipe_data,
			sizeof(data->edge_swipe_data));

	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		goto out;
	}

	f51->edge_swipe_data.sumsize = data->edge_swipe_mm;
	f51->edge_swipe_data.wx = data->edge_swipe_wx;
	f51->edge_swipe_data.wy = data->edge_swipe_wy;
	f51->edge_swipe_data.palm = data->edge_swipe_z;

	input_dbg(false, &rmi4_data->i2c_client->dev,
		"%s: edge_data : x[%d], y[%d], z[%d] ,wx[%d], wy[%d], area[%d]\n", __func__,
		data->edge_swipe_x_msb << 8 | data->edge_swipe_x_lsb,
		data->edge_swipe_y_msb << 8 | data->edge_swipe_y_lsb,
		data->edge_swipe_z, data->edge_swipe_wx, data->edge_swipe_wy,
		data->edge_swipe_mm);

out:
	return retval;
}

static int synaptics_rmi4_f51_lookup_detection_flag_2(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval = 0;

	if (!fhandler || !rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler or f51 is not exist\n", __func__);
		return -ENXIO;
	}

	retval = synaptics_rmi4_f51_edge_swipe(rmi4_data, fhandler);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read edge swipe data\n",
				__func__);
		goto out;
	}

	input_dbg(false, &rmi4_data->i2c_client->dev,
		"%s: edge_swipe_detected : sumsize[%d], palm[%d], wx[%d] ,wy[%d]\n",
		 __func__, rmi4_data->f51->edge_swipe_data.sumsize,
		rmi4_data->f51->edge_swipe_data.palm, rmi4_data->f51->edge_swipe_data.wx,
		rmi4_data->f51->edge_swipe_data.wy);
out:
	return retval;
}

static void synaptics_rmi4_f51_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return;
	}

	if (f51->proximity_controls & HAS_EDGE_SWIPE) {
		retval = synaptics_rmi4_f51_lookup_detection_flag_2(rmi4_data, fhandler);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to treat proximity data\n",
					__func__);
			return;
		}
	}

	return;
}

/**
 * synaptics_rmi4_report_touch()
 *
 * Called by synaptics_rmi4_sensor_report().
 *
 * This function calls the appropriate finger data reporting function
 * based on the function handler it receives and returns the number of
 * fingers detected.
 */
static void synaptics_rmi4_report_touch(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	unsigned char touch_count_2d;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return;
	}

	input_dbg(false, &rmi4_data->i2c_client->dev,
			"%s: Function %02x reporting\n",
			__func__, fhandler->fn_number);

	switch (fhandler->fn_number) {
	case SYNAPTICS_RMI4_F11:
		touch_count_2d = synaptics_rmi4_f11_abs_report(rmi4_data,
				fhandler);
		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F12:
		touch_count_2d = synaptics_rmi4_f12_abs_report(rmi4_data,
				fhandler);
		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F1A:
		synaptics_rmi4_f1a_report(rmi4_data, fhandler);
		break;
	case SYNAPTICS_RMI4_F51:
		synaptics_rmi4_f51_report(rmi4_data, fhandler);
		break;
	default:
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Undefined RMI function number[0X%02X]\n",
				__func__, fhandler->fn_number);
		break;
	}

	return;
}

/**
 * synaptics_rmi4_sensor_report()
 *
 * Called by synaptics_rmi4_irq().
 *
 * This function determines the interrupt source(s) from the sensor
 * and calls synaptics_rmi4_report_touch() with the appropriate
 * function handler for each function with valid data inputs.
 */
static int synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char data[MAX_INTR_REGISTERS + 1];
	unsigned char *intr = &data[1];
	struct synaptics_rmi4_f01_device_status status;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	/*
	 * Get interrupt status information from F01 Data1 register to
	 * determine the source(s) that are flagging the interrupt.
	 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			data,
			rmi4_data->num_of_intr_regs + 1);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return retval;
	}

	status.data[0] = data[0];
	if (status.unconfigured) {
		if (rmi4_data->doing_reflash) {
			input_err(true, &rmi4_data->i2c_client->dev,
				"Spontaneous reset detected during reflash.\n");
			return 0;
		}

		input_info(true, &rmi4_data->i2c_client->dev,
			"Spontaneous reset detected\n");
		retval = synaptics_rmi4_reinit_device(rmi4_data);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to reinit device\n",
					__func__);
		}
		return 0;
	}

	/*
	 * Traverse the function handler list and service the source(s)
	 * of the interrupt accordingly.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				if (fhandler->intr_mask &
						intr[fhandler->intr_reg_num]) {
					synaptics_rmi4_report_touch(rmi4_data,
							fhandler);
				}
			}
		}
	}

	if (!list_empty(&rmi->exp_fn_list)) {
		list_for_each_entry(exp_fhandler, &rmi->exp_fn_list, link) {
			if (exp_fhandler->initialized &&
					(exp_fhandler->func_attn != NULL))
				exp_fhandler->func_attn(rmi4_data, intr[0]);
		}
	}

	return 0;
}

/**
 * synaptics_rmi4_irq()
 *
 * Called by the kernel when an interrupt occurs (when the sensor
 * asserts the attention irq).
 *
 * This function is the ISR thread and handles the acquisition
 * and the reporting of finger data when the presence of fingers
 * is detected.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = data;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	do {
		retval = synaptics_rmi4_sensor_report(rmi4_data);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read",
				__func__);
			goto out;
		}

		if (rmi4_data->touch_stopped)
			goto out;

	} while (!gpio_get_value(pdata->gpio));

out:
	return IRQ_HANDLED;
}

/**
 * synaptics_rmi4_irq_enable()
 *
 * Called by synaptics_rmi4_probe() and the power management functions
 * in this driver and also exported to other expansion Function modules
 * such as rmi_dev.
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_enable(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	unsigned char intr_status[MAX_INTR_REGISTERS];
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	if (enable) {
		if (rmi4_data->irq_enabled)
			return retval;

		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				intr_status,
				rmi4_data->num_of_intr_regs);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return retval;
		}

		retval = request_threaded_irq(rmi4_data->irq, NULL,
				synaptics_rmi4_irq, pdata->irq_type,
				DRIVER_NAME, rmi4_data);
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to create irq thread\n",
					__func__);
			return retval;
		}

		rmi4_data->irq_enabled = true;
	} else {
		if (rmi4_data->irq_enabled) {
			disable_irq(rmi4_data->irq);
			free_irq(rmi4_data->irq, rmi4_data);
			rmi4_data->irq_enabled = false;
		}
	}

	return retval;
}

/**
 * synaptics_rmi4_f11_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 11 registers
 * and determines the number of fingers supported, x and y data ranges,
 * offset to the associated interrupt status register, interrupt bit
 * mask, and gathers finger data acquisition capabilities from the query
 * registers.
 */
static int synaptics_rmi4_f11_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char abs_data_size;
	unsigned char abs_data_blk_size;
	unsigned char query[F11_STD_QUERY_LEN];
	unsigned char control[F11_STD_CTRL_LEN];

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query,
			sizeof(query));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum number of fingers supported */
	if ((query[1] & MASK_3BIT) <= 4)
		fhandler->num_of_data_points = (query[1] & MASK_3BIT) + 1;
	else if ((query[1] & MASK_3BIT) == 5)
		fhandler->num_of_data_points = 10;

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			control,
			sizeof(control));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum x and y */
	rmi4_data->sensor_max_x = ((control[6] & MASK_8BIT) << 0) |
		((control[7] & MASK_4BIT) << 8);
	rmi4_data->sensor_max_y = ((control[8] & MASK_8BIT) << 0) |
		((control[9] & MASK_4BIT) << 8);
	input_dbg(false, &rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);
	if (rmi4_data->sensor_max_x <= 0 || rmi4_data->sensor_max_y <= 0) {
		rmi4_data->sensor_max_x = rmi4_data->board->sensor_max_x;
		rmi4_data->sensor_max_y = rmi4_data->board->sensor_max_y;

		input_info(true, &rmi4_data->i2c_client->dev,
				"%s: Function %02x read failed, run dtsi coords. max x = %d max y = %d\n",
				__func__, fhandler->fn_number,
				rmi4_data->sensor_max_x,
				rmi4_data->sensor_max_y);
	}

	rmi4_data->max_touch_width = MAX_F11_TOUCH_WIDTH;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num >= MAX_INTR_REGISTERS) {
		fhandler->intr_reg_num = 0;
		fhandler->num_of_data_sources = 0;
		fhandler->intr_mask = 0;
		return -EINVAL;
	}
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset; ii < ((fd->intr_src_count & MASK_3BIT) +	intr_offset); ii++)
		fhandler->intr_mask |= 1 << ii;

	abs_data_size = query[5] & MASK_2BIT;
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	fhandler->size_of_data_register_block = abs_data_blk_size;
	fhandler->data = NULL;
	fhandler->extra = NULL;

	return retval;
}

int synaptics_rmi4_f12_ctrl11_set(struct synaptics_rmi4_data *rmi4_data,
		unsigned char data)
{
	struct synaptics_rmi4_f12_ctrl_11 ctrl_11;

	int retval;

	if (rmi4_data->touch_stopped)
		return 0;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f12.ctrl11_addr,
			ctrl_11.data,
			sizeof(ctrl_11.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	ctrl_11.data[2] = data; /* set a value of jitter filter */

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f12.ctrl11_addr,
			ctrl_11.data,
			sizeof(ctrl_11.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}

static int synaptics_rmi4_f12_set_feature(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	if (!rmi4_data->f12.glove_mode_feature)
		goto out;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
				rmi4_data->f12.ctrl26_addr,
				&rmi4_data->f12.feature_enable,
				sizeof(rmi4_data->f12.feature_enable));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: write fail[%d]\n",
			__func__, retval);
		goto out;
	}

	input_info(true, &rmi4_data->i2c_client->dev, "%s: [0x%02X], Set %s\n",
		 __func__, rmi4_data->f12.feature_enable,
		(rmi4_data->f12.feature_enable == (CLEAR_COVER_MODE_EN | FAST_GLOVE_DECTION_EN)) ? "Clear cover & Fast glove mode" :
		(rmi4_data->f12.feature_enable == (FLIP_COVER_MODE_EN | FAST_GLOVE_DECTION_EN)) ? "Flip cover & Fast glove mode" :
		(rmi4_data->f12.feature_enable == (GLOVE_DETECTION_EN | FAST_GLOVE_DECTION_EN)) ? "Fast glove mode" :
		(rmi4_data->f12.feature_enable == CLEAR_COVER_MODE_EN) ? "Clear cover" :
		(rmi4_data->f12.feature_enable == FLIP_COVER_MODE_EN) ? "Flip cover" :
		(rmi4_data->f12.feature_enable == GLOVE_DETECTION_EN) ? "Glove mode" : "All disabled");

out:
	return retval;
}

static int synaptics_rmi4_f12_set_report(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f12.ctrl28_addr,
			&rmi4_data->f12.report_enable,
			sizeof(rmi4_data->f12.report_enable));
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s: write fail[%d]\n",
		__func__, retval);

	return retval;
}

#ifdef GLOVE_MODE
static int synaptics_rmi4_f12_obj_report_enable(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f12.ctrl23_addr,
			&rmi4_data->f12.obj_report_enable,
			sizeof(rmi4_data->f12.obj_report_enable));
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev,
			"%s: write fail[%d]\n",
			__func__, retval);

	input_info(true, &rmi4_data->i2c_client->dev, "%s: [0x%02X], Glove mode %s\n",
				__func__, rmi4_data->f12.obj_report_enable,
				(rmi4_data->f12.obj_report_enable & OBJ_TYPE_GLOVE) ? "enabled" : "disabled");

	return retval;
}

int synaptics_rmi4_glove_mode_enables(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	if (!rmi4_data->f12.glove_mode_feature) {
		input_err(true, &rmi4_data->i2c_client->dev,
			"%s: Ver[%02X%02X%02X] FW does not support glove mode %02X\n",
			__func__, rmi4_data->panel_revision, rmi4_data->ic_revision_of_ic,
			rmi4_data->fw_version_of_ic, rmi4_data->f12.glove_mode_feature);
		goto out;
	}

	if (rmi4_data->touch_stopped)
		goto out;

	retval = synaptics_rmi4_f12_set_feature(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
	}

	retval = synaptics_rmi4_f12_obj_report_enable(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
			"%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
	}


out:
	return retval;
}
#endif

static int synaptics_rmi4_f12_set_init(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;

	retval = synaptics_rmi4_f12_set_feature(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, Failed to set featrue. [%d]\n",
				__func__, retval);
		goto out;
	}

	if(rmi4_data->f12.obj_report_enable == 0) {
		retval = synaptics_rmi4_i2c_read(rmi4_data,
						rmi4_data->f12.ctrl23_addr,
						&rmi4_data->f12.obj_report_enable,
						sizeof(rmi4_data->f12.obj_report_enable));
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to read f12, %d\n",
					__func__, retval);
			goto out;
		}
		input_info(true, &rmi4_data->i2c_client->dev,
					"%s: obj_report_enable default val[0x%x]\n",
					__func__, rmi4_data->f12.obj_report_enable);
	}

#ifdef GLOVE_MODE
	retval = synaptics_rmi4_f12_obj_report_enable(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
			"%s, Failed to set object. [%d]\n",
			__func__, retval);
		goto out;
	}
#endif

	retval = synaptics_rmi4_f12_set_report(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set report. [%d]\n",
			__func__, retval);
		goto out;
	}

out:
	return retval;
}

/**
 * synaptics_rmi4_f12_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 12 registers and
 * determines the number of fingers supported, offset to the data1
 * register, x and y data ranges, offset to the associated interrupt
 * status register, interrupt bit mask, and allocates memory resources
 * for finger data acquisition.
 */
static int synaptics_rmi4_f12_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char size_of_2d_data;
	unsigned char size_of_query8;
	unsigned char ctrl_8_offset;
	unsigned char ctrl_9_offset;
	unsigned char ctrl_11_offset;
	unsigned char ctrl_15_offset;
	unsigned char ctrl_23_offset;
	unsigned char ctrl_26_offset;
	unsigned char ctrl_28_offset;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_query_5 query_5;
	struct synaptics_rmi4_f12_query_8 query_8;
	struct synaptics_rmi4_f12_query_10 query_10;
	struct synaptics_rmi4_f12_ctrl_8 ctrl_8;
	struct synaptics_rmi4_f12_ctrl_9 ctrl_9;
	struct synaptics_rmi4_f12_ctrl_23 ctrl_23;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;
	fhandler->extra = kzalloc(sizeof(*extra_data), GFP_KERNEL);
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 5,
			query_5.data,
			sizeof(query_5.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	ctrl_8_offset = query_5.ctrl0_is_present +
		query_5.ctrl1_is_present +
		query_5.ctrl2_is_present +
		query_5.ctrl3_is_present +
		query_5.ctrl4_is_present +
		query_5.ctrl5_is_present +
		query_5.ctrl6_is_present +
		query_5.ctrl7_is_present;

	ctrl_9_offset = ctrl_8_offset +
		query_5.ctrl8_is_present;

	ctrl_11_offset = ctrl_9_offset +
		query_5.ctrl9_is_present +
		query_5.ctrl10_is_present;

	ctrl_15_offset = ctrl_11_offset +
		query_5.ctrl11_is_present +
		query_5.ctrl12_is_present +
		query_5.ctrl13_is_present +
		query_5.ctrl14_is_present;

	ctrl_23_offset = ctrl_15_offset +
		query_5.ctrl15_is_present +
		query_5.ctrl16_is_present +
		query_5.ctrl17_is_present +
		query_5.ctrl18_is_present +
		query_5.ctrl19_is_present +
		query_5.ctrl20_is_present +
		query_5.ctrl21_is_present +
		query_5.ctrl22_is_present;

	ctrl_26_offset = ctrl_23_offset +
		query_5.ctrl23_is_present +
		query_5.ctrl24_is_present +
		query_5.ctrl25_is_present;

	ctrl_28_offset = ctrl_26_offset +
		query_5.ctrl26_is_present +
		query_5.ctrl27_is_present;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_23_offset,
			ctrl_23.data,
			sizeof(ctrl_23.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum number of fingers supported */
	fhandler->num_of_data_points = min(ctrl_23.max_reported_objects,
			(unsigned char)F12_FINGERS_TO_SUPPORT);

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;
	/* for protection num of finger is going to zero value */
	if (!rmi4_data->num_of_fingers)
		rmi4_data->num_of_fingers = MAX_NUMBER_OF_FINGERS;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 7,
			&size_of_query8,
			sizeof(size_of_query8));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 8,
			query_8.data,
			size_of_query8);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Determine the presence of the Data0 register */
	extra_data->data1_offset = query_8.data0_is_present;

	if ((size_of_query8 >= 3) && (query_8.data15_is_present)) {
		extra_data->data15_offset = query_8.data0_is_present +
			query_8.data1_is_present +
			query_8.data2_is_present +
			query_8.data3_is_present +
			query_8.data4_is_present +
			query_8.data5_is_present +
			query_8.data6_is_present +
			query_8.data7_is_present +
			query_8.data8_is_present +
			query_8.data9_is_present +
			query_8.data10_is_present +
			query_8.data11_is_present +
			query_8.data12_is_present +
			query_8.data13_is_present +
			query_8.data14_is_present;
		extra_data->data15_size = (rmi4_data->num_of_fingers + 7) / 8;
	} else {
		extra_data->data15_size = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 10,
			query_10.data,
			sizeof(query_10.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s:%4d read fail[%d]\n",
				__func__, __LINE__, retval);
		return retval;
	}
/*
 * Set f12 control register address.
 * control register address : cntrol base register address + offset
 */
	rmi4_data->f12.ctrl11_addr = fhandler->full_addr.ctrl_base + ctrl_11_offset;
	rmi4_data->f12.ctrl15_addr = fhandler->full_addr.ctrl_base + ctrl_15_offset;
	rmi4_data->f12.ctrl23_addr = fhandler->full_addr.ctrl_base + ctrl_23_offset;
	rmi4_data->f12.ctrl26_addr = fhandler->full_addr.ctrl_base + ctrl_26_offset;
	rmi4_data->f12.ctrl28_addr = fhandler->full_addr.ctrl_base + ctrl_28_offset;
#ifdef GLOVE_MODE
	rmi4_data->f12.glove_mode_feature = query_10.glove_mode_feature;
#endif
	rmi4_data->f12.report_enable = RPT_DEFAULT;
//	rmi4_data->f12.obj_report_enable = OBJ_TYPE_DEFAULT; /* depend on models */
	rmi4_data->f12.report_enable |= (RPT_WX | RPT_WY);
	rmi4_data->f12.report_enable |= RPT_Z;

	retval = synaptics_rmi4_f12_set_init(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to int F12. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_8_offset,
			ctrl_8.data,
			sizeof(ctrl_8.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* Maximum x and y */
	rmi4_data->sensor_max_x =
		((unsigned short)ctrl_8.max_x_coord_lsb << 0) |
		((unsigned short)ctrl_8.max_x_coord_msb << 8);
	rmi4_data->sensor_max_y =
		((unsigned short)ctrl_8.max_y_coord_lsb << 0) |
		((unsigned short)ctrl_8.max_y_coord_msb << 8);
	if (rmi4_data->sensor_max_x <= 0 || rmi4_data->sensor_max_y <= 0) {
		rmi4_data->sensor_max_x = rmi4_data->board->sensor_max_x;
		rmi4_data->sensor_max_y = rmi4_data->board->sensor_max_y;
	}

	if (ctrl_8.num_of_rx)
		rmi4_data->num_of_rx = ctrl_8.num_of_rx;
	if (ctrl_8.num_of_tx)
		rmi4_data->num_of_tx = ctrl_8.num_of_tx;

	if (rmi4_data->num_of_rx != rmi4_data->board->num_of_rx
		|| rmi4_data->num_of_tx != rmi4_data->board->num_of_tx)
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Num of rx/tx is not matched. IC(%d,%d) != DT(%d,%d)\n",
			__func__, rmi4_data->num_of_rx, rmi4_data->num_of_tx,
			rmi4_data->board->num_of_rx, rmi4_data->board->num_of_tx);

	rmi4_data->max_touch_width = max(rmi4_data->num_of_rx,
			rmi4_data->num_of_tx);
	rmi4_data->num_of_node = ctrl_8.num_of_rx * ctrl_8.num_of_tx;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_9_offset,
			ctrl_9.data,
			sizeof(ctrl_9.data));

	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	input_info(true, &rmi4_data->i2c_client->dev,
			"%s: %02x max_x,y(%d,%d) num_Rx,Tx(%d,%d), num_finger(%d),  node:%d, threshold:%d, gloved sensitivity:%x\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x, rmi4_data->sensor_max_y,
			rmi4_data->num_of_rx, rmi4_data->num_of_tx,
			rmi4_data->num_of_fingers,
			rmi4_data->num_of_node, ctrl_9.touch_threshold,
			ctrl_9.gloved_finger);

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num >= MAX_INTR_REGISTERS) {
		fhandler->intr_reg_num = 0;
		fhandler->num_of_data_sources = 0;
		fhandler->intr_mask = 0;
		return -EINVAL;
	}
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset; ii < ((fd->intr_src_count & MASK_3BIT) +	intr_offset); ii++)
		fhandler->intr_mask |= 1 << ii;

	/* Allocate memory for finger data storage space */
	fhandler->data_size = rmi4_data->num_of_fingers * size_of_2d_data;
	fhandler->data = kzalloc(fhandler->data_size, GFP_KERNEL);

	return retval;
}

static int synaptics_rmi4_f1a_alloc_mem(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f1a_handle *f1a;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	f1a = kzalloc(sizeof(*f1a), GFP_KERNEL);
	if (!f1a) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for function handle\n",
				__func__);
		return -ENOMEM;
	}

	fhandler->data = (void *)f1a;
	fhandler->extra = NULL;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			f1a->button_query.data,
			sizeof(f1a->button_query.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read query registers\n",
				__func__);
		return retval;
	}

	f1a->max_count = f1a->button_query.max_button_count + 1;

	f1a->button_bitmask_size = (f1a->max_count + 7) / 8;

	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
			sizeof(*(f1a->button_data_buffer)), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for data buffer\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->max_count,
			sizeof(*(f1a->button_map)), GFP_KERNEL);
	if (!f1a->button_map) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for button map\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int synaptics_rmi4_f1a_button_map(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	f1a = fhandler->data;

	if (!pdata->f1a_button_map) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f1a_button_map is NULL in board file\n",
				__func__);
		return -ENODEV;
	} else if (!pdata->f1a_button_map->map) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Button map is missing in board file\n",
				__func__);
		return -ENODEV;
	} else {
		if (pdata->f1a_button_map->nbuttons != f1a->max_count) {
			f1a->valid_button_count = min(f1a->max_count,
					pdata->f1a_button_map->nbuttons);
		} else {
			f1a->valid_button_count = f1a->max_count;
		}

		for (ii = 0; ii < f1a->valid_button_count; ii++)
			f1a->button_map[ii] = pdata->f1a_button_map->map[ii];
	}

	return 0;
}

static void synaptics_rmi4_f1a_kfree(struct synaptics_rmi4_fn *fhandler)
{
	struct synaptics_rmi4_f1a_handle *f1a;

	if (!fhandler) {
		pr_err("%s: fhandler is not exist\n", __func__);
		return;
	}

	f1a = fhandler->data;

	if (f1a) {
		kfree(f1a->button_data_buffer);
		kfree(f1a->button_map);
		kfree(f1a);
		fhandler->data = NULL;
	}

	return;
}

static int synaptics_rmi4_f1a_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num >= MAX_INTR_REGISTERS) {
		fhandler->intr_reg_num = 0;
		fhandler->num_of_data_sources = 0;
		fhandler->intr_mask = 0;
		retval = -EINVAL;
		goto error_exit;
	}
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset; ii < ((fd->intr_src_count & MASK_3BIT) +	intr_offset); ii++)
		fhandler->intr_mask |= 1 << ii;

	retval = synaptics_rmi4_f1a_alloc_mem(rmi4_data, fhandler);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Fail to alloc mem. error = %d\n",
			__func__, retval);
		goto error_exit;
	}
	retval = synaptics_rmi4_f1a_button_map(rmi4_data, fhandler);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to make button map. error = %d\n",
			__func__, __LINE__, retval);
		goto error_exit;
	}
	rmi4_data->button_0d_enabled = 1;

	return 0;

error_exit:
	synaptics_rmi4_f1a_kfree(fhandler);

	return retval;
}

int synaptics_rmi4_read_tsp_test_result(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char data;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f34_ctrl_base_addr,
			&data,
			sizeof(data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: read fail[%d]\n",
				__func__, retval);
		return retval;
	}

	return data >> 4;
}
EXPORT_SYMBOL(synaptics_rmi4_read_tsp_test_result);

static int synaptics_rmi4_f34_read_version(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f34_ctrl_0 custom_config_id;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	/* Read bootloader version */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			rmi4_data->bootloader_id,
			sizeof(rmi4_data->bootloader_id));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	rmi4_data->f34_ctrl_base_addr = fhandler->full_addr.ctrl_base;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			custom_config_id.data,
			sizeof(custom_config_id.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* custom_config_id.fw_release_month is composed
	 * MSB 4bit are used for factory test in TSP. and LSB 4bit represent month of fw release
	 */
	rmi4_data->fw_release_date_of_ic =
		((custom_config_id.fw_release_month << 8) | custom_config_id.fw_release_date);
	rmi4_data->ic_revision_of_ic = custom_config_id.fw_release_revision;
	rmi4_data->fw_version_of_ic = custom_config_id.fw_release_version;

	return retval;
}

/* hava to check it!(fw_release_month) */
static int synaptics_rmi4_f34_read_version_v7(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f34_ctrl_0 custom_config_id;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	/* Read bootloader version */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			rmi4_data->bootloader_revision,
			sizeof(rmi4_data->bootloader_revision));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	msleep(10);
	input_info(true, &rmi4_data->i2c_client->dev, "%s: Read Bootloader revision Major[%x]Minor[%x]Prop[%x]\n",
		__func__, rmi4_data->bootloader_revision[SYNAPTICS_BL_MAJOR_REV_OFFSET_V7]
		,rmi4_data->bootloader_revision[SYNAPTICS_BL_MINOR_REV_OFFSET_V7]
		,rmi4_data->bootloader_revision[SYNAPTICS_BL_PROPERTIES_V7]);

	rmi4_data->f34_ctrl_base_addr = fhandler->full_addr.ctrl_base;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			custom_config_id.data,
			sizeof(custom_config_id.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* custom_config_id.fw_release_month is composed
	 * MSB 4bit are used for factory test in TSP. and LSB 4bit represent month of fw release
	 */
	rmi4_data->fw_release_date_of_ic =
		((custom_config_id.fw_release_month << 8) | custom_config_id.fw_release_date);
	rmi4_data->ic_revision_of_ic = custom_config_id.fw_release_revision;
	rmi4_data->fw_version_of_ic = custom_config_id.fw_release_version;

	input_dbg(true, &rmi4_data->i2c_client->dev,
			"%s: fw_release_date_of_ic=[%X],ic_revision_of_ic[%X],fw_version_of_ic[%X]\n",
			__func__, rmi4_data->fw_release_date_of_ic
			,rmi4_data->ic_revision_of_ic
			,rmi4_data->fw_version_of_ic);

	return retval;
}

static int synaptics_rmi4_f34_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	if(rmi4_data->bootloader_version == BL_V7) {
		retval = synaptics_rmi4_f34_read_version_v7(rmi4_data, fhandler);
	} else {
		retval = synaptics_rmi4_f34_read_version(rmi4_data, fhandler);
	}

	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read version. error = %d\n",
			__func__, retval);
		return retval;
	}

	fhandler->data = NULL;

	return retval;
}

int synaptics_proximity_no_sleep_set(struct synaptics_rmi4_data *rmi4_data, bool enables)
{
	int retval;
	unsigned char no_sleep = 0;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&no_sleep,
			sizeof(no_sleep));

	if (retval <= 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s: fail to read no_sleep[ret:%d]\n",
				__func__, retval);

	if (enables)
		no_sleep |= NO_SLEEP_ON;
	else
		no_sleep &= ~(NO_SLEEP_ON);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&no_sleep,
			sizeof(no_sleep));
	if (retval <= 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s: fail to set no_sleep[%X][ret:%d]\n",
				__func__, no_sleep, retval);

	return retval;
}
EXPORT_SYMBOL(synaptics_proximity_no_sleep_set);

static int synaptics_rmi4_f51_set_proximity_enables(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	int retval;

	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return -ENXIO;
	}

	input_info(true, &rmi4_data->i2c_client->dev, "%s: [0x%02X], Hover is %s\n", __func__,
		f51->proximity_enables, (f51->proximity_enables & FINGER_HOVER_EN) ? "enabled" : "disabled");

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->proximity_enables_addr,
			&f51->proximity_enables,
			sizeof(f51->proximity_enables));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}

static int synaptics_rmi4_f51_set_init(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;

	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return -ENXIO;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data, f51->general_control_addr,
			&f51->general_control, sizeof(f51->general_control));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
			"%s, %4d: Failed to read (use default general control data) = %d\n",
			__func__, __LINE__, retval);
		f51->general_control = EDGE_SWIPE_EN;
	}

	/* Read default general_control_2 register */
	retval = synaptics_rmi4_i2c_read(rmi4_data, f51->general_control_2_addr,
			&f51->general_control_2, sizeof(f51->general_control_2));
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);

	input_info(true, &rmi4_data->i2c_client->dev, "%s: General control[0x%02X],2[0x%02X]\n",
			__func__, f51->general_control, f51->general_control_2);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			f51->general_control_addr,
			&f51->general_control,
			sizeof(f51->general_control));
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
				__func__, __LINE__, retval);

	retval = synaptics_rmi4_f51_set_proximity_enables(rmi4_data);
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set proximity enables\n", __func__);

	return retval;
}

static int synaptics_rmi4_f51_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;
	struct synaptics_rmi4_f51_data *data;
	struct synaptics_rmi4_f51_query query;
	struct synaptics_rmi4_f51_handle *f51;
	unsigned char data_addr_offset = 0;

	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return -ENXIO;
	}

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num >= MAX_INTR_REGISTERS) {
		fhandler->intr_reg_num = 0;
		fhandler->num_of_data_sources = 0;
		fhandler->intr_mask = 0;
		return -EINVAL;
	}
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) + intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	fhandler->data_size = sizeof(*data);
	data = kzalloc(fhandler->data_size, GFP_KERNEL);
	fhandler->data = (void *)data;
	fhandler->extra = NULL;

	rmi4_data->f51 = kzalloc(sizeof(*rmi4_data->f51), GFP_KERNEL);
	f51 = rmi4_data->f51;

//	f51->proximity_enables = AIR_SWIPE_EN | SLEEP_PROXIMITY;
	f51->proximity_enables_addr = fhandler->full_addr.ctrl_base +
		F51_PROXIMITY_ENABLES_OFFSET;
	f51->general_control_addr = fhandler->full_addr.ctrl_base +
		F51_GENERAL_CONTROL_OFFSET;
	f51->general_control_2_addr = fhandler->full_addr.ctrl_base +
		F51_GENERAL_CONTROL_2_OFFSET;
	f51->grip_edge_exclusion_rx_addr = fhandler->full_addr.ctrl_base +
		MANUAL_DEFINED_OFFSET_GRIP_EDGE_EXCLUSION_RX;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query.data,
			sizeof(query.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	/* query proximity controls to get which functions
	 * are supported by firmware and save start address
	 * of each feature's data.
	 */
	f51->proximity_controls = query.proximity_controls;
	f51->proximity_controls_2 = query.proximity_controls_2;

	f51->edge_swipe_data_addr = fhandler->full_addr.data_base + data_addr_offset;

	retval = synaptics_rmi4_f51_set_init(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to set proximity set init\n",
			__func__);
		return retval;
	}

	return 0;
}

int synaptics_rmi4_proximity_enables(struct synaptics_rmi4_data *rmi4_data,
	unsigned char enables)
{
	struct synaptics_rmi4_f51_handle *f51 = rmi4_data->f51;
	int retval;

	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return -ENXIO;
	}

	if (enables) {
		f51->proximity_enables |= FINGER_HOVER_EN;
		f51->proximity_enables &= ~(SLEEP_PROXIMITY);
	} else {
		f51->proximity_enables |= SLEEP_PROXIMITY;
		f51->proximity_enables &= ~(FINGER_HOVER_EN);
	}

	if (rmi4_data->touch_stopped)
		return 0;

	retval = synaptics_rmi4_f51_set_proximity_enables(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}
	return 0;
}
EXPORT_SYMBOL(synaptics_rmi4_proximity_enables);

static int synaptics_rmi4_check_status(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int timeout = CHECK_STATUS_TIMEOUT_MS;
	unsigned char intr_status;
	struct synaptics_rmi4_f01_device_status status;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			status.data,
			sizeof(status.data));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s:%4d read fail[%d]\n",
				__func__, __LINE__, retval);
		return retval;
	}
	input_info(true, &rmi4_data->i2c_client->dev, "%s: Device status[0x%02x] status.code[%d]\n",
			__func__, status.data[0], status.status_code);

	while (status.status_code == STATUS_CRC_IN_PROGRESS) {
		if (timeout > 0)
			msleep(20);
		else
			return -1;

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr,
				status.data,
				sizeof(status.data));
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
				__func__, __LINE__, retval);
			return retval;
		}
		timeout -= 20;
	}

	if (status.flash_prog == 1) {
		rmi4_data->flash_prog_mode = true;
		input_info(true, &rmi4_data->i2c_client->dev, "%s: In flash prog mode, status = 0x%02x\n",
				__func__, status.status_code);
	} else {
		rmi4_data->flash_prog_mode = false;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			&intr_status,
			sizeof(intr_status));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return retval;
	}

	return 0;
}

static void synaptics_rmi4_mode_reinit(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char boost_up_en = 0;
	unsigned char old_boost_up_en = 0;

	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return;
	}

	retval = rmi4_data->i2c_read(rmi4_data,
			rmi4_data->f51->general_control_addr, &boost_up_en, sizeof(boost_up_en));
    
	if (retval < 0) {
		input_err(true, &(rmi4_data->i2c_client->dev),
			"%s: Failed to synaptics_rmi4_set_boost_up\n", __func__);
	}
	old_boost_up_en = boost_up_en;

	/* 0: Default	, 1: Enable Boost Up 1phi */
	if (rmi4_data->use_brush)
		boost_up_en |= BOOST_UP_EN;
	else
		boost_up_en &= ~(BOOST_UP_EN);

	if (old_boost_up_en != boost_up_en) {
		retval = rmi4_data->i2c_write(rmi4_data,
			rmi4_data->f51->general_control_addr,
			&boost_up_en, sizeof(boost_up_en));

		if (retval < 0) {
			input_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to synaptics_rmi4_set_boost_up\n",__func__);
		}
	}

	input_info(true, &rmi4_data->i2c_client->dev,
		"%s : After_LockScreen_reinit[%x][%x]\n",
		__func__, old_boost_up_en, boost_up_en);
    
	return;
}

static void synaptics_rmi4_set_configured(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		input_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to set configured\n",
				__func__);
		return;
	}

	device_ctrl |= CONFIGURED;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		input_err(true, &(rmi4_data->i2c_client->dev),
				"%s: Failed to set configured\n",
				__func__);
	}

	return;
}

static int synaptics_rmi4_alloc_fh(struct synaptics_rmi4_fn **fhandler,
		struct synaptics_rmi4_fn_desc *rmi_fd, int page_number)
{
	*fhandler = kzalloc(sizeof(**fhandler), GFP_KERNEL);
	if (!(*fhandler))
		return -ENOMEM;

	(*fhandler)->full_addr.data_base =
		(rmi_fd->data_base_addr |
		 (page_number << 8));
	(*fhandler)->full_addr.ctrl_base =
		(rmi_fd->ctrl_base_addr |
		 (page_number << 8));
	(*fhandler)->full_addr.cmd_base =
		(rmi_fd->cmd_base_addr |
		 (page_number << 8));
	(*fhandler)->full_addr.query_base =
		(rmi_fd->query_base_addr |
		 (page_number << 8));

	return 0;
}

static void synaptics_rmi4_free_fh(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	if (!fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: fhandler is not exist\n", __func__);
		return;
	}
	if (!rmi4_data->f51) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: f51 is not exist\n", __func__);
		return;
	}

	if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
		synaptics_rmi4_f1a_kfree(fhandler);
	} else {
		if (fhandler->fn_number == SYNAPTICS_RMI4_F51) {
			kfree(rmi4_data->f51);
			rmi4_data->f51 = NULL;
		}
		kfree(fhandler->data);
		fhandler->data = NULL;
	}
	
	kfree(fhandler);
	fhandler = NULL;
}
static void	synaptics_init_product_info_v7(struct synaptics_rmi4_data *rmi4_data)
{

	/* Get production info for Bootloader Version 7. */
	if (strncmp(rmi4_data->rmi4_mod_info.product_id_string, "s5807", 5) == 0) {
		rmi4_data->product_id = SYNAPTICS_PRODUCT_ID_S5807;
	} else if (strncmp(rmi4_data->rmi4_mod_info.product_id_string, "s5806", 5) == 0) {
		rmi4_data->product_id = SYNAPTICS_PRODUCT_ID_S5806;
	} else {
		rmi4_data->product_id = SYNAPTICS_PRODUCT_ID_NONE;
		input_err(true, &rmi4_data->i2c_client->dev, "%s, Undefined product id: %s\n",
			__func__, rmi4_data->rmi4_mod_info.product_id_string);
	}

}

/**
 * synaptics_rmi4_query_device()
 *
 * Called by synaptics_rmi4_probe().
 *
 * This funtion scans the page description table, records the offsets
 * to the register types of Function $01, sets up the function handlers
 * for Function $11 and Function $12, determines the number of interrupt
 * sources from the sensor, adds valid Functions with data inputs to the
 * Function linked list, parses information from the query registers of
 * Function $01, and enables the interrupt sources from the valid Functions
 * with data inputs.
 */
static int synaptics_rmi4_query_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii = 0;
	unsigned char page_number;
	unsigned char intr_count = 0;
	unsigned char f01_query[F01_STD_QUERY_LEN];
	unsigned char f01_pr_number[4];
	unsigned short pdt_entry_addr;
	unsigned short intr_addr;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	INIT_LIST_HEAD(&rmi->support_fn_list);

	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev,
						"%s, %4d: Failed to read. error = %d\n",
						__func__, __LINE__, retval);
				return retval;
			}
			fhandler = NULL;

			if (rmi_fd.fn_number == 0) {
				input_dbg(false, &rmi4_data->i2c_client->dev,
						"%s: Reached end of PDT\n",
						__func__);
				break;
			}

			/* Display function description infomation */
			input_info(false, &rmi4_data->i2c_client->dev,
					"%s: F%02x found (page %d): INT_SRC[%02x]"
					" BASE_ADDRS[%02X,%02X,%02X,%02x]\n",
					__func__, rmi_fd.fn_number, page_number,
					rmi_fd.intr_src_count & MASK_3BIT,
					rmi_fd.data_base_addr, rmi_fd.ctrl_base_addr,
					rmi_fd.cmd_base_addr, rmi_fd.query_base_addr);

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_query_base_addr =
					rmi_fd.query_base_addr;
				rmi4_data->f01_ctrl_base_addr =
					rmi_fd.ctrl_base_addr;
				rmi4_data->f01_data_base_addr =
					rmi_fd.data_base_addr;
				rmi4_data->f01_cmd_base_addr =
					rmi_fd.cmd_base_addr;

				retval = synaptics_rmi4_check_status(rmi4_data);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to check status\n",
							__func__);
					return retval;
				}
				if (rmi4_data->flash_prog_mode)
					goto flash_prog_mode;
				break;
			case SYNAPTICS_RMI4_F11:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				retval = synaptics_rmi4_f11_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					synaptics_rmi4_free_fh(rmi4_data, fhandler);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F12:
				if (rmi_fd.intr_src_count == 0)
					break;
				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				retval = synaptics_rmi4_f12_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					synaptics_rmi4_free_fh(rmi4_data, fhandler);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F1A:
				if (rmi_fd.intr_src_count == 0)
					break;
				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				retval = synaptics_rmi4_f1a_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					synaptics_rmi4_free_fh(rmi4_data, fhandler);
					return retval;
				}
				break;
			case SYNAPTICS_RMI4_F34:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				if (rmi_fd.fn_version == 2) {
					rmi4_data->bootloader_version = BL_V7;
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: F34 version : Bootloader Ver7.0\n",
							__func__);
				} else {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Unrecognized F34 version[%d]\n", __func__, rmi_fd.fn_version);
				}

				retval = synaptics_rmi4_f34_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					synaptics_rmi4_free_fh(rmi4_data, fhandler);
					return retval;
				}
				break;

			case SYNAPTICS_RMI4_F51:
				if (rmi_fd.intr_src_count == 0)
					break;
				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				retval = synaptics_rmi4_f51_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
					input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to init for F%d\n",
						__func__, rmi_fd.fn_number);
					synaptics_rmi4_free_fh(rmi4_data, fhandler);
					return retval;
				}
				break;
			}

			/* Accumulate the interrupt count */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);

			if (fhandler && rmi_fd.intr_src_count) {
				list_add_tail(&fhandler->link,
						&rmi->support_fn_list);
			}
		}
	}

flash_prog_mode:
	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	input_info(true, &rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d sum of intr_count = %d\n",
			__func__, rmi4_data->num_of_intr_regs, intr_count);
	if (rmi4_data->num_of_intr_regs >= MAX_INTR_REGISTERS)
		return -EINVAL;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr,
			f01_query,
			sizeof(f01_query));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	/* RMI Version 4.0 currently supported */
	rmi->version_major = 4;
	rmi->version_minor = 0;

	rmi->manufacturer_id = f01_query[0];
	rmi->product_props = f01_query[1];
	rmi->product_info[0] = f01_query[2] & MASK_7BIT;
	rmi->product_info[1] = f01_query[3] & MASK_7BIT;
	rmi->date_code[0] = f01_query[4] & MASK_5BIT;
	rmi->date_code[1] = f01_query[5] & MASK_4BIT;
	rmi->date_code[2] = f01_query[6] & MASK_5BIT;
	rmi->tester_id = ((f01_query[7] & MASK_7BIT) << 8) |
		(f01_query[8] & MASK_7BIT);
	rmi->serial_number = ((f01_query[9] & MASK_7BIT) << 8) |
		(f01_query[10] & MASK_7BIT);
	memcpy(rmi->product_id_string, &f01_query[11], 10);

	synaptics_init_product_info_v7(rmi4_data);

	input_info(true, &rmi4_data->i2c_client->dev, "%s: Product ID [%s. %d]\n",
		__func__, rmi4_data->rmi4_mod_info.product_id_string, rmi4_data->product_id);

	/* F01_RMI_QUERY18 ,Product ID Query 7~9 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + 18,
			f01_pr_number,
			sizeof(f01_pr_number));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s:%4d read fail[%d]\n",
					__func__, __LINE__, retval);
		return retval;
	}
	memcpy(&rmi->pr_number, f01_pr_number, 3);

	input_info(true, &rmi4_data->i2c_client->dev,
		"%s: Result[%d],date[%02d/%02d], revision[0x%02X], version[0x%02X], pr_num[PR%X]\n",
		__func__, rmi4_data->fw_release_date_of_ic >> 12,
		(rmi4_data->fw_release_date_of_ic >> 8) & 0x0F,
		rmi4_data->fw_release_date_of_ic & 0x00FF,
		rmi4_data->ic_revision_of_ic, rmi4_data->fw_version_of_ic,
		rmi4_data->rmi4_mod_info.pr_number);

	if (rmi->manufacturer_id != 1) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Non-Synaptics device found, manufacturer ID = %d\n",
				__func__, rmi->manufacturer_id);
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + F01_BUID_ID_OFFSET,
			rmi->build_id,
			sizeof(rmi->build_id));
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to read. error = %d\n",
			__func__, __LINE__, retval);
		return retval;
	}

	memset(rmi4_data->intr_mask, 0x00, sizeof(rmi4_data->intr_mask));

	/*
	 * Map out the interrupt bit masks for the interrupt sources
	 * from the registered function handlers.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				rmi4_data->intr_mask[fhandler->intr_reg_num] |=
					fhandler->intr_mask;
			}

			/* To display each fhandler data */
			input_dbg(false, &rmi4_data->i2c_client->dev, "%s: F%02x : NUM_SOURCE[VER:%02X, NUM:%02X] NUM_INT_REG[%02X] INT_MASK[%02X]\n",
				__func__, fhandler->fn_number,
				(fhandler->num_of_data_sources & 0xF0) >> 4, fhandler->num_of_data_sources & MASK_3BIT,
				fhandler->intr_reg_num, fhandler->intr_mask);
		}
	}

	/* Enable the interrupt sources */
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			input_info(true, &rmi4_data->i2c_client->dev, "%s: Interrupt enable[%d] = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				return retval;
			}
		}
	}

	synaptics_rmi4_set_configured(rmi4_data);

	return 0;
}

static void synaptics_rmi4_release_support_fn(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_fn *fhandler, *n;
	struct synaptics_rmi4_device_info *rmi;
	rmi = &(rmi4_data->rmi4_mod_info);

	if (list_empty(&rmi->support_fn_list)) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: support_fn_list is empty\n",
				__func__);
		return;
	}

	list_for_each_entry_safe(fhandler, n, &rmi->support_fn_list, link) {
		input_dbg(false, &rmi4_data->i2c_client->dev, "%s: fn_number = %x\n",
				__func__, fhandler->fn_number);

		synaptics_rmi4_free_fh(rmi4_data, fhandler);
	}
}

static int synaptics_rmi4_set_input_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to allocate input device\n",
				__func__);
		retval = -ENOMEM;
		goto err_input_device;
	}

	rmi4_data->input_dev->name = "sec_touchscreen";

	input_info(true, &rmi4_data->i2c_client->dev, "%s: device_name : %s\n",
			__func__, rmi4_data->input_dev->name);

	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->dev.parent = &rmi4_data->i2c_client->dev;
	rmi4_data->input_dev->open = synaptics_rmi4_input_open;
	rmi4_data->input_dev->close = synaptics_rmi4_input_close;

	input_set_drvdata(rmi4_data->input_dev, rmi4_data);
#ifdef GLOVE_MODE
	input_set_capability(rmi4_data->input_dev, EV_SW, SW_GLOVE);
#endif
	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);
	set_bit(BTN_TOUCH, rmi4_data->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, rmi4_data->input_dev->keybit);

	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_X, 0,
			rmi4_data->board->sensor_max_x - 1, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_Y, 0,
			rmi4_data->board->sensor_max_y - 1, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			EDGE_SWIPE_WIDTH_MAX, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MINOR, 0,
			EDGE_SWIPE_WIDTH_MAX, 0, 0);
#ifdef CONFIG_SEC_FACTORY
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_DISTANCE, 0,
			HOVER_Z_MAX, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_CUSTOM, 0,
			0xFFFF, 0, 0);
	setup_timer(&rmi4_data->f51_finger_timer,
			synaptics_rmi4_f51_finger_timer,
			(unsigned long)rmi4_data);

	retval = input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers, INPUT_MT_DIRECT);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to input_mt_init_slots - num_of_fingers:%d, retval:%d\n",
				__func__, rmi4_data->num_of_fingers, retval);
	}

	f1a = NULL;
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				f1a = fhandler->data;
		}
	}

	if (f1a) {
		for (ii = 0; ii < f1a->valid_button_count; ii++) {
			set_bit(f1a->button_map[ii],
					rmi4_data->input_dev->keybit);
			input_set_capability(rmi4_data->input_dev,
					EV_KEY, f1a->button_map[ii]);
		}
	}

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register input device\n",
				__func__);
		goto err_register_input;
	}

	return 0;

err_register_input:
	input_free_device(rmi4_data->input_dev);

err_input_device:
	return retval;
}

static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;
	unsigned char ii = 0;
	unsigned short intr_addr;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	mutex_lock(&(rmi4_data->rmi4_reset_mutex));

	if (list_empty(&rmi->support_fn_list)) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Support function list is empty!!\n",
				__func__);
		retval = -EINVAL;
		goto exit;
	}

	list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
		input_info(true, &rmi4_data->i2c_client->dev, "%s: F%X\n", __func__, fhandler->fn_number);
		if (fhandler->fn_number == SYNAPTICS_RMI4_F12) {
			retval = synaptics_rmi4_f12_set_init(rmi4_data);
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to initialize F12 = %d\n",
					__func__, retval);
				goto exit;
			}
		}
#ifdef CONFIG_SEC_FACTORY
		/* Read firmware version from IC when every power up IC.
		 * During Factory process touch panel can be changed manually.
		 */
		if (fhandler->fn_number == SYNAPTICS_RMI4_F34) {
			retval = synaptics_rmi4_f34_read_version(rmi4_data, fhandler);
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to read version error = %d\n",
					__func__, retval);
				goto exit;
			}
		}
#endif
		if (fhandler->fn_number == SYNAPTICS_RMI4_F51) {
			retval = synaptics_rmi4_f51_set_init(rmi4_data);
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to initialize F51 = %d\n",
					__func__, retval);
				goto exit;
			}
		}
	}

	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			input_info(true, &rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask register[%d] = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev, "%s, %4d: Failed to write. error = %d\n",
					__func__, __LINE__, retval);
				goto exit;
			}
		}
	}

	synaptics_rmi4_set_configured(rmi4_data);
    	synaptics_rmi4_mode_reinit(rmi4_data);

exit:
	mutex_unlock(&(rmi4_data->rmi4_reset_mutex));
	return retval;
}

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char command = 0x01;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);
	struct synaptics_rmi4_exp_fn *exp_fhandler = NULL;

	mutex_lock(&(rmi4_data->rmi4_reset_mutex));

	disable_irq(rmi4_data->i2c_client->irq);

	synpatics_rmi4_release_all_event(rmi4_data, RELEASE_TYPE_ALL);

	if (!rmi4_data->stay_awake) {
		retval = synaptics_rmi4_i2c_write(rmi4_data,
				rmi4_data->f01_cmd_base_addr,
				&command,
				sizeof(command));
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to issue reset command, error = %d\n",
					__func__, retval);
			goto out;
		}

		msleep(SYNAPTICS_HW_RESET_TIME);
	} else {
		rmi4_data->board->power(rmi4_data, false);
		msleep(30);
		rmi4_data->board->power(rmi4_data, true);
		msleep(SYNAPTICS_HW_RESET_TIME);
	}

	if (rmi4_data->doing_reflash || rmi4_data->do_query) {
		synaptics_rmi4_release_support_fn(rmi4_data);
		retval = synaptics_rmi4_query_device(rmi4_data);

		if (retval < 0)
			input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to query device\n",
					__func__);
		rmi4_data->do_query = false;
	}

	if (rmi4_data->doing_reflash || rmi4_data->stay_awake) {
		if (!list_empty(&rmi->exp_fn_list)) {
			list_for_each_entry(exp_fhandler, &rmi->exp_fn_list, link) {
				if (exp_fhandler->initialized && (exp_fhandler->func_reinit != NULL)) {
					exp_fhandler->func_reinit(rmi4_data);
					input_dbg(false, &rmi4_data->i2c_client->dev, "%s: %d exp fun re_init\n",
						__func__, exp_fhandler->fn_type);
				}
			}
		}
	}
out:
	enable_irq(rmi4_data->i2c_client->irq);
	mutex_unlock(&(rmi4_data->rmi4_reset_mutex));

	return 0;
}

static void synaptics_rmi4_f51_finger_timer(unsigned long data)
{
	struct synaptics_rmi4_data *rmi4_data =
		(struct synaptics_rmi4_data *)data;

	if (rmi4_data->f51_finger) {
		rmi4_data->f51_finger = false;
		mod_timer(&rmi4_data->f51_finger_timer,
				jiffies + msecs_to_jiffies(F51_FINGER_TIMEOUT));
		return;
	}

	if (!rmi4_data->fingers_on_2d) {
		input_mt_slot(rmi4_data->input_dev, 0);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 0);
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
		input_report_key(rmi4_data->input_dev,
				BTN_TOOL_FINGER, 0);
		input_sync(rmi4_data->input_dev);
	}

	return;
}

static void synaptics_rmi4_remove_exp_fn(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_exp_fn *exp_fhandler, *n;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	if (list_empty(&rmi->exp_fn_list)) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: exp_fn_list empty\n",
				__func__);
		return;
	}

	list_for_each_entry_safe(exp_fhandler, n, &rmi->exp_fn_list, link) {
		if (exp_fhandler->initialized &&
				(exp_fhandler->func_remove != NULL)) {
			input_dbg(false, &rmi4_data->i2c_client->dev, "%s: [%d]\n",
				__func__, exp_fhandler->fn_type);
			exp_fhandler->func_remove(rmi4_data);
		}
		list_del(&exp_fhandler->link);
		kfree(exp_fhandler);
	}
}

static int synaptics_rmi4_init_exp_fn(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	INIT_LIST_HEAD(&rmi->exp_fn_list);

	retval = rmidev_module_register(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register rmidev module\n",
				__func__);
		goto error_exit;
	}

	retval = rmi4_f54_module_register(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register f54 module\n",
				__func__);
		goto error_exit;
	}

	retval = rmi4_fw_update_module_register(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to register fw update module\n",
				__func__);
		goto error_exit;
	}

	if (list_empty(&rmi->exp_fn_list))
		return -ENODEV;

	list_for_each_entry(exp_fhandler, &rmi->exp_fn_list, link) {
		if (exp_fhandler->initialized) {
			input_info(true, &rmi4_data->i2c_client->dev, "%s: [%d] is already initialzied.\n",
					__func__, exp_fhandler->fn_type);
			continue;
		}
		if (exp_fhandler->func_init != NULL) {
			retval = exp_fhandler->func_init(rmi4_data);
			if (retval < 0) {
				input_err(true, &rmi4_data->i2c_client->dev,
						"%s: Failed to init exp [%d] fn\n",
						__func__, exp_fhandler->fn_type);
				goto error_exit;
			} else {
				exp_fhandler->initialized = true;
			}

			input_dbg(false, &rmi4_data->i2c_client->dev, "%s: run [%d]'s init function\n",
						__func__, exp_fhandler->fn_type);
		}
	}
	return 0;

error_exit:
	synaptics_rmi4_remove_exp_fn(rmi4_data);

	return retval;
}

/**
 * synaptics_rmi4_new_function()
 *
 * Called by other expansion Function modules in their module init and
 * module exit functions.
 *
 * This function is used by other expansion Function modules such as
 * rmi_dev to register themselves with the driver by providing their
 * initialization and removal callback function pointers so that they
 * can be inserted or removed dynamically at module init and exit times,
 * respectively.
 */
int synaptics_rmi4_new_function(enum exp_fn fn_type,
		struct synaptics_rmi4_data *rmi4_data,
		int (*func_init)(struct synaptics_rmi4_data *rmi4_data),
		int (*func_reinit)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_remove)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_attn)(struct synaptics_rmi4_data *rmi4_data, unsigned char intr_mask))
{
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	exp_fhandler = kzalloc(sizeof(*exp_fhandler), GFP_KERNEL);

	if (!exp_fhandler) {
		input_err(true, &rmi4_data->i2c_client->dev,
						"%s: Failed to alloc mem for expansion function\\n",
						__func__);
		return -ENOMEM;
	}
	exp_fhandler->fn_type = fn_type;
	exp_fhandler->func_init = func_init;
	exp_fhandler->func_reinit = func_reinit;
	exp_fhandler->func_attn = func_attn;
	exp_fhandler->func_remove = func_remove;
	list_add_tail(&exp_fhandler->link, &rmi->exp_fn_list);

	return 0;
}

#ifdef CONFIG_OF
static int synaptics_power_ctrl(void *data, bool on)
{
	struct synaptics_rmi4_data *rmi4_data = (struct synaptics_rmi4_data *)data;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;
	struct device *dev = &rmi4_data->i2c_client->dev;
	struct regulator *regulator_dvdd = NULL;
	struct regulator *regulator_avdd = NULL;
	struct pinctrl_state *pinctrl_state;
	int retval = 0;

	if (rmi4_data->tsp_pwr_enabled == on)
		return retval;

	regulator_dvdd = regulator_get(dev, "tsp_io");
	if (IS_ERR_OR_NULL(regulator_dvdd)) {
		input_err(true, dev, "%s: Failed to get %s regulator.\n",
				 __func__, pdata->regulator_dvdd);
		retval = PTR_ERR(regulator_dvdd);
		goto out;
	}

	regulator_avdd = regulator_get(dev, "tsp_avdd");
	if (IS_ERR_OR_NULL(regulator_avdd)) {
		input_err(true, dev, "%s: Failed to get %s regulator.\n",
				 __func__, pdata->regulator_avdd);
		retval = PTR_ERR(regulator_avdd);
		goto out;
	}

	input_info(true, dev, "%s: %s\n", __func__, on ? "on" : "off");

	if (on) {
		retval = regulator_enable(regulator_avdd);
		if (retval) {
			input_err(true, dev, "%s: Failed to enable avdd: %d\n", __func__, retval);
			goto out;
		}
		retval = regulator_enable(regulator_dvdd);
		if (retval) {
			input_err(true, dev, "%s: Failed to enable vdd: %d\n", __func__, retval);
			goto out;
		}

		pinctrl_state = pinctrl_lookup_state(rmi4_data->pinctrl, "on_state");
	} else {
		if (regulator_is_enabled(regulator_dvdd))
			regulator_disable(regulator_dvdd);
		if (regulator_is_enabled(regulator_avdd))
			regulator_disable(regulator_avdd);

		pinctrl_state = pinctrl_lookup_state(rmi4_data->pinctrl, "off_state");
	}

	if (IS_ERR(pinctrl_state)) {
		input_err(true, dev, "%s: Failed to lookup pinctrl.\n", __func__);
	} else {
		retval = pinctrl_select_state(rmi4_data->pinctrl, pinctrl_state);
		if (retval)
			input_err(true, dev, "%s: Failed to configure pinctrl.\n", __func__);
	}

	rmi4_data->tsp_pwr_enabled = on;
out:
	regulator_put(regulator_dvdd);
	regulator_put(regulator_avdd);

	return retval;
}

static int synaptics_parse_dt(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct synaptics_rmi4_platform_data *pdata = dev->platform_data;
	struct device_node *np = dev->of_node;
	u32 coords[2], lines[2];
	int retval = 0;

	pdata->gpio = of_get_named_gpio(np, "synaptics,irq_gpio", 0);
	if (gpio_is_valid(pdata->gpio)) {
		retval = gpio_request_one(pdata->gpio, GPIOF_DIR_IN, "synaptics,tsp_int");
		if (retval) {
			input_err(true, dev, "Unable to request tsp_int [%d]\n", pdata->gpio);
			return -EINVAL;
		}
	} else {
		input_err(true, dev, "Failed to get irq gpio\n");
		return -EINVAL;
	}
	client->irq = gpio_to_irq(pdata->gpio);

	if (of_property_read_u32(np, "synaptics,irq_type", &pdata->irq_type)) {
		input_err(true, dev, "Failed to get irq_type property\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "synaptics,max_coords", coords, 2)) {
		input_err(true, dev, "Failed to get max_coords property\n");
		return -EINVAL;
	}
	pdata->sensor_max_x = coords[0];
	pdata->sensor_max_y = coords[1];

	if (of_property_read_u32_array(np, "synaptics,num_lines", lines, 2)) {
		input_err(true, dev, "Failed to get num_liness property\n");
		return -EINVAL;
	}
	pdata->num_of_rx = lines[0];
	pdata->num_of_tx = lines[1];
	pdata->max_touch_width = max(pdata->num_of_rx, pdata->num_of_tx);

	pdata->x_flip = of_property_read_bool(np, "synaptics,x_flip");
	pdata->y_flip = of_property_read_bool(np, "synaptics,y_flip");
	pdata->x_y_chnage = of_property_read_bool(np, "synaptics,x_y_chnage");

	if (of_property_read_u32(np, "synaptics,x_offset", &pdata->x_offset)) {
		input_info(true, dev, "Failed to get x_offset property\n");
	}

	/* Optional parmeters(those values are not mandatory)
	 * do not return error value even if fail to get the value
	 */
	if (of_property_read_string(np, "synaptics,firmware_name", &pdata->firmware_name))
		input_err(true, dev, "Failed to get firmware_name property\n");
	if (of_property_read_string_index(np, "synaptics,project_name", 0, &pdata->project_name))
		input_err(true, dev, "Failed to get project_name property\n");
	if (of_property_read_u32(np, "synaptics,panel_revision", &pdata->panel_revision))
		input_err(true, dev, "Failed to get panel_revision property\n");
	if (of_property_read_string_index(np, "synaptics,project_name", 1, &pdata->model_name))
		input_err(true, dev, "Failed to get model_name property\n");

	pdata->swreset_on_start = of_property_read_bool(np, "synaptics,swreset_on_start");

	input_info(true, dev, "irq :%d, irq_type: 0x%04x, project/model_name: %s/%s, "
			"swreset_on_start:%d, fw name:%s\n",
			pdata->gpio, pdata->irq_type, pdata->project_name, pdata->model_name,
			pdata->swreset_on_start, pdata->firmware_name);

	input_info(true, dev, "num_of[rx,tx]: [%d,%d], max[x,y]: [%d,%d], max_width: %d, flip[x,y,x_y,x_offset]:[%d,%d,%d %d]",
			pdata->num_of_rx, pdata->num_of_tx, pdata->sensor_max_x, pdata->sensor_max_y,pdata->max_touch_width,
			pdata->x_flip, pdata->y_flip, pdata->x_y_chnage, pdata->x_offset);

	pdata->power = synaptics_power_ctrl;

	return retval;
}
#endif

static int synaptics_rmi4_setup_drv_data(struct i2c_client *client)
{
	int retval = 0;
	struct synaptics_rmi4_platform_data *pdata;
	struct synaptics_rmi4_data *rmi4_data;

	/* parse dt */
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct synaptics_rmi4_platform_data), GFP_KERNEL);

		if (!pdata) {
			input_err(true, &client->dev, "Failed to allocate platform data\n");
			return -ENOMEM;
		}

		client->dev.platform_data = pdata;
		retval = synaptics_parse_dt(client);
		if (retval) {
			input_err(true, &client->dev, "Failed to parse dt\n");
			return retval;
		}
	} else {
		pdata = client->dev.platform_data;
	}

	if (!pdata) {
		input_err(true, &client->dev, "No platform data found\n");
			return -EINVAL;
	}
	if (!pdata->power) {
		input_err(true, &client->dev, "No power contorl found\n");
			return -EINVAL;
	}

	rmi4_data = kzalloc(sizeof(struct synaptics_rmi4_data), GFP_KERNEL);
	if (!rmi4_data) {
		input_err(true, &client->dev,
				"%s: Failed to alloc mem for rmi4_data\n",
				__func__);
		return -ENOMEM;
	}

	rmi4_data->i2c_client = client;
	rmi4_data->board = pdata;
	rmi4_data->touch_stopped = false;
	rmi4_data->sensor_sleep = false;
	rmi4_data->irq_enabled = false;
	rmi4_data->tsp_probe = false;
	rmi4_data->rebootcount = 0;
	rmi4_data->panel_revision = rmi4_data->board->panel_revision;
	rmi4_data->i2c_read = synaptics_rmi4_i2c_read;
	rmi4_data->i2c_write = synaptics_rmi4_i2c_write;
	rmi4_data->irq_enable = synaptics_rmi4_irq_enable;
	rmi4_data->reset_device = synaptics_rmi4_reset_device;
	rmi4_data->stop_device = synaptics_rmi4_stop_device;
	rmi4_data->start_device = synaptics_rmi4_start_device;
	rmi4_data->irq = rmi4_data->i2c_client->irq;

	/* To prevent input device is set up with defective values */
	rmi4_data->sensor_max_x = rmi4_data->board->sensor_max_x;
	rmi4_data->sensor_max_y = rmi4_data->board->sensor_max_y;
	rmi4_data->num_of_rx = rmi4_data->board->num_of_rx;
	rmi4_data->num_of_tx = rmi4_data->board->num_of_tx;
	rmi4_data->max_touch_width = max(rmi4_data->num_of_rx,
			rmi4_data->num_of_tx);
	rmi4_data->num_of_fingers = MAX_NUMBER_OF_FINGERS;

	mutex_init(&(rmi4_data->rmi4_io_ctrl_mutex));
	mutex_init(&(rmi4_data->rmi4_reset_mutex));
	mutex_init(&(rmi4_data->rmi4_reflash_mutex));
	mutex_init(&(rmi4_data->rmi4_device_mutex));
	init_completion(&rmi4_data->init_done);

	i2c_set_clientdata(client, rmi4_data);

	if (pdata->get_ddi_type) {
		rmi4_data->ddi_type = pdata->get_ddi_type();
		input_info(true, &client->dev, "%s: DDI Type is %s[%d]\n",
			__func__, rmi4_data->ddi_type ? "MAGNA" : "SDC", rmi4_data->ddi_type);
	}

	return retval;
}

/**
 * synaptics_rmi4_probe()
 *
 * Called by the kernel when an association with an I2C device of the
 * same name is made (after doing i2c_add_driver).
 *
 * This funtion allocates and initializes the resources for the driver
 * as an input driver, turns on the power to the sensor, queries the
 * sensor for its supported Functions and characteristics, registers
 * the driver to the input subsystem, sets up the interrupt, handles
 * the registration of the early_suspend and late_resume functions,
 * and creates a work queue for detection of other expansion Function
 * modules.
 */
extern int get_lcd_attached(char *mode);
static int synaptics_rmi4_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval, lcd_attached;
	struct synaptics_rmi4_data *rmi4_data = NULL;

	printk(KERN_ERR "%s: synaptics probe++\n", __func__);

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		input_err(true, &client->dev,
				"%s: SMBus byte data not supported\n",
				__func__);
		return -EIO;
	}

	lcd_attached = get_lcd_attached("GET");
	if (lcd_attached == 0xFFFFFF) {
		input_err(true, &client->dev,
				"%s: LCD is not attached\n", __func__);
		return -EIO;
	}
	/* Build up driver data */
	retval = synaptics_rmi4_setup_drv_data(client);
	if (retval < 0) {
		input_err(true, &client->dev, "%s: Failed to set up driver data\n", __func__);
		goto err_setup_drv_data;
	}

	rmi4_data = (struct synaptics_rmi4_data *)i2c_get_clientdata(client);
	if (!rmi4_data) {
		input_err(true, &client->dev, "%s: Failed to get driver data\n", __func__);
		goto err_get_drv_data;
	}

	rmi4_data->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(rmi4_data->pinctrl)) {
		input_err(true, &client->dev, "%s: Failed to get pinctrl data\n", __func__);
		retval = PTR_ERR(rmi4_data->pinctrl);
		goto err_get_pinctrl;
	}

err_tsp_reboot:
	rmi4_data->board->power(rmi4_data, true);
	msleep(SYNAPTICS_POWER_MARGIN_TIME);

	if (rmi4_data->rebootcount)
		synaptics_rmi4_release_support_fn(rmi4_data);

	/* Query device infomations */
	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to query device\n", __func__);

		/* Protection code */
		if ((retval == TSP_NEEDTO_REBOOT) && (rmi4_data->rebootcount < MAX_TSP_REBOOT)) {
			rmi4_data->board->power(rmi4_data, false);
			msleep(SYNAPTICS_POWER_MARGIN_TIME);
			msleep(SYNAPTICS_POWER_MARGIN_TIME);
			rmi4_data->rebootcount++;
			synaptics_rmi4_release_support_fn(rmi4_data);

			input_err(true, &client->dev, "%s: reboot sequence by i2c fail\n", __func__);
			goto err_tsp_reboot;
		} else {
			goto err_query_device;
		}
	}

	/* Set up input device */
	retval = synaptics_rmi4_set_input_device(rmi4_data);
	if (retval < 0) {
		input_err(true, &client->dev, "%s: Failed to set up input device\n",
				__func__);
			goto err_set_input_device;
	}

	/* Set up expanded function list */
	retval = synaptics_rmi4_init_exp_fn(rmi4_data);
	if (retval < 0) {
		input_err(true, &client->dev, "%s: Failed to initialize expandered functions\n",
				__func__);
		goto err_init_exp_fn;
	}

	/* Enable attn pin */
	retval = synaptics_rmi4_irq_enable(rmi4_data, true);
	if (retval < 0) {
		input_err(true, &client->dev, "%s: Failed to enable attention interrupt\n",
				__func__);
		goto err_enable_irq;
	}

	/* Creat sysfs files */
	retval = sysfs_create_group(&rmi4_data->input_dev->dev.kobj, &attr_group);
	if (retval < 0) {
			input_err(true, &client->dev, "%s: Failed to create sysfs attributes\n",
					__func__);
			goto err_sysfs;
	}

	/* Update firmware on probe */
	retval = synaptics_rmi4_fw_update_on_probe(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s: Failed to firmware update\n",
					__func__);
		goto err_fw_update;
	}

	INIT_DELAYED_WORK(&rmi4_data->work_reset, reset_func);

	/* for blocking to be excuted open function until probing */
	rmi4_data->tsp_probe = true;

	complete_all(&rmi4_data->init_done);
	
	if (rmi4_data->save_input_open) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s, re input open, %d",
				__func__, rmi4_data->save_input_open);
		rmi4_data->input_dev->open(rmi4_data->input_dev);

	}

	return retval;

/*
	if (rmi4_data->resetting == true)
		cancel_delayed_work_sync(&rmi4_data->work_reset);
*/
err_fw_update:
err_sysfs:
	sysfs_remove_group(&rmi4_data->input_dev->dev.kobj, &attr_group);
	synaptics_rmi4_irq_enable(rmi4_data, false);

err_enable_irq:
	synaptics_rmi4_remove_exp_fn(rmi4_data);

err_init_exp_fn:
	input_unregister_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;

err_set_input_device:
err_query_device:
	synaptics_rmi4_release_support_fn(rmi4_data);
	rmi4_data->board->power(rmi4_data, false);
	complete_all(&rmi4_data->init_done);

err_get_pinctrl:
err_get_drv_data:
err_setup_drv_data:
	kfree(rmi4_data);

	return retval;
}

/**
 * synaptics_rmi4_remove()
 *
 * Called by the kernel when the association with an I2C device of the
 * same name is broken (when the driver is unloaded).
 *
 * This funtion terminates the work queue, stops sensor data acquisition,
 * frees the interrupt, unregisters the driver from the input subsystem,
 * turns off the power to the sensor, and frees other allocated resources.
 */
static int synaptics_rmi4_remove(struct i2c_client *client)
{
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);

	if(rmi4_data->resetting == true)
		cancel_delayed_work_sync(&rmi4_data->work_reset);
	synaptics_rmi4_irq_enable(rmi4_data, false);

	sysfs_remove_group(&rmi4_data->input_dev->dev.kobj, &attr_group);

	input_unregister_device(rmi4_data->input_dev);

	synaptics_rmi4_remove_exp_fn(rmi4_data);

	rmi4_data->board->power(rmi4_data, false);
	rmi4_data->touch_stopped = true;

	synaptics_rmi4_release_support_fn(rmi4_data);

	kfree(rmi4_data);

	return 0;
}

static void synaptics_rmi4_shutdown(struct i2c_client *client)
{
	synaptics_rmi4_remove(client);
}

static int synaptics_rmi4_stop_device(struct synaptics_rmi4_data *rmi4_data)
{
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	mutex_lock(&rmi4_data->rmi4_device_mutex);

	if (rmi4_data->touch_stopped) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s already power off\n",
			__func__);
		goto out;
	}

	disable_irq(rmi4_data->i2c_client->irq);

	synpatics_rmi4_release_all_event(rmi4_data, RELEASE_TYPE_ALL);

	rmi4_data->touch_stopped = true;
	pdata->power(rmi4_data, false);

	input_info(true, &rmi4_data->i2c_client->dev, "%s\n", __func__);

out:
	mutex_unlock(&rmi4_data->rmi4_device_mutex);
	return 0;
}

static int synaptics_rmi4_start_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval = 0;
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	mutex_lock(&rmi4_data->rmi4_device_mutex);

	if (!rmi4_data->touch_stopped) {
		input_err(true, &rmi4_data->i2c_client->dev, "%s already power on\n",
			__func__);
		goto out;
	}

	pdata->power(rmi4_data, true);
	rmi4_data->touch_stopped = false;

	if (rmi4_data->board->swreset_on_start) {
		unsigned char command = 0x01;

		msleep(SYNAPTICS_HW_RESET_TIME);
		input_info(true, &rmi4_data->i2c_client->dev, "%s: sw reset\n", __func__);
		retval = synaptics_rmi4_i2c_write(rmi4_data,
				rmi4_data->f01_cmd_base_addr, &command, sizeof(command));
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to issue reset command, error = %d\n",
					__func__, retval);
		}
		msleep(SYNAPTICS_HW_RESET_TIME);
		
		input_info(true, &rmi4_data->i2c_client->dev, "%s: force cal\n", __func__);
		command = 0x02;
		retval = rmi4_data->i2c_write(rmi4_data,
				rmi4_data->f54->command_base_addr,
				&command,
				sizeof(command));
		if (retval < 0) {
			input_err(true, &rmi4_data->i2c_client->dev,
					"%s: Failed to write force cal command\n",
					__func__);
		}
	} else {
		msleep(SYNAPTICS_HW_RESET_TIME);
	}

	retval = synaptics_rmi4_reinit_device(rmi4_data);
	if (retval < 0) {
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to reinit device\n",
				__func__);
	}
	enable_irq(rmi4_data->i2c_client->irq);

	input_info(true, &rmi4_data->i2c_client->dev, "%s\n", __func__);

out:
	mutex_unlock(&rmi4_data->rmi4_device_mutex);
	return retval;
}

static int synaptics_rmi4_input_open(struct input_dev *dev)
{
	struct synaptics_rmi4_data *rmi4_data = input_get_drvdata(dev);
	int retval;


	if (!rmi4_data->init_done.done) {
		input_err(true, &rmi4_data->i2c_client->dev,"%s, not done probe, %d", __func__, rmi4_data->init_done.done);
		rmi4_data->save_input_open = 1;
		return 0;
	}

	input_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);

	retval = synaptics_rmi4_start_device(rmi4_data);
	if (retval < 0)
		input_err(true, &rmi4_data->i2c_client->dev,
				"%s: Failed to start device\n", __func__);

	return 0;
}

static void synaptics_rmi4_input_close(struct input_dev *dev)
{
	struct synaptics_rmi4_data *rmi4_data = input_get_drvdata(dev);

	input_dbg(false, &rmi4_data->i2c_client->dev, "%s\n", __func__);
	synaptics_rmi4_stop_device(rmi4_data);
}

static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);


#ifdef CONFIG_OF
static struct of_device_id synaptics_rmi4_dt_ids[] = {
	{ .compatible = "synaptics,rmi4" },
	{ }
};
#endif

static struct i2c_driver synaptics_rmi4_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table	= of_match_ptr(synaptics_rmi4_dt_ids),
#endif
	},
	.probe = synaptics_rmi4_probe,
	.remove = synaptics_rmi4_remove,
	.shutdown = synaptics_rmi4_shutdown,
	.id_table = synaptics_rmi4_id_table,
};

/**
 * synaptics_rmi4_init()
 *
 * Called by the kernel during do_initcalls (if built-in)
 * or when the driver is loaded (if a module).
 *
 * This function registers the driver to the I2C subsystem.
 *
 */
#ifdef CONFIG_SAMSUNG_LPM_MODE
extern int poweroff_charging;
#endif
static int __init synaptics_rmi4_init(void)
{
	printk(KERN_ERR "%s: synaptics init++\n", __func__);
	
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (poweroff_charging) {
		pr_notice("%s : LPM Charging Mode!!\n", __func__);
		return 0;
	}
#endif

	return i2c_add_driver(&synaptics_rmi4_driver);
}

/**
 * synaptics_rmi4_exit()
 *
 * Called by the kernel when the driver is unloaded.
 *
 * This funtion unregisters the driver from the I2C subsystem.
 *
 */
static void __exit synaptics_rmi4_exit(void)
{
	i2c_del_driver(&synaptics_rmi4_driver);
}

module_init(synaptics_rmi4_init);
module_exit(synaptics_rmi4_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics RMI4 I2C Touch Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(SYNAPTICS_RMI4_DRIVER_VERSION);
