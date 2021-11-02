/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "MSM-SENSOR-INIT %s:%d " fmt "\n", __func__, __LINE__

/* Header files */
#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
#include <linux/device.h>
#endif
#include "msm_sensor_init.h"
#include "msm_sensor_driver.h"
#include "msm_sensor.h"
#include "msm_sd.h"

/* Logging macro */
#undef CDBG
#define CDBG(fmt, args...) pr_debug(fmt, ##args)

#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
extern struct kset *devices_kset;
#endif
static struct msm_sensor_init_t *s_init;
struct class *camera_class;
static struct v4l2_file_operations msm_sensor_init_v4l2_subdev_fops;
/* Static function declaration */
static long msm_sensor_init_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg);

/* Static structure declaration */
static struct v4l2_subdev_core_ops msm_sensor_init_subdev_core_ops = {
	.ioctl = msm_sensor_init_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_sensor_init_subdev_ops = {
	.core = &msm_sensor_init_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops msm_sensor_init_internal_ops;

static int msm_sensor_wait_for_probe_done(struct msm_sensor_init_t *s_init)
{
	int rc;
	int tm = 10000;
	if (s_init->module_init_status == 1) {
		CDBG("msm_cam_get_module_init_status -2\n");
		return 0;
	}
	rc = wait_event_timeout(s_init->state_wait,
		(s_init->module_init_status == 1), msecs_to_jiffies(tm));
	if (rc == 0)
		pr_err("%s:%d wait timeout\n", __func__, __LINE__);

	return rc;
}

/* Static function definition */
static int32_t msm_sensor_driver_cmd(struct msm_sensor_init_t *s_init,
	void *arg)
{
	int32_t                      rc = 0;
	struct sensor_init_cfg_data *cfg = (struct sensor_init_cfg_data *)arg;

	/* Validate input parameters */
	if (!s_init || !cfg) {
		pr_err("failed: s_init %pK cfg %pK", s_init, cfg);
		return -EINVAL;
	}

	switch (cfg->cfgtype) {
	case CFG_SINIT_PROBE:
		mutex_lock(&s_init->imutex);
		s_init->module_init_status = 0;
		rc = msm_sensor_driver_probe(cfg->cfg.setting,
			&cfg->probed_info,
			cfg->entity_name);
		mutex_unlock(&s_init->imutex);
		if (rc < 0)
			pr_err("%s failed (non-fatal) rc %d", __func__, rc);
		break;

	case CFG_SINIT_PROBE_DONE:
		s_init->module_init_status = 1;
		wake_up(&s_init->state_wait);
		break;

	case CFG_SINIT_PROBE_WAIT_DONE:
		msm_sensor_wait_for_probe_done(s_init);
		break;

	default:
		pr_err("default");
		break;
	}

	return rc;
}

static long msm_sensor_init_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	long rc = 0;
	struct msm_sensor_init_t *s_init = v4l2_get_subdevdata(sd);
	CDBG("Enter");

	/* Validate input parameters */
	if (!s_init) {
		pr_err("failed: s_init %pK", s_init);
		return -EINVAL;
	}

	switch (cmd) {
	case VIDIOC_MSM_SENSOR_INIT_CFG:
		rc = msm_sensor_driver_cmd(s_init, arg);
		break;

	default:
		pr_err_ratelimited("default\n");
		break;
	}

	return rc;
}

#ifdef CONFIG_COMPAT
static long msm_sensor_init_subdev_do_ioctl(
	struct file *file, unsigned int cmd, void *arg)
{
	int32_t             rc = 0;
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct sensor_init_cfg_data32 *u32 =
		(struct sensor_init_cfg_data32 *)arg;
	struct sensor_init_cfg_data sensor_init_data;

