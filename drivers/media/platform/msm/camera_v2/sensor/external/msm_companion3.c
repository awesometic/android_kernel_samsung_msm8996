/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "msm_companion3.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"
#include "msm_camera_dt_util.h"
#include "msm_camera_i2c_mux.h"
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/msm_cam_sensor.h>
#include <media/msmb_pproc.h>
#include <linux/crc32.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/module.h>

#if defined(CONFIG_SEC_GRACEQLTE_PROJECT)
#include <media/companion3_reg_map_grace.h>
#else
#include <media/companion3_reg_map.h>
#endif

#ifdef BYPASS_COMPANION
#include "isp073Cfw_spi.h"
#include "concordmaster_spi.h"
#include "mode1_spi.h"  // for stats2
//#include "3_Mode2_8.h"  // for stats1
#else
#ifndef MSM_CC_FW
#include "isp073Cfw2_spi.h"
#include "concordmaster2_spi.h"
#include "3_Mode2_1.h"  // for stats2
//#include "3_Mode2_8.h"  // for stats1
#else
#include <linux/vmalloc.h>
#endif
#endif

#define BURST_WRITE_ENABLE
#define USE_STATIC_BUF
#ifdef USE_STATIC_BUF
#define COMPANION_FW_MAX_SIZE 256000   // 250k
#endif
//#define SKIP_CAL_CRC

#undef CDBG
//#define CONFIG_MSMB_CAMERA_DEBUG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#undef CDBG_SPI
//#define CONFIG_MSM_CC_SPI_TRANS_DEBUG
#ifdef CONFIG_MSM_CC_SPI_TRANS_DEBUG
#define CDBG_SPI(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG_SPI(fmt, args...) do { } while (0)
#endif

#undef CDBG_FW
//#define CONFIG_MSM_CC_DEBUG
#ifdef CONFIG_MSM_CC_DEBUG
#define CDBG_FW(fmt, args...) pr_err("[syscamera-fw][%s::%d]"fmt, __FUNCTION__, __LINE__, ##args)
#else
#define CDBG_FW(fmt, args...) do { } while (0)
#endif

#define MSM_COMP_DRV_NAME                    "msm_companion"
#define MSM_COMP_POLL_RETRIES		20
#if 0
static const char* ISP_COMPANION_BINARY_PATH = "/data/log/CamFW_Companion.bin";
#else
static const char* ISP_COMPANION_BINARY_PATH = "/data/media/0/CamFW_Companion.bin";
#endif

//  Enable to use default FW and master set file at /sdcard folder for test purpose only.
//#define USE_C3_FW_AT_SDCARD_FOLDER

extern struct regulator *pwr_binning_reg;

#if defined(CONFIG_EXTCON) && defined(CONFIG_BATTERY_SAMSUNG)
extern int poweroff_charging;
#endif
int wdr_mode;

bool retention_mode = 0;
bool retention_mode_pwr = 0;
uint32_t fw_crc_retention;
uint32_t fw_crc_size_retention;

uint8_t fw_name[2][2][64];
#ifdef USE_STATIC_BUF
static u8 spi_isp_buf[COMPANION_FW_MAX_SIZE] = {0,};
#endif

#ifdef CONFIG_COMPAT
static struct v4l2_file_operations msm_companion_v4l2_subdev_fops;
#endif
extern char fw_crc[10];

struct msm_camera_i2c_client *g_i2c_client;

int comp_fac_i2c_check = -1;
uint16_t comp_fac_valid_check;

static int msm_companion_get_crc(struct companion_device *companion_dev, struct companion_crc_check_param crc_param, int callByKernel);
static int msm_companion_release(struct companion_device *companion_dev);
static int msm_companion_fw_write(struct companion_device *companion_dev);


static atomic_t comp_streamon_set;

static int msm_companion_dump_register(struct companion_device *companion_dev, uint8_t *buffer)
{
	struct msm_camera_i2c_client *client = NULL;


	CDBG("[syscamera][%s::%d] Enter\n", __FUNCTION__, __LINE__);

	if (companion_dev) {
		client = &companion_dev->companion_i2c_client;
	} else {
		pr_err("[syscamera][%s::%d]companion_dev is null\n", __FUNCTION__, __LINE__);
		return 0;
	}

	client->i2c_func_tbl->i2c_write(
		client, 0x642c, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	client->i2c_func_tbl->i2c_write(
		client, 0x642e, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	client->i2c_func_tbl->i2c_read_multi(
		client, 0x8AFA, buffer);

	return 0;
}

static int msm_companion_get_string_length(uint8_t *str)
{
	int size = 0;

	while(size < 64 &&  str[size] != 0) {
		size++;
	}
	return size;
}

static int msm_companion_append_string(uint8_t *str, int offset, uint8_t *substr, int size)
{
	int i;

	if(str == NULL || substr == NULL) {
		pr_err("[%s::%d] NULL buffer error ! (str = %p, substr = %p)", __FUNCTION__, __LINE__, str, substr);
		return 0;
	}

	if(offset + size > 64 || str == NULL || substr == NULL) {
		pr_err("[%s::%d] string overflow ! (offset = %d, size = %d)", __FUNCTION__, __LINE__, offset, size);
		return 0;
	}

	for(i = 0; i < size; i++)
		str[offset+i] = substr[i];

	return 1;
}

static int msm_companion_fw_binary_set(struct companion_device *companion_dev, struct companion_fw_binary_param fw_bin,
	uint8_t *sensor_name)
{
	long ret = 0;

	// Setting fw binary
	if(fw_bin.size != 0 && fw_bin.buffer != NULL)
	{
		if(companion_dev->eeprom_fw_bin != NULL && companion_dev->eeprom_fw_bin_size > 0)
		{
			CDBG("[syscamera][%s::%d] eeprom_fw_bin is already set (bin_size = %d). return!\n",
				__FUNCTION__, __LINE__, companion_dev->eeprom_fw_bin_size);
		}
		else
		{
			if(companion_dev->eeprom_fw_bin != NULL)
				kfree(companion_dev->eeprom_fw_bin);

			companion_dev->eeprom_fw_bin_size = 0;
			companion_dev->eeprom_fw_bin = NULL;

			// Copy fw binary buffer from user-space area
			companion_dev->eeprom_fw_bin = (uint8_t *)kmalloc(fw_bin.size, GFP_KERNEL);
			if (!companion_dev->eeprom_fw_bin) {
				pr_err("[syscamera][%s::%d][Error] Memory allocation fail\n", __FUNCTION__, __LINE__);
				return -ENOMEM;
			}

			if (copy_from_user(companion_dev->eeprom_fw_bin, fw_bin.buffer, fw_bin.size)) {
				pr_err("[syscamera][%s::%d][Error] buffer from user space\n", __FUNCTION__, __LINE__);
				kfree(companion_dev->eeprom_fw_bin);
				return -EFAULT;
			}

			companion_dev->eeprom_fw_bin_size = fw_bin.size;
		}
	}

	if (copy_from_user(companion_dev->eeprom_fw_ver, fw_bin.version, 12)) {
		pr_err("[syscamera][%s::%d][Error] Failed to copy version info from user-space\n", __FUNCTION__, __LINE__);
		return -EFAULT;
	}

	pr_err("[%s:%d] Version string from EEPROM = %s, bin size = %d", __FUNCTION__, __LINE__, companion_dev->eeprom_fw_ver, companion_dev->eeprom_fw_bin_size);

	//Updating path for fw binary (companion_fw_path + sensor_version + companion_fw_name + sensor_name + extension)
	{
		char *sensor_version, *extension;
		char version[6];
		int fw_p, fw_n, offset, size;

		CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

		if (copy_from_user(version, fw_bin.hwinfo, 6)) {
			pr_err("[syscamera][%s::%d][Error] Failed to copy hwinfo from user-space\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		version[5] = 0;
		sensor_version = version;
		pr_err("[%s:%d] Valid hw version info : %s\n", __FUNCTION__, __LINE__, sensor_version);

		// Get sensor name
		CDBG("[syscamera][%s::%d] sensor_name : %s\n", __FUNCTION__, __LINE__, sensor_name);

		//Get extension
		extension = FW_EXTENSION;

		// Creating path table
		for(fw_p = 0; fw_p < FW_PATH_MAX; fw_p++) {
			for(fw_n = 0; fw_n < FW_NAME_MAX; fw_n++) {
				offset = 0;
				size = 0;

				// Add path to stringfw_p
				size = msm_companion_get_string_length(companion_fw_path[fw_p]);
				//pr_err("[syscamera][%s::%d] offset = %d, size = %d\n", __FUNCTION__, __LINE__, offset, size);
				if(msm_companion_append_string(fw_name[fw_p][fw_n], offset, companion_fw_path[fw_p], size) == 0) {
					pr_err("[syscamera][%s::%d][Error] fail to appending path string\n", __FUNCTION__, __LINE__);
					return -EFAULT;
				}
				offset+= size;

				if(fw_n != FW_NAME_MASTER)
				{
					// Add sensor version to string
					size = msm_companion_get_string_length(sensor_version);
					//pr_err("[syscamera][%s::%d] offset = %d, size = %d\n", __FUNCTION__, __LINE__, offset, size);

					if(msm_companion_append_string(fw_name[fw_p][fw_n], offset, sensor_version, size) == 0)
					{
						pr_err("[syscamera][%s::%d][Error] fail to appending sensor version string\n", __FUNCTION__, __LINE__);
						return -EFAULT;
					}
					offset+= size;
				}

				// Add name to string
				size = msm_companion_get_string_length(companion_fw_name[fw_n]);
				//pr_err("[syscamera][%s::%d] offset = %d, size = %d\n", __FUNCTION__, __LINE__, offset, size);
				if(msm_companion_append_string(fw_name[fw_p][fw_n], offset, companion_fw_name[fw_n], size) == 0) {
					pr_err("[syscamera][%s::%d][Error] fail to appending fw name string\n", __FUNCTION__, __LINE__);
					return -EFAULT;
				}
				offset+= size;

				if(fw_n != FW_NAME_MASTER)
				{
				   // Add sensor name to string
				   size = msm_companion_get_string_length(sensor_name);
				   //pr_err("[syscamera][%s::%d] offset = %d, size = %d\n", __FUNCTION__, __LINE__, offset, size);

					if(msm_companion_append_string(fw_name[fw_p][fw_n], offset, sensor_name, size) == 0) {
						pr_err("[syscamera][%s::%d][Error] fail to appending sensor name string\n", __FUNCTION__, __LINE__);
						return -EFAULT;
					}
					offset+= size;
				}

				// Add extension to string
				size = msm_companion_get_string_length(extension);
				//pr_err("[syscamera][%s::%d] offset = %d, size = %d\n", __FUNCTION__, __LINE__, offset, size);
				if(msm_companion_append_string(fw_name[fw_p][fw_n], offset, extension, size) == 0) {
					pr_err("[syscamera][%s::%d][Error] fail to appending extension string\n", __FUNCTION__, __LINE__);
					return -EFAULT;
				}
				offset+= size;

				if(fw_p == FW_PATH_SD && fw_n == FW_NAME_ISP)
					snprintf(fw_name[fw_p][fw_n], 64, "%s", ISP_COMPANION_BINARY_PATH);

				// print debug message
				pr_err("[syscamera][%s::%d] PathIDX = %d, NameIDX = %d, path = %s\n", __FUNCTION__, __LINE__, fw_p, fw_n, fw_name[fw_p][fw_n]);
			}
		}
	}
	retention_mode = 0;
	CDBG("[syscamera][%s::%d][X]\n", __FUNCTION__, __LINE__);

	return ret;
}

static int msm_companion_set_cal_tbl(struct companion_device *companion_dev, struct msm_camera_i2c_reg_setting cal_tbl)
{
	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

#if 0
	if(companion_dev->companion_cal_tbl != NULL) {
		kfree(companion_dev->companion_cal_tbl);
		companion_dev->companion_cal_tbl = NULL;
		companion_dev->companion_cal_tbl_size = 0;
		//pr_err("[syscamera][%s::%d] companion_cal_tbl is not NULL. return!\n", __FUNCTION__, __LINE__);
		//return -1;
	}
#else
	if(companion_dev->companion_cal_tbl != NULL) {
		pr_err("[syscamera][%s::%d] companion_cal_tbl is already set. return!\n", __FUNCTION__, __LINE__);
		return 0;
	}
#endif

	// Allocate memory for the calibration table
	companion_dev->companion_cal_tbl = (struct msm_camera_i2c_reg_array *)kmalloc(sizeof(struct msm_camera_i2c_reg_array) * cal_tbl.size, GFP_KERNEL);
	if (!companion_dev->companion_cal_tbl) {
		pr_err("[syscamera][%s::%d] Memory allocation fail\n", __FUNCTION__, __LINE__);
		companion_dev->companion_cal_tbl_size = 0;
		return -ENOMEM;
	}

	// Copy table from user-space area
	if (copy_from_user(companion_dev->companion_cal_tbl, (void *)cal_tbl.reg_setting, sizeof(struct msm_camera_i2c_reg_array)* cal_tbl.size)) {
		pr_err("[syscamera][%s::%d] failed to copy mode table from user-space\n", __FUNCTION__, __LINE__);
		kfree(companion_dev->companion_cal_tbl);
		companion_dev->companion_cal_tbl = NULL;
		companion_dev->companion_cal_tbl_size = 0;
		return -EFAULT;
	}
	companion_dev->companion_cal_tbl_size = cal_tbl.size;
	CDBG("[syscamera][%s::%d][X]\n", __FUNCTION__, __LINE__);

	return 0;
}

static int msm_companion_read_cal_tbl(struct companion_device *companion_dev, uint32_t offset, uint32_t read_size)
{
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	int rc = 0;

	if ((companion_dev->device_type != MSM_CAMERA_SPI_DEVICE) ||
		(client->spi_client == NULL)) {
		return rc;
	}

	rc = client->i2c_func_tbl->i2c_write(
	client, 0x642C, (offset & 0xFFFF0000) >> 16, MSM_CAMERA_I2C_WORD_DATA);
	if(rc < 0)
		pr_err("[syscamera][%s::%d] i2c_write failed.\n", __FUNCTION__, __LINE__);

	rc = client->i2c_func_tbl->i2c_write(
	client, 0x642E, (offset & 0xFFFF), MSM_CAMERA_I2C_WORD_DATA);
	if(rc < 0)
		pr_err("[syscamera][%s::%d] i2c_write failed.\n", __FUNCTION__, __LINE__);

	rc = client->i2c_func_tbl->i2c_read_multi(
	client, read_size, cal_data);
	if(rc < 0)
		pr_err("[syscamera][%s::%d] i2c_read_burst failed.\n", __FUNCTION__, __LINE__);

	return 0;
}

static int msm_companion_compare_FW_crc(struct companion_device *companion_dev)
{
	int ret = 0;
	uint32_t crc32;
	struct companion_crc_check_param crc_param;

	//  check crc inside companion FW after upload FW to companion.
	crc_param.addr = 0x80000;
	crc_param.count = fw_crc_size_retention-4;
	crc_param.CRC = &crc32;

	ret = msm_companion_get_crc(companion_dev, crc_param, 1);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] msm_companion_get_crc failed\n", __FUNCTION__, __LINE__);
		return -EIO;
	}

	CDBG("[syscamera][%s::%d] companion : 0x%08X vs AP : 0x%08X\n", __FUNCTION__, __LINE__, *crc_param.CRC, fw_crc_retention);

	if(*crc_param.CRC != fw_crc_retention)
	{
		msm_camera_fw_check('F', CHECK_COMPANION_FW); //F: Fail
		pr_err("[syscamera][%s::%d] msm_companion_get_crc failed.\n", __FUNCTION__, __LINE__);
		return -EFAULT;
	}

	return ret;
}

static int msm_companion_cal_data_write(struct companion_device *companion_dev)
{
	int ret = 0; //, idx;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	struct msm_camera_i2c_reg_array * cal_tbl = companion_dev->companion_cal_tbl;

	CDBG("[syscamera][%s::%d] writing cal table to companion chip hw (cal_tbl_size = %d)\n",
		__FUNCTION__, __LINE__, companion_dev->companion_cal_tbl_size);

	if(cal_tbl == NULL) {
		pr_err("[syscamera][%s::%d] cal table is empty. returning function.\n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}

#if 0
	// Writing cal data to companion chip hw in burst mode
	ret = client->i2c_func_tbl->i2c_write_burst(client,
		cal_tbl, companion_dev->companion_cal_tbl_size, MAX_SPI_SIZE);
#else
	{
		uint8_t  *spi_buffer;
		uint16_t  *buffer_16;
		struct spi_message m;
		struct spi_transfer tx;
		uint32_t  i, idx = 0;
		uint32_t  spi_trans_size = 2*companion_dev->companion_cal_tbl_size-1;

		spi_buffer = vmalloc(spi_trans_size);
		if (!spi_buffer) {
			pr_err("[syscamera][%s::%d] spi_buffer vmalloc failed.\n", __FUNCTION__, __LINE__);
			return -ENOMEM;
		}
		pr_err("%s:%d spi_trans_size = %d", __FUNCTION__, __LINE__, spi_trans_size);

		spi_buffer[0] = 0x02;
		spi_buffer[1] = 0x6F;
		spi_buffer[2] = 0x12;

		for(idx = 2, i = 3; i < spi_trans_size; idx ++, i += 2)
		{
			buffer_16 = (uint16_t *)&spi_buffer[i];
			*buffer_16 = cal_tbl[idx].reg_data;

			CDBG_SPI("%s:%d spi_buffer[%d] = cal_tbl[%d]", __FUNCTION__, __LINE__, i, idx);
			CDBG_SPI("%s:%d *buffer_16 = 0x%04X, spi_buffer = 0x%02X, 0x%02X, cal_tbl[%d] = 0x%04X",
				__FUNCTION__, __LINE__, *buffer_16, spi_buffer[i], spi_buffer[i+1], idx, cal_tbl[idx].reg_data);
		}

		ret = client->i2c_func_tbl->i2c_write(
			client, 0x6428, cal_tbl[0].reg_data, MSM_CAMERA_I2C_WORD_DATA);

		ret = client->i2c_func_tbl->i2c_write(
			client, 0x642A, cal_tbl[1].reg_data, MSM_CAMERA_I2C_WORD_DATA);

		CDBG_SPI("%s:%d addr = 0x%X, write = 0x%08X\n", __FUNCTION__, __LINE__, cal_tbl[0].reg_addr, cal_tbl[0].reg_data);
		CDBG_SPI("%s:%d addr = 0x%X, write = 0x%08X\n", __FUNCTION__, __LINE__, cal_tbl[1].reg_addr, cal_tbl[1].reg_data);
		CDBG_SPI("%s:%d addr = 0x%X, write = 0x%08X\n", __FUNCTION__, __LINE__, cal_tbl[2].reg_addr, cal_tbl[2].reg_data);
		CDBG_SPI("%s:%d addr = 0x%X, write = 0x%08X\n", __FUNCTION__, __LINE__, cal_tbl[3].reg_addr, cal_tbl[3].reg_data);

		for(i = 0; i < spi_trans_size; i ++)
			CDBG_SPI("%s:%d spi_buffer[%d] = 0x%04X\n", __FUNCTION__, __LINE__, i, spi_buffer[i]);

		memset(&tx, 0, sizeof(struct spi_transfer));
		tx.tx_buf = spi_buffer;
		tx.len = spi_trans_size;

		spi_message_init(&m);
		spi_message_add_tail(&tx, &m);
		ret = spi_sync(client->spi_client->spi_master, &m);

		if(spi_buffer)
			vfree(spi_buffer);
	}
#endif
	return ret;
}

static int msm_companion_pll_init(struct companion_device *companion_dev)
{
	long ret = 0;
	u16 read_value = 0xFFFF;
	u16 bin_info = 0xFFFF, wafer_info = 0xFFFF;
	u32 binning_voltage = 825000;
	struct msm_camera_i2c_client *client = NULL;
	char isp_core[10];

	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

	if (companion_dev)
		client = &companion_dev->companion_i2c_client;
	else {
		pr_err("[syscamera][%s::%d][ERROR][companion_dev is NULL]\n", __FUNCTION__, __LINE__);
		return -EIO;
	}

	// Read Device ID
	ret = client->i2c_func_tbl->i2c_read(client, 0x0000, &read_value, MSM_CAMERA_I2C_WORD_DATA);
        comp_fac_i2c_check = ret;
        comp_fac_valid_check = read_value;
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, read_value, ret);
		return -EIO;
	}

	if (read_value != 0x73C3) {
		pr_err("[syscamera][%s::%d][PID::0x%4x] Device ID failed\n", __FUNCTION__, __LINE__, read_value);
		return -EIO;
	}
	CDBG("[syscamera][%s::%d][PID::0x%4x] Device ID ok\n", __FUNCTION__, __LINE__, read_value);

	//Power Binning
	//1. Read BIN_INFO
	ret = client->i2c_func_tbl->i2c_write(client, 0x602C, 0x5000, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] I2C[0x602C] write fail [rc::%ld]\n", __FUNCTION__, __LINE__, ret);
		return -EIO;
	}
	ret = client->i2c_func_tbl->i2c_write(client, 0x602E, 0x5002, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] I2C[0x602E] write fail [rc::%ld]\n", __FUNCTION__, __LINE__, ret);
		return -EIO;
	}
	ret = client->i2c_func_tbl->i2c_read(client, 0x6F12, &bin_info, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][0x6F12::0x%04x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, bin_info, ret);
		return -EIO;
	}
	pr_info("[syscamera][%s::%d][BIN_INFO::0x%04x]\n", __FUNCTION__, __LINE__, bin_info);

	//2. Read WAFER_INFO
	ret = client->i2c_func_tbl->i2c_write(client, 0x602C, 0x5000, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] I2C[0x602C] write fail [rc::%ld]\n", __FUNCTION__, __LINE__, ret);
		return -EIO;
	}
	ret = client->i2c_func_tbl->i2c_write(client, 0x602E, 0x5018, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] I2C[0x602E] write fail [rc::%ld]\n", __FUNCTION__, __LINE__, ret);
		return -EIO;
	}
	ret = client->i2c_func_tbl->i2c_read(client, 0x6F12, &wafer_info, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][0x6F12::0x%04x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, wafer_info, ret);
		return -EIO;
	}
	pr_info("[syscamera][%s::%d][WAFER_INFO::0x%04x]\n", __FUNCTION__, __LINE__, wafer_info);

	if (bin_info & 0x3F) {
		if (bin_info & (1<<CC_BIN6)) {
			binning_voltage = 825000;
			strncpy(isp_core, "0.825", sizeof(isp_core));
		} else if (bin_info & (1<<CC_BIN5)) {
			binning_voltage = 800000;
			strncpy(isp_core, "0.800", sizeof(isp_core));
		} else if (bin_info & (1<<CC_BIN4)) {
			binning_voltage = 775000;
			strncpy(isp_core, "0.775", sizeof(isp_core));
		} else if (bin_info & (1<<CC_BIN3)) {
			binning_voltage = 750000;
			strncpy(isp_core, "0.750", sizeof(isp_core));
		} else if (bin_info & (1<<CC_BIN2)) {
			binning_voltage = 725000;
			strncpy(isp_core, "0.725", sizeof(isp_core));
		} else if (bin_info & (1<<CC_BIN1)) {
			binning_voltage = 700000;
			strncpy(isp_core, "0.700", sizeof(isp_core));
		} else {
			binning_voltage = 825000;
			strncpy(isp_core, "0.825", sizeof(isp_core));
		}
	} else {
		if (((wafer_info & 0x3E0) >> 5) == 0x11) {
			binning_voltage = 825000;
			strncpy(isp_core, "0.825", sizeof(isp_core));
		} else {
			binning_voltage = 800000;
			strncpy(isp_core, "0.800", sizeof(isp_core));
		}
		pr_warn("[syscamera][%s::%d] Old hw version \n", __FUNCTION__, __LINE__);
	}

	if (pwr_binning_reg) {
		int rc = 0;
		pr_err("[syscamera][%s::%d] cam_vdig_comp voltage setting: %d for pwr binning\n",
			__FUNCTION__, __LINE__, binning_voltage);
		rc = regulator_set_voltage(pwr_binning_reg, binning_voltage, binning_voltage);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] cam_vdig_comp set voltage failed\n", __FUNCTION__, __LINE__);
		}
	} else {
		strncpy(isp_core, "0.825", sizeof(isp_core));
		pr_err("[syscamera][%s::%d] pwr_binning_reg is NULL. Use default voltage\n", __FUNCTION__, __LINE__);
	}

	pr_err("[syscamera][%s::%d][BIN_INFO::0x%04x][WAFER_INFO::0x%04x][voltage %s]\n",
		__FUNCTION__, __LINE__, bin_info, wafer_info, isp_core);
	msm_camera_write_sysfs(SYSFS_ISP_CORE_PATH, isp_core, sizeof(isp_core));

	//Read Ver
	read_value = 0xFFFF;
	ret = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, read_value, ret);
		return -EIO;
	}

	//PLL Initialize
	if (read_value == 0x00A0) {
		companion_version_info = COMP_EVT0;
	} else {
		pr_err("[syscamera][%s::%d][Invalid Companion Version : 0x%4x]\n", __FUNCTION__, __LINE__, read_value);
		return -EIO;
	}

	usleep_range(1000, 2000);

	// Initialization for download
	ret = client->i2c_func_tbl->i2c_write(
			client, 0x0008, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d] Initialization for download failed\n", __FUNCTION__, __LINE__);
		return -EIO;
	}
	pr_err("[syscamera][%s::%d] Initialization for download ok\n", __FUNCTION__, __LINE__);

	usleep_range(1000, 2000);

	return 0;
}

