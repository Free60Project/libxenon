#ifndef __xenos_xenos_h
#define __xenos_xenos_h

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MODE_AUTO		    -1
#define VIDEO_MODE_VGA_640x480   0
#define VIDEO_MODE_VGA_1024x768  1
#define VIDEO_MODE_PAL60         2
#define VIDEO_MODE_YUV_480P      3
#define VIDEO_MODE_PAL50         4
#define VIDEO_MODE_VGA_1280x768  5
#define VIDEO_MODE_VGA_1360x768  6
#define VIDEO_MODE_VGA_1280x720  7
#define VIDEO_MODE_VGA_1440x900  8
#define VIDEO_MODE_VGA_1280x1024 9
#define VIDEO_MODE_HDMI_720P     10
#define VIDEO_MODE_YUV_720P      11
#define VIDEO_MODE_NTSC          12

#define D1CRTC_UPDATE_LOCK             0x60e8
#define DCP_LB_DATA_GAP_BETWEEN_CHUNK  0x6cbc
#define D1CRTC_DOUBLE_BUFFER_CONTROL   0x60ec
#define D1CRTC_V_TOTAL                 0x6020
#define D1CRTC_H_TOTAL                 0x6000
#define D1CRTC_H_SYNC_B                0x6010
#define D1CRTC_H_BLANK_START_END       0x6004
#define D1CRTC_H_SYNC_B_CNTL           0x6014
#define D1CRTC_H_SYNC_A                0x6008
#define D1CRTC_V_SYNC_B                0x6030
#define D1CRTC_H_SYNC_A_CNTL           0x600c
#define D1CRTC_MVP_INBAND_CNTL_CAP     0x604c
#define D1CRTC_MVP_INBAND_CNTL_INSERT  0x6050
#define D1CRTC_MVP_FIFO_STATUS         0x6044
#define D1CRTC_MVP_SLAVE_STATUS        0x6048
#define	D1GRPH_UPDATE                  0x6144
#define	D1GRPH_PITCH                   0x6120
#define	D1GRPH_CONTROL                 0x6104
#define	D1GRPH_LUT_SEL                 0x6108
#define	D1GRPH_SURFACE_OFFSET_X        0x6124
#define	D1GRPH_SURFACE_OFFSET_Y        0x6128
#define	D1GRPH_X_START                 0x612c
#define	D1GRPH_Y_START                 0x6130
#define	D1GRPH_X_END                   0x6134
#define	D1GRPH_Y_END                   0x6138
#define	D1GRPH_PRIMARY_SURFACE_ADDRESS 0x6110
#define	D1GRPH_ENABLE                  0x6100
#define	AVIVO_D1SCL_UPDATE             0x65cc
#define	AVIVO_D1SCL_SCALER_ENABLE      0x6590
#define AVIVO_D1MODE_VIEWPORT_START    0x6580
#define AVIVO_D1MODE_VIEWPORT_SIZE     0x6584
#define AVIVO_D1MODE_DATA_FORMAT       0x6528
#define D1GRPH_FLIP_CONTROL            0x6148
#define AVIVO_D1SCL_SCALER_TAP_CONTROL 0x6594
#define DC_LUTA_CONTROL                0x64C0
#define DC_LUT_RW_INDEX	               0x6488
#define DC_LUT_RW_MODE                 0x6484
#define DC_LUT_WRITE_EN_MASK	       0x649C
#define DC_LUT_AUTOFILL      	       0x64a0
#define D1CRTC_MVP_CONTROL1		       0x6038
#define D1CRTC_MVP_CONTROL2		       0x603c
#define D1CRTC_MVP_FIFO_CONTROL        0x6040
#define AVIVO_D1CRTC_V_BLANK_START_END 0x6024
#define D1CRTC_MVP_INBAND_CNTL_INSERT_TIMER	 0x6054
#define D1CRTC_MVP_BLACK_KEYER	       0x6058
#define D1CRTC_TRIGA_CNTL	           0x6060
#define D1CRTC_TRIGA_MANUAL_TRIG	   0x6064
#define D1CRTC_TRIGB_CNTL	           0x6068
#define AVIVO_D1MODE_DESKTOP_HEIGHT    0x652c
#define	D1GRPH_COLOR_MATRIX_TRANSFORMATION_CNTL   0x6380
#define D1COLOR_MATRIX_COEF_1_1        0x6384
#define	D1COLOR_MATRIX_COEF_1_2        0x6388
#define	D1COLOR_MATRIX_COEF_1_3        0x638c
#define	D1COLOR_MATRIX_COEF_1_4        0x6390
#define	D1COLOR_MATRIX_COEF_2_1        0x6394
#define	D1COLOR_MATRIX_COEF_2_2        0x6398
#define	D1COLOR_MATRIX_COEF_2_3        0x639c
#define	D1COLOR_MATRIX_COEF_2_4        0x63a0
#define	D1COLOR_MATRIX_COEF_3_1        0x63a4
#define	D1COLOR_MATRIX_COEF_3_2        0x63a8
#define	D1COLOR_MATRIX_COEF_3_3        0x63ac
#define	D1COLOR_MATRIX_COEF_3_4        0x63b0

int xenos_is_initialized();
void xenos_init(int videoMode);
int xenos_is_overscan();

#ifdef __cplusplus
};
#endif

#endif      
