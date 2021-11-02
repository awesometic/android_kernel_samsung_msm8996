/*
* Copyright (c) 2014 JY Kim, jy.kim@maximintegrated.com
* Copyright (c) 2013 Maxim Integrated Products, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
* 02110-1301 USA
*/

#include "hrmsensor.h"
#include "hrm_max86902.h"

extern int hrm_debug;
extern int hrm_info;

#define VENDOR "MAXIM"
#define MAX86900_CHIP_NAME		"MAX86900"
#define MAX86902_CHIP_NAME		"MAX86902"
#define MAX86907_CHIP_NAME		"MAX86907"
#define MAX86907A_CHIP_NAME		"MAX86907A"
#define MAX86907B_CHIP_NAME		"MAX86907B"

#define CONFIG_SENSORS_ENABLE_SELFTEST

#define MAX86900_SLAVE_ADDR			0x51
#define MAX86900A_SLAVE_ADDR		0x57
#define MAX86902_SLAVE_ADDR		0x57

#define MAX86900_I2C_RETRY_DELAY	10
#define MAX86900_I2C_MAX_RETRIES	5
#define MAX86900_COUNT_MAX		65532
#define MAX86902_COUNT_MAX		65532

#define MAX86900C_WHOAMI		0x11
#define MAX86900A_REV_ID		0x00
#define MAX86900B_REV_ID		0x04
#define MAX86900C_REV_ID		0x05

#define MAX86902_PART_ID1			0xFF
#define MAX86902_PART_ID2			0x15
#define MAX86902_REV_ID1			0xFE
#define MAX86902_REV_ID2			0x03
#define MAX86906_OTP_ID				0x15
#define MAX86907_OTP_ID				0x01
#define MAX86907A_OTP_ID			0x02
#define MAX86907B_OTP_ID			0x04
#define MAX86907E_OTP_ID			0x06


#define MAX86900_DEFAULT_CURRENT	0x55
#define MAX86900A_DEFAULT_CURRENT	0xFF
#define MAX86900C_DEFAULT_CURRENT	0x0F
#define MAX86906_DEFAULT_CURRENT	0x0F

#define MAX86902_DEFAULT_CURRENT1	0x00 /* RED */
#define MAX86902_DEFAULT_CURRENT2	0xFF /* IR */
#define MAX86902_DEFAULT_CURRENT3	0x60 /* NONE */
#define MAX86902_DEFAULT_CURRENT4	0x60 /* Violet */

#define MAX86900_SAMPLE_RATE	4
#ifdef CONFIG_SENSORS_MAX_NOTCHFILTER
#define MAX86902_SAMPLE_RATE	4
#else
#define MAX86902_SAMPLE_RATE	1
#endif

//#define MAX86902_SPO2_ADC_RGE	2

/* MAX86900 Registers */
#define MAX86900_INTERRUPT_STATUS	0x00
#define MAX86900_INTERRUPT_ENABLE	0x01

#define MAX86900_FIFO_WRITE_POINTER	0x02
#define MAX86900_OVF_COUNTER		0x03
#define MAX86900_FIFO_READ_POINTER	0x04
#define MAX86900_FIFO_DATA			0x05
#define MAX86900_MODE_CONFIGURATION	0x06
#define MAX86900_SPO2_CONFIGURATION	0x07
#define MAX86900_UV_CONFIGURATION	0x08
#define MAX86900_LED_CONFIGURATION	0x09
#define MAX86900_UV_DATA			0x0F
#define MAX86900_TEMP_INTEGER		0x16
#define MAX86900_TEMP_FRACTION		0x17

#define MAX86900_WHOAMI_REG			0xFE

#define MAX86900_FIFO_SIZE				16
#define MAX_EOL_RESULT 132
/* MAX86902 Registers */

#define MAX_UV_DATA			2

#define MODE_UV				1
#define MODE_HR				2
#define MODE_SPO2			3
#define MODE_FLEX			7
#define MODE_GEST			8

#define IR_LED_CH			1
#define RED_LED_CH			2
#define NA_LED_CH			3
#define VIOLET_LED_CH		4

#define NUM_BYTES_PER_SAMPLE			3
#define MAX_LED_NUM						4

#define MAX86902_INTERRUPT_STATUS			0x00
#define PWR_RDY_MASK					0x01
#define UV_INST_OVF_MASK				0x02
#define UV_ACCUM_OVF_MASK				0x04
#define UV_RDY_MASK						0x08
#define UV_RDY_OFFSET					0x03
#define PROX_INT_MASK					0x10
#define ALC_OVF_MASK					0x20
#define PPG_RDY_MASK					0x40
#define A_FULL_MASK						0x80

#define MAX86902_INTERRUPT_STATUS_2			0x01
#define THERM_RDY						0x01
#define DIE_TEMP_RDY					0x02

#define MAX86902_INTERRUPT_ENABLE			0x02
#define MAX86902_INTERRUPT_ENABLE_2			0x03

#define MAX86902_FIFO_WRITE_POINTER			0x04
#define MAX86902_OVF_COUNTER				0x05
#define MAX86902_FIFO_READ_POINTER			0x06
#define MAX86902_FIFO_DATA					0x07

#define MAX86902_FIFO_CONFIG				0x08
#define MAX86902_FIFO_A_FULL_MASK		0x0F
#define MAX86902_FIFO_A_FULL_OFFSET		0x00
#define MAX86902_FIFO_ROLLS_ON_MASK		0x10
#define MAX86902_FIFO_ROLLS_ON_OFFSET	0x04
#define MAX86902_SMP_AVE_MASK			0xE0
#define MAX86902_SMP_AVE_OFFSET			0x05


#define MAX86902_MODE_CONFIGURATION			0x09
#define MAX86902_MODE_MASK				0x07
#define MAX86902_MODE_OFFSET			0x00
#define MAX86902_GESTURE_EN_MASK		0x08
#define MAX86902_GESTURE_EN_OFFSET		0x03
#define MAX86902_RESET_MASK				0x40
#define MAX86902_RESET_OFFSET			0x06
#define MAX86902_SHDN_MASK				0x80
#define MAX86902_SHDN_OFFSET			0x07

#define MAX86902_SPO2_CONFIGURATION			0x0A
#define MAX86902_LED_PW_MASK			0x03
#define MAX86902_LED_PW_OFFSET			0x00
#define MAX86902_SPO2_SR_MASK			0x1C
#define MAX86902_SPO2_SR_OFFSET			0x02
#define MAX86902_SPO2_ADC_RGE_MASK		0x60
#define MAX86902_SPO2_ADC_RGE_OFFSET	0x05
#define MAX86902_SPO2_EN_DACX_MASK		0x80
#define MAX86902_SPO2_EN_DACX_OFFSET	0x07

#define MAX86902_UV_CONFIGURATION			0x0B
#define MAX86902_UV_ADC_RGE_MASK		0x03
#define MAX86902_UV_ADC_RGE_OFFSET		0x00
#define MAX86902_UV_SR_MASK				0x04
#define MAX86902_UV_SR_OFFSET			0x02
#define MAX86902_UV_TC_ON_MASK			0x08
#define MAX86902_UV_TC_ON_OFFSET		0x03
#define MAX86902_UV_ACC_CLR_MASK		0x80
#define MAX86902_UV_ACC_CLR_OFFSET		0x07

#define MAX86902_LED1_PA					0x0C
#define MAX86902_LED2_PA					0x0D
#define MAX86902_LED3_PA					0x0E
#define MAX86902_LED4_PA					0x0F
#define MAX86902_PILOT_PA					0x10

/*THIS IS A BS ENTRY. KEPT HERE TO KEEP LEGACY CODE COMPUILING*/
#define MAX86902_LED_CONFIGURATION			0x10

#define MAX86902_LED_FLEX_CONTROL_1			0x11
#define MAX86902_S1_MASK				0x07
#define MAX86902_S1_OFFSET				0x00
#define MAX86902_S2_MASK				0x70
#define MAX86902_S2_OFFSET				0x04

#define MAX86902_LED_FLEX_CONTROL_2			0x12
#define MAX86902_S3_MASK				0x07
#define MAX86902_S3_OFFSET				0x00
#define MAX86902_S4_MASK				0x70
#define MAX86902_S4_OFFSET				0x04

#define MAX86902_UV_HI_THRESH				0x13
#define MAX86902_UV_LOW_THRESH			0x14
#define MAX86902_UV_ACC_THRESH_HI		0x15
#define MAX86902_UV_ACC_THRESH_MID		0x16
#define MAX86902_UV_ACC_THRESH_LOW		0x17

#define MAX86902_UV_DATA_HI					0x18
#define MAX86902_UV_DATA_LOW				0x19
#define MAX86902_UV_ACC_DATA_HI				0x1A
#define MAX86902_UV_ACC_DATA_MID			0x1B
#define MAX86902_UV_ACC_DATA_LOW			0x1C

#define MAX86902_UV_COUNTER_HI				0x1D
#define MAX86902_UV_COUNTER_LOW				0x1E

#define MAX86902_TEMP_INTEGER				0x1F
#define MAX86902_TEMP_FRACTION				0x20

#define MAX86902_TEMP_CONFIG				0x21
#define MAX86902_TEMP_EN_MASK			0x01
#define MAX86902_TEMP_EN_OFFSET			0x00

#define MAX86902_WHOAMI_REG_REV		0xFE
#define MAX86902_WHOAMI_REG_PART	0xFF

#define MAX86902_FIFO_SIZE				64

#define AWB_INTERVAL		20 /* 20 sample(from 17 to 28) */
/* 150ms */
/* #define AWB_INTERVAL		60 */
/* 200ms */
/* #define AWB_INTERVAL		80 */

#define EOL_INTERVAL		4 /* 20 sample(from 17 to 28) */

#define CONFIG_SKIP_CNT		8
#define FLICKER_DATA_CNT	200
#define MAX86900_IOCTL_MAGIC		0xFD
#define MAX86900_IOCTL_READ_FLICKER	_IOR(MAX86900_IOCTL_MAGIC, 0x01, int *)
#define AWB_CONFIG_TH1		2000
#define AWB_CONFIG_TH2		240000
#define AWB_CONFIG_TH3		20000
#define AWB_CONFIG_TH4		90

#define MAX86902_I2C_RETRY_DELAY	10
#define MAX86902_I2C_MAX_RETRIES	5

#define MAX86902_READ_REGFILE_PATH "/data/HRM/MAX86902_READ_REG.txt"
#define MAX86902_WRITE_REGFILE_PATH "/data/HRM/MAX86902_WRITE_REG.txt"

#define HRM_LDO_ON				1
#define HRM_LDO_OFF				0

#define I2C_WRITE_ENABLE		0x00
#define I2C_READ_ENABLE			0x01

#define MAX86900_THRESHOLD_DEFAULT 30000
#define MAX86902_THRESHOLD_DEFAULT 72000

#define AGC_MAX 253952
#define AGC_MIN 163850
#define MOD_TH 2

enum _PART_TYPE {
	PART_TYPE_MAX86900 = 0,
	PART_TYPE_MAX86900A,
	PART_TYPE_MAX86900B,
	PART_TYPE_MAX86900C,
	PART_TYPE_MAX86906,
	PART_TYPE_MAX86902A = 10,
	PART_TYPE_MAX86902B,
	PART_TYPE_MAX86907,
	PART_TYPE_MAX86907A,
	PART_TYPE_MAX86907B,
	PART_TYPE_MAX86907E,
} PART_TYPE;

enum _AWB_CONFIG {
	AWB_CONFIG0 = 0,
	AWB_CONFIG1,
	AWB_CONFIG2,
} AWB_CONFIG;

enum {
	LED1_UP		= 1 << 0,
	LED1_DOWN	= 1 << 1,
	LED2_UP		= 1 << 2,
	LED2_DOWN	= 1 << 3,
	LED1_SET	= 1 << 4,
	LED2_SET	= 1 << 5,
} LED_CON;

static enum {
	S_INIT = 0,
	S_F_A,
	S_CAL,
	S_F_D,
	S_ERR,
	S_UNKNOWN
} agc_pre_s, agc_cur_s;

enum {
	DEBUG_WRITE_REG_TO_FILE = 1,
	DEBUG_WRITE_FILE_TO_REG = 2,
	DEBUG_SHOW_DEBUG_INFO = 3,
	DEBUG_ENABLE_AGC = 4,
	DEBUG_DISABLE_AGC = 5,
};

enum {
	M_HRM,
	M_HRMLED_IR,
	M_HRMLED_RED,
	M_HRMLED_BOTH,
	M_NONE
};

//Enable AGC start
#define MAX869_LED1 0
#define MAX869_LED2 1

#define MAX869_MIN_CURRENT	0
#define MAX869_MAX_CURRENT	51000

#define MAX86900_MIN_CURRENT	0
#define MAX86900_MAX_CURRENT	51000
#define MAX86900_CURRENT_FULL_SCALE		\
		(MAX86900_MAX_CURRENT - MAX86900_MIN_CURRENT)

#define MAX86902_MIN_CURRENT	0
#define MAX86902_MAX_CURRENT	51000
#define MAX86902_CURRENT_FULL_SCALE		\
		(MAX86902_MAX_CURRENT - MAX86902_MIN_CURRENT)

#define MAX86900_MIN_DIODE_VAL	0
#define MAX86900_MAX_DIODE_VAL	((1 << 16) - 1)

#define MAX86902_MIN_DIODE_VAL	0
#define MAX86902_MAX_DIODE_VAL	((1 << 18) - 1)

#define MAX86900_CURRENT_PER_STEP	3400
#define MAX86902_CURRENT_PER_STEP	200

#define MAX86900_AGC_DEFAULT_LED_OUT_RANGE				70
#define MAX86900_AGC_DEFAULT_CORRECTION_COEFF			50
#define MAX86900_AGC_DEFAULT_SENSITIVITY_PERCENT		 14
#define MAX86900_AGC_DEFAULT_MIN_NUM_PERCENT			 (20 * MAX86900_SAMPLE_RATE)

#define MAX86902_AGC_DEFAULT_LED_OUT_RANGE				70
#define MAX86902_AGC_DEFAULT_CORRECTION_COEFF			50
#define MAX86902_AGC_DEFAULT_SENSITIVITY_PERCENT		 14
#define MAX86902_AGC_DEFAULT_MIN_NUM_PERCENT			 (20 * MAX86902_SAMPLE_RATE)

#define ILLEGAL_OUTPUT_POINTER -1
#define CONSTRAINT_VIOLATION -2
//Enable AGC end

#ifdef CONFIG_SENSORS_ENABLE_SELFTEST

#define ENABLE_EOL_SEQ3_100HZ

enum _EOL_STATE_TYPE {
	_EOL_STATE_TYPE_INIT = 0,
	_EOL_STATE_TYPE_P2P_INIT,
	_EOL_STATE_TYPE_P2P_MODE,
	_EOL_STATE_TYPE_FREQ_INIT,
	_EOL_STATE_TYPE_FREQ_MODE,
	_EOL_STATE_TYPE_AVERAGE_INIT,
	_EOL_STATE_TYPE_AVERAGE_MODE,
	_EOL_STATE_TYPE_END,
} _EOL_STATE_TYPE;

#define SELF_MODE_400HZ 	0
#define SELF_MODE_100HZ		1

#define SELF_DIVID_400HZ 	1
#define SELF_DIVID_100HZ	4


#define SELF_IR_CH			0
#define SELF_RED_CH			1
#define SELF_MAX_CH_NUM		2

#define SELF_SQ1_START_THRESH  8000

#define SELF_SQ2_SKIP_CNT  128
#define SELF_SQ3_SKIP_CNT  10
#define SELF_SQ4_SKIP_CNT  128



#define CONVER_FLOAT	65536
//#define CONVER_FLOAT	1

#define SELF_INTERRUT_MIN	385
#define SELF_SQ3_CNT_MAX	400

#define SELF_RETRY_COUNT	5
#define SELF_SKEW_RETRY_COUNT	5

#define SELF_SQ2_SIZE   256
#define SELF_SQ4_SIZE   50

#define MODE_100HZ_NORMAL
//#define MODE_100HZ_AVERAGE
#if defined(MODE_100HZ_NORMAL)
#define SELF_SQ1_VALID_POS	3
#define SELF_SQ1_VALID_CNT	1
#define SELF_SQ1_RETRY_CNT  48  
#define SELF_SQ1_SKIP_CNT   10
#define SELF_SQ1_SIZE		65
#define SELF_SAMPLE_SIZE	1
#define SELF_SQ1_DEFAULT_SPO2	0x07
#define MAX86902_SPO2_ADC_RGE	2
#elif defined(MODE_100HZ_AVERAGE)
#define SELF_SQ1_VALID_POS	3
#define SELF_SQ1_VALID_CNT	3
#define SELF_SQ1_RETRY_CNT  80
#define SELF_SQ1_SKIP_CNT   60
#define SELF_SQ1_SIZE		25
#define SELF_SAMPLE_SIZE	1
#define SELF_SQ1_DEFAULT_SPO2	0x07
#define MAX86902_SPO2_ADC_RGE	2
#else
#define SELF_SQ1_VALID_POS	0
#define SELF_SQ1_VALID_CNT	1
#define SELF_SQ1_RETRY_CNT  48  
#define SELF_SQ1_SKIP_CNT   10
#define SELF_SQ1_SIZE		200
#define SELF_SAMPLE_SIZE	1
#define MAX86902_SPO2_ADC_RGE	2
#define SELF_SQ1_DEFAULT_SPO2	0x07
#endif

#define EOL_TEST_MODE	1
#define EOL_TEST_GRIP	2
#define EOL_TEST_STOP	0

