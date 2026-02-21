#include <xenos/xenos.h>

#include <xenos/xenos_edid.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <xenon_nand/xenon_config.h>
#include <xb360/xb360.h>
#include <pci/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <time/time.h>
#include <string.h>
#include <assert.h>
#include <xetypes.h>
#include "xenos_videomodesdata.h"

#define require(x, label) if (!(x)) { printf("%s:%d [%s]\n", __FILE__, __LINE__, #x); goto label; }

#define FB_BASE 0x1e000000

u32 xenos_id = 0; // 5841=slim, 5831=jasper, 5821=zephyr/falcon?, 5811=xenon?
int xenos_is_hdmi = 0, xenos_is_corona = 0;
static struct edid * xenos_edid = NULL;


void xenos_write32(int reg, uint32_t val)
{
	write32n(0xec800000 + reg, val);
//	printf("set reg %4x %08x, read %08x\n", reg, val, read32n(0x200ec800000 + reg));
}

uint32_t xenos_read32(int reg)
{
	return read32n(0xec800000 + reg);
}

static void xenos_ana_write(int addr, uint32_t reg)
{
	xenos_write32(0x7950, addr);
	xenos_read32(0x7950);
	xenos_write32(0x7954, reg);
	uint32_t cycle = 0;
	while (xenos_read32(0x7950) == addr && cycle < 1000) {
		if (!(cycle % 250)) printf(".");
		cycle++;
	}
	if (cycle == 1000)
		printf("\nxenos_ana_write - addr: 0x%X reg: 0x%X - FAILED!\n", addr, reg);
}

static int isCoronaOrWinchester()
{
	int type = xenon_get_console_type();
	if (type >= REV_CORONA)
		return 1;
	return 0;
}

static struct mode_s * xenos_current_mode = NULL;

