/* drivers/battery/s2mu004_charger.c
 * S2MU004 Charger Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include <linux/mfd/samsung/s2mu004.h>
#include <linux/power/s2mu004_charger.h>
#include <linux/version.h>
#include <linux/muic/muic.h>

#define ENABLE_MIVR 0

#define EN_OVP_IRQ 1
#define EN_IEOC_IRQ 1
#define EN_TOPOFF_IRQ 1
#define EN_RECHG_REQ_IRQ 0
#define EN_TR_IRQ 0
#define EN_MIVR_SW_REGULATION 0
#define EN_BST_IRQ 0
#define MINVAL(a, b) ((a <= b) ? a : b)

#define EOC_DEBOUNCE_CNT 2
#define HEALTH_DEBOUNCE_CNT 3
#define DEFAULT_CHARGING_CURRENT 500

#define S2MU004_CHGFULL_CNT 3
#define EOC_SLEEP 200
#define EOC_TIMEOUT (EOC_SLEEP * 6)
#ifndef EN_TEST_READ
#define EN_TEST_READ 1
#endif

#define ENABLE 1
#define DISABLE 0

struct s2mu004_charger_data {
	struct i2c_client       *client;
	struct device *dev;
	struct s2mu004_platform_data *s2mu004_pdata;
	struct delayed_work	charger_work;
	struct workqueue_struct *charger_wqueue;
	struct power_supply	*psy_chg;
	struct power_supply_desc psy_chg_desc;
	struct power_supply	*psy_battery;
	struct power_supply_desc psy_battery_desc;
	struct power_supply	*psy_otg;
	struct power_supply_desc psy_otg_desc;
	struct power_supply	*psy_usb;
	struct power_supply_desc psy_usb_desc;
	struct power_supply	*psy_ac;
	struct power_supply_desc psy_ac_desc;

	s2mu004_charger_platform_data_t *pdata;
	int dev_id;
	int charging_current;
	int siop_level;
	int cable_type;
	int battery_cable_type;
	bool is_charging;
	int charge_mode;
	struct mutex io_lock;
	bool noti_check;

	struct notifier_block batt_nb;
	int muic_cable_type;
	/* register programming */
	int reg_addr;
	int reg_data;

	bool full_charged;
	bool ovp;
	bool otg_on;

	int full_check_cnt;
	int unhealth_cnt;
	bool battery_valid;
	int status;
	int health;

	struct delayed_work afc_current_down;
	/* s2mu004 */
	int irq_det_bat;
	int irq_chg;
	struct delayed_work polling_work;
	int irq_event;
	int irq_vbus;
	int irq_rst;
	int irq_done;
	int irq_sys;

#if defined(CONFIG_FUELGAUGE_S2MU004)
	int voltage_now;
	int voltage_avg;
	int voltage_ocv;
	int current_now;
	int current_avg;
	unsigned int capacity;
	int temperature;
#endif

#if defined(CONFIG_MUIC_NOTIFIER)
	struct notifier_block cable_check;
#endif
};

static char *s2mu004_supplied_to[] = {
	"s2mu004-battery",
};


static enum power_supply_property s2mu004_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property sec_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_AUTHENTIC,
};


static enum power_supply_property s2mu004_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property s2mu004_otg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int s2mu004_get_charging_health(struct s2mu004_charger_data *charger);

static int charger_handle_notification(struct notifier_block *nb,
		unsigned long action, void *data);

static void s2mu004_test_read(struct i2c_client *i2c)
{
	u8 data;
	char str[1016] = {0,};
	int i;

	s2mu004_read_reg(i2c, 0x33, &data);
	pr_debug("%s: 0x33 = %d\n", __func__, data);
	s2mu004_read_reg(i2c, 0x9f, &data);
	pr_debug("%s: 0x9f = %d\n", __func__, data);
	s2mu004_read_reg(i2c, 0x2f, &data);
	pr_debug("%s: 0x2f = %d\n", __func__, data);
	s2mu004_read_reg(i2c, 0xa5, &data);
	pr_debug("%s: 0xa5 = %d\n", __func__, data);
	for (i = 0x0A; i <= 0x24; i++) {
		s2mu004_read_reg(i2c, i, &data);

		sprintf(str+strlen(str), "0x%02x:0x%02x, ", i, data);
	}

	pr_err("%s: %s\n", __func__, str);
}

static void s2mu004_charger_otg_control(struct s2mu004_charger_data *charger,
		bool enable)
{
	pr_info("%s : %d\n", __func__, enable);
	if (!enable) {
		/* set mode to BUCK mode */
		/* todo: mask VMID_INT */
		/* CHGIN normal setting */
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL0, CHG_MODE, REG_MODE_MASK);

		pr_info("%s : Turn off OTG\n",	__func__);
	} else {
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL0, OTG_BST_MODE, REG_MODE_MASK);

		pr_info("%s : Turn on OTG\n",	__func__);
	}
}

static void s2mu004_analog_ivr_switch(
		struct s2mu004_charger_data *charger, int enable)
{
	u8 reg_data = 0;
	int cable_type = POWER_SUPPLY_TYPE_BATTERY;
	union power_supply_propval value;

	if (charger->dev_id >= 0x3) {
		/* control IVRl only under PMIC REV < 0x3 */
		return;
	}

	psy_do_property("s2mu004-battery", get,
			POWER_SUPPLY_PROP_ONLINE, value);
	cable_type = value.intval;

	if (charger->charge_mode == SEC_BAT_CHG_MODE_CHARGING_OFF ||
			charger->charge_mode == SEC_BAT_CHG_MODE_BUCK_OFF ||
			(cable_type == POWER_SUPPLY_TYPE_PDIC) ||
			(cable_type == POWER_SUPPLY_TYPE_HV_PREPARE_MAINS)) {
		pr_info("[DEBUG]%s(%d): digital IVR\n", __func__, __LINE__);
		enable = 0;
	}

	s2mu004_read_reg(charger->client, 0xB3, &reg_data);
	pr_info("%s : 0xB3 : 0x%x\n", __func__, reg_data);

	if (enable) {
		if (!(reg_data & 0x08)) {
			/* Enable Analog IVR */
			pr_info("[DEBUG]%s: Enable Analog IVR\n", __func__);
			s2mu004_update_reg(charger->client, 0xB3, 0x1 << 3, 0x1 << 3);
		}
	} else {
		if (reg_data & 0x08) {
			/* Disable Analog IVR - Digital IVR enable*/
			pr_info("[DEBUG]%s: Disable Analog IVR - Digital IVR enable\n",
					__func__);
			s2mu004_update_reg(charger->client, 0xB3, 0x0, 0x1 << 3);
		}
	}
}

static void s2mu004_enable_charger_switch(struct s2mu004_charger_data *charger,
		int onoff)
{
	if (onoff > 0) {
		pr_err("[DEBUG]%s: turn on charger\n", __func__);

		/* forced ASYNC */
		s2mu004_update_reg(charger->client, 0x30, 0x03, 0x03);
		mdelay(30);

		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL0, CHG_MODE, REG_MODE_MASK);

		/* auto SYNC to ASYNC - default */
		s2mu004_update_reg(charger->client, 0x30, 0x01, 0x03);
		/* async off */
		s2mu004_update_reg(charger->client, 0x96, 0x00, 0x01 << 3);
	} else {
		pr_err("[DEBUG] %s: turn off charger\n", __func__);

		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL0, BUCK_MODE, REG_MODE_MASK);

		/* async on */
		s2mu004_update_reg(charger->client, 0x96, 0x01 << 3, 0x01 << 3);
	}

	s2mu004_test_read(charger->client);
}

