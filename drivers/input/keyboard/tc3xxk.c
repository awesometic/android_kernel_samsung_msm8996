/* tc3xxk.c -- Linux driver for coreriver chip as touchkey
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 * Author: Junkyeong Kim <jk0430.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#ifdef CONFIG_SEC_SYSFS
#include <linux/sec_class.h>
#else
extern struct class *sec_class;
#endif
#include <linux/regulator/consumer.h>

#ifdef CONFIG_TOUCHKEY_GRIP
#include <linux/wakelock.h>
#define FEATURE_GRIP_FOR_SAR
#endif

#ifdef CONFIG_VBUS_NOTIFIER
#include <linux/muic/muic.h>
#include <linux/muic/muic_notifier.h>
#include <linux/vbus_notifier.h>
#endif

/* registers */
#define TC3XXK_KEYCODE			0x00
#define TC3XXK_FWVER			0x01
#define TC3XXK_MDVER			0x02
#define TC3XXK_MODE			0x03
#define TC3XXK_CHECKS_H			0x04
#define TC3XXK_CHECKS_L			0x05
#define TC3XXK_THRES_H			0x06
#define TC3XXK_THRES_L			0x07
#define TC3XXK_1KEY_DATA		0x08
#define TC3XXK_2KEY_DATA		0x0E
#define TC3XXK_3KEY_DATA		0x14
#define TC3XXK_4KEY_DATA		0x1A
#define TC3XXK_5KEY_DATA		0x20
#define TC3XXK_6KEY_DATA		0x26

#define TC3XXK_CH_PCK_H_OFFSET		0x00
#define TC3XXK_CH_PCK_L_OFFSET		0x01
#define TC3XXK_DIFF_H_OFFSET		0x02
#define TC3XXK_DIFF_L_OFFSET		0x03
#define TC3XXK_RAW_H_OFFSET		0x04
#define TC3XXK_RAW_L_OFFSET		0x05

/* registers for tc350k */
#define TC350K_1KEY			0x10 // recent inner
#define TC350K_2KEY			0x18 // back inner
#define TC350K_3KEY			0x20 // recent outer
#define TC350K_4KEY			0x28 // back outer

#ifdef FEATURE_GRIP_FOR_SAR
/* registers for grip sensor */
#define TC305K_GRIP_THD_PRESS		0x30
#define TC305K_GRIP_THD_RELEASE		0x32
#define TC305K_GRIP_THD_NOISE		0x34
#define TC305K_GRIP_CH_PERCENT		0x36
#define TC305K_GRIP_DIFF_DATA		0x38
#define TC305K_GRIP_RAW_DATA		0x3A
#define TC305K_GRIP_BASELINE		0x3C
#define TC305K_GRIP_TOTAL_CAP		0x3E
#endif

#define TC350K_THRES_DATA_OFFSET	0x00
#define TC350K_CH_PER_DATA_OFFSET	0x02
#define TC350K_CH_DIFF_DATA_OFFSET	0x04
#define TC350K_CH_RAW_DATA_OFFSET	0x06

#define TC350K_DATA_SIZE		0x02
#define TC350K_DATA_H_OFFSET		0x00
#define TC350K_DATA_L_OFFSET		0x01

/* command */
#define TC3XXK_CMD_ADDR			0x00
#define TC3XXK_CMD_LED_ON		0x10
#define TC3XXK_CMD_LED_OFF		0x20
#define TC3XXK_CMD_GLOVE_ON		0x30
#define TC3XXK_CMD_GLOVE_OFF		0x40
#define TC3XXK_CMD_TA_ON		0x50
#define TC3XXK_CMD_TA_OFF		0x60
#define TC3XXK_CMD_CAL_CHECKSUM		0x70
#define TC3XXK_CMD_KEY_THD_ADJUST	0x80
#define TC3XXK_CMD_ALLKEY_THD_ADJUST	0x8F
#define TC3XXK_CMD_STOP_MODE		0x90
#define TC3XXK_CMD_NORMAL_MODE		0x91
#define TC3XXK_CMD_SAR_DISABLE		0xA0
#define TC3XXK_CMD_SAR_ENABLE		0xA1
#define TC3XXK_CMD_GRIP_SENSING_OFF	0xA2
#define TC3XXK_CMD_GRIP_SENSING_ON	0xA3
#define TC3XXK_CMD_FLIP_OFF		0xB0
#define TC3XXK_CMD_FLIP_ON		0xB1
#define TC3XXK_CMD_GRIP_BASELINE_CAL	0xC0
#define TC3XXK_CMD_WAKE_UP		0xF0

#define TC3XXK_CMD_DELAY		50

/* mode status bit */
#define TC3XXK_MODE_TA_CONNECTED	(1 << 0)
#define TC3XXK_MODE_RUN			(1 << 1)
#define TC3XXK_MODE_SAR			(1 << 2)
#define TC3XXK_MODE_GRIP_SENSING	(1 << 3)
#define TC3XXK_MODE_GLOVE		(1 << 4)

/* connecter check */
#define SUB_DET_DISABLE			0
#define SUB_DET_ENABLE_CON_OFF		1
#define SUB_DET_ENABLE_CON_ON		2

/* firmware */
#define TC3XXK_FW_PATH_SDCARD	"/sdcard/Firmware/TOUCHKEY/tc3xxk.bin"

#define TK_UPDATE_PASS			0
#define TK_UPDATE_DOWN			1
#define TK_UPDATE_FAIL			2

/* ISP command */
#define TC3XXK_CSYNC1			0xA3
#define TC3XXK_CSYNC2			0xAC
#define TC3XXK_CSYNC3			0xA5
#define TC3XXK_CCFG			0x92
#define TC3XXK_PRDATA			0x81
#define TC3XXK_PEDATA			0x82
#define TC3XXK_PWDATA			0x83
#define TC3XXK_PECHIP			0x8A
#define TC3XXK_PEDISC			0xB0
#define TC3XXK_LDDATA			0xB2
#define TC3XXK_LDMODE			0xB8
#define TC3XXK_RDDATA			0xB9
#define TC3XXK_PCRST			0xB4
#define TC3XXK_PCRED			0xB5
#define TC3XXK_PCINC			0xB6
#define TC3XXK_RDPCH			0xBD

/* ISP delay */
#define TC3XXK_TSYNC1			300	/* us */
#define TC3XXK_TSYNC2			50	/* 1ms~50ms */
#define TC3XXK_TSYNC3			100	/* us */
#define TC3XXK_TDLY1			1	/* us */
#define TC3XXK_TDLY2			2	/* us */
#define TC3XXK_TFERASE			10	/* ms */
#define TC3XXK_TPROG			20	/* us */

#define TC3XXK_CHECKSUM_DELAY		500
#define TC3XXK_POWERON_DELAY		450
#define TC3XXK_POWEROFF_DELAY		50

enum {
	FW_INKERNEL,
	FW_SDCARD,
};

struct fw_image {
	u8 hdr_ver;
	u8 hdr_len;
	u16 first_fw_ver;
	u16 second_fw_ver;
	u16 third_ver;
	u32 fw_len;
	u16 checksum;
	u16 alignment_dummy;
	u8 data[0];
} __attribute__ ((packed));

#define	TSK_RELEASE	0x00
#define	TSK_PRESS	0x01

struct tsk_event_val {
	u16	tsk_bitmap;
	u8	tsk_status;
	int	tsk_keycode;
	char*	tsk_keyname;
};

#ifdef FEATURE_GRIP_FOR_SAR
struct tsk_event_val tsk_ev[6] =
{
	{0x01 << 0, TSK_PRESS, KEY_RECENT, "recent"},
	{0x01 << 1, TSK_PRESS, KEY_BACK, "back"},
	{0x01 << 2, TSK_PRESS, KEY_CP_GRIP, "grip"},
	{0x01 << 4, TSK_RELEASE, KEY_RECENT, "recent"},
	{0x01 << 5, TSK_RELEASE, KEY_BACK, "back"},
	{0x01 << 6, TSK_RELEASE, KEY_CP_GRIP, "grip"}
};
#else
struct tsk_event_val tsk_ev[4] =
{
	{0x01 << 0, TSK_PRESS, KEY_RECENT, "recent"},
	{0x01 << 1, TSK_PRESS, KEY_BACK, "back"},
	{0x01 << 4, TSK_RELEASE, KEY_RECENT, "recent"},
	{0x01 << 5, TSK_RELEASE, KEY_BACK, "back"},
};
#endif


#define TC3XXK_NAME "tc3xxk"

struct tc3xxk_devicetree_data {
	int irq_gpio;
	int sda_gpio;
	int scl_gpio;
	int sub_det_gpio;
	int power_gpio;
	int led_gpio;
	int i2c_gpio;
	bool boot_on_pwr;
	u32 bringup;
	bool use_tkey_led;

	int *keycode;
	int key_num;
	const char *fw_name;
	u32 sensing_ch_num;
	u32 tc300k;
};

struct tc3xxk_data {
	struct device *sec_touchkey;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct tc3xxk_devicetree_data *dtdata;
	struct mutex lock;
	struct mutex lock_fac;
	struct fw_image *fw_img;
	const struct firmware *fw;
	struct regulator *pwr_reg;
	struct pinctrl *pinctrl;
	char phys[32];
	u16 checksum;
	u16 threhold;
	int mode;
	u8 fw_ver;
	u8 fw_ver_bin;
	u8 md_ver;
	u8 md_ver_bin;
	u8 fw_update_status;
	bool enabled;
	bool fw_downloding;
	bool glove_mode;
	bool led_on;

	int key_num;
	struct tsk_event_val *tsk_ev_val;
	int (*power) (struct tc3xxk_data *data, bool on);

#ifdef FEATURE_GRIP_FOR_SAR
	struct wake_lock touchkey_wake_lock;
	u16 grip_p_thd;
	u16 grip_r_thd;
	u16 grip_n_thd;
	u16 grip_s1;
	u16 grip_s2;
	u16 grip_baseline;
	u16 grip_raw1;
	u16 grip_raw2;
	u16 grip_event;
	bool sar_mode;
	bool sar_enable;
	bool sar_enable_off;
	bool grip_sensing;
#ifdef CONFIG_PM
	struct completion resume_done;
	bool is_lpm_suspend;
#endif
#else
	struct work_struct start_work;
	bool grip_sensing_status;
#endif
#ifdef CONFIG_VBUS_NOTIFIER
	struct notifier_block vbus_nb;
#endif
#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	bool flip_mode;
#endif
	u8 vbus_state;
};

#ifdef CONFIG_SAMSUNG_LPM_MODE
extern int poweroff_charging;
#endif

static void tc3xxk_input_close(struct input_dev *dev);
static int tc3xxk_input_open(struct input_dev *dev);
static int read_tc3xxk_register_data(struct tc3xxk_data *data, int read_key_num, int read_offset);
#ifdef FEATURE_GRIP_FOR_SAR
static void tc3xxk_reset(struct tc3xxk_data *data);
#else
static void tc3xxk_start_work(struct work_struct *work);
#endif

static int tc3xxk_touchkey_power(struct tc3xxk_data *data, bool on)
{
	struct tc3xxk_devicetree_data *dtdata = data->dtdata;
	struct device *dev = &data->client->dev;
	int ret =  0;

	if (data->pwr_reg) {
		if (on) {
			ret = regulator_enable(data->pwr_reg);
			if (ret) {
				input_err(true, dev, "%s: Failed to enable\n", __func__);
				return ret;
			}
		} else {
			if (regulator_is_enabled(data->pwr_reg)) {
				ret = regulator_disable(data->pwr_reg);
				if (ret) {
					input_err(true, dev, "%s: Failed to disable\n", __func__);
					return ret;
				}
			} else {
				regulator_force_disable(data->pwr_reg);
			}
		}
	} else if (gpio_is_valid(dtdata->power_gpio)) {
		gpio_set_value(dtdata->power_gpio, on);
	}

	input_info(true, dev, "%s: %s", __func__, on ? "on" : "off");

	return ret;
}

static void tc3xxk_touchkey_led_power(struct tc3xxk_data *data, bool on)
{
	struct tc3xxk_devicetree_data *dtdata = data->dtdata;

	if (dtdata->use_tkey_led && gpio_is_valid(dtdata->led_gpio)) {
		gpio_direction_output(dtdata->led_gpio, on);
	}
}

