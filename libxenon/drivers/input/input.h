#ifndef __input_h
#define __input_h

#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */

#include <stdint.h>

#define CTRL_TYPE_NONE 0x0
#define CTRL_TYPE_GAMEPAD 0x1
#define CTRL_TYPE_WHEEL 0x2
#define CTRL_TYPE_ARCADE 0x3
#define CTRL_TYPE_FLIGHT 0x4
#define CTRL_TYPE_DANCE 0x5
#define CTRL_TYPE_GUITAR 0x6
#define CTRL_TYPE_GUITAR_ALT 0x7
#define CTRL_TYPE_DRUMS 0x8
#define CTRL_TYPE_GUITAR_BASS 0xB
#define CTRL_TYPE_ARCADE_PAD 0x13

struct controller_data_s
{
	signed short s1_x, s1_y, s2_x, s2_y;
	int s1_z, s2_z, lb, rb, start, back, a, b, x, y, up, down, left, right;
	unsigned char lt, rt;
	int logo;
};

int get_controller_data(struct controller_data_s *d, int port);

void set_controller_data(int port, const struct controller_data_s *d);

int get_controller_type(int port);

void set_controller_type(int port, int type);

void set_controller_rumble(int port, uint8_t l, uint8_t r);

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif
