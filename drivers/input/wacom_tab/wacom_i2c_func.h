/*
 *  wacom_i2c_func.h - Wacom G5 Digitizer Controller (I2C bus)
 *
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_WACOM_I2C_FUNC_H_
#define _LINUX_WACOM_I2C_FUNC_H_

#define WACOM_I2C_STOP		0x30
#define WACOM_I2C_START		0x31
#define WACOM_I2C_GRID_CHECK	0xC9
#define WACOM_STATUS		0xD8

extern int wacom_i2c_send(struct wacom_i2c *wac_i2c,
			  const char *buf, int count, bool mode);
extern int wacom_i2c_recv(struct wacom_i2c *wac_i2c,
			char *buf, int count, bool mode);
extern int wacom_i2c_test(struct wacom_i2c *wac_i2c);
extern int wacom_i2c_coord(struct wacom_i2c *wac_i2c);
extern int wacom_i2c_query(struct wacom_i2c *wac_i2c);
extern int wacom_checksum(struct wacom_i2c *wac_i2c);
extern int wacom_i2c_set_sense_mode(struct wacom_i2c *wac_i2c);
#ifdef WACOM_USE_SURVEY_MODE
extern int wacom_i2c_enter_survey_mode(struct wacom_i2c *wac_i2c);
extern int wacom_i2c_exit_survey_mode(struct wacom_i2c *wac_i2c);
extern int wacom_get_aop_data(struct wacom_i2c *wac_i2c);
#endif
extern void forced_release(struct wacom_i2c *wac_i2c);
#ifdef WACOM_PDCT_WORK_AROUND
extern void forced_hover(struct wacom_i2c *wac_i2c);
#endif

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_DSX2
extern bool synaptics_epen_mode;
#endif

#endif	/* _LINUX_WACOM_I2C_FUNC_H */