static int msm_companion_release_arm_reset(struct companion_device *companion_dev)
{
	long ret = 0;
	uint16_t read_value = 0xFFFF;
	struct msm_camera_i2c_client *client = NULL;
	uint16_t loop_cnt = 0;

	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

	if (companion_dev)
		client = &companion_dev->companion_i2c_client;
	else {
		pr_err("[syscamera][%s::%d][ERROR][companion_dev is NULL]\n", __FUNCTION__, __LINE__);
		return -EIO;
	}

	usleep_range(1000, 2000);

	//ARM Reset & Memory Remap
	ret = client->i2c_func_tbl->i2c_write(
			client, 0x6048, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d]ARM Reset & Memory Remap failed\n", __FUNCTION__, __LINE__);
		return -EIO;
	}

	//ARM Go
	ret = client->i2c_func_tbl->i2c_write(
			client, 0x6014, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d]Release ARM reset failed\n", __FUNCTION__, __LINE__);
		return -EIO;
	}

	usleep_range(2000, 4000);

	//Check Rev Number
	read_value = 0xFFFF;
	ret = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, read_value, ret);
		return -EIO;
	}

	if (read_value == 0x10A0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x][ret::%ld] Rev EVT 0\n", __FUNCTION__, __LINE__, read_value, ret);
	} else if (read_value == 0x10A1) {
		pr_err("[syscamera][%s::%d][PID::0x%4x][ret::%ld] Rev EVT 1\n", __FUNCTION__, __LINE__, read_value, ret);
	} else {
		pr_err("[syscamera::ERROR][%s::%d][PID::0x%4x][ret::%ld] Rev Number fail\n", __FUNCTION__, __LINE__, read_value, ret);
	}

	/* Recovery code for the ARM go fail */
	while((read_value == 0xA0) && (loop_cnt < 2)) {
		gpio_set_value_cansleep(GPIO_COMP_RSTN, GPIO_OUT_LOW); // COMP_RSTN_OFF
		usleep_range(2000, 4000);
		gpio_set_value_cansleep(GPIO_COMP_RSTN, GPIO_OUT_HIGH); // COMP_RSTN_ON
		usleep_range(2000, 4000);

		//write FW
		pr_err("[syscamera::RECOVERY][%s::%d][Write FW]\n", __FUNCTION__, __LINE__);
		ret = msm_companion_fw_write(companion_dev);
		if(ret < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		usleep_range(4000, 8000);

		//ARM Reset & Memory Remap
		pr_err("[syscamera::RECOVERY][%s::%d][ARM Reset & Memory Remap]\n", __FUNCTION__, __LINE__);
		ret = client->i2c_func_tbl->i2c_write(
			client, 0x6048, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera::RECOVERY][%s::%d]ARM Reset & Memory Remap failed\n", __FUNCTION__, __LINE__);
			return -EIO;
		}

		//ARM Go
		pr_err("[syscamera::RECOVERY][%s::%d][ARM Go]\n", __FUNCTION__, __LINE__);
		ret = client->i2c_func_tbl->i2c_write(
			client, 0x6014, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera::RECOVERY][%s::%d]Release ARM reset failed\n", __FUNCTION__, __LINE__);
			return -EIO;
		}

		usleep_range(2000, 4000);

		//Check Rev Number
		pr_err("[syscamera::RECOVERY][%s::%d][Check Rev Number]\n", __FUNCTION__, __LINE__);
		read_value = 0xFFFF;
		ret = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera::RECOVERY][%s::%d][PID::0x%4x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, read_value, ret);
			return -EIO;
		}

		if (read_value == 0x10A0) {
			pr_err("[syscamera::RECOVERY][%s::%d][PID::0x%4x][ret::%ld] Rev EVT 0\n", __FUNCTION__, __LINE__, read_value, ret);
		} else if (read_value == 0x10A1) {
			pr_err("[syscamera::RECOVERY][%s::%d][PID::0x%4x][ret::%ld] Rev EVT 1\n", __FUNCTION__, __LINE__, read_value, ret);
		} else {
			pr_err("[syscamera::RECOVERY][%s::%d][PID::0x%4x][ret::%ld] Rev Number fail\n", __FUNCTION__, __LINE__, read_value, ret);
		}
		// Read Device ID
		ret = client->i2c_func_tbl->i2c_read(client, 0x0000, &read_value, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera::RECOVERY][%s::%d][PID::0x%4x][ret::%ld] I2C read fail \n", __FUNCTION__, __LINE__, read_value, ret);
			return -EIO;
		}

		if (read_value != 0x73C3)
			pr_err("[syscamera][%s::%d][PID::0x%4x] Device ID failed\n", __FUNCTION__, __LINE__, read_value);

		loop_cnt++;
	}
	if (loop_cnt >= 2) {
		pr_err("[syscamera::RECOVERY::ERROR][Arm go recovery fail]\n");
		return -EIO;
	}

	//	Enable Using M2M Calibration data(Firmware User Guide 8p.)
	client->i2c_func_tbl->i2c_write(client, 0x0044, 0x0001, MSM_CAMERA_I2C_WORD_DATA);

	return 0;
}

int msm_companion_sysfs_fw_version_write(const char* eeprom_ver, const char* phone_ver, const char* load_ver)
{
	int ret = 0;
	char fw_ver[37] = "NULL NULL NULL\n";

	if(strcmp(phone_ver, "") == 0)
		snprintf(fw_ver, sizeof(fw_ver), "%s NULL %s\n", eeprom_ver, load_ver);
	else
		snprintf(fw_ver, sizeof(fw_ver), "%s %s %s\n", eeprom_ver, phone_ver, load_ver);

	CDBG("%s:[FW_DBG][EEPROM/PHONE/LOAD] %s", __func__, fw_ver);

	ret = msm_camera_write_sysfs(SYSFS_COMP_FW_PATH, fw_ver, sizeof(fw_ver));
	if (ret < 0) {
		pr_err("%s: msm_camera_write_sysfs failed.", __func__);
		ret = -1;
	}

	return ret;
}

static int msm_companion_fw_write(struct companion_device *companion_dev)
{
#ifdef MSM_CC_DEBUG
	uint16_t data = 0;
#endif
	long ret = 0;
	int rc = 0;
	struct file *isp_filp = NULL;
	u8 *isp_vbuf = NULL, *isp_fbuf = NULL, *buf_backup = NULL;
#ifndef USE_STATIC_BUF
	u8 *spi_isp_buf = NULL;
#endif
	u32 spi_isp_buf_size = 0;
	u32 isp_size = 0, isp_vsize = 0, isp_fsize = 0, size_backup = 0;
	struct msm_camera_i2c_client *client = NULL;
	mm_segment_t old_fs;

	u8 fs_fw_version[12] = "NULL";
	int i, isEepromFwUsed = 0;

	struct spi_message m;
	struct spi_transfer tx;
	char *sd_fw_isp_path = NULL;
	char *cc_fw_isp_path = NULL;
	uint8_t iter = 0, crc_pass = 0, max_iter = 3;
	uint32_t crc_cal = ~0;
	uint16_t read_value = 0xFFFF;

	if(companion_dev == NULL)
	{
		pr_err("[syscamera][%s::%d][companion_dev is NULL]\n", __FUNCTION__, __LINE__);
		return -EIO;
	}
	client = &companion_dev->companion_i2c_client;
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	client->i2c_func_tbl->i2c_read(client, 0x0000, &read_value, MSM_CAMERA_I2C_WORD_DATA);
	CDBG("[syscamera][%s::%d][PID::0x%4x] Device ID ok\n", __FUNCTION__, __LINE__, read_value);

	//PLL Initialize
	if (companion_version_info == COMP_EVT0) {
		CDBG("[syscamera][%s::%d][Companion EVT 0]\n", __FUNCTION__, __LINE__);
		fw_name[FW_PATH_CC][FW_NAME_ISP][REV_OFFSET_ISP_CC] = '0';

		fw_name[FW_PATH_SD][FW_NAME_MASTER][REV_OFFSET_MASTER_SD] = '0';
		fw_name[FW_PATH_CC][FW_NAME_MASTER][REV_OFFSET_MASTER_CC] = '0';
	} else if (companion_version_info == COMP_EVT1) {
		CDBG("[syscamera][%s::%d][Companion EVT 1]\n", __FUNCTION__, __LINE__);
		fw_name[FW_PATH_CC][FW_NAME_ISP][REV_OFFSET_ISP_CC] = '1';

		fw_name[FW_PATH_SD][FW_NAME_MASTER][REV_OFFSET_MASTER_SD] = '1';
		fw_name[FW_PATH_CC][FW_NAME_MASTER][REV_OFFSET_MASTER_CC] = '1';
	} else {
		pr_err("[syscamera][%s::%d][Invalid Companion Version : %d]\n", __FUNCTION__, __LINE__, companion_version_info);
		/* restore kernel memory setting */
		set_fs(old_fs);
		return -EIO;
	}

	sd_fw_isp_path = fw_name[FW_PATH_SD][FW_NAME_ISP]; // SD_FW_EVT0_ISP_PATH;
	cc_fw_isp_path = fw_name[FW_PATH_CC][FW_NAME_ISP]; // CC_FW_EVT0_ISP_PATH;
	pr_err("[syscamera][%s::%d] sd path = %s, cc path = %s\n", __FUNCTION__, __LINE__, sd_fw_isp_path, cc_fw_isp_path);
	pr_err("[syscamera][%s::%d] cc_fw_isp_path = %s, cc_master_isp_path = %s\n", __FUNCTION__, __LINE__, fw_name[FW_PATH_CC][FW_NAME_ISP], fw_name[FW_PATH_CC][FW_NAME_MASTER]);

	for (iter = 0; iter < max_iter; iter++) {
#ifdef USE_C3_FW_AT_SDCARD_FOLDER
		isp_filp = filp_open(sd_fw_isp_path, O_RDONLY, 0);
		if (IS_ERR(isp_filp)) {
			pr_err("[syscamera]%s does not exist, err %ld, search next path.\n",
					sd_fw_isp_path, PTR_ERR(isp_filp));
#endif
			isp_filp = filp_open(cc_fw_isp_path, O_RDONLY, 0);
			if (IS_ERR(isp_filp)) {
				pr_err("[syscamera]failed to open %s, err %ld\n",
						cc_fw_isp_path, PTR_ERR(isp_filp));
				isp_filp = NULL;
				goto isp_check_multimodule;
			} else {
				CDBG("[syscamera]open success : %s\n", cc_fw_isp_path);
			}
#ifdef USE_C3_FW_AT_SDCARD_FOLDER
		} else {
			CDBG("[syscamera]open success : %s\n", sd_fw_isp_path);
		}
#endif

		isp_size = isp_filp->f_path.dentry->d_inode->i_size;
		isp_fsize = isp_size - isp_vsize;
		CDBG_FW("[syscamera]ISP size %d, fsize %d Bytes\n", isp_size, isp_fsize);
		if(isp_size < 16) {
			pr_err("fatal : invalid file size, %d Bytes\n", isp_size);

			if(iter == max_iter-1) {
				ret = -EIO;
				goto isp_filp_ferr;
			} else {
				continue;
			}
		}

		/* version info is located at the end of 16byte of the buffer. */
		isp_vbuf = vmalloc(isp_size);
		isp_filp->f_pos = 0;
		ret = vfs_read(isp_filp, (char __user *)isp_vbuf, isp_size, &isp_filp->f_pos);
		if (ret != isp_size) {
			pr_err("failed to read Concord info, %ld Bytes\n", ret);
			ret = -EIO;
			goto isp_filp_verr_iter;
		}

		/* Isp set */
		isp_fbuf = vmalloc(isp_fsize);
		isp_filp->f_pos = 0;

		ret = vfs_read(isp_filp, (char __user *)isp_fbuf, isp_fsize, &isp_filp->f_pos);
		if (ret != isp_fsize) {
			pr_err("failed to read Isp, %ld Bytes\n", ret);
			ret = -EIO;
			goto isp_filp_ferr_iter;
		}
#ifdef CONFIG_MSM_CC_DEBUG
		for (arr_idx = 0 ; arr_idx < isp_fsize ; arr_idx ++) {
			CDBG_FW("%02x", isp_fbuf[arr_idx]);
			if(((arr_idx % 8)==0) && (arr_idx != 0)) {
				CDBG_FW("\n");
			}
		}
		CDBG_FW("\n");
#endif

		// Version from file-system
		for (i = 0; i < 11; i++)
			fs_fw_version[i] = isp_vbuf[isp_size - 16 + i];
		fs_fw_version[11] = 0;

		crc_cal = ~0;
		crc_cal = crc32_le(crc_cal, isp_vbuf, isp_size-4);
		crc_cal = ~crc_cal;
		companion_dev->crc_by_ap = crc_cal;
		companion_dev->loading_fw_bin_size = isp_size;
		fw_crc_retention = crc_cal; //for retention mode
		fw_crc_size_retention = isp_size;
		CDBG("[syscamera][%s::%d][companion_dev = %p size = %d crc_by_ap = 0x%08X]\n", __FUNCTION__, __LINE__, companion_dev, companion_dev->loading_fw_bin_size, companion_dev->crc_by_ap);
		CDBG("[syscamera][%s::%d][crc_cal = 0x%08X, expected = 0x%08X]\n", __FUNCTION__, __LINE__, crc_cal, *(uint32_t *)(&isp_vbuf[isp_size-4]));
		if (crc_cal != *(uint32_t *)(&isp_vbuf[isp_size-4])) {
			pr_err("[syscamera][%s::%d][Err::CRC32 is not correct. iter = %d(max=3)]\n", __FUNCTION__, __LINE__, iter);
			msm_camera_fw_check('F', CHECK_COMPANION_FW); //F: Fail
isp_filp_ferr_iter:
			if (isp_fbuf) {
				vfree(isp_fbuf);
				isp_fbuf = NULL;
			} else {
				pr_err("[syscamera][%s::%d][Err::isp_fbuf is NULL]\n", __FUNCTION__, __LINE__);
			}
isp_filp_verr_iter:
			if (isp_vbuf) {
				vfree(isp_vbuf);
				isp_vbuf = NULL;
			} else {
				pr_err("[syscamera][%s::%d][Err::isp_vbuf is NULL]\n", __FUNCTION__, __LINE__);
			}
		} else {
			crc_pass = 1;
			msm_camera_fw_check('N', CHECK_COMPANION_FW);//N:Normal, F: Fail
			CDBG("[syscamera][%s::%d][CRC32 is correct. iter = %d(max=3)]\n", __FUNCTION__, __LINE__, iter);
			break;
		}
	}

	if(crc_pass == 0)
		goto isp_filp_ferr;

	// Multi module support
isp_check_multimodule:
	CDBG("[syscamera][%s::%d][fs version = %s, eeprom version = %s]\n", __FUNCTION__, __LINE__, fs_fw_version, companion_dev->eeprom_fw_ver);
	if(companion_dev->eeprom_fw_bin != NULL && companion_dev->eeprom_fw_bin_size != 0) {
		// HW version check
		for(i = 0; i < 5; i++)
			if(fs_fw_version[i] != companion_dev->eeprom_fw_ver[i])
				isEepromFwUsed = 1;

		// SW version check
		if(isEepromFwUsed != 1) {
			for(i = 5; i < 9; i++) {
				if(fs_fw_version[i] != companion_dev->eeprom_fw_ver[i]) {
					if(fs_fw_version[i] < companion_dev->eeprom_fw_ver[i]) {
						isEepromFwUsed = 1;
						break;
					} else {
						isEepromFwUsed = 0;
						break;
					}
				}
			}
		}
	} else {
		isEepromFwUsed = 0;
	}

	CDBG("[syscamera][%s::%d][fs version = %s, eeprom version = %s, isEepromUsed = %d]\n", __FUNCTION__, __LINE__,
				fs_fw_version, companion_dev->eeprom_fw_ver, isEepromFwUsed);

	if(isEepromFwUsed) {
		buf_backup = isp_fbuf;
		size_backup = isp_fsize;

		crc_cal = ~0;
		crc_cal = crc32_le(crc_cal, companion_dev->eeprom_fw_bin, companion_dev->eeprom_fw_bin_size - 4);
		crc_cal = ~crc_cal;
		companion_dev->crc_by_ap = crc_cal;
		companion_dev->loading_fw_bin_size = companion_dev->eeprom_fw_bin_size;
		fw_crc_retention = crc_cal; //for retention mode
		fw_crc_size_retention = companion_dev->loading_fw_bin_size;
		CDBG("[syscamera][%s::%d][EEPROM companion_dev = %p size = %d crc_by_ap = 0x%08X]\n", __FUNCTION__, __LINE__, companion_dev, companion_dev->loading_fw_bin_size, companion_dev->crc_by_ap);
		CDBG("[syscamera][%s::%d][EEPROM Companion FW crc_cal = 0x%08X, expected = 0x%08X]\n",
			__FUNCTION__, __LINE__, crc_cal, *(uint32_t *)(&(companion_dev->eeprom_fw_bin[companion_dev->eeprom_fw_bin_size-4])));

		if (crc_cal != *(uint32_t *)(&companion_dev->eeprom_fw_bin[companion_dev->eeprom_fw_bin_size-4])) {
			pr_err("[syscamera][%s::%d][Err::EEPROM Companion FW CRC32 is not correct. \n", __FUNCTION__, __LINE__);
			msm_camera_fw_check('F', CHECK_COMPANION_FW);//F: Fail
			goto isp_filp_ferr;
		} else {
			msm_camera_fw_check('N', CHECK_COMPANION_FW);//N:Normal, F: Fail
		}

		isp_fbuf = companion_dev->eeprom_fw_bin;
		isp_fsize = companion_dev->eeprom_fw_bin_size;
		CDBG("[syscamera][%s::%d][fw from eeprom will be used !]\n", __FUNCTION__, __LINE__);
		msm_companion_sysfs_fw_version_write(companion_dev->eeprom_fw_ver, fs_fw_version, companion_dev->eeprom_fw_ver);
	} else {
		CDBG("[syscamera][%s::%d][fw from phone will be used !]\n", __FUNCTION__, __LINE__);
		msm_companion_sysfs_fw_version_write(companion_dev->eeprom_fw_ver, fs_fw_version, fs_fw_version);
	}

	ret = client->i2c_func_tbl->i2c_write(
		client, 0x6428, 0x0008, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera]%s: Isp1 init failed\n", __func__);
		ret = -EIO;
		goto isp_filp_ferr;
	}
	ret = client->i2c_func_tbl->i2c_write(
			client, 0x642A, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera]%s: Isp2 init failed\n", __func__);
		ret = -EIO;
		goto isp_filp_ferr;
	}

	if (isp_fsize > COMPANION_FW_MAX_SIZE - 3) {
		pr_err("[syscamera]%s: FW file size is bigger than buffer size\n", __func__);
		ret = -EIO;
		goto isp_filp_ferr;
	}
	spi_isp_buf_size = isp_fsize + 3;
#ifndef USE_STATIC_BUF
	spi_isp_buf = kmalloc(spi_isp_buf_size, GFP_KERNEL);

	if (!spi_isp_buf){
		pr_err("[syscamera][%s::%d][Err::kmalloc spi_isp_buf is NULL]\n", __FUNCTION__, __LINE__);
		ret = -EIO;
		goto isp_filp_ferr;
	}
#endif
	spi_isp_buf[0] = 0x02;
	spi_isp_buf[1] = 0x6F;
	spi_isp_buf[2] = 0x12;
	memcpy(spi_isp_buf+3, isp_fbuf, isp_fsize);
	memset(&tx, 0, sizeof(struct spi_transfer));
	tx.tx_buf = spi_isp_buf;
	tx.len = spi_isp_buf_size;

	spi_message_init(&m);
	spi_message_add_tail(&tx, &m);
	rc = spi_sync(client->spi_client->spi_master, &m);

#ifndef USE_STATIC_BUF
	kfree(spi_isp_buf);
	spi_isp_buf = NULL;