static void s2mu004_set_buck(
		struct s2mu004_charger_data *charger, int enable) {

	if (enable) {
		pr_info("[DEBUG]%s: set buck on\n", __func__);
		s2mu004_enable_charger_switch(charger, charger->is_charging);
	} else {
		pr_info("[DEBUG]%s: set buck off (charger off mode)\n", __func__);

		if (charger->dev_id < 0x3) {
			/* Disable Analog IVR - Digital IVR enable*/
			s2mu004_analog_ivr_switch(charger, DISABLE);
		}

		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL0, CHARGER_OFF_MODE, REG_MODE_MASK);

		/* async on */
		s2mu004_update_reg(charger->client, 0x96, 0x01 << 3, 0x01 << 3);
		mdelay(100);
	}
}

static int s2mu004_usb_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =  power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the USB charger is connected */
	switch (charger->battery_cable_type) {
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_DCP:
	case POWER_SUPPLY_TYPE_USB_CDP:
	case POWER_SUPPLY_TYPE_USB_ACA:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}

static int s2mu004_ac_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =  power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the AC charger is connected */
	switch (charger->battery_cable_type) {
	case POWER_SUPPLY_TYPE_MAINS:
	case POWER_SUPPLY_TYPE_UARTOFF:
	case POWER_SUPPLY_TYPE_LAN_HUB:
	case POWER_SUPPLY_TYPE_UNKNOWN:
	case POWER_SUPPLY_TYPE_HV_PREPARE_MAINS:
	case POWER_SUPPLY_TYPE_HV_ERR:
	case POWER_SUPPLY_TYPE_HV_UNKNOWN:
	case POWER_SUPPLY_TYPE_HV_MAINS:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}



static int s2mu004_otg_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =	power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->otg_on;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int s2mu004_otg_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =  power_supply_get_drvdata(psy);
	union power_supply_propval value;
	bool enable = false;

	pr_info("%s %d\n", __func__, __LINE__);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		value.intval = val->intval;
		pr_info("%s: OTG %s\n", __func__, value.intval > 0 ? "ON" : "OFF");
		enable = value.intval > 0 ? true : false;
		s2mu004_charger_otg_control(charger, enable);
		pr_info("%s: OTG %s\n", __func__, value.intval > 0 ? "ON" : "OFF");
#if 0
		psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL, value);
#endif
		power_supply_changed(charger->psy_otg);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void s2mu004_set_regulation_voltage(struct s2mu004_charger_data *charger,
		int float_voltage)
{
	int data;

	pr_err("[DEBUG]%s: float_voltage %d\n", __func__, float_voltage);
	if (float_voltage <= 3900)
		data = 0;
	else if (float_voltage > 3900 && float_voltage <= 4530)
		data = (float_voltage - 3900) / 10;
	else
		data = 0x3f;

	s2mu004_update_reg(charger->client,
		S2MU004_CHG_CTRL6, data << SET_VF_VBAT_SHIFT, SET_VF_VBAT_MASK);
}

static void s2mu004_set_input_current_limit(
		struct s2mu004_charger_data *charger, int charging_current)
{
	u8 data;

	if (charging_current <= 100)
		data = 0x02;
	else if (charging_current > 100 && charging_current <= 2500)
		data = (charging_current - 50) / 25;
	else
		data = 0x62;

	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL2,
			data << INPUT_CURRENT_LIMIT_SHIFT, INPUT_CURRENT_LIMIT_MASK);

	pr_info("[DEBUG]%s: current  %d, 0x%x\n", __func__, charging_current, data);

#if EN_TEST_READ
	s2mu004_test_read(charger->client);
#endif
}

static int s2mu004_get_input_current_limit(struct i2c_client *i2c)
{
	u8 data;

	s2mu004_read_reg(i2c, S2MU004_CHG_CTRL2, &data);
	if (data < 0)
		return data;

	data = data & INPUT_CURRENT_LIMIT_MASK;

	if (data > 0x7F)
		data = 0x7F;
	return  data * 25 + 50;

}

static void s2mu004_set_fast_charging_current(struct i2c_client *i2c,
		int charging_current)
{
	int data;

	pr_err("[DEBUG]%s: current %d\n", __func__, charging_current);
	if (charging_current <= 100)
		data = 0;

	else if (charging_current >= 100 && charging_current <= 3200)
		data = ((charging_current - 100) / 25) + 3;
	else
		data = 0x7F;

	s2mu004_update_reg(i2c, S2MU004_CHG_CTRL9, data << FAST_CHARGING_CURRENT_SHIFT,
			FAST_CHARGING_CURRENT_MASK);
	s2mu004_test_read(i2c);
}

static int s2mu004_get_fast_charging_current(struct i2c_client *i2c)
{
	u8 data;

	s2mu004_read_reg(i2c, S2MU004_CHG_CTRL9, &data);
	if (data < 0)
		return data;

	data = data & FAST_CHARGING_CURRENT_MASK;

	if (data > 0x7D)
		data = 0x7D;
	return (data - 3) * 25 + 100;
}

static int s2mu004_get_current_eoc_setting(struct s2mu004_charger_data *charger)
{
	u8 data;

	s2mu004_read_reg(charger->client, S2MU004_CHG_CTRL11, &data);
	if (data < 0)
		return data;

	data = data & FIRST_TOPOFF_CURRENT_MASK;

	if (data > 0x0F)
		data = 0x0F;
	return data * 25 + 100;
}

static void s2mu004_set_topoff_current(struct i2c_client *i2c,
		int eoc_1st_2nd, int current_limit)
{
	int data;

	pr_err("[DEBUG]%s: current %d\n", __func__, current_limit);
	if (current_limit <= 100)
		data = 0;
	else if (current_limit > 100 && current_limit <= 475)
		data = (current_limit - 100) / 25;
	else
		data = 0x0F;

	switch (eoc_1st_2nd) {
	case 1:
		s2mu004_update_reg(i2c, S2MU004_CHG_CTRL11, data << FIRST_TOPOFF_CURRENT_SHIFT,
			FIRST_TOPOFF_CURRENT_MASK);
		break;
	case 2:
		s2mu004_update_reg(i2c, S2MU004_CHG_CTRL11, data << SECOND_TOPOFF_CURRENT_SHIFT,
			SECOND_TOPOFF_CURRENT_MASK);
		break;
	default:
		break;
	}
}


#define SUB_CHARGER_CURRENT 1300

static void s2mu004_set_charging_current(struct s2mu004_charger_data *charger)
{
	int adj_current = 0;
	pr_err("[DEBUG]%s: charger->siop_level %d\n", __func__, charger->siop_level);
	adj_current = charger->charging_current * charger->siop_level / 100;

	s2mu004_set_fast_charging_current(charger->client, adj_current);
	s2mu004_test_read(charger->client);
}

enum {
	S2MU004_CHG_2L_IVR_4300MV = 0,
	S2MU004_CHG_2L_IVR_4500MV,
	S2MU004_CHG_2L_IVR_4700MV,
	S2MU004_CHG_2L_IVR_4900MV,
};

enum {
	S2MU004_CHG_3L_IVR_7950MV = 0,
	S2MU004_CHG_3L_IVR_8150MV,
	S2MU004_CHG_3L_IVR_8350MV,
	S2MU004_CHG_3L_IVR_8550MV,
};

#if ENABLE_MIVR
/* charger input regulation voltage setting */
static void s2mu004_set_ivr_level(struct s2mu004_charger_data *charger)
{
	int chg_2l_ivr = S2MU004_CHG_2L_IVR_4500MV;
	int chg_3l_ivr = S2MU004_CHG_3L_IVR_7950MV;

	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL5, 0, 0xff);
	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL5,
		chg_2l_ivr << SET_CHG_2L_DROP_SHIFT ||
		chg_3l_ivr << SET_CHG_3L_DROP_SHIFT,
		SET_CHG_2L_DROP_MASK || SET_CHG_3L_DROP_MASK);
}
#endif /*ENABLE_MIVR*/

static void s2mu004_configure_charger(struct s2mu004_charger_data *charger)
{

	int eoc, current_limit = 0;
	union power_supply_propval chg_mode;
	union power_supply_propval swelling_state;

	pr_err("[DEBUG] %s\n", __func__);
	if (charger->charging_current < 0) {
		pr_err("%s : OTG is activated. Ignore command!\n",
				__func__);
		return;
	}

#if ENABLE_MIVR
	s2mu004_set_ivr_level(charger);
#endif /*DISABLE_MIVR*/

	/* Input current limit */
	if ((charger->dev_id == 0) && (charger->cable_type == POWER_SUPPLY_TYPE_USB)) {
		current_limit = 500; /* only for EVT0 */
	} else {
		current_limit = charger->pdata->charging_current_table
			[charger->cable_type].input_current_limit;
	}
	pr_err("[DEBUG] %s : input current (%dmA)\n", __func__, current_limit);
	s2mu004_set_input_current_limit(charger, current_limit);

	/* Float voltage */
	pr_err("[DEBUG] %s : float voltage (%dmV)\n",
			__func__, charger->pdata->chg_float_voltage);

	s2mu004_set_regulation_voltage(charger,
			charger->pdata->chg_float_voltage);

	charger->charging_current = charger->pdata->charging_current_table
		[charger->cable_type].fast_charging_current;

	/* Fast charge */
	pr_err("[DEBUG] %s : fast charging current (%dmA)\n",
			__func__, charger->charging_current);

	s2mu004_set_charging_current(charger);

	/* Termination current */
	if (charger->pdata->chg_eoc_dualpath == true) {
		eoc = charger->pdata->charging_current_table
			[charger->cable_type].full_check_current_1st;
		s2mu004_set_topoff_current(charger->client, 1, eoc);

		eoc = charger->pdata->charging_current_table
			[charger->cable_type].full_check_current_2nd;
		s2mu004_set_topoff_current(charger->client, 2, eoc);
	} else {
		if (charger->pdata->full_check_type_2nd == SEC_BATTERY_FULLCHARGED_CHGPSY) {
			psy_do_property("battery", get,
					POWER_SUPPLY_PROP_CHARGE_NOW,
					chg_mode);
#if defined(CONFIG_BATTERY_SWELLING)
			psy_do_property("battery", get,
					POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
					swelling_state);
#else
			swelling_state.intval = 0;
#endif
			if (chg_mode.intval == SEC_BATTERY_CHARGING_2ND || swelling_state.intval) {
				s2mu004_enable_charger_switch(charger, 0);
				eoc = charger->pdata->charging_current_table
					[charger->cable_type].full_check_current_2nd;
			} else {
				eoc = charger->pdata->charging_current_table
					[charger->cable_type].full_check_current_1st;
			}
		} else {
			eoc = charger->pdata->charging_current_table
				[charger->cable_type].full_check_current_1st;
		}
		pr_info("[DEBUG]%s : termination current (%dmA)\n",
			__func__, eoc);
		s2mu004_set_topoff_current(charger->client, 1, eoc);
	}
}

/* here is set init charger data */
static bool s2mu004_chg_init(struct s2mu004_charger_data *charger)
{
	u8 temp;

	/* Read Charger IC Dev ID */
	s2mu004_read_reg(charger->client, S2MU004_REG_REV_ID, &temp);
	charger->dev_id = (temp & 0xF0) >> 4;

	pr_info("%s : DEV ID : 0x%x\n", __func__, charger->dev_id);

	/* Poor-Chg-INT Masking */
	s2mu004_update_reg(charger->client, 0x32, 0x03, 0x03);

	/*
	 * When Self Discharge Function is activated, Charger doesn't stop charging.
	 * If you write 0xb0[4]=1, charger will stop the charging, when self discharge
	 * condition is satisfied.
	 */
	s2mu004_update_reg(charger->client, 0xb0, 0x0, 0x1 << 4);

	s2mu004_update_reg(charger->client, S2MU004_REG_SC_INT1_MASK,
			Poor_CHG_INT_MASK, Poor_CHG_INT_MASK);

	s2mu004_write_reg(charger->client, 0x02, 0x0);
	s2mu004_write_reg(charger->client, 0x03, 0x0);

	/* Set VSYSMIN 3.6V */
	s2mu004_write_reg(charger->client, S2MU004_CHG_CTRL7, 0xD6);

	/* ready for self-discharge, 0x76 */
	s2mu004_update_reg(charger->client, S2MU004_REG_SELFDIS_CFG3,
			SELF_DISCHG_MODE_MASK, SELF_DISCHG_MODE_MASK);

	/* Set Top-Off timer to 30 minutes */
	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL17,
			S2MU004_TOPOFF_TIMER_30m << TOP_OFF_TIME_SHIFT,
			TOP_OFF_TIME_MASK);

	s2mu004_read_reg(charger->client, S2MU004_CHG_CTRL17, &temp);
	pr_info("%s : S2MU004_CHG_CTRL17 : 0x%x\n", __func__, temp);

	/* IVR Recovery enable */
	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL13,
			0x1 << SET_IVR_Recovery_SHIFT, SET_IVR_Recovery_MASK);

	/* Boost OSC 1Mhz */
	s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL15,
			0x02 << SET_OSC_BST_SHIFT, SET_OSC_BST_MASK);

	/* QBAT switch speed config */
	s2mu004_update_reg(charger->client, 0xB2, 0x0, 0xf << 4);

	/* Top off debounce time set 1 sec */
	s2mu004_update_reg(charger->client, 0xC0, 0x3 << 6, 0x3 << 6);

	/* SC_CTRL21 register Minumum Charging OCP Level set to 6A */
	s2mu004_write_reg(charger->client, 0x29, 0x04);

	switch (charger->pdata->chg_switching_freq) {
	case S2MU004_OSC_BUCK_FRQ_750kHz:
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL12,
				S2MU004_OSC_BUCK_FRQ_750kHz << SET_OSC_BUCK_SHIFT, SET_OSC_BUCK_MASK);
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL12,
				S2MU004_OSC_BUCK_FRQ_750kHz << SET_OSC_BUCK_3L_SHIFT, SET_OSC_BUCK_3L_MASK);
		break;
	default:
		/* Set OSC BUCK/BUCK 3L frequencies to default 1MHz */
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL12,
				S2MU004_OSC_BUCK_FRQ_1MHz << SET_OSC_BUCK_SHIFT, SET_OSC_BUCK_MASK);
		s2mu004_update_reg(charger->client, S2MU004_CHG_CTRL12,
				S2MU004_OSC_BUCK_FRQ_1MHz << SET_OSC_BUCK_3L_SHIFT, SET_OSC_BUCK_3L_MASK);
		break;
	}
	s2mu004_read_reg(charger->client, S2MU004_CHG_CTRL12, &temp);
	pr_info("%s : S2MU004_CHG_CTRL12 : 0x%x\n", __func__, temp);

	return true;
}