	switch (cmd) {
	case VIDIOC_MSM_SENSOR_INIT_CFG32:
		memset(&sensor_init_data, 0, sizeof(sensor_init_data));
		sensor_init_data.cfgtype = u32->cfgtype;
		sensor_init_data.cfg.setting = compat_ptr(u32->cfg.setting);
		cmd = VIDIOC_MSM_SENSOR_INIT_CFG;
		rc = msm_sensor_init_subdev_ioctl(sd, cmd, &sensor_init_data);
		if (rc < 0) {
			pr_err("%s:%d VIDIOC_MSM_SENSOR_INIT_CFG failed (non-fatal)",
				__func__, __LINE__);
			return rc;
		}
		u32->probed_info = sensor_init_data.probed_info;
		strlcpy(u32->entity_name, sensor_init_data.entity_name,
			sizeof(sensor_init_data.entity_name));
		return 0;
	default:
		return msm_sensor_init_subdev_ioctl(sd, cmd, arg);
	}
}

static long msm_sensor_init_subdev_fops_ioctl(
	struct file *file, unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, msm_sensor_init_subdev_do_ioctl);
}
#endif

char cam_fw_ver[40] = "NULL NULL\n";//multi module
static ssize_t back_camera_firmware_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", cam_fw_ver);
	return snprintf(buf, sizeof(cam_fw_ver), "%s", cam_fw_ver);
}

static ssize_t back_camera_firmware_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cam_fw_ver, sizeof(cam_fw_ver), "%s", buf);

	return size;
}

static ssize_t back_camera_type_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	char cam_type_lsi[] = "SLSI_S5K2L1SX\n";
	char cam_type_sony[] = "SONY_IMX260\n";

  if (cam_fw_ver[4] == 'L') {
    return snprintf(buf, sizeof(cam_type_lsi), "%s", cam_type_lsi);
  } else {
    return snprintf(buf, sizeof(cam_type_sony), "%s", cam_type_sony);
  }
}

static ssize_t front_camera_type_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
#if defined(CONFIG_S5K5E3YX)
	char cam_type[] = "SLSI_S5K5E3YX\n";
#else
	char cam_type[] = "SLSI_S5K4E6XX\n";
#endif
	return snprintf(buf, sizeof(cam_type), "%s", cam_type);
}

char cam_fw_user_ver[40] = "NULL\n";//multi module
static ssize_t back_camera_firmware_user_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", cam_fw_user_ver);
	return snprintf(buf, sizeof(cam_fw_user_ver), "%s", cam_fw_user_ver);
}

static ssize_t back_camera_firmware_user_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cam_fw_user_ver, sizeof(cam_fw_user_ver), "%s", buf);

	return size;
}

char cam_fw_factory_ver[40] = "NULL\n";//multi module
static ssize_t back_camera_firmware_factory_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", cam_fw_factory_ver);
	return snprintf(buf, sizeof(cam_fw_factory_ver), "%s", cam_fw_factory_ver);
}

static ssize_t back_camera_firmware_factory_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cam_fw_factory_ver, sizeof(cam_fw_factory_ver), "%s", buf);

	return size;
}

char cam_fw_full_ver[40] = "NULL NULL NULL\n";//multi module
static ssize_t back_camera_firmware_full_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", cam_fw_full_ver);
	return snprintf(buf, sizeof(cam_fw_full_ver), "%s", cam_fw_full_ver);
}

static ssize_t back_camera_firmware_full_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cam_fw_full_ver, sizeof(cam_fw_full_ver), "%s", buf);

	return size;
}

char cam_load_fw[25] = "NULL\n";
static ssize_t back_camera_firmware_load_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_load_fw : %s\n", cam_load_fw);
	return snprintf(buf, sizeof(cam_load_fw), "%s", cam_load_fw);
}

static ssize_t back_camera_firmware_load_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cam_load_fw, sizeof(cam_load_fw), "%s\n", buf);
	return size;
}

#if defined(CONFIG_COMPANION2) || defined(CONFIG_COMPANION3)
char companion_fw_ver[40] = "NULL NULL NULL\n";
static ssize_t back_companion_firmware_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] companion_fw_ver : %s\n", companion_fw_ver);
	return snprintf(buf, sizeof(companion_fw_ver), "%s", companion_fw_ver);
}

static ssize_t back_companion_firmware_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(companion_fw_ver, sizeof(companion_fw_ver), "%s", buf);

	return size;
}
#endif

char fw_crc[10] = "NN\n";//camera and companion
static ssize_t back_fw_crc_check_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] fw_crc : %s\n", fw_crc);
	return snprintf(buf, sizeof(fw_crc), "%s", fw_crc);
}

static ssize_t back_fw_crc_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(fw_crc, sizeof(fw_crc), "%s", buf);

	return size;
}