static int tc3xxk_mode_enable(struct i2c_client *client, u8 cmd)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, TC3XXK_CMD_ADDR, cmd);
	msleep(TC3XXK_CMD_DELAY);

	input_err(true, &client->dev, "%s: %X %s\n", __func__, cmd, ret < 0 ? "fail" : "");
	return ret;
}

static int tc3xxk_mode_check(struct i2c_client *client)
{
	int mode = i2c_smbus_read_byte_data(client, TC3XXK_MODE);
	if (mode < 0)
		input_err(true, &client->dev, "[TK] %s: failed to read mode (%d)\n",
				__func__, mode);

	return mode;
}

#ifdef FEATURE_GRIP_FOR_SAR
static int tc3xxk_wake_up(struct i2c_client *client, u8 cmd)
{
	//If stop mode enabled, touch key IC need wake_up CMD
	//After wake_up CMD, IC need 10ms delay

	int ret;
	input_info(true, &client->dev, "%s Send WAKE UP cmd: 0x%02x \n", __func__, cmd);
	ret = i2c_smbus_write_byte_data(client, TC3XXK_CMD_ADDR, TC3XXK_CMD_WAKE_UP);
	msleep(10);

	return ret;
}

static void tc3xxk_stop_mode(struct tc3xxk_data *data, bool on)
{
	struct i2c_client *client = data->client;
	int retry = 3;
	int ret;
	u8 cmd;
	int mode_retry = 1;
	bool reset_done = false;
	bool mode;

	mutex_lock(&data->lock);
	if (data->sar_mode == on){
		input_err(true, &client->dev, "[TK] %s : skip already %s\n",
				__func__, on ? "stop mode" : "normal mode");
		mutex_unlock(&data->lock);
		return;
	}

	if (on)
		cmd = TC3XXK_CMD_STOP_MODE;
	else
		cmd = TC3XXK_CMD_NORMAL_MODE;

	input_info(true, &client->dev, "[TK] %s: %s, cmd=%x\n",
			__func__, on ? "stop mode" : "normal mode", cmd);
sar_mode:
	//Add wake up command before change mode from STOP mode to NORMAL mode
	if (on == 0 && data->sar_mode == 1) {
		ret = tc3xxk_wake_up(client, TC3XXK_CMD_WAKE_UP);
		if (ret < 0)
			input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
					__func__, ret);
	}

	while (retry > 0) {
		ret = tc3xxk_mode_enable(client, cmd);
		if (ret < 0) {
			input_err(true, &client->dev, "%s fail to write mode(%d), retry %d\n",
					__func__, ret, retry);
			retry--;
			msleep(20);
			continue;
		}
		break;
	}

	msleep(40);

	// Add wake up command before i2c read/write in STOP mode
	if (on == 1) {
		ret = tc3xxk_wake_up(client, TC3XXK_CMD_WAKE_UP);
		if (ret < 0)
			input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
					__func__, ret);
	}

mode_check:
	retry = 3;
	while (retry > 0) {
		ret = tc3xxk_mode_check(client);
		if (ret < 0) {
			input_err(true, &client->dev, "%s fail to read mode(%d), retry %d\n",
					__func__, ret, retry);
			retry--;
			msleep(20);
			continue;
		}
		break;
	}

	/*	RUN MODE
	 *	1 : NORMAL TOUCH MODE
	 *	0 : STOP MODE
	 */
	mode = !(ret & TC3XXK_MODE_RUN);

	input_info(true, &client->dev, "%s: run mode:%s reg:0x%02X\n",
			__func__, mode ? "stop" : "normal", ret);

	if (mode != on) {
		input_err(true, &client->dev, "%s change fail %d, %d\n", __func__, mode, on);
		if (mode_retry == 1) {
			input_err(true, &client->dev, "%s retry\n", __func__);
			mode_retry = 0;
			goto sar_mode;
		}

		if (!on && !reset_done && data->enabled && !data->fw_downloding) {
			input_err(true, &client->dev,
					"%s change to normal mode is failed. do reset\n", __func__);
			tc3xxk_reset(data);
			reset_done = true;
			mode_retry = 1;
			goto mode_check;
		}
	}

	data->sar_mode = mode;

#ifdef CONFIG_TOUCHKEY_GRIP_SENSOR_OFF
	tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#endif
	mutex_unlock(&data->lock);
}

#ifndef CONFIG_TOUCHKEY_GRIP_SENSOR_OFF
static void touchkey_sar_sensing(struct tc3xxk_data *data, bool on)
{
	/* enable/disable sar sensing
	 * need to disable when earjack is connected (FM radio can't work normally)
	 */
}

static void tc3xxk_grip_cal_reset(struct tc3xxk_data *data)
{
	/* calibrate grip sensor chn */
	struct i2c_client *client = data->client;

	input_info(true, &client->dev, "[TK] %s\n", __func__);
	i2c_smbus_write_byte_data(client, TC3XXK_CMD_ADDR, TC3XXK_CMD_GRIP_BASELINE_CAL);
	msleep(TC3XXK_CMD_DELAY);
}
#endif
#endif


static void tc3xxk_release_all_fingers(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	int i;

	input_info(true, &client->dev, "[TK] %s\n", __func__);

	for (i = 0; i < data->key_num; i++) {
		input_report_key(data->input_dev,
				data->tsk_ev_val[i].tsk_keycode, 0);
	}
	input_sync(data->input_dev);
}

static void tc3xxk_reset(struct tc3xxk_data *data)
{
	input_info(true, &data->client->dev, "%s", __func__);
	disable_irq_nosync(data->client->irq);
	tc3xxk_release_all_fingers(data);

	data->power(data, false);
	msleep(TC3XXK_POWEROFF_DELAY);

	data->power(data, true);
	msleep(TC3XXK_POWERON_DELAY);

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	if (data->flip_mode)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_FLIP_ON);
#endif
	if (data->glove_mode)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_GLOVE_ON);

#ifdef FEATURE_GRIP_FOR_SAR
	if (data->sar_enable)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_SAR_ENABLE);
#ifndef CONFIG_TOUCHKEY_GRIP_SENSOR_OFF
	if (!data->grip_sensing)
#endif
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#else
	if (data->grip_sensing_status)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#endif
#ifdef CONFIG_VBUS_NOTIFIER
	if (data->vbus_state == STATUS_VBUS_HIGH)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_TA_ON);
#endif

	enable_irq(data->client->irq);

}

static void tc3xxk_reset_probe(struct tc3xxk_data *data)
{
	input_info(true, &data->client->dev, "%s", __func__);
	data->power(data, false);
	msleep(TC3XXK_POWEROFF_DELAY);

	data->power(data, true);
	msleep(TC3XXK_POWERON_DELAY);
}

int tc3xxk_get_fw_version(struct tc3xxk_data *data, bool probe)
{
	struct i2c_client *client = data->client;
	int retry = 3;
	int buf;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK]can't excute %s\n", __func__);
		return -1;
	}

	buf = i2c_smbus_read_byte_data(client, TC3XXK_FWVER);
	if (buf < 0) {
		while (retry--) {
			input_err(true, &client->dev, "[TK]%s read fail(%d)\n",
					__func__, retry);
			if (probe)
				tc3xxk_reset_probe(data);
			else
				tc3xxk_reset(data);
			buf = i2c_smbus_read_byte_data(client, TC3XXK_FWVER);
			if (buf > 0)
				break;
		}
		if (retry <= 0) {
			input_err(true, &client->dev, "[TK]%s read fail\n", __func__);
			data->fw_ver = 0;
			return -1;
		}
	}
	data->fw_ver = (u8)buf;

	buf = i2c_smbus_read_byte_data(client, TC3XXK_MDVER);
	if (buf < 0) {
		input_err(true, &client->dev, "[TK] %s: fail to read model ID", __func__);
		data->md_ver = 0;
	} else {
		data->md_ver = (u8)buf;
	}

	input_info(true, &client->dev, "[TK] %s:[IC] fw ver : 0x%x, model id:0x%x\n",
			__func__, data->fw_ver, data->md_ver);

	return 0;
}

static irqreturn_t tc3xxk_interrupt(int irq, void *dev_id)
{
	struct tc3xxk_data *data = dev_id;
	struct i2c_client *client = data->client;
	int ret, retry;
	u8 key_val;
	int i = 0;
	bool key_handle_flag;

#ifdef FEATURE_GRIP_FOR_SAR
#ifdef CONFIG_PM
	if (data->is_lpm_suspend) {
		input_info(true, &client->dev, "[TK] %s: irq in lpm suspend\n", __func__);
		wake_lock_timeout(&data->touchkey_wake_lock,
				msecs_to_jiffies(3 * MSEC_PER_SEC));
		/* waiting for blsp block resuming, if not occurs i2c error */
		ret = wait_for_completion_interruptible_timeout(&data->resume_done,
				msecs_to_jiffies(5 * MSEC_PER_SEC));
		if (ret == 0) {
			input_err(true, &client->dev,
					"%s: LPM: pm resume is not handled [timeout]\n", __func__);
			return IRQ_HANDLED;
		}
	}
#else
	wake_lock_timeout(&data->touchkey_wake_lock,
			msecs_to_jiffies(3 * MSEC_PER_SEC));
#endif
#endif

	input_dbg(false, &client->dev, "[TK] %s\n", __func__);

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return IRQ_HANDLED;
	}

#ifdef FEATURE_GRIP_FOR_SAR
	// if sar_mode is on => must send wake-up command
	if (data->sar_mode) {
		ret = tc3xxk_wake_up(client, TC3XXK_CMD_WAKE_UP);
		if (ret < 0)
			input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
					__func__, ret);
	}
#endif
	ret = i2c_smbus_read_byte_data(client, TC3XXK_KEYCODE);
	if (ret < 0) {
		retry = 3;
		while (retry--) {
			input_err(true, &client->dev, "[TK] %s read fail ret=%d(retry:%d)\n",
					__func__, ret, retry);
			msleep(10);
			ret = i2c_smbus_read_byte_data(client, TC3XXK_KEYCODE);
			if (ret > 0)
				break;
		}
		if (retry <= 0) {
			tc3xxk_reset(data);
			return IRQ_HANDLED;
		}
	}
	key_val = (u8)ret;

	for (i = 0 ; i < data->key_num * 2 ; i++){
		key_handle_flag = (key_val & data->tsk_ev_val[i].tsk_bitmap);

		if (key_handle_flag) {
			input_report_key(data->input_dev,
					data->tsk_ev_val[i].tsk_keycode, data->tsk_ev_val[i].tsk_status);
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
			input_info(true, &client->dev, "key %s vb:%c ver0x%02x\n",
					data->tsk_ev_val[i].tsk_status? "P" : "R",
					"0LH"[data->vbus_state], data->fw_ver);
#else
			input_err(true, &client->dev,
					"%s key %s(0x%02X) vb:%c ver0x%02x\n",
					data->tsk_ev_val[i].tsk_keyname,
					data->tsk_ev_val[i].tsk_status? "P" : "R", key_val,
					"0LH"[data->vbus_state], data->fw_ver);
#endif
#ifdef FEATURE_GRIP_FOR_SAR
			if (data->tsk_ev_val[i].tsk_keycode == KEY_CP_GRIP) {
				data->grip_event = data->tsk_ev_val[i].tsk_status;
			}
#endif
		}
	}

	input_sync(data->input_dev);
	return IRQ_HANDLED;
}

static ssize_t keycode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}


	ret = i2c_smbus_read_byte_data(client, TC3XXK_KEYCODE);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read threshold_h (%d)\n",
				__func__, ret);
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t third_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_CH_PER_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
		return sprintf(buf, "%d\n", value);
	} else {
		value = 0;
		return sprintf(buf, "%d\n", value);
	}

}

static ssize_t third_raw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_CH_RAW_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
		return sprintf(buf, "%d\n", value);
	} else {
		value = 0;
		return sprintf(buf, "%d\n", value);
	}
}

static ssize_t fourth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_CH_PER_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
		return sprintf(buf, "%d\n", value);
	} else {
		value = 0;
		return sprintf(buf, "%d\n", value);
	}
}

static ssize_t fourth_raw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_CH_RAW_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
		return sprintf(buf, "%d\n", value);
	} else {
		value = 0;
		return sprintf(buf, "%d\n", value);
	}
}

