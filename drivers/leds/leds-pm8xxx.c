/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/mfd/pm8xxx/core.h>
#include <linux/mfd/pm8xxx/pwm.h>
#include <linux/leds-pm8xxx.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define SSBI_REG_ADDR_DRV_KEYPAD	0x48
#define PM8XXX_DRV_KEYPAD_BL_MASK	0xf0
#define PM8XXX_DRV_KEYPAD_BL_SHIFT	0x04

#define SSBI_REG_ADDR_FLASH_DRV0        0x49
#define PM8XXX_DRV_FLASH_MASK           0xf0
#define PM8XXX_DRV_FLASH_SHIFT          0x04

#define SSBI_REG_ADDR_FLASH_DRV1        0xFB

#define SSBI_REG_ADDR_LED_CTRL_BASE	0x131
#define SSBI_REG_ADDR_LED_CTRL(n)	(SSBI_REG_ADDR_LED_CTRL_BASE + (n))
#define PM8XXX_DRV_LED_CTRL_MASK	0xf8
#define PM8XXX_DRV_LED_CTRL_SHIFT	0x03

#define SSBI_REG_ADDR_WLED_CTRL_BASE	0x25A
#define SSBI_REG_ADDR_WLED_CTRL(n)	(SSBI_REG_ADDR_WLED_CTRL_BASE + (n) - 1)

/* wled control registers */
#define WLED_MOD_CTRL_REG		SSBI_REG_ADDR_WLED_CTRL(1)
#define WLED_MAX_CURR_CFG_REG(n)	SSBI_REG_ADDR_WLED_CTRL(n + 2)
#define WLED_BRIGHTNESS_CNTL_REG1(n)	SSBI_REG_ADDR_WLED_CTRL((2 * n) + 5)
#define WLED_BRIGHTNESS_CNTL_REG2(n)	SSBI_REG_ADDR_WLED_CTRL((2 * n) + 6)
#define WLED_SYNC_REG			SSBI_REG_ADDR_WLED_CTRL(11)
#define WLED_OVP_CFG_REG		SSBI_REG_ADDR_WLED_CTRL(13)
#define WLED_BOOST_CFG_REG		SSBI_REG_ADDR_WLED_CTRL(14)
#define WLED_HIGH_POLE_CAP_REG		SSBI_REG_ADDR_WLED_CTRL(16)

#define WLED_STRINGS			0x03
#define WLED_OVP_VAL_MASK		0x30
#define WLED_OVP_VAL_BIT_SHFT		0x04
#define WLED_BOOST_LIMIT_MASK		0xE0
#define WLED_BOOST_LIMIT_BIT_SHFT	0x05
#define WLED_BOOST_OFF			0x00
#define WLED_EN_MASK			0x01
#define WLED_CP_SELECT_MAX		0x03
#define WLED_CP_SELECT_MASK		0x03
#define WLED_DIG_MOD_GEN_MASK		0x70
#define WLED_CS_OUT_MASK		0x0E
#define WLED_CTL_DLY_STEP		200
#define WLED_CTL_DLY_MAX		1400
#define WLED_CTL_DLY_MASK		0xE0
#define WLED_CTL_DLY_BIT_SHFT		0x05
#define WLED_MAX_CURR			25
#define WLED_MAX_CURR_MASK		0x1F
#define WLED_OP_FDBCK_MASK		0x1C
#define WLED_OP_FDBCK_BIT_SHFT		0x02

#define WLED_MAX_LEVEL			255
#define WLED_8_BIT_MASK			0xFF
#define WLED_8_BIT_SHFT			0x08
#define WLED_MAX_DUTY_CYCLE		0xFFF

#define WLED_SYNC_VAL			0x07
#define WLED_SYNC_RESET_VAL		0x00
#define WLED_SYNC_MASK			0xF8

#define ONE_WLED_STRING			1
#define TWO_WLED_STRINGS		2
#define THREE_WLED_STRINGS		3

#define WLED_CABC_ONE_STRING		0x01
#define WLED_CABC_TWO_STRING		0x03
#define WLED_CABC_THREE_STRING		0x07

#define WLED_CABC_SHIFT			3

#define SSBI_REG_ADDR_RGB_CNTL1		0x12D
#define SSBI_REG_ADDR_RGB_CNTL2		0x12E

#define PM8XXX_DRV_RGB_RED_LED		BIT(2)
#define PM8XXX_DRV_RGB_GREEN_LED	BIT(1)
#define PM8XXX_DRV_RGB_BLUE_LED		BIT(0)

#define MAX_FLASH_LED_CURRENT		300
#define MAX_LC_LED_CURRENT		40
#define MAX_KP_BL_LED_CURRENT		300

#ifdef ORIGIN
#define PM8XXX_ID_LED_CURRENT_FACTOR	2  /* Iout = x * 2mA */
#define PM8XXX_ID_FLASH_CURRENT_FACTOR	20 /* Iout = x * 20mA */
#else
#define PM8XXX_ID_LED_CURRENT_FACTOR	1  /* Iout = x * 2mA */
#define PM8XXX_ID_FLASH_CURRENT_FACTOR	20 /* Iout = x * 20mA */
#endif
#define PM8XXX_FLASH_MODE_DBUS1		1
#define PM8XXX_FLASH_MODE_DBUS2		2
#define PM8XXX_FLASH_MODE_PWM		3

#define MAX_LC_LED_BRIGHTNESS		20
#define MAX_FLASH_BRIGHTNESS		15
#define MAX_KB_LED_BRIGHTNESS		15

#define PM8XXX_LED_OFFSET(id) ((id) - PM8XXX_ID_LED_0)

#define PM8XXX_LED_PWM_FLAGS	(PM_PWM_LUT_LOOP | PM_PWM_LUT_RAMP_UP)

#define LED_MAP(_version, _kb, _led0, _led1, _led2, _flash_led0, _flash_led1, \
	_wled, _rgb_led_red, _rgb_led_green, _rgb_led_blue)\
	{\
		.version = _version,\
		.supported = _kb << PM8XXX_ID_LED_KB_LIGHT | \
			_led0 << PM8XXX_ID_LED_0 | _led1 << PM8XXX_ID_LED_1 | \
			_led2 << PM8XXX_ID_LED_2  | \
			_flash_led0 << PM8XXX_ID_FLASH_LED_0 | \
			_flash_led1 << PM8XXX_ID_FLASH_LED_1 | \
			_wled << PM8XXX_ID_WLED | \
			_rgb_led_red << PM8XXX_ID_RGB_LED_RED | \
			_rgb_led_green << PM8XXX_ID_RGB_LED_GREEN | \
			_rgb_led_blue << PM8XXX_ID_RGB_LED_BLUE, \
	}
#if defined(CONFIG_SKY_EF52S_BOARD)|defined(CONFIG_SKY_EF52K_BOARD)|defined(CONFIG_SKY_EF52L_BOARD)|defined(CONFIG_SKY_EF52W_BOARD) //p12911
//p12911 2012-04-16
#define PANTECH_LED_CONTROL
//p12911 201-04-16
#define PANTECH_7_LED_CONTROL
//#define PMIC_WRITE_TEST
#else
//20120625_kmh_sensor
#define LED_DISABLE
#endif
//#define PANTECH_DEBUG
#ifdef PANTECH_LED_CONTROL
#define DBG_ENABLE //LS5 TEAM SHS : 2011-10-25
#endif
#ifdef DBG_ENABLE
#define dbg(fmt, args...)   pr_debug("[LED] " fmt, ##args)
#define dbg_func_in()       dbg("[+] %s\n", __func__)
#define dbg_func_out()      dbg("[-] %s\n", __func__)
#define dbg_line()          dbg("[LINE] %d(%s)\n", __LINE__, __func__)
#else
#define dbg(fmt, args...)
#endif
/**
 * supported_leds - leds supported for each PMIC version
 * @version - version of PMIC
 * @supported - which leds are supported on version
 */