char cal_crc[37] = "NULL NULL NULL\n";
static ssize_t back_cal_data_check_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cal_crc : %s\n", cal_crc);
	return snprintf(buf, sizeof(cal_crc), "%s", cal_crc);
}

static ssize_t back_cal_data_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(cal_crc, sizeof(cal_crc), "%s", buf);

	return size;
}

char module_info[40] = "NULL\n";
static ssize_t back_module_info_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] module_info : %s\n", module_info);
	return snprintf(buf, sizeof(module_info), "%s", module_info);
}

static ssize_t back_module_info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(module_info, sizeof(module_info), "%s", buf);

	return size;
}

char isp_core[10];
static ssize_t back_isp_core_check_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
#if 0// Power binning is used
	char cam_isp_core[] = "0.8V\n";
	return snprintf(buf, sizeof(cam_isp_core), "%s", cam_isp_core);
#else
	CDBG("[FW_DBG] isp_core : %s\n", isp_core);
	return snprintf(buf, sizeof(isp_core), "%s\n", isp_core);
#endif
}

static ssize_t back_isp_core_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(isp_core, sizeof(isp_core), "%s", buf);

	return size;
}

uint32_t rear_af_cal_pan;
uint32_t rear_af_cal_macro;
static ssize_t back_camera_afcal_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] rear_af_cal_pan: %d, rear_af_cal_macro: %d\n", rear_af_cal_pan, rear_af_cal_macro);
	return sprintf(buf, "1 %d %d\n", rear_af_cal_macro, rear_af_cal_pan);
}

static ssize_t front_camera_afcal_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "NULL NULL NULL\n");
}

#if defined(CONFIG_MSM_FRONT_EEPROM)
char front_cam_fw_ver[25] = "NULL NULL\n";
#else
char front_cam_fw_ver[25] = "S5K5E3YX N\n";
#endif
static ssize_t front_camera_firmware_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] front_cam_fw_ver : %s\n", front_cam_fw_ver);
	return snprintf(buf, sizeof(front_cam_fw_ver), "%s", front_cam_fw_ver);
}

static ssize_t front_camera_firmware_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(front_cam_fw_ver, sizeof(front_cam_fw_ver), "%s", buf);

	return size;
}

#if defined(CONFIG_MSM_FRONT_EEPROM)
char front_cam_fw_full_ver[40] = "NULL NULL NULL\n";
#else
char front_cam_fw_full_ver[40] = "S5K5E3YX N N\n";
#endif
static ssize_t front_camera_firmware_full_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] front_cam_fw_full_ver : %s\n", front_cam_fw_full_ver);
	return snprintf(buf, sizeof(front_cam_fw_full_ver), "%s", front_cam_fw_full_ver);
}
static ssize_t front_camera_firmware_full_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(front_cam_fw_full_ver, sizeof(front_cam_fw_full_ver), "%s", buf);
	return size;
}

#if defined(CONFIG_MSM_FRONT_EEPROM)
char front_cam_fw_user_ver[40] = "NULL\n";
#else
char front_cam_fw_user_ver[40] = "OK\n";
#endif
static ssize_t front_camera_firmware_user_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", front_cam_fw_user_ver);
	return snprintf(buf, sizeof(front_cam_fw_user_ver), "%s", front_cam_fw_user_ver);
}

static ssize_t front_camera_firmware_user_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(front_cam_fw_user_ver, sizeof(front_cam_fw_user_ver), "%s", buf);

	return size;
}

#if defined(CONFIG_MSM_FRONT_EEPROM)
char front_cam_fw_factory_ver[40] = "NULL\n";
#else
char front_cam_fw_factory_ver[40] = "OK\n";
#endif
static ssize_t front_camera_firmware_factory_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_fw_ver : %s\n", front_cam_fw_factory_ver);
	return snprintf(buf, sizeof(front_cam_fw_factory_ver), "%s", front_cam_fw_factory_ver);
}

static ssize_t front_camera_firmware_factory_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(front_cam_fw_factory_ver, sizeof(front_cam_fw_factory_ver), "%s", buf);

	return size;
}

#if defined (CONFIG_CAMERA_SYSFS_V2)
char rear_cam_info[100] = "NULL\n";	//camera_info
static ssize_t rear_camera_info_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_info : %s\n", rear_cam_info);
	return snprintf(buf, sizeof(rear_cam_info), "%s", rear_cam_info);
}