static ssize_t debug_c0_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}


	ret = i2c_smbus_read_byte_data(client, 0xC0);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read 0xC0 Register (%d)\n",
				__func__, ret);
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t debug_c1_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}


	ret = i2c_smbus_read_byte_data(client, 0xC1);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read 0xC1 Register (%d)\n",
				__func__, ret);
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t debug_c2_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}


	ret = i2c_smbus_read_byte_data(client, 0xC2);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read 0xC2 Register (%d)\n",
				__func__, ret);
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t debug_c3_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}


	ret = i2c_smbus_read_byte_data(client, 0xC3);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read 0xC3 Register (%d)\n",
				__func__, ret);
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}
static ssize_t tc3xxk_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	int value;
	u8 threshold_h, threshold_l;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_THRES_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
		return sprintf(buf, "%d\n", value);
	} else {
		ret = i2c_smbus_read_byte_data(client, TC3XXK_THRES_H);
		if (ret < 0) {
			input_err(true, &client->dev, "[TK] %s: failed to read threshold_h (%d)\n",
					__func__, ret);
			return ret;
		}
		threshold_h = ret;

		ret = i2c_smbus_read_byte_data(client, TC3XXK_THRES_L);
		if (ret < 0) {
			input_err(true, &client->dev, "[TK] %s: failed to read threshold_l (%d)\n",
					__func__, ret);
			return ret;
		}
		threshold_l = ret;

		data->threhold = (threshold_h << 8) | threshold_l;
		return sprintf(buf, "%d\n", data->threhold);
	}
}

static ssize_t tc3xxk_led_control(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int scan_buffer;
	int ret;
	u8 cmd;

	ret = kstrtoint(buf, 10, &scan_buffer);
	if (ret) {
		input_err(true, &client->dev, "[TK] %s: cmd read err\n", __func__);
		return count;
	}

	if (!(scan_buffer == 0 || scan_buffer == 1)) {
		input_err(true, &client->dev, "[TK] %s: wrong command(%d)\n",
				__func__, scan_buffer);
		return count;
	}

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		if (scan_buffer == 1)
			data->led_on = true;
		return count;
	}

	if (scan_buffer == 1) {
		input_info(true, &client->dev, "[TK] led on\n");
		cmd = TC3XXK_CMD_LED_ON;
	} else {
		input_info(true, &client->dev, "[TK] led off\n");
		cmd = TC3XXK_CMD_LED_OFF;
	}

	tc3xxk_mode_enable(client, cmd);

	return count;
}

static int load_fw_in_kernel(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = request_firmware(&data->fw, data->dtdata->fw_name, &client->dev);
	if (ret) {
		input_err(true, &client->dev, "[TK] %s fail(%d)\n", __func__, ret);
		return -1;
	}
	data->fw_img = (struct fw_image *)data->fw->data;

	input_info(true, &client->dev, "[TK] [BIN] fw ver : 0x%x (size=%d), model id : 0x%x\n",
			data->fw_img->first_fw_ver, data->fw_img->fw_len,
			data->fw_img->second_fw_ver);

	data->fw_ver_bin = data->fw_img->first_fw_ver;
	data->md_ver_bin = data->fw_img->second_fw_ver;
	return 0;
}

static int load_fw_sdcard(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int ret = 0;

	old_fs = get_fs();
	set_fs(get_ds());
	fp = filp_open(TC3XXK_FW_PATH_SDCARD, O_RDONLY, S_IRUSR);
	if (IS_ERR(fp)) {
		input_err(true, &client->dev, "[TK] %s %s open error\n",
				__func__, TC3XXK_FW_PATH_SDCARD);
		ret = -ENOENT;
		goto fail_sdcard_open;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;

	data->fw_img = kzalloc((size_t)fsize, GFP_KERNEL);
	if (!data->fw_img) {
		input_err(true, &client->dev, "[TK] %s fail to kzalloc for fw\n", __func__);
		filp_close(fp, current->files);
		ret = -ENOMEM;
		goto fail_sdcard_kzalloc;
	}

	nread = vfs_read(fp, (char __user *)data->fw_img, fsize, &fp->f_pos);
	if (nread != fsize) {
		input_err(true, &client->dev,
				"[TK] %s fail to vfs_read file\n", __func__);
		ret = -EINVAL;
		goto fail_sdcard_size;
	}
	filp_close(fp, current->files);
	set_fs(old_fs);

	input_info(true, &client->dev, "[TK] fw_size : %lu\n", nread);
	input_info(true, &client->dev, "[TK] %s done\n", __func__);

	return ret;

fail_sdcard_size:
	kfree(data->fw_img);
fail_sdcard_kzalloc:
	filp_close(fp, current->files);
fail_sdcard_open:
	set_fs(old_fs);

	return ret;
}

static inline void setsda(struct tc3xxk_data *data, int state)
{
	if (state)
		gpio_direction_output(data->dtdata->sda_gpio, 1);
	else
		gpio_direction_output(data->dtdata->sda_gpio, 0);
}

static inline void setscl(struct tc3xxk_data *data, int state)
{
	if (state)
		gpio_direction_output(data->dtdata->scl_gpio, 1);
	else
		gpio_direction_output(data->dtdata->scl_gpio, 0);
}

static inline int getsda(struct tc3xxk_data *data)
{
	return gpio_get_value(data->dtdata->sda_gpio);
}

static inline int getscl(struct tc3xxk_data *data)
{
	return gpio_get_value(data->dtdata->scl_gpio);
}

static void send_9bit(struct tc3xxk_data *data, u8 buff)
{
	int i;

	setscl(data, 1);
	ndelay(20);
	setsda(data, 0);
	ndelay(20);
	setscl(data, 0);
	ndelay(20);

	for (i = 0; i < 8; i++) {
		setscl(data, 1);
		ndelay(20);
		setsda(data, (buff >> i) & 0x01);
		ndelay(20);
		setscl(data, 0);
		ndelay(20);
	}

	setsda(data, 0);
}

static u8 wait_9bit(struct tc3xxk_data *data)
{
	int i;
	int buf;
	u8 send_buf = 0;

	gpio_direction_input(data->dtdata->sda_gpio);

	getsda(data);
	ndelay(10);
	setscl(data, 1);
	ndelay(40);
	setscl(data, 0);
	ndelay(20);

	for (i = 0; i < 8; i++) {
		setscl(data, 1);
		ndelay(20);
		buf = getsda(data);
		ndelay(20);
		setscl(data, 0);
		ndelay(20);
		send_buf |= (buf & 0x01) << i;
	}
	setsda(data, 0);

	return send_buf;
}

static void tc3xxk_reset_for_isp(struct tc3xxk_data *data, bool start)
{
	if (start) {
		setscl(data, 0);
		setsda(data, 0);
		data->power(data, false);

		msleep(TC3XXK_POWEROFF_DELAY * 2);

		data->power(data, true);

		usleep_range(5000, 6000);
	} else {
		data->power(data, false);
		msleep(TC3XXK_POWEROFF_DELAY * 2);

		data->power(data, true);
		msleep(TC3XXK_POWERON_DELAY);

		gpio_direction_input(data->dtdata->sda_gpio);
		gpio_direction_input(data->dtdata->scl_gpio);
	}
}

static void load(struct tc3xxk_data *data, u8 buff)
{
	send_9bit(data, TC3XXK_LDDATA);
	udelay(1);
	send_9bit(data, buff);
	udelay(1);
}

static void step(struct tc3xxk_data *data, u8 buff)
{
	send_9bit(data, TC3XXK_CCFG);
	udelay(1);
	send_9bit(data, buff);
	udelay(2);
}

static void setpc(struct tc3xxk_data *data, u16 addr)
{
	u8 buf[4];
	int i;

	buf[0] = 0x02;
	buf[1] = addr >> 8;
	buf[2] = addr & 0xff;
	buf[3] = 0x00;

	for (i = 0; i < 4; i++)
		step(data, buf[i]);
}

static void configure_isp(struct tc3xxk_data *data)
{
	u8 buf[7];
	int i;

	buf[0] = 0x75;
	buf[1] = 0xFC;
	buf[2] = 0xAC;
	buf[3] = 0x75;
	buf[4] = 0xFC;
	buf[5] = 0x35;
	buf[6] = 0x00;

	/* Step(cmd) */
	for (i = 0; i < 7; i++)
		step(data, buf[i]);
}

static int tc3xxk_erase_fw(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	int i;
	u8 state = 0;

	tc3xxk_reset_for_isp(data, true);

	/* isp_enable_condition */
	send_9bit(data, TC3XXK_CSYNC1);
	udelay(9);
	send_9bit(data, TC3XXK_CSYNC2);
	udelay(9);
	send_9bit(data, TC3XXK_CSYNC3);
	usleep_range(150, 160);

	state = wait_9bit(data);
	if (state != 0x01) {
		input_err(true, &client->dev, "[TK] %s isp enable error %d\n", __func__, state);
		return -1;
	}

	configure_isp(data);

	/* Full Chip Erase */
	send_9bit(data, TC3XXK_PCRST);
	udelay(1);
	send_9bit(data, TC3XXK_PECHIP);
	usleep_range(15000, 15500);


	state = 0;
	for (i = 0; i < 100; i++) {
		udelay(2);
		send_9bit(data, TC3XXK_CSYNC3);
		udelay(1);

		state = wait_9bit(data);
		if ((state & 0x04) == 0x00)
			break;
	}

	if (i == 100) {
		input_err(true, &client->dev, "[TK] %s fail\n", __func__);
		return -1;
	}

	input_info(true, &client->dev, "[TK] %s success\n", __func__);
	return 0;
}

static int tc3xxk_write_fw(struct tc3xxk_data *data)
{
	u16 addr = 0;
	u8 code_data;

	setpc(data, addr);
	load(data, TC3XXK_PWDATA);
	send_9bit(data, TC3XXK_LDMODE);
	udelay(1);

	while (addr < data->fw_img->fw_len) {
		code_data = data->fw_img->data[addr++];
		load(data, code_data);
		usleep_range(20, 21);
	}

	send_9bit(data, TC3XXK_PEDISC);
	udelay(1);

	return 0;
}

static int tc3xxk_verify_fw(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	u16 addr = 0;
	u8 code_data;

	setpc(data, addr);

	input_info(true, &client->dev, "[TK] fw code size = %#x (%u)",
			data->fw_img->fw_len, data->fw_img->fw_len);
	while (addr < data->fw_img->fw_len) {
		if ((addr % 0x40) == 0)
			input_dbg(true, &client->dev, "[TK] fw verify addr = %#x\n", addr);

		send_9bit(data, TC3XXK_PRDATA);
		udelay(2);
		code_data = wait_9bit(data);
		udelay(1);

		if (code_data != data->fw_img->data[addr++]) {
			input_err(true, &client->dev,
					"%s addr : %#x data error (0x%2x)\n",
					__func__, addr - 1, code_data );
			return -1;
		}
	}
	input_info(true, &client->dev, "[TK] %s success\n", __func__);

	return 0;
}

static void t300k_release_fw(struct tc3xxk_data *data, u8 fw_path)
{
	if (fw_path == FW_INKERNEL)
		release_firmware(data->fw);
	else if (fw_path == FW_SDCARD)
		kfree(data->fw_img);
}

static int tc3xxk_flash_fw(struct tc3xxk_data *data, u8 fw_path)
{
	struct i2c_client *client = data->client;
	int retry = 5;
	int ret;

	do {
		ret = tc3xxk_erase_fw(data);
		if (ret)
			input_err(true, &client->dev, "[TK] %s erase fail(retry=%d)\n",
					__func__, retry);
		else
			break;
	} while (retry-- > 0);
	if (retry < 0)
		goto err_tc3xxk_flash_fw;

	retry = 5;
	do {
		tc3xxk_write_fw(data);

		ret = tc3xxk_verify_fw(data);
		if (ret)
			input_err(true, &client->dev, "[TK] %s verify fail(retry=%d)\n",
					__func__, retry);
		else
			break;
	} while (retry-- > 0);

	tc3xxk_reset_for_isp(data, false);

	if (retry < 0)
		goto err_tc3xxk_flash_fw;

	return 0;

err_tc3xxk_flash_fw:

	return -1;
}

static int tc3xxk_crc_check(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	u16 checksum;
	u8 cmd;
	u8 checksum_h, checksum_l;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] %s can't excute\n", __func__);
		return -1;
	}

	cmd = TC3XXK_CMD_CAL_CHECKSUM;
	ret = tc3xxk_mode_enable(client, cmd);
	if (ret) {
		input_err(true, &client->dev, "[TK] %s command fail (%d)\n", __func__, ret);
		return ret;
	}

	msleep(TC3XXK_CHECKSUM_DELAY);

	ret = i2c_smbus_read_byte_data(client, TC3XXK_CHECKS_H);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read checksum_h (%d)\n",
				__func__, ret);
		return ret;
	}
	checksum_h = ret;

	ret = i2c_smbus_read_byte_data(client, TC3XXK_CHECKS_L);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s: failed to read checksum_l (%d)\n",
				__func__, ret);
		return ret;
	}
	checksum_l = ret;

	checksum = (checksum_h << 8) | checksum_l;

	if (data->fw_img->checksum != checksum) {
		input_err(true, &client->dev,
				"%s checksum fail - firm checksum(%d), compute checksum(%d)\n",
				__func__, data->fw_img->checksum, checksum);
		return -1;
	}

	input_info(true, &client->dev, "[TK] %s success (%d)\n", __func__, checksum);

	return 0;
}