static int s2mu004_get_charging_status(struct s2mu004_charger_data *charger)
{
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	int ret;
	u8 chg_sts0, chg_sts1;
	union power_supply_propval value;

	ret = s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS0, &chg_sts0);
	ret = s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS1, &chg_sts1);

	psy_do_property("s2mu004-fuelgauge", get, POWER_SUPPLY_PROP_CURRENT_AVG, value);

	if (ret < 0)
		return status;

	if (chg_sts1 & 0x80)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else if (chg_sts1 & 0x02 || chg_sts1 & 0x01) {
		if (value.intval < charger->pdata->charging_current_table[
				charger->cable_type].full_check_current_2nd)
			status = POWER_SUPPLY_STATUS_FULL;
		else
			status = POWER_SUPPLY_STATUS_CHARGING;
	} else if ((chg_sts0 & 0xE0) == 0xA0 || (chg_sts0 & 0xE0) == 0x60)
		status = POWER_SUPPLY_STATUS_CHARGING;
	else
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;

	s2mu004_test_read(charger->client);
	return status;
}

static int s2mu004_get_charge_type(struct i2c_client *iic)
{
	int status = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	u8 ret;

	s2mu004_read_reg(iic, S2MU004_CHG_STATUS3, &ret);
	if (ret < 0)
		dev_err(&iic->dev, "%s fail\n", __func__);

	switch ((ret & BAT_STATUS_MASK) >> BAT_STATUS_SHIFT) {
	case 0x4:
	case 0x5:
		status = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case 0x2:
		/* pre-charge mode */
		status = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	}

	return status;
}

static bool s2mu004_get_batt_present(struct i2c_client *iic)
{
	u8 ret;

	s2mu004_read_reg(iic, S2MU004_CHG_STATUS3, &ret);
	if (ret < 0)
		return false;

	return (ret & DET_BAT_STATUS_MASK) ? true : false;
}

static int s2mu004_get_charging_health(struct s2mu004_charger_data *charger)
{

	u8 ret, chg_sts;

	s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS0, &ret);
	pr_err("[DEBUG] %s %d %02x\n", __func__, __LINE__, ret);

	return POWER_SUPPLY_HEALTH_GOOD;
	if (ret < 0)
		return POWER_SUPPLY_HEALTH_UNKNOWN;

	chg_sts = (ret & (CHGIN_STATUS_MASK)) >> CHGIN_STATUS_SHIFT;
	if (chg_sts == 5 || chg_sts == 3) {
		charger->ovp = false;
		return POWER_SUPPLY_HEALTH_GOOD;
	}

	charger->unhealth_cnt++;
	if (charger->unhealth_cnt < HEALTH_DEBOUNCE_CNT)
		return POWER_SUPPLY_HEALTH_GOOD;

	/* 005 need to check ovp & health count */
	charger->unhealth_cnt = HEALTH_DEBOUNCE_CNT;
	if (charger->ovp)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	return POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
}


static int s2mu004_get_charging_current_avg(struct s2mu004_charger_data *charger)
{
	union power_supply_propval value;

	psy_do_property("s2mu004-fuelgauge", get, POWER_SUPPLY_PROP_CURRENT_AVG, value);
	pr_err("%s %d\n", __func__, value.intval);
	return value.intval;
}

static int sec_chg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int chg_curr, aicr;
	struct s2mu004_charger_data *charger =
		power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->is_charging ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = s2mu004_get_charging_status(charger);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = s2mu004_get_charging_health(charger);
		s2mu004_test_read(charger->client);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 2000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (charger->charging_current) {
			aicr = s2mu004_get_input_current_limit(charger->client);
			chg_curr = s2mu004_get_fast_charging_current(charger->client);
			val->intval = MINVAL(aicr, chg_curr);
		} else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = s2mu004_get_charge_type(charger->client);
		break;
#if defined(CONFIG_BATTERY_SWELLING) || defined(CONFIG_BATTERY_SWELLING_SELF_DISCHARGING)
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = charger->pdata->chg_float_voltage;
		break;
#endif
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = s2mu004_get_batt_present(charger->client);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = charger->is_charging;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = s2mu004_get_charging_current_avg(charger);
		break;
	case POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL:
	case POWER_SUPPLY_PROP_AUTHENTIC:
	case POWER_SUPPLY_PROP_MAX ... POWER_SUPPLY_EXT_PROP_MAX:
		return -ENODATA;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sec_chg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =
			power_supply_get_drvdata(psy);
	enum power_supply_ext_property ext_psp = psp;
	int buck_state = ENABLE;
	union power_supply_propval value;
	int eoc;
	int previous_cable_type = charger->cable_type;
	u8 temp;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		charger->status = val->intval;
		break;
		/* val->intval : type */
	case POWER_SUPPLY_PROP_ONLINE:
		charger->cable_type = val->intval;

		if (charger->cable_type == POWER_SUPPLY_TYPE_BATTERY ||
				charger->cable_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			pr_err("[DEBUG]%s:[BATT] Type Battery\n", __func__);

			if (previous_cable_type == POWER_SUPPLY_TYPE_OTG)
				s2mu004_charger_otg_control(charger, false);

			charger->charging_current = charger->pdata->charging_current_table
				[POWER_SUPPLY_TYPE_USB].fast_charging_current;
			s2mu004_set_input_current_limit(charger,
					charger->pdata->charging_current_table
					[POWER_SUPPLY_TYPE_USB].input_current_limit);
			s2mu004_set_charging_current(charger);
			s2mu004_set_topoff_current(charger->client, 1,
					charger->pdata->charging_current_table
					[POWER_SUPPLY_TYPE_USB].full_check_current_1st);
			charger->is_charging = false;
			s2mu004_enable_charger_switch(charger, 0);
		} else if (charger->cable_type == POWER_SUPPLY_TYPE_OTG) {
			pr_err("[DEBUG]%s: OTG mode\n", __func__);
			s2mu004_charger_otg_control(charger, true);
		} else {
			if (charger->cable_type == POWER_SUPPLY_TYPE_HV_MAINS)
				s2mu004_update_reg(charger->client, 0x33, 0x00, 0x3 << 4);

			s2mu004_read_reg(charger->client, 0x33, &temp);

			pr_err("[DEBUG]%s:[BATT] Set charging Cable type = %d 0x33 = %d\n",
					__func__, charger->cable_type, temp);
			/* Enable charger */
			s2mu004_configure_charger(charger);
			charger->is_charging = true;
			s2mu004_enable_charger_switch(charger, 1);
		}
#if EN_TEST_READ
		msleep(100);
#endif
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		pr_err("[DEBUG] %s: is_charging %d\n", __func__, charger->is_charging);
		/* set charging current */
		if (charger->is_charging) {
			/* decrease the charging current according to siop level */
			charger->siop_level = val->intval;
			/* charger->siop_level = 100; */
			pr_err("[DEBUG] %s:SIOP level = %d, chg current = %d\n", __func__,
					val->intval, charger->charging_current);
			eoc = s2mu004_get_current_eoc_setting(charger);
			s2mu004_set_charging_current(charger);
			s2mu004_set_topoff_current(charger->client, 1, 0);
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		break;
#if defined(CONFIG_BATTERY_SWELLING) || defined(CONFIG_BATTERY_SWELLING_SELF_DISCHARGING)
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pr_err("[DEBUG]%s: float voltage(%d)\n", __func__, val->intval);
		charger->pdata->chg_float_voltage = val->intval;
		s2mu004_set_regulation_voltage(charger,
				charger->pdata->chg_float_voltage);
		break;
#endif
	case POWER_SUPPLY_PROP_POWER_NOW:
		eoc = s2mu004_get_current_eoc_setting(charger);
		pr_err("[DEBUG]%s:Set Power Now -> chg current = %d mA, eoc = %d mA\n", __func__,
				val->intval, eoc);
		s2mu004_set_charging_current(charger);
		s2mu004_set_topoff_current(charger->client, 1, 0);
		break;
	case POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL:
		s2mu004_charger_otg_control(charger, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		charger->charge_mode = val->intval;
		psy_do_property("s2mu004-battery", get, POWER_SUPPLY_PROP_ONLINE, value);
		if (value.intval != POWER_SUPPLY_TYPE_OTG) {
			switch (charger->charge_mode) {
			case SEC_BAT_CHG_MODE_BUCK_OFF:
				buck_state = DISABLE;
			case SEC_BAT_CHG_MODE_CHARGING_OFF:
				charger->is_charging = false;
				break;
			case SEC_BAT_CHG_MODE_CHARGING:
				charger->is_charging = true;
				break;
			}
			value.intval = charger->is_charging;
			psy_do_property("s2mu004-fuelgauge", set,
					POWER_SUPPLY_PROP_CHARGING_ENABLED, value);

			if (buck_state) {
				s2mu004_enable_charger_switch(charger, charger->is_charging);
			} else {
				/* set buck off only if SEC_BAT_CHG_MODE_BUCK_OFF */
				s2mu004_set_buck(charger, buck_state);
			}
		} else {
			pr_info("[DEBUG]%s: SKIP CHARGING CONTROL while OTG(%d)\n",
					__func__, value.intval);
		}
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		if (val->intval) {
			pr_info("%s: Relieve VBUS2BAT\n", __func__);
			s2mu004_write_reg(charger->client, 0x2F, 0x5D);
		}
		break;
	case POWER_SUPPLY_PROP_AUTHENTIC:
		if (val->intval) {
			pr_info("%s: Bypass set\n", __func__);
			s2mu004_update_reg(charger->client, 0x29, 0x02, 0x02);
			s2mu004_update_reg(charger->client, 0x2C, 0x00, 0xC0);
			s2mu004_update_reg(charger->client, 0x32, 0x00, 0x30);
		}
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		break;
	case POWER_SUPPLY_PROP_MAX ... POWER_SUPPLY_EXT_PROP_MAX:
		switch (ext_psp) {
		case POWER_SUPPLY_EXT_PROP_FUELGAUGE_RESET:
			s2mu004_write_reg(charger->client, 0x6F, 0xC4);
			msleep(1000);
			s2mu004_write_reg(charger->client, 0x6F, 0x04);
			msleep(50);
			pr_info("%s: reset fuelgauge when surge occur!\n", __func__);
			break;
		case POWER_SUPPLY_EXT_PROP_ANDIG_IVR_SWITCH:
			if (charger->dev_id < 0x3)
				s2mu004_analog_ivr_switch(charger, val->intval);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#if defined(CONFIG_FUELGAUGE_S2MU004) || defined(CONFIG_MUIC_NOTIFIER)
static void sec_bat_get_battery_info(struct work_struct *work)
{
	struct s2mu004_charger_data *charger =
	container_of(work, struct s2mu004_charger_data, polling_work.work);

#if defined(CONFIG_FUELGAUGE_S2MU004)
	u8 ret = 0;
	union power_supply_propval value;

	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, value);
	charger->voltage_now = value.intval;

	value.intval = SEC_BATTERY_VOLTAGE_AVERAGE;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
	charger->voltage_avg = value.intval;

	value.intval = SEC_BATTERY_VOLTAGE_OCV;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
	charger->voltage_ocv = value.intval;

	value.intval = 0;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_CURRENT_NOW, value);
	charger->current_now = value.intval;

	value.intval = 0;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_CURRENT_AVG, value);
	charger->current_avg = value.intval;

	/* To get SOC value (NOT raw SOC), need to reset value */
	value.intval = 0;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_CAPACITY, value);
	charger->capacity = value.intval;

	/* To get temperature info */
	psy_do_property(charger->pdata->fuelgauge_name, get,
			POWER_SUPPLY_PROP_TEMP, value);
	charger->temperature = value.intval;

	value.intval = 0;
	psy_do_property(charger->pdata->charger_name, get,
		POWER_SUPPLY_PROP_STATUS, value);
	charger->status = value.intval;

	pr_info("%s: status(%d) is_charging(%d)\n", __func__, charger->status, charger->is_charging);

	if (charger->status == POWER_SUPPLY_STATUS_NOT_CHARGING)
		goto print_logs;

	/* 1. recharging check */
	if (charger->voltage_now < charger->pdata->recharge_vcell &&
			!charger->is_charging) {
		pr_info("%s: recharging start\n", __func__);
		value.intval = SEC_BAT_CHG_MODE_CHARGING;
		psy_do_property(charger->pdata->charger_name, set,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, value);
	}

	if (!charger->is_charging)
		goto print_logs;

	/* 2. Full charged check */
	if ((charger->current_now > 0 && charger->current_now <
		charger->pdata->charging_current_table[
		charger->cable_type].full_check_current_2nd) &&
		(charger->voltage_avg > charger->pdata->chg_full_vcell)) {
		charger->full_check_cnt++;
		pr_info("%s: Full Check Cnt (%d)\n", __func__, charger->full_check_cnt);
	}
	/* Check charging current status */
	if (charger->status == POWER_SUPPLY_STATUS_CHARGING &&
			charger->is_charging == true &&
			charger->cable_type == POWER_SUPPLY_TYPE_MAINS) {
		s2mu004_read_reg(charger->client, S2MU004_CHG_CTRL2, &ret);
		if ((ret & INPUT_CURRENT_LIMIT_MASK) == 0x12) {
			pr_err("%s: reconfigure charger current limit\n", __func__);
			s2mu004_configure_charger(charger);  /* re-configure charger */
		}
	}

	if (charger->full_check_cnt >= S2MU004_CHGFULL_CNT) {
		charger->full_check_cnt = 0;
		/* charging off (BUCK mode) */
		value.intval = SEC_BAT_CHG_MODE_CHARGING_OFF;
		psy_do_property(charger->pdata->charger_name, set,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, value);
		pr_info("%s: Full charged, charger off\n", __func__);
	}

print_logs:
	pr_info("%s: voltage_now: (%d), voltage_avg: (%d),"
		"voltage_ocv: (%d), current_avg: (%d), capacity: (%d)\n",
		__func__, charger->voltage_now, charger->voltage_avg,
				charger->voltage_ocv, charger->current_avg,
				charger->capacity);

	if (!charger->battery_valid) {
		s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS3, &ret);
		if (ret & DET_BAT_STATUS_MASK)
			charger->battery_valid = true;
		else
			charger->battery_valid = false;
	}

	s2mu004_test_read(charger->client);

	power_supply_changed(charger->psy_battery);
	schedule_delayed_work(&charger->polling_work, HZ * 10);
#endif

#if defined(CONFIG_MUIC_NOTIFIER)
	if (!charger->noti_check)
		muic_notifier_register(&charger->batt_nb,
					charger_handle_notification,
					MUIC_NOTIFY_DEV_CHARGER);
	charger->noti_check = true;
#endif
}
#endif


/* s2mu004 interrupt service routine */
static irqreturn_t s2mu004_det_bat_isr(int irq, void *data)
{
	struct s2mu004_charger_data *charger = data;
	u8 val;

	s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS3, &val);
	if ((val & DET_BAT_STATUS_MASK) == 0) {
		s2mu004_enable_charger_switch(charger, 0);
		pr_err("charger-off if battery removed\n");
	}
	return IRQ_HANDLED;
}

static irqreturn_t s2mu004_done_isr(int irq, void *data)
{
	struct s2mu004_charger_data *charger = data;
	u8 val;

	s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS1, &val);
	pr_err("%s , %02x\n", __func__, val);
	if (val & (DONE_STATUS_MASK)) {
		pr_err("add self chg done\n");
		/* add chg done code here */
	}
	return IRQ_HANDLED;
}

static irqreturn_t s2mu004_top_off_isr(int irq, void *data)
{
	struct s2mu004_charger_data *charger = data;
	u8 val;

	s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS1, &val);
	pr_err("%s , %02x\n", __func__, val);
	if (val & (TOP_OFF_STATUS_MASK)) {
		pr_err("add self chg done\n");
		/* add chg done code here */
	}
	return IRQ_HANDLED;
}