static ssize_t rear_camera_info_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
//	snprintf(rear_cam_info, sizeof(rear_cam_info), "%s", buf);

	return size;
}

char front_cam_info[100] = "NULL\n";	//camera_info
static ssize_t front_camera_info_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] cam_info : %s\n", front_cam_info);
	return snprintf(buf, sizeof(front_cam_info), "%s", front_cam_info);
}

static ssize_t front_camera_info_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
//	snprintf(front_cam_info, sizeof(front_cam_info), "%s", buf);

	return size;
}
#endif

#if defined(CONFIG_SEC_GRACEQLTE_PROJECT)
char iris_cam_fw_ver[40] = "UNKNOWN N\n";
static ssize_t iris_camera_firmware_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] iris_cam_fw_ver : %s\n", cam_fw_ver);
	return snprintf(buf, sizeof(iris_cam_fw_ver), "%s", iris_cam_fw_ver);
}

static ssize_t iris_camera_firmware_store(struct device *dev,
					 struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(iris_cam_fw_ver, sizeof(iris_cam_fw_ver), "%s", buf);
	return size;
}

char iris_cam_fw_full_ver[40] = "UNKNOWN N N\n";
static ssize_t iris_camera_firmware_full_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] iris_cam_fw_full_ver : %s\n", iris_cam_fw_full_ver);
	return snprintf(buf, sizeof(iris_cam_fw_full_ver), "%s", iris_cam_fw_full_ver);
}

static ssize_t iris_camera_firmware_full_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(iris_cam_fw_full_ver, sizeof(iris_cam_fw_full_ver), "%s", buf);
	return size;
}

char iris_cam_fw_user_ver[40] = "OK\n";
static ssize_t iris_camera_firmware_user_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] iris_cam_fw_user_ver : %s\n", iris_cam_fw_user_ver);
	return snprintf(buf, sizeof(iris_cam_fw_user_ver), "%s", iris_cam_fw_user_ver);
}

static ssize_t iris_camera_firmware_user_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(iris_cam_fw_user_ver, sizeof(iris_cam_fw_user_ver), "%s", buf);
	return size;
}

char iris_cam_fw_factory_ver[40] = "OK\n";
static ssize_t iris_camera_firmware_factory_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] iris_cam_fw_factory_ver : %s\n", iris_cam_fw_factory_ver);
	return snprintf(buf, sizeof(iris_cam_fw_factory_ver), "%s", iris_cam_fw_factory_ver);
}

static ssize_t iris_camera_firmware_factory_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
	snprintf(iris_cam_fw_factory_ver, sizeof(iris_cam_fw_factory_ver), "%s", buf);
	return size;
}

#if defined(CONFIG_CAMERA_SYSFS_V2)
char iris_cam_info[100] = "NULL\n";	//camera_info
static ssize_t iris_camera_info_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] iris_cam_info : %s\n", iris_cam_info);
	return snprintf(buf, sizeof(iris_cam_info), "%s", iris_cam_info);
}

static ssize_t iris_camera_info_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
//	snprintf(front_cam_info, sizeof(front_cam_info), "%s", buf);
	return size;
}
#endif
#endif

#define FROM_SENSOR_ID_SIZE 16
char rear_sensor_id[FROM_SENSOR_ID_SIZE + 1] = "\0";
static ssize_t rear_sensorid_exif_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	void * ret = NULL;
	CDBG("[FW_DBG] rear_sensor_id : %s\n", rear_sensor_id);

	ret = memcpy(buf, rear_sensor_id, sizeof(rear_sensor_id));
	if (ret)
		return sizeof(rear_sensor_id);
	return 0;
}

static ssize_t rear_sensorid_exif_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
//	snprintf(rear_sensor_id, sizeof(rear_sensor_id), "%s", buf);

	return size;
}

char front_sensor_id[FROM_SENSOR_ID_SIZE + 1] = "\0";
static ssize_t front_sensorid_exif_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	void * ret = NULL;
	CDBG("[FW_DBG] front_sensor_id : %s\n", front_sensor_id);

	ret = memcpy(buf, front_sensor_id, sizeof(front_sensor_id));
	if (ret)
		return sizeof(front_sensor_id);
	return 0;
}

