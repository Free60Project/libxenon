#include "xenon_smc.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <pci/io.h>

#define SMC_BASE 0xea001000

void xenon_smc_send_message(const unsigned char *msg)
{
/*	printf("SEND: ");
	int i;
	for (i = 0; i < 16; ++i)
		printf("%02x ", msg[i]);
	printf("\n");
*/
	while (!(read32(SMC_BASE + 0x84) & 4));
	write32(SMC_BASE + 0x84, 4);
	write32n(SMC_BASE + 0x80, *(uint32_t*)(msg + 0));
	write32n(SMC_BASE + 0x80, *(uint32_t*)(msg + 4));
	write32n(SMC_BASE + 0x80, *(uint32_t*)(msg + 8));
	write32n(SMC_BASE + 0x80, *(uint32_t*)(msg + 12));
	write32(SMC_BASE + 0x84, 0);
}

int xenon_smc_receive_message(unsigned char *msg)
{
	if (read32(SMC_BASE + 0x94) & 4)
	{
		uint32_t *msgl = (uint32_t*)msg;
		int i;
		write32(SMC_BASE + 0x94, 4);
		for (i = 0; i < 4; ++i)
			*msgl++ = read32n(SMC_BASE + 0x90);
		write32(SMC_BASE + 0x94, 0);
		return 0;
	}
	return -1;
}

/* store the last IR keypress */
int xenon_smc_last_ir = -1;

int xenon_smc_get_ir() {
	int ret = xenon_smc_last_ir;
	xenon_smc_last_ir = -1;
	return ret;
}

void xenon_smc_handle_bulk(unsigned char *msg)
{
	switch (msg[1])
	{
	case 0x11:
	case 0x20:
		printf("SMC power message\n");
		break;
	case 0x23:
		//printf("IR RX [%02x %02x]\n", msg[2], msg[3]);
		xenon_smc_last_ir = msg[3];
		break;
	case 0x60 ... 0x65:
		printf("DVD cover state: %02x\n", msg[1]);
		break;
	default:
		printf("unknown SMC bulk msg\n");
		break;
	}
}

int xenon_smc_poll()
{
	uint8_t buf[16];
	memset(buf, 0, 16);

	if (!xenon_smc_receive_message(buf)) {
		if (buf[0] == 0x83)
		{
			xenon_smc_handle_bulk(buf);
		}
		return 0;
	}
	return -1;
}

int xenon_smc_receive_response(unsigned char *msg)
{
	while (1)
	{
		if (xenon_smc_receive_message(msg))
			continue;

/*		printf("REC: ");
		int i;
		for (i = 0; i < 16; ++i)
			printf("%02x ", msg[i]);
		printf("\n");
*/
		if (msg[0] == 0x83)
		{
			xenon_smc_handle_bulk(msg);
			continue;
		}
		return 0;
	}
}

int xenon_smc_ana_write(uint8_t addr, uint32_t val)
{
	uint8_t buf[16];
	
	memset(buf, 0, 16);
	
	buf[0] = 0x11;
	buf[1] = 0x60;
	buf[3] = 0x80 | 0x70;
	
	buf[6] = addr;
	
	buf[8] = val;
	buf[9] = val >> 8;
	buf[10] = val >> 16;
	buf[11] = val >> 24;

	xenon_smc_send_message(buf);

	xenon_smc_receive_response(buf);
	if (buf[1] != 0)
	{
		printf("xenon_smc_ana_write failed, addr=%02x, err=%d\n", addr, buf[1]);
		return -1;
	}
	
	return 0;
}

int xenon_smc_ana_read(uint8_t addr, uint32_t *val)
{
	uint8_t buf[16];
	memset(buf, 0, 16);

	buf[0] = 0x11;
	buf[1] = 0x10;
	buf[2] = 5;
	buf[3] = 0x80 | 0x70;
	buf[5] = 0xF0;
	buf[6] = addr;
	
	xenon_smc_send_message(buf);
	xenon_smc_receive_response(buf);
	if (buf[1] != 0)
	{
		printf("xenon_smc_ana_read failed, addr=%02x, err=%d\n", addr, buf[1]);
		return -1;
	}
	*val = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
	return 0;
}