#define INTEGER_LEN 16
#define DECIMAL_LEN 10

#ifdef CONFIG_SENSORS_HRM_MAX869_ENHANCED_EOL
#define EOL_SEQ2_LED_CURRNET 0xE1
#else
#define EOL_SEQ2_LED_CURRNET 0x50
#endif


struct max86902_div_data{
	char left_integer[INTEGER_LEN];
	char left_decimal[DECIMAL_LEN];
	char right_integer[INTEGER_LEN];
	char right_decimal[DECIMAL_LEN];
	char result_integer[INTEGER_LEN];
	char result_decimal[DECIMAL_LEN];
};

struct  max86902_self_seq1_data{
	int count;
	int state;
	int retry;
	int index;
	u64 buf[SELF_MAX_CH_NUM][SELF_SQ1_SIZE];
	u64 sum[SELF_MAX_CH_NUM];
	u64 average[SELF_MAX_CH_NUM];
	u64 std_sum[SELF_MAX_CH_NUM];
	int done;
	u32 seq1_update_flag;
	int seq1_valid_pos;	
	int seq1_valid_cnt;		
	int seq1_valid_ir;
	int seq1_valid_red;	
	int d_seq1_valid_ir;
	int d_seq1_valid_red;	
	
	u32 seq1_current;
};

struct  max86902_self_seq2_data{
	int count;
	int index;
	u64 buf[SELF_MAX_CH_NUM][SELF_SQ2_SIZE];
	u64 sum[SELF_MAX_CH_NUM];
	u64 average[SELF_MAX_CH_NUM];
	u64 std_sum[SELF_MAX_CH_NUM];
	int done;
};

struct  max86902_self_seq3_data{
	int state;
	int retry;
	int skew_retry;
	unsigned long end_time;
	unsigned long start_time;
	struct timeval start_time_t;
	struct timeval work_time_t;
	unsigned long prev_time;
	int count;
	int skip_count;
	int done;
};

struct  max86902_self_seq4_data{
	int count;
	int index;
	u64 buf[SELF_MAX_CH_NUM][SELF_SQ2_SIZE];
	u64 sum[SELF_MAX_CH_NUM];
	u64 average[SELF_MAX_CH_NUM];
	u64 std_sum[SELF_MAX_CH_NUM];
	int done;
};

struct  max86902_selftest_data{

	int selftest_count;
	u8 status;
	struct timeval start_time;
	struct timeval end_time;
	struct  max86902_self_seq1_data seq1_data;
	struct  max86902_self_seq2_data seq2_data;
	struct  max86902_self_seq3_data	seq3_data;
	struct  max86902_self_seq4_data	seq4_data; 	//test the ir routine.
};
#endif

struct max869_device_data {
	struct i2c_client *client;
	struct mutex flickerdatalock;
	struct miscdevice miscdev;
	s32 agc_threshold;
	s32 hrm_mode;
	u8 agc_mode;
	u8 eol_test_status;
	char *eol_test_result;
	u8 part_type;
	u8 flex_mode;
	s32 num_samples;
	u16 led;
	u16 sample_cnt;
	s32 led_sum[4];
	s32 ir_sum;
	s32 r_sum;
	u8 awb_flicker_status;
	u8 eol_test_is_enable;
	u8 led1_pa_addr;
	u8 led2_pa_addr;
	u8 ir_led_ch;
	u8 red_led_ch;
	u8 led_current;
	u8 led_current1;
	u8 led_current2;
	u8 led_current3;
	u8 led_current4;
	u8 hr_range;
	u8 hr_range2;
	u8 look_mode_ir;
	u8 look_mode_red;
	s32 hrm_temp;
	u8 default_current;
	u8 default_current1;
	u8 default_current2;
	u8 default_current3;
	u8 default_current4;
	u8 test_current_ir;
	u8 test_current_red;
	u8 test_current_led1;
	u8 test_current_led2;
	u8 test_current_led3;
	u8 test_current_led4;
	u16 awb_sample_cnt;
	int *flicker_data;
	int flicker_data_cnt;
	//AGC routine start
	u8 ir_led;
	u8 red_led;

	bool agc_is_enable;
	int agc_sum[2];
	u32 agc_current[2];

	s32 agc_led_out_percent;
	s32 agc_corr_coeff;
	s32 agc_min_num_samples;
	s32 agc_sensitivity_percent;
	s32 change_by_percent_of_range[2];
	s32 change_by_percent_of_current_setting[2];
	s32 change_led_by_absolute_count[2];
	s32 reached_thresh[2];
	s32 threshol_default;

	int (*update_led)(struct max869_device_data*, int, int);
	//AGC routine end

#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
	struct max86902_selftest_data selftest_data;
	u8 self_mode;
	u8 self_divid;
	u8 self_sync;
#endif
	int isTrimmed;

	u32 i2c_err_cnt;
	u16 ir_curr;
	u16 red_curr;
	u32 ir_adc;
	u32 red_adc;
};

#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
static void __max869_init_selftest_data(struct  max86902_selftest_data *self_data);
static void __max869_selftest_sequence_1(int ir_data, int red_data, struct  max86902_selftest_data *self_data);
static void __max869_selftest_sequence_2(int ir_data, int red_data, struct  max86902_selftest_data *self_data, u8 divid);
static void __max869_selftest_sequence_3(struct  max86902_selftest_data *self_data, u8 divid);
static void __max869_selftest_sequence_4(int ir_data, int red_data, struct  max86902_selftest_data *self_data, u8 divid);
static int __max869_selftest_check_done(struct  max86902_selftest_data *self_data, u8 mode, struct max869_device_data *device);
static void __max869_div_float(struct max86902_div_data *div_data, u64 left_operand, u64 right_operand);
static void __max869_selftest_conv2float(struct max86902_selftest_data *self_data, u8 mode, struct max869_device_data *device);
static void __max869_div_str(char *left_integer, char *left_decimal, char *right_integer, char *right_decimal, char *result_integer, char *result_decimal);
static void __max869_float_sqrt(char *integer_operand, char *decimal_operand);
static void __max869_plus_str(char *integer_operand, char *decimal_operand, char *result_integer, char *result_decimal);
#endif

static struct max869_device_data *max869_data;
static u8 agc_is_enabled = 1;
static uint64_t agc_count;

static int __max869_write_default_regs(void)
{
	int err = 0;

	return err;
}

static int __max869_write_reg(struct max869_device_data *device,
	u8 reg_addr, u8 data)
{
	int err;
	int tries = 0;
	u8 buffer[2] = { reg_addr, data };
	struct i2c_msg msgs[] = {
		{
			.addr = device->client->addr,
			.flags = device->client->flags & I2C_M_TEN,
			.len = 2,
			.buf = buffer,
		},
	};

	do {
		err = i2c_transfer(device->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(MAX86902_I2C_RETRY_DELAY);
		if (err < 0)
			HRM_dbg("%s - i2c_transfer error = %d\n", __func__, err);
	} while ((err != 1) && (++tries < MAX86900_I2C_MAX_RETRIES));

	if (err != 1) {
		HRM_dbg("%s -write transfer error:%d\n", __func__, err);
		err = -EIO;
		device->i2c_err_cnt++;
		return err;
	}

	return 0;
}

static int __max869_read_reg(struct max869_device_data *data,
	u8 *buffer, int length)
{
	int err = -1;
	int tries = 0; /* # of attempts to read the device */

	struct i2c_msg msgs[] = {
		{
			.addr = data->client->addr,
			.flags = data->client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buffer,
		},
		{
			.addr = data->client->addr,
			.flags = (data->client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = length,
			.buf = buffer,
		},
	};

	do {
		err = i2c_transfer(data->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(MAX86902_I2C_RETRY_DELAY);
		if (err < 0)
			HRM_dbg("%s - i2c_transfer error = %d\n", __func__, err);
	} while ((err != 2) && (++tries < MAX86900_I2C_MAX_RETRIES));

	if (err != 2) {
		HRM_dbg("%s -read transfer error:%d\n", __func__, err);
		err = -EIO;
		data->i2c_err_cnt++;
	} else
		err = 0;

	return err;
}

static int __max86900_hrm_enable(struct max869_device_data *data)
{
	int err;
	data->led = 0;
	data->sample_cnt = 0;
	data->ir_sum = 0;
	data->r_sum = 0;

	err = __max869_write_reg(data, MAX86900_INTERRUPT_ENABLE, 0x10);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_INTERRUPT_ENABLE!\n",
			__func__);
		return -EIO;
	}

#if MAX86900_SAMPLE_RATE == 1
	err = __max869_write_reg(data, MAX86900_SPO2_CONFIGURATION, 0x47);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
#endif

#if MAX86900_SAMPLE_RATE == 2
	err = __max869_write_reg(data, MAX86900_SPO2_CONFIGURATION, 0x4E);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
#endif

#if MAX86900_SAMPLE_RATE == 4
	err = __max869_write_reg(data, MAX86900_SPO2_CONFIGURATION, 0x51);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
#endif

	err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION,
		data->default_current);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_FIFO_WRITE_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_FIFO_WRITE_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_OVF_COUNTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_OVF_COUNTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_FIFO_READ_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_FIFO_READ_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x0B);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	//AGC control
	data->reached_thresh[data->ir_led] = 0;
	data->reached_thresh[data->red_led] = 0;
	data->agc_sum[0] = 0;
	data->agc_sum[1] = 0;
	data->agc_current[data->ir_led] =
		((data->default_current & 0x0f) * MAX86900_CURRENT_PER_STEP);
	data->agc_current[data->red_led] =
		(((data->default_current & 0xf0) >> 4) * MAX86900_CURRENT_PER_STEP);
	data->led_current1 = ((data->default_current & 0xf0) >> 4); /*set red */
	data->led_current2 = data->default_current & 0x0f; /*set ir */
	return 0;
}

static int __max86902_hrm_enable(struct max869_device_data *data)
{
	int err;
	u8 flex_config[2] = {0, };
	data->led = 0;
	data->sample_cnt = 0;
	data->led_sum[0] = 0;
	data->led_sum[1] = 0;
	data->led_sum[2] = 0;
	data->led_sum[3] = 0;

	data->num_samples = 0;
	data->flex_mode = 0;

	flex_config[0] = (data->ir_led_ch << MAX86902_S2_OFFSET) | data->red_led_ch;
	flex_config[1] = 0x00;
	if (flex_config[0] & MAX86902_S1_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 0);
	}
	if (flex_config[0] & MAX86902_S2_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 1);
	}
	if (flex_config[1] & MAX86902_S3_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 2);
	}
	if (flex_config[1] & MAX86902_S4_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 3);
	}

	HRM_info("%s - flexmode : 0x%02x, num_samples : %d\n", __func__,
			data->flex_mode, data->num_samples);

	/*write LED currents ir=1, red=2, violet=4*/
	err = __max869_write_reg(data, data->led1_pa_addr,
		data->default_current1);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
			__func__);
		return -EIO;
	}
	err = __max869_write_reg(data, data->led2_pa_addr,
			data->default_current2);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_INTERRUPT_ENABLE, PPG_RDY_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_INTERRUPT_ENABLE!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_LED_FLEX_CONTROL_1,
			flex_config[0]);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED_FLEX_CONTROL_1!\n",
			__func__);
		return -EIO;
	}
	err = __max869_write_reg(data, MAX86902_LED_FLEX_CONTROL_2,
			flex_config[1]);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED_FLEX_CONTROL_2!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_SPO2_CONFIGURATION,
			0x0E | (0x03 << MAX86902_SPO2_ADC_RGE_OFFSET));
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

#if MAX86902_SAMPLE_RATE == 1
	err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
			(0x02 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
			__func__);
		return -EIO;
	}
#endif

#if MAX86902_SAMPLE_RATE == 2
	err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
			(0x01 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
			__func__);
		return -EIO;
	}
#endif

#if MAX86902_SAMPLE_RATE == 4
	err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
			(0x00 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
			__func__);
		return -EIO;
	}
#endif

	err = __max869_write_reg(data, MAX86902_FIFO_WRITE_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_WRITE_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_OVF_COUNTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_OVF_COUNTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_FIFO_READ_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_READ_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_MODE_CONFIGURATION, MODE_FLEX);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_MODE_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
	/* Temperature Enable */
	err = __max869_write_reg(data, MAX86902_TEMP_CONFIG, 0x01);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_TEMP_CONFIG!\n",
			__func__);
		return -EIO;
	}

	//AGC control
	data->reached_thresh[data->ir_led] = 0;
	data->reached_thresh[data->red_led] = 0;
	data->agc_sum[0] = 0;
	data->agc_sum[1] = 0;
	data->agc_current[data->ir_led] =
		(data->default_current1 * MAX86902_CURRENT_PER_STEP);
	data->agc_current[data->red_led] =
		(data->default_current2 * MAX86902_CURRENT_PER_STEP);
	data->led_current1 = data->default_current1;/* set red */
	data->led_current2 = data->default_current2;/* set ir */
	return 0;
}

static int __max86900_disable(struct max869_device_data *data)
{
	int err;

	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x40);
	if (err != 0) {
		HRM_dbg("%s - error init MAX86900_MODE_CONFIGURATION!\n",
			__func__);
		goto i2c_err;
	}
	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x80);
	if (err != 0) {
		HRM_dbg("%s - error init MAX86900_MODE_CONFIGURATION!\n",
			__func__);
		goto i2c_err;
	}
	data->led_current1 = 0;
	data->led_current2 = 0;
	return 0;
i2c_err:
	return -EIO;
}

static int __max86902_disable(struct max869_device_data *data)
{
	int err;

	err = __max869_write_reg(data, MAX86902_MODE_CONFIGURATION, 0x40);
	if (err != 0) {
		HRM_dbg("%s - error init MAX86902_MODE_CONFIGURATION!\n",
			__func__);
		goto i2c_err;
	}
	err = __max869_write_reg(data, MAX86902_MODE_CONFIGURATION, 0x80);
	if (err != 0) {
		HRM_dbg("%s - error init MAX86902_MODE_CONFIGURATION!\n",
			__func__);
		goto i2c_err;
	}
	data->awb_sample_cnt = 0;
	data->flicker_data_cnt = 0;

	data->led_current1 = 0;
	data->led_current2 = 0;

	return 0;

i2c_err:
	return -EIO;
}
static int __max86900_init_device(struct max869_device_data *data)
{
	int err;
	u8 recvData;

	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x40);
	if (err != 0) {
		HRM_dbg("%s - error sw shutdown device!\n", __func__);
		return -EIO;
	}

	/* Interrupt Clear */
	recvData = MAX86900_INTERRUPT_STATUS;
	err = __max869_read_reg(data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x80);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_UV_CONFIGURATION, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_UV_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
	HRM_info("%s done, part_type = %u\n", __func__, data->part_type);

	return err;
}


static int __max86902_init_device(struct max869_device_data *data)
{
	int err = 0;
	u8 recvData;

	err = __max869_write_reg(data, MAX86902_MODE_CONFIGURATION, 0x40);
	if (err != 0) {
		HRM_dbg("%s - error sw shutdown device!\n", __func__);
		return -EIO;
	}

	/* Interrupt Clear */
	recvData = MAX86902_INTERRUPT_STATUS;
	err = __max869_read_reg(data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}

	/* Interrupt2 Clear */
	recvData = MAX86902_INTERRUPT_STATUS_2;
	err = __max869_read_reg(data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}
	HRM_info("%s done, part_type = %u\n", __func__, data->part_type);

	return err;
}
static int __max869_init(struct max869_device_data *data)
{
	int err = 0;

	if (data->part_type < PART_TYPE_MAX86902A)
		err = __max86900_init_device(data);
	else
		err = __max86902_init_device(data);

	if (err < 0)
		HRM_dbg("%s init fail, err  = %d\n", __func__, err);

	err = __max869_write_default_regs();

	HRM_info("%s init done, err  = %d\n", __func__, err);

	return err;
}

static int __max869_enable(struct max869_device_data *data)
{
	int err = 0;

	if (data->part_type < PART_TYPE_MAX86902A)
		err = __max86900_hrm_enable(data);
	else
		err = __max86902_hrm_enable(data);

	if (err < 0)
		HRM_dbg("%s enable fail, err  = %d\n", __func__, err);

	HRM_info("%s enable done, err  = %d\n", __func__, err);

	return err;
}

static int __max869_disable(struct max869_device_data *data)
{
	int err = 0;

	if (data->part_type < PART_TYPE_MAX86902A)
		err = __max86900_disable(data);
	else
		err = __max86902_disable(data);

	if (err < 0)
		HRM_dbg("%s enable fail, err  = %d\n", __func__, err);

	HRM_info("%s enable done, err  = %d\n", __func__, err);

	return err;
}