#endif

	if(isEepromFwUsed) {
		isp_fbuf = buf_backup;
		isp_fsize = size_backup;
	}

	if (isp_filp) {
		filp_close(isp_filp, NULL);
		isp_filp = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_filp is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (isp_vbuf) {
		vfree(isp_vbuf);
		isp_vbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_vbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (isp_fbuf) {
		vfree(isp_fbuf);
		isp_fbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_fbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	/* restore kernel memory setting */
	set_fs(old_fs);
#if 0
	client->i2c_func_tbl->i2c_write(
		client, 0x642c, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	client->i2c_func_tbl->i2c_write(
		client, 0x642e, 0x0000, MSM_CAMERA_I2C_WORD_DATA);

	for (i = 0; i < 20; i++) {
		read_value = 0xFFFF;
		client->i2c_func_tbl->i2c_read(client, 0x6F12, &read_value, MSM_CAMERA_I2C_WORD_DATA);
		pr_err("[syscamera][%s::%d][FW[%d]::0x%04x]\n", __FUNCTION__, __LINE__, i, read_value);
	}
#endif
	return 0;
isp_filp_ferr:
//isp_filp_verr:
#ifndef USE_STATIC_BUF
	if (spi_isp_buf){
		kfree(spi_isp_buf);
		spi_isp_buf = NULL;
	}
#endif

	if(isEepromFwUsed) {
		isp_fbuf = buf_backup;
		isp_fsize = size_backup;
	}

	if (isp_filp) {
		filp_close(isp_filp, NULL);
		isp_filp = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_filp is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (isp_vbuf) {
		vfree(isp_vbuf);
		isp_vbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_vbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (isp_fbuf) {
		vfree(isp_fbuf);
		isp_fbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::isp_fbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	/* restore kernel memory setting */
	set_fs(old_fs);

	return -EFAULT;
}

static int msm_companion_master_write(struct companion_device *companion_dev)
{
	long ret = 0;
	struct file *cc_filp = NULL;
	u8 *cc_vbuf = NULL, *cc_fbuf = NULL;
	u32 cc_size = 0, cc_vsize = 16, cc_fsize = 0;
	uint16_t addr = 0, data = 0;
	struct msm_camera_i2c_client *client = NULL;
	mm_segment_t old_fs;

	u32 arr_idx = 0;

	client = &companion_dev->companion_i2c_client;
	old_fs = get_fs();
	set_fs(KERNEL_DS);

#ifdef USE_C3_FW_AT_SDCARD_FOLDER
	cc_filp = filp_open(fw_name[FW_PATH_SD][FW_NAME_MASTER], O_RDONLY, 0);
	if (IS_ERR(cc_filp)) {
		pr_err("[syscamera]%s does not exist, err %ld, search next path.\n",
				fw_name[FW_PATH_SD][FW_NAME_MASTER], PTR_ERR(cc_filp));
#endif
		cc_filp = filp_open(fw_name[FW_PATH_CC][FW_NAME_MASTER], O_RDONLY, 0);
		if (IS_ERR(cc_filp)) {
			pr_err("[syscamera]failed to open %s, err %ld\n",
					fw_name[FW_PATH_CC][FW_NAME_MASTER], PTR_ERR(cc_filp));
			goto cc_filp_ferr;
		} else {
			pr_err("[syscamera]open success : %s\n", fw_name[FW_PATH_CC][FW_NAME_MASTER]);
		}
#ifdef USE_C3_FW_AT_SDCARD_FOLDER
	} else {
		pr_err("[syscamera]open success : %s\n", fw_name[FW_PATH_SD][FW_NAME_MASTER]);
	}
#endif

	if (!cc_filp) {
		pr_err("cc_flip is NULL\n");
		goto cc_filp_ferr;
	}

	cc_size = cc_filp->f_path.dentry->d_inode->i_size;
	cc_fsize = cc_size - cc_vsize;
	CDBG_FW("[syscamera]concord size %d, fsize %d Bytes\n", cc_size, cc_fsize);

	/* version & setfile info */
	cc_vbuf = vmalloc(cc_vsize+1);
	memset(cc_vbuf, 0x00, cc_vsize+1);
	cc_filp->f_pos = cc_fsize;
	ret = vfs_read(cc_filp, (char __user *)cc_vbuf, cc_vsize, &cc_filp->f_pos);
	if (ret != cc_vsize) {
		pr_err("failed to read Concord info, %ld Bytes\n", ret);
		ret = -EIO;
		goto cc_filp_verr;
	}
	CDBG_FW("[master-version]%s\n",cc_vbuf);
	/* Concord set */
	cc_fbuf = vmalloc(cc_fsize);
	cc_filp->f_pos = 0;	//swap
	ret = vfs_read(cc_filp, (char __user *)cc_fbuf, cc_fsize, &cc_filp->f_pos);
	if (ret != cc_fsize) {
		pr_err("failed to read Concord, %ld Bytes\n", ret);
		ret = -EIO;
		goto cc_filp_ferr;
	}
#ifdef CONFIG_MSM_CC_DEBUG
	for (arr_idx = 0 ; arr_idx < cc_fsize ; arr_idx ++) {
		CDBG_FW("%02x", cc_fbuf[arr_idx]);
		if(((arr_idx % 15)==0) && (arr_idx != 0)) {
			CDBG_FW("\n");
		}
	}
	CDBG_FW("\n");
#endif
	if (cc_fsize % 4 == 0) {
		for (arr_idx = 0 ; arr_idx < cc_fsize ; arr_idx += 4) {
			addr = (((cc_fbuf[arr_idx] << 8) & 0xFF00) | ((cc_fbuf[arr_idx+1] << 0) & 0x00FF));
			data = (((cc_fbuf[arr_idx+2] << 8) & 0xFF00) | ((cc_fbuf[arr_idx+3] << 0) & 0x00FF));
			CDBG_FW("[syscamera]addr : %04x\n", addr);
			CDBG_FW(" data : %04x\n", data);

			ret = client->i2c_func_tbl->i2c_write(
					client, addr, data, MSM_CAMERA_I2C_WORD_DATA);
			if (ret < 0) {
				pr_err("[syscamera]%s: Concord failed\n", __func__);
				ret = -EIO;
				goto cc_filp_ferr;
			}
		}
	} else {
		pr_err("[syscamera]error : The size of Master set file should be multiple of 4. (size = %d byte)", cc_fsize);
		ret = -EIO;
		goto cc_filp_ferr;
	}

	if (cc_filp) {
		filp_close(cc_filp, NULL);
		cc_filp = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_filp is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (cc_vbuf) {
		vfree(cc_vbuf);
		cc_vbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_vbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (cc_fbuf) {
		vfree(cc_fbuf);
		cc_fbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_fbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	/* restore kernel memory setting */
	set_fs(old_fs);
	return 0;

cc_filp_ferr:
cc_filp_verr:
	if (cc_filp) {
		filp_close(cc_filp, NULL);
		cc_filp = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_filp is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (cc_vbuf) {
		vfree(cc_vbuf);
		cc_vbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_vbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	if (cc_fbuf) {
		vfree(cc_fbuf);
		cc_fbuf = NULL;
	} else {
		pr_err("[syscamera][%s::%d][Err::cc_fbuf is NULL]\n", __FUNCTION__, __LINE__);
	}

	/* restore kernel memory setting */
	set_fs(old_fs);
	return -EFAULT;
}

static int msm_companion_get_crc(struct companion_device *companion_dev, struct companion_crc_check_param crc_param, int callByKernel) {
	int ret = 0;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	int i;
	uint16_t crc_high, crc_low;
	uint32_t crc;

	CDBG("[syscamera][%s::%d]addr: 0x%x, count: %d\n", __FUNCTION__, __LINE__, crc_param.addr, crc_param.count);

	// 1. Reset CRC32
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_DATA_LOW, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_DATA_HIGH, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 2. Set address
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_START_ADD_HIGH, (crc_param.addr & 0xFFFF0000) >> 16, MSM_CAMERA_I2C_WORD_DATA);
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_START_ADD_LOW, (crc_param.addr & 0x0000FFFF), MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 3. Set size
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_SIZE_HIGH, (crc_param.count & 0xFFFF0000) >> 16, MSM_CAMERA_I2C_WORD_DATA);
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_SIZE_LOW, (crc_param.count & 0x0000FFFF), MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 4. Start CRC calculation
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_ENABLE, 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 5. Waiting CRC finish
	for (i = 0; i < 100; i++) {
		uint16_t polling = 0;
		ret += client->i2c_func_tbl->i2c_read(
				client, CRC_FINISH, &polling, MSM_CAMERA_I2C_WORD_DATA);
		if(polling == 0x0001) {
			CDBG("[syscamera][%s::%d] break the loop after %d tries.\n", __FUNCTION__, __LINE__, i);
			break;
		}
		usleep_range(1000, 2000);
	}
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 6. Stop CRC
	ret += client->i2c_func_tbl->i2c_write(
			client, CRC_ENABLE, 0x0000, MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 7. Read CRC32
	ret += client->i2c_func_tbl->i2c_read(
			client, CRC_DATA_HIGH, &crc_high, MSM_CAMERA_I2C_WORD_DATA);
	ret += client->i2c_func_tbl->i2c_read(
			client, CRC_DATA_LOW, &crc_low, MSM_CAMERA_I2C_WORD_DATA);
	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on creating crc]\n", __FUNCTION__, __LINE__);
		return ret;
	}

	// 8. Return CRC32
	crc = (((uint32_t)crc_high) << 16) + crc_low;

	if(callByKernel == 1)
	{
		memcpy(crc_param.CRC, &crc, sizeof(crc));
	}
	else
	{
		ret = copy_to_user(crc_param.CRC, &crc, sizeof(crc));
		if(ret < 0) {
			pr_err("[syscamera][%s::%d][Error on copy to user]\n", __FUNCTION__, __LINE__);
			return ret;
		}
	}

	CDBG("[syscamera][%s::%d] CRC = 0x%08X[X]\n", __FUNCTION__, __LINE__, crc);

	return ret;
}

static int msm_companion_stream_on(struct msm_camera_i2c_client *companion_i2c_dev, uint16_t enable)
{
	int ret = 0;

	pr_err("[syscamera][%s::%d][E][enable::%d]\n", __FUNCTION__, __LINE__, enable);
	//wdr mode setting
	if (wdr_mode == 0) {	// WDR off
		CDBG("[syscamera][%s::%d][E][WDR : %d]\n", __FUNCTION__, __LINE__, wdr_mode);
		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_WdrMode) , 0x0000, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_PowerMode), 0x0000, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(HOST_INTRP_ChangeConfig), 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	} else if (wdr_mode == 1) { // WDR On
		CDBG("[syscamera][%s::%d][E][WDR : %d]\n", __FUNCTION__, __LINE__, wdr_mode);
		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_WdrMode) , 0x0001, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_PowerMode), 0x0001, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(HOST_INTRP_ChangeConfig), 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	} else if (wdr_mode == 2) { // WDR Auto
		CDBG("[syscamera][%s::%d][E][WDR : %d]\n", __FUNCTION__, __LINE__, wdr_mode);
		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_WdrMode) , 0x0001, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(api_otf_PowerMode), 0x0002, MSM_CAMERA_I2C_WORD_DATA);

		ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
				companion_i2c_dev, LOWER(HOST_INTRP_ChangeConfig), 0x0001, MSM_CAMERA_I2C_WORD_DATA);
	}
	wdr_mode = -1;

	// a clk control needs to be enabled 03.Dec.2013
	ret = companion_i2c_dev->i2c_func_tbl->i2c_write(
			companion_i2c_dev, 0x6800, enable, MSM_CAMERA_I2C_WORD_DATA);

	if(ret < 0) {
		pr_err("[syscamera][%s::%d][Error on streaming control][enable::%d]\n", __FUNCTION__, __LINE__, enable);
	}

	return ret;
}


static int msm_companion_set_mode(struct companion_device *companion_dev, struct msm_camera_i2c_reg_setting mode_setting)
{
	int ret = 0, idx;
	uint16_t read_value = 0xFFFF;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	struct msm_camera_i2c_reg_array * mode_tbl;

	// Allocate memory for the mode table
	mode_tbl = (struct msm_camera_i2c_reg_array *)kmalloc(sizeof(struct msm_camera_i2c_reg_array) * mode_setting.size, GFP_KERNEL);
	if (!mode_tbl) {
		pr_err("[syscamera][%s::%d][Error] Memory allocation fail\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	// Copy table from user-space area
	if (copy_from_user(mode_tbl, (void *)mode_setting.reg_setting,sizeof(struct msm_camera_i2c_reg_array)* mode_setting.size)) {
		pr_err("[syscamera][%s::%d][Error] Failed to copy mode table from user-space\n", __FUNCTION__, __LINE__);
		kfree(mode_tbl);
		return -EFAULT;
	}

	// Mode setting
	for (idx = 0; idx < mode_setting.size; idx++) {

		if(mode_tbl[idx].delay) {
			usleep_range(mode_tbl[idx].delay*1000, mode_tbl[idx].delay*1000+1000);
			pr_err("[syscamera][%s::%d] delay : %d ms\n", __FUNCTION__, __LINE__, mode_tbl[idx].delay);
		}

		ret = client->i2c_func_tbl->i2c_write(
				client, mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] mode setting failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
		pr_err("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][reg_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
			mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data);
	}

	// Releasing memory for the mode table
	kfree(mode_tbl);

	//recovery code
	read_value = 0xFFFF;
	ret = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
	if (ret < 0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x] I2C read fail\n", __FUNCTION__, __LINE__, read_value);
		return -EIO;
	}

	if (read_value == 0x10A0) {
		pr_err("[syscamera][%s::%d][PID::0x%4x] EVT0 Swap module!! Apply recovery code\n", __FUNCTION__, __LINE__, read_value);
		ret = client->i2c_func_tbl->i2c_write(
				client, 0x1450, 0x0804, MSM_CAMERA_I2C_WORD_DATA);
	}

	return ret;
}

static int msm_companion_aec_update(struct companion_device *companion_dev, struct msm_camera_i2c_reg_setting mode_setting)
{
	int ret = 0, idx;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	struct msm_camera_i2c_reg_array * mode_tbl;

	// Allocate memory for the mode table
	mode_tbl = (struct msm_camera_i2c_reg_array *)kmalloc(sizeof(struct msm_camera_i2c_reg_array) * mode_setting.size, GFP_KERNEL);
	if (!mode_tbl) {
		pr_err("[syscamera][%s::%d][Error] Memory allocation fail\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	// Copy table from user-space area
	if (copy_from_user(mode_tbl, (void *)mode_setting.reg_setting,sizeof(struct msm_camera_i2c_reg_array)* mode_setting.size)) {
		pr_err("[syscamera][%s::%d][Error] Failed to copy aec update table from user-space\n", __FUNCTION__, __LINE__);
		kfree(mode_tbl);
	        return -EFAULT;
	}

	// Mode setting
	for (idx = 0; idx < mode_setting.size; idx++) {
		ret = client->i2c_func_tbl->i2c_write(
				client, mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] aec update failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
//		CDBG("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][reg_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
//			mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data);
	}

#if 0
	// Delay for 2 ms
	msleep(2);

	// read data
	for (idx = 0; idx < mode_setting.size; idx++) {
		uint16_t readval;
		ret = client->i2c_func_tbl->i2c_read(
			client, mode_tbl[idx].reg_addr, &readval, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] i2c read failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
		pr_err("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][read_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
			mode_tbl[idx].reg_addr, readval);
	}
#endif

	// Releasing memory for the mode table
	kfree(mode_tbl);

	return ret;
}

static int msm_companion_awb_update(struct companion_device *companion_dev, struct msm_camera_i2c_reg_setting mode_setting)
{
	int ret = 0, idx;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	struct msm_camera_i2c_reg_array * mode_tbl;

	// Allocate memory for the mode table
	mode_tbl = (struct msm_camera_i2c_reg_array *)kmalloc(sizeof(struct msm_camera_i2c_reg_array) * mode_setting.size, GFP_KERNEL);
	if (!mode_tbl) {
		pr_err("%s,%d Memory allocation fail\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	// Copy table from user-space area
	if (copy_from_user(mode_tbl, (void *)mode_setting.reg_setting,sizeof(struct msm_camera_i2c_reg_array)* mode_setting.size)) {
		pr_err("%s,%d failed to copy awb update table from user-space\n", __FUNCTION__, __LINE__);
		kfree(mode_tbl);
      		return -EFAULT;
	}

	// Mode setting
	for (idx = 0; idx < mode_setting.size; idx++) {
		ret = client->i2c_func_tbl->i2c_write(
				client, mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] awb update failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
//		CDBG("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][reg_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
//			mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data);
	}

  /* for test */
#if 0
	// Delay for 2 ms
	msleep(2);

	// read data
	for (idx = 0; idx < mode_setting.size; idx++) {
                uint16_t readval;
		ret = client->i2c_func_tbl->i2c_read(
				client, mode_tbl[idx].reg_addr, &readval, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] i2c read failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
		pr_err("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][read_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
			mode_tbl[idx].reg_addr, readval);
	}
#endif

	// Releasing memory for the mode table
	kfree(mode_tbl);

	return ret;
}

static int msm_companion_af_update(struct companion_device *companion_dev, struct msm_camera_i2c_reg_setting mode_setting)
{
	int ret = 0, idx;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	struct msm_camera_i2c_reg_array * mode_tbl;

	// Allocate memory for the mode table
	mode_tbl = (struct msm_camera_i2c_reg_array *)kmalloc(sizeof(struct msm_camera_i2c_reg_array) * mode_setting.size, GFP_KERNEL);
	if (!mode_tbl) {
		pr_err("%s,%d Memory allocation fail\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	// Copy table from user-space area
	if (copy_from_user(mode_tbl, (void *)mode_setting.reg_setting,sizeof(struct msm_camera_i2c_reg_array)* mode_setting.size)) {
		pr_err("%s,%d failed to copy af update table from user-space\n", __FUNCTION__, __LINE__);
		kfree(mode_tbl);
		return -EFAULT;
	}

	// Mode setting
	for (idx = 0; idx < mode_setting.size; idx++) {
		ret = client->i2c_func_tbl->i2c_write(
				client, mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] af update failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
//		CDBG("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][reg_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
//			mode_tbl[idx].reg_addr, mode_tbl[idx].reg_data);
	}
#if 0
	// Delay for 2 ms
	msleep(2);

	// read data
	for (idx = 0; idx < mode_setting.size; idx++) {
                uint16_t readval;
		ret = client->i2c_func_tbl->i2c_read(
				client, mode_tbl[idx].reg_addr, &readval, MSM_CAMERA_I2C_WORD_DATA);
		if (ret < 0) {
			pr_err("[syscamera][%s::%d] i2c read failed\n", __FUNCTION__, __LINE__);
			ret = 0;
		}
		pr_err("[syscamera][%s::%d][idx::%d][reg_addr::0x%4x][read_data::0x%4x]\n", __FUNCTION__, __LINE__, idx,
			mode_tbl[idx].reg_addr, readval);
	}
#endif
	// Releasing memory for the mode table
	kfree(mode_tbl);

	return ret;
}

static int msm_companion_get_dt_data(struct device_node *of_node,
	struct companion_device *device)
{
	int32_t rc = 0;
	uint32_t id_info[3];
	rc = of_property_read_u32(of_node, "qcom,cci-master",
		&device->cci_i2c_master);
	CDBG("[syscamera][%s::%d][qcom,cci-master::%d][rc::%d]\n", __FUNCTION__, __LINE__, device->cci_i2c_master, rc);
	if (rc < 0) {
		/* Set default master 0 */
		device->cci_i2c_master = MASTER_0;
		rc = 0;
	}

	device->slave_info = kzalloc(sizeof(struct msm_camera_slave_info),
		GFP_KERNEL);
	if (!device->slave_info) {
		pr_err("[syscamera][%s::%d] Memory allocation fail\n", __FUNCTION__, __LINE__);
		rc = -ENOMEM;
		return rc;;
	}

	rc = of_property_read_u32_array(of_node, "qcom,slave-id",
		id_info, 3);
	if (rc < 0) {
		pr_err("[syscamera][%s::%d] Failed to of_property_read_u32_array\n", __FUNCTION__, __LINE__);
		goto FREE_COMPANION_INFO;
	}

	device->slave_info->sensor_slave_addr = id_info[0];
	device->slave_info->sensor_id_reg_addr = id_info[1];
	device->slave_info->sensor_id = id_info[2];
	CDBG("[syscamera][%s::%d]slave addr = %x, sensor id = %x\n", __FUNCTION__, __LINE__, id_info[0], id_info[2]);
	return rc;
FREE_COMPANION_INFO:
	kfree(device->slave_info);
	return rc;
}

#if 0
static void msm_companion_poll(uint32_t val)
{
	uint32_t tmp = 0, retry = 0;
	do {
		usleep_range(1000, 2000);
		/*Add read register here for status*/
	} while ((tmp != val) && (retry++ < MSM_COMP_POLL_RETRIES));
	if (retry < MSM_COMP_POLL_RETRIES)
		CDBG("Poll finished\n");
	else
		pr_err("[syscamera][%s::%d] Poll failed: expect: 0x%x\n", __FUNCTION__, __LINE__, val);
}
#endif

static int msm_companion_read_stats2(struct companion_device *companion_dev, unsigned char * buffer)
{
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
	int rc = 0;

	CDBG("[syscamera][%s::%d] Enter\n", __FUNCTION__, __LINE__);

	if ((companion_dev->device_type != MSM_CAMERA_SPI_DEVICE) ||
		(client->spi_client == NULL)) {
		return rc;
	}

	rc = client->i2c_func_tbl->i2c_write(
		client, 0x642c, STAT2_READREG_ADDR_MSB, MSM_CAMERA_I2C_WORD_DATA);
	rc = client->i2c_func_tbl->i2c_write(
		client, 0x642e, STAT2_READREG_ADDR_LSB, MSM_CAMERA_I2C_WORD_DATA);
	rc = client->i2c_func_tbl->i2c_read_multi(
		client, stats2_len, buffer);

	CDBG("[syscamera][%s::%d] Exit\n", __FUNCTION__, __LINE__);
	return rc;
}

#ifdef STATS2_WORKQUEUE
static void msm_companion_stat2_read(struct work_struct *work)
{
#define STAT2_DATA_SIZE 432

	struct companion_device *companion_dev = NULL;
	struct companion_isr_resource *isr_resource = NULL;
	struct msm_camera_i2c_client *client = NULL;
	int i;
	uint8_t *buffer = NULL;
	uint32_t len = STAT2_DATA_SIZE;
	int32_t rc = 0;
	static int s_cnt = 0;

	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

	companion_dev = container_of(work, struct companion_device, companion_read_work);
	CDBG("[syscamera][%s::%d]companion_dev=0x%p\n", __FUNCTION__, __LINE__, companion_dev);

	isr_resource = &companion_dev->isr_resource;
	client = &companion_dev->companion_i2c_client;
	buffer = (uint8_t *)kmalloc(STAT2_DATA_READ_SIZE, GFP_KERNEL);
	if (!buffer) {
		pr_err("[syscamera][%s::%d]memory allocation fail\n", __FUNCTION__, __LINE__);
		return;
	}

	// ToDo do word read from SPI
	if ( !(s_cnt++ % 200) ) {
		rc = client->i2c_func_tbl->i2c_write(
					client, 0x642c, STAT2_READREG_ADDR_MSB, MSM_CAMERA_I2C_WORD_DATA);
		rc = client->i2c_func_tbl->i2c_write(
					client, 0x642e, STAT2_READREG_ADDR_LSB, MSM_CAMERA_I2C_WORD_DATA);
		rc = client->i2c_func_tbl->i2c_read_burst(
					client, MAX_SPI_SIZE, len, buffer);

		for (i = 0; i < MAX_SPI_SIZE; i++) {
			pr_err("[syscamera][%s] stat2_data[%d] = 0x%x\n", __func__, i, buffer[i]);
		}
	}

	if (buffer) {
		kzfree(buffer);
	}

	CDBG("[syscamera][%s::%d][X]\n", __FUNCTION__, __LINE__);
}
#endif

static void msm_companion_do_tasklet(unsigned long data)
{
	unsigned long flags;
	struct companion_isr_queue_cmd *qcmd = NULL;
	struct companion_device *companion_dev = NULL;
	struct companion_isr_resource *isr_resource = NULL;
	companion_dev = (struct companion_device *)data;
	isr_resource = &companion_dev->isr_resource;
	while (atomic_read(&isr_resource->comp_irq_cnt)) {
		if (atomic_read(&comp_streamon_set)) {
			struct v4l2_event v4l2_evt;
			v4l2_evt.id = 0;
			v4l2_evt.type = V4L2_EVENT_COMPANION_IRQ_IN;
			v4l2_event_queue(companion_dev->msm_sd.sd.devnode, &v4l2_evt);
		}

		spin_lock_irqsave(&isr_resource->comp_tasklet_lock, flags);
		qcmd = list_first_entry(&isr_resource->comp_tasklet_q,
			struct companion_isr_queue_cmd, list);

		atomic_sub(1, &isr_resource->comp_irq_cnt);
		CDBG("[syscamera][%s:%d] cnt = %d\n", __FUNCTION__, __LINE__, atomic_read(&isr_resource->comp_irq_cnt));

		if (!qcmd) {
		    atomic_set(&isr_resource->comp_irq_cnt, 0);
			spin_unlock_irqrestore(&isr_resource->comp_tasklet_lock,
				flags);
            return;
        }
		list_del(&qcmd->list);
		spin_unlock_irqrestore(&isr_resource->comp_tasklet_lock,
			flags);
		kfree(qcmd);
	}
}

irqreturn_t msm_companion_process_irq(int irq_num, void *data)
{
	unsigned long flags;
	struct companion_isr_queue_cmd *qcmd;
	struct companion_device *companion_dev =
		(struct companion_device *) data;
	struct companion_isr_resource *isr_resource = NULL;

	if (NULL == data)
		return IRQ_HANDLED;

	isr_resource = &companion_dev->isr_resource;
	qcmd = kzalloc(sizeof(struct companion_isr_queue_cmd),
		GFP_ATOMIC);
	if (!qcmd) {
		pr_err("[syscamera][%s::%d]qcmd malloc failed!\n", __FUNCTION__, __LINE__);
		return IRQ_HANDLED;
	}
	/*This irq wil fire whenever the gpio toggles - rising or falling edge*/
	qcmd->compIrqStatus = 0;

	spin_lock_irqsave(&isr_resource->comp_tasklet_lock, flags);
	list_add_tail(&qcmd->list, &isr_resource->comp_tasklet_q);

	atomic_add(1, &isr_resource->comp_irq_cnt);
	CDBG("[syscamera][%s::%d] Companion IRQ, cnt = %d\n", __FUNCTION__, __LINE__, atomic_read(&isr_resource->comp_irq_cnt));
	spin_unlock_irqrestore(&isr_resource->comp_tasklet_lock, flags);
#ifdef STATS2_WORKQUEUE
        queue_work(companion_dev->companion_queue, &companion_dev->companion_read_work);
#else
	tasklet_schedule(&isr_resource->comp_tasklet);
#endif
	return IRQ_HANDLED;
}


static int msm_companion_init(struct companion_device *companion_dev, uint32_t size, uint16_t companion_dump)
{
	int rc = 0;

	CDBG("[syscamera][%s::%d][E] : (dump : %d)\n", __FUNCTION__, __LINE__, companion_dump);
	if (companion_dev->companion_state == COMP_POWER_UP) {
		pr_err("[syscamera][%s::%d]companion invalid state %d\n", __FUNCTION__, __LINE__,
			companion_dev->companion_state);
		msm_companion_release(companion_dev);
		rc = -EINVAL;
		return rc;
	}

	stats2 = kmalloc(size, GFP_KERNEL);
	if (stats2 == NULL) {
		pr_err("[syscamera][%s::%d] stats2 memory alloc fail\n", __FUNCTION__, __LINE__);
		rc = -ENOMEM;
		return rc;
	}

	if (companion_dump == 1) {
		pr_err("[syscamera][%s::%d] companion dump enable\n", __FUNCTION__, __LINE__);
		dump_buf = (uint8_t *)kmalloc(0x8AFA, GFP_KERNEL);
		if (dump_buf == NULL) {
			pr_err("[syscamera][%s::%d] dump_buf memory alloc fail\n", __FUNCTION__, __LINE__);
			kfree(stats2);
			rc = -ENOMEM;
			return rc;
		}
	}
	stats2_len = size;

	init_completion(&companion_dev->wait_complete);
	spin_lock_init(&companion_dev->isr_resource.comp_tasklet_lock);
	INIT_LIST_HEAD(&companion_dev->isr_resource.comp_tasklet_q);
	tasklet_init(&companion_dev->isr_resource.comp_tasklet,
		msm_companion_do_tasklet, (unsigned long)companion_dev);
	atomic_set(&comp_streamon_set, 0);
	enable_irq(companion_dev->isr_resource.comp_irq_num);
	companion_dev->companion_state = COMP_POWER_UP;
	CDBG("[syscamera][%s::%d][X]\n", __FUNCTION__, __LINE__);
	return rc;
}

static int msm_companion_release(struct companion_device *companion_dev)
{
	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);

	if (companion_dev->companion_state != COMP_POWER_UP) {
		pr_err("[syscamera][%s::%d]companion invalid state %d\n", __FUNCTION__, __LINE__,
			companion_dev->companion_state);
		return -EINVAL;
	}

	disable_irq(companion_dev->isr_resource.comp_irq_num);
	tasklet_kill(&companion_dev->isr_resource.comp_tasklet);

	if (stats2) {
		kfree(stats2);
		stats2 = NULL;
		pr_err("[syscamera][%s::%d][stats2 free success]\n", __FUNCTION__, __LINE__);
	}

	if (dump_buf) {
		kfree(dump_buf);
		dump_buf = NULL;
		pr_err("[syscamera][%s::%d][dump_buf free success]\n", __FUNCTION__, __LINE__);
	}

	companion_dev->companion_state = COMP_POWER_DOWN;
	return 0;
}

static long msm_companion_cmd(struct companion_device *companion_dev, void *arg)
{
	int rc = 0;
	struct companion_cfg_data *cdata = (struct companion_cfg_data *)arg;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;

	if (!companion_dev || !cdata) {
		pr_err("[syscamera][%s::%d]companion_dev %p, cdata %p\n", __FUNCTION__, __LINE__,
			companion_dev, cdata);
		return -EINVAL;
	}

	CDBG("[syscamera][%s::%d]cfgtype = %d\n", __FUNCTION__, __LINE__, cdata->cfgtype);
	switch (cdata->cfgtype) {
	case COMPANION_CMD_INIT: {
		uint32_t stats2_size = *(uint32_t *)cdata->cfg.setting;
		uint16_t companion_dump = cdata->isDump;

		CDBG("[syscamera][%s::%d] Companion init\n", __FUNCTION__, __LINE__);
		rc = msm_companion_init(companion_dev, stats2_size, companion_dump);
		}
		break;
	case COMPANION_CMD_AEC_UPDATE: {
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata->cfg.mode_setting.size, cdata->cfg.mode_setting.data_type);
		rc = msm_companion_aec_update(companion_dev, cdata->cfg.mode_setting);
		}

		break;
	case COMPANION_CMD_AWB_UPDATE: {
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata->cfg.mode_setting.size, cdata->cfg.mode_setting.data_type);
		rc = msm_companion_awb_update(companion_dev, cdata->cfg.mode_setting);
		}
		break;

	case COMPANION_CMD_AF_UPDATE: {
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata->cfg.mode_setting.size, cdata->cfg.mode_setting.data_type);
		rc = msm_companion_af_update(companion_dev, cdata->cfg.mode_setting);
	        }
		break;

	case COMPANION_CMD_GET_INFO: {
			uint16_t read_value = 0;
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
//			CDBG("[syscamera][%s::%d][Get info on i2c::%x][sid::%d]\n", __FUNCTION__, __LINE__, (unsigned int)client, client->cci_client->sid);
			rc = client->i2c_func_tbl->i2c_read(client, 0x0000, &read_value, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, read_value);
				return -EFAULT;
			}
			if (copy_to_user(cdata->cfg.read_id, &read_value, sizeof(read_value))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_GET_REV: {
			uint16_t read_value = 0;
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
//			CDBG("[syscamera][%s::%d][Get EVT rev on i2c::%x][sid::%d]\n", __FUNCTION__, __LINE__, (unsigned int)client, client->cci_client->sid);
			rc = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, read_value);
				return -EFAULT;
			}
			if (copy_to_user(cdata->cfg.rev, &read_value, sizeof(read_value))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_I2C_READ: {
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
			uint16_t local_data = 0;
			uint16_t local_addr =cdata->isDump;

			rc = client->i2c_func_tbl->i2c_read(client, local_addr, &local_data, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, local_addr);
				return -EFAULT;
			}
			CDBG("[syscamera][%s::%d][local_addr::0x%4x][local_data::0x%4x]\n", __FUNCTION__, __LINE__, local_addr, local_data);
			if (copy_to_user(cdata->cfg.read_id, (void *)&local_data, sizeof(uint16_t))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_FW_BINARY_SET:
		CDBG("[syscamera][%s::%d] Setting fw binary\n", __FUNCTION__, __LINE__);

		rc = msm_companion_fw_binary_set(companion_dev, cdata->cfg.fw_bin, NULL);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on writing cal data\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_SET_CAL_TBL:
		CDBG("[syscamera][%s::%d] Setting calibration table (size = %d)\n", __FUNCTION__, __LINE__, cdata->cfg.mode_setting.size);
		rc = msm_companion_set_cal_tbl(companion_dev, cdata->cfg.mode_setting);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on writing cal data\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_READ_CAL_TBL:
		CDBG("[syscamera][%s::%d] Read calibration table (cal_size = %d)\n", __FUNCTION__, __LINE__, cdata->cfg.read_cal.size);
		CDBG("[syscamera][%s::%d] Read calibration table (write size = %d)\n", __FUNCTION__, __LINE__, cdata->cfg.read_cal.offset);
		CDBG("[syscamera][%s::%d] Read calibration table (buffer = %p)\n", __FUNCTION__, __LINE__, cdata->cfg.read_cal.cal_data);

		cal_data = kmalloc(sizeof(uint8_t)*cdata->cfg.read_cal.size, GFP_KERNEL);
		if (!cal_data) {
			pr_err("[syscamera][%s::%d][Error] Memory allocation fail\n", __FUNCTION__, __LINE__);
			return -ENOMEM;
		}

		rc = msm_companion_read_cal_tbl(companion_dev, cdata->cfg.read_cal.offset, cdata->cfg.read_cal.size);
		if(rc < 0) {
		  pr_err("[syscamera][%s::%d] error on reading cal data\n", __FUNCTION__, __LINE__);
		  kfree(cal_data);
		  break;
		}
		if (copy_to_user(cdata->cfg.read_cal.cal_data, cal_data, sizeof(uint8_t)*cdata->cfg.read_cal.size)) {
		  pr_err("[syscamera][%s::%d] copy_to_user failed\n", __func__, __LINE__);
		  rc = -EFAULT;
		  kfree(cal_data);
		  break;
		}
		kfree(cal_data);

		break;

	case COMPANION_CMD_LOAD_FIRMWARE_STEP_A:
		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_A\n", __FUNCTION__, __LINE__);
		rc = msm_companion_pll_init(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
		break;
	case COMPANION_CMD_LOAD_FIRMWARE_STEP_B:
		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_B\n", __FUNCTION__, __LINE__);
		rc = msm_companion_fw_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
		break;
	case COMPANION_CMD_LOAD_FIRMWARE_STEP_C:
		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_C\n", __FUNCTION__, __LINE__);
		rc = msm_companion_release_arm_reset(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_LOAD_MASTER:
		CDBG("[syscamera][%s::%d] Loading master\n", __FUNCTION__, __LINE__);
		rc = msm_companion_master_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading master\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_CAL_DATA_WRITE:
		CDBG("[syscamera][%s::%d] compare CRC calculated inside\n", __FUNCTION__, __LINE__);
		rc = msm_companion_compare_FW_crc(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on compare crc data\n", __FUNCTION__, __LINE__);
			break;
		}

		CDBG("[syscamera][%s::%d] Writing cal data\n", __FUNCTION__, __LINE__);
		rc = msm_companion_cal_data_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on writing cal data\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_GET_CRC:
		rc = msm_companion_get_crc(companion_dev, cdata->cfg.crc_check, 0);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_get_crc failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		break;

	case COMPANION_CMD_STREAM_ON:
		CDBG("[syscamera][%s::%d] Companion stream on[enable::%d]\n", __FUNCTION__, __LINE__, cdata->cfg.stream_on);
		if (cdata->cfg.stream_on == 1)
			atomic_set(&comp_streamon_set, 1);
		else
			atomic_set(&comp_streamon_set, 0);
		rc = msm_companion_stream_on(client, cdata->cfg.stream_on);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_stream_on failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		break;

	case COMPANION_CMD_DUMP_REGISTER:
		rc = msm_companion_dump_register(companion_dev, dump_buf);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_dump_register failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		if (copy_to_user(cdata->cfg.dump_buf, dump_buf, 0x8AFA)) {
			pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;

	case COMPANION_CMD_SET_MODE:
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata->cfg.mode_setting.size, cdata->cfg.mode_setting.data_type);
		rc = msm_companion_set_mode(companion_dev, cdata->cfg.mode_setting);
		break;

	case COMPANION_CMD_GET_STATS2:
		if (NULL == stats2 || NULL == cdata->cfg.stats2) {
			pr_err("[syscamera][%s::%d] source or destination is null[stats2::%p][cfg.stats2::%p]\n", __FUNCTION__, __LINE__, stats2, cdata->cfg.stats2);
			return -EFAULT;
		}
		rc = msm_companion_read_stats2(companion_dev, stats2);
		if (copy_to_user(cdata->cfg.stats2, stats2, stats2_len)) {
			pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;

	case COMPANION_CMD_RELEASE:
		CDBG("[syscamera][%s::%d]COMPANION_CMD_RELEASE\n", __FUNCTION__, __LINE__);
		rc = msm_companion_release(companion_dev);
		break;

	default:
		pr_err("[syscamera][%s::%d]failed\n", __FUNCTION__, __LINE__);
		rc = -ENOIOCTLCMD;
		break;
	}
	return rc;
}

static int32_t msm_companion_get_subdev_id(struct companion_device *companion_dev, void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	if (!subdev_id) {
		pr_err("[syscamera][%s::%d]failed\n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}
	*subdev_id = companion_dev->subdev_id;
	CDBG("[syscamera][%s::%d]subdev_id %d\n", __FUNCTION__, __LINE__, *subdev_id);
	return 0;
}

static long msm_companion_subdev_ioctl(struct v4l2_subdev *sd,
			unsigned int cmd, void *arg)
{
	int rc = -ENOIOCTLCMD;
	struct companion_device *companion_dev = v4l2_get_subdevdata(sd);
	mutex_lock(&companion_dev->comp_mutex);
	CDBG("[syscamera][%s::%d]\n", __FUNCTION__, __LINE__);
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		rc = msm_companion_get_subdev_id(companion_dev, arg);
		break;
	case VIDIOC_MSM_COMPANION_IO_CFG:
		rc = msm_companion_cmd(companion_dev, arg);
		break;
	case MSM_SD_SHUTDOWN:
		pr_warn("[syscamera][%s::%d]MSM_SD_SHUTDOWN\n", __FUNCTION__, __LINE__);
		rc = msm_companion_release(companion_dev);
		break;
	default:
		pr_err("[syscamera][%s::%d]command not found\n", __FUNCTION__, __LINE__);
	}
	CDBG("[syscamera][%s::%d] Exit\n", __FUNCTION__, __LINE__);
	mutex_unlock(&companion_dev->comp_mutex);
	return rc;
}

int msm_companion_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	CDBG("[syscamera][%s::%d][E]-[sd::%s][navailable::%d]\n", __FUNCTION__, __LINE__, sd->name, fh->navailable);
	return v4l2_event_subscribe(fh, sub, 30, NULL);
}

int msm_companion_unsubscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	CDBG("[syscamera][%s::%d][E]-[sd::%s][navailable::%d]\n", __FUNCTION__, __LINE__, sd->name, fh->navailable);
	return v4l2_event_unsubscribe(fh, sub);
}

#ifdef CONFIG_COMPAT
void msm_companion_print_config(struct companion_cfg_data cdata, struct companion_cfg_data32 *cdata32)
{
	pr_err("[%s] cfgtype: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfgtype, cdata32->cfgtype);
	pr_err("[%s] cfg.setting: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.setting, compat_ptr(cdata32->cfg.setting));
	pr_err("[%s] cfg.stream_on: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.stream_on, cdata32->cfg.stream_on);
	pr_err("[%s] cfg.stats2: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.stats2, compat_ptr(cdata32->cfg.stats2));
	pr_err("[%s] cfg.dump_buf: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.dump_buf, compat_ptr(cdata32->cfg.dump_buf));
	pr_err("[%s] cfg.read_id: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.read_id, compat_ptr(cdata32->cfg.read_id));
	pr_err("[%s] cfg.rev: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.rev, compat_ptr(cdata32->cfg.rev));

	pr_err("[%s] cfg.read_cal.cal_data: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.read_cal.cal_data, compat_ptr(cdata32->cfg.read_cal.cal_data));
	pr_err("[%s] cfg.read_cal.size: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.read_cal.size, cdata32->cfg.read_cal.size);
	pr_err("[%s] cfg.read_cal.offset: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.read_cal.offset, cdata32->cfg.read_cal.offset);

	pr_err("[%s] cfg.mode_setting.reg_setting: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.mode_setting.reg_setting, compat_ptr(cdata32->cfg.mode_setting.reg_setting));
	pr_err("[%s] cfg.mode_setting.size: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.mode_setting.size, cdata32->cfg.mode_setting.size);
	pr_err("[%s] cfg.mode_setting.addr_type: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.mode_setting.addr_type, cdata32->cfg.mode_setting.addr_type);
	pr_err("[%s] cfg.mode_setting.data_type: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.mode_setting.data_type, cdata32->cfg.mode_setting.data_type);
	pr_err("[%s] cfg.mode_setting.delay: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.mode_setting.delay, cdata32->cfg.mode_setting.delay);

	pr_err("[%s] cfg.crc_check.addr: [0x%x], compat32  [0x%x] \n",
		__FUNCTION__, cdata.cfg.crc_check.addr, cdata32->cfg.crc_check.addr);
	pr_err("[%s] cfg.crc_check.count: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.crc_check.count, cdata32->cfg.crc_check.count);
	pr_err("[%s] cfg.crc_check.CRC: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.crc_check.CRC, compat_ptr(cdata32->cfg.crc_check.CRC));

	pr_err("[%s] cfg.fw_bin.version: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.fw_bin.version, compat_ptr(cdata32->cfg.fw_bin.version));
	pr_err("[%s] cfg.fw_bin.buffer: [%p], compat32  [%p] \n",
		__FUNCTION__, cdata.cfg.fw_bin.buffer, compat_ptr(cdata32->cfg.fw_bin.buffer));
	pr_err("[%s] cfg.fw_bin.size: [%d], compat32  [%d] \n",
		__FUNCTION__, cdata.cfg.fw_bin.size, cdata32->cfg.fw_bin.size);
}

static int32_t msm_companion_cmd32(struct companion_device *companion_dev, void __user *arg)
{
	int rc = 0;
	struct companion_cfg_data32 *cdata32 = (struct companion_cfg_data32 *)arg;
	struct companion_cfg_data cdata;
	struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;

	if (!companion_dev || !cdata32) {
		pr_err("[syscamera][%s::%d]companion_dev %p, cdata32 %p\n", __FUNCTION__, __LINE__,
			companion_dev, cdata32);
		return -EINVAL;
	}

	cdata.cfgtype = cdata32->cfgtype;
	CDBG("[syscamera][%s::%d]cfgtype = %d\n", __FUNCTION__, __LINE__, cdata.cfgtype);
	switch (cdata.cfgtype) {
	case COMPANION_CMD_INIT: {
		uint32_t stats2_size;
		uint16_t companion_dump = cdata32->isDump;

		if (copy_from_user(&stats2_size,
				compat_ptr(cdata32->cfg.setting),
				sizeof(uint32_t))) {
			pr_err("[syscamera][%s::%d] Companion init, can't get stats2_size!!\n",
				__FUNCTION__, __LINE__);
			return -EFAULT;
		}

		CDBG("[syscamera][%s::%d] Companion init, stats2_size = %d\n", __FUNCTION__, __LINE__, stats2_size);
		rc = msm_companion_init(companion_dev, stats2_size, companion_dump);
		}
		break;
	case COMPANION_CMD_AEC_UPDATE: {
		cdata.cfg.mode_setting.reg_setting = compat_ptr(cdata32->cfg.mode_setting.reg_setting);
		cdata.cfg.mode_setting.size = cdata32->cfg.mode_setting.size;
		cdata.cfg.mode_setting.addr_type = cdata32->cfg.mode_setting.addr_type;
		cdata.cfg.mode_setting.data_type = cdata32->cfg.mode_setting.data_type;
		cdata.cfg.mode_setting.delay= cdata32->cfg.mode_setting.delay;
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata.cfg.mode_setting.size, cdata.cfg.mode_setting.data_type);
		rc = msm_companion_aec_update(companion_dev, cdata.cfg.mode_setting);
		}

		break;
	case COMPANION_CMD_AWB_UPDATE: {
		cdata.cfg.mode_setting.reg_setting = compat_ptr(cdata32->cfg.mode_setting.reg_setting);
		cdata.cfg.mode_setting.size = cdata32->cfg.mode_setting.size;
		cdata.cfg.mode_setting.addr_type = cdata32->cfg.mode_setting.addr_type;
		cdata.cfg.mode_setting.data_type = cdata32->cfg.mode_setting.data_type;
		cdata.cfg.mode_setting.delay= cdata32->cfg.mode_setting.delay;
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata.cfg.mode_setting.size, cdata.cfg.mode_setting.data_type);
		rc = msm_companion_awb_update(companion_dev, cdata.cfg.mode_setting);
		}
		break;

	case COMPANION_CMD_AF_UPDATE: {
		cdata.cfg.mode_setting.reg_setting = compat_ptr(cdata32->cfg.mode_setting.reg_setting);
		cdata.cfg.mode_setting.size = cdata32->cfg.mode_setting.size;
		cdata.cfg.mode_setting.addr_type = cdata32->cfg.mode_setting.addr_type;
		cdata.cfg.mode_setting.data_type = cdata32->cfg.mode_setting.data_type;
		cdata.cfg.mode_setting.delay= cdata32->cfg.mode_setting.delay;
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata.cfg.mode_setting.size, cdata.cfg.mode_setting.data_type);
		rc = msm_companion_af_update(companion_dev, cdata.cfg.mode_setting);
			}
		break;

	case COMPANION_CMD_GET_INFO: {
			uint16_t read_value = 0;
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
			cdata.cfg.read_id= compat_ptr(cdata32->cfg.read_id);
			rc = client->i2c_func_tbl->i2c_read(client, 0x0000, &read_value, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, read_value);
				return -EFAULT;
			}
			if (copy_to_user(cdata.cfg.read_id, &read_value, sizeof(read_value))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_GET_REV: {
			uint16_t read_value = 0;
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
			cdata.cfg.rev= compat_ptr(cdata32->cfg.rev);
			rc = client->i2c_func_tbl->i2c_read(client, 0x0002, &read_value, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, read_value);
				return -EFAULT;
			}
			if (copy_to_user(cdata.cfg.rev, &read_value, sizeof(read_value))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_I2C_READ: {
			struct msm_camera_i2c_client *client = &companion_dev->companion_i2c_client;
			uint16_t local_data = 0;
			uint16_t local_addr  = cdata32->isDump;
			cdata.cfg.read_id= compat_ptr(cdata32->cfg.read_id);

			rc = client->i2c_func_tbl->i2c_read(client, local_addr, &local_data, MSM_CAMERA_I2C_WORD_DATA);
			if (rc < 0) {
				pr_err("[syscamera][%s::%d][PID::0x%4x] read failed\n", __FUNCTION__, __LINE__, local_addr);
				return -EFAULT;
			}
			CDBG("[syscamera][%s::%d][local_addr::0x%4x][local_data::0x%4x]\n", __FUNCTION__, __LINE__, local_addr, local_data);
			if (copy_to_user(cdata.cfg.read_id, (void *)&local_data, sizeof(uint16_t))) {
				pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
				rc = -EFAULT;
				break;
			}
		}
		break;

	case COMPANION_CMD_FW_BINARY_SET:
		CDBG("[syscamera][%s::%d] Setting fw binary\n", __FUNCTION__, __LINE__);
		cdata.cfg.fw_bin.version = compat_ptr(cdata32->cfg.fw_bin.version);
		cdata.cfg.fw_bin.hwinfo = compat_ptr(cdata32->cfg.fw_bin.hwinfo);
		cdata.cfg.fw_bin.buffer= compat_ptr(cdata32->cfg.fw_bin.buffer);
		cdata.cfg.fw_bin.size = cdata32->cfg.fw_bin.size;
		cdata.cfg.fw_bin.sensor_name = compat_ptr(cdata32->cfg.fw_bin.sensor_name);

		pr_err("[%s::%d] version = %s, hwinfo = %s, size = %d, sensor_name=%s\n", __FUNCTION__, __LINE__,
			cdata.cfg.fw_bin.version, cdata.cfg.fw_bin.hwinfo, cdata.cfg.fw_bin.size, cdata.cfg.fw_bin.sensor_name);

		rc = msm_companion_fw_binary_set(companion_dev, cdata.cfg.fw_bin, cdata.cfg.fw_bin.sensor_name);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on Setting fw binary\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_SET_CAL_TBL:
		cdata.cfg.mode_setting.reg_setting = compat_ptr(cdata32->cfg.mode_setting.reg_setting);
		cdata.cfg.mode_setting.size = cdata32->cfg.mode_setting.size;
		cdata.cfg.mode_setting.addr_type = cdata32->cfg.mode_setting.addr_type;
		cdata.cfg.mode_setting.data_type = cdata32->cfg.mode_setting.data_type;
		cdata.cfg.mode_setting.delay= cdata32->cfg.mode_setting.delay;
		CDBG("[syscamera][%s::%d] Setting calibration table (size = %d)\n", __FUNCTION__, __LINE__, cdata.cfg.mode_setting.size);
		rc = msm_companion_set_cal_tbl(companion_dev, cdata.cfg.mode_setting);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on writing cal data\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_READ_CAL_TBL:
		cdata.cfg.read_cal.cal_data = compat_ptr(cdata32->cfg.read_cal.cal_data);
		cdata.cfg.read_cal.size = cdata32->cfg.read_cal.size;
		cdata.cfg.read_cal.offset = cdata32->cfg.read_cal.offset;
		CDBG("[syscamera][%s::%d] Read calibration table (cal_size = %d)\n", __FUNCTION__, __LINE__, cdata.cfg.read_cal.size);
		CDBG("[syscamera][%s::%d] Read calibration table (write size = %d)\n", __FUNCTION__, __LINE__, cdata.cfg.read_cal.offset);
		CDBG("[syscamera][%s::%d] Read calibration table (buffer = %p)\n", __FUNCTION__, __LINE__, cdata.cfg.read_cal.cal_data);

		cal_data = kmalloc(sizeof(uint8_t)*cdata.cfg.read_cal.size, GFP_KERNEL);
		if (!cal_data) {
			pr_err("[syscamera][%s::%d][Error] Memory allocation fail\n", __FUNCTION__, __LINE__);
			return -ENOMEM;
		}

		rc = msm_companion_read_cal_tbl(companion_dev, cdata.cfg.read_cal.offset, cdata.cfg.read_cal.size);
		if(rc < 0) {
		  pr_err("[syscamera][%s::%d] error on reading cal data\n", __FUNCTION__, __LINE__);
		  kfree(cal_data);
		  break;
		}
		if (copy_to_user(cdata.cfg.read_cal.cal_data, cal_data, sizeof(uint8_t)*cdata.cfg.read_cal.size)) {
		  pr_err("[syscamera][%s::%d] copy_to_user failed\n", __func__, __LINE__);
		  rc = -EFAULT;
		  kfree(cal_data);
		  break;
		}
		kfree(cal_data);

		break;

	case COMPANION_CMD_LOAD_FIRMWARE_STEP_A:
		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_A\n", __FUNCTION__, __LINE__);
		wdr_mode = -1;
		rc = msm_companion_pll_init(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
#ifdef SKIP_CAL_CRC
		if (retention_mode == 1) {
			rc = retention_mode;
		}
#endif
		break;

	case COMPANION_CMD_LOAD_FIRMWARE_STEP_B:
		if (retention_mode == 1) {
#ifdef SKIP_CAL_CRC
			CDBG("[syscamera][%s::%d] retention mode : skip fw write & crc check\n", __FUNCTION__, __LINE__);
#else
			CDBG("[syscamera][%s::%d] compare CRC calculated inside\n", __FUNCTION__, __LINE__);
			rc = msm_companion_compare_FW_crc(companion_dev);
			if(rc < 0) {
				pr_err("[syscamera][%s::%d] error on compare crc data. retry write fw\n", __FUNCTION__, __LINE__);
				rc = msm_companion_fw_write(companion_dev);
				if(rc < 0) {
					pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
					break;
				}
			}
#endif
			break;
		}

		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_B\n", __FUNCTION__, __LINE__);
		rc = msm_companion_fw_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_CAL_DATA_WRITE:
#ifdef SKIP_CAL_CRC
		if (retention_mode == 1) {
			CDBG("[syscamera][%s::%d] retention mode : skip cal write\n", __FUNCTION__, __LINE__);
			break;
		}
#endif
		CDBG("[syscamera][%s::%d] Writing cal data\n", __FUNCTION__, __LINE__);
		rc = msm_companion_cal_data_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on writing cal data\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_GET_CRC:
#ifdef SKIP_CAL_CRC
		if (retention_mode == 1) {
			CDBG("[syscamera][%s::%d] retention mode : skip get crc\n", __FUNCTION__, __LINE__);
			break;
		}
#endif
		cdata.cfg.crc_check.addr = cdata32->cfg.crc_check.addr;
		cdata.cfg.crc_check.count = cdata32->cfg.crc_check.count;
		cdata.cfg.crc_check.CRC = compat_ptr(cdata32->cfg.crc_check.CRC);
		rc = msm_companion_get_crc(companion_dev, cdata.cfg.crc_check, 0);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_get_crc failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		break;

	case COMPANION_CMD_LOAD_FIRMWARE_STEP_C:
		CDBG("[syscamera][%s::%d] COMPANION_CMD_LOAD_FIRMWARE_STEP_C\n", __FUNCTION__, __LINE__);
		rc = msm_companion_release_arm_reset(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading firmware\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_LOAD_MASTER:
		if (retention_mode == 0) {
			pr_err("[syscamera][%s::%d] Entering retention mode\n", __FUNCTION__, __LINE__);
			retention_mode = 1;
			retention_mode_pwr = 1;// Do not turn off after entering retention mode
		}

		CDBG("[syscamera][%s::%d] Loading master\n", __FUNCTION__, __LINE__);
		rc = msm_companion_master_write(companion_dev);
		if(rc < 0) {
			pr_err("[syscamera][%s::%d] error on loading master\n", __FUNCTION__, __LINE__);
			break;
		}
		break;

	case COMPANION_CMD_STREAM_ON:
		cdata.cfg.stream_on = cdata32->cfg.stream_on;
		CDBG("[syscamera][%s::%d] Companion stream on[enable::%d]\n", __FUNCTION__, __LINE__, cdata.cfg.stream_on);
		if (cdata.cfg.stream_on == 1)
			atomic_set(&comp_streamon_set, 1);
		else
			atomic_set(&comp_streamon_set, 0);
		rc = msm_companion_stream_on(client, cdata.cfg.stream_on);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_stream_on failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		break;

	case COMPANION_CMD_DUMP_REGISTER:
		cdata.cfg.dump_buf = compat_ptr(cdata32->cfg.dump_buf);
		rc = msm_companion_dump_register(companion_dev, dump_buf);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d] msm_companion_dump_register failed\n", __FUNCTION__, __LINE__);
			return -EFAULT;
		}
		if (copy_to_user(cdata.cfg.dump_buf, dump_buf, 0x8AFA)) {
			pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;

	case COMPANION_CMD_WDR_MODE:
		wdr_mode = cdata32->cfg.wdr_mode;
		break;

	case COMPANION_CMD_SET_MODE:
		cdata.cfg.mode_setting.reg_setting = compat_ptr(cdata32->cfg.mode_setting.reg_setting);
		cdata.cfg.mode_setting.size = cdata32->cfg.mode_setting.size;
		cdata.cfg.mode_setting.addr_type = cdata32->cfg.mode_setting.addr_type;
		cdata.cfg.mode_setting.data_type = cdata32->cfg.mode_setting.data_type;
		cdata.cfg.mode_setting.delay= cdata32->cfg.mode_setting.delay;
		CDBG("[syscamera][%s::%d] Companion mode setting Array size = %d, Data type = %d\n", __FUNCTION__, __LINE__, cdata.cfg.mode_setting.size, cdata.cfg.mode_setting.data_type);
		rc = msm_companion_set_mode(companion_dev, cdata.cfg.mode_setting);
		break;

	case COMPANION_CMD_GET_STATS2:
		cdata.cfg.stats2 = compat_ptr(cdata32->cfg.stats2);
		if (NULL == stats2 || NULL == cdata.cfg.stats2) {
			pr_err("[syscamera][%s::%d] source or destination is null[stats2::%p][cfg.stats2::%p]\n", __FUNCTION__, __LINE__, stats2, cdata.cfg.stats2);
			return -EFAULT;
		}
		rc = msm_companion_read_stats2(companion_dev, stats2);
		if (copy_to_user(cdata.cfg.stats2, stats2, stats2_len)) {
			pr_err("[syscamera][%s::%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;

	case COMPANION_CMD_RELEASE:
		CDBG("[syscamera][%s::%d]COMPANION_CMD_RELEASE\n", __FUNCTION__, __LINE__);
		rc = msm_companion_release(companion_dev);
		break;

	default:
		pr_err("[syscamera][%s::%d]failed\n", __FUNCTION__, __LINE__);
		rc = -ENOIOCTLCMD;
		break;
	}
	/* for config data debug */
	//msm_companion_print_config(cdata, cdata32);
	return rc;
}


static long msm_companion_subdev_ioctl32(struct v4l2_subdev *sd,
			unsigned int cmd, void *arg)
{
	int rc = -ENOIOCTLCMD;
	struct companion_device *companion_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&companion_dev->comp_mutex);
	CDBG("[syscamera][%s::%d] \n", __FUNCTION__, __LINE__);
	CDBG("%s: _IOC_TYPE '%c', _IOC_DIR =%d, _IOC_NR %d (0x%08x)\n", __func__,
		_IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd),
		cmd);
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID: //195
		rc = msm_companion_get_subdev_id(companion_dev, arg);
		break;
	case VIDIOC_MSM_COMPANION_IO_CFG: //203
		rc = msm_companion_cmd32(companion_dev, arg);
		break;
	case MSM_SD_SHUTDOWN:
		rc = msm_companion_release(companion_dev);
		break;
	default:
		pr_err_ratelimited("%s: command not found\n", __func__);
	}
	CDBG("%s:%d\n", __func__, __LINE__);
	mutex_unlock(&companion_dev->comp_mutex);
	return rc;
}

static long msm_companion_subdev_do_ioctl32(
	struct file *file, unsigned int cmd, void *arg)
{
#if 1
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);

	CDBG("[syscamera][%s::%d] Enter cmd(%d)\n", __FUNCTION__, __LINE__, _IOC_NR(cmd));
	if (cmd == VIDIOC_MSM_COMPANION_IO_CFG32)
		cmd = VIDIOC_MSM_COMPANION_IO_CFG;

	return msm_companion_subdev_ioctl32(sd, cmd, arg);
#else
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct companion_cfg_data32 *u32 =
		(struct companion_cfg_data32 *)arg;
	struct companion_cfg_data companion_data;

	CDBG("[syscamera][%s::%d] Enter cmd(%d)\n", __FUNCTION__, __LINE__, cmd);
	switch (cmd) {
	case VIDIOC_MSM_COMPANION_IO_CFG32:
		cmd = VIDIOC_MSM_COMPANION_IO_CFG;
		companion_data.cfgtype = u32->cfgtype;
		companion_data.cfg.setting = compat_ptr(u32->cfg.setting);
		companion_data.cfg.stream_on = u32->cfg.stream_on;
		companion_data.cfg.stats2 = compat_ptr(u32->cfg.stats2);
		companion_data.cfg.dump_buf= compat_ptr(u32->cfg.dump_buf);
		companion_data.cfg.read_id= compat_ptr(u32->cfg.read_id);
		companion_data.cfg.rev= compat_ptr(u32->cfg.rev);

		companion_data.cfg.read_cal.cal_data = compat_ptr(u32->cfg.read_cal.cal_data);
		companion_data.cfg.read_cal.size = u32->cfg.read_cal.size;
		companion_data.cfg.read_cal.offset = u32->cfg.read_cal.offset;

		companion_data.cfg.mode_setting.reg_setting = compat_ptr(u32->cfg.mode_setting.reg_setting);
		companion_data.cfg.mode_setting.size = u32->cfg.mode_setting.size;
		companion_data.cfg.mode_setting.addr_type = u32->cfg.mode_setting.addr_type;
		companion_data.cfg.mode_setting.data_type = u32->cfg.mode_setting.data_type;
		companion_data.cfg.mode_setting.delay= u32->cfg.mode_setting.delay;

		companion_data.cfg.crc_check.addr = u32->cfg.crc_check.addr;
		companion_data.cfg.crc_check.count = u32->cfg.crc_check.count;
		companion_data.cfg.crc_check.CRC = compat_ptr(u32->cfg.crc_check.CRC);

		companion_data.cfg.fw_bin.version = compat_ptr(u32->cfg.fw_bin.version);
		companion_data.cfg.fw_bin.buffer= compat_ptr(u32->cfg.fw_bin.buffer);
		companion_data.cfg.fw_bin.size = u32->cfg.fw_bin.size;

		companion_data.isDump = u32->isDump;
		CDBG("[syscamera][%s::%d] Enter cmd(%d)\n", __FUNCTION__, __LINE__, cmd);
		return msm_companion_subdev_ioctl32(sd, cmd, &companion_data);
		break;
	default:
		CDBG("[syscamera][%s::%d] Enter cmd(%d)\n", __FUNCTION__, __LINE__, cmd);
		return msm_companion_subdev_ioctl32(sd, cmd, arg);
	}
#endif
}

static long msm_companion_subdev_fops_ioctl32(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return video_usercopy(file, cmd, arg, msm_companion_subdev_do_ioctl32);
}
#endif


static const struct v4l2_subdev_internal_ops msm_companion_internal_ops;

static struct v4l2_subdev_core_ops msm_companion_subdev_core_ops = {
	.ioctl = msm_companion_subdev_ioctl,
	.subscribe_event = msm_companion_subscribe_event,
	.unsubscribe_event = msm_companion_unsubscribe_event,
};

static const struct v4l2_subdev_ops msm_companion_subdev_ops = {
	.core = &msm_companion_subdev_core_ops,
};

static struct msm_camera_i2c_fn_t msm_companion_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
	.i2c_write_conf_tbl = msm_camera_cci_i2c_write_conf_tbl,
};

static struct msm_camera_i2c_fn_t msm_companion_spi_func_tbl = {
	.i2c_read = msm_camera_spi_read,
	.i2c_read_seq = msm_camera_spi_read_seq,
	.i2c_read_multi = msm_camera_spi_read_multi,
	.i2c_write_seq = msm_camera_spi_write_seq,
	.i2c_write = msm_camera_spi_write,
	.i2c_write_table = msm_camera_spi_write_table,
	.i2c_write_burst = msm_camera_spi_write_burst,
};

#define msm_companion_spi_parse_cmd(spic, str, name, out, size)		\
	{								\
		if (of_property_read_u32_array(				\
			spic->spi_master->dev.of_node,			\
			str, out, size)) {				\
			return -EFAULT;					\
		} else {						\
			spic->cmd_tbl.name.opcode = out[0];		\
			spic->cmd_tbl.name.addr_len = out[1];		\
			spic->cmd_tbl.name.dummy_len = out[2];		\
			spic->cmd_tbl.name.delay_intv = out[3];		\
			spic->cmd_tbl.name.delay_count = out[4];		\
		}							\
	}

static int msm_companion_spi_parse_of(struct msm_camera_spi_client *spic)
{
	int rc = -EFAULT;
	uint32_t tmp[5];
	struct device_node *of = spic->spi_master->dev.of_node;
        memset(&spic->cmd_tbl, 0x00, sizeof(struct msm_camera_spi_inst_tbl));
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-read", read, tmp, 5);
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-readseq", read_seq, tmp, 5);
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-queryid", query_id, tmp, 5);
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-pprog", page_program, tmp, 5);
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-readst", read_status, tmp, 5);
	msm_companion_spi_parse_cmd(spic, "qcom,spiop-erase", erase, tmp, 5);

	rc = of_property_read_u32(of, "qcom,spi-busy-mask", tmp);
	if (rc < 0) {
			pr_err("[syscamera][%s::%d]Failed to get busy mask\n", __FUNCTION__, __LINE__);
			return rc;
	}
	spic->busy_mask = tmp[0];
	rc = of_property_read_u32(of, "qcom,spi-page-size", tmp);
	if (rc < 0) {
			pr_err("[syscamera][%s::%d]Failed to get page size\n", __FUNCTION__, __LINE__);
			return rc;
	}
	spic->page_size = tmp[0];
	rc = of_property_read_u32(of, "qcom,spi-erase-size", tmp);
	if (rc < 0) {
			pr_err("[syscamera][%s::%d]Failed to get erase size\n", __FUNCTION__, __LINE__);
			return rc;
	}
	spic->erase_size = tmp[0];
	return 0;
}

static int msm_companion_spi_setup(struct spi_device *spi)
{
	struct msm_camera_i2c_client *client = NULL;
	struct msm_camera_spi_client *spi_client = NULL;
	struct companion_device *companion_dev = NULL;
	struct companion_isr_resource *isr_resource = NULL;
	int32_t rc = 0;
	uint8_t tmp[2];
	companion_dev = kzalloc(sizeof(struct companion_device), GFP_KERNEL);
	if (!companion_dev) {
		pr_err("[syscamera][%s::%d]no enough memory\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	client = &companion_dev->companion_i2c_client;
	spi_client = kzalloc(sizeof(struct msm_camera_spi_client), GFP_KERNEL);
	if (!spi_client) {
		pr_err("[syscamera][%s::%d]kzalloc failed\n", __FUNCTION__, __LINE__);
		kfree(companion_dev);
		return -ENOMEM;
	}
	rc = of_property_read_u32(spi->dev.of_node, "cell-index",
				  &companion_dev->subdev_id);
	pr_err("[syscamera][%s::%d] cell-index %d, rc %d\n", __FUNCTION__, __LINE__, companion_dev->subdev_id, rc);
	if (rc) {
		pr_err("[syscamera][%s::%d]failed rc %d\n", __FUNCTION__, __LINE__, rc);
		goto device_free;
	}

	isr_resource = &companion_dev->isr_resource;
	isr_resource->comp_gpio_irq_pin = of_get_named_gpio(spi->dev.of_node, "qcom,gpio-irq", 0);
	pr_err("[syscamera][%s::%d] gpio-irq %d\n", __FUNCTION__, __LINE__,
		isr_resource->comp_gpio_irq_pin);
	if (!gpio_is_valid(isr_resource->comp_gpio_irq_pin)) {
		pr_err("[syscamera][%s::%d]failed\n", __FUNCTION__, __LINE__);
		rc = ENOMEM;
		goto device_free;
	}

	rc = of_property_read_u32(spi->dev.of_node,
					"qcom,companion-id", &companion_dev->companion_device_id);
	pr_err("[syscamera][%s::%d] companion-device-id %d, rc %d\n", __FUNCTION__, __LINE__,
		companion_dev->companion_device_id, rc);
	if (rc) {
		pr_err("[syscamera][%s::%d]Failed to get companion id\n", __FUNCTION__, __LINE__);
		goto device_free;
	}

	client->spi_client = spi_client;
	spi_client->spi_master = spi;
	client->i2c_func_tbl = &msm_companion_spi_func_tbl;
	client->addr_type = MSM_CAMERA_I2C_WORD_ADDR;

	/* set spi instruction info */
	spi_client->retry_delay = 1;
	spi_client->retries = 0;
	if (msm_companion_spi_parse_of(spi_client)) {
		dev_err(&spi->dev,
			"%s: Error parsing device properties\n", __func__);
		goto device_free;
	}

	rc = msm_camera_spi_query_id(client, 0, &tmp[0], 2);
	if (!rc) {
		spi_client->mfr_id0 = tmp[0];
		spi_client->device_id0 = tmp[1];
	}

	/*query spi device may be added here*/
	rc = gpio_request(isr_resource->comp_gpio_irq_pin, "comp-gpio-irq");
	if (rc) {
		pr_err("[syscamera][%s::%d]err gpio request\n", __FUNCTION__, __LINE__);
		goto device_free;
	}
	CDBG("[syscamera][%s::%d]gpio_request success\n", __FUNCTION__, __LINE__);

	isr_resource->comp_irq_num =
		gpio_to_irq(isr_resource->comp_gpio_irq_pin);
	if (isr_resource->comp_irq_num < 0) {
		pr_err("[syscamera][%s::%d]irq request failed\n", __FUNCTION__, __LINE__);
		goto free_gpio;
	}

	rc = request_irq(isr_resource->comp_irq_num, msm_companion_process_irq,
		IRQF_TRIGGER_RISING, "companion", companion_dev);
	if (rc < 0) {
		pr_err("[syscamera][%s::%d]irq request failed\n", __FUNCTION__, __LINE__);
		goto device_free;
	}
	disable_irq(isr_resource->comp_irq_num);
	CDBG("[syscamera][%s::%d]request_irq success\n", __FUNCTION__, __LINE__);

#ifdef STATS2_WORKQUEUE //aswoogi temp
	/* Initialize workqueue */
	companion_dev->companion_queue= alloc_workqueue("companion_queue", WQ_HIGHPRI|WQ_CPU_INTENSIVE, 0);
	if (!companion_dev->companion_queue) {
		pr_err("[syscamera][%s::%d]could not create companion_queue for companion dev id %d\n",
			__FUNCTION__, __LINE__, companion_dev->companion_device_id);
		goto device_free;
	}

	INIT_WORK(&companion_dev->companion_read_work, msm_companion_stat2_read);
#endif
 	mutex_init(&companion_dev->comp_mutex);
	/* initialize subdev */
	v4l2_spi_subdev_init(&companion_dev->msm_sd.sd,
		companion_dev->companion_i2c_client.spi_client->spi_master,
		&msm_companion_subdev_ops);
	v4l2_set_subdevdata(&companion_dev->msm_sd.sd, companion_dev);
	companion_dev->msm_sd.sd.internal_ops = &msm_companion_internal_ops;
	companion_dev->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	companion_dev->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	media_entity_init(&companion_dev->msm_sd.sd.entity, 0, NULL, 0);
	companion_dev->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	companion_dev->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_COMPANION;
	companion_dev->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0xA;
	msm_sd_register(&companion_dev->msm_sd);

#ifdef CONFIG_COMPAT
	companion_dev->msm_sd.sd.devnode->fops = &msm_companion_v4l2_subdev_fops;
#endif

	// Initialize resources
	companion_dev->companion_cal_tbl = NULL;
	companion_dev->companion_cal_tbl_size = 0;
	companion_dev->eeprom_fw_bin = NULL;
	companion_dev->eeprom_fw_bin_size = 0;
	companion_dev->device_type = MSM_CAMERA_SPI_DEVICE;

	CDBG("[syscamera][%s::%d]spi probe success =%d X\n", __FUNCTION__, __LINE__, rc);
	return 0;
free_gpio:
	gpio_free(isr_resource->comp_gpio_irq_pin);
device_free:
	kfree(companion_dev);
	kfree(spi_client);
	return rc;
}

static int msm_companion_spi_probe(struct spi_device *spi)
{
	int irq, cs, cpha, cpol, cs_high;

	CDBG("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);
#if defined(CONFIG_EXTCON) && defined(CONFIG_BATTERY_SAMSUNG)
	if (poweroff_charging == 1) {
		pr_err("forced return companion_spi_probe at lpm mode\n");
		return 0;
	}
#endif

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi_setup(spi);

	irq = spi->irq;
	cs = spi->chip_select;
	cpha = (spi->mode & SPI_CPHA) ? 1 : 0;
	cpol = (spi->mode & SPI_CPOL) ? 1 : 0;
	cs_high = (spi->mode & SPI_CS_HIGH) ? 1 : 0;
	dev_info(&spi->dev, "irq[%d] cs[%x] CPHA[%x] CPOL[%x] CS_HIGH[%x]\n",
			irq, cs, cpha, cpol, cs_high);
	dev_info(&spi->dev, "max_speed[%u]\n", spi->max_speed_hz);
#if 1
	pr_err("%s: irq[%d] cs[%x] CPHA[%x] CPOL[%x] CS_HIGH[%x]\n",
			__func__, irq, cs, cpha, cpol, cs_high);
	pr_err("%s: max_speed[%u]\n", __func__, spi->max_speed_hz);
#endif
	return msm_companion_spi_setup(spi);
}

static int msm_companion_spi_remove(struct spi_device *sdev)
{
	struct v4l2_subdev *sd = spi_get_drvdata(sdev);
	struct companion_device *companion_dev = NULL;
	if (!sd) {
		pr_err("[syscamera][%s::%d]Subdevice is NULL\n", __FUNCTION__, __LINE__);
		return 0;
	}

	companion_dev = (struct companion_device *)v4l2_get_subdevdata(sd);
	if (!companion_dev) {
		pr_err("[syscamera][%s::%d]companion device is NULL\n", __FUNCTION__, __LINE__);
		return 0;
	}

#ifdef STATS2_WORKQUEUE
	if (companion_dev->companion_queue) {
		destroy_workqueue(companion_dev->companion_queue);
		companion_dev->companion_queue = NULL;
	}
#endif

	kfree(companion_dev->companion_i2c_client.spi_client);
	kfree(companion_dev);
	return 0;
}
static int msm_companion_probe(struct platform_device *pdev)
{
	struct companion_device *companion_dev = NULL;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_camera_i2c_client *client = NULL;
	int32_t rc = 0;

	pr_warn("[syscamera][%s::%d][E]\n", __FUNCTION__, __LINE__);
#if defined(CONFIG_EXTCON) && defined(CONFIG_BATTERY_SAMSUNG)
	if (poweroff_charging == 1) {
		pr_err("forced return companion_probe at lpm mode\n");
		return rc;
	}
#endif

	if (pdev->dev.of_node) {
		rc = of_property_read_u32((&pdev->dev)->of_node,
			"cell-index", &pdev->id);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d]failed to read cell-index\n", __FUNCTION__, __LINE__);
			goto companion_no_resource;
		}
		CDBG("[syscamera][%s::%d] device id %d\n", __FUNCTION__, __LINE__, pdev->id);
	}

	companion_dev = kzalloc(sizeof(struct companion_device), GFP_KERNEL);
	if (!companion_dev) {
		pr_err("[syscamera][%s::%d]no enough memory\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	companion_dev->subdev_id = pdev->id;
	if (pdev->dev.of_node) {
		rc = msm_companion_get_dt_data(pdev->dev.of_node, companion_dev);
		if (rc < 0) {
			pr_err("[syscamera][%s::%d]failed to msm_companion_get_dt_data\n", __FUNCTION__, __LINE__);
			kfree(companion_dev);
			return rc;
		}
	}

	companion_dev->companion_i2c_client.cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!companion_dev->companion_i2c_client.cci_client) {
		pr_err("[syscamera][%s::%d]memory allocation fail\n", __FUNCTION__, __LINE__);
		kfree(companion_dev);
		return rc;
	}

	client = &companion_dev->companion_i2c_client;
	client->addr_type = MSM_CAMERA_I2C_WORD_ADDR;
	g_i2c_client = &companion_dev->companion_i2c_client;
	cci_client = companion_dev->companion_i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = companion_dev->cci_i2c_master;
	cci_client->sid =
		companion_dev->slave_info->sensor_slave_addr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;
	cci_client->i2c_freq_mode = I2C_FAST_MODE;
	if (!companion_dev->companion_i2c_client.i2c_func_tbl) {
		pr_err("[syscamera][%s::%d] i2c_func_tbl=cci \n", __FUNCTION__, __LINE__);
		companion_dev->companion_i2c_client.i2c_func_tbl =
			&msm_companion_cci_func_tbl;
	} else {
		pr_err("[syscamera][%s::%d] i2c_func_tbl=cci skipped \n", __FUNCTION__, __LINE__);
	}
	mutex_init(&companion_dev->comp_mutex);
	v4l2_subdev_init(&companion_dev->msm_sd.sd, &msm_companion_subdev_ops);
	v4l2_set_subdevdata(&companion_dev->msm_sd.sd, companion_dev);
	platform_set_drvdata(pdev, &companion_dev->msm_sd.sd);

	companion_dev->pdev = pdev;
	companion_dev->msm_sd.sd.internal_ops = &msm_companion_internal_ops;
	companion_dev->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	companion_dev->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(companion_dev->msm_sd.sd.name,
			ARRAY_SIZE(companion_dev->msm_sd.sd.name), "msm_companion");
	media_entity_init(&companion_dev->msm_sd.sd.entity, 0, NULL, 0);
	companion_dev->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	companion_dev->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_COMPANION;
	companion_dev->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x9;
	msm_sd_register(&companion_dev->msm_sd);

#ifdef CONFIG_COMPAT
	msm_companion_v4l2_subdev_fops = v4l2_subdev_fops;
	msm_companion_v4l2_subdev_fops.compat_ioctl32 = msm_companion_subdev_fops_ioctl32;
	companion_dev->msm_sd.sd.devnode->fops = &msm_companion_v4l2_subdev_fops;
#endif

	// Initialize resources
	companion_dev->companion_cal_tbl = NULL;
	companion_dev->companion_cal_tbl_size = 0;
	companion_dev->eeprom_fw_bin = NULL;
	companion_dev->eeprom_fw_bin_size = 0;
	companion_dev->companion_state = COMP_POWER_DOWN;
	companion_dev->device_type = MSM_CAMERA_PLATFORM_DEVICE;
	pr_warn("[syscamera][%s::%d] platform probe success\n", __FUNCTION__, __LINE__);
	return 0;

companion_no_resource:
	pr_err("[syscamera][%s::%d] probe failed\n", __FUNCTION__, __LINE__);
	return rc;
}

static const struct of_device_id msm_companion_dt_match[] = {
	{.compatible = "qcom,companion"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_companion_dt_match);

static struct platform_driver companion_driver = {
	.probe = msm_companion_probe,
	.driver = {
		.name = MSM_COMP_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = msm_companion_dt_match,
	},
};

static struct spi_driver companion_spi_driver = {
	.driver = {
		.name = "qcom_companion",
		.owner = THIS_MODULE,
		.of_match_table = msm_companion_dt_match,
	},
	.probe = msm_companion_spi_probe,
	.remove = msm_companion_spi_remove,
};

static int __init msm_companion_init_module(void)
{
	int32_t rc = 0, spi_rc = 0;
	pr_warn("[syscamera][%s::%d]init companion module\n", __FUNCTION__, __LINE__);
	rc = platform_driver_register(&companion_driver);
	spi_rc = spi_register_driver(&companion_spi_driver);

	pr_warn("[syscamera][%s::%d] rc = %d, spi_rc = %d\n", __FUNCTION__, __LINE__, rc, spi_rc);

	if (rc < 0 && spi_rc < 0)
		pr_err("%s:%d probe failed\n", __FUNCTION__, __LINE__);

	return spi_rc;
}

static void __exit msm_companion_exit_module(void)
{
	platform_driver_unregister(&companion_driver);
	spi_unregister_driver(&companion_spi_driver);
}

late_initcall(msm_companion_init_module);
module_exit(msm_companion_exit_module);
MODULE_DESCRIPTION("MSM Companion driver");
MODULE_LICENSE("GPL v2");