struct supported_leds {
	enum pm8xxx_version version;
	u32 supported;
};

static const struct supported_leds led_map[] = {
	LED_MAP(PM8XXX_VERSION_8058, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0),
	LED_MAP(PM8XXX_VERSION_8921, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0),
	LED_MAP(PM8XXX_VERSION_8018, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0),
	LED_MAP(PM8XXX_VERSION_8922, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1),
	LED_MAP(PM8XXX_VERSION_8038, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1),
};

/**
 * struct pm8xxx_led_data - internal led data structure
 * @led_classdev - led class device
 * @id - led index
 * @work - workqueue for led
 * @lock - to protect the transactions
 * @reg - cached value of led register
 * @pwm_dev - pointer to PWM device if LED is driven using PWM
 * @pwm_channel - PWM channel ID
 * @pwm_period_us - PWM period in micro seconds
 * @pwm_duty_cycles - struct that describes PWM duty cycles info
 */
struct pm8xxx_led_data {
	struct led_classdev	cdev;
	int			id;
	u8			reg;
	u8			wled_mod_ctrl_val;
	struct device		*dev;
	struct work_struct	work;
#ifdef PANTECH_7_LED_CONTROL	
	struct delayed_work	 led_work; 
    unsigned manual_on;
#endif
	struct mutex		lock;
	struct pwm_device	*pwm_dev;
	int			pwm_channel;
	u32			pwm_period_us;
	struct pm8xxx_pwm_duty_cycles *pwm_duty_cycles;
	struct wled_config_data *wled_cfg;
	int			max_current;
};
#ifdef PANTECH_7_LED_CONTROL
static struct pm8xxx_led_data pm_led_data[PM8XXX_ID_MAX];
#define DIMING_OPER_INTERVAL_TIME		100		//	diming operation interval time is 100msec
													// Set to 100 or 200 for debug 

//p12911 : shs 
#define DIMMING_MODE_ACTIVE		2
#define DIMMING_MODE_READY		1
#define DIMMING_MODE_NOTACTIVE	0

#define DIMMING_STATE_LIGHTUP 	2
#define DIMMING_STATE_DARK		1


#define DIMMING_UPDATE_LED	1
#define DIMMING_NO_UPDATE_LED	 0



//	DIMING Duration Time = DIMING_DATA_BASE_TIME + (diming data value)*DIMING_DATA_RESULTION;
#define DIMING_DATA_BASE_TIME				1400		
#define DIMING_DATA_RESOLUTION			100			


#define DIMING_LED_MASK			0x1F0 //20 STEP.

static int diming_duration_time;
static int diming_timer_count;
static unsigned int diming_oper_mode;			// 0: Nothing, 1: get dark, 2: light up, 

static unsigned int diming_set_state[3];


static int diming_end_value[3];

static void leds_diming_timer_register(struct pm8xxx_led_data *led);
static void leds_diming_timer_check(struct pm8xxx_led_data *led);
static long leds_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static void led_kp_set(struct pm8xxx_led_data *led, enum led_brightness value);
static void led_lc_set(struct pm8xxx_led_data *led, enum led_brightness value);
#endif

#ifdef PANTECH_LED_CONTROL
static struct file_operations leds_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = leds_fops_ioctl,
};
static struct miscdevice led_event = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "led_fops",
	.fops = &leds_fops,
};
static long leds_fops_ioctl(struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	int led_offset=arg+2;
	
#ifdef PANTECH_7_LED_CONTROL
	//ls5 team shs : modify here
	//++ p16092 -2014.01.11  for control lpg  
	struct	pm8xxx_led_data *led;
	int rc = 0; 
	int on_count = 0; //on Time
	int off_count = 0; //off Time
	int duty_ms_temp = 0;
	led = &pm_led_data[arg];
	//-- p16092 -2014.01.11  for control lpg
	
	dbg("leds_fops_ioctl : id = %lu, brightness = %d, led_offset =[%d] \n", arg, cmd,led_offset );

	switch(led_offset)
	{
 		case PM8XXX_ID_LED_0 :
		case PM8XXX_ID_LED_1 :
		case PM8XXX_ID_LED_2 :
			// ++p16092 -2014.01.11  for control lpg 
			if( (int)cmd > led->cdev.max_brightness)
				led->cdev.brightness = led->cdev.max_brightness;
			else{
				led->cdev.brightness = (int)cmd;
			}			
			led->pwm_duty_cycles->duty_pcts[0] = ((int)cmd*100)/led->cdev.max_brightness;
			led->pwm_duty_cycles->duty_pcts[1] = ((int)cmd*100)/led->cdev.max_brightness;
			led->pwm_duty_cycles->duty_pcts[2] = ((int)cmd*100)/led->cdev.max_brightness;
			led->pwm_duty_cycles->start_idx = 1;
			led->pwm_duty_cycles->num_duty_pcts = 2;
			led->pwm_duty_cycles->duty_ms = 100;
		
			if ( led->cdev.brightness > 0 ) {
				led_lc_set(led, led->cdev.max_brightness);
				rc = pm8xxx_pwm_lut_config(
						led->pwm_dev,
						led->pwm_period_us,
						led->pwm_duty_cycles->duty_pcts,
						led->pwm_duty_cycles->duty_ms,
						led->pwm_duty_cycles->start_idx,
						led->pwm_duty_cycles->num_duty_pcts,
						0, 0,
						PM8XXX_LED_PWM_FLAGS );		
				dbg("pm8xxx_pwm_lut_config: period=%u, num_duty_pcts=%d, duty_ms=%d, on=%d, off=%d\n", led->pwm_period_us, led->pwm_duty_cycles->num_duty_pcts, duty_ms_temp, on_count, off_count);
			}
			else {
  		        dbg("leds_fops_ioctl: id = %lu , brightness is setting up 0. \n",arg);
				led_lc_set(led, 0);
			}

			schedule_work(&led->work);
			// --p16092 -2014.01.11  for control lpg
			
			/* control led on menual mode.
	 			diming_set_state[0]=DIMMING_MODE_NOTACTIVE;
				diming_set_state[1]=DIMMING_MODE_NOTACTIVE;
				diming_set_state[2]=DIMMING_MODE_NOTACTIVE;	
				pm_led_data[arg].cdev.brightness = (int)cmd;
				led_lc_set(&pm_led_data[arg], (int)cmd);					
			*/
		break;		
		default:
		dbg("leds_fops_ioctl ERROR  led_offset => [%d]\n",led_offset);
		break;
		
	}
#endif

	return true;
}
#endif