static int __max869_set_reg_hrm(struct max869_device_data *data)
{
	int err = 0;

	err = __max869_init(data);
	err = __max869_enable(data);
	data->agc_mode = M_HRM;

	return err;
}
static int __max869_set_reg_hrmled_ir(struct max869_device_data *data)
{
	int err = 0;

	err = __max869_init(data);
	err = __max869_enable(data);
	data->agc_mode = M_HRMLED_IR;

	return err;
}
static int __max869_set_reg_hrmled_red(struct max869_device_data *data)
{
	int err = 0;

	err = __max869_init(data);
	err = __max869_enable(data);
	data->agc_mode = M_HRMLED_RED;

	return err;
}
static int __max869_set_reg_hrmled_both(struct max869_device_data *data)
{
	int err = 0;

	err = __max869_init(data);
	err = __max869_enable(data);
	data->agc_mode = M_HRMLED_BOTH;

	return err;
}
static int __max869_set_reg_ambient(struct max869_device_data *data)
{
	int err = 0;
	u8 recvData = 0;

	err = __max869_init(data);
	err = __max869_enable(data);

	if (data->part_type < PART_TYPE_MAX86902A) { /* 86900 */
			err = __max869_write_reg(data, 0xFF, 0x54);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0xFF, 0x4d);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x82, 0x04);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x80!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x8f, 0x01);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x8f!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0xff, 0x00);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0xff!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x07, 0x31);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0xff!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x09, 0x00);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0xff!\n",
					__func__);
				return -EIO;
			}
	} else { /* 86902 */
			/* Mode change to AWB */
			err = __max869_write_reg(data,
				MAX86902_MODE_CONFIGURATION, 0x40);
			if (err != 0) {
				HRM_dbg("%s - error sw shutdown data!\n", __func__);
				return -EIO;
			}

			/* Interrupt Clear */
			recvData = MAX86902_INTERRUPT_STATUS;
			err = __max869_read_reg(data, &recvData, 1);
			if (err != 0) {
				HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
					__func__, err, recvData);
				return -EIO;
			}

			/* Interrupt2 Clear */
			recvData = MAX86902_INTERRUPT_STATUS_2;
			err = __max869_read_reg(data, &recvData, 1);
			if (err != 0) {
				HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
					__func__, err, recvData);
				return -EIO;
			}
			err = __max869_write_reg(data, 0xFF, 0x54);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0xFF, 0x4d);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x82, 0x04);
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x82!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0x8f, 0x01); /* PW_EN = 0 */
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x8f!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, 0xFF, 0x00);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST3!\n",
					__func__);
				return -EIO;
			}
			/* 400Hz, LED_PW=400us, SPO2_ADC_RANGE=2048nA */
			err = __max869_write_reg(data,
				MAX86902_SPO2_CONFIGURATION, 0x0F);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data,
				MAX86902_FIFO_CONFIG, ((32 - AWB_INTERVAL) & 0x0f));
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data,
				MAX86902_INTERRUPT_ENABLE, A_FULL_MASK);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_INTERRUPT_ENABLE!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data,
					MAX86902_MODE_CONFIGURATION, 0x02);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_MODE_CONFIGURATION!\n",
					__func__);
				return -EIO;
			}
			data->awb_sample_cnt = 0;
			data->flicker_data_cnt = 0;
			data->awb_flicker_status = AWB_CONFIG1;
	}
	data->agc_mode = M_NONE;

	return err;
}

static int __max869_set_reg_prox(struct max869_device_data *data)
{
	int err = 0;
	int led_max = 0xf;

	err = __max869_init(data);
	err = __max869_enable(data);

	if (data->part_type < PART_TYPE_MAX86902A)
		led_max = 0x0f;
	else
		led_max = 0xff;
	max869_set_current(led_max, 0, 0, 0);
	data->agc_mode = M_NONE;

	return err;
}

static int max86900_update_led_current(struct max869_device_data *data,
	int new_led_uA,
	int led_num)
{
	int err;
	u8 old_led_reg_val;
	u8 new_led_reg_val;

	if (new_led_uA > MAX86900_MAX_CURRENT) {
		HRM_dbg("%s - Tried to set LED%d to %duA. Max is %duA\n",
				__func__, led_num, new_led_uA, MAX86900_MAX_CURRENT);
		new_led_uA = MAX86900_MAX_CURRENT;
	} else if (new_led_uA < MAX86900_MIN_CURRENT) {
		HRM_dbg("%s - Tried to set LED%d to %duA. Min is %duA\n",
				__func__, led_num, new_led_uA, MAX86900_MIN_CURRENT);
		new_led_uA = MAX86900_MIN_CURRENT;
	}

	new_led_reg_val = new_led_uA / MAX86900_CURRENT_PER_STEP;
	HRM_info("%s - %d-new_led_uA:%d,%d\n", __func__, led_num, new_led_uA, new_led_reg_val);

	old_led_reg_val = MAX86900_LED_CONFIGURATION;
	err = __max869_read_reg(data, &old_led_reg_val, 1);
	if (err) {
		HRM_dbg("%s - error updating led current!\n", __func__);
		return -EIO;
	}

	if (led_num == data->ir_led)
		new_led_reg_val = (old_led_reg_val & 0xF0) | (new_led_reg_val);
	else
		new_led_reg_val = (old_led_reg_val & 0x0F) | (new_led_reg_val << 4);

	HRM_dbg("%s - setting LED reg to %.2X\n", __func__, new_led_reg_val);

	err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION, new_led_reg_val);
	if (err) {
		HRM_dbg("%s - error updating led current!\n", __func__);
		return -EIO;
	}

	if (led_num == data->ir_led)
		data->led_current2 = new_led_reg_val & 0x0F; /*set ir */
	else
		data->led_current1 = (new_led_reg_val & 0xF0) >> 4; /* set red */

	return 0;
}

static int max86902_update_led_current(struct max869_device_data *data,
	int new_led_uA,
	int led_num)
{
	int err;
	u8 led_reg_val;
	int led_reg_addr;

	if (new_led_uA > MAX86902_MAX_CURRENT) {
		HRM_dbg("%s - Tried to set LED%d to %duA. Max is %duA\n",
				__func__, led_num, new_led_uA, MAX86902_MAX_CURRENT);
		new_led_uA = MAX86902_MAX_CURRENT;
	} else if (new_led_uA < MAX86902_MIN_CURRENT) {
		HRM_dbg("%s - Tried to set LED%d to %duA. Min is %duA\n",
				__func__, led_num, new_led_uA, MAX86902_MIN_CURRENT);
		new_led_uA = MAX86902_MIN_CURRENT;
	}

	led_reg_val = new_led_uA / MAX86902_CURRENT_PER_STEP;
	led_reg_addr = MAX86902_LED1_PA + led_num;

	HRM_dbg("%s - Setting LED%d to 0x%.2X (%duA)\n", __func__, led_num, led_reg_val, new_led_uA);

	err = __max869_write_reg(data, led_reg_addr, led_reg_val);
	if (err != 0) {
		HRM_dbg("%s - error initializing register 0x%.2X!\n",
			__func__, led_reg_addr);
		return -EIO;
	}

	if (data->part_type > PART_TYPE_MAX86907A) {
		if (led_num)
			data->led_current1 = led_reg_val;/* set red */
		else
			data->led_current2 = led_reg_val;/* set ir */
	} else {
		if (led_num)
			data->led_current2 = led_reg_val;/* set ir */
		else
			data->led_current1 = led_reg_val;/* set red */
	}
	return 0;
}

static int agc_adj_calculator(struct max869_device_data *data,
			s32 *change_by_percent_of_range,
			s32 *change_by_percent_of_current_setting,
			s32 *change_led_by_absolute_count,
			s32 *set_led_to_absolute_count,
			s32 target_percent_of_range,
			s32 correction_coefficient,
			s32 allowed_error_in_percentage,
			s32 *reached_thresh,
			s32 current_average,
			s32 number_of_samples_averaged,
			s32 led_drive_current_value)
{
	s32 diode_min_val;
	s32 diode_max_val;
	s32 diode_min_current;
	s32 diode_fs_current;

	s32 current_percent_of_range = 0;
	s32 delta = 0;
	s32 desired_delta = 0;
	s32 current_power_percent = 0;

	if (change_by_percent_of_range == 0
			 || change_by_percent_of_current_setting == 0
			 || change_led_by_absolute_count == 0
			 || set_led_to_absolute_count == 0)
		return ILLEGAL_OUTPUT_POINTER;

	if (target_percent_of_range > 90 || target_percent_of_range < 10)
		return CONSTRAINT_VIOLATION;

	if (correction_coefficient > 100 || correction_coefficient < 0)
		return CONSTRAINT_VIOLATION;

	if (allowed_error_in_percentage > 100
			|| allowed_error_in_percentage < 0)
		return CONSTRAINT_VIOLATION;

#if ((MAX86900_MAX_DIODE_VAL-MAX86900_MIN_DIODE_VAL) <= 0 \
			 || (MAX86902_MAX_DIODE_VAL < 0) || (MAX86902_MIN_DIODE_VAL < 0))
	#error "Illegal max86900 diode Min/Max Pair"
#endif

#if ((MAX86900_CURRENT_FULL_SCALE) <= 0 \
		|| (MAX86902_MAX_CURRENT < 0) || (MAX86902_MIN_CURRENT < 0))
	#error "Illegal max86900 LED Min/Max current Pair"
#endif

#if ((MAX86902_MAX_DIODE_VAL-MAX86902_MIN_DIODE_VAL) <= 0 \
			 || (MAX86902_MAX_DIODE_VAL < 0) || (MAX86902_MIN_DIODE_VAL < 0))
	#error "Illegal max86902 diode Min/Max Pair"
#endif

#if ((MAX86902_CURRENT_FULL_SCALE) <= 0 \
		|| (MAX86902_MAX_CURRENT < 0) || (MAX86902_MIN_CURRENT < 0))
	#error "Illegal max86902 LED Min/Max current Pair"
#endif

	if (led_drive_current_value > MAX86902_MAX_CURRENT
			|| led_drive_current_value < MAX86902_MIN_CURRENT)
		return CONSTRAINT_VIOLATION;

	if (current_average < MAX86902_MIN_DIODE_VAL
				|| current_average > MAX86902_MAX_DIODE_VAL)
		return CONSTRAINT_VIOLATION;

	if (data->part_type < PART_TYPE_MAX86902A) {
		diode_min_val = MAX86900_MIN_DIODE_VAL;
		diode_max_val = MAX86900_MAX_DIODE_VAL;
		diode_min_current = MAX86900_MIN_CURRENT;
		diode_fs_current = MAX86900_CURRENT_FULL_SCALE;
	} else {
		diode_min_val = MAX86902_MIN_DIODE_VAL;
		diode_max_val = MAX86902_MAX_DIODE_VAL;
		diode_min_current = MAX86902_MIN_CURRENT;
		diode_fs_current = MAX86902_CURRENT_FULL_SCALE;
	}

	current_percent_of_range = 100 *
		(current_average - diode_min_val) /
		(diode_max_val - diode_min_val);

	delta = current_percent_of_range - target_percent_of_range;
	delta = delta * correction_coefficient / 100;

	if (!(*reached_thresh))
		allowed_error_in_percentage =
			allowed_error_in_percentage * 3 / 4;

	if (delta > -allowed_error_in_percentage
			&& delta < allowed_error_in_percentage) {
		*change_by_percent_of_range = 0;
		*change_by_percent_of_current_setting = 0;
		*change_led_by_absolute_count = 0;
		*set_led_to_absolute_count = led_drive_current_value;
		*reached_thresh = 1;
		return 0;
	}

	current_power_percent = 100 *
			(led_drive_current_value - diode_min_current) /
			diode_fs_current;

	if (delta < 0) {
		if (current_power_percent > 97 && (current_percent_of_range < (data->threshol_default * 100) / (diode_max_val - diode_min_val))) {
			desired_delta = -delta * (102 - current_power_percent) /
				(100 - current_percent_of_range);
		} else
			desired_delta = -delta * (100 - current_power_percent) /
				(100 - current_percent_of_range);
	}

	if (delta > 0)
		desired_delta = -delta * (current_power_percent)
				/ (current_percent_of_range);

	*change_by_percent_of_range = desired_delta;

	*change_led_by_absolute_count = (desired_delta
			* diode_fs_current / 100);
	*change_by_percent_of_current_setting =
			(*change_led_by_absolute_count * 100)
			/ (led_drive_current_value);
	*set_led_to_absolute_count = led_drive_current_value
			+ *change_led_by_absolute_count;
	return 0;
}

int __max869_hrm_agc(struct max869_device_data *data, int counts, int led_num)
{
	int err = 0;
	int avg = 0;
	int led_num0 = 0;
	int led_num1 = 0;

	HRM_info("%s - led_num(%d) = %d\n", __func__, led_num, counts);

	if (led_num < MAX869_LED1 || led_num > MAX869_LED2)
		return -EIO;

	data->agc_sum[led_num] += counts;
	if ((data->sample_cnt + 1) % data->agc_min_num_samples)
		return 0;

	avg = data->agc_sum[led_num] / data->agc_min_num_samples;
	data->agc_sum[led_num] = 0;

	err = agc_adj_calculator(data,
			&data->change_by_percent_of_range[led_num],
			&data->change_by_percent_of_current_setting[led_num],
			&data->change_led_by_absolute_count[led_num],
			&data->agc_current[led_num],
			data->agc_led_out_percent,
			data->agc_corr_coeff,
			data->agc_sensitivity_percent,
			&data->reached_thresh[led_num],
			avg,
			data->agc_min_num_samples,
			data->agc_current[led_num]);

	if (err)
		return err;

	HRM_info("%s - %d-a:%d\n", __func__, led_num, data->change_led_by_absolute_count[led_num]);

	if (data->change_led_by_absolute_count[led_num] == 0) {

		if (data->part_type < PART_TYPE_MAX86902A) { 
			led_num0 = data->ir_led; 
			led_num1 = data->red_led; 
		} else { 
			led_num0 = data->red_led; 
			led_num1 = data->ir_led; 
		} 

		if (led_num == led_num0) {
			data->ir_adc = counts;
			data->ir_curr = data->led_current2;
		} else if (led_num == led_num1) {
			data->red_adc = counts;
			data->red_curr = data->led_current1;
		}

		HRM_info("%s - ir_curr = 0x%2x, red_curr = 0x%2x, ir_adc = %d, red_adc = %d\n",
			__func__, data->ir_curr, data->red_curr, data->ir_adc, data->red_adc);

		return 0;
	}

	err = data->update_led(data, data->agc_current[led_num], led_num);
	if (err)
		HRM_dbg("%s failed\n", __func__);

	return err;
}

static int __max869_cal_agc(int ir, int red, int ext)
{
	int err = 0;
	int led_num0, led_num1;

	if (max869_data->part_type < PART_TYPE_MAX86902A) {
		led_num0 = max869_data->ir_led;
		led_num1 = max869_data->red_led;
	} else {
		led_num0 = max869_data->red_led;
		led_num1 = max869_data->ir_led;
	}

	HRM_info("%s - led_num0(%d) = %d, led_num1(%d) = %d\n", __func__, led_num0, ir, led_num1, red);

	if (max869_data->agc_mode == M_HRM) {
		if (ir >= max869_data->threshol_default) { //Detect
			err |= __max869_hrm_agc(max869_data, ir, led_num0); //IR
			if (max869_data->agc_current[led_num1] != MAX869_MIN_CURRENT) { //Disable led_num1
				err |= __max869_hrm_agc(max869_data, red, led_num1); //RED
			} else {
				//init
				max869_data->agc_current[led_num1] = MAX869_MAX_CURRENT;
				max869_data->reached_thresh[led_num1] = 0;
				max869_data->update_led(max869_data,
						max869_data->agc_current[led_num1], led_num1);
			}
		} else {
			if (max869_data->agc_current[led_num1] != MAX869_MIN_CURRENT) { //Disable led_num1
				//init
				max869_data->agc_current[led_num1] = MAX869_MIN_CURRENT;
				max869_data->reached_thresh[led_num1] = MAX869_MIN_CURRENT;
				max869_data->update_led(max869_data,
						max869_data->agc_current[led_num1], led_num1);
			}
			if (max869_data->agc_current[led_num0] != MAX869_MAX_CURRENT) { //Disable led_num0
				//init
				max869_data->agc_current[led_num0] = MAX869_MAX_CURRENT;
				max869_data->reached_thresh[led_num0] = 0;
				max869_data->update_led(max869_data,
						max869_data->agc_current[led_num0], led_num0);
			}
		}
	} else if (max869_data->agc_mode == M_HRMLED_IR) {
		if (max869_data->agc_current[led_num0] != MAX869_MIN_CURRENT) { //Disable led_num0
			err |= __max869_hrm_agc(max869_data, ir, led_num0); //IR
		} else {
			max869_data->agc_current[led_num0] = MAX869_MAX_CURRENT;
			max869_data->reached_thresh[led_num0] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num0], led_num0);
		}
		if (max869_data->agc_current[led_num1] != 0) { //Disable led_num1
			//init
			max869_data->agc_current[led_num1] = 0;
			max869_data->reached_thresh[led_num1] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num1], led_num1);
		}
	} else if (max869_data->agc_mode == M_HRMLED_RED) {
		if (max869_data->agc_current[led_num1] != MAX869_MIN_CURRENT) { //Disable led_num1
			err |= __max869_hrm_agc(max869_data, red, led_num1); //RED
		} else {
			max869_data->agc_current[led_num1] = MAX869_MAX_CURRENT;
			max869_data->reached_thresh[led_num1] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num1], led_num1);
		}
		if (max869_data->agc_current[led_num0] != 0) { //Disable led_num0
			//init
			max869_data->agc_current[led_num0] = 0;
			max869_data->reached_thresh[led_num0] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num0], led_num0);
		}
	} else if (max869_data->agc_mode == M_HRMLED_BOTH) {
		if (max869_data->agc_current[led_num0] != MAX869_MIN_CURRENT) { //Disable led_num0
			err |= __max869_hrm_agc(max869_data, ir, led_num0); //IR
		} else {
			max869_data->agc_current[led_num0] = MAX869_MAX_CURRENT;
			max869_data->reached_thresh[led_num0] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num0], led_num0);
		}
		if (max869_data->agc_current[led_num1] != MAX869_MIN_CURRENT) { //Disable led_num1
			err |= __max869_hrm_agc(max869_data, red, led_num1); //RED
		} else {
			max869_data->agc_current[led_num1] = MAX869_MAX_CURRENT;
			max869_data->reached_thresh[led_num1] = 0;
			max869_data->update_led(max869_data,
					max869_data->agc_current[led_num1], led_num1);
		}
	}

	return err;
}

static int __max869_write_reg_to_file(void)
{
	return 0;
}


static int __max869_write_file_to_reg(void)
{
	return -EIO;
}

static void __max869_print_mode(int mode)
{
	HRM_info("%s - ===== mode(%d) =====\n", __func__, mode);
	if (mode == MODE_HRM)
		HRM_info("%s - %s(%d)\n", __func__, "HRM", MODE_HRM);
	if (mode == MODE_AMBIENT)
		HRM_info("%s - %s(%d)\n", __func__, "AMBIENT", MODE_AMBIENT);
	if (mode == MODE_PROX)
		HRM_info("%s - %s(%d)\n", __func__, "PROX", MODE_PROX);
	if (mode == MODE_HRMLED_IR)
		HRM_info("%s - %s(%d)\n", __func__, "HRMLED_IR", MODE_HRMLED_IR);
	if (mode == MODE_HRMLED_RED)
		HRM_info("%s - %s(%d)\n", __func__, "HRMLED_RED", MODE_HRMLED_RED);
	if (mode == MODE_UNKNOWN)
		HRM_info("%s - %s(%d)\n", __func__, "UNKNOWN", MODE_UNKNOWN);
	HRM_info("%s - ====================\n", __func__);
}


static int __max869_otp_id(struct max869_device_data *data)
{
	u8 recvData;
	int err;

	err = __max869_write_reg(data, 0xFF, 0x54);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, 0xFF, 0x4d);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
			__func__);
		return -EIO;
	}

	recvData = 0x8B;
	err = __max869_read_reg(data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}

	err = __max869_write_reg(data, 0xFF, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
			__func__);
		return -EIO;
	}

	HRM_dbg("%s - 0x8B :%x\n", __func__, recvData);

	return recvData;

}

static int __max86900_read_temperature(struct max869_device_data *data)
{
	u8 recvData[2] = { 0x00, };
	int err;

	recvData[0] = MAX86900_TEMP_INTEGER;

	err = __max869_read_reg(data, recvData, 2);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}
	data->hrm_temp = ((char)recvData[0]) * 16 + recvData[1];

	HRM_info("%s - %d(%x, %x)\n", __func__,
		data->hrm_temp, recvData[0], recvData[1]);

	return 0;
}

static int __max86902_read_temperature(struct max869_device_data *data)
{
	u8 recvData[2] = { 0x00, };
	int err;

	recvData[0] = MAX86902_TEMP_INTEGER;

	err = __max869_read_reg(data, recvData, 1);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}

	recvData[1] = MAX86902_TEMP_FRACTION;
	err = __max869_read_reg(data, &recvData[1], 1);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}

	data->hrm_temp = ((char)recvData[0]) * 16 + recvData[1];

	err = __max869_write_reg(data, MAX86902_TEMP_CONFIG, 0x01);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_TEMP_CONFIG!\n",
			__func__);
		return -EIO;
	}

	HRM_info("%s - %d(%x, %x)\n", __func__,
			data->hrm_temp, recvData[0], recvData[1]);

	return 0;
}

#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
static void __max869_init_selftest_data(struct max86902_selftest_data *self_data)
{
	memset(self_data, 0, sizeof(struct max86902_selftest_data));
	do_gettimeofday(&self_data->start_time);
}

static void __max869_selftest_sequence_1(int ir_data, int red_data, struct  max86902_selftest_data *self_data)
{
	int i = 0;
	int seq1_retry = self_data->seq1_data.retry;
	int seq1_count = self_data->seq1_data.count;
	int seq1_index = self_data->seq1_data.index;
	int seq1_pos = self_data->seq1_data.seq1_valid_pos;
	int seq1_cnt = self_data->seq1_data.seq1_valid_cnt;	
	u64 data[2];
	u64 average[2];

	if (!seq1_count)
		HRM_dbg("EOL_TEST sequence 1 started pos : %d, cnt :%d", self_data->seq1_data.seq1_valid_pos, self_data->seq1_data.seq1_valid_cnt);

	if (seq1_pos)
		self_data->seq1_data.seq1_valid_pos--; // 3->2->1->0
	else { // gather  datasets
		if (seq1_cnt) {
			self_data->seq1_data.seq1_valid_ir += ir_data;
			self_data->seq1_data.seq1_valid_red += red_data;				
			self_data->seq1_data.seq1_valid_cnt--;
		}
	}

	if (!self_data->seq1_data.seq1_valid_cnt) {
		self_data->seq1_data.seq1_valid_ir /= SELF_SQ1_VALID_CNT;
		self_data->seq1_data.seq1_valid_red /= SELF_SQ1_VALID_CNT;
		self_data->seq1_data.d_seq1_valid_ir = self_data->seq1_data.seq1_valid_ir;
		self_data->seq1_data.d_seq1_valid_red = self_data->seq1_data.seq1_valid_red;
		self_data->seq1_data.seq1_update_flag = 1;
	}


	if (seq1_count < SELF_SQ1_SKIP_CNT)
			goto seq1_exit;

	if (!self_data->seq1_data.state) {
		if (ir_data < SELF_SQ1_START_THRESH)
			self_data->seq1_data.state = 1;
		else {
			if (seq1_retry < SELF_SQ1_RETRY_CNT) {
				HRM_dbg("EOL_TEST sequence 1 Start Cond Retry : %d", self_data->seq1_data.retry);
				self_data->seq1_data.retry++;
				goto seq1_exit;
			} else {
				self_data->seq1_data.state = 1;
			}
		}
	}
	if (seq1_index < SELF_SQ1_SIZE && self_data->seq1_data.seq1_update_flag) {
		self_data->seq1_data.buf[SELF_IR_CH][seq1_index] = (u64)self_data->seq1_data.seq1_valid_ir*CONVER_FLOAT;
		self_data->seq1_data.buf[SELF_RED_CH][seq1_index] = (u64)self_data->seq1_data.seq1_valid_red*CONVER_FLOAT;
		self_data->seq1_data.sum[SELF_IR_CH] += self_data->seq1_data.buf[SELF_IR_CH][seq1_index];
		self_data->seq1_data.sum[SELF_RED_CH] += self_data->seq1_data.buf[SELF_RED_CH][seq1_index];
		self_data->seq1_data.seq1_update_flag = 0;
		self_data->seq1_data.seq1_valid_ir = 0;
		self_data->seq1_data.seq1_valid_red = 0;
		self_data->seq1_data.index++;
	} else if (seq1_index == SELF_SQ1_SIZE && self_data->seq1_data.done != 1) {
		do_div(self_data->seq1_data.sum[SELF_IR_CH], SELF_SQ1_SIZE);
		self_data->seq1_data.average[SELF_IR_CH] = self_data->seq1_data.sum[SELF_IR_CH];
		do_div(self_data->seq1_data.sum[SELF_RED_CH], SELF_SQ1_SIZE);
		self_data->seq1_data.average[SELF_RED_CH] = self_data->seq1_data.sum[SELF_RED_CH];

		average[SELF_IR_CH] = self_data->seq1_data.average[SELF_IR_CH];
		do_div(average[SELF_IR_CH], CONVER_FLOAT);
		average[SELF_RED_CH] = self_data->seq1_data.average[SELF_RED_CH];
		do_div(average[SELF_RED_CH], CONVER_FLOAT);

		for (i = 0; i < SELF_SQ1_SIZE; i++) {
			do_div(self_data->seq1_data.buf[SELF_IR_CH][i], CONVER_FLOAT);
			data[SELF_IR_CH] = self_data->seq1_data.buf[SELF_IR_CH][i];
			do_div(self_data->seq1_data.buf[SELF_RED_CH][i], CONVER_FLOAT);
			data[SELF_RED_CH] = self_data->seq1_data.buf[SELF_RED_CH][i];

			self_data->seq1_data.std_sum[SELF_IR_CH] += (data[SELF_IR_CH] - average[SELF_IR_CH]) * (data[SELF_IR_CH] - average[SELF_IR_CH]);
			self_data->seq1_data.std_sum[SELF_RED_CH] += (data[SELF_RED_CH] - average[SELF_RED_CH]) * (data[SELF_RED_CH] - average[SELF_RED_CH]);
		}

		self_data->seq1_data.done = 1;

		HRM_dbg("EOL_TEST sequence 1 Done");
	}
seq1_exit:
	if(self_data->seq1_data.seq1_update_flag){
		self_data->seq1_data.seq1_update_flag = 0;
		self_data->seq1_data.seq1_valid_ir = 0;
		self_data->seq1_data.seq1_valid_red = 0;
	}
	self_data->seq1_data.count++;

}

static void __max869_selftest_sequence_2(int ir_data, int red_data, struct  max86902_selftest_data *self_data, u8 divid)
{
	int i = 0;
	int seq2_count = self_data->seq2_data.count;
	int seq2_index = self_data->seq2_data.index;
	u64 data[2];
	u64 average[2];
	u32 self_sq2_skip_cnt = SELF_SQ2_SKIP_CNT/divid;
	u32 self_sq2_size = SELF_SQ2_SIZE / divid;
	if (!seq2_count) {
		HRM_dbg("EOL_TEST sequence 2 started");
	}
	if (seq2_count < self_sq2_skip_cnt)
			goto seq2_exit;

	if (seq2_index < self_sq2_size) {
		self_data->seq2_data.buf[SELF_IR_CH][seq2_index] = (u64)ir_data*CONVER_FLOAT;
		self_data->seq2_data.buf[SELF_RED_CH][seq2_index] = (u64)red_data*CONVER_FLOAT;
		self_data->seq2_data.sum[SELF_IR_CH] += self_data->seq2_data.buf[SELF_IR_CH][seq2_index];
		self_data->seq2_data.sum[SELF_RED_CH] += self_data->seq2_data.buf[SELF_RED_CH][seq2_index];
		self_data->seq2_data.index++;
	} else if (seq2_index == self_sq2_size && self_data->seq2_data.done != 1) {
		do_div(self_data->seq2_data.sum[SELF_IR_CH], self_sq2_size);
		self_data->seq2_data.average[SELF_IR_CH] = self_data->seq2_data.sum[SELF_IR_CH];
		do_div(self_data->seq2_data.sum[SELF_RED_CH], self_sq2_size);
		self_data->seq2_data.average[SELF_RED_CH] = self_data->seq2_data.sum[SELF_RED_CH];

		average[SELF_IR_CH] = self_data->seq2_data.average[SELF_IR_CH];
		do_div(average[SELF_IR_CH], CONVER_FLOAT);
		average[SELF_RED_CH] = self_data->seq2_data.average[SELF_RED_CH];
		do_div(average[SELF_RED_CH], CONVER_FLOAT);

		for (i = 0; i <	self_sq2_size; i++) {
			do_div(self_data->seq2_data.buf[SELF_IR_CH][i], CONVER_FLOAT);
			data[SELF_IR_CH] = self_data->seq2_data.buf[SELF_IR_CH][i];
			do_div(self_data->seq2_data.buf[SELF_RED_CH][i], CONVER_FLOAT);
			data[SELF_RED_CH] = self_data->seq2_data.buf[SELF_RED_CH][i];
			self_data->seq2_data.std_sum[SELF_IR_CH] += (data[SELF_IR_CH] - average[SELF_IR_CH]) * (data[SELF_IR_CH] - average[SELF_IR_CH]);
			self_data->seq2_data.std_sum[SELF_RED_CH] += (data[SELF_RED_CH] - average[SELF_RED_CH]) * (data[SELF_RED_CH] - average[SELF_RED_CH]);
		}
		self_data->seq2_data.done = 1;

		HRM_dbg("EOL_TEST sequence 2 Done");
	}

seq2_exit:
	self_data->seq2_data.count++;
}

static void __max869_selftest_sequence_3(struct  max86902_selftest_data *self_data, u8 divid)
{
	int seq3_state = self_data->seq3_data.state;
	unsigned long seq3_end_time = self_data->seq3_data.end_time;
	unsigned long seq3_prev_time = self_data->seq3_data.prev_time;
	unsigned long seq3_current_time = jiffies;
	int seq3_count = self_data->seq3_data.skip_count;
	unsigned long seq3_interval;
	int seq3_start_time;
	int seq3_work_time;
	u32 self_sq3_skip_cnt = SELF_SQ3_SKIP_CNT/divid;
	u32 self_interrupt_min = SELF_INTERRUT_MIN/divid;
	u32 self_sq3_timeout = (HZ / (HZ / divid));

	if (seq3_count < self_sq3_skip_cnt)
		goto seq3_exit;

	switch (seq3_state) {
	case 0:
		HRM_dbg("EOL_TEST sequence 3 stated ###");
		self_data->seq3_data.start_time = jiffies;
		do_gettimeofday(&self_data->seq3_data.start_time_t);
		self_data->seq3_data.end_time = jiffies + HZ;        /* timeout in 1s */
		self_data->seq3_data.state = 1;
		self_data->seq3_data.count++;
		break;

	case 1:
		if (time_is_after_eq_jiffies(seq3_end_time)) {
			if (time_is_after_eq_jiffies(seq3_prev_time)) {
				self_data->seq3_data.count++;
			} else {
				if (self_data->seq3_data.skew_retry < SELF_SKEW_RETRY_COUNT) {
					self_data->seq3_data.skew_retry++;
					seq3_interval = seq3_current_time-seq3_prev_time;
					HRM_dbg("!!!!!! EOL_TEST sequence 3 Clock skew too large retry : %d , interval : %lu ms, count : %d",
						self_data->seq3_data.skew_retry, seq3_interval*10, self_data->seq3_data.count);
					self_data->seq3_data.state = 0;
					self_data->seq3_data.count = 0;
					self_data->seq3_data.prev_time = 0;
				}
			}
			do_gettimeofday(&self_data->seq3_data.work_time_t);
		} else {
			if (self_data->seq3_data.count < self_interrupt_min && self_data->seq3_data.retry < SELF_RETRY_COUNT) {
				HRM_dbg("!!!!!! EOL_TEST sequence 3 CNT Gap too large : %d, retry : %d", self_data->seq3_data.count, self_data->seq3_data.retry);
				self_data->seq3_data.retry++;
				self_data->seq3_data.state = 0;
				self_data->seq3_data.count = 0;
				self_data->seq3_data.prev_time = 0;
			} else {
				self_data->seq3_data.count *= divid;
				self_data->seq3_data.done = 1;
				seq3_start_time = (self_data->seq3_data.start_time_t.tv_sec * 1000) + (self_data->seq3_data.start_time_t.tv_usec / 1000);
				seq3_work_time = (self_data->seq3_data.work_time_t.tv_sec * 1000) + (self_data->seq3_data.work_time_t.tv_usec / 1000);
				HRM_dbg("EOL_TEST sequence 3 Done###");
				HRM_dbg("EOL_TEST sequence 3 sample_no_cnt : %d, time : %d ms, divid : %d ",
					self_data->seq3_data.count, seq3_work_time - seq3_start_time, divid);
			}
		}
		break;

	default:
		HRM_dbg("EOL_TEST sequence 3 Should not call this routine !!!###");
		break;
	}
seq3_exit:
	self_data->seq3_data.prev_time = seq3_current_time + self_sq3_timeout;  /* timeout in 10 or 40 ms */
	self_data->seq3_data.skip_count++;
}

