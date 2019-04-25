/*
 * driver/ccic/ccic_alternate.c - S2MM005 USB CCIC Alternate mode driver
 *
 * Copyright (C) 2016 Samsung Electronics
 * Author: Wookwang Lee <wookwang.lee@samsung.com>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/ccic/s2mm005.h>
#include <linux/ccic/s2mm005_ext.h>
#include <linux/ccic/ccic_alternate.h>
/* switch device header */
#if defined(CONFIG_SWITCH)
#include <linux/switch.h>
#endif /* CONFIG_SWITCH */
////////////////////////////////////////////////////////////////////////////////
// s2mm005_cc.c called s2mm005_alternate.c
////////////////////////////////////////////////////////////////////////////////
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
#if defined(CONFIG_SWITCH)
static struct switch_dev switch_dock = {
	.name = "ccic_dock",
};
#endif /* CONFIG_SWITCH */

static char VDM_MSG_IRQ_State_Print[9][40] =
{
    {"bFLAG_Vdm_Reserve_b0"},
    {"bFLAG_Vdm_Discover_ID"},
    {"bFLAG_Vdm_Discover_SVIDs"},
    {"bFLAG_Vdm_Discover_MODEs"},
    {"bFLAG_Vdm_Enter_Mode"},
    {"bFLAG_Vdm_Exit_Mode"},
    {"bFLAG_Vdm_Attention"},
    {"bFlag_Vdm_DP_Status_Update"},
    {"bFlag_Vdm_DP_Configure"},
};

static char DP_UFP_Pin_Assignment_Print[6][40] =
{
    {"DP_UFP_Pin_Assignment_A"},
    {"DP_UFP_Pin_Assignment_B"},
    {"DP_UFP_Pin_Assignment_C"},
    {"DP_UFP_Pin_Assignment_D"},
    {"DP_UFP_Pin_Assignment_E"},
    {"DP_UFP_Pin_Assignment_F"},
};

static char DP_DFP_Pin_Assignment_Print[6][40] =
{
    {"DP_DFP_Pin_Assignment_A"},
    {"DP_DFP_Pin_Assignment_B"},
    {"DP_DFP_Pin_Assignment_C"},
    {"DP_DFP_Pin_Assignment_D"},
    {"DP_DFP_Pin_Assignment_E"},
    {"DP_DFP_Pin_Assignment_F"},
};
////////////////////////////////////////////////////////////////////////////////
// Alternate mode processing
////////////////////////////////////////////////////////////////////////////////
int ccic_register_switch_device(int mode)
{
#ifdef CONFIG_SWITCH
	int ret = 0;
	if (mode) {
		ret = switch_dev_register(&switch_dock);
		if (ret < 0) {
			pr_err("%s: Failed to register dock switch(%d)\n",
				__func__, ret);
			return -ENODEV;
		}
	} else {
		switch_dev_unregister(&switch_dock);
	}
#endif /* CONFIG_SWITCH */
	return 0;
}

static void ccic_send_dock_intent(int type)
{
	pr_info("%s: CCIC dock type(%d)\n", __func__, type);
#ifdef CONFIG_SWITCH
	switch_set_state(&switch_dock, type);
#endif /* CONFIG_SWITCH */
}

void acc_detach_check(struct work_struct *wk)
{
	struct delayed_work *delay_work =
		container_of(wk, struct delayed_work, work);
	struct s2mm005_data *usbpd_data =
		container_of(delay_work, struct s2mm005_data, acc_detach_work);
	CC_NOTI_TYPEDEF ccic_alt_noti;

	pr_info("%s: usbpd_data->pd_state : %d\n", __func__, usbpd_data->pd_state);
	if (usbpd_data->pd_state == State_PE_Initial_detach) {
		if (usbpd_data->acc_type == CCIC_DOCK_DP) {
			ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
			ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
			ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_CONNECT;
			ccic_alt_noti.sub1 = CCIC_NOTIFY_DETACH;
			ccic_alt_noti.sub2 = 0;
			ccic_alt_noti.sub3 = 0;
			ccic_notifier_notify((CC_NOTI_TYPEDEF*)&ccic_alt_noti, NULL, 0);
			usbpd_data->acc_type = CCIC_DOCK_DETACHED;
		}
		if (usbpd_data->acc_type == CCIC_DOCK_HMT) {
			ccic_send_dock_intent(CCIC_DOCK_DETACHED);
			usbpd_data->acc_type = CCIC_DOCK_DETACHED;
		}
	}
}