static irqreturn_t s2mu004_chg_isr(int irq, void *data)
{
	struct s2mu004_charger_data *charger = data;
	u8 val;

	s2mu004_read_reg(charger->client, S2MU004_CHG_STATUS0, &val);
	pr_info("%s , %02x\n", __func__, val);
#if 0
	if ((val & CHGIN_STATUS_MASK) == (2 << CHGIN_STATUS_SHIFT)) {
		charger->ovp = true;
		pr_info("%s: OVP triggered\n", __func__);
		value.intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		s2mu004_update_reg(charger->client, 0xBE, 0x10, 0x10);
		psy_do_property("battery", set,
			POWER_SUPPLY_PROP_HEALTH, value);
	} else if ((val & CHGIN_STATUS_MASK) == (3 << CHGIN_STATUS_SHIFT) ||
			(val & CHGIN_STATUS_MASK) == (5 << CHGIN_STATUS_SHIFT)) {
		dev_dbg(&charger->client->dev, "%s: Vbus status 0x%x\n", __func__, val);
		charger->unhealth_cnt = HEALTH_DEBOUNCE_CNT;
		charger->ovp = false;
		pr_info("%s: recover from OVP\n", __func__);
		value.intval = POWER_SUPPLY_HEALTH_GOOD;
		s2mu004_update_reg(charger->client, 0xBE, 0x00, 0x10);
		psy_do_property("battery", set,
			POWER_SUPPLY_PROP_HEALTH, value);
	}
#endif
	return IRQ_HANDLED;
}

static irqreturn_t s2mu004_event_isr(int irq, void *data)
{
	pr_info("%s event!\n", __func__);

	return IRQ_HANDLED;
}
static irqreturn_t s2mu004_ovp_isr(int irq, void *data)
{
	pr_info("%s ovp!\n", __func__);

	return IRQ_HANDLED;
}

static int s2mu004_charger_parse_dt(struct device *dev,
		struct s2mu004_charger_platform_data *pdata)
{
	struct device_node *np = of_find_node_by_name(NULL, "s2mu004-charger");
	const u32 *p;
	int ret, i, len;

	if (!np) {
		pr_err("%s np NULL\n", __func__);
	} else {
		/* SC_CTRL8 , SET_VF_VBAT , Battery regulation voltage setting */
		ret = of_property_read_u32(np, "battery,chg_float_voltage",
			&pdata->chg_float_voltage);

		ret = of_property_read_string(np,
			"battery,charger_name", (char const **)&pdata->charger_name);

		ret = of_property_read_u32(np, "battery,full_check_type_2nd",
				&pdata->full_check_type_2nd);
		if (ret)
			pr_info("%s : Full check type 2nd is Empty\n", __func__);

		pdata->chg_eoc_dualpath = of_property_read_bool(np,
				"battery,chg_eoc_dualpath");

		ret = of_property_read_u32(np, "battery,chg_recharge_vcell",
			&pdata->recharge_vcell);
		pr_err("%s, recharge_vcell %d\n", __func__, pdata->recharge_vcell);

		ret = of_property_read_u32(np, "battery,chg_full_vcell",
			&pdata->chg_full_vcell);
		pr_err("%s, chg_full_vcell %d\n", __func__, pdata->chg_full_vcell);

		p = of_get_property(np, "battery,input_current_limit", &len);
		if (!p)
			return 1;

		len = len / sizeof(u32);

		pdata->charging_current_table = kzalloc(sizeof(sec_charging_current_t) * len,
				GFP_KERNEL);

		for (i = 0; i < len; i++) {
			ret = of_property_read_u32_index(np,
					"battery,input_current_limit", i,
					&pdata->charging_current_table[i].input_current_limit);
			ret = of_property_read_u32_index(np,
					"battery,fast_charging_current", i,
					&pdata->charging_current_table[i].fast_charging_current);
			ret = of_property_read_u32_index(np,
					"battery,full_check_current_1st", i,
					&pdata->charging_current_table[i].full_check_current_1st);
			ret = of_property_read_u32_index(np,
					"battery,full_check_current_2nd", i,
					&pdata->charging_current_table[i].full_check_current_2nd);
		}
	}

	dev_dbg(dev, "s2mu004 charger parse dt retval = %d\n", ret);
	return ret;
}

/* if need to set s2mu004 pdata */
static const struct of_device_id s2mu004_charger_match_table[] = {
	{ .compatible = "samsung,s2mu004-charger",},
	{},
};

void s2mu004_afc_current_down(struct s2mu004_charger_data *charger)
{
	pr_err("[DEBUG] %s(%d)\n", __func__, __LINE__);
	charger->charging_current = charger->pdata->charging_current_table
		[charger->cable_type].fast_charging_current;
	s2mu004_set_input_current_limit(charger, 1350);
	s2mu004_set_charging_current(charger);
}

static int s2mu004_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =  power_supply_get_drvdata(psy);

#if defined(CONFIG_FUELGAUGE_S2MU004)
	union power_supply_propval value;