static void __max869_selftest_sequence_4(int ir_data, int red_data, struct  max86902_selftest_data *self_data, u8 divid)
{
	int i = 0;
	int seq4_count = self_data->seq4_data.count;
	int seq4_index = self_data->seq4_data.index;
	u64 data[2];
	u64 average[2];
	u32 self_sq4_skip_cnt = SELF_SQ4_SKIP_CNT / divid;
	u32 self_sq4_size = SELF_SQ4_SIZE / divid;

	if (!seq4_count) {
		HRM_dbg("EOL_TEST sequence 4 started\n");
	}
	if (seq4_count < self_sq4_skip_cnt)
		goto seq4_exit;

	if (seq4_index < self_sq4_size) {
		self_data->seq4_data.buf[SELF_IR_CH][seq4_index] = (u64)ir_data*CONVER_FLOAT;
		self_data->seq4_data.buf[SELF_RED_CH][seq4_index] = (u64)red_data*CONVER_FLOAT;
		self_data->seq4_data.sum[SELF_IR_CH] += self_data->seq4_data.buf[SELF_IR_CH][seq4_index];
		self_data->seq4_data.sum[SELF_RED_CH] += self_data->seq4_data.buf[SELF_RED_CH][seq4_index];
		self_data->seq4_data.index++;
	} else if (seq4_index == self_sq4_size && self_data->seq4_data.done != 1) {
		do_div(self_data->seq4_data.sum[SELF_IR_CH], self_sq4_size);
		self_data->seq4_data.average[SELF_IR_CH] = self_data->seq4_data.sum[SELF_IR_CH];
		do_div(self_data->seq4_data.sum[SELF_RED_CH], self_sq4_size);
		self_data->seq4_data.average[SELF_RED_CH] = self_data->seq4_data.sum[SELF_RED_CH];

		average[SELF_IR_CH] = self_data->seq4_data.average[SELF_IR_CH];
		do_div(average[SELF_IR_CH], CONVER_FLOAT);
		average[SELF_RED_CH] = self_data->seq4_data.average[SELF_RED_CH];
		do_div(average[SELF_RED_CH], CONVER_FLOAT);

		for (i = 0; i <	self_sq4_size; i++) {
			do_div(self_data->seq4_data.buf[SELF_IR_CH][i], CONVER_FLOAT);
			data[SELF_IR_CH] = self_data->seq4_data.buf[SELF_IR_CH][i];
			do_div(self_data->seq4_data.buf[SELF_RED_CH][i], CONVER_FLOAT);
			data[SELF_RED_CH] = self_data->seq4_data.buf[SELF_RED_CH][i];
			self_data->seq4_data.std_sum[SELF_IR_CH] += (data[SELF_IR_CH] - average[SELF_IR_CH]) * (data[SELF_IR_CH] - average[SELF_IR_CH]);
			self_data->seq4_data.std_sum[SELF_RED_CH] += (data[SELF_RED_CH] - average[SELF_RED_CH]) * (data[SELF_RED_CH] - average[SELF_RED_CH]);
		}
		self_data->seq4_data.done = 1;
		HRM_dbg("EOL_TEST sequence 4 Done\n");
	}

seq4_exit:
	self_data->seq4_data.count++;
}
static int __max869_selftest_check_done(struct max86902_selftest_data *self_data, u8 mode, struct max869_device_data *device)
{
	int i = 0;
	int start_time;
	int end_time;
	u32 self_sq2_size = SELF_SQ2_SIZE;

	if (self_data->seq1_data.done && self_data->seq2_data.done && self_data->seq3_data.done && self_data->seq4_data.done) {
		do_gettimeofday(&self_data->end_time);

		start_time = (self_data->start_time.tv_sec * 1000) + (self_data->start_time.tv_usec / 1000);
		end_time = (self_data->end_time.tv_sec * 1000) + (self_data->end_time.tv_usec / 1000);
		HRM_dbg("Tested Time :  %d ms Tested Count : %d\n", end_time - start_time, self_data->seq3_data.count);

		for (i = 0; i < self_sq2_size; i++) {
			if (SELF_SQ4_SIZE < SELF_SQ1_SIZE) {
				if (i < SELF_SQ4_SIZE) {
					HRM_dbg("EOLRAW,%llu,%llu,%llu,%llu,%llu,%llu\n", self_data->seq1_data.buf[SELF_IR_CH][i], self_data->seq1_data.buf[SELF_RED_CH][i], self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i],
					self_data->seq4_data.buf[SELF_IR_CH][i], self_data->seq4_data.buf[SELF_RED_CH][i]);
				} else if (i < SELF_SQ1_SIZE) {
					HRM_dbg("EOLRAW,%llu,%llu,%llu,%llu,0,0\n", self_data->seq1_data.buf[SELF_IR_CH][i], self_data->seq1_data.buf[SELF_RED_CH][i], self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i]);
				} else {
					HRM_dbg("EOLRAW,0,0,%llu,%llu,0,0\n", self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i]);
				}
			} else {
				if (i < SELF_SQ1_SIZE) {
					HRM_dbg("EOLRAW,%llu,%llu,%llu,%llu,%llu,%llu\n", self_data->seq1_data.buf[SELF_IR_CH][i], self_data->seq1_data.buf[SELF_RED_CH][i], self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i],
					self_data->seq4_data.buf[SELF_IR_CH][i], self_data->seq4_data.buf[SELF_RED_CH][i]);
				} else if (i < SELF_SQ4_SIZE) {
					HRM_dbg("EOLRAW,0,0,%llu,%llu,%llu,%llu\n", self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i],self_data->seq4_data.buf[SELF_IR_CH][i], self_data->seq4_data.buf[SELF_RED_CH][i]);
				} else {
					HRM_dbg("EOLRAW,0,0,%llu,%llu,0,0\n", self_data->seq2_data.buf[SELF_IR_CH][i], self_data->seq2_data.buf[SELF_RED_CH][i]);
				}
			}
		}

		HRM_dbg("EOL_TEST [FINISH STEP 1, 2, 3] %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %d, %d\n",
		self_data->seq1_data.average[SELF_IR_CH], self_data->seq1_data.average[SELF_RED_CH],
		self_data->seq1_data.std_sum[SELF_IR_CH], self_data->seq1_data.std_sum[SELF_RED_CH],
		self_data->seq2_data.average[SELF_IR_CH], self_data->seq2_data.average[SELF_RED_CH],
		self_data->seq2_data.std_sum[SELF_IR_CH], self_data->seq2_data.std_sum[SELF_RED_CH],
		self_data->seq3_data.count, self_data->selftest_count);

		HRM_dbg("EOL_TEST [FINISH STEP 4] %llu, %llu, %llu, %llu\n",
		self_data->seq4_data.average[SELF_IR_CH], self_data->seq4_data.average[SELF_RED_CH],
		self_data->seq4_data.std_sum[SELF_IR_CH], self_data->seq4_data.std_sum[SELF_RED_CH]);
		__max869_selftest_conv2float(self_data, mode, device);
		self_data->status = 1;
		device->eol_test_status = 1;
		return 1;
	} else {
		return 0;
	}
}

static void __max869_div_float(struct max86902_div_data *div_data, u64 left_operand, u64 right_operand)
{
	sprintf(div_data->left_integer, "%llu", left_operand);
	memset(div_data->left_decimal, '0', sizeof(div_data->left_decimal));
	div_data->left_decimal[DECIMAL_LEN - 1] = 0;

	sprintf(div_data->right_integer, "%llu", right_operand);
	memset(div_data->right_decimal, '0', sizeof(div_data->right_decimal));
	div_data->right_decimal[DECIMAL_LEN - 1] = 0;

	__max869_div_str(div_data->left_integer, div_data->left_decimal, div_data->right_integer, div_data->right_decimal, div_data->result_integer, div_data->result_decimal);
}

static void __max869_selftest_conv2float(struct max86902_selftest_data *self_data, u8 mode, struct max869_device_data *device)
{
	unsigned int seq2_size = SELF_SQ2_SIZE;
	struct max86902_div_data div_data;
	char std1_ir[2][INTEGER_LEN], std1_red[2][INTEGER_LEN];
	char avg1_ir[2][INTEGER_LEN], avg1_red[2][INTEGER_LEN];
	char ratio_ir[2][INTEGER_LEN], ratio_red[2][INTEGER_LEN];
	char std2_ir[2][INTEGER_LEN], std2_red[2][INTEGER_LEN];
	char avg2_ir[2][INTEGER_LEN], avg2_red[2][INTEGER_LEN];
	char r_factor[2][INTEGER_LEN], temp_data[2][INTEGER_LEN];
	char avg4_ir[2][INTEGER_LEN], avg4_red[2][INTEGER_LEN];

	HRM_info("EOL_TEST __max869_selftest_conv2float\n");
	if (mode)
		seq2_size /= SELF_DIVID_100HZ;
	else
		seq2_size /= SELF_DIVID_400HZ;

	__max869_div_float(&div_data, self_data->seq1_data.average[SELF_IR_CH], CONVER_FLOAT);
	strcpy(avg1_ir[0], div_data.result_integer);
	strcpy(avg1_ir[1], div_data.result_decimal);
	__max869_div_float(&div_data, self_data->seq1_data.average[SELF_RED_CH], CONVER_FLOAT);
	strcpy(avg1_red[0], div_data.result_integer);
	strcpy(avg1_red[1], div_data.result_decimal);

	__max869_div_float(&div_data, self_data->seq1_data.std_sum[SELF_IR_CH], SELF_SQ1_SIZE);
	strcpy(std1_ir[0], div_data.result_integer);
	strcpy(std1_ir[1], div_data.result_decimal);
	__max869_div_float(&div_data, self_data->seq1_data.std_sum[SELF_RED_CH], SELF_SQ1_SIZE);
	strcpy(std1_red[0], div_data.result_integer);
	strcpy(std1_red[1], div_data.result_decimal);

	__max869_float_sqrt(std1_ir[0], std1_ir[1]);
	__max869_float_sqrt(std1_red[0], std1_red[1]);

	__max869_div_str(std1_ir[0], std1_ir[1], avg1_ir[0], avg1_ir[1], ratio_ir[0], ratio_ir[1]);
	__max869_div_str(std1_red[0], std1_red[1], avg1_red[0], avg1_red[1], ratio_red[0], ratio_red[1]);

	__max869_div_float(&div_data, self_data->seq2_data.average[SELF_IR_CH], CONVER_FLOAT);
	strcpy(avg2_ir[0], div_data.result_integer);
	strcpy(avg2_ir[1], div_data.result_decimal);
	__max869_div_float(&div_data, self_data->seq2_data.average[SELF_RED_CH], CONVER_FLOAT);
	strcpy(avg2_red[0], div_data.result_integer);
	strcpy(avg2_red[1], div_data.result_decimal);

	__max869_div_float(&div_data, self_data->seq2_data.std_sum[SELF_IR_CH], seq2_size);
	strcpy(std2_ir[0], div_data.result_integer);
	strcpy(std2_ir[1], div_data.result_decimal);
	__max869_div_float(&div_data, self_data->seq2_data.std_sum[SELF_RED_CH], seq2_size);
	strcpy(std2_red[0], div_data.result_integer);
	strcpy(std2_red[1], div_data.result_decimal);

	__max869_float_sqrt(std2_ir[0], std2_ir[1]);
	__max869_float_sqrt(std2_red[0], std2_red[1]);

	__max869_div_str(std1_red[0], std1_red[1], std1_ir[0], std1_ir[1], r_factor[0], r_factor[1]);
	__max869_div_str(r_factor[0], r_factor[1], avg1_red[0], avg1_red[1], r_factor[0], r_factor[1]);
	sprintf(temp_data[0], "%d", 1);
	memset(temp_data[1], '0', sizeof(temp_data[1]));
	temp_data[1][DECIMAL_LEN - 1] = 0;
	__max869_div_str(temp_data[0], temp_data[1], avg1_ir[0], avg1_ir[1], temp_data[0], temp_data[1]);
	__max869_div_str(r_factor[0], r_factor[1], temp_data[0], temp_data[1], r_factor[0], r_factor[1]);

	__max869_div_float(&div_data, self_data->seq4_data.average[SELF_IR_CH], CONVER_FLOAT);
	strcpy(avg4_ir[0], div_data.result_integer);
	strcpy(avg4_ir[1], div_data.result_decimal);
	__max869_div_float(&div_data, self_data->seq4_data.average[SELF_RED_CH], CONVER_FLOAT);
	strcpy(avg4_red[0], div_data.result_integer);
	strcpy(avg4_red[1], div_data.result_decimal);

	if (device->eol_test_result != NULL)
		kfree(device->eol_test_result);

	device->eol_test_result = kzalloc(sizeof(char) * MAX_EOL_RESULT, GFP_KERNEL);
	if (device->eol_test_result == NULL) {
		HRM_dbg("max86900_%s - couldn't allocate memory\n",
			__func__);
		return;
	}

	snprintf(device->eol_test_result, MAX_EOL_RESULT, "%s.%c%c, %s.%c%c, %s.%c%c, %s.%c%c, %s.%c%c, %s.%c%c, %d, %d, %s.%c%c, %s.%c%c, %s.%c%c, %s.%c%c, %s.%c%c%c%c, %s, %s\n",
		std1_ir[0], std1_ir[1][0], std1_ir[1][1], std1_red[0], std1_red[1][0], std1_red[1][1],
		avg1_ir[0], avg1_ir[1][0], avg1_ir[1][1], avg1_red[0], avg1_red[1][0], avg1_red[1][1],
		ratio_ir[0], ratio_ir[1][0], ratio_ir[1][1], ratio_red[0], ratio_red[1][0], ratio_red[1][1],
		self_data->seq3_data.count, self_data->seq3_data.count,
		avg2_ir[0], avg2_ir[1][0], avg2_ir[1][1], avg2_red[0], avg2_red[1][0], avg2_red[1][1],
		std2_ir[0], std2_ir[1][0], std2_ir[1][1], std2_red[0], std2_red[1][0], std2_red[1][1],
		r_factor[0], r_factor[1][0], r_factor[1][1], r_factor[1][2], r_factor[1][3],
		avg4_ir[0], avg4_red[0]);
}

static void __max869_plus_str(char *integer_operand, char *decimal_operand, char *result_integer, char *result_decimal)
{
	unsigned long long a, b;
	int i;
	for (i = 0; i < DECIMAL_LEN - 1; i++) {
		decimal_operand[i] -= '0';
		result_decimal[i] -= '0';
	}

	for (i = 0; i < DECIMAL_LEN - 1; i++)
		result_decimal[i] += decimal_operand[i];

	sscanf(integer_operand, "%llu", &a);
	sscanf(result_integer, "%llu", &b);

	for (i = DECIMAL_LEN - 1; i >= 0; i--) {
		if (result_decimal[i] >= 10) {
			if (i > 0)
				result_decimal[i - 1] += 1;
			else
				a += 1;
			result_decimal[i] -= 10;
		}
	}

	a += b;

	for (i = 0; i < DECIMAL_LEN - 1; i++)
		result_decimal[i] += '0';

	sprintf(result_integer, "%llu", a);
}

static void __max869_float_sqrt(char *integer_operand, char *decimal_operand)
{
	int i;
	char left_integer[INTEGER_LEN], left_decimal[DECIMAL_LEN];
	char result_integer[INTEGER_LEN], result_decimal[DECIMAL_LEN];
	char temp_integer[INTEGER_LEN], temp_decimal[DECIMAL_LEN];

	strcpy(left_integer, integer_operand);
	strcpy(left_decimal, decimal_operand);

	for (i = 0; i < 100; i++) {
		__max869_div_str(left_integer, left_decimal, integer_operand, decimal_operand, result_integer, result_decimal);
		__max869_plus_str(integer_operand, decimal_operand, result_integer, result_decimal);

		sprintf(temp_integer, "%d", 2);
		memset(temp_decimal, '0', sizeof(temp_decimal));
		temp_decimal[DECIMAL_LEN - 1] = 0;

		__max869_div_str(result_integer, result_decimal, temp_integer, temp_decimal, integer_operand, decimal_operand);
	}
}

static void __max869_div_str(char *left_integer, char *left_decimal, char *right_integer, char *right_decimal, char *result_integer, char *result_decimal)
{
	int i;
	unsigned long long loperand, roperand;
	unsigned long long ldigit, rdigit, temp;
	char str[10] = { 0, };
	ldigit = 0;
	rdigit = 0;

	sscanf(left_integer, "%llu", &loperand);
	for (i = DECIMAL_LEN - 2; i >= 0; i--) {
		if (left_decimal[i] != '0') {
			ldigit = i + 1;
			break;
		}
	}

	sscanf(right_integer, "%llu", &roperand);
	for (i = DECIMAL_LEN - 2; i >= 0; i--) {
		if (right_decimal[i] != '0') {
			rdigit = i + 1;
			break;
		}
	}

	if (ldigit < rdigit)
		ldigit = rdigit;

	for (i = 0; i < ldigit; i++) {
		loperand *= 10;
		roperand *= 10;
	}
	if (ldigit != 0) {
		sprintf(str, "%%%llullu", ldigit);
		sscanf(left_decimal, str, &temp);
		loperand += temp;

		sprintf(str, "%%%llullu", ldigit);
		sscanf(right_decimal, str, &temp);
		roperand += temp;
	}
	if (roperand == 0)
		roperand = 1;
	temp = loperand / roperand;
	sprintf(result_integer, "%llu", temp);
	loperand -= roperand * temp;
	loperand *= 10;
	for (i = 0; i < DECIMAL_LEN; i++) {
		if (roperand == 0)
			roperand = 1;
		temp = loperand / roperand;
		if (temp != 0) {
			loperand -= roperand * temp;
			loperand *= 10;
		} else
			loperand *= 10;
		result_decimal[i] = '0' + temp;
	}
	result_decimal[DECIMAL_LEN - 1] = 0;
}
#endif

static int __max86900_hrm_eol_test_enable(struct max869_device_data *data)
{
	int err;
	u8 led_current;
	data->led = 1; /* Prevent resetting MAX86900_LED_CONFIGURATION */
	data->sample_cnt = 0;

	HRM_info("%s\n", __func__);
	/* Test Mode Setting Start */
	data->hr_range = 0; /* Set test phase as 0 */
	data->eol_test_status = 0;
	data->test_current_ir = data->look_mode_ir;
	data->test_current_red = data->look_mode_red;
	led_current = (data->test_current_red << 4) | data->test_current_ir;

	err = __max869_write_reg(data, MAX86900_INTERRUPT_ENABLE, 0x10);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_INTERRUPT_ENABLE!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION, led_current);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_SPO2_CONFIGURATION, 0x47);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	/* Clear FIFO */
	err = __max869_write_reg(data, MAX86900_FIFO_WRITE_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_FIFO_WRITE_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_OVF_COUNTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_OVF_COUNTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86900_FIFO_READ_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_FIFO_READ_POINTER!\n",
			__func__);
		return -EIO;
	}

	/* Shutdown Clear */
	err = __max869_write_reg(data, MAX86900_MODE_CONFIGURATION, 0x0B);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	return 0;
}