void xenos_init_ana_new(uint32_t *mode_ana, int hdmi)
{
	uint32_t tmp;
	int i;
	
	require(!xenon_smc_ana_read(0xfe, &tmp), ana_error);
	
	require(!xenon_smc_ana_read(0xD9, &tmp), ana_error);
	tmp &= ~(1<<18);
	require(!xenon_smc_ana_write(0xD9, tmp), ana_error);

	xenon_smc_ana_write(0, 0);

	if (xenos_is_corona)
	{
		// pll stuff
		// all reads from the video_mode are 4 byte ints
		xenon_smc_ana_write(0xCD, 0x62);
 
		xenon_smc_ana_write(0xD0, mode_ana[0xD0]&~0x04000000);
 
		xenon_smc_ana_write(0xD1, mode_ana[0xD1]);

		u32 rd;
		uint32_t r9, r11, r4, r30;

		xenon_smc_ana_read(0xD2,&rd);
		r9 = rd & 0xFFFF0000;
		r11 = mode_ana[0xD2]& 0x0000FFFF;
		r4 = r11 | r9;
		xenon_smc_ana_write(0xD2, r4);
 
		xenon_smc_ana_write(0xCF, 0x854ACC0);
		udelay(1000);
		xenon_smc_ana_read(0xCD,&rd);
		xenon_smc_ana_write(0xCD, rd | 0x10);
		udelay(1000);
		xenon_smc_ana_read(0xCD,&rd);
		xenon_smc_ana_write(0xCD, rd & ~0x40);
		udelay(1000);
		xenon_smc_ana_write(0xD3, 0x1B0A659D);
		udelay(1000);
		xenon_smc_ana_write(0xD3, 0x1B02659D);
 
		xenon_smc_ana_read(0xCF,&rd);
 
		r30 = rd | 0x40000000;
		xenon_smc_ana_write(0xCF, r30);
		udelay(1000);
		xenon_smc_ana_write(0xCF, r30 & 0xBFFFFFFF);
		xenon_smc_ana_write(0xCF, r30 & 0xF7FFFFFF);
 
		if(mode_ana[0xD0]&0x04000000)
		{
			xenon_smc_ana_read(0xD0,&rd);
			xenon_smc_ana_write(0xD0, rd | 0x04000000);
		}

		// dac stuff
		xenon_smc_ana_write(0, 0);
		xenon_smc_ana_write(0xD7, 0xFF);
		xenon_smc_ana_write(0xF0, 0x291028E);
		xenon_smc_ana_write(0xF1, 0x28E0285);
		xenon_smc_ana_read(0xF0,&rd);
		xenon_smc_ana_write(0xF0, rd | 0x80000000);
		xenon_smc_ana_read(0xF0,&rd);
		xenon_smc_ana_write(0xF0, rd & 0x7FFFFFFF);
		xenon_smc_ana_read(0xD8,&rd);
		xenon_smc_ana_write(0xD8, rd | 0x60);
		xenon_smc_ana_read(0xD7,&rd);
		xenon_smc_ana_write(0xD7, rd | 0x80);
		udelay(1000);
		xenon_smc_ana_read(0xD8,&rd);
		xenon_smc_ana_write(0xD8, rd | 0x80);
		xenon_smc_ana_write(0xD8, 0x1F);
	}

	int addr_0[] = {0xD5, 0xD0, 0xD1, 0xD6, 0xD8, 0xD, 0xC};

	for (i = 1; i < 7; ++i)
	{
		require(!xenon_smc_ana_write(addr_0[i], mode_ana[addr_0[i]]), ana_error);
		if (addr_0[i] == 0xd6){
			udelay(1000);

			if (hdmi){
				xenon_smc_ana_write(0x000,0x00000181);
				udelay(5000);
				xenon_smc_ana_write(0x000,0x00000081);
			}
		}
	}
	
	require(!xenon_smc_ana_write(0, 0x60), ana_error);
	
	uint32_t old;
	xenos_write32(0x7938, (old = xenos_read32(0x7938)) & ~1);
	xenos_write32(0x7910, 1);
	xenos_write32(0x7900, 1);

	// fixed stuff

	int fixed_addr[] = {2, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76};
	int fixed_val[] = {0, 0x2540F38, 0xE600002, 0x2540409,2, 0x2540000, 0x3310002,0,0,0,0,0,0,0,0,0,0};
	
	for (i = 0; i < 0x11; ++i)
		xenos_ana_write(fixed_addr[i], fixed_val[i]);

	// levels stuff

	for (i = 0x26; i <= 0x34; ++i)
		xenos_ana_write(i, mode_ana[i]);

	for (i = 0x35; i <= 0x43; ++i)
		xenos_ana_write(i, mode_ana[i]);

	for (i = 0x44; i <= 0x52; ++i)
		xenos_ana_write(i, mode_ana[i]);
	
	for (i = 0x53; i <= 0x54; ++i)
		xenos_ana_write(i, mode_ana[i]);
	
	for (i = 0x55; i <= 0x57; ++i)
		xenos_ana_write(i, mode_ana[i]);

	// digital video stuff

	int addr_1[] = {3, 6, 7, 8, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};

	for (i = 0; i < sizeof(addr_1)/sizeof(int); ++i)
		xenos_ana_write(addr_1[i], mode_ana[addr_1[i]]);

	xenos_write32(0x7938, old);

	xenon_smc_ana_write(0, mode_ana[0]);

	return;
	
ana_error:
	printf("error reading/writing ana\n");
}



void xenos_init_phase0(void)
{
	xenos_write32(0xe0, 0x10000000);
	xenos_write32(0xec, 0xffffffff);

	xenos_write32(0x1724, 0);
	int i;
	for (i = 0; i < 8; ++i)
	{
		int addr[] = {0x8000, 0x8c60, 0x8008, 0x8004, 0x800c, 0x8010, 0x8014, 0x880c, 0x8800, 0x8434, 0x8820, 0x8804, 0x8824, 0x8828, 0x882c, 0x8420, 0x841c, 0x841c, 0x8418, 0x8414, 0x8c78, 0x8c7c, 0x8c80};
		int j;
		for (j = 0; j < 23; ++j)
			xenos_write32(addr[j] + i * 0x1000, 0);
	}
}