static int process_check_accessory(void * data)
{
	struct s2mm005_data *usbpd_data = data;

	/* detect Gear VR */
	if (usbpd_data->Vendor_ID == SAMSUNG_VENDOR_ID &&
		usbpd_data->Product_ID >= GEARVR_PRODUCT_ID &&
		usbpd_data->Product_ID <= GEARVR_PRODUCT_ID + 5) {
		pr_info("%s : Samsung Gear VR connected.\n", __func__);
		if (usbpd_data->acc_type == CCIC_DOCK_DETACHED) {
			ccic_send_dock_intent(CCIC_DOCK_HMT);
			usbpd_data->acc_type = CCIC_DOCK_HMT;
		}
		return 1;
	}
	return 0;
}

static void process_discover_identity(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_ID;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;

	/* Message Type Definition */
	U_DATA_MSG_ID_HEADER_Type *DATA_MSG_ID =
		(U_DATA_MSG_ID_HEADER_Type *)&ReadMSG[8];
	U_PRODUCT_VDO_Type *DATA_MSG_PRODUCT =
		(U_PRODUCT_VDO_Type *)&ReadMSG[16];

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}

	usbpd_data->Vendor_ID = DATA_MSG_ID->BITS.USB_Vendor_ID;
	usbpd_data->Product_ID = DATA_MSG_PRODUCT->BITS.Product_ID;

	dev_info(&i2c->dev, "%s Vendor_ID : 0x%X, Product_ID : 0x%X\n",
		__func__, usbpd_data->Vendor_ID, usbpd_data->Product_ID);
	if (process_check_accessory(usbpd_data))
		dev_info(&i2c->dev, "%s : Samsung Accessory Connected.\n", __func__);
}

static void process_discover_svids(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_SVID;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;
	CC_NOTI_TYPEDEF ccic_alt_noti;

	/* Message Type Definition */
	U_VDO1_Type  *DATA_MSG_VDO1 = (U_VDO1_Type *)&ReadMSG[8];

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}

	usbpd_data->SVID_0 = DATA_MSG_VDO1->BITS.SVID_0;
	usbpd_data->SVID_1 = DATA_MSG_VDO1->BITS.SVID_1;

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {
		usbpd_data->acc_type = CCIC_DOCK_DP;
		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_CONNECT;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_ATTACH;
		ccic_alt_noti.sub2 = usbpd_data->Vendor_ID;
		ccic_alt_noti.sub3 = usbpd_data->Product_ID;
		ccic_notifier_notify((CC_NOTI_TYPEDEF*)&ccic_alt_noti, NULL, 0);
	}

	dev_info(&i2c->dev, "%s SVID_0 : 0x%X, SVID_1 : 0x%X\n",
		 __func__, usbpd_data->SVID_0, usbpd_data->SVID_1);
}

static void process_discover_modes(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_MODE;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}
}

static void process_enter_mode(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD;
	uint8_t ReadMSG[32] = {0,};
	uint8_t W_DATA[3] = {0,};
	int ret = 0;
	CC_NOTI_TYPEDEF ccic_alt_noti;
	U_DATA_MSG_VDM_HEADER_Type *DATA_MSG_VDM =
		(U_DATA_MSG_VDM_HEADER_Type*)&ReadMSG[4];
	DIS_MODE_DP_CAPA_Type *pDP_DIS_MODE =
		(DIS_MODE_DP_CAPA_Type *)&ReadMSG[0];

	dev_info(&i2c->dev, "%s\n", __func__);

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {

		REG_ADD = REG_RX_MODE;
		ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
		if (ret < 0) {
			dev_err(&i2c->dev, "%s has i2c read error.\n", __func__);
			return;
		}

		REG_ADD = 0x10;
		W_DATA[0] = MODE_INTERFACE;	/* Mode Interface */
		W_DATA[1] = DP_ALT_MODE_REQ;   /* DP Alternate Mode Request */
		W_DATA[2] = 0;	/* DP Pin Assign select */

		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_LINK_CONF;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_UNKNOWN;
		ccic_alt_noti.sub2 = 0;
		ccic_alt_noti.sub3 = 0;

		/*  pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS. */
		dev_info(&i2c->dev, "pDP_DIS_MODE->MSG_HEADER.DATA = 0x%08X\n\r",
			 pDP_DIS_MODE->MSG_HEADER.DATA);
		dev_info(&i2c->dev, "pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA = 0x%08X\n\r",
			 pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA);
		dev_info(&i2c->dev, "pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA = 0x%08X\n\r",
			 pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA);

		if(pDP_DIS_MODE->MSG_HEADER.BITS.Number_of_obj > 1) {
			if(((pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_UFP_D_Capable)
			    && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_Receptacle))
			   ||((pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_DFP_D_Capable)
			      && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_PLUG) ) )
			{
				if( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.UFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_D)
				    == DP_PIN_ASSIGNMENT_D )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_D;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
				}
				else if((pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.UFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_C) == DP_PIN_ASSIGNMENT_C )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_C;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_C;
				}
				else if( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.UFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_E)
					 == DP_PIN_ASSIGNMENT_E )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_E;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_E;
				}
				dev_info(&i2c->dev, "%s %s \n", __func__, DP_UFP_Pin_Assignment_Print[W_DATA[2]]);

			} else if( ( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_DFP_D_Capable)
				     && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_Receptacle) )
				   ||  ( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_UFP_D_Capable)
					 && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_PLUG) ) )
			{
				if( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.DFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_D)
				    == DP_PIN_ASSIGNMENT_D )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_D;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
				}
				else if( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.DFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_C)
					 == DP_PIN_ASSIGNMENT_C )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_C;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_C;
				}
				else if( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.DFP_D_Pin_Assignments & DP_PIN_ASSIGNMENT_E)
					 == DP_PIN_ASSIGNMENT_E )
				{
					W_DATA[2] = DP_PIN_ASSIGNMENT_E;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_E;
				}
				dev_info(&i2c->dev, "%s %s \n", __func__, DP_DFP_Pin_Assignment_Print[W_DATA[2]]);
			}
		}
		ret =s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3 );
		if (ret < 0) {
			dev_err(&i2c->dev, "%s has i2c write error.\n", __func__);
			return;
		}
		ccic_notifier_notify((CC_NOTI_TYPEDEF*)&ccic_alt_noti, NULL, 0);

	} else {
		REG_ADD = REG_RX_ENTER_MODE;
		/* Message Type Definition */
		DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type *)&ReadMSG[4];
		ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
		if (ret < 0) {
			dev_err(&i2c->dev, "%s has i2c read error.\n", __func__);
		dev_info(&i2c->dev, "pDP_DIS_MODE->MSG_HEADER.DATA = 0x%08X\n\r",
			 pDP_DIS_MODE->MSG_HEADER.DATA);
			return;
		}

		if (DATA_MSG_VDM->BITS.VDM_command_type == 1)
			dev_info(&i2c->dev, "%s : EnterMode ACK.\n", __func__);
		else
			dev_info(&i2c->dev, "%s : EnterMode NAK.\n", __func__);
	}
}

