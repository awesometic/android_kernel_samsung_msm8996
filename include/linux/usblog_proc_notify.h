/*
 * Copyright (C) 2016 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

  /* usb notify layer v2.0 */

#ifndef __LINUX_USBLOG_PROC_NOTIFY_H__
#define __LINUX_USBLOG_PROC_NOTIFY_H__

enum usblog_type {
	NOTIFY_FUNCSTATE,
	NOTIFY_ALTERNATEMODE,
	NOTIFY_CCIC_EVENT,
	NOTIFY_MANAGER,
	NOTIFY_USBMODE,
	NOTIFY_USBSTATE,
	NOTIFY_EVENT,
	NOTIFY_DWC,
};

enum usblog_state {
	NOTIFY_CONFIGURED = 1,
	NOTIFY_CONNECTED,
	NOTIFY_DISCONNECTED,
	NOTIFY_RESET,
	NOTIFY_ACCSTART,
	NOTIFY_HIGH,
	NOTIFY_SUPER,
};

enum dwc_state {
	NOTIFY_IDLE = 0,
	NOTIFY_SUSPEND,
	NOTIFY_RESUME,
};

enum dwc_param_bit {
	NOTIFY_DWC_PREV_LPM = 1 << 0,
	NOTIFY_DWC_CUR_LPM = 1 << 1,
};

enum usblog_status {
	NOTIFY_DETACH = 0,
	NOTIFY_ATTACH_DFP,
	NOTIFY_ATTACH_UFP,
	NOTIFY_ATTACH_DRP,
};

enum ccic_device {
	NOTIFY_DEV_INITIAL = 0,
	NOTIFY_DEV_USB,
	NOTIFY_DEV_BATTERY,
	NOTIFY_DEV_PDIC,
	NOTIFY_DEV_MUIC,
	NOTIFY_DEV_CCIC,
	NOTIFY_DEV_MANAGER,
};

enum ccic_id {
	NOTIFY_ID_INITIAL = 0,
	NOTIFY_ID_ATTACH,
	NOTIFY_ID_RID,
	NOTIFY_ID_USB,
	NOTIFY_ID_POWER_STATUS,
	NOTIFY_ID_WATER,
	NOTIFY_ID_VCONN,
	NOTIFY_ID_ROLE_SWAP,
};

enum ccic_rid {
	NOTIFY_RID_UNDEFINED = 0,
	NOTIFY_RID_000K,
	NOTIFY_RID_001K,
	NOTIFY_RID_255K,
	NOTIFY_RID_301K,
	NOTIFY_RID_523K,
	NOTIFY_RID_619K,
	NOTIFY_RID_OPEN,
};

enum ccic_con {
	NOTIFY_CON_DETACH = 0,
	NOTIFY_CON_ATTACH,
};

enum ccic_rprd {
	NOTIFY_RD = 0,
	NOTIFY_RP,
};

#define ALTERNATE_MODE_NOT_READY	(1 << 0)
#define ALTERNATE_MODE_READY		(1 << 1)
#define ALTERNATE_MODE_STOP		(1 << 2)
#define ALTERNATE_MODE_START		(1 << 3)
#define ALTERNATE_MODE_RESET		(1 << 4)

#ifdef CONFIG_USB_NOTIFY_PROC_LOG
extern void store_usblog_notify(int type, void *param1, void *parma2);
extern void store_ccic_version(unsigned char *hw, unsigned char *sw_main,
			unsigned char *sw_boot);
extern int register_usblog_proc(void);
extern void unregister_usblog_proc(void);
#else
static inline void store_usblog_notify(int type, void *param1, void *parma2) {}
static inline void store_ccic_version(unsigned char *hw, unsigned char *sw_main,
			unsigned char *sw_boot) {}
static inline int register_usblog_proc(void)
			{return 0; }
static inline void unregister_usblog_proc(void) {}
#endif
#endif

