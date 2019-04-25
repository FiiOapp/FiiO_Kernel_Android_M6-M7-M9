/*
 * u_uac2.h
 *
 * Utility definitions for UAC2 function
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_UAC2_H
#define U_UAC2_H

#include <linux/usb/composite.h>

#define UAC2_DEF_PCHMASK 0x3
#define UAC2_DEF_PSRATE 48000
#define UAC2_DEF_PSSIZE 2
#define UAC2_DEF_CCHMASK 0x3
#define UAC2_DEF_CSRATE 48000//44100
#define UAC2_DEF_CSSIZE 2//2

#define UAC2_RATE_32000     32000
#define UAC2_RATE_44100     44100
#define UAC2_RATE_48000     48000
#define UAC2_RATE_88200     88200
#define UAC2_RATE_96000     96000
#define UAC2_RATE_176400    176400
#define UAC2_RATE_192000    192000

#define UAC2_DEFAULT_RATE   48000//44100

#define UAC2_MIN_RATE       32000
#define UAC2_MAX_RATE       192000

#define UAC2_RATE_VALUE_32000     32//128
#define UAC2_RATE_VALUE_44100     44//176
#define UAC2_RATE_VALUE_48000     48//192
#define UAC2_RATE_VALUE_88200     88//352
#define UAC2_RATE_VALUE_96000     96//384
#define UAC2_RATE_VALUE_176400    176//704
#define UAC2_RATE_VALUE_192000    192//768

struct f_uac2_opts {
	struct usb_function_instance	func_inst;
	int				p_chmask;
	int				p_srate;
	int				p_ssize;
	int				c_chmask;
	int				c_srate;
	int				c_ssize;
	bool				bound;

	struct mutex			lock;
	int				refcnt;

        struct work_struct              uevent_work;
        bool                            uac2_status;
        bool                            uac2_binded;
};

#endif