#ifdef PANTECH_7_LED_CONTROL
static int prev_diming_level[3]={-1,-1,-1};
static void pmic8xxx_timer_work(struct work_struct *work)
{
	struct pm8xxx_led_data *led = 
		container_of(work, struct pm8xxx_led_data, led_work.work);
//	static unsigned long flags;
	int rc=0;
	u8 tmp=0;
	int diming_brightness[3];
	int diming_update=DIMMING_NO_UPDATE_LED;
	int diming_total_level=(diming_duration_time/DIMING_OPER_INTERVAL_TIME);
	int diming_interval=0;
	int i=0;
	mutex_lock(&led->lock);	
	do{
		if(diming_end_value[i]!=0){
			diming_interval=(diming_total_level/diming_end_value[i])+(diming_total_level%diming_end_value[i]);
			break;
		}
		i++;
	}while(i<3);
	
	if(diming_set_state[0]==DIMMING_MODE_NOTACTIVE && diming_set_state[1]==DIMMING_MODE_NOTACTIVE &&diming_set_state[2]==DIMMING_MODE_NOTACTIVE )
		goto error;

	if ( diming_oper_mode == DIMMING_STATE_DARK ) --diming_timer_count;
	
	if ( diming_oper_mode == DIMMING_STATE_LIGHTUP ) ++diming_timer_count;	

	// 	reach to max brightness, set to get-dark mode
	if ( (diming_duration_time/DIMING_OPER_INTERVAL_TIME) +diming_interval < diming_timer_count ) {
		diming_timer_count = diming_duration_time/DIMING_OPER_INTERVAL_TIME;
		diming_oper_mode=DIMMING_STATE_DARK;
	}

	//	reach to zero, set to light up mode
	if ( diming_interval > diming_timer_count ) {
		diming_timer_count = diming_interval;
		diming_oper_mode = DIMMING_STATE_LIGHTUP;
	}
	
	
	diming_brightness[0] = (diming_end_value[0]*diming_timer_count)/diming_total_level;
	diming_brightness[1] = (diming_end_value[1]*diming_timer_count)/diming_total_level;
	diming_brightness[2] = (diming_end_value[2]*diming_timer_count)/diming_total_level;
	if((prev_diming_level[0] !=diming_brightness[0]) ||(prev_diming_level[1] !=diming_brightness[1]) ||(prev_diming_level[2] !=diming_brightness[2]) )
	{
		do{
			if(diming_end_value[i] < diming_brightness[i]){
				diming_brightness[i]=diming_end_value[i];
				break;
			}
			i++;
		}while(i<3);		
		diming_update=DIMMING_UPDATE_LED;
	}
	prev_diming_level[0] =diming_brightness[0];
	prev_diming_level[1] =diming_brightness[1];	
	prev_diming_level[2] =diming_brightness[2];			
	dbg(" >> DIMING timer << cnt=%d, mode=%d level=%d [R:%d, %d][G:%d, %d][B:%d, %d]\n", 
		diming_timer_count, diming_oper_mode,diming_total_level,
		diming_end_value[0], diming_brightness[0],
		diming_end_value[1], diming_brightness[1],
		diming_end_value[2], diming_brightness[2]);	 
	if(diming_update){
			dbg(" >> WRITE I2C DIMING  <<\n");
	//	RED LED 
	tmp = diming_brightness[0] << PM8XXX_DRV_LED_CTRL_SHIFT ;
	
	rc = pm8xxx_writeb(led->dev->parent,	SSBI_REG_ADDR_LED_CTRL(0),tmp );	
	if (rc) {
		dev_err(led->cdev.dev, "can't set (%d) led value\n", led->id);
		goto error;
	}

	//	GREEN LED
	tmp = diming_brightness[1] << PM8XXX_DRV_LED_CTRL_SHIFT ;

	
	rc = pm8xxx_writeb(led->dev->parent,	SSBI_REG_ADDR_LED_CTRL(1),tmp );
	if (rc) {
		dev_err(led->cdev.dev, "can't set (%d) led value\n", led->id);
		goto error;
	}	

	//	BLUE LED
	tmp = diming_brightness[2] << PM8XXX_DRV_LED_CTRL_SHIFT ;
	
	rc = pm8xxx_writeb(led->dev->parent,	SSBI_REG_ADDR_LED_CTRL(2),tmp );
	if (rc) {
		dev_err(led->cdev.dev, "can't set (%d) led value\n", led->id);
		goto error;
			}	
	}
	//	repeat timer 	
	schedule_delayed_work(&led->led_work, 10);
error:
	mutex_unlock(&led->lock);
	return ;
}

static void leds_diming_timer_check(struct pm8xxx_led_data *led)
{
	dbg("leds_diming_timer_check : led->cdev.brightness = [%d] led->id = [%d]\n", led->cdev.brightness,led->id);	
	if ( (led->cdev.brightness & 0xF) > 0 ) {		
		diming_end_value[led->id-PM8XXX_ID_LED_0] = (led->cdev.brightness & 0x1F0) >> 0x4 ;			
		diming_set_state[led->id-PM8XXX_ID_LED_0] = DIMMING_MODE_READY; //p12911
		diming_timer_count = 0;
		diming_oper_mode = DIMMING_STATE_LIGHTUP;
		diming_duration_time = DIMING_DATA_BASE_TIME +
			((led->cdev.brightness & 0x0F))*DIMING_DATA_RESOLUTION;
	dbg("leds_diming_timer_check : diming_end_value[led->id-PM8XXX_ID_LED_0]=[%d]\n",(led->cdev.brightness & 0x1F0) >> 0x4);			
	}
	else
	{
		if(led->cdev.brightness ==0) //set the offline
		diming_end_value[led->id-PM8XXX_ID_LED_0] = 0;	
		diming_set_state[led->id-PM8XXX_ID_LED_0] = DIMMING_MODE_NOTACTIVE;
	dbg("leds_diming_timer_check : diming_set_state[led->id-PM8XXX_ID_LED_0]=[%d]\n",diming_set_state[led->id-PM8XXX_ID_LED_0]);					
	}
	dbg("leds_diming_timer_check : led->cdev.brightness = [%d] led->id = [%d] diming_set_state[%d]=[%d]  diming_end_value[led->id-PM8XXX_ID_LED_0]=[%d] \n", led->cdev.brightness,led->id,led->id-PM8XXX_ID_LED_0,diming_set_state[led->id-PM8XXX_ID_LED_0],diming_end_value[led->id-PM8XXX_ID_LED_0] );		
}

static void leds_diming_timer_register(struct pm8xxx_led_data *led)
{	
	dbg(" >> DIMING << [END VALUE] R=%d, G=%d, B=%d\n", diming_end_value[0], diming_end_value[1], diming_end_value[2]);
	dbg(" >> DIMING << [STATE VALUE] R=%d, G=%d, B=%d\n", diming_set_state[0], diming_set_state[1], diming_set_state[2]);
	if(diming_set_state[0]==DIMMING_MODE_ACTIVE || diming_set_state[1]==DIMMING_MODE_ACTIVE ||diming_set_state[2]==DIMMING_MODE_ACTIVE )
	diming_set_state[led->id-PM8XXX_ID_LED_0] =DIMMING_MODE_ACTIVE;	
	else
	{
	dbg(" >> DIMING << schdeule_delayed_work function call\n");		
	schedule_delayed_work(&led->led_work, 10);
	diming_set_state[led->id-PM8XXX_ID_LED_0] =DIMMING_MODE_ACTIVE;	
	}
}
#endif


static void led_kp_set(struct pm8xxx_led_data *led, enum led_brightness value)
{
	int rc;
	u8 level;

	level = (value << PM8XXX_DRV_KEYPAD_BL_SHIFT) &
				 PM8XXX_DRV_KEYPAD_BL_MASK;

	led->reg &= ~PM8XXX_DRV_KEYPAD_BL_MASK;
	led->reg |= level;

	rc = pm8xxx_writeb(led->dev->parent, SSBI_REG_ADDR_DRV_KEYPAD,
								led->reg);
	if (rc < 0)
		dev_err(led->cdev.dev,
			"can't set keypad backlight level rc=%d\n", rc);
}



static void led_lc_set(struct pm8xxx_led_data *led, enum led_brightness value)
{
	int rc=0; 
	int offset=0;
	u8 level;
#ifdef PMIC_WRITE_TEST
       u8 val1,val2,val3;
#endif

#ifdef ORIGIN
	level = (value << PM8XXX_DRV_LED_CTRL_SHIFT) &
				PM8XXX_DRV_LED_CTRL_MASK;

	offset = PM8XXX_LED_OFFSET(led->id);

	led->reg &= ~PM8XXX_DRV_LED_CTRL_MASK;
	led->reg |= level;
#else

	level = (value & 0x1F0) >> 0x4 ;
//p12911 delete
//	level = level*2;		

	level = (level << PM8XXX_DRV_LED_CTRL_SHIFT) &
				PM8XXX_DRV_LED_CTRL_MASK;
	offset = PM8XXX_LED_OFFSET(led->id);

	led->reg &= ~PM8XXX_DRV_LED_CTRL_MASK;
	led->reg |= level;	
#endif

//20120625_kmh_sensor
#ifndef LED_DISABLE
	rc = pm8xxx_writeb(led->dev->parent, SSBI_REG_ADDR_LED_CTRL(offset),
								led->reg);
#endif

	if (rc)
		dev_err(led->cdev.dev, "can't set (%d) led value rc=%d\n",
				led->id, rc);
				
//20120409_kmh_sensor
#ifdef PMIC_WRITE_TEST
       pr_debug("========LED->id=0x%x, offset=0x%x, value=%d, level : %d, led->reg=0x%x \n",led->id, offset,value,level, led->reg);	   
       pm8xxx_readb(led->dev->parent,	0x131, &val1);
       pm8xxx_readb(led->dev->parent,	0x132, &val2);
       pm8xxx_readb(led->dev->parent,	0x133, &val3);
       pr_debug("=======LED read val value : 0x%x, 0x%x, 0x%x\n",val1,val2,val3);
#endif
}