static int __max86902_hrm_eol_test_enable(struct max869_device_data *data)
{
	int err;
	u8 flex_config[2] = {0, };
	data->led = 1; /* Prevent resetting MAX86902_LED_CONFIGURATION */
	data->sample_cnt = 0;
	data->num_samples = 0;
	data->flex_mode = 0;

	flex_config[0] = (data->ir_led_ch << MAX86902_S2_OFFSET) | data->red_led_ch;
	flex_config[1] = 0x00;
	if (flex_config[0] & MAX86902_S1_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 0);
	}
	if (flex_config[0] & MAX86902_S2_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 1);
	}
	if (flex_config[1] & MAX86902_S3_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 2);
	}
	if (flex_config[1] & MAX86902_S4_MASK) {
		data->num_samples++;
		data->flex_mode |= (1 << 3);
	}

	HRM_info("%s - flexmode : 0x%02x, num_samples : %d\n", __func__,
			data->flex_mode, data->num_samples);
	/* Test Mode Setting Start */
	data->hr_range = 0; /* Set test phase as 0 */
	data->eol_test_status = 0;
	data->test_current_led1 = ((data->look_mode_ir >> 4) & 0x0f) << 4;
	data->test_current_led2 = ((data->look_mode_ir) & 0x0f) << 4;
	data->test_current_led3 = ((data->look_mode_red >> 4) & 0x0f) << 4;
	data->test_current_led4 = ((data->look_mode_red) & 0x0f) << 4;

	/*write LED currents ir=1, red=2, violet=4*/
	err = __max869_write_reg(data,
			data->led1_pa_addr, data->test_current_led1);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
			__func__);
		return -EIO;
	}
	err = __max869_write_reg(data,
			data->led2_pa_addr, data->test_current_led2);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
			__func__);
		return -EIO;
	}
	err = __max869_write_reg(data,
			MAX86902_LED4_PA, data->test_current_led4);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED4_PA!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_INTERRUPT_ENABLE, PPG_RDY_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_INTERRUPT_ENABLE!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_LED_FLEX_CONTROL_1,
			flex_config[0]);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED_FLEX_CONTROL_1!\n",
			__func__);
		return -EIO;
	}
	err = __max869_write_reg(data, MAX86902_LED_FLEX_CONTROL_2,
			flex_config[1]);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_LED_FLEX_CONTROL_2!\n",
			__func__);
		return -EIO;
	}

	/* 100Hz setting no average for calculating  P2P */
	err = __max869_write_reg(data, MAX86902_SPO2_CONFIGURATION,
			SELF_SQ1_DEFAULT_SPO2 | (MAX86902_SPO2_ADC_RGE << MAX86902_SPO2_ADC_RGE_OFFSET));
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
			(0x00 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_FIFO_WRITE_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_WRITE_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_OVF_COUNTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_OVF_COUNTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_FIFO_READ_POINTER, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_FIFO_READ_POINTER!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(data, MAX86902_MODE_CONFIGURATION, MODE_FLEX);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_MODE_CONFIGURATION!\n",
			__func__);
		return -EIO;
	}
	/* Temperature Enable */
	err = __max869_write_reg(data, MAX86902_TEMP_CONFIG, 0x01);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86902_TEMP_CONFIG!\n",
			__func__);
		return -EIO;
	}

	return 0;
}
static void __max86900_eol_test_onoff(struct max869_device_data *data, int onoff)
{
	int err;

	if (onoff) {
		err = __max86900_hrm_eol_test_enable(data);
		data->eol_test_is_enable = onoff;
		if (err != 0)
			HRM_dbg("__max86900_hrm_eol_test_enable err : %d\n", err);
	} else {
		HRM_info("%s - eol test off\n", __func__);
		err = __max86900_disable(data);
		if (err != 0)
			HRM_dbg("__max86900_disable err : %d\n", err);

		data->hr_range = 0;
		data->led_current = data->default_current;

		err = __max86900_init_device(data);
		if (err)
			HRM_dbg("%s max86900_init device fail err = %d\n",
				__func__, err);

		err = __max86900_hrm_enable(data);
		if (err != 0)
			HRM_dbg("max86900_enable err : %d\n", err);

		data->eol_test_is_enable = 0;
	}
	HRM_info("%s - onoff = %d\n", __func__, onoff);
}

static void __max86902_eol_test_onoff(struct max869_device_data *data, int onoff)
{
	int err;

	if (onoff) {
		data->agc_mode = M_NONE;
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		data->hr_range2 = 48;
		data->look_mode_ir = 0;
		data->look_mode_red = 0;
#endif
		err = __max86902_hrm_eol_test_enable(data);
		data->eol_test_is_enable = 1;
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		data->sample_cnt = 0;
		__max869_init_selftest_data(&data->selftest_data);
		if (data->self_mode)
			data->self_divid = SELF_DIVID_100HZ;
		else
			data->self_divid = SELF_DIVID_400HZ;
#endif
		if (err != 0)
			HRM_dbg("__max86902_hrm_eol_test_enable err : %d\n", err);
	} else {
		HRM_info("%s - eol test off\n", __func__);
		err = __max86902_disable(data);
		if (err != 0)
			HRM_dbg("__max86902_disable err : %d\n", err);

		data->hr_range = 0;
		data->led_current1 = data->default_current1;
		data->led_current2 = data->default_current2;
		data->led_current3 = data->default_current3;
		data->led_current4 = data->default_current4;

		err = __max86902_init_device(data);
		if (err)
			HRM_dbg("%s max86900_init device fail err = %d\n",
				__func__, err);

		err = __max86902_hrm_enable(data);
		if (err != 0)
			HRM_dbg("max86902_enable err : %d\n", err);

		data->eol_test_is_enable = 0;
	}
	HRM_info("%s - onoff = %d\n", __func__, onoff);
}
static int __max86900_eol_test_control(struct max869_device_data *data)
{
	int err = 0;
	u8 led_current = 0;

	if (data->sample_cnt < data->hr_range2)	{
		data->hr_range = 1;
	} else if (data->sample_cnt < (data->hr_range2 + 297)) {
		if (data->eol_test_is_enable == 1) {
			/* Fake pulse */
			if (data->sample_cnt % 8 < 4) {
				data->test_current_ir++;
				data->test_current_red++;
			} else {
				data->test_current_ir--;
				data->test_current_red--;
			}

			led_current = (data->test_current_red << 4)
				| data->test_current_ir;
			err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION,
					led_current);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
					__func__);
				return -EIO;
			}
			data->hr_range = 2;
		} else if (data->eol_test_is_enable == 2) {
			data->sample_cnt = data->hr_range2 + 297 - 1;
		}
	} else if (data->sample_cnt == (data->hr_range2 + 297)) {
		/* Measure */
		err = __max869_write_reg(data, MAX86900_LED_CONFIGURATION,
				data->led_current);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
		/* 400Hz setting */
		err = __max869_write_reg(data,
				MAX86900_SPO2_CONFIGURATION, 0x51);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
	} else if (data->sample_cnt < ((data->hr_range2 + 297) + 400 * 10)) {
		data->hr_range = 3;
	} else if (data->sample_cnt == ((data->hr_range2 + 297) + 400 * 10)) {
		err = __max869_write_reg(data,
				MAX86900_LED_CONFIGURATION, data->default_current);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
#if MAX86900_SAMPLE_RATE == 1
		err = __max869_write_reg(data,
				MAX86900_SPO2_CONFIGURATION, 0x47);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
					__func__);
			return -EIO;
		}
#endif

#if MAX86900_SAMPLE_RATE == 2
		err = __max869_write_reg(data,
				MAX86900_SPO2_CONFIGURATION, 0x4E);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
					__func__);
			return -EIO;
		}
#endif

#if MAX86900_SAMPLE_RATE == 4
		err = __max869_write_reg(data,
				MAX86900_SPO2_CONFIGURATION, 0x51);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_SPO2_CONFIGURATION!\n",
					__func__);
			return -EIO;
		}
#endif
	}
	data->sample_cnt++;
	return 0;
}

static int __max86902_eol_test_control(struct max869_device_data *data)
{
	int err = 0;
	int seq1_valid_pos = data->selftest_data.seq1_data.seq1_valid_pos; //SELF_SQ1_VALID_POS
	int seq1_valid_cnt = data->selftest_data.seq1_data.seq1_valid_cnt; //SELF_SQ1_VALID_CNT
	int seq1_current = data->selftest_data.seq1_data.seq1_current; //SELF_SQ1_VALID_CNT
	int eol_step1_cnt= data->hr_range2;
	int eol_step2_cnt= (data->hr_range2 + (330*SELF_SAMPLE_SIZE));
	int eol_step3_cnt= ((data->hr_range2 + (330*SELF_SAMPLE_SIZE)) + 400);
	int eol_step4_cnt= ((data->hr_range2 + (330*SELF_SAMPLE_SIZE))+ 400 * 15);

	if (data->sample_cnt < eol_step1_cnt)	{
		data->hr_range = _EOL_STATE_TYPE_P2P_INIT;
	} else if (data->sample_cnt < eol_step2_cnt) {
		/* Fake pulse */
		if(!seq1_valid_pos && !seq1_valid_cnt){
			if (seq1_current % 8 < 4) {
				data->test_current_led1 += 0x10;
				data->test_current_led2 += 0x10;
				data->test_current_led3 += 0x10;
				data->test_current_led4 += 0x10;
			} else {
				data->test_current_led1 -= 0x10;
				data->test_current_led2 -= 0x10;
				data->test_current_led3 -= 0x10;
				data->test_current_led4 -= 0x10;
			}

			/*write LED currents ir=2, red=1, violet=4*/
			err = __max869_write_reg(data, data->led1_pa_addr,
					data->test_current_led1);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, data->led2_pa_addr,
					data->test_current_led2);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(data, MAX86902_LED4_PA,
					data->test_current_led4);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_LED4_PA!\n",
					__func__);
				return -EIO;
			}
			data->selftest_data.seq1_data.seq1_valid_pos = SELF_SQ1_VALID_POS;	
			data->selftest_data.seq1_data.seq1_valid_cnt=SELF_SQ1_VALID_CNT;
			data->selftest_data.seq1_data.seq1_current++;				
		}		
		data->hr_range = _EOL_STATE_TYPE_P2P_MODE;
	} else if (data->sample_cnt == eol_step2_cnt) {
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		/*write LED currents ir=1, red=2, violet=4*/
		err = __max869_write_reg(data, data->led1_pa_addr,
			EOL_SEQ2_LED_CURRNET);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(data, data->led2_pa_addr,
			EOL_SEQ2_LED_CURRNET);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
				__func__);
			return -EIO;
		}
#else
		/* Measure */
		/*write LED currents ir=1, red=2, violet=4*/
		err = __max869_write_reg(data, data->led1_pa_addr,
				data->led_current1);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(data, data->led2_pa_addr,
				data->led_current2);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
				__func__);
			return -EIO;
		}
#endif
		err = __max869_write_reg(data, MAX86902_LED4_PA,
				data->led_current4);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED4_PA!\n",
				__func__);
			return -EIO;
		}
		/* 400Hz setting */
		err = __max869_write_reg(data, MAX86902_SPO2_CONFIGURATION,
				0x0E | (MAX86902_SPO2_ADC_RGE << MAX86902_SPO2_ADC_RGE_OFFSET));
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		if (data->self_mode)
			err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
					(0x02 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		else
#endif
		err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
				(0x00 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
				__func__);
			return -EIO;
		}

		data->hr_range = _EOL_STATE_TYPE_FREQ_INIT;
	} else if (data->sample_cnt < eol_step3_cnt) {
		data->hr_range = _EOL_STATE_TYPE_FREQ_MODE;
	} else if (data->sample_cnt == eol_step3_cnt) {

#ifdef ENABLE_EOL_SEQ3_100HZ
		//AVERAGE 4
		err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
				(0x02 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
				__func__);
			return -EIO;
		}
#endif
		data->hr_range = _EOL_STATE_TYPE_AVERAGE_INIT; //setting the fifo mode.
	} else if (data->sample_cnt < eol_step4_cnt) {
		data->hr_range = _EOL_STATE_TYPE_AVERAGE_MODE;
	} else if (data->sample_cnt == eol_step4_cnt) {
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		/*write LED currents ir=1, red=2, violet=4*/
		err = __max869_write_reg(data, data->led1_pa_addr,
				0x50);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(data, data->led2_pa_addr,
				0x50);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
				__func__);
			return -EIO;
		}
#else
		/* Measure */
		/*write LED currents ir=1, red=2, violet=4*/
		err = __max869_write_reg(data, data->led1_pa_addr,
				data->default_current1);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(data, data->led2_pa_addr,
				data->default_current2);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
				__func__);
			return -EIO;
		}
#endif
		err = __max869_write_reg(data, MAX86902_LED4_PA,
				data->default_current4);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED4_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(data, MAX86902_SPO2_CONFIGURATION,
				0x0E | (MAX86902_SPO2_ADC_RGE << MAX86902_SPO2_ADC_RGE_OFFSET));
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
#if MAX86902_SAMPLE_RATE == 1
		err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
				(0x02 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
				__func__);
			return -EIO;
		}
#endif

#if MAX86902_SAMPLE_RATE == 2
		err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
				(0x01 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
				__func__);
			return -EIO;
		}
#endif

#if MAX86902_SAMPLE_RATE == 4
		err = __max869_write_reg(data, MAX86902_FIFO_CONFIG,
				(0x01 << MAX86902_SMP_AVE_OFFSET) & MAX86902_SMP_AVE_MASK);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_FIFO_CONFIG!\n",
				__func__);
			return -EIO;
		}
#endif
	data->hr_range = _EOL_STATE_TYPE_END;
	}

#ifdef ENABLE_EOL_SEQ3_100HZ
	if (data->hr_range == _EOL_STATE_TYPE_AVERAGE_MODE)
		data->sample_cnt += SELF_DIVID_100HZ;
	else
#endif
	data->sample_cnt++;
	return 0;
}
static int __max86900_hrm_read_data(struct max869_device_data *device, int *data)
{
	int err;
	u8 recvData[4] = { 0x00, };
	int i;
	int ret = 0;

	if (device->sample_cnt == MAX86900_COUNT_MAX)
		device->sample_cnt = 0;

	recvData[0] = MAX86900_FIFO_DATA;
	err = __max869_read_reg(device, recvData, 4);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}

	for (i = 0; i < 2; i++)	{
		data[i] = ((((int)recvData[i*2]) << 8) & 0xff00)
			| (((int)recvData[i*2+1]) & 0x00ff);
	}

	data[2] = device->led;

	if ((device->sample_cnt % 1000) == 1)
		HRM_info("%s - %u, %u, %u, %u\n", __func__,
			data[0], data[1], data[2], data[3]);

	if (device->sample_cnt == 20 && device->led == 0) {
		err = __max86900_read_temperature(device);
		if (err < 0) {
			HRM_dbg("%s - __max86900_read_temperature err : %d\n",
				__func__, err);
			return -EIO;
		}
	}

	if (device->eol_test_is_enable) {
		err = __max86900_eol_test_control(device);
		if (err < 0) {
			HRM_dbg("%s - __max86900_eol_test_control err : %d\n",
					__func__, err);
			return -EIO;
		}
	} else {
		device->ir_sum += data[0];
		device->r_sum += data[1];
		if ((device->sample_cnt % MAX86900_SAMPLE_RATE) == MAX86900_SAMPLE_RATE - 1) {
			data[0] = device->ir_sum / MAX86900_SAMPLE_RATE;
			data[1] = device->r_sum / MAX86900_SAMPLE_RATE;
			device->ir_sum = 0;
			device->r_sum = 0;
			ret = 0;
		} else
			ret = 1;

		if (device->sample_cnt++ > 100 && device->led == 0)
			device->led = 1;
	}

	return ret;
}

static int __max86902_hrm_read_data(struct max869_device_data *device, int *data)
{
	int err;
	u8 recvData[MAX_LED_NUM * NUM_BYTES_PER_SAMPLE] = { 0x00, };
	int i, j = 0;
	int ret = 0;
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
	int hr_range = 0;
#endif

	if (device->sample_cnt == MAX86902_COUNT_MAX)
		device->sample_cnt = 0;

	recvData[0] = MAX86902_FIFO_DATA;
	err = __max869_read_reg(device, recvData,
			device->num_samples * NUM_BYTES_PER_SAMPLE);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}

	for (i = 0; i < MAX_LED_NUM; i++)	{
		if (device->flex_mode | (1 << i)) {
			data[i] =  recvData[j++] << 16 & 0x30000;
			data[i] += recvData[j++] << 8;
			data[i] += recvData[j++] << 0;
		} else
			data[i] = 0;
	}

	if ((device->sample_cnt % 1000) == 1)
		HRM_info("%s - %u, %u, %u, %u\n", __func__,
			data[0], data[1], data[2], data[3]);

	if (device->sample_cnt == 20 && device->led == 0) {
		err = __max86902_read_temperature(device);
		if (err < 0) {
			HRM_dbg("%s - __max86900_read_temperature err : %d\n",
				__func__, err);
			return -EIO;
		}
	}

	if (device->eol_test_is_enable) {
		err = __max86902_eol_test_control(device);
		if (err < 0) {
			HRM_dbg("%s - __max86900_eol_test_control err : %d\n",
					__func__, err);
			return -EIO;
		}
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
		hr_range = device->hr_range;

		if ((!device->selftest_data.status)) {
			device->selftest_data.selftest_count++;

			switch (hr_range) {
			case _EOL_STATE_TYPE_P2P_INIT:
				break;

			case _EOL_STATE_TYPE_P2P_MODE:
				__max869_selftest_sequence_1(data[0], data[1], &device->selftest_data);
				break;

			case _EOL_STATE_TYPE_FREQ_MODE:
				__max869_selftest_sequence_2(data[0], data[1], &device->selftest_data, device->self_divid);
				__max869_selftest_sequence_4(data[0], data[1], &device->selftest_data, device->self_divid);
				break;

			case _EOL_STATE_TYPE_AVERAGE_MODE:
				if (device->selftest_data.seq3_data.done!=1) {
#ifdef ENABLE_EOL_SEQ3_100HZ
					__max869_selftest_sequence_3(&device->selftest_data, SELF_DIVID_100HZ);
#else
					__max869_selftest_sequence_3(&device->selftest_data, SELF_DIVID_400HZ);
#endif
					__max869_selftest_check_done(&device->selftest_data, device->self_mode, device);
				}
				break;

			case _EOL_STATE_TYPE_END:
#ifdef ENABLE_EOL_SEQ3_100HZ
				device->selftest_data.seq3_data.count *= SELF_DIVID_100HZ;
#endif
				device->selftest_data.seq3_data.done = 1;
				HRM_dbg("NEVER CALL !!!!FAIL EOL TEST  : HR_STATE: EOL_STATE_TYPE_END !!!!!!!!!!!!!!!!!!\n");
				__max869_selftest_check_done(&device->selftest_data, device->self_mode, device);
				break;

			default:
				break;
			}
		}
#endif
	} else {
		for (i = 0; i < MAX_LED_NUM; i++)
#ifdef CONFIG_SENSORS_MAX_NOTCHFILTER
			data[i] = downsample(data[i] >> 2, i) << 2;
#else
			device->led_sum[i] += data[i];
#endif

		if ((device->sample_cnt % MAX86902_SAMPLE_RATE) == MAX86902_SAMPLE_RATE - 1) {
#ifndef CONFIG_SENSORS_MAX_NOTCHFILTER
			for (i = 0; i < MAX_LED_NUM; i++) {
				data[i] = device->led_sum[i] / MAX86902_SAMPLE_RATE;
				device->led_sum[i] = 0;
			}
#endif
			ret = 0;
		} else
			ret = 1;

		if (device->sample_cnt++ > 100 && device->led == 0)
			device->led = 1;
	}

	return ret;
}