static int tc3xxk_fw_update(struct tc3xxk_data *data, u8 fw_path, bool force)
{
	struct i2c_client *client = data->client;
	int retry = 4;
	int ret;

#ifndef FEATURE_GRIP_FOR_SAR
	cancel_work_sync(&data->start_work);
#endif

	if (fw_path == FW_INKERNEL) {
		if (!force) {
			ret = tc3xxk_get_fw_version(data, false);
			if (ret)
				return -1;
		}

		ret = load_fw_in_kernel(data);
		if (ret)
			return -1;

		if (!data->dtdata->bringup && data->md_ver != data->md_ver_bin) {
			input_err(true, &client->dev,
					"[TK] model id is different(IC:0x%x, BIN:0x%x)."
					" do force firm up\n",
					data->md_ver, data->md_ver_bin);
			force = true;
		}

		if (!force && (data->fw_ver >= data->fw_ver_bin)) {
			input_info(true, &client->dev,
					"[TK] do not need firm update (IC:0x%x, BIN:0x%x)\n",
					data->fw_ver, data->fw_ver_bin);
			t300k_release_fw(data, fw_path);
			return 0;
		}
	} else if (fw_path == FW_SDCARD) {
		ret = load_fw_sdcard(data);
		if (ret)
			return -1;
	}

	while (retry--) {
		data->fw_downloding = true;
		ret = tc3xxk_flash_fw(data, fw_path);
		data->fw_downloding = false;
		if (ret) {
			input_err(true, &client->dev, "[TK] %s tc3xxk_flash_fw fail (%d)\n",
					__func__, retry);
			continue;
		}

		ret = tc3xxk_get_fw_version(data, false);
		if (ret) {
			input_err(true, &client->dev, "[TK] %s tc3xxk_get_fw_version fail (%d)\n",
					__func__, retry);
			continue;
		}
		if (data->fw_ver != data->fw_img->first_fw_ver) {
			input_err(true, &client->dev, "[TK] %s fw version fail (0x%x, 0x%x)(%d)\n",
					__func__, data->fw_ver, data->fw_img->first_fw_ver, retry);
			continue;
		}

		ret = tc3xxk_crc_check(data);
		if (ret) {
			input_err(true, &client->dev, "[TK] %s crc check fail (%d)\n",
					__func__, retry);
			continue;
		}
		break;
	}

	if (retry > 0)
		input_info(true, &client->dev, "%s success\n", __func__);

	t300k_release_fw(data, fw_path);

	return ret;
}

static ssize_t tc3xxk_update_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 fw_path;

	switch(*buf) {
	case 's':
	case 'S':
	case 'f':
	case 'F':
		fw_path = FW_INKERNEL;
		break;
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
	case 'i':
	case 'I':
		fw_path = FW_SDCARD;
		break;
#endif
	default:
		input_err(true, &client->dev, "[TK] %s wrong command fail\n", __func__);
		data->fw_update_status = TK_UPDATE_FAIL;
		return count;
	}

	data->fw_update_status = TK_UPDATE_DOWN;

	disable_irq(client->irq);
	ret = tc3xxk_fw_update(data, fw_path, true);
	enable_irq(client->irq);
	if (ret < 0) {
		input_err(true, &client->dev, "[TK] %s fail\n", __func__);
		data->fw_update_status = TK_UPDATE_FAIL;
	} else {
		data->fw_update_status = TK_UPDATE_PASS;
	}

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	if (data->flip_mode)
		tc3xxk_mode_enable(client, TC3XXK_CMD_FLIP_ON);
#endif
	if (data->glove_mode)
		tc3xxk_mode_enable(client, TC3XXK_CMD_GLOVE_ON);

#ifdef FEATURE_GRIP_FOR_SAR
	if (data->sar_enable)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_SAR_ENABLE);
#ifndef CONFIG_TOUCHKEY_GRIP_SENSOR_OFF
	if (!data->grip_sensing)
#endif
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#else
	ret = tc3xxk_mode_check(client);
	if (ret >= 0) {
		data->grip_sensing_status = !!(ret & TC3XXK_MODE_GRIP_SENSING);
		input_info(true, dev, "%s: mode %02X, grip_sensing %d\n",
				__func__, ret, data->grip_sensing_status);

		if (data->grip_sensing_status)
			tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
	}
#endif
#ifdef CONFIG_VBUS_NOTIFIER
	if (data->vbus_state == STATUS_VBUS_HIGH)
		tc3xxk_mode_enable(data->client, TC3XXK_CMD_TA_ON);
#endif

	return count;
}

static ssize_t tc3xxk_firm_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->fw_update_status == TK_UPDATE_PASS)
		ret = sprintf(buf, "PASS\n");
	else if (data->fw_update_status == TK_UPDATE_DOWN)
		ret = sprintf(buf, "DOWNLOADING\n");
	else if (data->fw_update_status == TK_UPDATE_FAIL)
		ret = sprintf(buf, "FAIL\n");
	else
		ret = sprintf(buf, "NG\n");

	return ret;
}

static ssize_t tc3xxk_firm_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "0x%02x\n", data->fw_ver_bin);
}

static ssize_t tc3xxk_firm_version_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	ret = tc3xxk_get_fw_version(data, false);
	if (ret < 0)
		input_err(true, &client->dev, "[TK] %s: failed to read firmware version (%d)\n",
				__func__, ret);

	return sprintf(buf, "0x%02x\n", data->fw_ver);
}

static ssize_t recent_key_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_CH_PER_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
	} else {
		ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_2KEY_DATA, 6, buff);
		if (ret != 6) {
			input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
			return -1;
		}
		value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
			buff[TC3XXK_CH_PCK_L_OFFSET];
	}
	return sprintf(buf, "%d\n", value);
}

static ssize_t recent_key_ref_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (data->dtdata->sensing_ch_num < 6)
		return sprintf(buf, "%d\n", 0);

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_6KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
			buff[TC3XXK_CH_PCK_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t back_key_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_CH_PER_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
	} else {
		ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_1KEY_DATA, 6, buff);
		if (ret != 6) {
			input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
			return -1;
		}
		value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
			buff[TC3XXK_CH_PCK_L_OFFSET];
	}
	return sprintf(buf, "%d\n", value);
}

static ssize_t back_key_ref_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (data->dtdata->sensing_ch_num < 6)
		return sprintf(buf, "%d\n", 0);

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_5KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
		buff[TC3XXK_CH_PCK_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t dummy_recent_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[6];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_4KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
		buff[TC3XXK_CH_PCK_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t dummy_back_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[6];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_3KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_CH_PCK_H_OFFSET] << 8) |
		buff[TC3XXK_CH_PCK_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t recent_key_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	input_info(true, &client->dev, "[TK] %s called!\n", __func__);

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_CH_RAW_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
	} else {
		ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_2KEY_DATA, 6, buff);

		if (ret != 6) {
			input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
			return -1;
		}
		value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
			buff[TC3XXK_RAW_L_OFFSET];
	}
	return sprintf(buf, "%d\n", value);
}

static ssize_t recent_key_raw_ref(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (data->dtdata->sensing_ch_num < 6)
		return sprintf(buf, "%d\n", 0);

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_6KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
		buff[TC3XXK_RAW_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t back_key_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	input_info(true, &client->dev, "[TK] %s called!\n", __func__);

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	if (!data->dtdata->tc300k) {
		mutex_lock(&data->lock_fac);
		value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_CH_RAW_DATA_OFFSET);
		mutex_unlock(&data->lock_fac);
	} else {
		ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_1KEY_DATA, 6, buff);
		if (ret != 6) {
			input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
			return -1;
		}
		value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
			buff[TC3XXK_RAW_L_OFFSET];
	}
	return sprintf(buf, "%d\n", value);
}

static ssize_t back_key_raw_ref(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[8];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}


	if (data->dtdata->sensing_ch_num < 6)
		return sprintf(buf, "%d\n", 0);

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_5KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
		buff[TC3XXK_RAW_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t dummy_recent_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[6];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_4KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
		buff[TC3XXK_RAW_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static ssize_t dummy_back_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[6];
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	ret = i2c_smbus_read_i2c_block_data(client, TC3XXK_3KEY_DATA, 6, buff);
	if (ret != 6) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		return -1;
	}
	value = (buff[TC3XXK_RAW_H_OFFSET] << 8) |
		buff[TC3XXK_RAW_L_OFFSET];

	return sprintf(buf, "%d\n", value);
}

static int read_tc3xxk_register_data(struct tc3xxk_data *data, int read_key_num, int read_offset)
{
	struct i2c_client *client = data->client;
	int ret;
	u8 buff[2];
	int value;

	ret = i2c_smbus_read_i2c_block_data(client, read_key_num + read_offset, TC350K_DATA_SIZE, buff);
	if (ret != TC350K_DATA_SIZE) {
		input_err(true, &client->dev, "[TK] %s read fail(%d)\n", __func__, ret);
		value = 0;
		goto exit;
	}
	value = (buff[TC350K_DATA_H_OFFSET] << 8) | buff[TC350K_DATA_L_OFFSET];

	input_info(true, &client->dev, "[TK] %s : read key num/offset = [0x%X/0x%X], value : [%d]\n",
			__func__, read_key_num, read_offset, value);

exit:
	return value;
}

static ssize_t back_raw_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_CH_RAW_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_raw_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_CH_RAW_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_raw_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_CH_RAW_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_raw_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_CH_RAW_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_idac_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_CH_DIFF_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_idac_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_CH_DIFF_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_idac_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_CH_DIFF_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_idac_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_CH_DIFF_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_CH_PER_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_CH_PER_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_CH_PER_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_CH_PER_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_threshold_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_2KEY, TC350K_THRES_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t back_threshold_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_4KEY, TC350K_THRES_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_threshold_inner(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_1KEY, TC350K_THRES_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}
static ssize_t recent_threshold_outer(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int value;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &data->client->dev, "[TK] can't excute %s\n", __func__);
		return -1;
	}

	mutex_lock(&data->lock_fac);
	value = read_tc3xxk_register_data(data, TC350K_3KEY, TC350K_THRES_DATA_OFFSET);
	mutex_unlock(&data->lock_fac);

	return sprintf(buf, "%d\n", value);
}

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
static ssize_t tc3xxk_flip_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int flip_mode_on, ret;
	u8 cmd;

	ret = kstrtoint(buf, 10, &flip_mode_on);
	if (ret) {
		input_err(true, &client->dev,
				"%s : failed to read cmd, %d\n", __func__, ret);
		goto out;
	}

	input_info(true, &client->dev, "%s : %d\n", __func__, flip_mode_on);

	if (!data->enabled || data->fw_downloding)
		goto out;

	if (flip_mode_on)
		cmd = TC3XXK_CMD_FLIP_ON;
	else
		cmd = TC3XXK_CMD_FLIP_OFF;

	tc3xxk_mode_enable(client, cmd);
out:
	data->flip_mode = flip_mode_on;
	return count;
}
#endif

static ssize_t tc3xxk_glove_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int scan_buffer;
	int ret;
	u8 cmd;

	ret = kstrtoint(buf, 10, &scan_buffer);
	if (ret) {
		input_err(true, &client->dev, "[TK] %s: cmd read err\n", __func__);
		return count;
	}
	if (!(scan_buffer == 0 || scan_buffer == 1)) {
		input_err(true, &client->dev, "[TK] %s: wrong command(%d)\n",
				__func__, scan_buffer);
		return count;
	}
	if (data->glove_mode == (bool)scan_buffer) {
		input_info(true, &client->dev, "[TK] %s same command(%d)\n",
				__func__, scan_buffer);
		return count;
	}

	if (scan_buffer == 1) {
		input_info(true, &client->dev, "[TK] glove mode\n");
		cmd = TC3XXK_CMD_GLOVE_ON;
	} else {
		input_info(true, &client->dev, "[TK] normal mode\n");
		cmd = TC3XXK_CMD_GLOVE_OFF;

	}
	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		data->glove_mode = (bool)scan_buffer;
		return count;
	}

	tc3xxk_mode_enable(client, cmd);
	data->glove_mode = (bool)scan_buffer;

	return count;
}