static void
led_flash_set(struct pm8xxx_led_data *led, enum led_brightness value)
{
	int rc;
	u8 level;
	u16 reg_addr;

	level = (value << PM8XXX_DRV_FLASH_SHIFT) &
				 PM8XXX_DRV_FLASH_MASK;

	led->reg &= ~PM8XXX_DRV_FLASH_MASK;
	led->reg |= level;

	if (led->id == PM8XXX_ID_FLASH_LED_0)
		reg_addr = SSBI_REG_ADDR_FLASH_DRV0;
	else
		reg_addr = SSBI_REG_ADDR_FLASH_DRV1;

	rc = pm8xxx_writeb(led->dev->parent, reg_addr, led->reg);
	if (rc < 0)
		dev_err(led->cdev.dev, "can't set flash led%d level rc=%d\n",
			 led->id, rc);
}

static int
led_wled_set(struct pm8xxx_led_data *led, enum led_brightness value)
{
	int rc, duty;
	u8 val, i, num_wled_strings;
#ifdef PANTECH_DEBUG
	dump_stack();
#endif

	if (value > WLED_MAX_LEVEL)
		value = WLED_MAX_LEVEL;

	if (value == 0) {
		rc = pm8xxx_writeb(led->dev->parent, WLED_MOD_CTRL_REG,
				WLED_BOOST_OFF);
		if (rc) {
			dev_err(led->dev->parent, "can't write wled ctrl config"
				" register rc=%d\n", rc);
			return rc;
		}
	} else {
		rc = pm8xxx_writeb(led->dev->parent, WLED_MOD_CTRL_REG,
				led->wled_mod_ctrl_val);
		if (rc) {
			dev_err(led->dev->parent, "can't write wled ctrl config"
				" register rc=%d\n", rc);
			return rc;
		}
	}

	duty = (WLED_MAX_DUTY_CYCLE * value) / WLED_MAX_LEVEL;

	num_wled_strings = led->wled_cfg->num_strings;

	/* program brightness control registers */
	for (i = 0; i < num_wled_strings; i++) {
		rc = pm8xxx_readb(led->dev->parent,
				WLED_BRIGHTNESS_CNTL_REG1(i), &val);
		if (rc) {
			dev_err(led->dev->parent, "can't read wled brightnes ctrl"
				" register1 rc=%d\n", rc);
			return rc;
		}

		val = (val & ~WLED_MAX_CURR_MASK) | (duty >> WLED_8_BIT_SHFT);
		rc = pm8xxx_writeb(led->dev->parent,
				WLED_BRIGHTNESS_CNTL_REG1(i), val);
		if (rc) {
			dev_err(led->dev->parent, "can't write wled brightness ctrl"
				" register1 rc=%d\n", rc);
			return rc;
		}

		val = duty & WLED_8_BIT_MASK;
		rc = pm8xxx_writeb(led->dev->parent,
				WLED_BRIGHTNESS_CNTL_REG2(i), val);
		if (rc) {
			dev_err(led->dev->parent, "can't write wled brightness ctrl"
				" register2 rc=%d\n", rc);
			return rc;
		}
	}
	rc = pm8xxx_readb(led->dev->parent, WLED_SYNC_REG, &val);
	if (rc) {
		dev_err(led->dev->parent,
			"can't read wled sync register rc=%d\n", rc);
		return rc;
	}
	/* sync */
	val &= WLED_SYNC_MASK;
	val |= WLED_SYNC_VAL;
	rc = pm8xxx_writeb(led->dev->parent, WLED_SYNC_REG, val);
	if (rc) {
		dev_err(led->dev->parent,
			"can't read wled sync register rc=%d\n", rc);
		return rc;
	}
	val &= WLED_SYNC_MASK;
	val |= WLED_SYNC_RESET_VAL;
	rc = pm8xxx_writeb(led->dev->parent, WLED_SYNC_REG, val);
	if (rc) {
		dev_err(led->dev->parent,
			"can't read wled sync register rc=%d\n", rc);
		return rc;
	}
	return 0;
}

static void wled_dump_regs(struct pm8xxx_led_data *led)
{
	int i;
	u8 val;
#ifdef PANTECH_DEBUG
	dump_stack();
#endif
	for (i = 1; i < 17; i++) {
		pm8xxx_readb(led->dev->parent,
				SSBI_REG_ADDR_WLED_CTRL(i), &val);
		pr_debug("WLED_CTRL_%d = 0x%x\n", i, val);
	}
}

static void
led_rgb_write(struct pm8xxx_led_data *led, u16 addr, enum led_brightness value)
{
	int rc;
	u8 val, mask;

	if (led->id != PM8XXX_ID_RGB_LED_BLUE &&
		led->id != PM8XXX_ID_RGB_LED_RED &&
		led->id != PM8XXX_ID_RGB_LED_GREEN)
		return;

	rc = pm8xxx_readb(led->dev->parent, addr, &val);
	if (rc) {
		dev_err(led->cdev.dev, "can't read rgb ctrl register rc=%d\n",
							rc);
		return;
	}

	switch (led->id) {
	case PM8XXX_ID_RGB_LED_RED:
		mask = PM8XXX_DRV_RGB_RED_LED;
		break;
	case PM8XXX_ID_RGB_LED_GREEN:
		mask = PM8XXX_DRV_RGB_GREEN_LED;
		break;
	case PM8XXX_ID_RGB_LED_BLUE:
		mask = PM8XXX_DRV_RGB_BLUE_LED;
		break;
	default:
		return;
	}

	if (value)
		val |= mask;
	else
		val &= ~mask;

	rc = pm8xxx_writeb(led->dev->parent, addr, val);
	if (rc < 0)
		dev_err(led->cdev.dev, "can't set rgb led %d level rc=%d\n",
			 led->id, rc);
}

static void
led_rgb_set(struct pm8xxx_led_data *led, enum led_brightness value)
{
	if (value) {
		led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1, value);
		led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL2, value);
	} else {
		led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL2, value);
		led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1, value);
	}
}