void xenos_init_phase1(void)
{
	if (xenos_id>=0x5841){
		uint32_t v,v2;
		
		v = xenos_read32(0x0218) & 0xFE7FFFFF;
		xenos_write32(0x0218,v);
		xenos_read32(0x0218);

		v2 = 0xa058a34;

		xenos_write32(0x244,v2);

		v &= 0xFFFFF803;
		v |= 0x388;
		xenos_write32(0x0218,v);
		xenos_read32(0x0218);
		udelay(1000);

		v2 |= 1;
		xenos_write32(0x244,v2);
		xenos_read32(0x244);
		udelay(1000);

		v |= 0x1000000;
		xenos_write32(0x0218,v);
		xenos_read32(0x0218);

		xenos_write32(0x0218,xenos_read32(0x0218) & 0xFFFFFFFE);
	}else{
		uint32_t v;
		xenos_write32(0x0244, 0x20000000); //
		v = 0x200c0011;
		xenos_write32(0x0244, v);
		udelay(1000);
		v &=~ 0x20000000;
		xenos_write32(0x0244, v);
		udelay(1000);

		assert(xenos_read32(0x0244) == 0xc0011);

		xenos_write32(0x0218, 0);
	}

	xenos_write32(0x3c04, 0xe);
	xenos_write32(0x00f4, 4);
	
	if (xenos_id>=0x5841){
		xenos_write32(0x0204, 0x947FF386);
		xenos_write32(0x0208, 0x3FC7C2);
	}else{
		int gpu_clock = xenos_read32(0x0210);
		int mem_clock = xenos_read32(0x284);
		int edram_clock = xenos_read32(0x244);
		int fsb_clock = xenos_read32(0x248);
		assert(gpu_clock == 9);
		printf("GPU Clock at %d MHz\n", 50 * ((gpu_clock & 0xfff)+1) / (((gpu_clock >> 12) & 0x3f)+1));
		printf("MEM Clock at %d MHz\n", 50 * ((mem_clock & 0xfff)+1) / (((mem_clock >> 12) & 0x3f)+1));
		printf("EDRAM Clock at %d MHz\n", 100 * ((edram_clock & 0xfff)+1) / (((edram_clock >> 12) & 0x3f)+1));
		printf("FSB Clock at %d MHz\n", 100 * ((fsb_clock & 0xfff)+1) / (((fsb_clock >> 12) & 0x3f)+1));

		xenos_write32(0x0204, (xenos_id<0x5821)?0x4000300:0x4400380);
		xenos_write32(0x0208, 0x180002);
	}
	
	xenos_write32(0x01a8, 0);
	xenos_write32(0x0e6c, 0x0c0f0000);
	xenos_write32(0x3400, 0x40401);
	udelay(1000);
	xenos_write32(0x3400, 0x40400);
	xenos_write32(0x3300, 0x3a22);
	xenos_write32(0x340c, 0x1003f1f);
	xenos_write32(0x00f4, 0x1e);
	
	
	xenos_write32(0x2800, 0);
	xenos_write32(0x2804, 0x20000000);
	xenos_write32(0x2808, 0x20000000);
	xenos_write32(0x280c, 0);
	xenos_write32(0x2810, 0x20000000);
	xenos_write32(0x2814, 0);

	udelay(1000);	
	xenos_write32(0x6548, 0);
	
		// resetgui
		// initcp
		
	xenos_write32(0x04f0, (xenos_read32(0x04f0) &~ 0xf0000) | 0x40100);
}

static void xenos_ana_stop_display(void)
{
	uint32_t tmp;
	xenon_smc_ana_read(0, &tmp);
	tmp &= ~0x1e;
	xenon_smc_ana_write(0, tmp);
}

void xenos_ana_preinit(void)
{
	xenos_ana_stop_display();
	uint32_t v;
	xenos_write32(0x6028, v = (xenos_read32(0x6028) | 0x01000000));
	while (!(xenos_read32(0x281c) & 2));
	xenon_smc_ana_write(0, 0);
	xenon_smc_ana_write(0xd7, 0xff);
	xenos_write32(0x6028, v & ~0x301);
	xenos_write32(0x7900, 0);
	xenon_smc_ana_read(0xd9, &v);
	xenon_smc_ana_write(0xd9, v | 0x40000);
	xenon_smc_i2c_write(0x108,0x36);
}