static ssize_t front_sensorid_exif_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("[FW_DBG] buf : %s\n", buf);
//	snprintf(front_sensor_id, sizeof(front_sensor_id), "%s", buf);

	return size;
}

#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
#define FROM_MODULE_ID_SIZE	10
uint8_t rear_module_id[FROM_MODULE_ID_SIZE + 1] = "\0";
static ssize_t back_camera_moduleid_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	CDBG("[FW_DBG] rear_module_id : %c%c%c%c%c%02X%02X%02X%02X%02X\n",
	  rear_module_id[0], rear_module_id[1], rear_module_id[2], rear_module_id[3], rear_module_id[4],
	  rear_module_id[5], rear_module_id[6], rear_module_id[7], rear_module_id[8], rear_module_id[9]);
	return sprintf(buf, "%c%c%c%c%c%02X%02X%02X%02X%02X\n",
	  rear_module_id[0], rear_module_id[1], rear_module_id[2], rear_module_id[3], rear_module_id[4],
	  rear_module_id[5], rear_module_id[6], rear_module_id[7], rear_module_id[8], rear_module_id[9]);
}
#endif

#if defined(CONFIG_COMPANION3)
extern int comp_fac_i2c_check;
extern uint16_t comp_fac_valid_check;
static ssize_t back_camera_comp_ic_check_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	CDBG("%s: comp_fac_i2c_check[%u] comp_fac_valid_check[0x%04x]\n", __func__, comp_fac_i2c_check, comp_fac_valid_check);

	if (comp_fac_i2c_check < 0)
		len = sprintf(buf, "%s\n", "NG_I2C");
	else if (comp_fac_valid_check)
		len = sprintf(buf, "%04X\n", comp_fac_valid_check);
	else
		len = sprintf(buf, "%s\n", "NG");

	return len;
}

static ssize_t back_camera_comp_ic_check_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("%s: buf[0]=%c\n", __func__, buf[0]);
	switch (buf[0]) {
	case '0':
		break;
	case '1':
		break;
	default:
		pr_debug("%s: %c\n", __func__, buf[0]);
		break;
	}

	return size;
}
#endif

#define SSRM_CAMERA_INFO_SIZE 64
char ssrm_camera_info[SSRM_CAMERA_INFO_SIZE + 1] = "\0";
static ssize_t ssrm_camera_info_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
    int rc = 0;
	CDBG("ssrm_camera_info : %s\n", ssrm_camera_info);

	rc = scnprintf(buf, PAGE_SIZE, "%s", ssrm_camera_info);
	if (rc)
		return rc;
	return 0;
}

static ssize_t ssrm_camera_info_store(struct device *dev,
					  struct device_attribute *attr, const char *buf, size_t size)
{
	CDBG("ssrm_camera_info buf : %s\n", buf);
	scnprintf(ssrm_camera_info, sizeof(ssrm_camera_info), "%s", buf);

	return size;
}


static DEVICE_ATTR(rear_camtype, S_IRUGO, back_camera_type_show, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO|S_IWUSR|S_IWGRP,
    back_camera_firmware_show, back_camera_firmware_store);
static DEVICE_ATTR(rear_checkfw_user, S_IRUGO|S_IWUSR|S_IWGRP,
    back_camera_firmware_user_show, back_camera_firmware_user_store);
static DEVICE_ATTR(rear_checkfw_factory, S_IRUGO|S_IWUSR|S_IWGRP,
    back_camera_firmware_factory_show, back_camera_firmware_factory_store);
static DEVICE_ATTR(rear_camfw_full, S_IRUGO|S_IWUSR|S_IWGRP,
    back_camera_firmware_full_show, back_camera_firmware_full_store);
static DEVICE_ATTR(rear_camfw_load, S_IRUGO|S_IWUSR|S_IWGRP,
    back_camera_firmware_load_show, back_camera_firmware_load_store);
#if defined(CONFIG_COMPANION2) || defined(CONFIG_COMPANION3)
static DEVICE_ATTR(rear_companionfw_full, S_IRUGO|S_IWUSR|S_IWGRP,
    back_companion_firmware_show, back_companion_firmware_store);
#endif
static DEVICE_ATTR(rear_fwcheck, S_IRUGO|S_IWUSR|S_IWGRP,
    back_fw_crc_check_show, back_fw_crc_check_store);