static int __max86902_awb_flicker_read_data(struct max869_device_data *device, int *data)
{
	int err;
	u8 recvData[AWB_INTERVAL * NUM_BYTES_PER_SAMPLE] = { 0x00, };
	int ret = 0;
	int mode_changed = 0;
	int i;
	u8 irq_status = 0;
	int previous_awb_data = 0;

	recvData[0] = MAX86902_INTERRUPT_STATUS;
	err = __max869_read_reg(device, recvData, 1);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}
	irq_status = recvData[0];
	if (irq_status != 0x80)
		return 1;

	recvData[0] = MAX86902_FIFO_DATA;

	err = __max869_read_reg(device, recvData, AWB_INTERVAL * NUM_BYTES_PER_SAMPLE);
	if (err != 0) {
		HRM_dbg("%s __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData[0]);
		return -EIO;
	}

	*data = (recvData[0 + (AWB_INTERVAL - 1)*NUM_BYTES_PER_SAMPLE] << 16 & 0x30000)
		+ (recvData[1 + (AWB_INTERVAL - 1)*NUM_BYTES_PER_SAMPLE] << 8)
		+ (recvData[2 + (AWB_INTERVAL - 1)*NUM_BYTES_PER_SAMPLE] << 0);

	previous_awb_data = (recvData[0 + (AWB_INTERVAL - 2)*NUM_BYTES_PER_SAMPLE] << 16 & 0x30000)
		+ (recvData[1 + (AWB_INTERVAL - 2)*NUM_BYTES_PER_SAMPLE] << 8)
		+ (recvData[2 + (AWB_INTERVAL - 2)*NUM_BYTES_PER_SAMPLE] << 0);

	if (device->awb_sample_cnt > CONFIG_SKIP_CNT) {
		mutex_lock(&device->flickerdatalock);
		for (i = 0; i < AWB_INTERVAL; i++) {
			if (device->flicker_data_cnt < FLICKER_DATA_CNT) {
				device->flicker_data[device->flicker_data_cnt++] =
					(recvData[0 + i*NUM_BYTES_PER_SAMPLE] << 16 & 0x30000)
					+ (recvData[1 + i*NUM_BYTES_PER_SAMPLE] << 8)
					+ (recvData[2 + i*NUM_BYTES_PER_SAMPLE] << 0);
			}
		}
		mutex_unlock(&device->flickerdatalock);
	}

	/* Change Configuation */
	if (device->awb_flicker_status == AWB_CONFIG0
		&& device->awb_sample_cnt > CONFIG_SKIP_CNT) {
		if (*data > AWB_CONFIG_TH1
			&& previous_awb_data > AWB_CONFIG_TH1) { /* Change to AWB_CONFIG1 (*/
			err = __max869_write_reg(device, 0xFF, 0x54);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0xFF, 0x4d);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0x8f, 0x01); /* PW_EN = 0 */
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x01!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0xFF, 0x00);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST3!\n",
					__func__);
				return -EIO;
			}
			device->awb_flicker_status = AWB_CONFIG1;
			mode_changed = 1;
		}
	} else if (device->awb_flicker_status == AWB_CONFIG1) {
		if (*data < AWB_CONFIG_TH4) { /* Change to AWB_CONFIG0 */
			err = __max869_write_reg(device, 0xFF, 0x54);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0xFF, 0x4d);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0x8f, 0x81); /* PW_EN = 1 */
			if (err != 0) {
				HRM_dbg("%s - error initializing 0x01!\n",
					__func__);
				return -EIO;
			}
			err = __max869_write_reg(device, 0xFF, 0x00);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86900_MODE_TEST3!\n",
					__func__);
				return -EIO;
			}
			device->awb_flicker_status = AWB_CONFIG0;
			mode_changed = 1;
		} else if (*data > AWB_CONFIG_TH2
			&& previous_awb_data > AWB_CONFIG_TH2) { /* Change to AWB_CONFIG2 */
			/* 16384 uA setting */
			err = __max869_write_reg(device,
				MAX86902_SPO2_CONFIGURATION, 0x6F);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
					__func__);
				return -EIO;
			}
			device->awb_flicker_status = AWB_CONFIG2;
			mode_changed = 1;
		}
	} else if (device->awb_flicker_status == AWB_CONFIG2) {
		if (*data < AWB_CONFIG_TH3
			&& previous_awb_data < AWB_CONFIG_TH3) { /* Change to AWB_CONFIG1 */
			/* 2048 uA setting */
			err = __max869_write_reg(device,
				MAX86902_SPO2_CONFIGURATION, 0x0F);
			if (err != 0) {
				HRM_dbg("%s - error initializing MAX86902_SPO2_CONFIGURATION!\n",
					__func__);
				return -EIO;
			}
			device->awb_flicker_status = AWB_CONFIG1;
			mode_changed = 1;
		}
	}

	if (device->awb_sample_cnt > CONFIG_SKIP_CNT) {
		if (device->awb_flicker_status < AWB_CONFIG2)
			*data = *data >> 3; /* 2uA setting should devided by 8 */
		ret = 0;
	} else
		ret = 1;

	if (mode_changed == 1) {
		/* Flush Buffer */
		err = __max869_write_reg(device,
			MAX86902_MODE_CONFIGURATION, 0x02);
		if (err != 0) {
			HRM_dbg("%s - error init MAX86902_MODE_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
		device->flicker_data_cnt = 0;
		device->awb_sample_cnt = 0;
		ret = 1;
		HRM_info("%s - mode changed to : %d\n", __func__, device->awb_flicker_status);
	} else
		device->awb_sample_cnt += AWB_INTERVAL;

	return ret;
}

int max869_i2c_read(u32 reg, u32 *value, u32 *size)
{
	int err;

	*value = reg;
	err = __max869_read_reg(max869_data, (u8 *)value, 1);

	*size = 1;

	return err;
}

int max869_i2c_write(u32 reg, u32 value)
{
	int err;

	err = __max869_write_reg(max869_data, (u8)reg, (u8)value);

	return err;
}

static long __max869_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = 0;

	struct max869_device_data *data = container_of(file->private_data,
	struct max869_device_data, miscdev);

	HRM_info("%s - ioctl start\n", __func__);
	mutex_lock(&data->flickerdatalock);

	switch (cmd) {
	case MAX86900_IOCTL_READ_FLICKER:
		ret = copy_to_user(argp,
			data->flicker_data,
			sizeof(int)*FLICKER_DATA_CNT);

		if (unlikely(ret))
			goto ioctl_error;

		break;

	default:
		HRM_dbg("%s - invalid cmd\n", __func__);
		break;
	}

	mutex_unlock(&data->flickerdatalock);
	return ret;

ioctl_error:
	mutex_unlock(&data->flickerdatalock);
	HRM_dbg("%s - read flicker data err(%d)\n", __func__, ret);
	return -ret;
}

static const struct file_operations __max869_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.unlocked_ioctl = __max869_ioctl,
};


static void __max869_init_agc_settings(struct max869_device_data *data)
{
	data->agc_is_enable = true;

	if (data->part_type < PART_TYPE_MAX86902A) {
		data->update_led = max86900_update_led_current;
		data->agc_led_out_percent = MAX86900_AGC_DEFAULT_LED_OUT_RANGE;
		data->agc_corr_coeff = MAX86900_AGC_DEFAULT_CORRECTION_COEFF;
		data->agc_min_num_samples = MAX86900_AGC_DEFAULT_MIN_NUM_PERCENT;
		data->agc_sensitivity_percent = MAX86900_AGC_DEFAULT_SENSITIVITY_PERCENT;
		data->threshol_default = MAX86900_THRESHOLD_DEFAULT;
	} else {
		data->update_led = max86902_update_led_current;
		data->agc_led_out_percent = MAX86902_AGC_DEFAULT_LED_OUT_RANGE;
		data->agc_corr_coeff = MAX86902_AGC_DEFAULT_CORRECTION_COEFF;
		data->agc_min_num_samples = MAX86902_AGC_DEFAULT_MIN_NUM_PERCENT;
		data->agc_sensitivity_percent = MAX86902_AGC_DEFAULT_SENSITIVITY_PERCENT;
		data->threshol_default = MAX86902_THRESHOLD_DEFAULT;
	}
}

static int __max869_trim_check(void)
{
	u8 recvData;
	u8 reg97_val;
	int err;

	max869_data->isTrimmed = 0;

	err = __max869_write_reg(max869_data, 0xFF, 0x54);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
			__func__);
		return -EIO;
	}

	err = __max869_write_reg(max869_data, 0xFF, 0x4d);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
			__func__);
		return -EIO;
	}

	recvData = 0x97;
	if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
		HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}
	reg97_val = recvData;

	err = __max869_write_reg(max869_data, 0xFF, 0x00);
	if (err != 0) {
		HRM_dbg("%s - error initializing MAX86900_MODE_TEST3!\n",
			__func__);
		return -EIO;
	}

	max869_data->isTrimmed = (reg97_val & 0x20);
	HRM_dbg("%s isTrimmed :%d, reg97_val : %d\n", __func__, max869_data->isTrimmed, reg97_val);

	return err;
}

static int __max869_init_var(struct max869_device_data *data)
{
	int err;
	u8 buffer[2] = {0, };

	buffer[0] = MAX86902_WHOAMI_REG_PART;
	err = __max869_read_reg(data, buffer, 1);

	data->led1_pa_addr = MAX86902_LED1_PA;
	data->led2_pa_addr = MAX86902_LED2_PA;
	data->ir_led_ch = IR_LED_CH;
	data->red_led_ch = RED_LED_CH;
	data->ir_led = MAX869_LED1;
	data->red_led = MAX869_LED2;
	data->i2c_err_cnt = 0;
	data->ir_curr = 0;
	data->red_curr = 0;
	data->ir_adc = 0;
	data->red_adc = 0;

	if (buffer[0] == MAX86902_PART_ID1 ||
		buffer[0] == MAX86902_PART_ID2) {/*Max86902*/
		buffer[0] = MAX86902_WHOAMI_REG_REV;
		err = __max869_read_reg(data, buffer, 1);
		if (err) {
			HRM_dbg("%s Max86902 WHOAMI read fail\n", __func__);
			err = -ENODEV;
			goto err_of_read_chipid;
		}
		if (buffer[0] == MAX86902_REV_ID1)
			data->part_type = PART_TYPE_MAX86902A;
		else if (buffer[0] == MAX86902_REV_ID2) {
			if (__max869_otp_id(data) == MAX86907_OTP_ID)
				data->part_type = PART_TYPE_MAX86907;
			else if (__max869_otp_id(data) == MAX86907A_OTP_ID)
				data->part_type = PART_TYPE_MAX86907A;
			else if (__max869_otp_id(data) == MAX86907B_OTP_ID) {
				data->part_type = PART_TYPE_MAX86907B;
				data->led1_pa_addr = MAX86902_LED2_PA;
				data->led2_pa_addr = MAX86902_LED1_PA;
				data->ir_led_ch = RED_LED_CH;
				data->red_led_ch = IR_LED_CH;
				data->ir_led = MAX869_LED2;
				data->red_led = MAX869_LED1;
			} else if (__max869_otp_id(data) == MAX86907E_OTP_ID) {
				data->part_type = PART_TYPE_MAX86907E;
				data->led1_pa_addr = MAX86902_LED2_PA;
				data->led2_pa_addr = MAX86902_LED1_PA;
				data->ir_led_ch = RED_LED_CH;
				data->red_led_ch = IR_LED_CH;
				data->ir_led = MAX869_LED2;
				data->red_led = MAX869_LED1;
			} else
				data->part_type = PART_TYPE_MAX86902B;
		} else {
			HRM_dbg("%s Max86902 WHOAMI read error : REV ID : 0x%02x\n",
					__func__, buffer[0]);
					err = -ENODEV;
					goto err_of_read_chipid;
		}
		data->default_current1 = MAX86902_DEFAULT_CURRENT1;
		data->default_current2 = MAX86902_DEFAULT_CURRENT2;
		data->default_current3 = MAX86902_DEFAULT_CURRENT3;
		data->default_current4 = MAX86902_DEFAULT_CURRENT4;
	} else {
		data->client->addr = MAX86900A_SLAVE_ADDR;
		buffer[0] = MAX86900_WHOAMI_REG;
		err = __max869_read_reg(data, buffer, 2);

		if (buffer[1] == MAX86900C_WHOAMI) {
			/* MAX86900A & MAX86900B */
			switch (buffer[0]) {
			case MAX86900A_REV_ID:
				data->part_type = PART_TYPE_MAX86900A;
				data->default_current =	MAX86900A_DEFAULT_CURRENT;
				break;
			case MAX86900B_REV_ID:
				data->part_type = PART_TYPE_MAX86900B;
				data->default_current = MAX86900A_DEFAULT_CURRENT;
				break;
			case MAX86900C_REV_ID:
				if (__max869_otp_id(data) == MAX86906_OTP_ID) {
					data->part_type = PART_TYPE_MAX86906;
					data->default_current = MAX86906_DEFAULT_CURRENT;
				} else {
					data->part_type = PART_TYPE_MAX86900C;
					data->default_current = MAX86900C_DEFAULT_CURRENT;
				}
				break;
			default:
				HRM_dbg("%s WHOAMI read error : REV ID : 0x%02x\n",
				__func__, buffer[0]);
				err = -ENODEV;
				goto err_of_read_chipid;
			}
			HRM_info("%s - MAX86900 OS21(0x%X), REV ID : 0x%02x\n",
				__func__, MAX86900A_SLAVE_ADDR, buffer[0]);
		} else {
			/* MAX86900 */
			data->client->addr = MAX86900_SLAVE_ADDR;
			buffer[0] = MAX86900_WHOAMI_REG;
			err = __max869_read_reg(data, buffer, 2);

			if (err) {
				HRM_dbg("%s WHOAMI read fail\n", __func__);
				err = -ENODEV;
				goto err_of_read_chipid;
			}
			data->part_type = PART_TYPE_MAX86900;
			data->default_current = MAX86900_DEFAULT_CURRENT;
			HRM_info("%s - MAX86900 OS20 (0x%X)\n", __func__,
					MAX86900_SLAVE_ADDR);
		}
	}

	//AGC setting
	__max869_init_agc_settings(data);

	return 0;

err_of_read_chipid:
	return -EINVAL;
}

int max869_init_device(struct i2c_client *client)
{
	int err = 0;

	HRM_info("%s client  = %p\n", __func__, client);

	/* allocate some memory for the device */
	max869_data = kzalloc(sizeof(struct max869_device_data), GFP_KERNEL);
	if (max869_data == NULL) {
		HRM_dbg("%s - couldn't allocate memory\n", __func__);
		return -ENOMEM;
	}

	max869_data->miscdev.minor = MISC_DYNAMIC_MINOR;
	max869_data->miscdev.name = "max_hrm";
	max869_data->miscdev.fops = &__max869_fops;
	max869_data->miscdev.mode = S_IRUGO;
	err = misc_register(&max869_data->miscdev);
	if (err < 0) {
		HRM_dbg("%s - failed to register Device\n", __func__);
		goto err_register_fail;
	}

	max869_data->flicker_data = kzalloc(sizeof(int)*FLICKER_DATA_CNT, GFP_KERNEL);
	if (max869_data->flicker_data == NULL) {
		HRM_dbg("%s - couldn't allocate flicker memory\n", __func__);
		goto err_flicker_alloc_fail;
	}

	mutex_init(&max869_data->flickerdatalock);

	max869_data->agc_threshold = MAX86902_THRESHOLD_DEFAULT;
	max869_data->client = client;
#ifdef CONFIG_SENSORS_ENABLE_SELFTEST
	max869_data->self_mode = SELF_MODE_400HZ;
#endif

	err = __max869_init_var(max869_data);
	if (err < 0) {
		err = -EIO;
		goto err_init_fail;
	}

	if (__max869_init(max869_data) < 0) {
		err = -EIO;
		HRM_dbg("%s __max869_init failed\n", __func__);
		goto err_init_fail;
	}

	__max869_trim_check();

	goto done;

err_init_fail:
	mutex_destroy(&max869_data->flickerdatalock);
	kfree(max869_data->flicker_data);
err_flicker_alloc_fail:
	misc_deregister(&max869_data->miscdev);
err_register_fail:
	kfree(max869_data);
	HRM_dbg("%s failed\n", __func__);

done:
	return err;
}