void xenos_set_mode_f1(struct mode_s *mode)
{
	xenos_write32(D1CRTC_UPDATE_LOCK, 1);
	xenos_write32(DCP_LB_DATA_GAP_BETWEEN_CHUNK, 3);
	xenos_write32(D1CRTC_DOUBLE_BUFFER_CONTROL, 0);
	xenos_write32(D1CRTC_V_TOTAL, 0);

	int interlace_factor = mode->is_progressive ? 1 : 2;

	xenos_write32(D1CRTC_H_TOTAL, mode->total_width - 1);
	xenos_write32(D1CRTC_H_SYNC_B, mode->total_height - 1);
	xenos_write32(D1CRTC_H_BLANK_START_END, (mode->hsync_offset << 16) | (mode->real_active_width + mode->hsync_offset));
	xenos_write32(D1CRTC_H_SYNC_B_CNTL, (mode->vsync_offset << 16) | (mode->active_height * interlace_factor + mode->vsync_offset));
	xenos_write32(D1CRTC_H_SYNC_A, 0x100000);
	xenos_write32(0x6018, 0x60000);
	xenos_write32(D1CRTC_V_SYNC_B, mode->is_progressive ? 0 : 1);
	xenos_write32(D1CRTC_H_SYNC_A_CNTL, 0);
	xenos_write32(0x601c, 0);

	xenos_write32(D1CRTC_MVP_INBAND_CNTL_CAP, 0);
	xenos_write32(D1CRTC_MVP_INBAND_CNTL_INSERT, 0);
	xenos_write32(D1CRTC_MVP_FIFO_STATUS, 0);
	xenos_write32(D1CRTC_MVP_SLAVE_STATUS, 0);

	xenos_write32(D1CRTC_UPDATE_LOCK, 0);
}

void xenos_set_mode_f2(struct mode_s *mode)
{
	int interlace_factor = mode->is_progressive ? 1 : 2;

	xenos_write32(D1GRPH_UPDATE, 1);
	xenos_write32(D1GRPH_PITCH, mode->width); /* pitch */
	xenos_write32(D1GRPH_CONTROL, 2);
	xenos_write32(D1GRPH_LUT_SEL, 0); /* lut override */
	xenos_write32(D1GRPH_SURFACE_OFFSET_X, 0);
	xenos_write32(D1GRPH_SURFACE_OFFSET_Y, 0);
	xenos_write32(D1GRPH_X_START, 0);
	xenos_write32(D1GRPH_Y_START, 0);
	xenos_write32(D1GRPH_X_END, mode->width);
	xenos_write32(D1GRPH_Y_END, mode->active_height * interlace_factor);
	xenos_write32(D1GRPH_PRIMARY_SURFACE_ADDRESS, FB_BASE);
	xenos_write32(D1GRPH_PITCH, mode->width);
	xenos_write32(D1GRPH_ENABLE, 1);
	xenos_write32(D1GRPH_UPDATE, 0);

			/* scaler update */
	xenos_write32(AVIVO_D1SCL_UPDATE, 1);
	xenos_write32(AVIVO_D1SCL_SCALER_ENABLE , 0);
	xenos_write32(0x2840, FB_BASE);
	xenos_write32(0x2844, mode->width);
	xenos_write32(0x2848, 0x80000);

	xenos_write32(AVIVO_D1MODE_VIEWPORT_START , 0); /* viewport */
	xenos_write32(AVIVO_D1MODE_VIEWPORT_SIZE, (mode->width << 16) | (mode->active_height * interlace_factor));
	xenos_write32(0x65e8, (mode->width >> 2) - 1);
	xenos_write32(AVIVO_D1MODE_DATA_FORMAT, mode->is_progressive ? 0 : 1);
	xenos_write32(0x6550, 0xff);
	xenos_write32(0x6524, 0x300);
	xenos_write32(0x65d0, 0x100);
	xenos_write32(D1GRPH_FLIP_CONTROL, 1);

	xenos_write32(AVIVO_D1SCL_SCALER_TAP_CONTROL, 0x905);
	xenos_write32(AVIVO_D1MODE_VIEWPORT_SIZE, (mode->width << 16) | (mode->height * interlace_factor));
	xenos_write32(0x65e8, (mode->width / 4) - 1);
	xenos_write32(AVIVO_D1MODE_VIEWPORT_START, 0);
	xenos_write32(D1GRPH_X_START, 0);
	xenos_write32(D1GRPH_Y_START, 0);
	xenos_write32(D1GRPH_X_END, mode->width);
	xenos_write32(D1GRPH_Y_END, mode->height * interlace_factor);
	xenos_write32(D1CRTC_MVP_FIFO_STATUS, 0);
	xenos_write32(D1CRTC_MVP_SLAVE_STATUS, 0);
	xenos_write32(D1CRTC_MVP_INBAND_CNTL_CAP, 0);
	xenos_write32(D1CRTC_MVP_INBAND_CNTL_INSERT, 0);
	xenos_write32(0x65a0, 0);
	xenos_write32(0x65b4, 0x01000000);
	xenos_write32(0x65c4, 0x01000000);
	xenos_write32(0x65b0, 0);
	xenos_write32(0x65c0, 0x01000000);
	xenos_write32(0x65b8, 0x00060000);
	xenos_write32(0x65c8, 0x00040000);
	xenos_write32(0x65dc, 0);
	xenos_write32(AVIVO_D1SCL_UPDATE, 0);

	xenos_write32(DC_LUTA_CONTROL, 0);
	xenos_write32(DC_LUT_RW_INDEX, 0);
	xenos_write32(DC_LUT_RW_MODE, 0);
	xenos_write32(DC_LUT_WRITE_EN_MASK, 7);
	xenos_write32(DC_LUT_AUTOFILL, 1);
	while (!(xenos_read32(DC_LUT_AUTOFILL) & 2));
}