static DEVICE_ATTR(rear_calcheck, S_IRUGO|S_IWUSR|S_IWGRP,
    back_cal_data_check_show, back_cal_data_check_store);
static DEVICE_ATTR(rear_moduleinfo, S_IRUGO|S_IWUSR|S_IWGRP,
    back_module_info_show, back_module_info_store);
static DEVICE_ATTR(isp_core, S_IRUGO|S_IWUSR|S_IWGRP,
    back_isp_core_check_show, back_isp_core_check_store);
static DEVICE_ATTR(rear_afcal, S_IRUGO, back_camera_afcal_show, NULL);
static DEVICE_ATTR(front_afcal, S_IRUGO, front_camera_afcal_show, NULL);
static DEVICE_ATTR(front_camtype, S_IRUGO, front_camera_type_show, NULL);
static DEVICE_ATTR(front_camfw, S_IRUGO|S_IWUSR|S_IWGRP,
		front_camera_firmware_show, front_camera_firmware_store);
static DEVICE_ATTR(front_camfw_full, S_IRUGO | S_IWUSR | S_IWGRP,
		front_camera_firmware_full_show, front_camera_firmware_full_store);
static DEVICE_ATTR(front_checkfw_user, S_IRUGO|S_IWUSR|S_IWGRP,
    front_camera_firmware_user_show, front_camera_firmware_user_store);
static DEVICE_ATTR(front_checkfw_factory, S_IRUGO|S_IWUSR|S_IWGRP,
    front_camera_firmware_factory_show, front_camera_firmware_factory_store);
#if defined (CONFIG_CAMERA_SYSFS_V2)
static DEVICE_ATTR(rear_caminfo, S_IRUGO|S_IWUSR|S_IWGRP,
		rear_camera_info_show, rear_camera_info_store);
static DEVICE_ATTR(front_caminfo, S_IRUGO|S_IWUSR|S_IWGRP,
		front_camera_info_show, front_camera_info_store);
#endif
static DEVICE_ATTR(rear_sensorid_exif, S_IRUGO|S_IWUSR|S_IWGRP,
		rear_sensorid_exif_show, rear_sensorid_exif_store);
static DEVICE_ATTR(front_sensorid_exif, S_IRUGO|S_IWUSR|S_IWGRP,
		front_sensorid_exif_show, front_sensorid_exif_store);
#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
static DEVICE_ATTR(rear_moduleid, S_IRUGO, back_camera_moduleid_show, NULL);
static DEVICE_ATTR(SVC_rear_module, S_IRUGO, back_camera_moduleid_show, NULL);
#endif
static DEVICE_ATTR(ssrm_camera_info, S_IRUGO|S_IWUSR|S_IWGRP,
		ssrm_camera_info_show, ssrm_camera_info_store);

#if defined(CONFIG_SEC_GRACEQLTE_PROJECT)
static DEVICE_ATTR(iris_camfw, S_IRUGO|S_IWUSR|S_IWGRP,
		iris_camera_firmware_show, iris_camera_firmware_store);
static DEVICE_ATTR(iris_checkfw_user, S_IRUGO|S_IWUSR|S_IWGRP,
		iris_camera_firmware_user_show, iris_camera_firmware_user_store);
static DEVICE_ATTR(iris_checkfw_factory, S_IRUGO|S_IWUSR|S_IWGRP,
		iris_camera_firmware_factory_show, iris_camera_firmware_factory_store);
static DEVICE_ATTR(iris_camfw_full, S_IRUGO|S_IWUSR|S_IWGRP,
		iris_camera_firmware_full_show, iris_camera_firmware_full_store);
#if defined(CONFIG_CAMERA_SYSFS_V2)
static DEVICE_ATTR(iris_caminfo, S_IRUGO|S_IWUSR|S_IWGRP,
		iris_camera_info_show, iris_camera_info_store);
#endif
#endif

#if defined(CONFIG_COMPANION3)
static DEVICE_ATTR(companion_ic_check, S_IRUGO|S_IWUSR|S_IWGRP,
		back_camera_comp_ic_check_show, back_camera_comp_ic_check_store);
#endif