static int pm8xxx_led_pwm_work(struct pm8xxx_led_data *led)
{
	int duty_us;
	int rc = 0;
#ifdef PANTECH_DEBUG
	dump_stack();
#endif
	if (led->pwm_duty_cycles == NULL) {
		duty_us = (led->pwm_period_us * led->cdev.brightness) /
								LED_FULL;
		rc = pwm_config(led->pwm_dev, duty_us, led->pwm_period_us);
		if (led->cdev.brightness) {
			led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1,
				led->cdev.brightness);
			rc = pwm_enable(led->pwm_dev);
		} else {
			pwm_disable(led->pwm_dev);
			led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1,
				led->cdev.brightness);
		}
	} else {
		//++ ++140205:LED_BLINK_CONTROL(p16092)  - 2012.11.21 for MARUKO LED (p11309)
#ifdef PANTECH_7_LED_CONTROL
//++ 140520: p16092 Add pwm disable and returning routine when the brightness is 0.	
      pwm_disable(led->pwm_dev);
      pm8xxx_pwm_lut_enable(led->pwm_dev, 0);
        if (led->cdev.brightness == 0) {
          return rc;
      }
      if(led->manual_on==1){
        duty_us = (led->pwm_period_us * led->cdev.brightness) / LED_FULL;
        rc = pwm_config(led->pwm_dev, duty_us, led->pwm_period_us);
        rc = pwm_enable(led->pwm_dev);
      }else{
//-- 140520: p16092 Add pwm disable and returning routine when the brightness is 0.	
        rc = pm8xxx_pwm_lut_enable(led->pwm_dev, led->cdev.brightness);
        led_lc_set(led, led->cdev.brightness);
//++ 140520: p16092		
      }
//-- 140520: p16092
#else	
				if (led->cdev.brightness)
					led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1,
						led->cdev.brightness);
				rc = pm8xxx_pwm_lut_enable(led->pwm_dev, led->cdev.brightness);
				if (!led->cdev.brightness)
					led_rgb_write(led, SSBI_REG_ADDR_RGB_CNTL1,
						led->cdev.brightness);
#endif
		//--140205:LED_BLINK_CONTROL(p16092)
	}

	return rc;
}

static void __pm8xxx_led_work(struct pm8xxx_led_data *led,
					enum led_brightness level)
{
	int rc;

	mutex_lock(&led->lock);
#ifdef PANTECH_DEBUG
	dump_stack();
#endif

	switch (led->id) {
	case PM8XXX_ID_LED_KB_LIGHT:
		led_kp_set(led, level);
		break;

	//RED
	case PM8XXX_ID_LED_0:
#ifdef PANTECH_7_LED_CONTROL
		leds_diming_timer_check(led);
		if ( diming_set_state[led->id-PM8XXX_ID_LED_0]  == DIMMING_MODE_NOTACTIVE ) led_lc_set(led, level);
		else leds_diming_timer_register(led);
#else
		led_lc_set(led, level);		
#endif
		break;
	//GREEN
	case PM8XXX_ID_LED_1:
#ifdef PANTECH_7_LED_CONTROL
		leds_diming_timer_check(led);
		if ( diming_set_state[led->id-PM8XXX_ID_LED_0]  == DIMMING_MODE_NOTACTIVE ) led_lc_set(led, level);
		else leds_diming_timer_register(led);
#else
		led_lc_set(led, level);		
#endif		
		break;
	//BLUE
	case PM8XXX_ID_LED_2:
#ifdef PANTECH_7_LED_CONTROL
		leds_diming_timer_check(led);
		if ( diming_set_state[led->id-PM8XXX_ID_LED_0]  == DIMMING_MODE_NOTACTIVE ) led_lc_set(led, level);
		else leds_diming_timer_register(led);
#else
		led_lc_set(led, level);
#endif		
		break;
	case PM8XXX_ID_FLASH_LED_0:
	case PM8XXX_ID_FLASH_LED_1:
		led_flash_set(led, level);
		break;
	case PM8XXX_ID_WLED:
		rc = led_wled_set(led, level);
		if (rc < 0)
			pr_err("wled brightness set failed %d\n", rc);
		break;
	case PM8XXX_ID_RGB_LED_RED:
	case PM8XXX_ID_RGB_LED_GREEN:
	case PM8XXX_ID_RGB_LED_BLUE:
		led_rgb_set(led, level);
		break;
	default:
		dev_err(led->cdev.dev, "unknown led id %d", led->id);
		break;
	}

	mutex_unlock(&led->lock);
}

static void pm8xxx_led_work(struct work_struct *work)
{
	int rc;

	struct pm8xxx_led_data *led = container_of(work,
					 struct pm8xxx_led_data, work);
#ifdef PANTECH_DEBUG
	dump_stack();
#endif
	if (led->pwm_dev == NULL) {
		__pm8xxx_led_work(led, led->cdev.brightness);
	} else {
		rc = pm8xxx_led_pwm_work(led);
		if (rc)
			pr_err("could not configure PWM mode for LED:%d\n",
								led->id);
	}
}

static void pm8xxx_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct	pm8xxx_led_data *led;

	led = container_of(led_cdev, struct pm8xxx_led_data, cdev);

	if (value < LED_OFF || value > led->cdev.max_brightness) {
		dev_err(led->cdev.dev, "Invalid brightness value exceeds");
		return;
	}
#ifdef PANTECH_7_LED_CONTROL
	if ( led->id >= PM8XXX_ID_LED_0 && led->id <= PM8XXX_ID_LED_2) 
		led->cdev.brightness = value;
	dbg("[LED] pm8xxx_led_set led->id = [%d], brigthness = [%d]\n", led->id,value);
//++ 140520: p16092	
    led->manual_on=1;
//-- 140520: p16092	
#endif
	led->cdev.brightness = value;
	schedule_work(&led->work);
}