int xenon_smc_i2c_ddc_lock(int lock)
{
	uint8_t buf[16];
    memset(buf, 0, 16);
	
	buf[0] = 0x11;
	buf[1] = (lock)?3:5;

	xenon_smc_send_message(buf);

	xenon_smc_receive_response(buf);
	if (buf[1] != 0)
	{
		printf("xenon_smc_i2c_ddc_lock failed, err=%d\n", buf[1]);
		return -1;
	}
	
	return 0;
}

int xenon_smc_i2c_write(uint16_t addr, uint8_t val)
{
	uint8_t buf[16];
    memset(buf, 0, 16);
	
	int tmp=(addr>=0x200)?0x3d:0x39;
	int ddc=(addr>=0x1d0 && addr<=0x1f5);

    buf[0] = 0x11;
	buf[1] = (ddc)?0x21:0x20;
	buf[3] = tmp | 0x80; //3d
	
	buf[6] = addr & 0xff; //3a
	buf[7] = val;

	xenon_smc_send_message(buf);

	xenon_smc_receive_response(buf);
	if (buf[1] != 0)
	{
		printf("xenon_smc_i2c_write failed, addr=%04x, err=%d\n", addr, buf[1]);
		return -1;
	}
	
	return 0;
}

int xenon_smc_i2c_read(uint16_t addr, uint8_t *val)
{
	uint8_t buf[16];
	memset(buf, 0, 16);

    int tmp=(addr>=0x200)?0x3d:0x39;
	int ddc=(addr>=0x1d0 && addr<=0x1f5);

	buf[0] = 0x11; //40
	buf[1] = (ddc)?0x11:0x10;//3f
	buf[2] = 1; //3e
	buf[3] = buf[5] = tmp | 0x80; //3d 3b
    buf[6] = addr & 0xff; //3a
	
	xenon_smc_send_message(buf);
	xenon_smc_receive_response(buf);
	if (buf[1] != 0)
	{
		printf("xenon_smc_i2c_read failed, addr=%04x, err=%d\n", addr, buf[1]);
		return -1;
	}
	*val = buf[3];
	return 0;
}

void xenon_smc_set_led(int override, int value)
{
	uint8_t buf[16];
	memset(buf, 0, 16);
	
	buf[0] = 0x99;
	buf[1] = override;
	buf[2] = value;
	
	xenon_smc_send_message(buf);
}

void xenon_smc_set_power_led(int override, int state, int startanim)
{
	uint8_t buf[16];
	memset(buf, 0, 16);

	buf[0] = 0x8C;
	buf[1] = (override ? 1 : 0) | (state ? 0 : 2);
	buf[2] = startanim;

	xenon_smc_send_message(buf);
}

void xenon_smc_power_shutdown(void)
{
	uint8_t buf[16] = {0x82, 0x01};
	xenon_smc_send_message(buf);
}

void xenon_smc_power_reboot(void)
{
	uint8_t buf[16] = {0x82, 0x04, 0x30, 0x00};
	xenon_smc_send_message(buf);
}

void xenon_smc_start_bootanim(void)
{
	uint8_t buf[16] = {0x8c, 0x01, 0x00,0,0,0,0,0,0,0,0,0,0,0,0,0};
	xenon_smc_send_message(buf);
	buf[2] = 0x01;
	xenon_smc_send_message(buf);
}

void xenon_smc_query_sensors(uint16_t *data)
{
	uint8_t buf[16] = {0x07};
	xenon_smc_send_message(buf);
	xenon_smc_receive_response(buf);
	int i;
	for (i = 0; i < 4; ++i)
		*data++ = (buf[i * 2 + 1] | (buf[i * 2 + 2] << 8));
}

int xenon_smc_read_avpack(void)
{
	uint8_t buf[16] = {0x0f};
	xenon_smc_send_message(buf);
	xenon_smc_receive_response(buf);
	return buf[1];
}

void xenon_smc_set_fan_algorithm(int algorithm)
{
	uint8_t buf[16] = {0x88, algorithm};
	xenon_smc_send_message(buf);
}