#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
int svc_cheating_prevent_device_file_create(struct kobject **obj)
{
	struct kernfs_node *SVC_sd;
	struct kobject *data;
	struct kobject *Camera;
	
	/* To find SVC kobject */
	SVC_sd = sysfs_get_dirent(devices_kset->kobj.sd, "svc");
	if (IS_ERR_OR_NULL(SVC_sd)) {
		/* try to create SVC kobject */
		data = kobject_create_and_add("svc", &devices_kset->kobj);
		if (IS_ERR_OR_NULL(data))
		        pr_info("Failed to create sys/devices/svc already exist SVC : 0x%p\n", data);
		else
			pr_info("Success to create sys/devices/svc SVC : 0x%p\n", data);
	} else {
		data = (struct kobject *)SVC_sd->priv;
		pr_info("Success to find SVC_sd : 0x%p SVC : 0x%p\n", SVC_sd, data);
	}

	Camera = kobject_create_and_add("Camera", data);
	if (IS_ERR_OR_NULL(Camera))
	        pr_info("Failed to create sys/devices/svc/Camera : 0x%p\n", Camera);
	else
		pr_info("Success to create sys/devices/svc/Camera : 0x%p\n", Camera);


	*obj = Camera;
	return 0;
}
#endif

static int __init msm_sensor_init_module(void)
{
	struct device         *cam_dev_back;
	struct device         *cam_dev_front;
#if defined(CONFIG_SEC_GRACEQLTE_PROJECT)
	struct device         *cam_dev_iris;
#endif
	int ret = 0;
#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
	struct kobject *SVC = 0;
	svc_cheating_prevent_device_file_create(&SVC);
#endif
	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class))
	    pr_err("failed to create device cam_dev_rear!\n");

	/* Allocate memory for msm_sensor_init control structure */
	s_init = kzalloc(sizeof(struct msm_sensor_init_t), GFP_KERNEL);
	if (!s_init) {
		class_destroy(camera_class);
		pr_err("failed: no memory s_init %pK", NULL);
		return -ENOMEM;
	}

	CDBG("MSM_SENSOR_INIT_MODULE %pK", NULL);

	/* Initialize mutex */
	mutex_init(&s_init->imutex);

	/* Create /dev/v4l-subdevX for msm_sensor_init */
	v4l2_subdev_init(&s_init->msm_sd.sd, &msm_sensor_init_subdev_ops);
	snprintf(s_init->msm_sd.sd.name, sizeof(s_init->msm_sd.sd.name), "%s",
		"msm_sensor_init");
	v4l2_set_subdevdata(&s_init->msm_sd.sd, s_init);
	s_init->msm_sd.sd.internal_ops = &msm_sensor_init_internal_ops;
	s_init->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = media_entity_init(&s_init->msm_sd.sd.entity, 0, NULL, 0);
	if (ret < 0) {
		pr_err("Failed to media entity init!\n");
		goto error;
	}
	s_init->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	s_init->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_SENSOR_INIT;
	s_init->msm_sd.sd.entity.name = s_init->msm_sd.sd.name;
	s_init->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x6;
	ret = msm_sd_register(&s_init->msm_sd);
	if (ret) {
		CDBG("%s: msm_sd_register error = %d\n", __func__, ret);
		goto msm_sd_register_fail;
	}
	msm_cam_copy_v4l2_subdev_fops(&msm_sensor_init_v4l2_subdev_fops);
#ifdef CONFIG_COMPAT
	msm_sensor_init_v4l2_subdev_fops.compat_ioctl32 =
		msm_sensor_init_subdev_fops_ioctl;
#endif
	s_init->msm_sd.sd.devnode->fops =
		&msm_sensor_init_v4l2_subdev_fops;

	init_waitqueue_head(&s_init->state_wait);

	cam_dev_back = device_create(camera_class, NULL,
		1, NULL, "rear");
	if (IS_ERR(cam_dev_back)) {
		printk("Failed to create cam_dev_back device!\n");
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_camtype) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_camtype.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_camfw) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_camfw.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_checkfw_user) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_checkfw_user.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_checkfw_factory) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_checkfw_factory.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_camfw_full) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_camfw_full.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_camfw_load) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_camfw_load.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#if defined(CONFIG_COMPANION2) || defined(CONFIG_COMPANION3)
	if (device_create_file(cam_dev_back, &dev_attr_rear_companionfw_full) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_companionfw_full.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#endif
	if (device_create_file(cam_dev_back, &dev_attr_rear_fwcheck) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_fwcheck.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_calcheck) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_calcheck.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_rear_moduleinfo) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_moduleinfo.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_back, &dev_attr_isp_core) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_isp_core.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}