//++ p16092 add -140122:LED_BLINK_CONTROL  - 2012.09.09 for using LPG on PM8921(p11309) 
static int pm8xxx_led_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on, unsigned long *delay_off, int led_percent_level)
{
	int i=0;
	int rc = 0;
	int on_count = 0;
	int off_count = 0;
	int duty_ms_temp = 0;

//++ 2012.11.21 for LPG Error fix. LPG0 is disable. 63 usable. start_idx is from 1
/*
 * Note: There is a bug in LPG module that results in incorrect
 * behavior of pattern when LUT index 0 is used. So effectively
 * there are 63 usable LUT entries.
 */
	int num_duty_pcts_temp = 1;
//-- 

//++  2012.09.30 for calculating duration time for PMIC LPG
	int min_duation_diff_index=16;
	int min_duation_diff_value=1024;
	const int duty_msec_const[16] = { 0, 1, 2, 3, 4, 6, 8, 16, 18, 24, 32, 36, 64, 128, 256, 512 };
//-- 

	struct	pm8xxx_led_data *led;
	led = container_of(led_cdev, struct pm8xxx_led_data, cdev);	
#ifdef PANTECH_7_LED_CONTROL
//++ 140520: p16092		
    led->manual_on=0;
//-- 140520: p16092	
#endif
	dbg("pm8xxx_led_blink_set: name=%s(%d), on=%lu, off=%lu, pcts=%d\n", led_cdev->name, led->id, *delay_on, *delay_off, led_percent_level);

	if ( *delay_off == 0 && *delay_on != 0) {
		// always on
		led->pwm_duty_cycles->duty_pcts[0] = led_percent_level;
		led->pwm_duty_cycles->duty_pcts[1] = led_percent_level;
		led->pwm_duty_cycles->duty_pcts[2] = led_percent_level;
		led->pwm_duty_cycles->start_idx = 1;
		led->pwm_duty_cycles->num_duty_pcts = 2;
		led->pwm_duty_cycles->duty_ms = 100;
	}	
	else if ( *delay_on == 0 ) {
		//always off
		led->cdev.brightness = 0;	
	}
	else {
		if ( *delay_on < *delay_off) {			

//++  - 2012.09.30 for onms over 500msec++ p16092 
//++  - 2012.09.30 for calculating duration time for PMIC LPG++
			if ( (*delay_on) > duty_msec_const[15] ) {
				on_count = (*delay_on)/duty_msec_const[15];
				if ( (*delay_on)%duty_msec_const[15] > (duty_msec_const[15]/2) ) on_count++;
				duty_ms_temp = duty_msec_const[15];
			}
			else {
				on_count = 1;
				duty_ms_temp = *delay_on;
				for (i=0; i<16; i++ ) {
					if ( duty_ms_temp >= duty_msec_const[i] ) {
						if ( (duty_ms_temp - duty_msec_const[i]) < min_duation_diff_value ) {
							min_duation_diff_index = i;
							min_duation_diff_value = duty_ms_temp - duty_msec_const[i];
						}
					}
					else {
						if ( (duty_msec_const[i] - duty_ms_temp) < min_duation_diff_value ) {
							min_duation_diff_index = i;
							min_duation_diff_value = duty_msec_const[i] - duty_ms_temp;
						}
					}				
				}
				duty_ms_temp = duty_msec_const[min_duation_diff_index];
			}
			
			off_count = (*delay_off)/duty_ms_temp;
			if ( (*delay_off)%duty_ms_temp > (duty_ms_temp/2)) off_count++;

			if ( (on_count + off_count) > (PM_PWM_LUT_SIZE-1) ) off_count = (PM_PWM_LUT_SIZE-1) - on_count;
		}
		else {

			if ( (*delay_off) > duty_msec_const[15] ) {
				off_count = (*delay_off)/duty_msec_const[15];
				if ( (*delay_off)%duty_msec_const[15] > (duty_msec_const[15]/2) ) off_count++;
				duty_ms_temp = duty_msec_const[15];
			}
			else {
				off_count = 1;
				duty_ms_temp = *delay_off;
				for (i=0; i<16; i++ ) {
					if ( duty_ms_temp >= duty_msec_const[i] ) {
						if ( (duty_ms_temp - duty_msec_const[i]) < min_duation_diff_value ) {
							min_duation_diff_index = i;
							min_duation_diff_value = duty_ms_temp - duty_msec_const[i];
						}
					}
					else {
						if ( (duty_msec_const[i] - duty_ms_temp) < min_duation_diff_value ) {
							min_duation_diff_index = i;
							min_duation_diff_value = duty_msec_const[i] - duty_ms_temp;
						}
					}				
				}
				duty_ms_temp = duty_msec_const[min_duation_diff_index];
			}		

			on_count = (*delay_on)/duty_ms_temp;
			if ( (*delay_on)%duty_ms_temp > (duty_ms_temp/2)) on_count++;

			if ( (on_count + off_count) > (PM_PWM_LUT_SIZE-1) ) on_count = (PM_PWM_LUT_SIZE-1) - off_count;
		}			
//-- 

		for (i=0; i<on_count; i++) {
			led->pwm_duty_cycles->duty_pcts[num_duty_pcts_temp] = led_percent_level;
			num_duty_pcts_temp++;
		}
		for (i=0; i<off_count; i++) {
			led->pwm_duty_cycles->duty_pcts[num_duty_pcts_temp] = 0;
			num_duty_pcts_temp++;
		}

		led->pwm_period_us = 1000;
		led->pwm_duty_cycles->start_idx = 1;
		led->pwm_duty_cycles->num_duty_pcts = num_duty_pcts_temp;
		led->pwm_duty_cycles->duty_ms = duty_ms_temp;
	}

	if ( led->cdev.brightness > 0 ) {

		led_lc_set(led, led->cdev.max_brightness);
		rc = pm8xxx_pwm_lut_config(
				led->pwm_dev,
				led->pwm_period_us,
				led->pwm_duty_cycles->duty_pcts,
				led->pwm_duty_cycles->duty_ms,
				led->pwm_duty_cycles->start_idx,
				led->pwm_duty_cycles->num_duty_pcts,
				0, 0,
				PM8XXX_LED_PWM_FLAGS );		
		dbg("pm8xxx_pwm_lut_config: period=%u, num_duty_pcts=%d, duty_ms=%d, on=%d, off=%d\n", led->pwm_period_us, num_duty_pcts_temp, duty_ms_temp, on_count, off_count);
	}
	else {
		led_lc_set(led, 0);
	}

	schedule_work(&led->work);
	return rc;
}
//++ 

static int pm8xxx_set_led_mode_and_max_brightness(struct pm8xxx_led_data *led,
		enum pm8xxx_led_modes led_mode, int max_current)
{
	switch (led->id) {
	case PM8XXX_ID_LED_0:
	case PM8XXX_ID_LED_1:
	case PM8XXX_ID_LED_2:
#ifdef PANTECH_7_LED_CONTROL		
		led->cdev.max_brightness =0x1ff;
		led->reg = led_mode;
#else
		led->cdev.max_brightness = max_current /
						PM8XXX_ID_LED_CURRENT_FACTOR;
		if (led->cdev.max_brightness > MAX_LC_LED_BRIGHTNESS)
			led->cdev.max_brightness = MAX_LC_LED_BRIGHTNESS; 
		led->reg = led_mode;
#endif
		break;
	case PM8XXX_ID_LED_KB_LIGHT:
	case PM8XXX_ID_FLASH_LED_0:
	case PM8XXX_ID_FLASH_LED_1:
		led->cdev.max_brightness = max_current /
						PM8XXX_ID_FLASH_CURRENT_FACTOR;
		if (led->cdev.max_brightness > MAX_FLASH_BRIGHTNESS)
			led->cdev.max_brightness = MAX_FLASH_BRIGHTNESS;

		switch (led_mode) {
		case PM8XXX_LED_MODE_PWM1:
		case PM8XXX_LED_MODE_PWM2:
		case PM8XXX_LED_MODE_PWM3:
			led->reg = PM8XXX_FLASH_MODE_PWM;
			break;
		case PM8XXX_LED_MODE_DTEST1:
			led->reg = PM8XXX_FLASH_MODE_DBUS1;
			break;
		case PM8XXX_LED_MODE_DTEST2:
			led->reg = PM8XXX_FLASH_MODE_DBUS2;
			break;
		default:
			led->reg = PM8XXX_LED_MODE_MANUAL;
			break;
		}
		break;
	case PM8XXX_ID_WLED:
		led->cdev.max_brightness = WLED_MAX_LEVEL;
		break;
	case PM8XXX_ID_RGB_LED_RED:
	case PM8XXX_ID_RGB_LED_GREEN:
	case PM8XXX_ID_RGB_LED_BLUE:
		led->cdev.max_brightness = LED_FULL;
		break;
	default:
		dev_err(led->cdev.dev, "LED Id is invalid");
		return -EINVAL;
	}

	return 0;
}

static enum led_brightness pm8xxx_led_get(struct led_classdev *led_cdev)
{
	struct pm8xxx_led_data *led;

	led = container_of(led_cdev, struct pm8xxx_led_data, cdev);

	return led->cdev.brightness;
}