static ssize_t tc3xxk_glove_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->glove_mode);
}

static ssize_t tc3xxk_modecheck_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	u8 mode, glove, run, sar, ta, grip_sensing;

	if ((!data->enabled) || data->fw_downloding) {
		input_err(true, &client->dev, "[TK] can't excute %s\n", __func__);
		return -EPERM;
	}

	ret = tc3xxk_mode_check(client);
	if (ret < 0)
		return ret;
	else
		mode = ret;

	glove = !!(mode & TC3XXK_MODE_GLOVE);
	run = !!(mode & TC3XXK_MODE_RUN);
	sar = !!(mode & TC3XXK_MODE_SAR);
	grip_sensing = !!(mode & TC3XXK_MODE_GRIP_SENSING);
	ta = !!(mode & TC3XXK_MODE_TA_CONNECTED);

	input_info(true, &client->dev, "%s: bit:%x, glove:%d, run:%d, sar:%d, grip_sensing:%d, ta:%d\n",
			__func__, mode, glove, run, sar, grip_sensing, ta);
	return sprintf(buf, "bit:%x, glove:%d, run:%d, sar:%d, grip_sensing:%d, ta:%d\n",
			mode, glove, run, sar, grip_sensing, ta);
}

#if defined(FEATURE_GRIP_FOR_SAR) && !defined(CONFIG_TOUCHKEY_GRIP_SENSOR_OFF)
static ssize_t touchkey_sar_enable(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int buff;
	int ret;
	bool on;
	int cmd;

	ret = kstrtoint(buf, 10, &buff);
	if (ret) {
		input_err(true, &client->dev, "%s: cmd read err\n", __func__);
		return count;
	}

	input_info(true, &data->client->dev, "%s (%d)\n", __func__, buff);

	if (!(buff >= 0 && buff <= 3)) {
		input_err(true, &client->dev, "%s: wrong command(%d)\n",
				__func__, buff);
		return count;
	}

	/*	sar enable param
	 *	0	off
	 *	1	on
	 *	2	force off
	 *	3	force off -> on
	 */

	if (buff == 3) {
		data->sar_enable_off = 0;
		input_info(true, &data->client->dev,
				"%s : Power back off _ force off -> on (%d)\n",
				__func__, data->sar_enable);
		if (data->sar_enable)
			buff = 1;
		else
			return count;
	}

	if (data->sar_enable_off) {
		if (buff == 1)
			data->sar_enable = true;
		else
			data->sar_enable = false;
		input_info(true, &data->client->dev,
				"%s skip, Power back off _ force off mode (%d)\n",
				__func__, data->sar_enable);
		return count;
	}

	if (buff == 1) {
		on = true;
		cmd = TC3XXK_CMD_SAR_ENABLE;
	} else if (buff == 2) {
		on = false;
		data->sar_enable_off = 1;
		cmd = TC3XXK_CMD_SAR_DISABLE;
	} else {
		on = false;
		cmd = TC3XXK_CMD_SAR_DISABLE;
	}

	if (data->fw_downloding) {
		input_err(true, &client->dev,
				"%s: can't excute now. just save status\n", __func__);
		goto out;
	}

	// if sar_mode is on => must send wake-up command
	if (data->sar_mode) {
		ret = tc3xxk_wake_up(data->client, TC3XXK_CMD_WAKE_UP);
		if (ret < 0)
			input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
					__func__, ret);
	}

	ret = tc3xxk_mode_enable(data->client, cmd);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail(%d)\n", __func__, ret);
		return count;
	}

out:
	if (buff == 1) {
		data->sar_enable = true;
	} else {
		input_report_key(data->input_dev, KEY_CP_GRIP, TSK_RELEASE);
		data->grip_event = 0;
		data->sar_enable = false;
	}

	input_info(true, &data->client->dev, "%s data:%d on:%d\n",__func__, buff, on);
	return count;
}

static ssize_t touchkey_grip_sensing_enable(struct device *dev,
		 struct device_attribute *attr, const char *buf,
		 size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int buff;
	int ret;
	bool on;
	int cmd;

	ret = kstrtoint(buf, 10, &buff);
	if (ret) {
		input_err(true, &client->dev, "%s: cmd read err\n", __func__);
		return count;
	}

	if (buff != 0 && buff != 1) {
		input_err(true, &client->dev, "%s: wrong command %d\n",
				__func__, buff);
		return count;
	}

	on = !!buff;

	if (on == data->grip_sensing) {
		input_err(true, &client->dev, "%s: already %sabled\n",
				__func__, on ? "en" : "dis");
		return count;
	}

	if (data->fw_downloding) {
		input_err(true, &client->dev,
				"%s: can't excute now. just save status\n", __func__);
		goto out;
	}

	input_info(true, &data->client->dev, "%s: %d\n", __func__, buff);

	if (on)
		cmd = TC3XXK_CMD_GRIP_SENSING_ON;
	else
		cmd = TC3XXK_CMD_GRIP_SENSING_OFF;

	if (data->sar_mode) {
		ret = tc3xxk_wake_up(data->client, TC3XXK_CMD_WAKE_UP);
		if (ret < 0)
			input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
					__func__, ret);
	}

	ret = tc3xxk_mode_enable(data->client, cmd);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s: fail(%d)\n", __func__, ret);
		return count;
	}

out:
	data->grip_sensing = on;
	input_info(true, &data->client->dev,
			"%s: %sabled\n",__func__, data->grip_sensing ? "en" : "dis");
	return count;
}

static ssize_t touchkey_grip_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_THD_PRESS, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail to read press thd(%d)\n", __func__, ret);
		data->grip_p_thd = 0;
		return sprintf(buf, "%d\n", 0);
	}
	data->grip_p_thd = ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_THD_RELEASE, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail to read release thd(%d)\n", __func__, ret);
		data->grip_r_thd = 0;
		return sprintf(buf, "%d\n", 0);
	}

	data->grip_r_thd = ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_THD_NOISE, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail to read noise thd(%d)\n", __func__, ret);
		data->grip_n_thd = 0;
		return sprintf(buf, "%d\n", 0);
	}
	data->grip_n_thd = ret;

	return sprintf(buf, "%d,%d,%d\n",
			data->grip_p_thd, data->grip_r_thd, data->grip_n_thd );
}

static ssize_t touchkey_total_cap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, TC305K_GRIP_TOTAL_CAP);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail(%d)\n", __func__, ret);
		return sprintf(buf, "%d\n", 0);
	}

	return sprintf(buf, "%d\n", ret / 100);
}

static ssize_t touchkey_grip_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_DIFF_DATA, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail(%d)\n", __func__, ret);
		data->grip_s1 = 0;
		return sprintf(buf, "%d\n", 0);
	}
	data->grip_s1 = ret;

	return sprintf(buf, "%d\n", data->grip_s1);
}

static ssize_t touchkey_grip_baseline_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_BASELINE, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail(%d)\n", __func__, ret);
		data->grip_baseline = 0;
		return sprintf(buf, "%d\n", 0);
	}
	data->grip_baseline = ret;

	return sprintf(buf, "%d\n", data->grip_baseline);

}

static ssize_t touchkey_grip_raw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock_fac);
	ret = read_tc3xxk_register_data(data, TC305K_GRIP_RAW_DATA, 0);
	mutex_unlock(&data->lock_fac);
	if (ret < 0) {
		input_err(true, &data->client->dev, "%s fail(%d)\n", __func__, ret);
		data->grip_raw1 = 0;
		data->grip_raw2 = 0;
		return sprintf(buf, "%d\n", 0);
	}
	data->grip_raw1 = ret;
	data->grip_raw2 = 0;

	return sprintf(buf, "%d,%d\n", data->grip_raw1, data->grip_raw2);
}

static ssize_t touchkey_grip_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d,%d,%d,%d\n", 0, 0, 0, 0);
}

static ssize_t touchkey_grip_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	input_info(true, &data->client->dev, "%s event:%d\n", __func__, data->grip_event);

	return sprintf(buf, "%d\n", data->grip_event);
}

static ssize_t touchkey_grip_sw_reset(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int buff;
	int ret;

	ret = kstrtoint(buf, 10, &buff);
	if (ret) {
		input_err(true, &client->dev, "%s: cmd read err\n", __func__);
		return count;
	}

	if (!(buff == 1)) {
		input_err(true, &client->dev, "%s: wrong command(%d)\n",
				__func__, buff);
		return count;
	}

	data->grip_event = 0;

	input_info(true, &data->client->dev, "%s data(%d)\n", __func__, buff);

	tc3xxk_grip_cal_reset(data);

	return count;
}

static ssize_t touchkey_sensing_change(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret, buff;

	ret = kstrtoint(buf, 10, &buff);
	if (ret) {
		input_err(true, &client->dev, "%s: cmd read err\n", __func__);
		return count;
	}

	if (!(buff == 0 || buff == 1)) {
		input_err(true, &client->dev, "%s: wrong command(%d)\n",
				__func__, buff);
		return count;
	}

	touchkey_sar_sensing(data, buff);

	input_info(true, &data->client->dev, "%s earjack (%d)\n", __func__, buff);

	return count;
}

#if 0//ndef CONFIG_SAMSUNG_PRODUCT_SHIP
static ssize_t touchkey_sar_press_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	int ret;
	int threshold;
	u8 cmd[2];

	ret = kstrtoint(buf, 10, &threshold);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s: failed to read thresold, buf is %s\n",
				__func__,buf);
		return count;
	}

	if (threshold > 0xff) {
		cmd[0] = (threshold >> 8) & 0xff;
		cmd[1] = 0xff & threshold;
	} else if (threshold < 0) {
		cmd[0] = 0x0;
		cmd[1] = 0x0;
	} else {
		cmd[0] = 0x0;
		cmd[1] = (u8)threshold;
	}

	input_info(true, &data->client->dev, "%s buf : %d, threshold : %d\n",
			__func__, threshold, (cmd[0] << 8) | cmd[1]);

	ret = abov_tk_i2c_write(data->client, CMD_SAR_THRESHOLD, &cmd[0], 1);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s failed to write press_threhold data1", __func__);
		goto press_threshold_out;
	}

	ret = abov_tk_i2c_write(data->client, CMD_SAR_THRESHOLD + 0x01, &cmd[1], 1);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s failed to write press_threhold data2", __func__);
		goto press_threshold_out;
	}

press_threshold_out:
	return count;
}

static ssize_t touchkey_sar_release_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	int ret;
	int threshold;
	u8 cmd[2];

	ret = kstrtoint(buf, 10, &threshold);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s: failed to read thresold, buf is %s\n",
				__func__, buf);
		return count;
	}

	if (threshold > 0xff) {
		cmd[0] = (threshold >> 8) & 0xff;
		cmd[1] = 0xff & threshold;
	} else if (threshold < 0) {
		cmd[0] = 0x0;
		cmd[1] = 0x0;
	} else {
		cmd[0] = 0x0;
		cmd[1] = (u8)threshold;
	}

	input_info(true, &data->client->dev, "%s buf : %d, threshold : %d\n",
			__func__, threshold,(cmd[0] << 8) | cmd[1]);

	ret = abov_tk_i2c_write(data->client, CMD_SAR_THRESHOLD + 0x02, &cmd[0], 1);
	input_info(true, &data->client->dev, "%s ret : %d\n", __func__, ret);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s failed to write release_threshold_data1", __func__);
		goto release_threshold_out;
	}

	ret = abov_tk_i2c_write(data->client, CMD_SAR_THRESHOLD + 0x03, &cmd[1], 1);
	input_info(true, &data->client->dev, "%s ret : %d\n", __func__, ret);
	if (ret) {
		input_err(true, &data->client->dev,
				"%s failed to write release_threshold_data2", __func__);
		goto release_threshold_out;
	}

release_threshold_out:
	return count;
}
#endif

static ssize_t touchkey_mode_change(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret, buff;

	ret = kstrtoint(buf, 10, &buff);
	if (ret) {
		input_err(true, &client->dev, "%s: cmd read err\n", __func__);
		return count;
	}

	if (!(buff == 0 || buff == 1)) {
		input_err(true, &client->dev, "%s: wrong command(%d)\n", __func__, buff);
		return count;
	}

	input_info(true, &data->client->dev, "%s data(%d)\n", __func__, buff);

	tc3xxk_stop_mode(data, buff);

	return count;
}
#endif


static ssize_t touchkey_chip_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc3xxk_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	input_info(true, &client->dev, "%s\n", __func__);

	return sprintf(buf, "TC305K\n");
}

static DEVICE_ATTR(touchkey_threshold, S_IRUGO, tc3xxk_threshold_show, NULL);
static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		tc3xxk_led_control);
static DEVICE_ATTR(touchkey_firm_update, S_IRUGO | S_IWUSR | S_IWGRP,
		NULL, tc3xxk_update_store);
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO,
		tc3xxk_firm_status_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO,
		tc3xxk_firm_version_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO,
		tc3xxk_firm_version_read_show, NULL);
static DEVICE_ATTR(touchkey_recent, S_IRUGO, recent_key_show, NULL);
static DEVICE_ATTR(touchkey_recent_ref, S_IRUGO, recent_key_ref_show, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO, back_key_show, NULL);
static DEVICE_ATTR(touchkey_back_ref, S_IRUGO, back_key_ref_show, NULL);
static DEVICE_ATTR(touchkey_d_menu, S_IRUGO, dummy_recent_show, NULL);
static DEVICE_ATTR(touchkey_d_back, S_IRUGO, dummy_back_show, NULL);
static DEVICE_ATTR(touchkey_recent_raw, S_IRUGO, recent_key_raw, NULL);
static DEVICE_ATTR(touchkey_recent_raw_ref, S_IRUGO, recent_key_raw_ref, NULL);
static DEVICE_ATTR(touchkey_back_raw, S_IRUGO, back_key_raw, NULL);
static DEVICE_ATTR(touchkey_back_raw_ref, S_IRUGO, back_key_raw_ref, NULL);
static DEVICE_ATTR(touchkey_d_menu_raw, S_IRUGO, dummy_recent_raw, NULL);
static DEVICE_ATTR(touchkey_d_back_raw, S_IRUGO, dummy_back_raw, NULL);

/* for tc350k */
static DEVICE_ATTR(touchkey_back_raw_inner, S_IRUGO, back_raw_inner, NULL);
static DEVICE_ATTR(touchkey_back_raw_outer, S_IRUGO, back_raw_outer, NULL);
static DEVICE_ATTR(touchkey_recent_raw_inner, S_IRUGO, recent_raw_inner, NULL);
static DEVICE_ATTR(touchkey_recent_raw_outer, S_IRUGO, recent_raw_outer, NULL);

static DEVICE_ATTR(touchkey_back_idac_inner, S_IRUGO, back_idac_inner, NULL);
static DEVICE_ATTR(touchkey_back_idac_outer, S_IRUGO, back_idac_outer, NULL);
static DEVICE_ATTR(touchkey_recent_idac_inner, S_IRUGO, recent_idac_inner, NULL);
static DEVICE_ATTR(touchkey_recent_idac_outer, S_IRUGO, recent_idac_outer, NULL);

static DEVICE_ATTR(touchkey_back_idac, S_IRUGO, back_idac_inner, NULL);
static DEVICE_ATTR(touchkey_recent_idac, S_IRUGO, recent_idac_inner, NULL);

static DEVICE_ATTR(touchkey_back_inner, S_IRUGO, back_inner, NULL);
static DEVICE_ATTR(touchkey_back_outer, S_IRUGO, back_outer, NULL);
static DEVICE_ATTR(touchkey_recent_inner, S_IRUGO, recent_inner, NULL);
static DEVICE_ATTR(touchkey_recent_outer, S_IRUGO, recent_outer, NULL);

static DEVICE_ATTR(touchkey_recent_threshold_inner, S_IRUGO, recent_threshold_inner, NULL);
static DEVICE_ATTR(touchkey_back_threshold_inner, S_IRUGO, back_threshold_inner, NULL);
static DEVICE_ATTR(touchkey_recent_threshold_outer, S_IRUGO, recent_threshold_outer, NULL);
static DEVICE_ATTR(touchkey_back_threshold_outer, S_IRUGO, back_threshold_outer, NULL);
/* end 350k */

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
static DEVICE_ATTR(flip_mode, S_IRUGO | S_IWUSR | S_IWGRP,
		NULL, tc3xxk_flip_mode);
#endif
static DEVICE_ATTR(glove_mode, S_IRUGO | S_IWUSR | S_IWGRP,
		tc3xxk_glove_mode_show, tc3xxk_glove_mode);
static DEVICE_ATTR(modecheck, S_IRUGO, tc3xxk_modecheck_show, NULL);


static DEVICE_ATTR(touchkey_keycode, S_IRUGO, keycode_show, NULL);
static DEVICE_ATTR(touchkey_3rd, S_IRUGO, third_show, NULL);
static DEVICE_ATTR(touchkey_3rd_raw, S_IRUGO, third_raw_show, NULL);
static DEVICE_ATTR(touchkey_4th, S_IRUGO, fourth_show, NULL);
static DEVICE_ATTR(touchkey_4th_raw, S_IRUGO, fourth_raw_show, NULL);
static DEVICE_ATTR(touchkey_debug0, S_IRUGO, debug_c0_show, NULL);
static DEVICE_ATTR(touchkey_debug1, S_IRUGO, debug_c1_show, NULL);
static DEVICE_ATTR(touchkey_debug2, S_IRUGO, debug_c2_show, NULL);
static DEVICE_ATTR(touchkey_debug3, S_IRUGO, debug_c3_show, NULL);

#if defined(FEATURE_GRIP_FOR_SAR) && !defined(CONFIG_TOUCHKEY_GRIP_SENSOR_OFF)
static DEVICE_ATTR(touchkey_grip_threshold, S_IRUGO, touchkey_grip_threshold_show, NULL);
static DEVICE_ATTR(touchkey_total_cap, S_IRUGO, touchkey_total_cap_show, NULL);
static DEVICE_ATTR(sar_enable, S_IWUSR | S_IWGRP, NULL, touchkey_sar_enable);
static DEVICE_ATTR(sw_reset, S_IWUSR | S_IWGRP, NULL, touchkey_grip_sw_reset);
static DEVICE_ATTR(touchkey_earjack, S_IWUSR | S_IWGRP, NULL, touchkey_sensing_change);
static DEVICE_ATTR(touchkey_grip, S_IRUGO, touchkey_grip_show, NULL);
static DEVICE_ATTR(touchkey_grip_baseline, S_IRUGO, touchkey_grip_baseline_show, NULL);
static DEVICE_ATTR(touchkey_grip_raw, S_IRUGO, touchkey_grip_raw_show, NULL);
static DEVICE_ATTR(touchkey_grip_gain, S_IRUGO, touchkey_grip_gain_show, NULL);
static DEVICE_ATTR(touchkey_grip_check, S_IRUGO, touchkey_grip_check_show, NULL);
static DEVICE_ATTR(touchkey_sar_only_mode,  S_IWUSR | S_IWGRP, NULL, touchkey_mode_change);
static DEVICE_ATTR(grip_sensing_enable, S_IWUSR | S_IWGRP, NULL, touchkey_grip_sensing_enable);
#if 0//ndef CONFIG_SAMSUNG_PRODUCT_SHIP
static DEVICE_ATTR(touchkey_sar_press_threshold,  S_IRUGO | S_IWUSR | S_IWGRP | S_IWOTH,
		NULL, touchkey_sar_press_threshold_store);
static DEVICE_ATTR(touchkey_sar_release_threshold,  S_IRUGO | S_IWUSR | S_IWGRP | S_IWOTH,
		NULL, touchkey_sar_release_threshold_store);
#endif
#endif
static DEVICE_ATTR(touchkey_chip_name, S_IRUGO, touchkey_chip_name, NULL);


static struct attribute *sec_touchkey_attributes[] = {
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_touchkey_firm_update.attr,
	&dev_attr_touchkey_firm_update_status.attr,
	&dev_attr_touchkey_firm_version_phone.attr,
	&dev_attr_touchkey_firm_version_panel.attr,
	&dev_attr_touchkey_recent.attr,
	&dev_attr_touchkey_recent_ref.attr,
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_back_ref.attr,
	&dev_attr_touchkey_d_menu.attr,
	&dev_attr_touchkey_d_back.attr,
	&dev_attr_touchkey_recent_raw.attr,
	&dev_attr_touchkey_recent_raw_ref.attr,
	&dev_attr_touchkey_back_raw.attr,
	&dev_attr_touchkey_back_raw_ref.attr,
	&dev_attr_touchkey_d_menu_raw.attr,
	&dev_attr_touchkey_d_back_raw.attr,
#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	&dev_attr_flip_mode.attr,
#endif
	&dev_attr_glove_mode.attr,
	&dev_attr_modecheck.attr,
#if defined(FEATURE_GRIP_FOR_SAR) && !defined(CONFIG_TOUCHKEY_GRIP_SENSOR_OFF)
	&dev_attr_touchkey_grip_threshold.attr,
	&dev_attr_touchkey_total_cap.attr,
	&dev_attr_sar_enable.attr,
	&dev_attr_sw_reset.attr,
	&dev_attr_touchkey_earjack.attr,
	&dev_attr_touchkey_grip.attr,
	&dev_attr_touchkey_grip_baseline.attr,
	&dev_attr_touchkey_grip_raw.attr,
	&dev_attr_touchkey_grip_gain.attr,
	&dev_attr_touchkey_grip_check.attr,
	&dev_attr_touchkey_sar_only_mode.attr,
	&dev_attr_grip_sensing_enable.attr,
#if 0//ndef CONFIG_SAMSUNG_PRODUCT_SHIP
	&dev_attr_touchkey_sar_press_threshold.attr,
	&dev_attr_touchkey_sar_release_threshold.attr,
#endif
#endif
	&dev_attr_touchkey_chip_name.attr,
	NULL,
};

static struct attribute_group sec_touchkey_attr_group = {
	.attrs = sec_touchkey_attributes,
};

static struct attribute *sec_touchkey_attributes_350k[] = {
	&dev_attr_touchkey_firm_update.attr,
	&dev_attr_touchkey_firm_update_status.attr,
	&dev_attr_touchkey_firm_version_phone.attr,
	&dev_attr_touchkey_firm_version_panel.attr,

	&dev_attr_touchkey_back_raw_inner.attr,
	&dev_attr_touchkey_back_raw_outer.attr,
	&dev_attr_touchkey_recent_raw_inner.attr,
	&dev_attr_touchkey_recent_raw_outer.attr,

	&dev_attr_touchkey_back_idac_inner.attr,
	&dev_attr_touchkey_back_idac_outer.attr,
	&dev_attr_touchkey_recent_idac_inner.attr,
	&dev_attr_touchkey_recent_idac_outer.attr,

	&dev_attr_touchkey_back_inner.attr,
	&dev_attr_touchkey_back_outer.attr,
	&dev_attr_touchkey_recent_inner.attr,
	&dev_attr_touchkey_recent_outer.attr,

	&dev_attr_touchkey_recent_threshold_inner.attr,
	&dev_attr_touchkey_back_threshold_inner.attr,
	&dev_attr_touchkey_recent_threshold_outer.attr,
	&dev_attr_touchkey_back_threshold_outer.attr,

	&dev_attr_touchkey_recent.attr,
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_recent_raw.attr,
	&dev_attr_touchkey_back_raw.attr,
	&dev_attr_touchkey_recent_idac.attr,
	&dev_attr_touchkey_back_idac.attr,
	&dev_attr_touchkey_threshold.attr,

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	&dev_attr_flip_mode.attr,
#endif
	&dev_attr_modecheck.attr,

	&dev_attr_touchkey_keycode.attr,
	&dev_attr_touchkey_3rd.attr,
	&dev_attr_touchkey_3rd_raw.attr,
	&dev_attr_touchkey_4th.attr,
	&dev_attr_touchkey_4th_raw.attr,
	&dev_attr_touchkey_debug0.attr,
	&dev_attr_touchkey_debug1.attr,
	&dev_attr_touchkey_debug2.attr,
	&dev_attr_touchkey_debug3.attr,

#if defined(FEATURE_GRIP_FOR_SAR) && !defined(CONFIG_TOUCHKEY_GRIP_SENSOR_OFF)
	&dev_attr_touchkey_grip_threshold.attr,
	&dev_attr_touchkey_total_cap.attr,
	&dev_attr_sar_enable.attr,
	&dev_attr_sw_reset.attr,
	&dev_attr_touchkey_earjack.attr,
	&dev_attr_touchkey_grip.attr,
	&dev_attr_touchkey_grip_baseline.attr,
	&dev_attr_touchkey_grip_raw.attr,
	&dev_attr_touchkey_grip_gain.attr,
	&dev_attr_touchkey_grip_check.attr,
	&dev_attr_touchkey_sar_only_mode.attr,
	&dev_attr_grip_sensing_enable.attr,
#if 0//ndef CONFIG_SAMSUNG_PRODUCT_SHIP
	&dev_attr_touchkey_sar_press_threshold.attr,
	&dev_attr_touchkey_sar_release_threshold.attr,
#endif
#endif
	&dev_attr_touchkey_chip_name.attr,
	NULL,
};

static struct attribute_group sec_touchkey_attr_group_350k = {
	.attrs = sec_touchkey_attributes_350k,
};

static struct attribute *sec_touchkey_attributes_brightness[] = {
	&dev_attr_brightness.attr,
	NULL,
};

static struct attribute_group sec_touchkey_attr_brightness = {
	.attrs = sec_touchkey_attributes_brightness,
};

#ifdef CONFIG_VBUS_NOTIFIER
static int tkey_vbus_notification(struct notifier_block *nb,
		unsigned long cmd, void *data)
{
	struct tc3xxk_data *tkey_data = container_of(nb, struct tc3xxk_data, vbus_nb);
	struct i2c_client *client;
	vbus_status_t vbus_type = *(vbus_status_t *)data;
	int ret;

	if (!tkey_data) {
		pr_err("%s %s: no device\n", SECLOG, __func__);
		return 0;
	}

	client = tkey_data->client;

	input_info(true, &client->dev, "%s cmd=%lu, vbus_type=%d\n", __func__, cmd, vbus_type);

	switch (vbus_type) {
	case STATUS_VBUS_HIGH:
		input_info(true, &client->dev, "%s : attach\n",__func__);
#ifdef FEATURE_GRIP_FOR_SAR
		// if sar_mode is on => must send wake-up command
		if (tkey_data->sar_mode) {
			ret = tc3xxk_wake_up(client, TC3XXK_CMD_WAKE_UP);
			if (ret < 0)
				input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
						__func__, ret);
		}
#endif
		ret = tc3xxk_mode_enable(client, TC3XXK_CMD_TA_ON);
		if (ret < 0)
			input_err(true, &client->dev, "[TK] %s TA mode ON fail(%d)\n", __func__, ret);
		break;
	case STATUS_VBUS_LOW:
		input_info(true, &client->dev, "%s : detach\n",__func__);
#ifdef FEATURE_GRIP_FOR_SAR
		// if sar_mode is on => must send wake-up command
		if (tkey_data->sar_mode) {
			ret = tc3xxk_wake_up(client, TC3XXK_CMD_WAKE_UP);
			if (ret < 0)
				input_err(true, &client->dev, "%s fail to wakeup(%d)\n",
						__func__, ret);
		}
#endif
		ret = tc3xxk_mode_enable(client, TC3XXK_CMD_TA_OFF);
		if (ret < 0)
			input_err(true, &client->dev, "[TK] %s TA mode OFF fail(%d)\n", __func__, ret);
		break;
	default:
		break;
	}

	tkey_data->vbus_state = vbus_type;
	return 0;
}

#endif

static int tc3xxk_connecter_check(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;

	if (!gpio_is_valid(data->dtdata->sub_det_gpio)) {
		input_err(true, &client->dev, "%s: Not use sub_det pin\n", __func__);
		return SUB_DET_DISABLE;

	} else {
		if (gpio_get_value(data->dtdata->sub_det_gpio)) {
			return SUB_DET_ENABLE_CON_OFF;
		} else {
			return SUB_DET_ENABLE_CON_ON;
		}

	}
}
static int tc3xxk_fw_check(struct tc3xxk_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	int tsk_connecter_status;
	bool force_update = false;

	tsk_connecter_status = tc3xxk_connecter_check(data);
	input_info(true, &data->client->dev, "%s: sub_det state %d\n",
			__func__, tsk_connecter_status);
#ifdef CONFIG_SEC_FACTORY
	if (tsk_connecter_status == SUB_DET_ENABLE_CON_OFF) {
		input_err(true, &client->dev, "%s : TSK IC is disconnected! skip probe(%d)\n",
				__func__, gpio_get_value(data->dtdata->sub_det_gpio));
		return -1;
	}
#endif
	ret = tc3xxk_get_fw_version(data, true);
	if (ret < 0) {
		input_err(true, &client->dev,
				"[TK] %s: i2c fail...[%d], addr[%d]\n",
				__func__, ret, data->client->addr);
		data->fw_ver = 0xFF;
	}

	if (data->fw_ver == 0xFF) {
		input_info(true, &client->dev,
				"[TK] fw version 0xFF, Excute firmware update!\n");
		force_update = true;
	} else {
		force_update = false;
	}

	ret = tc3xxk_fw_update(data, FW_INKERNEL, force_update);
	if (ret)
		input_err(true, &client->dev, "%s: fail to fw update\n", __func__);

	return ret;
}

static void tc3xxk_set_power(struct tc3xxk_data *data)
{
	struct tc3xxk_devicetree_data *dtdata = data->dtdata;
	struct device *dev = &data->client->dev;
	struct regulator *reg;

	reg = regulator_get(dev, "vtouch_2p2");
	if (IS_ERR(reg)) {
		input_err(true, dev, "Failed to get vtouch_2p2 regulator\n", __func__);
	} else {
		input_info(true, dev, "vtouch_2p2 regulator is get\n", __func__);
		data->pwr_reg = reg;
	}

	if (gpio_is_valid(dtdata->power_gpio)) {
		gpio_direction_output(dtdata->power_gpio, 0);
	}
}

static int tc3xxk_gpio_request(struct tc3xxk_data *data)
{
	struct tc3xxk_devicetree_data *dtdata = data->dtdata;
	struct device *dev = &data->client->dev;
	int ret = 0;

	ret = devm_gpio_request(dev, dtdata->irq_gpio, "touchkey_irq");
	if (ret)
		input_err(true, dev,
				"%s: Failed to request touchkey_irq\n", __func__);

	if (gpio_is_valid(dtdata->power_gpio)) {
		ret = devm_gpio_request(dev, dtdata->power_gpio, "touchkey_en");
		if (ret)
			input_err(true, dev,
					"%s: Failed to request touchkey_en\n", __func__);
	}

	if (gpio_is_valid(dtdata->led_gpio)) {
		ret = devm_gpio_request(dev, dtdata->led_gpio, "touchkey_led_en");
		if (ret)
			input_err(true, dev,
					"%s: Failed to request touchkey_led_en\n", __func__);
	}

	return ret;
}

static struct input_dev *tc3xxk_set_input_dev_data(struct tc3xxk_data *data)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev;
	int i;

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev) {
		input_err(ture, dev,
				"%s: Failed to allocate memory\n", __func__);
		goto out;
	}

	snprintf(data->phys, sizeof(data->phys),
			"%s/input0", dev_name(dev));
	input_dev->name = "sec_touchkey";
	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = dev;
	input_dev->open = tc3xxk_input_open;
	input_dev->close = tc3xxk_input_close;

	data->tsk_ev_val = tsk_ev;
	data->key_num = ARRAY_SIZE(tsk_ev)/2;
	input_info(true, dev, "number of keys = %d\n", data->key_num);

	set_bit(EV_KEY, input_dev->evbit);
	if (data->dtdata->use_tkey_led) {
		set_bit(EV_LED, input_dev->evbit);
		set_bit(LED_MISC, input_dev->ledbit);
	}
	for (i = 0; i < data->key_num; i++) {
		set_bit(data->tsk_ev_val[i].tsk_keycode, input_dev->keybit);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		input_info(true, dev,
				"keycode[%d]= %3d\n", i,
				data->tsk_ev_val[i].tsk_keycode);
#endif
	}

	input_set_drvdata(input_dev, data);

out:
	return input_dev;
}

static int tc3xxk_dev_pinctrl_configure(struct tc3xxk_data *data, bool active)
{
	struct pinctrl_state *set_state;
	int retval;

	if (!data->pinctrl)
		return -ENODEV;

	input_info(true, &data->client->dev, "%s: %s\n", __func__, active ? "ACTIVE" : "SUSPEND");

	set_state = pinctrl_lookup_state(data->pinctrl, active ? "active_state" : "suspend_state");
	if (IS_ERR(set_state)) {
		input_err(true, &data->client->dev, "%s: cannot get active state\n", __func__);
		return -EINVAL;
	}

	retval = pinctrl_select_state(data->pinctrl, set_state);
	if (retval) {
		input_err(true, &data->client->dev, "%s: cannot set pinctrl %s state\n",
				__func__, active ? "active" : "suspend");
		return -EINVAL;
	}

	return 0;
}


#ifdef CONFIG_OF
static int tc3xxk_parse_dt(struct device *dev,
		struct tc3xxk_devicetree_data *dtdata)
{
	struct device_node *np = dev->of_node;
	int ret;

	dtdata->scl_gpio = of_get_named_gpio(np, "coreriver,scl-gpio", 0);
	if (!gpio_is_valid(dtdata->scl_gpio)) {
		input_err(true, dev, "%s: Failed to get scl-gpio\n", __func__);
		return -EINVAL;
	}

	dtdata->sda_gpio = of_get_named_gpio(np, "coreriver,sda-gpio", 0);
	if (!gpio_is_valid(dtdata->scl_gpio)) {
		input_err(ture, dev, "%s: Failed to get sda-gpio\n", __func__);
		return -EINVAL;
	}

	dtdata->irq_gpio = of_get_named_gpio(np, "coreriver,irq-gpio", 0);
	if (!gpio_is_valid(dtdata->scl_gpio)) {
		input_err(true, dev, "%s: Failed to get irq-gpio\n", __func__);
		return -EINVAL;
	}

	dtdata->power_gpio = of_get_named_gpio(np, "coreriver,power-gpio", 0);
	if (!gpio_is_valid(dtdata->power_gpio))
		input_info(true, dev, "%s: Failed to get power-gpio\n",
				__func__);

	dtdata->led_gpio = of_get_named_gpio(np, "coreriver,led-gpio", 0);
	if (!gpio_is_valid(dtdata->led_gpio))
		input_info(true, dev, "%s: Failed to get led-gpio\n",
				__func__);

	dtdata->sub_det_gpio = of_get_named_gpio(np, "coreriver,sub-det-gpio", 0);
	if (!gpio_is_valid(dtdata->sub_det_gpio))
		input_info(true, dev, "%s: Failed to get sub-det-gpio\n",
				__func__);

	ret = of_property_read_string(np, "coreriver,fw-name", &dtdata->fw_name);
	if (ret)
		dtdata->fw_name = NULL;

	input_info(true, dev, "%s: fw_name is %s\n", __func__, dtdata->fw_name);

	ret = of_property_read_u32(np, "coreriver,sensing-ch-num",
			&dtdata->sensing_ch_num);
	if (ret) {
		input_err(true, dev, "%s: Failed to get sensing_ch_numn",
				__func__);
		return -EINVAL;
	}

	dtdata->tc300k = of_property_read_bool(np, "coreriver,tc300k");
	dtdata->bringup = of_property_read_bool(np, "coreriver,bringup");
	dtdata->use_tkey_led = of_property_read_bool(np, "coreriver,use-tkey-led");
	dtdata->boot_on_pwr = of_property_read_bool(np, "coreriver,boot-on-pwr");

	input_info(true, dev, "%s: scl:%d, sda:%d, irq:%d, vdd_gpio:%d, reg booton:%d, use led:%d"
			"(led_en:%d), sub_det:%d, sensing ch:%d, bringup:%d, tc300k:%d, fw name:%s\n",
			__func__, dtdata->scl_gpio, dtdata->sda_gpio, dtdata->irq_gpio, dtdata->power_gpio,
			dtdata->boot_on_pwr, dtdata->use_tkey_led, dtdata->led_gpio, dtdata->sub_det_gpio,
			dtdata->sensing_ch_num, dtdata->bringup, dtdata->tc300k, dtdata->fw_name);

	return 0;
}
#else
static int tc3xxk_parse_dt(struct device *dev,
		struct tc3xxk_devicetree_data *dtdata)
{
	return -ENODEV;
}
#endif

static int tc3xxk_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct tc3xxk_devicetree_data *dtdata;
	struct input_dev *input_dev;
	struct tc3xxk_data *data;
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		input_err(true, dev, "I2C not supported\n");
		return -EIO;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		input_err(true, dev,
				"%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	if (dev->of_node) {
		dtdata = devm_kzalloc(dev, sizeof(*dtdata), GFP_KERNEL);
		if (!dtdata) {
			input_err(true, dev,
					"%s: Failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = tc3xxk_parse_dt(dev, dtdata);
		if (ret) {
			input_err(true, dev,
					"%s: Failed to parse device tree\n", __func__);
			return ret;
		}
	} else {
		dtdata = dev->platform_data;

		if (!dtdata) {
			input_err(true, dev, "Invalid dtdata\n");
			return -ENOMEM;
		}
	}

	data->dtdata = dtdata;
	data->client = client;

	/* Get pinctrl if target uses pinctrl */
	data->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(data->pinctrl)) {
		if (PTR_ERR(data->pinctrl) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		input_err(true, &client->dev, "%s: Target does not use pinctrl\n", __func__);
		data->pinctrl = NULL;
	}

	tc3xxk_dev_pinctrl_configure(data, true);

	input_dev = tc3xxk_set_input_dev_data(data);
	if (!input_dev) {
		input_err(ture, dev, "Failed to set input device data\n");
		return -ENOMEM;
	}

#ifndef FEATURE_GRIP_FOR_SAR
	INIT_WORK(&data->start_work, tc3xxk_start_work);
#endif
	data->input_dev = input_dev;

	i2c_set_clientdata(client, data);

	ret = tc3xxk_gpio_request(data);
	if (ret) {
		input_err(ture, dev, "Failed to request gpio\n");
		return ret;
	}

	tc3xxk_set_power(data);

	data->power = tc3xxk_touchkey_power;

	if (!dtdata->boot_on_pwr) {
		data->power(data, true);
		msleep(TC3XXK_POWERON_DELAY);
	}

	data->enabled = true;

	mutex_init(&data->lock);

	ret = tc3xxk_fw_check(data);
	if (ret) {
		input_err(true, dev, "Failed to firmware check\n");
		goto out;
	}

	ret = input_register_device(input_dev);
	if (ret) {
		input_err(true, dev, "Failed to register input_dev\n");
		goto out;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL, tc3xxk_interrupt,
			IRQF_DISABLED | IRQF_TRIGGER_LOW |
			IRQF_ONESHOT, TC3XXK_NAME, data);
	if (ret < 0) {
		input_err(true, dev, "Failed to request irq\n");
		goto out;
	}

	mutex_init(&data->lock_fac);
#ifdef FEATURE_GRIP_FOR_SAR
	wake_lock_init(&data->touchkey_wake_lock, WAKE_LOCK_SUSPEND, "touchkey wake lock");
#ifdef CONFIG_PM
	init_completion(&data->resume_done);
#endif
#endif

#ifdef CONFIG_SEC_SYSFS
	data->sec_touchkey = sec_device_create(
			(dev_t)(uintptr_t)&data->sec_touchkey,
			data, "sec_touchkey");
#else
	data->sec_touchkey = device_create(sec_class, NULL,
			(dev_t)(uintptr_t)&data->sec_touchkey, data, "sec_touchkey");
#endif
	if (IS_ERR(data->sec_touchkey))
		input_err(true, dev,
				"Failed to create device for the touchkey sysfs\n");

	if (!data->dtdata->tc300k) {
		ret = sysfs_create_group(&data->sec_touchkey->kobj,
				&sec_touchkey_attr_group_350k);
	} else {
		ret = sysfs_create_group(&data->sec_touchkey->kobj,
				&sec_touchkey_attr_group);
	}

	if (ret)
		input_err(ture, dev, "Failed to create sysfs group\n");

	if (data->dtdata->use_tkey_led) {
		ret = sysfs_create_group(&data->sec_touchkey->kobj,
				&sec_touchkey_attr_brightness);
		if (ret)
			input_err(true, dev,
					"Failed to create brightness sysfs\n");
	}

	ret = sysfs_create_link(&data->sec_touchkey->kobj,
			&input_dev->dev.kobj, "input");
	if (ret < 0)
		input_err(ture, dev, "Failed to create input symbolic link\n");

	dev_set_drvdata(data->sec_touchkey, data);

#ifdef CONFIG_VBUS_NOTIFIER
	vbus_notifier_register(&data->vbus_nb, tkey_vbus_notification,
			VBUS_NOTIFY_DEV_CHARGER);
#endif

#ifdef CONFIG_TOUCHKEY_GRIP_SENSOR_OFF
	tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#endif
	ret = tc3xxk_mode_check(client);
	if (ret >= 0) {
#ifdef FEATURE_GRIP_FOR_SAR
		data->sar_enable = !!(ret & TC3XXK_MODE_SAR);
		data->grip_sensing = !!(ret & TC3XXK_MODE_GRIP_SENSING);
		input_info(true, dev, "%s: mode %02X, sar %d, grip_sensing %d\n",
				__func__, ret, data->sar_enable, data->grip_sensing);

		device_init_wakeup(dev, true);
		input_info(true, dev, "%s: touchkey grip feature enabled\n", __func__);
#else
		data->grip_sensing_status = !!(ret & TC3XXK_MODE_GRIP_SENSING);
		input_info(true, dev, "%s: mode %02X, grip_sensing %d\n",
				__func__, ret, data->grip_sensing_status);

		if (data->grip_sensing_status)
			tc3xxk_mode_enable(data->client, TC3XXK_CMD_GRIP_SENSING_OFF);
#endif
	}

	input_info(true, dev, "%s: done\n", __func__);
	return 0;

out:
	mutex_destroy(&data->lock);

	return ret;
}

static int tc3xxk_remove(struct i2c_client *client)
{
	struct tc3xxk_data *data = i2c_get_clientdata(client);

#ifdef FEATURE_GRIP_FOR_SAR
	device_init_wakeup(&client->dev, false);
	wake_lock_destroy(&data->touchkey_wake_lock);
#else
	cancel_work_sync(&data->start_work);
#endif
	mutex_destroy(&data->lock);
	mutex_destroy(&data->lock_fac);

	return 0;
}

static void tc3xxk_shutdown(struct i2c_client *client)
{
	struct tc3xxk_data *data = i2c_get_clientdata(client);

#ifdef FEATURE_GRIP_FOR_SAR
	device_init_wakeup(&client->dev, false);
	wake_lock_destroy(&data->touchkey_wake_lock);
#else
	cancel_work_sync(&data->start_work);
#endif
	mutex_destroy(&data->lock);
	mutex_destroy(&data->lock_fac);

	data->power(data, false);
}

#ifndef FEATURE_GRIP_FOR_SAR
static int tc3xxk_stop(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tc3xxk_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->lock);

	if (!data->enabled) {
		mutex_unlock(&data->lock);
		return 0;
	}

	input_info(true, &data->client->dev, "[TK] %s: users=%d\n",
			__func__, data->input_dev->users);

	disable_irq(client->irq);
	data->enabled = false;
	tc3xxk_release_all_fingers(data);
	data->power(data, false);
	data->led_on = false;

	mutex_unlock(&data->lock);

	return 0;
}

static int tc3xxk_start(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tc3xxk_data *data = i2c_get_clientdata(client);
	u8 cmd;

	mutex_lock(&data->lock);
	if (data->enabled) {
		mutex_unlock(&data->lock);
		return 0;
	}
	input_info(true, &data->client->dev, "[TK] %s: users=%d\n", __func__, data->input_dev->users);

	data->power(data, true);
	msleep(TC3XXK_POWERON_DELAY);
	enable_irq(client->irq);

	data->enabled = true;
	if (data->led_on == true) {
		data->led_on = false;
		input_info(true, &client->dev, "[TK] led on(resume)\n");
		cmd = TC3XXK_CMD_LED_ON;
		tc3xxk_mode_enable(client, cmd);
	}

#ifndef CONFIG_KEYBOARD_STMPE1801_POGO
	if (data->flip_mode)
		tc3xxk_mode_enable(client, TC3XXK_CMD_FLIP_ON);
#endif
	if (data->glove_mode)
		tc3xxk_mode_enable(client, TC3XXK_CMD_GLOVE_ON);

	if (data->grip_sensing_status)
		tc3xxk_mode_enable(client, TC3XXK_CMD_GRIP_SENSING_OFF);

#ifdef CONFIG_VBUS_NOTIFIER
	if (data->vbus_state == STATUS_VBUS_HIGH)
		tc3xxk_mode_enable(client, TC3XXK_CMD_TA_ON);
#endif

	mutex_unlock(&data->lock);

	return 0;
}

static void tc3xxk_start_work(struct work_struct *work)
{
	struct tc3xxk_data *data =
		container_of(work, struct tc3xxk_data, start_work);

	tc3xxk_start(&data->client->dev);
}
#endif

static void tc3xxk_input_close(struct input_dev *dev)
{
	struct tc3xxk_data *data = input_get_drvdata(dev);

#ifdef FEATURE_GRIP_FOR_SAR
	input_info(true, &data->client->dev,
			"%s: sar_enable:%d\n", __func__, data->sar_enable);
	tc3xxk_stop_mode(data, 1);

	if (device_may_wakeup(&data->client->dev))
		enable_irq_wake(data->client->irq);
#else
	cancel_work_sync(&data->start_work);
	input_info(true, &data->client->dev, "[TK] %s: users=%d\n", __func__,
			data->input_dev->users);
	tc3xxk_stop(&data->client->dev);
#endif
	tc3xxk_touchkey_led_power(data, false);
}

static int tc3xxk_input_open(struct input_dev *dev)
{
	struct tc3xxk_data *data = input_get_drvdata(dev);

	tc3xxk_touchkey_led_power(data, true);

#ifdef FEATURE_GRIP_FOR_SAR
	input_info(true, &data->client->dev,
			"%s: sar_enable:%d\n", __func__, data->sar_enable);
	tc3xxk_stop_mode(data, 0);

	if (device_may_wakeup(&data->client->dev))
		disable_irq_wake(data->client->irq);
#else
	cancel_work_sync(&data->start_work);
	input_info(true, &data->client->dev, "[TK] %s: users=%d\n", __func__,
			data->input_dev->users);

	schedule_work(&data->start_work);
#endif

	return 0;
}

#ifdef CONFIG_PM
static int tc3xxk_suspend(struct device *dev)
{
#ifdef FEATURE_GRIP_FOR_SAR
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	data->is_lpm_suspend = true;
	reinit_completion(&data->resume_done);
#endif
	return 0;
}

static int tc3xxk_resume(struct device *dev)
{
#ifdef FEATURE_GRIP_FOR_SAR
	struct tc3xxk_data *data = dev_get_drvdata(dev);

	data->is_lpm_suspend = false;
	complete_all(&data->resume_done);
#endif
	return 0;
}

static const struct dev_pm_ops tc3xxk_pm_ops = {
	.suspend = tc3xxk_suspend,
	.resume = tc3xxk_resume,
};
#endif

static const struct i2c_device_id tc3xxk_id[] = {
	{TC3XXK_NAME, 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tc3xxk_id);

#ifdef CONFIG_OF
static struct of_device_id coreriver_match_table[] = {
	{ .compatible = "coreriver,tc3xxk",},
	{ },
};
#endif

static struct i2c_driver tc3xxk_driver = {
	.probe = tc3xxk_probe,
	.remove = tc3xxk_remove,
	.shutdown = tc3xxk_shutdown,
	.driver = {
		.name = TC3XXK_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &tc3xxk_pm_ops,
#endif
		.of_match_table = of_match_ptr(coreriver_match_table),
	},
	.id_table = tc3xxk_id,
};

static int __init tc3xxk_init(void)
{
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (poweroff_charging) {
		pr_notice("%s %s : LPM Charging Mode!!\n", SECLOG, __func__);
		return 0;
	}
#endif
	return i2c_add_driver(&tc3xxk_driver);
}

static void __exit tc3xxk_exit(void)
{
	i2c_del_driver(&tc3xxk_driver);
}

late_initcall(tc3xxk_init);
module_exit(tc3xxk_exit);

MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("Touchkey driver for Coreriver TC3XXK series");
MODULE_LICENSE("GPL");