void xenos_set_mode(struct mode_s *mode)
{
	xenos_write32(0x7938, xenos_read32(0x7938) | 1);
	xenos_write32(0x06ac, 1);
	
	printf(" . ana disable\n");
	xenos_ana_preinit();
	xenos_write32(0x04a0, 0x100);
	printf(" . ana enable\n");
	xenos_init_ana_new(mode->ana,mode->hdmi);
	xenos_write32(0x7900, 1);

	printf(" . f1\n");
	xenos_set_mode_f1(mode);
	printf(" . f2\n");
	xenos_set_mode_f2(mode);

	xenos_write32(0x6028, 0x10001);
	
	if (!mode->composite_sync)
	{
		xenos_write32(D1CRTC_MVP_CONTROL1, 0x04010040);
		xenos_write32(D1CRTC_MVP_CONTROL2, 0);
		xenos_write32(D1CRTC_MVP_FIFO_CONTROL, 0x04010040);
	} else
	{
		xenos_write32(D1CRTC_MVP_CONTROL1, 0x20000200);
		xenos_write32(D1CRTC_MVP_CONTROL2, 0x20010200);
		xenos_write32(D1CRTC_MVP_FIFO_CONTROL, 0x20010200);
	}
	xenos_write32(0x793c, 0);
	xenos_write32(0x7938, 0x700);


	xenos_write32(AVIVO_D1CRTC_V_BLANK_START_END, 0x04010040);
	xenos_write32(D1CRTC_MVP_INBAND_CNTL_INSERT_TIMER, 0x00010002);
	xenos_write32(D1CRTC_MVP_BLACK_KEYER, 0xec02414a);

	xenos_write32(D1CRTC_TRIGA_CNTL, 0x000000ec);
	xenos_write32(D1CRTC_TRIGA_MANUAL_TRIG, 0x014a00ec);
	xenos_write32(D1CRTC_TRIGB_CNTL, 0x00d4014a);
	xenos_write32(D1GRPH_PRIMARY_SURFACE_ADDRESS, FB_BASE);


	if (!mode->rgb)
	{
		xenos_write32(D1GRPH_COLOR_MATRIX_TRANSFORMATION_CNTL, 0x00000001);
		xenos_write32(D1COLOR_MATRIX_COEF_1_1, 0x00702000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_2, 0x87a26000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_3, 0x87ede000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_4, 0x02008000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_1, 0x00418000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_2, 0x0080a000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_3, 0x00192000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_4, 0x00408000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_1, 0x87da4000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_2, 0x87b5e000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_3, 0x00702000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_4, 0x02008000);
		xenos_write32(0x63b4, 0x00000000);
	} else
	{
		xenos_write32(D1GRPH_COLOR_MATRIX_TRANSFORMATION_CNTL, 0x00000001);
		xenos_write32(D1COLOR_MATRIX_COEF_1_1, 0x00db4000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_2, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_3, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_1_4, 0x00408000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_1, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_2, 0x00db4000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_3, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_2_4, 0x00408000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_1, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_2, 0x00000000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_3, 0x00db4000);
		xenos_write32(D1COLOR_MATRIX_COEF_3_4, 0x00408000);
		xenos_write32(0x63b4, 0x00000000);
	}

	if (mode->hdmi) xenon_smc_ana_write(0,0x2c1);
	
	xenos_current_mode = mode;
}