static int __devinit init_wled(struct pm8xxx_led_data *led)
{
	int rc, i;
	u8 val, num_wled_strings;

	num_wled_strings = led->wled_cfg->num_strings;

	/* program over voltage protection threshold */
	if (led->wled_cfg->ovp_val > WLED_OVP_37V) {
		dev_err(led->dev->parent, "Invalid ovp value");
		return -EINVAL;
	}

	rc = pm8xxx_readb(led->dev->parent, WLED_OVP_CFG_REG, &val);
	if (rc) {
		dev_err(led->dev->parent, "can't read wled ovp config"
			" register rc=%d\n", rc);
		return rc;
	}

	val = (val & ~WLED_OVP_VAL_MASK) |
		(led->wled_cfg->ovp_val << WLED_OVP_VAL_BIT_SHFT);

	rc = pm8xxx_writeb(led->dev->parent, WLED_OVP_CFG_REG, val);
	if (rc) {
		dev_err(led->dev->parent, "can't write wled ovp config"
			" register rc=%d\n", rc);
		return rc;
	}

	/* program current boost limit and output feedback*/
	if (led->wled_cfg->boost_curr_lim > WLED_CURR_LIMIT_1680mA) {
		dev_err(led->dev->parent, "Invalid boost current limit");
		return -EINVAL;
	}

	rc = pm8xxx_readb(led->dev->parent, WLED_BOOST_CFG_REG, &val);
	if (rc) {
		dev_err(led->dev->parent, "can't read wled boost config"
			" register rc=%d\n", rc);
		return rc;
	}

	val = (val & ~WLED_BOOST_LIMIT_MASK) |
		(led->wled_cfg->boost_curr_lim << WLED_BOOST_LIMIT_BIT_SHFT);

	val = (val & ~WLED_OP_FDBCK_MASK) |
		(led->wled_cfg->op_fdbck << WLED_OP_FDBCK_BIT_SHFT);

	rc = pm8xxx_writeb(led->dev->parent, WLED_BOOST_CFG_REG, val);
	if (rc) {
		dev_err(led->dev->parent, "can't write wled boost config"
			" register rc=%d\n", rc);
		return rc;
	}

	/* program high pole capacitance */
	if (led->wled_cfg->cp_select > WLED_CP_SELECT_MAX) {
		dev_err(led->dev->parent, "Invalid pole capacitance");
		return -EINVAL;
	}

	rc = pm8xxx_readb(led->dev->parent, WLED_HIGH_POLE_CAP_REG, &val);
	if (rc) {
		dev_err(led->dev->parent, "can't read wled high pole"
			" capacitance register rc=%d\n", rc);
		return rc;
	}

	val = (val & ~WLED_CP_SELECT_MASK) | led->wled_cfg->cp_select;

	rc = pm8xxx_writeb(led->dev->parent, WLED_HIGH_POLE_CAP_REG, val);
	if (rc) {
		dev_err(led->dev->parent, "can't write wled high pole"
			" capacitance register rc=%d\n", rc);
		return rc;
	}

	/* program activation delay and maximum current */
	for (i = 0; i < num_wled_strings; i++) {
		rc = pm8xxx_readb(led->dev->parent,
				WLED_MAX_CURR_CFG_REG(i), &val);
		if (rc) {
			dev_err(led->dev->parent, "can't read wled max current"
				" config register rc=%d\n", rc);
			return rc;
		}

		if ((led->wled_cfg->ctrl_delay_us % WLED_CTL_DLY_STEP) ||
			(led->wled_cfg->ctrl_delay_us > WLED_CTL_DLY_MAX)) {
			dev_err(led->dev->parent, "Invalid control delay\n");
			return rc;
		}

		val = val / WLED_CTL_DLY_STEP;
		val = (val & ~WLED_CTL_DLY_MASK) |
			(led->wled_cfg->ctrl_delay_us << WLED_CTL_DLY_BIT_SHFT);

		if ((led->max_current > WLED_MAX_CURR)) {
			dev_err(led->dev->parent, "Invalid max current\n");
			return -EINVAL;
		}

		val = (val & ~WLED_MAX_CURR_MASK) | led->max_current;

		rc = pm8xxx_writeb(led->dev->parent,
				WLED_MAX_CURR_CFG_REG(i), val);
		if (rc) {
			dev_err(led->dev->parent, "can't write wled max current"
				" config register rc=%d\n", rc);
			return rc;
		}
	}

	if (led->wled_cfg->cabc_en) {
		rc = pm8xxx_readb(led->dev->parent, WLED_SYNC_REG, &val);
		if (rc) {
			dev_err(led->dev->parent,
				"can't read cabc register rc=%d\n", rc);
			return rc;
		}

		switch (num_wled_strings) {
		case ONE_WLED_STRING:
			val |= (WLED_CABC_ONE_STRING << WLED_CABC_SHIFT);
			break;
		case TWO_WLED_STRINGS:
			val |= (WLED_CABC_TWO_STRING << WLED_CABC_SHIFT);
			break;
		case THREE_WLED_STRINGS:
			val |= (WLED_CABC_THREE_STRING << WLED_CABC_SHIFT);
			break;
		default:
			break;
		}

		rc = pm8xxx_writeb(led->dev->parent, WLED_SYNC_REG, val);
		if (rc) {
			dev_err(led->dev->parent,
				"can't write to enable cabc rc=%d\n", rc);
			return rc;
		}
	}

	/* program digital module generator, cs out and enable the module */
	rc = pm8xxx_readb(led->dev->parent, WLED_MOD_CTRL_REG, &val);
	if (rc) {
		dev_err(led->dev->parent, "can't read wled module ctrl"
			" register rc=%d\n", rc);
		return rc;
	}

	if (led->wled_cfg->dig_mod_gen_en)
		val |= WLED_DIG_MOD_GEN_MASK;

	if (led->wled_cfg->cs_out_en)
		val |= WLED_CS_OUT_MASK;

	val |= WLED_EN_MASK;

	rc = pm8xxx_writeb(led->dev->parent, WLED_MOD_CTRL_REG, val);
	if (rc) {
		dev_err(led->dev->parent, "can't write wled module ctrl"
			" register rc=%d\n", rc);
		return rc;
	}
	led->wled_mod_ctrl_val = val;

	/* dump wled registers */
	wled_dump_regs(led);

	return 0;
}

static int __devinit get_init_value(struct pm8xxx_led_data *led, u8 *val)
{
	int rc, offset;
	u16 addr;

	switch (led->id) {
	case PM8XXX_ID_LED_KB_LIGHT:
		addr = SSBI_REG_ADDR_DRV_KEYPAD;
		break;
	case PM8XXX_ID_LED_0:
	case PM8XXX_ID_LED_1:
	case PM8XXX_ID_LED_2:
		offset = PM8XXX_LED_OFFSET(led->id);
		addr = SSBI_REG_ADDR_LED_CTRL(offset);
		break;
	case PM8XXX_ID_FLASH_LED_0:
		addr = SSBI_REG_ADDR_FLASH_DRV0;
		break;
	case PM8XXX_ID_FLASH_LED_1:
		addr = SSBI_REG_ADDR_FLASH_DRV1;
		break;
	case PM8XXX_ID_WLED:
		rc = init_wled(led);
		if (rc)
			dev_err(led->cdev.dev, "can't initialize wled rc=%d\n",
								rc);
		return rc;
	case PM8XXX_ID_RGB_LED_RED:
	case PM8XXX_ID_RGB_LED_GREEN:
	case PM8XXX_ID_RGB_LED_BLUE:
		addr = SSBI_REG_ADDR_RGB_CNTL1;
		break;
	default:
		dev_err(led->cdev.dev, "unknown led id %d", led->id);
		return -EINVAL;
	}

	rc = pm8xxx_readb(led->dev->parent, addr, val);
	if (rc)
		dev_err(led->cdev.dev, "can't get led(%d) level rc=%d\n",
							led->id, rc);

	return rc;
}

static int pm8xxx_led_pwm_configure(struct pm8xxx_led_data *led)
{
	int start_idx, idx_len, duty_us, rc;

	led->pwm_dev = pwm_request(led->pwm_channel,
					led->cdev.name);

	if (IS_ERR_OR_NULL(led->pwm_dev)) {
		pr_err("could not acquire PWM Channel %d, "
			"error %ld\n", led->pwm_channel,
			PTR_ERR(led->pwm_dev));
		led->pwm_dev = NULL;
		return -ENODEV;
	}

	if (led->pwm_duty_cycles != NULL) {
		start_idx = led->pwm_duty_cycles->start_idx;
		idx_len = led->pwm_duty_cycles->num_duty_pcts;

		if (idx_len >= PM_PWM_LUT_SIZE && start_idx) {
			pr_err("Wrong LUT size or index\n");
			return -EINVAL;
		}
		if ((start_idx + idx_len) > PM_PWM_LUT_SIZE) {
			pr_err("Exceed LUT limit\n");
			return -EINVAL;
		}

		rc = pm8xxx_pwm_lut_config(led->pwm_dev, led->pwm_period_us,
				led->pwm_duty_cycles->duty_pcts,
				led->pwm_duty_cycles->duty_ms,
				start_idx, idx_len, 0, 0,
				PM8XXX_LED_PWM_FLAGS);
	} else {
		duty_us = led->pwm_period_us;
		rc = pwm_config(led->pwm_dev, duty_us, led->pwm_period_us);
	}

	return rc;
}