#endif
	int ret = 0;

	dev_dbg(&charger->client->dev, "prop: %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = s2mu004_get_charging_status(charger);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = s2mu004_get_charging_health(charger);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->battery_cable_type;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->battery_valid;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
#if defined(CONFIG_FUELGAUGE_S2MU004)
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!charger->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else {
			psy_do_property_dup(charger->pdata->fuelgauge_name, get,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, value);
			charger->voltage_now = value.intval;
			dev_err(&charger->client->dev,
				"%s: voltage now(%d)\n", __func__,
							charger->voltage_now);
			val->intval = charger->voltage_now * 1000;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		value.intval = SEC_BATTERY_VOLTAGE_AVERAGE;
		psy_do_property_dup(charger->pdata->fuelgauge_name, get,
				POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
		charger->voltage_avg = value.intval;
		dev_err(&charger->client->dev,
			"%s: voltage avg(%d)\n", __func__,
						charger->voltage_avg);
		val->intval = charger->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = charger->temperature;
		break;
#endif
	case POWER_SUPPLY_PROP_CAPACITY:
#if defined(CONFIG_FUELGAUGE_S2MU004)
		if (!charger->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else
			val->intval = charger->capacity;
#else
		val->intval = FAKE_BAT_LEVEL;
#endif
		break;
	default:
		ret = -ENODATA;
	}

	return ret;
}

static int s2mu004_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2mu004_charger_data *charger =  power_supply_get_drvdata(psy);
	int ret = 0;

	dev_dbg(&charger->client->dev, "prop: %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		charger->status = val->intval;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		charger->health = val->intval;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		charger->battery_cable_type = val->intval;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int s2mu005_bat_cable_check(struct s2mu004_charger_data *charger,
				muic_attached_dev_t attached_dev)
{
	int current_cable_type = -1;

	pr_info("[%s]ATTACHED(%d)\n", __func__, attached_dev);

	switch (attached_dev) {
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		break;
	case ATTACHED_DEV_SMARTDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case ATTACHED_DEV_OTG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:
	case ATTACHED_DEV_HMT_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_OTG;
		break;
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_SMARTDOCK_USB_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_USB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_UARTOFF;
		break;
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_CARDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_VB_MUIC:
	case ATTACHED_DEV_SMARTDOCK_TA_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_ANY_MUIC:
	case ATTACHED_DEV_QC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_MAINS;
		break;
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_CDP_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case ATTACHED_DEV_USB_LANHUB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_LAN_HUB;
		break;
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_POWER_SHARING;
		break;
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_PREPARE_MAINS;
		break;
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_MAINS;
		break;
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_ERR_V_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_ERR;
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case ATTACHED_DEV_HV_ID_ERR_UNDEFINED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_UNSUPPORTED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_SUPPORTED_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_UNKNOWN;
		break;
	default:
		pr_err("%s: invalid type for charger:%d\n",
			__func__, attached_dev);
	}

	return current_cable_type;
}

static int charger_handle_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
	muic_attached_dev_t attached_dev = *(muic_attached_dev_t *)data;
	const char *cmd;
	int cable_type;
	union power_supply_propval value;
	struct s2mu004_charger_data *charger =
		container_of(nb, struct s2mu004_charger_data, batt_nb);

	if (attached_dev == ATTACHED_DEV_MHL_MUIC)
		return 0;

	switch (action) {
	case MUIC_NOTIFY_CMD_DETACH:
	case MUIC_NOTIFY_CMD_LOGICALLY_DETACH:
		cmd = "DETACH";
		cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case MUIC_NOTIFY_CMD_ATTACH:
	case MUIC_NOTIFY_CMD_LOGICALLY_ATTACH:
		cmd = "ATTACH";
		cable_type = s2mu005_bat_cable_check(charger, attached_dev);
		break;
	default:
		cmd = "ERROR";
		cable_type = -1;
		break;
	}

	pr_info("%s: current_cable(%d) former cable_type(%d) battery_valid(%d)\n",
			__func__, cable_type, charger->battery_cable_type,
							charger->battery_valid);
	if (charger->battery_valid == false) {
		pr_info("%s: Battery is disconnected\n",
						__func__);
	}

	if (attached_dev == ATTACHED_DEV_OTG_MUIC) {
		if (!strcmp(cmd, "ATTACH")) {
			value.intval = true;
			charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
					value);
			pr_info("%s: OTG cable attached\n", __func__);
		} else {
			value.intval = false;
			charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
					value);
			pr_info("%s: OTG cable detached\n", __func__);
		}
	}

	if ((cable_type >= 0) &&
		cable_type <= SEC_SIZEOF_POWER_SUPPLY_TYPE) {
		if (cable_type != charger->battery_cable_type) {
			value.intval = charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_ONLINE,
					value);
		} else {
			pr_info("%s: Cable is Not Changed(%d)\n",
				__func__, charger->battery_cable_type);
		}
	}
	power_supply_changed(charger->psy_battery);

	pr_info("%s: CMD=%s, attached_dev=%d battery_cable=%d\n",
		__func__, cmd, attached_dev, charger->battery_cable_type);

	/* Workaround to notify cable status to fuelgauge */
	value.intval = cable_type; /* To use unique value  */
	psy_do_property("s2mu004-fuelgauge", set, POWER_SUPPLY_PROP_ONLINE, value);

	value.intval = 0x78; /* To use unique value */
	psy_do_property("s2mu004-fuelgauge", set, POWER_SUPPLY_PROP_STATUS, value);
	/* WA till here */

	return 0;
}

static int enable_irq_flag = 1;
static int s2mu004_charger_probe(struct platform_device *pdev)
{
	struct s2mu004_dev *s2mu004 = dev_get_drvdata(pdev->dev.parent);
	struct s2mu004_platform_data *pdata = dev_get_platdata(s2mu004->dev);
	struct s2mu004_charger_data *charger;
	struct power_supply_config psy_cfg = {};
	int ret = 0;
	u8 temp;

	pr_err("%s:[BATT] S2MU004 Charger driver probe\n", __func__);
	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	mutex_init(&charger->io_lock);

	charger->dev = &pdev->dev;
	charger->client = s2mu004->i2c;
	charger->pdata = devm_kzalloc(&pdev->dev, sizeof(*(charger->pdata)),
			GFP_KERNEL);
	if (!charger->pdata) {
		ret = -ENOMEM;
		goto err_parse_dt_nomem;
	}
	ret = s2mu004_charger_parse_dt(&pdev->dev, charger->pdata);
	if (ret < 0)
		goto err_parse_dt;

	platform_set_drvdata(pdev, charger);

	if (charger->pdata->charger_name == NULL)
		charger->pdata->charger_name = "s2mu004-charger";

	charger->psy_chg_desc.name           = charger->pdata->charger_name;
	charger->psy_chg_desc.type           = POWER_SUPPLY_TYPE_UNKNOWN;
	charger->psy_chg_desc.get_property   = sec_chg_get_property;
	charger->psy_chg_desc.set_property   = sec_chg_set_property;
	charger->psy_chg_desc.properties     = sec_charger_props;
	charger->psy_chg_desc.num_properties = ARRAY_SIZE(sec_charger_props);

#ifdef CONFIG_FUELGAUGE_S2MU004
	if (charger->pdata->fuelgauge_name == NULL)
		charger->pdata->fuelgauge_name = "s2mu004-fuelgauge";
#endif

	charger->psy_battery_desc.name = "s2mu004-battery";
	charger->psy_battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	charger->psy_battery_desc.get_property =  s2mu004_battery_get_property;
	charger->psy_battery_desc.set_property =  s2mu004_battery_set_property;
	charger->psy_battery_desc.properties = s2mu004_battery_props;
	charger->psy_battery_desc.num_properties =  ARRAY_SIZE(s2mu004_battery_props);

	charger->psy_usb_desc.name = "s2mu004-usb";
	charger->psy_usb_desc.type = POWER_SUPPLY_TYPE_USB;
	charger->psy_usb_desc.get_property = s2mu004_usb_get_property;
	charger->psy_usb_desc.properties = s2mu004_power_props;
	charger->psy_usb_desc.num_properties = ARRAY_SIZE(s2mu004_power_props);

	charger->psy_ac_desc.name = "s2mu004-ac";
	charger->psy_ac_desc.type = POWER_SUPPLY_TYPE_MAINS;
	charger->psy_ac_desc.properties = s2mu004_power_props;
	charger->psy_ac_desc.num_properties = ARRAY_SIZE(s2mu004_power_props);
	charger->psy_ac_desc.get_property = s2mu004_ac_get_property;

	charger->psy_otg_desc.name           = "otg";
	charger->psy_otg_desc.type           = POWER_SUPPLY_TYPE_OTG;
	charger->psy_otg_desc.get_property   = s2mu004_otg_get_property;
	charger->psy_otg_desc.set_property   = s2mu004_otg_set_property;
	charger->psy_otg_desc.properties     = s2mu004_otg_props;
	charger->psy_otg_desc.num_properties = ARRAY_SIZE(s2mu004_otg_props);

	/* need to check siop level */
	charger->siop_level = 100;

	s2mu004_chg_init(charger);
	charger->is_charging = false;

	psy_cfg.drv_data = charger;
	psy_cfg.supplied_to = s2mu004_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(s2mu004_supplied_to),

	charger->psy_battery = power_supply_register(&pdev->dev, &charger->psy_battery_desc, &psy_cfg);
	if (IS_ERR(charger->psy_battery)) {
		pr_err("%s: Failed to Register psy_battery\n", __func__);
		ret = PTR_ERR(charger->psy_battery);
		goto err_power_supply_register;
	}

	charger->psy_usb = power_supply_register(&pdev->dev, &charger->psy_usb_desc, &psy_cfg);
	if (IS_ERR(charger->psy_usb)) {
		pr_err("%s: Failed to Register psy_usb\n", __func__);
		ret = PTR_ERR(charger->psy_usb);
		goto err_power_supply_register;
	}

	charger->psy_ac = power_supply_register(&pdev->dev, &charger->psy_ac_desc, &psy_cfg);
	if (IS_ERR(charger->psy_ac)) {
		pr_err("%s: Failed to Register psy_ac\n", __func__);
		ret = PTR_ERR(charger->psy_ac);
		goto err_power_supply_register;
	}

	charger->psy_chg = power_supply_register(&pdev->dev, &charger->psy_chg_desc, &psy_cfg);
	if (IS_ERR(charger->psy_chg)) {
		pr_err("%s: Failed to Register psy_chg\n", __func__);
		ret = PTR_ERR(charger->psy_chg);
		goto err_power_supply_register;
	}

	charger->psy_otg = power_supply_register(&pdev->dev, &charger->psy_otg_desc, &psy_cfg);
	if (IS_ERR(charger->psy_otg)) {
		pr_err("%s: Failed to Register psy_otg\n", __func__);
		ret = PTR_ERR(charger->psy_otg);
		goto err_reg_irq;
	}

	charger->charger_wqueue = create_singlethread_workqueue("charger-wq");
	if (!charger->charger_wqueue) {
		dev_dbg(&charger->client->dev, "%s: failed to create wq.\n", __func__);
		ret = -ESRCH;
		goto err_create_wq;
	}


if (enable_irq_flag) {
	/*
	 * irq request
	 * if you need to add irq , please refer below code.
	 */
	charger->irq_sys = pdata->irq_base + S2MU004_CHG1_IRQ_SYS;
	ret = request_threaded_irq(charger->irq_sys, NULL,
			s2mu004_ovp_isr, 0, "det-bat-in-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to request det bat in IRQ: %d: %d\n",
					__func__, charger->irq_det_bat, ret);
		goto err_reg_irq;
	}

	charger->irq_det_bat = pdata->irq_base + S2MU004_CHG2_IRQ_DET_BAT;
	ret = request_threaded_irq(charger->irq_det_bat, NULL,
			s2mu004_det_bat_isr, 0, "det-bat-in-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to request det bat in IRQ: %d: %d\n",
					__func__, charger->irq_det_bat, ret);
		goto err_reg_irq;
	}

	charger->irq_chg = pdata->irq_base + S2MU004_CHG1_IRQ_CHGIN;
	ret = request_threaded_irq(charger->irq_chg, NULL,
			s2mu004_chg_isr, 0, "chg-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to request det bat in IRQ: %d: %d\n",
					__func__, charger->irq_chg, ret);
		goto err_reg_irq;
	}

	charger->irq_rst = pdata->irq_base + S2MU004_CHG1_IRQ_CHG_RSTART;
	ret = request_threaded_irq(charger->irq_rst, NULL,
			s2mu004_chg_isr, 0, "restart-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to reset in IRQ: %d: %d\n",
					__func__, charger->irq_rst, ret);
		goto err_reg_irq;
	}

	charger->irq_done = pdata->irq_base + S2MU004_CHG1_IRQ_DONE;
	ret = request_threaded_irq(charger->irq_done, NULL,
			s2mu004_done_isr, 0, "done-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to DONE-irq in IRQ: %d: %d\n",
					__func__, charger->irq_done, ret);
		goto err_reg_irq;
	}

	charger->irq_event = pdata->irq_base + S2MU004_CHG1_IRQ_CHG_Fault;
	ret = request_threaded_irq(charger->irq_event, NULL,
			s2mu004_event_isr, 0, "event-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to request event-irq in IRQ: %d: %d\n",
					__func__, charger->irq_event, ret);
		goto err_reg_irq;
	}
} else {
	charger->irq_done = pdata->irq_base + S2MU004_CHG1_IRQ_DONE;
	ret = request_threaded_irq(charger->irq_done, NULL,
			s2mu004_done_isr, 0, "done-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to DONE-irq in IRQ: %d: %d\n",
					__func__, charger->irq_done, ret);
		goto err_reg_irq;
	}

	charger->irq_done = pdata->irq_base + S2MU004_CHG1_IRQ_TOP_OFF;
	ret = request_threaded_irq(charger->irq_done, NULL,
			s2mu004_top_off_isr, 0, "done-irq", charger);
	if (ret < 0) {
		dev_err(s2mu004->dev, "%s: Fail to DONE-irq in IRQ: %d: %d\n",
					__func__, charger->irq_done, ret);
		goto err_reg_irq;
	}
}

	s2mu004_test_read(charger->client);

	/* WA to remove topoff & done isr */
	s2mu004_read_reg(charger->client, 0x02, &temp);
	pr_info("[##Debug 01] CHG_INT1_MASK %x\n", temp);
	temp |= 0x30;
	pr_info("[##Debug 02] CHG_INT1_MASK %x\n", temp);
	s2mu004_write_reg(charger->client, 0x02, temp);
	/* WA at here */

	charger->noti_check = false;