int xenos_hdmi_vga_mode_fix(int mode)
{
	struct edid * monitor_edid = xenos_get_edid();
		// fix some non standart mode (vga + yuv or hdmi/dvi + yuv )
		if(monitor_edid) {
			// HDMI or DVI
			if(monitor_edid->input&DRM_EDID_INPUT_DIGITAL)
				return VIDEO_MODE_HDMI_720P;
			// VGA
			else
				return VIDEO_MODE_VGA_1024x768;
		}
		return mode;
}

void xenos_autoset_mode(void)
{
	int mode;
	int avpack = xenon_smc_read_avpack();
	//mode = VIDEO_MODE_VGA_1024x768;

	switch (xenon_config_get_avregion())
	{
	case AVREGION_NTSCJ:
	case AVREGION_NTSCM:
		mode = VIDEO_MODE_NTSC;
		break;
	case AVREGION_PAL50:
		mode = VIDEO_MODE_PAL50;
		break;
	case AVREGION_PAL60:
		mode = VIDEO_MODE_PAL60;
		break;
	default:
		mode = VIDEO_MODE_PAL60;
		break;
	}

	printf("AVPACK detected: %02x\n", avpack);

	switch (avpack)
	{
	case 0x13: // HDMI_AUDIO
	case 0x14: // HDMI_AUDIO - GHETTO MOD
	case 0x1C: // HDMI_AUDIO - GHETTO MOD
	case 0x1E: // HDMI
	case 0x1F: // HDMI
		mode = VIDEO_MODE_HDMI_720P;
		break;
	case 0x43: // COMPOSITE - TV MODE
	case 0x47: // SCART
	case 0x54: // COMPOSITE + S-VIDEO
	case 0x57: // NORMAL COMPOSITE
		break;
	case 0x0C: // COMPONENT
	case 0x0F: // COMPONENT
	case 0x4F: // COMPOSITE - HD MODE
		mode = VIDEO_MODE_YUV_720P;
		break;
	case 0x5B: // VGA
	case 0x59: // also vga
	case 0x1B: // this fixes a generic vga adapter, was under HDMI
		mode = VIDEO_MODE_VGA_1024x768;
		break;
	default:
		break;
	}
		
	switch (avpack)
	{
	case 0x43: // COMPOSITE - TV MODE
	case 0x47: // SCART
	case 0x54: // COMPOSITE + S-VIDEO
	case 0x57: // NORMAL COMPOSITE
	case 0x0C: // COMPONENT
	case 0x0F: // COMPONENT
	case 0x4F: // COMPOSITE - HD MODE
		break;
	default:
		mode = xenos_hdmi_vga_mode_fix(mode);
		break;
	}

	if (xenos_is_corona)
		xenos_set_mode(&xenos_modes_corona[mode]);
	else
		xenos_set_mode(&xenos_modes[mode]);
}

void xenos_init(int videoMode)
{
	xenos_id = xenon_get_XenosID();
	printf("Xenos GPU ID=%04x\n", (unsigned int)xenos_id);

	xenos_is_corona = isCoronaOrWinchester();
	if (xenos_is_corona)
		printf("Detected Corona motherboard!\n");

	xenos_init_phase0();
	xenos_init_phase1();
	
	xenon_gpio_set(0, 0x2300);
	xenon_gpio_set_oe(0, 0x2300);

	if (videoMode <= VIDEO_MODE_AUTO || videoMode >= VIDEO_MODE_COUNT){
		xenon_config_init();
		xenos_autoset_mode();
	}
	else if (xenos_is_corona)
		xenos_set_mode(&xenos_modes_corona[videoMode]);
	else
		xenos_set_mode(&xenos_modes[videoMode]);

	xenon_smc_ana_write(0xdf, 0);
	xenos_write32(AVIVO_D1MODE_DESKTOP_HEIGHT, 0x00000300);
	
	if (xenos_current_mode->hdmi){
		xenos_edid=xenos_get_edid();
		xenos_is_hdmi=xenos_detect_hdmi_monitor(xenos_edid);
		if(xenos_is_hdmi) printf("Detected HDMI monitor!\n");
	}

	printf("Video Mode: %s\n", xenos_current_mode->name);
}

int xenos_is_overscan()
{
	if(xenos_current_mode)
		return xenos_current_mode->overscan;
	else{
		printf("Xenos not initialized\n");
		exit(-1);
	}
}

int xenos_is_initialized()
{
	return xenos_current_mode != NULL;
}