static void process_attention(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_ATTENTION;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;
	int i;
	CC_NOTI_TYPEDEF ccic_alt_noti;
	VDO_MESSAGE_Type *VDO_MSG;
	DP_STATUS_UPDATE_Type *DP_STATUS;

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {
		DP_STATUS = (DP_STATUS_UPDATE_Type *)&ReadMSG[0];

		dev_info(&i2c->dev, "%s DP_STATUS_UPDATE = 0x%08X\n", __func__,
			 DP_STATUS->DATA_DP_STATUS_UPDATE.DATA);

		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_HPD;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;	/*HPD_STATE*/
		ccic_alt_noti.sub2 = 0;			/*HPD IRQ*/
		ccic_alt_noti.sub3 = 0;

		if ( DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_State == 1) {
			ccic_alt_noti.sub1 = CCIC_NOTIFY_HIGH;
		} else if(DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_State == 0){
			ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;
		}
		if(DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_Interrupt == 1) {
			ccic_alt_noti.sub2 = CCIC_NOTIFY_IRQ;
		}
		ccic_notifier_notify((CC_NOTI_TYPEDEF*)&ccic_alt_noti, NULL, 0);
	} else {
		VDO_MSG = (VDO_MESSAGE_Type *)&ReadMSG[8];

		for(i=0;i<6;i++)
			dev_info(&i2c->dev, "%s : VDO_%d : %d\n", __func__,
				 i+1, VDO_MSG->VDO[i]);
	}

}

static void process_status_update(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_DP_STATUS_UPDATE;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;

	pr_info("%s \n",__func__);
	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}
}

static void process_dp_configure(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_MODE;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;

	dev_info(&i2c->dev, "%s\n", __func__);

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return;
	}

	return;
}


static void process_alternate_mode(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint32_t mode = usbpd_data->alternate_state;

	if (mode) {
		dev_info(&i2c->dev, "%s, mode : 0x%x\n", __func__, mode);

		if (mode & VDM_DISCOVER_ID)
			process_discover_identity(usbpd_data);
		if (mode & VDM_DISCOVER_SVIDS)
			process_discover_svids(usbpd_data);
		if (mode & VDM_DISCOVER_MODES)
			process_discover_modes(usbpd_data);
		if (mode & VDM_ENTER_MODE)
			process_enter_mode(usbpd_data);
		if (mode & VDM_ATTENTION)
			process_attention(usbpd_data);
		if (mode & VDM_DP_STATUS_UPDATE)
			process_status_update(usbpd_data);
		if (mode & VDM_DP_CONFIGURE)
			process_dp_configure(usbpd_data);
		usbpd_data->alternate_state = 0;
	}
}