#if defined(CONFIG_FUELGAUGE_S2MU004) || defined(CONFIG_MUIC_NOTIFIER)
	INIT_DELAYED_WORK(&charger->polling_work, sec_bat_get_battery_info);
	schedule_delayed_work(&charger->polling_work, HZ * 5);
#endif

	pr_err("%s:[BATT] S2MU004 charger driver loaded OK\n", __func__);

	return 0;

err_create_wq:
	destroy_workqueue(charger->charger_wqueue);
	power_supply_unregister(charger->psy_otg);
err_reg_irq:
	power_supply_unregister(charger->psy_chg);
err_power_supply_register:
err_parse_dt:
err_parse_dt_nomem:
	mutex_destroy(&charger->io_lock);
	kfree(charger);
	return ret;
}

static int s2mu004_charger_remove(struct platform_device *pdev)
{
	struct s2mu004_charger_data *charger =
		platform_get_drvdata(pdev);

	power_supply_unregister(charger->psy_chg);
	mutex_destroy(&charger->io_lock);
	kfree(charger);
	return 0;
}

#if defined CONFIG_PM
static int s2mu004_charger_prepare(struct device *dev)
{
	struct s2mu004_charger_data *charger = dev_get_drvdata(dev);

	pr_info("%s : cancel polling_work\n", __func__);
	cancel_delayed_work_sync(&charger->polling_work);

	return 0;
}

static int s2mu004_charger_suspend(struct device *dev)
{
	return 0;
}

static int s2mu004_charger_resume(struct device *dev)
{
	return 0;
}

static void s2mu004_charger_complete(struct device *dev)
{
	struct s2mu004_charger_data *charger = dev_get_drvdata(dev);

	pr_info("%s : scheduling polling_work\n", __func__);
	schedule_delayed_work(&charger->polling_work, HZ * 2);
}
#else
#define s2mu004_charger_suspend NULL
#define s2mu004_charger_resume NULL
#endif

static void s2mu004_charger_shutdown(struct device *dev)
{
	pr_info("%s: S2MU004 Charger driver shutdown\n", __func__);
}

static const struct dev_pm_ops s2mu004_charger_pm_ops = {
	.prepare = s2mu004_charger_prepare,
	.suspend = s2mu004_charger_suspend,
	.resume = s2mu004_charger_resume,
	.complete = s2mu004_charger_complete,
};

static struct platform_driver s2mu004_charger_driver = {
	.driver = {
		.name   = "s2mu004-charger",
		.owner  = THIS_MODULE,
		.of_match_table = s2mu004_charger_match_table,
		.pm     = &s2mu004_charger_pm_ops,
		.shutdown = s2mu004_charger_shutdown,
	},
	.probe      = s2mu004_charger_probe,
	.remove		= s2mu004_charger_remove,
};

static int __init s2mu004_charger_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&s2mu004_charger_driver);
	return ret;
}
module_init(s2mu004_charger_init);

static void __exit s2mu004_charger_exit(void)
{
	platform_driver_unregister(&s2mu004_charger_driver);
}
module_exit(s2mu004_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("Charger driver for S2MU004");