int max869_deinit_device(void)
{
	mutex_destroy(&max869_data->flickerdatalock);
	misc_deregister(&max869_data->miscdev);

	if (max869_data->flicker_data != NULL)
		kfree(max869_data->flicker_data);

	if (max869_data != NULL) {
		kfree(max869_data);
	}
	if (max869_data != NULL)
		return -ENODEV;
	return 0;
}


int max869_enable(enum hrm_mode mode)
{
	int err = 0;

	max869_data->hrm_mode = mode;

	HRM_dbg("%s - enable_m : 0x%x\t cur_m : 0x%x\t cur_p : 0x%x\n",
		__func__, mode, max869_data->hrm_mode, max869_data->part_type);

	__max869_print_mode(max869_data->hrm_mode);

	if (mode == MODE_HRM)
		err = __max869_set_reg_hrm(max869_data);
	else if (mode == MODE_AMBIENT)
		err = __max869_set_reg_ambient(max869_data);
	else if (mode == MODE_PROX)
		err = __max869_set_reg_prox(max869_data);
	else if (mode == MODE_HRMLED_IR)
		err = __max869_set_reg_hrmled_ir(max869_data);
	else if (mode == MODE_HRMLED_RED)
		err = __max869_set_reg_hrmled_red(max869_data);
	else if (mode == MODE_HRMLED_BOTH)
		err = __max869_set_reg_hrmled_both(max869_data);
	else
		HRM_dbg("%s - MODE_UNKNOWN\n", __func__);

	if (max869_data->agc_mode != M_NONE) {
		agc_pre_s = S_UNKNOWN;
		agc_cur_s = S_INIT;
	}

	agc_count = 0;

	return err;
}

int max869_disable(enum hrm_mode mode)
{
	int err = 0;

	max869_data->hrm_mode = mode;
	HRM_info("%s - disable_m : 0x%x\t cur_m : 0x%x\n", __func__, mode, max869_data->hrm_mode);
	__max869_print_mode(max869_data->hrm_mode);

	if (max869_data->hrm_mode == 0) {
		__max869_disable(max869_data);
		return err;
	}

	if (max869_data->agc_mode != M_NONE) {
		agc_pre_s = S_UNKNOWN;
		agc_cur_s = S_INIT;
	}

	return err;
}

int max869_get_current(u8 *d1, u8 *d2, u8 *d3, u8 *d4)
{
	*d1 = max869_data->led_current2; /* IR */
	*d2 = max869_data->led_current1; /* RED */
	*d3 = 0;
	*d4 = 0;

	return 0;
}

int max869_set_current(u8 d1, u8 d2, u8 d3, u8 d4)
{
	int err = 0;
	max869_data->led_current1 = d2; /* set red; */
	max869_data->led_current2 = d1; /* set ir; */

	HRM_info("%s - 0x%x 0x%x\n", __func__, max869_data->led_current2, max869_data->led_current1);

	if (max869_data->part_type < PART_TYPE_MAX86902A) {
		err = __max869_write_reg(max869_data, MAX86900_LED_CONFIGURATION,
			((max869_data->led_current1&0xf)<<4)|(max869_data->led_current2&0xf));
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_LED_CONFIGURATION!\n",
				__func__);
			return -EIO;
		}
	} else {
		err = __max869_write_reg(max869_data, max869_data->led1_pa_addr, max869_data->led_current1);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED1_PA!\n",
				__func__);
			return -EIO;
		}
		err = __max869_write_reg(max869_data, max869_data->led2_pa_addr, max869_data->led_current2);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86902_LED2_PA!\n",
				__func__);
			return -EIO;
		}
	}

	max869_data->default_current1 = max869_data->led_current1;
	max869_data->default_current2 = max869_data->led_current2;
	return err;
}


int max869_read_data(struct hrm_output_data *data)
{
	int err = 0;
	int ret;
	int raw_data[4] = {0x00, };
	u8 recvData;
	s32 d1 = 0, d2 = 0, d3 = 0;

	data->sub_num = 0;

	if (max869_data->part_type < PART_TYPE_MAX86902A) {
		err = __max86900_hrm_read_data(max869_data, raw_data);
		if (err < 0)
			HRM_dbg("max86900_hrm_read_data err : %d\n", err);
	} else {
		if (max869_data->hrm_mode == MODE_AMBIENT)
			err = __max86902_awb_flicker_read_data(max869_data, raw_data);
		else
			err = __max86902_hrm_read_data(max869_data, raw_data);

		if (err < 0)
			HRM_dbg("__max86902_hrm_read_data err : %d\n", err);
	}

	if (err == 0) {
		data->main_num = 2;
		if (max869_data->hrm_mode == MODE_AMBIENT) {
			d1 = data->data_main[0] = raw_data[0];
			if (max869_data->flicker_data_cnt == FLICKER_DATA_CNT) {
				d2 = data->data_main[1] = -2;
				d3 = 0;
				max869_data->flicker_data_cnt = 0;
			} else {
				d2 = data->data_main[1] = 0;
				d3 = 0;
			}
		} else {
			data->main_num = 3;
			d1 = data->data_main[0] = raw_data[0];
			d2 = data->data_main[1] = raw_data[1];
			d3 = data->data_main[2] = max869_data->hrm_temp;
		}
		data->mode = max869_data->hrm_mode;
		ret = 0;
	} else
		ret = 1;

	/* Interrupt Clear */
	recvData = MAX86902_INTERRUPT_STATUS;
	err = __max869_read_reg(max869_data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}

	/* Interrupt2 Clear */
	recvData = MAX86902_INTERRUPT_STATUS_2;
	err = __max869_read_reg(max869_data, &recvData, 1);
	if (err != 0) {
		HRM_dbg("%s - __max869_read_reg err:%d, address:0x%02x\n",
			__func__, err, recvData);
		return -EIO;
	}

	if (max869_data->agc_mode != M_NONE
			&& agc_is_enabled
			&& !max869_data->eol_test_is_enable)
		__max869_cal_agc(raw_data[0], raw_data[1], 0);

	return ret;
}

int max869_get_chipid(u64 *chip_id)
{
	u8 recvData;
	int err;
	int low = 0;
	int high = 0;
	int clock_code = 0;
	int VREF_trim_code = 0;
	int IREF_trim_code = 0;
	int UVL_trim_code = 0;
	int SPO2_trim_code = 0;
	int ir_led_code = 0;
	int red_led_code = 0;
	int TS_trim_code = 0;

	u8 reg_0x88;
	u8 reg_0x89;
	u8 reg_0x8A;
	u8 reg_0x90;
	u8 reg_0x98;
	u8 reg_0x99;
	u8 reg_0x9D;

	*chip_id = 0;

	if (max869_data->part_type < PART_TYPE_MAX86902A) {
		err = __max869_write_reg(max869_data, 0xFF, 0x54);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
				__func__);
			return -EIO;
		}

		err = __max869_write_reg(max869_data, 0xFF, 0x4d);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
				__func__);
			return -EIO;
		}

		recvData = 0x88;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x88 = recvData;

		recvData = 0x89;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x89 = recvData;

		recvData = 0x8A;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x8A = recvData;

		recvData = 0x90;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x90 = recvData;

		recvData = 0x98;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x98 = recvData;

		recvData = 0x99;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x99 = recvData;

		recvData = 0x9D;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		reg_0x9D = recvData;

		err = __max869_write_reg(max869_data, 0xFF, 0x00);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
				__func__);
			return -EIO;
		}

		*chip_id = reg_0x88 * 16 + (reg_0x89 & 0x0F);
		*chip_id = *chip_id * 16 + ((reg_0x8A & 0xF0) >> 4);
		*chip_id = *chip_id * 16 + (reg_0x8A & 0x0F);
		*chip_id = *chip_id * 128 + reg_0x90;
		*chip_id = *chip_id * 64 + reg_0x99;
		*chip_id = *chip_id * 64 + reg_0x98;
		*chip_id = *chip_id * 16 + reg_0x9D;
	} else {
		err = __max869_write_reg(max869_data, 0xFF, 0x54);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
				__func__);
			return -EIO;
		}

		err = __max869_write_reg(max869_data, 0xFF, 0x4d);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST1!\n",
				__func__);
			return -EIO;
		}

		recvData = 0x8B;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		high = recvData;

		recvData = 0x8C;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		low = recvData;

		recvData = 0x88;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		clock_code = recvData;

		recvData = 0x89;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		VREF_trim_code = recvData & 0x1F;

		recvData = 0x8A;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		IREF_trim_code = (recvData >> 4) & 0x0F;
		UVL_trim_code = recvData & 0x0F;

		recvData = 0x90;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		SPO2_trim_code = recvData & 0x7F;

		recvData = 0x98;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		ir_led_code = (recvData >> 4) & 0x0F;
		red_led_code = recvData & 0x0F;

		recvData = 0x9D;
		if ((err = __max869_read_reg(max869_data, &recvData, 1)) != 0) {
			HRM_dbg("%s - max86900_read_reg err:%d, address:0x%02x\n",
				__func__, err, recvData);
			return -EIO;
		}
		TS_trim_code = recvData;

		err = __max869_write_reg(max869_data, 0xFF, 0x00);
		if (err != 0) {
			HRM_dbg("%s - error initializing MAX86900_MODE_TEST0!\n",
				__func__);
			return -EIO;
		}

		*chip_id = clock_code * 16 + VREF_trim_code;
		*chip_id = *chip_id * 16 + IREF_trim_code;
		*chip_id = *chip_id * 16 + UVL_trim_code;
		*chip_id = *chip_id * 128 + SPO2_trim_code;
		*chip_id = *chip_id * 64 + ir_led_code;
		*chip_id = *chip_id * 64 + red_led_code;
		*chip_id = *chip_id * 16 + TS_trim_code;
	}
	HRM_info("%s - Device ID = %lld\n", __func__, *chip_id);

	return 0;
}

int max869_get_part_type(u16 *part_type)
{
	int err;
	u8 buffer[2] = {0, };

	buffer[0] = MAX86902_WHOAMI_REG_PART;
	err = __max869_read_reg(max869_data, buffer, 1);

	if (buffer[0] == MAX86902_PART_ID1 ||
		buffer[0] == MAX86902_PART_ID2) {/*Max86902*/
		buffer[0] = MAX86902_WHOAMI_REG_REV;
		err = __max869_read_reg(max869_data, buffer, 1);
		if (err) {
			HRM_dbg("%s Max86902 WHOAMI read fail\n", __func__);
			err = -ENODEV;
			goto err_of_read_part_type;
		}
		if (buffer[0] == MAX86902_REV_ID1)
			max869_data->part_type = PART_TYPE_MAX86902A;
		else if (buffer[0] == MAX86902_REV_ID2) {
			if (__max869_otp_id(max869_data) == MAX86907_OTP_ID)
				max869_data->part_type = PART_TYPE_MAX86907;
			else if (__max869_otp_id(max869_data) == MAX86907A_OTP_ID)
				max869_data->part_type = PART_TYPE_MAX86907A;
			else if (__max869_otp_id(max869_data) == MAX86907B_OTP_ID)
				max869_data->part_type = PART_TYPE_MAX86907B;
			else if (__max869_otp_id(max869_data) == MAX86907E_OTP_ID)
				max869_data->part_type = PART_TYPE_MAX86907E;
			else
				max869_data->part_type = PART_TYPE_MAX86902B;
		} else {
			HRM_dbg("%s Max86902 WHOAMI read error : REV ID : 0x%02x\n",
				__func__, buffer[0]);
			err = -ENODEV;
			goto err_of_read_part_type;
		}
	} else {
		max869_data->client->addr = MAX86900A_SLAVE_ADDR;
		buffer[0] = MAX86900_WHOAMI_REG;
		err = __max869_read_reg(max869_data, buffer, 2);

		if (buffer[1] == MAX86900C_WHOAMI) {
			/* MAX86900A & MAX86900B */
			switch (buffer[0]) {
			case MAX86900A_REV_ID:
				max869_data->part_type = PART_TYPE_MAX86900A;
				break;

			case MAX86900B_REV_ID:
				max869_data->part_type = PART_TYPE_MAX86900B;
				break;

			case MAX86900C_REV_ID:
				if (__max869_otp_id(max869_data) == MAX86906_OTP_ID)
					max869_data->part_type = PART_TYPE_MAX86906;
				else
					max869_data->part_type = PART_TYPE_MAX86900C;
				break;

			default:
				HRM_dbg("%s WHOAMI read error : REV ID : 0x%02x\n",
				__func__, buffer[0]);
				err = -ENODEV;
				goto err_of_read_part_type;
			}

			HRM_info("%s - MAX86900 OS21(0x%X), REV ID : 0x%02x\n",
				__func__, MAX86900A_SLAVE_ADDR, buffer[0]);

		} else {
			/* MAX86900 */
			max869_data->client->addr = MAX86900_SLAVE_ADDR;
			buffer[0] = MAX86900_WHOAMI_REG;
			err = __max869_read_reg(max869_data, buffer, 2);

			if (err) {
				HRM_dbg("%s WHOAMI read fail\n", __func__);
				err = -ENODEV;
				goto err_of_read_part_type;
			}
			max869_data->part_type = PART_TYPE_MAX86900;

			HRM_info("%s - MAX86900 OS20 (0x%X)\n", __func__,
					MAX86900_SLAVE_ADDR);
		}
	}

	*part_type = max869_data->part_type;

	return err;

err_of_read_part_type:
	return -EINVAL;
}

int max869_get_i2c_err_cnt(u32 *err_cnt)
{
	int err = 0;

	*err_cnt = max869_data->i2c_err_cnt;

	return err;
}

int max869_get_curr_adc(u16 *ir_curr, u16 *red_curr, u32 *ir_adc, u32 *red_adc)
{
	int err = 0;

	*ir_curr = max869_data->ir_curr;
	*red_curr = max869_data->red_curr;
	*ir_adc = max869_data->ir_adc;
	*red_adc = max869_data->red_adc;

	return err;
}

int max869_get_name_chipset(char *name)
{
	if (max869_data->part_type < PART_TYPE_MAX86902A)
		strlcpy(name, MAX86900_CHIP_NAME, strlen(MAX86900_CHIP_NAME) + 1);
	else if (max869_data->part_type < PART_TYPE_MAX86907)
		strlcpy(name, MAX86902_CHIP_NAME, strlen(MAX86902_CHIP_NAME) + 1);
	else if (max869_data->part_type < PART_TYPE_MAX86907A)
		strlcpy(name, MAX86907_CHIP_NAME, strlen(MAX86907_CHIP_NAME) + 1);
	else if (max869_data->part_type < PART_TYPE_MAX86907B)
		strlcpy(name, MAX86907A_CHIP_NAME, strlen(MAX86907A_CHIP_NAME) + 1);
	else
		strlcpy(name, MAX86907B_CHIP_NAME, strlen(MAX86907B_CHIP_NAME) + 1);

	return 0;
}

int max869_get_name_vendor(char *name)
{
	strlcpy(name, VENDOR, strlen(VENDOR) + 1);
	if (strncmp(name, VENDOR, strlen(VENDOR) + 1))
		return -EINVAL;
	else
		return 0;
}

int max869_get_threshold(s32 *threshold)
{
	*threshold = max869_data->agc_threshold;
	if (*threshold != max869_data->agc_threshold)
		return -EINVAL;
	else
		return 0;
}

int max869_set_threshold(s32 threshold)
{
	max869_data->agc_threshold = threshold;
	if (threshold != max869_data->agc_threshold)
		return -EINVAL;
	else
		return 0;
}

int max869_set_eol_enable(u8 enable)
{
	int err = 0;

	if (max869_data->part_type < PART_TYPE_MAX86902A)
		__max86900_eol_test_onoff(max869_data, enable);
	else
		__max86902_eol_test_onoff(max869_data, enable);

	return err;
}

int max869_get_eol_result(char *result)
{
	if (max869_data->eol_test_status == 0) {
		HRM_info("%s - max869_data->eol_test_status is 0\n",
			__func__);
		max869_data->eol_test_status = 0;
		return snprintf(result, MAX_EOL_RESULT, "%s\n", "NO_EOL_TEST");
	}

	HRM_info("%s - result = %d\n", __func__, max869_data->eol_test_status);
	max869_data->eol_test_status = 0;


	snprintf(result, MAX_EOL_RESULT, max869_data->eol_test_result);

	return 0;
}

int max869_get_eol_status(u8 *status)
{
	*status = max869_data->eol_test_status;
	if (*status != max869_data->eol_test_status)
		return -EINVAL;
	else
		return 0;
}

int max869_debug_set(u8 mode)
{
	HRM_info("%s - mode = %d\n", __func__, mode);

	switch (mode) {
	case DEBUG_WRITE_REG_TO_FILE:
		__max869_write_reg_to_file();
		break;
	case DEBUG_WRITE_FILE_TO_REG:
		__max869_write_file_to_reg();
		__max869_write_default_regs();
		break;
	case DEBUG_SHOW_DEBUG_INFO:
		break;
	case DEBUG_ENABLE_AGC:
		agc_is_enabled = 1;
		break;
	case DEBUG_DISABLE_AGC:
		agc_is_enabled = 0;
		break;
	default:
		break;
	}
	return 0;
}

int max869_get_fac_cmd(char *cmd_result)
{
	HRM_dbg("%s isTrimmed :%d\n", __func__, max869_data->isTrimmed);
	if (max869_data->isTrimmed)
		return snprintf(cmd_result, PAGE_SIZE, "%d", 1);
	else
		return snprintf(cmd_result, PAGE_SIZE, "%d", 0);
}