void send_alternate_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_VDM_MSG_REQ;
	u8 mode = (u8)cmd;
	dev_info(&i2c->dev, "%s : %s\n",__func__,
		 &VDM_MSG_IRQ_State_Print[cmd][0]);
	s2mm005_write_byte(i2c, REG_ADD, &mode, 1);
}

void receive_alternate_message(void * data, VDM_MSG_IRQ_STATUS_Type *VDM_MSG_IRQ_State)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;

	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_ID) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, 
			 &VDM_MSG_IRQ_State_Print[1][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_ID;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_SVIDs) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[2][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_SVIDS;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_MODEs) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[3][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_MODES;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Enter_Mode) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[4][0]);
		usbpd_data->alternate_state |= VDM_ENTER_MODE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Exit_Mode) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[5][0]);
		usbpd_data->alternate_state |= VDM_EXIT_MODE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Attention) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[6][0]);
		usbpd_data->alternate_state |= VDM_ATTENTION;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_DP_Status_Update) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[7][0]);
		usbpd_data->alternate_state |= VDM_DP_STATUS_UPDATE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_DP_Configure) {
		dev_info(&i2c->dev, "%s : %s\n",__func__,
			 &VDM_MSG_IRQ_State_Print[8][0]);
		usbpd_data->alternate_state |= VDM_DP_CONFIGURE;
	}

	process_alternate_mode(usbpd_data);
}

void send_unstructured_vdm_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_SEND;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[2];
	uint32_t message = (uint32_t)cmd;
	int i;

	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_UNSTRUCTURED_VDM_HEADER_Type	*DATA_MSG_UVDM = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	// fill message
	MSG_HDR->Message_Type = 15; // send VDM message
	MSG_HDR->Number_of_obj = 7; // VDM Header + 6 VDOs = MAX 7
	DATA_MSG_UVDM->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID

	for(i=0;i<6;i++)
		VDO_MSG->VDO[i] = message; // VD01~VDO6 : Max 24bytes

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = SEL_SSM_MSG_REQ;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void receive_unstructured_vdm_message(void * data, SSM_MSG_IRQ_STATUS_Type *SSM_MSG_IRQ_State)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_READ;
	uint8_t ReadMSG[32] = {0,};
	u8 W_DATA[1];
	int i;

	uint16_t *VID = (uint16_t *)&ReadMSG[6];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&ReadMSG[8];

	s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	dev_info(&i2c->dev, "%s : VID : 0x%x\n", __func__, *VID);
	for(i=0;i<6;i++)
		dev_info(&i2c->dev, "%s : VDO_%d : %d\n", __func__, i+1, VDO_MSG->VDO[i]);

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INT_CLEAR;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 1);
}

void send_role_swap_message(void * data, int cmd) // cmd 0 : PR_SWAP, cmd 1 : DR_SWAP
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_I2C_SLV_CMD;
	u8 mode = (u8)cmd;
	u8 W_DATA[2];

	/* send uVDM message */
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = mode ? REQ_DR_SWAP : REQ_PR_SWAP;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s : sent %s message\n", __func__, mode ? "DR_SWAP" : "PR_SWAP");
}

void send_attention_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_TX_DIS_ATTENTION_RESPONSE;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[3];
	uint32_t message = (uint32_t)cmd;
	int i;

	/* Message Type Definition */
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_DATA_MSG_VDM_HEADER_Type	  *DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	/* fill message */
	DATA_MSG_VDM->BITS.VDM_command = 6;	/* attention*/
	DATA_MSG_VDM->BITS.VDM_Type = 1;	/* structured VDM */
	DATA_MSG_VDM->BITS.Standard_Vendor_ID = SAMSUNG_VENDOR_ID;

	MSG_HDR->Message_Type = 15;		/* send VDM message */
	MSG_HDR->Number_of_obj = 7;		/* VDM Header + 6 VDOs = MAX 7 */

	for(i=0;i<6;i++)
		VDO_MSG->VDO[i] = message;	/* VD01~VDO6 : Max 24bytes */

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = PD_NEXT_STATE;
	W_DATA[2] = 100;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void do_alternate_mode_step_by_step(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = 0;
	u8 W_DATA[3];

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = PD_NEXT_STATE;
	switch (cmd) {
		case VDM_DISCOVER_ID:
			W_DATA[2] = 80;
			break;
		case VDM_DISCOVER_SVIDS:
			W_DATA[2] = 83;
			break;
		case VDM_DISCOVER_MODES:
			W_DATA[2] = 86;
			break;
		case VDM_ENTER_MODE:
			W_DATA[2] = 89;
			break;
		case VDM_EXIT_MODE:
			W_DATA[2] = 92;
			break;
	}
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3);

	dev_info(&i2c->dev, "%s\n", __func__);
}
#endif