#if defined (CONFIG_CAMERA_SYSFS_V2)
	if (device_create_file(cam_dev_back, &dev_attr_rear_caminfo) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_caminfo.attr.name);
		goto device_create_fail;
	}
#endif
	if (device_create_file(cam_dev_back, &dev_attr_rear_afcal) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_afcal.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}

	if (device_create_file(cam_dev_back, &dev_attr_rear_sensorid_exif) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_sensorid_exif.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#if defined(CONFIG_REAR_MODULE_ID_SUPPORT)
	if (device_create_file(cam_dev_back, &dev_attr_rear_moduleid) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_rear_moduleid.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (sysfs_create_file(SVC, &dev_attr_SVC_rear_module.attr) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_SVC_rear_module.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#endif
	if (device_create_file(cam_dev_back, &dev_attr_ssrm_camera_info) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_ssrm_camera_info.attr.name);
	}
	cam_dev_front = device_create(camera_class, NULL,
		2, NULL, "front");
	if (IS_ERR(cam_dev_front)) {
		printk("Failed to create cam_dev_front device!");
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_front, &dev_attr_front_camtype) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_camtype.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_front, &dev_attr_front_camfw) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_camfw.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_front, &dev_attr_front_camfw_full) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_camfw_full.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_front, &dev_attr_front_checkfw_user) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_checkfw_user.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_front, &dev_attr_front_checkfw_factory) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_checkfw_factory.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}

#if defined (CONFIG_CAMERA_SYSFS_V2)
	if (device_create_file(cam_dev_front, &dev_attr_front_caminfo) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_caminfo.attr.name);
		goto device_create_fail;
	}
#endif
	if (device_create_file(cam_dev_front, &dev_attr_front_afcal) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_afcal.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}

	if (device_create_file(cam_dev_front, &dev_attr_front_sensorid_exif) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_front_sensorid_exif.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#if defined(CONFIG_SEC_GRACEQLTE_PROJECT)
	cam_dev_iris = device_create(camera_class, NULL,
		2, NULL, "secure");
	if (IS_ERR(cam_dev_iris)) {
		printk("Failed to create cam_dev_iris device!");
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_iris, &dev_attr_iris_camfw) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_iris_camfw.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_iris, &dev_attr_iris_camfw_full) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_iris_camfw_full.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_iris, &dev_attr_iris_checkfw_user) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_iris_checkfw_user.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
	if (device_create_file(cam_dev_iris, &dev_attr_iris_checkfw_factory) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_iris_checkfw_factory.attr.name);
		ret = -ENODEV;
		goto device_create_fail;
	}
#if defined (CONFIG_CAMERA_SYSFS_V2)
	if (device_create_file(cam_dev_iris, &dev_attr_iris_caminfo) < 0) {
		printk("Failed to create device file!(%s)!\n",
			dev_attr_iris_caminfo.attr.name);
		goto device_create_fail;
	}
#endif
#endif
#if defined(CONFIG_COMPANION3)
        if (device_create_file(cam_dev_back, &dev_attr_companion_ic_check) < 0) {
            printk("Failed to create device file!(%s)!\n",
                dev_attr_companion_ic_check.attr.name);
            goto device_create_fail;
        }
#endif
	pr_warn("MSM_SENSOR_INIT_MODULE : X");
	return 0;

device_create_fail:
	msm_sd_unregister(&s_init->msm_sd);
msm_sd_register_fail:
	media_entity_cleanup(&s_init->msm_sd.sd.entity);
error:
	mutex_destroy(&s_init->imutex);
	kfree(s_init);
	class_destroy(camera_class);
	return ret;
}

static void __exit msm_sensor_exit_module(void)
{
	msm_sd_unregister(&s_init->msm_sd);
	mutex_destroy(&s_init->imutex);
	kfree(s_init);
	return;
}

module_init(msm_sensor_init_module);
module_exit(msm_sensor_exit_module);
MODULE_DESCRIPTION("msm_sensor_init");
MODULE_LICENSE("GPL v2");