static int __devinit pm8xxx_led_probe(struct platform_device *pdev)
{
	const struct pm8xxx_led_platform_data *pdata = pdev->dev.platform_data;
	const struct led_platform_data *pcore_data;
	struct led_info *curr_led;
	struct pm8xxx_led_data *led, *led_dat;
	struct pm8xxx_led_config *led_cfg;
	enum pm8xxx_version version;
	bool found = false;
	int rc, i, j;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data not supplied\n");
		return -EINVAL;
	}

	pcore_data = pdata->led_core;

	if (pcore_data->num_leds != pdata->num_configs) {
		dev_err(&pdev->dev, "#no. of led configs and #no. of led"
				"entries are not equal\n");
		return -EINVAL;
	}
#ifdef PANTECH_7_LED_CONTROL
	led=&pm_led_data[0];
//++ 140520: p16092	
    led->manual_on=0;
//-- 140520: p16092	
#else
	led = kcalloc(pcore_data->num_leds, sizeof(*led), GFP_KERNEL);
	if (led == NULL) {
		dev_err(&pdev->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}
#endif

	for (i = 0; i < pcore_data->num_leds; i++) {
		curr_led	= &pcore_data->leds[i];
#ifdef 	PANTECH_7_LED_CONTROL	
		led_dat		= &pm_led_data[i];
#else
		led_dat		= &led[i];
#endif
		led_cfg		= &pdata->configs[i];

		led_dat->id     = led_cfg->id;
		led_dat->pwm_channel = led_cfg->pwm_channel;
		led_dat->pwm_period_us = led_cfg->pwm_period_us;
		led_dat->pwm_duty_cycles = led_cfg->pwm_duty_cycles;
		led_dat->wled_cfg = led_cfg->wled_cfg;
		led_dat->max_current = led_cfg->max_current;

		if (!((led_dat->id >= PM8XXX_ID_LED_KB_LIGHT) &&
				(led_dat->id < PM8XXX_ID_MAX))) {
			dev_err(&pdev->dev, "invalid LED ID(%d) specified\n",
				led_dat->id);
			rc = -EINVAL;
			goto fail_id_check;

		}

		found = false;
		version = pm8xxx_get_version(pdev->dev.parent);
		for (j = 0; j < ARRAY_SIZE(led_map); j++) {
			if (version == led_map[j].version
			&& (led_map[j].supported & (1 << led_dat->id))) {
				found = true;
				break;
			}
		}

		if (!found) {
			dev_err(&pdev->dev, "invalid LED ID(%d) specified\n",
				led_dat->id);
			rc = -EINVAL;
			goto fail_id_check;
		}

		led_dat->cdev.name		= curr_led->name;
		led_dat->cdev.default_trigger   = curr_led->default_trigger;
		led_dat->cdev.brightness_set    = pm8xxx_led_set;
		led_dat->cdev.brightness_get    = pm8xxx_led_get;
//++ 140122:LED_BLINK_CONTROL 2012.09.09 for using LPG on PM8921 
		if ( led_cfg->mode >= PM8XXX_LED_MODE_PWM1 && led_cfg->mode <= PM8XXX_LED_MODE_PWM3)
			led_dat->cdev.blink_set		= pm8xxx_led_blink_set;
//-- 140122:LED_BLINK_CONTROL
		led_dat->cdev.brightness	= LED_OFF;
		led_dat->cdev.flags		= curr_led->flags;
		led_dat->dev			= &pdev->dev;

		rc =  get_init_value(led_dat, &led_dat->reg);
		if (rc < 0)
			goto fail_id_check;

		rc = pm8xxx_set_led_mode_and_max_brightness(led_dat,
					led_cfg->mode, led_cfg->max_current);
		if (rc < 0)
			goto fail_id_check;

		mutex_init(&led_dat->lock);
		INIT_WORK(&led_dat->work, pm8xxx_led_work);
#ifdef PANTECH_7_LED_CONTROL
	diming_set_state[0] = DIMMING_MODE_NOTACTIVE;
	diming_set_state[1] = DIMMING_MODE_NOTACTIVE;
	diming_set_state[2] = DIMMING_MODE_NOTACTIVE;
	INIT_DELAYED_WORK(&led_dat->led_work, pmic8xxx_timer_work);		
#endif

		rc = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (rc) {
			dev_err(&pdev->dev, "unable to register led %d,rc=%d\n",
						 led_dat->id, rc);
			goto fail_id_check;
		}

		/* configure default state */
		if (led_cfg->default_state)
			led->cdev.brightness = led_dat->cdev.max_brightness;
		else
			led->cdev.brightness = LED_OFF;

		if (led_cfg->mode != PM8XXX_LED_MODE_MANUAL) {
			if (led_dat->id == PM8XXX_ID_RGB_LED_RED ||
				led_dat->id == PM8XXX_ID_RGB_LED_GREEN ||
				led_dat->id == PM8XXX_ID_RGB_LED_BLUE)
				__pm8xxx_led_work(led_dat, 0);
			else
				__pm8xxx_led_work(led_dat,
					led_dat->cdev.max_brightness);

			if (led_dat->pwm_channel != -1) {
				led_dat->cdev.max_brightness = LED_FULL;
				rc = pm8xxx_led_pwm_configure(led_dat);
				if (rc) {
					dev_err(&pdev->dev, "failed to "
					"configure LED, error: %d\n", rc);
					goto fail_id_check;
				}
			schedule_work(&led->work);
			}
		} else {
			__pm8xxx_led_work(led_dat, led->cdev.brightness);
		}
	}

	platform_set_drvdata(pdev, led);

#ifdef PANTECH_LED_CONTROL	
	// p12911 Added TestMenu
	misc_register(&led_event);
#endif
	return 0;

fail_id_check:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			mutex_destroy(&led[i].lock);
			led_classdev_unregister(&led[i].cdev);
			if (led[i].pwm_dev != NULL)
				pwm_free(led[i].pwm_dev);
		}
	}
	kfree(led);
	return rc;
}

static int __devexit pm8xxx_led_remove(struct platform_device *pdev)
{
	int i;
	const struct led_platform_data *pdata =
				pdev->dev.platform_data;
	struct pm8xxx_led_data *led = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		cancel_work_sync(&led[i].work);
		mutex_destroy(&led[i].lock);
		led_classdev_unregister(&led[i].cdev);
		if (led[i].pwm_dev != NULL)
			pwm_free(led[i].pwm_dev);
	}

	kfree(led);

	return 0;
}

#if 0//def PANTECH_7_LED_CONTROL
static int pm8xxx_suspend(struct device *dev)
{
	dbg("pm8xxx_suspend enter\n");			
	diming_set_state[0]=DIMMING_MODE_NOTACTIVE;
	diming_set_state[1]=DIMMING_MODE_NOTACTIVE;
	diming_set_state[2]=DIMMING_MODE_NOTACTIVE;
	led_lc_set(&pm_led_data[0], 0);		
	led_lc_set(&pm_led_data[1], 0);			
	led_lc_set(&pm_led_data[2], 0);		
	dbg("pm8xxx_suspend exit\n");		
	return 0;
}

static int pm8xxx_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops pm8xxx_pm_ops = {
	.suspend = pm8xxx_suspend,
	.resume = pm8xxx_resume,
};
#endif

static struct platform_driver pm8xxx_led_driver = {
	.probe		= pm8xxx_led_probe,
	.remove		= __devexit_p(pm8xxx_led_remove),
	.driver		= {
		.name	= PM8XXX_LEDS_DEV_NAME,
		.owner	= THIS_MODULE,
#if 0//def PANTECH_7_LED_CONTROL				
		.pm	= &pm8xxx_pm_ops,		
#endif
	},
};

static int __init pm8xxx_led_init(void)
{
	return platform_driver_register(&pm8xxx_led_driver);
}
subsys_initcall(pm8xxx_led_init);

static void __exit pm8xxx_led_exit(void)
{
	platform_driver_unregister(&pm8xxx_led_driver);
}
module_exit(pm8xxx_led_exit);

MODULE_DESCRIPTION("PM8XXX LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:pm8xxx-led");
