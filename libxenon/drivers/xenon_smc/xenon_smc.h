#ifndef __XENON_SMC
#define __XENON_SMC

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// http://free60.org/Media_Remote

enum ir_remote_codes {
IR_NUM_0,IR_NUM_1,		// 0x00 - 0x01
IR_NUM_2,IR_NUM_3,		// 0x02 - 0x03
IR_NUM_4,IR_NUM_5,		// 0x04 - 0x05
IR_NUM_6,IR_NUM_7,		// 0x06 - 0x07
IR_NUM_8,IR_NUM_9, 		// 0x08 - 0x09
IR_CLEAR,			// 0x0A
IR_ENTER,			// 0x0B
IR_UNKNOWN_0,			// 0x0C
IR_WIN_MEDIA,			// 0x0D
IR_MUTE,			// 0x0E
IR_INFO,			// 0x0F
IR_VOLUME_UP, 			// 0x10
IR_VOLUME_DOWN, 		// 0x11
IR_FAST_FORWARD = 		0x14,
IR_FAST_REWIND = 		0x15,
IR_PLAY = 			0x16,
IR_REC =			0x17,
IR_PAUSE =			0x18,
IR_STOP =			0x19,
IR_GOTO_END =			0x1A,
IR_GOTO_START =			0x1B,
IR_UNKNOWN_1 =			0x1C,
IR_100 =			0x1D,
IR_UP =				0x1E,
IR_DOWN =			0x1F,
IR_LEFT =			0x20,
IR_RIGHT =			0x21,
IR_OK =				0x22,
IR_BACK =			0x23,
IR_DVD_MENU =			0x24,
IR_BTN_B =			0x25,
IR_BTN_Y =			0x26,
IR_DISPLAY =			0x4F,
IR_TITLE =			0x51,
IR_GUIDE =			0x64,
IR_BTN_A =			0x66,
IR_BTN_X =			0x68,
IR_CH_UP =			0x6C,
IR_CH_DOWN =			0x6D };

void xenon_smc_send_message(const unsigned char *msg);
int xenon_smc_receive_message(unsigned char *msg);
int xenon_smc_receive_response(unsigned char *msg);
int xenon_smc_poll();
int xenon_smc_get_ir();

int xenon_smc_ana_write(uint8_t addr, uint32_t val);
int xenon_smc_ana_read(uint8_t addr, uint32_t *val);

int xenon_smc_i2c_ddc_lock(int lock);
int xenon_smc_i2c_write(uint16_t addr, uint8_t val);
int xenon_smc_i2c_read(uint16_t addr, uint8_t *val);

void xenon_smc_set_led(int override, int value);
void xenon_smc_set_power_led(int override, int state, int startanim);
void xenon_smc_power_shutdown(void);
void xenon_smc_power_reboot(void);
void xenon_smc_start_bootanim(void);
void xenon_smc_set_fan_algorithm(int algorithm);

void xenon_smc_query_sensors(uint16_t *data);
int xenon_smc_read_avpack(void);

enum avpack_types {
    AV_NONE = 0b111,
    AV_VGA = 0b110,
    AV_COMPOSITE = 0b101,
    AV_AUDIO_ONLY = 0b100,
    AV_COMPONENT_HD = 0b011,
    AV_SVIDEO = 0b010,
    AV_SCART_RGB = 0b001,
    AV_COMPONENT_TV = 0b000
};
#define xenon_avpack_type(pack) ((pack >> 2) & 7)

// do not use on Xenon/ANA (DVE version 0x01 / < 0x11)
#define xenon_avpack_is_hdmi(pack) (!((pack >> 6) & 1))

#ifdef __cplusplus
};
#endif

#endif
