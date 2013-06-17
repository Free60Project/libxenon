#include "xe.h"
#include "xe_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <cache.h>
#include <xenon_smc/xenon_smc.h>
#include <stdint.h>
#include <time/time.h>
#include <debug.h>

#define WRITEBACK_ZONE_SIZE 0x20000

#define RPTR_WRITEBACK 0x10000
#define SCRATCH_WRITEBACK 0x10100

#define RINGBUFFER_PRIMARY_SIZE (0x8000/4)
#define RINGBUFFER_SECONDARY_SIZE (0x400000/4)
#define RINGBUFFER_SECONDARY_GUARD (0x200000/4)

#define RADEON_CP_PACKET0               0x00000000
#define RADEON_ONE_REG_WR                (1 << 15)

#define CP_PACKET0( reg, n )                                            \
        (RADEON_CP_PACKET0 | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET0_TABLE( reg, n )                                      \
        (RADEON_CP_PACKET0 | RADEON_ONE_REG_WR | ((n) << 16) | ((reg) >> 2))

#define VBPOOL_NUM_TRIANGLES 2000

static inline void Xe_pWriteReg(struct XenosDevice *xe, u32 reg, u32 val)
{
	rput32(CP_PACKET0(reg, 0));
	rput32(val);
}

static inline u32 SurfaceInfo(int surface_pitch, int msaa_samples, int hi_zpitch)
{
	return surface_pitch | (msaa_samples << 16) | (hi_zpitch << 18);
}

static inline u32 xy32(int x, int y)
{
	return x | (y << 16);
}


void Xe_pSyncToDevice(struct XenosDevice *xe, volatile void *data, int len)
{
	memdcbst((void *)data, len);
}

void Xe_pSyncFromDevice(struct XenosDevice *xe, volatile void *data, int len)
{
	memdcbf((void *)data, len);
}

void *Xe_pAlloc(struct XenosDevice *xe, u32 *phys, int size, int align)
{
	void *r;
	if (!align)
		align = size;

    r=memalign(align,size);
    if (!r)
        Xe_Fatal(xe, "out of memory\n");

    if (phys)
        *phys = (u32) r &~ 0x80000000;

    return r;
}

void Xe_pFree(struct XenosDevice *xe, void * ptr)
{
    free(ptr);
}

void Xe_pInvalidateGpuCache_Primary(struct XenosDevice *xe, int base, int size)
{
	size +=  0x1FFF;
	size &= ~0x1FFF;

	rput32p(0x00000a31); 
		rput32p(0x01000000);
	rput32p(0x00010a2f); 
		rput32p(size); rput32p(base);
	rput32p(0xc0043c00); 
		rput32p(0x00000003); rput32p(0x00000a31); rput32p(0x00000000); rput32p(0x80000000); rput32p(0x00000008);
}


void Xe_pRBCommitPrimary(struct XenosDevice *xe)
{
	int i;
	for (i=0; i<0x20; ++i)
		rput32p(0x80000000);
	Xe_pSyncToDevice(xe, xe->rb_primary, RINGBUFFER_PRIMARY_SIZE * 4);
	__asm__ ("sync");
	w32(0x0714, xe->rb_primary_wptr);
//	printf("committed to %08x\n", xe->rb_primary_wptr);
}

void Xe_pRBKickSegment(struct XenosDevice *xe, int base, int len)
{
//	printf("kick_segment: %x..%x, len=%x\n", base, base + len * 4, len * 4);
	Xe_pSyncToDevice(xe, xe->rb_secondary + base * 4, len * 4);
	Xe_pInvalidateGpuCache_Primary(xe, xe->rb_secondary_base + base * 4, len * 4 + 0x1000);
	rput32p(0xc0013f00);
		rput32p(xe->rb_secondary_base + base * 4); rput32p(len);
}

void Xe_pRBKick(struct XenosDevice *xe)
{
//	printf("kick: wptr = %x, last_wptr = %x\n", xe->rb_secondary_wptr, xe->last_wptr);
	
	Xe_pRBKickSegment(xe, xe->last_wptr, xe->rb_secondary_wptr - xe->last_wptr);

	xe->rb_secondary_wptr += (-xe->rb_secondary_wptr)&0x1F; /* 128byte align */
	
	if (xe->rb_secondary_wptr >= RINGBUFFER_SECONDARY_SIZE)
		Xe_Fatal(xe, "increase guardband");

	if (xe->rb_secondary_wptr > (RINGBUFFER_SECONDARY_SIZE - RINGBUFFER_SECONDARY_GUARD))
		xe->rb_secondary_wptr = 0;
	
	xe->last_wptr = xe->rb_secondary_wptr;

	Xe_pRBCommitPrimary(xe);
}

#define SEGMENT_SIZE 1024

void Xe_pRBMayKick(struct XenosDevice *xe)
{
//	printf("may kick: wptr = %x, last_wptr = %x\n", rb_secondary_wptr, last_wptr);
	int distance = xe->rb_secondary_wptr - xe->last_wptr;
	if (distance < 0)
		distance += RINGBUFFER_SECONDARY_SIZE;
	
	if (distance >= SEGMENT_SIZE)
		Xe_pRBKick(xe);
}

u32 Xe_pRBAlloc(struct XenosDevice *xe)
{
	u32 rb_primary_phys;
	xe->rb_primary = Xe_pAlloc(xe, &rb_primary_phys, RINGBUFFER_PRIMARY_SIZE * 4, 0);
	xe->rb_secondary = Xe_pAlloc(xe, &xe->rb_secondary_base, RINGBUFFER_SECONDARY_SIZE * 4, 0x100);
	return rb_primary_phys;
}

void Xe_pSetSurfaceClip(struct XenosDevice *xe, int offset_x, int offset_y, int sc_left, int sc_top, int sc_right, int sc_bottom)
{
	rput32(0x00022080);
		rput32(xy32(offset_x, offset_y));
		rput32(xy32(sc_left, sc_top));
		rput32(xy32(sc_right, sc_bottom));
}

void Xe_pSetBin(struct XenosDevice *xe, u32 mask_low, u32 select_low, u32 mask_hi, u32 select_hi)
{
	rput32(0xc0006000);
		rput32(mask_low);
	rput32(0xc0006200);
		rput32(select_low);
	rput32(0xc0006100);
		rput32(mask_hi);
	rput32(0xc0006300);
		rput32(select_hi); 
}

void Xe_pWaitUntilIdle(struct XenosDevice *xe, u32 what)
{
	rput32(0x000005c8);
		rput32(what);
}

void Xe_pDrawNonIndexed(struct XenosDevice *xe, int num_points, int primtype)
{
	rput32(0xc0012201);
	rput32(0x00000000);
	rput32(0x00000080 | (num_points << 16) | primtype);
}

void Xe_pDrawIndexedPrimitive(struct XenosDevice *xe, int primtype, int num_points, u32 indexbuffer, u32 indexbuffer_size, int indextype)
{
	assert(num_points <= XE_MAX_INDICES_PER_DRAW);
	int type = 0;
	
	rput32(0xc0032201);
		rput32(0x00000000);
		rput32(0x00000000 | (type << 6) | primtype | (num_points << 16) | (indextype << 11));
		rput32(indexbuffer);
		rput32((indexbuffer_size | 0x40000000) << indextype);
}

void Xe_pSetIndexOffset(struct XenosDevice *xe, int offset)
{
	rput32(0x00002102);
		rput32(offset);
}

void Xe_pResetRingbuffer(struct XenosDevice *xe)
{
	w32(0x0704, r32(0x0704) | 0x80000000);
	w32(0x017c, 0);
	w32(0x0714, 0);
	w32(0x0704, r32(0x0704) &~0x80000000);
}

void Xe_pSetupRingbuffer(struct XenosDevice *xe, u32 buffer_base, u32 size_in_l2qw)
{
	Xe_pResetRingbuffer(xe);
	w32(0x0704, size_in_l2qw | 0x8020000);
	w32(0x0700, buffer_base);
	w32(0x0718, 0x10);
}

void Xe_pLoadUcodes(struct XenosDevice *xe, const u32 *ucode0, const u32 *ucode1)
{
	int i;
	
	w32(0x117c, 0);
	udelay(100);
	
	for (i = 0; i < 0x120; ++i)
		w32(0x1180, ucode0[i]);

	w32(0x117c, 0);
	udelay(100);
	for (i = 0; i < 0x120; ++i)
		r32(0x1180);

	w32(0x07e0, 0);
	for (i = 0; i < 0x900; ++i)
		w32(0x07e8, ucode1[i]);

	w32(0x07e4, 0);
	for (i = 0; i < 0x900; ++i)
		if (r32(0x07e8) != ucode1[i])
		{
			printf("%04x: %08x %08x\n", i, r32(0x07e8), ucode1[i]);
			break;
		}

	if (i != 0x900)
		Xe_Fatal(xe, "ucode1 microcode verify error\n");	
}

void Xe_pWaitReady(struct XenosDevice *xe)
{
	int timeout = 1<<24;
	while (r32(0x1740) & 0x80000000)
	{
		if (!timeout--)
			Xe_Fatal(xe, "timeout in init, likely the GPU was already hung before we started\n");
	}
}

void Xe_pWaitReady2(struct XenosDevice *xe)
{
	while (!(r32(0x1740) & 0x00040000));
}

void Xe_pInit1(struct XenosDevice *xe)
{
	w32(0x01a8, 0);
	w32(0x0e6c, 0xC0F0000);
	w32(0x3400, 0x40401);
	udelay(1000);
	w32(0x3400, 0x40400);
	w32(0x3300, 0x3A22);
	w32(0x340c, 0x1003F1F);
	w32(0x00f4, 0x1E);
}

void Xe_pReset(struct XenosDevice *xe)
{
	Xe_pWaitReady2(xe);
	Xe_pWaitReady(xe);
#if 1
	printf("waiting for reset.\n");
	do {
		w32(0x00f0, 0x8064); r32(0x00f0);
		w32(0x00f0, 0);
		w32(0x00f0, 0x11800); r32(0x00f0);
		w32(0x00f0, 0);
		udelay(1000);
	} while (r32(0x1740) & 0x80000000);
#endif

	if (r32(0x00e0) != 0x10)
		Xe_Fatal(xe, "value after reset not ok (%08x)\n", r32(0xe0));

	Xe_pInit1(xe);
}

void Xe_pResetCP(struct XenosDevice *xe, u32 buffer_base, u32 size_in_l2qw)
{
	w32(0x07d8, 0x1000FFFF);
	udelay(2000);
	w32(0x00f0, 1);
	(void)r32(0x00f0);
	udelay(1000);
	w32(0x00f0, 0);
	udelay(1000);
	Xe_pSetupRingbuffer(xe, buffer_base, size_in_l2qw);
	Xe_pWaitReady(xe);

	if (!(r32(0x07d8) & 0x10000000))
		Xe_Fatal(xe, "something wrong (1)\n");

	w32(0x07d8, 0xFFFF);
	udelay(1000);

	w32(0x3214, 7);
	w32(0x3294, 1);
	w32(0x3408, 0x800);
	
	Xe_pWaitReady(xe);
	
	if (r32(0x0714))
		Xe_Fatal(xe, "[WARN] something wrong (2)\n");
	
	if (r32(0x0710))
		Xe_Fatal(xe, "[WARN] something wrong (3)\n");

	w32(0x07ec, 0x1A);
}

void Xe_pSetup(struct XenosDevice *xe, u32 buffer_base, u32 buffer_size, const u32 *ucode0, const u32 *ucode1)
{
	Xe_pWaitReady(xe);

	w32(0x07d8, 0x1000FFFF);
	
	Xe_pSetupRingbuffer(xe, buffer_base, buffer_size);
	Xe_pLoadUcodes(xe, ucode0, ucode1);
	Xe_pWaitReady(xe);
	
	w32(0x07d8, 0xFFFF);
	w32(0x07d0, 0xFFFF);
	w32(0x07f0, 0);
	w32(0x0774, 0);
	w32(0x0770, 0);
	w32(0x3214, 7);
	w32(0x3294, 1);
	w32(0x3408, 0x800);
	Xe_pResetCP(xe, buffer_base, buffer_size);
	Xe_pWaitReady(xe);
}

extern u32 xenos_ucode0[];
extern u32 xenos_ucode1[];

void Xe_pMasterInit(struct XenosDevice *xe, u32 buffer_base)
{
	if ((r32(0x0e6c) & 0xF00) != 0xF00)
		printf("something wrong (3)\n");

	Xe_pSetup(xe, buffer_base, 0xC, xenos_ucode0, xenos_ucode1);

	w32(0x07d4, 0);
    w32(0x07d4, 1);

	w32(0x2054, 0x1E);
	w32(0x2154, 0x1E);
	
	w32(0x3c10, 0xD);
	
	w32(0x3c40, 0x17);
	w32(0x3c48, 0);
	while (r32(0x3c4c) & 0x80000000);

	w32(0x3c40, 0x1017);
	w32(0x3c48, 0);
	while (r32(0x3c4c) & 0x80000000);

	w32(0x87e4, 0x17);
}

void Xe_pEnableWriteback(struct XenosDevice *xe, u32 addr, int blocksize)
{
	u32 v = r32(0x0704);

	v &= ~0x8003F00;
	w32(0x0704, v);
	
	w32(0x070c, addr | 2);
	w32(0x0704, v | (blocksize << 8));
}

void Xe_pGInit0(struct XenosDevice *xe)
{
	rput32(0xc0003b00);
		rput32(0x00000300);
	
	rput32(0xc0192b00);
		rput32(0x00000000); rput32(0x00000018); 
		rput32(0x00001003); rput32(0x00001200); rput32(0xc4000000); rput32(0x00001004); 
		rput32(0x00001200); rput32(0xc2000000); rput32(0x00001005); rput32(0x10061200); 
		rput32(0x22000000); rput32(0xc8000000); rput32(0x00000000); rput32(0x02000000); 
		rput32(0xc800c000); rput32(0x00000000); rput32(0xc2000000); rput32(0xc888c03e); 
		rput32(0x00000000); rput32(0xc2010100); rput32(0xc8000000); rput32(0x00000000); 
		rput32(0x02000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);
	rput32(0xc00a2b00);
		rput32(0x00000001); rput32(0x00000009); 
		
		rput32(0x00000000); rput32(0x1001c400); rput32(0x22000000); rput32(0xc80f8000); 
		rput32(0x00000000); rput32(0xc2000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000);

	rput32(0x00012180);
		rput32(0x1000000e); rput32(0x00000000);
	rput32(0x00022100);
		rput32(0x0000ffff); rput32(0x00000000); rput32(0x00000000);
	rput32(0x00022204); 
		rput32(0x00010000); rput32(0x00010000); rput32(0x00000300);
	rput32(0x00002312); 
		rput32(0x0000ffff);
	rput32(0x0000200d); 
		rput32(0x00000000);
	rput32(0x00002200); 
		rput32(0x00000000);
	rput32(0x00002203); 
		rput32(0x00000000);
	rput32(0x00002208); 
		rput32(0x00000004);
	rput32(0x00002104); 
		rput32(0x00000000);
	rput32(0x00002280); 
		rput32(0x00080008);
	rput32(0x00002302); 
		rput32(0x00000004);
	Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, 16, 16);
}

void Xe_pGInit1(struct XenosDevice *xe, int arg)
{
	rput32(0x000005c8); 
		rput32(0x00020000);
	rput32(0x00078d00); 
		rput32(arg | 1); rput32(arg | 1); rput32(arg | 1); rput32(arg | 1); 
		rput32(arg | 1); rput32(arg | 1); rput32(arg | 1); rput32(arg | 1);
	rput32(0x00000d00); 
		rput32(arg);
}

void Xe_pGInit2(struct XenosDevice *xe)
{
	int i;
	for (i=0; i<24; ++i)
	{
		rput32(0xc0003600);
			rput32(0x00010081);
	}
}

void Xe_pGInit3(struct XenosDevice *xe)
{
	rput32(0x000005c8); 
		rput32(0x00020000);
	rput32(0x00000d04); 
		rput32(0x00000000);
}

void Xe_pGInit4(struct XenosDevice *xe) /* "init_0" */
{
	rput32(0x00000d02);  /* ? */
		rput32(0x00010800);
	rput32(0x00030a02); 
		rput32(0xc0100000); rput32(0x07f00000); rput32(0xc0000000); rput32(0x00100000);

	Xe_pGInit3(xe);
}

void Xe_pGInit5(struct XenosDevice *xe) /* "init_1" */
{
	rput32(0x00000d01); 
		rput32(0x04000000);
	rput32(0xc0022100); 
		rput32(0x00000081); rput32(0xffffffff); rput32(0x80010000);
	rput32(0xc0022100); 
		rput32(0x00000082); rput32(0xffffffff); rput32(0x00000000);
	rput32(0x00000e42); 
		rput32(0x00001f60);
	rput32(0x00000c85); 
		rput32(0x00000003);
	rput32(0x0000057c); 
		rput32(0x0badf00d);
	rput32(0x0000057b); 
		rput32(0x00000000);
}

void Xe_pGInit6(struct XenosDevice *xe)
{
	Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, 1024, 720);
	rput32(0x0002857e); 
		rput32(0x00010017); rput32(0x00000000); rput32(0x03ff02cf);
	rput32(0x0002857e); 
		rput32(0x00010017); rput32(0x00000004); rput32(0x03ff02cf);
}

void Xe_pGInit7(struct XenosDevice *xe)
{
	rput32(0x000005c8); 
		rput32(0x00020000);
	rput32(0x00000f01); 
		rput32(0x0000200e);
}

void Xe_pGInit8(struct XenosDevice *xe)
{
	Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, 1024, 720);
}

void Xe_pGInit9(struct XenosDevice *xe)
{
	int i;
	rput32(0x0000057e);
		rput32(0x00010019);
		
	Xe_pGInit0(xe);
	
	for (i = 0x10; i <= 0x70; ++i)
		Xe_pGInit1(xe, 0x00000000 | (i << 12) | ((0x80 - i) << 4));

	Xe_pGInit2(xe);
	rput32(0x0000057e); 
		rput32(0x0001001a);

	Xe_pGInit8(xe);
}

void Xe_pGInit10(struct XenosDevice *xe)
{
	Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, 1024, 720);

	rput32(0x0000057e); 
		rput32(0x00010019);
	rput32(0xc0003b00); 
		rput32(0x00000300);

	Xe_pGInit7(xe);

	Xe_pGInit9(xe);
}

void Xe_pGInit(struct XenosDevice *xe)
{
	rput32(0xc0114800); 
		rput32(0x000003ff); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000080); rput32(0x00000100); rput32(0x00000180); rput32(0x00000200); 
		rput32(0x00000280); rput32(0x00000300); rput32(0x00000380); rput32(0x00010800); 
		rput32(0x00000007); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000);

	Xe_pGInit4(xe);

	Xe_pGInit5(xe);
	Xe_pGInit6(xe);
	Xe_pGInit10(xe);
}

void Xe_DirtyAluConstant(struct XenosDevice *xe, int base, int len)
{
	len += base & 15;
	base >>= 4;
	while (len > 0)
	{
		xe->alu_dirty |= 1 << base;
		++base;
		len -= 16;
	}
	xe->dirty |= DIRTY_ALU;
}

void Xe_DirtyFetch(struct XenosDevice *xe, int base, int len)
{
	len += base % 3;
	base /= 3;
	while (len > 0)
	{
		xe->fetch_dirty |= 1 << base;
		++base;
		len -= 3;
	}
	xe->dirty |= DIRTY_FETCH;
}

struct XenosShader *Xe_LoadShader(struct XenosDevice *xe, const char *filename)
{
	FILE *f = fopen(filename, "rb");
	if (!f)
		Xe_Fatal(xe, "FATAL: shader %s not found!\n", filename);

	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, SEEK_SET);

	void *m = malloc(size);
	fread(m, size, 1, f);
	fclose(f);
	
	return Xe_LoadShaderFromMemory(xe, m);
}

struct XenosShader *Xe_LoadShaderFromMemory(struct XenosDevice *xe, void *m)
{
	struct XenosShaderHeader *hdr = m;
	
	if ((hdr->magic >> 16) != 0x102a)
		Xe_Fatal(xe, "shader version: %08x, expected: something with 102a.\n", hdr->magic);

	struct XenosShader *s = malloc(sizeof(struct XenosShader));
	memset(s, 0, sizeof(*s));
	s->shader = m;

	struct XenosShaderData *data = m + hdr->off_shader;
	s->program_control = data->program_control;
	s->context_misc = data->context_misc;

	return s;
}

void Xe_pUploadShaderConstants(struct XenosDevice *xe, struct XenosShader *s)
{
	struct XenosShaderHeader *hdr = s->shader;
	
	if (hdr->off_constants)
	{
			/* upload shader constants */
//		printf("off_constants: %d\n", hdr->off_constants);
		void *constants = s->shader + hdr->off_constants;
		
		constants += 16;

//		int size = *(u32*)constants;
		constants += 4;
		
//		printf("uploading shader constants..\n");

		// float constants
		for(;;)
		{
			u16 start = *(u16*)constants; constants += 2;
			u16 count = *(u16*)constants; constants += 2;

			if(!count) break;
			
			u32 offset = *(u32*)constants; constants += 4;

			float *c = s->shader + hdr->offset + offset;
			memcpy(xe->alu_constants + start * 4, c, count * 4);
			Xe_DirtyAluConstant(xe, start, count);

/*			int i;
			printf("float cst count: %d\n",count);
			for(i=0;i<count;++i) printf("%f ",c[i]);
			printf("\n");*/
		}

		// int constants
		for(;;)
		{
			u16 start = *(u16*)constants; constants += 2;
			u16 count = *(u16*)constants; constants += 2;

			if(!count) break;

			memcpy(&xe->integer_constants[(start-0x2300)/4],constants,count*4);
			xe->dirty |= DIRTY_INTEGER;

/*			int i;
			printf("int cst count: %d\n",count);
			for(i=0;i<count*2;++i) printf("%04x ",((u16*)constants)[i]);
			printf("\n");*/

			constants += count * 4 + 4;
		}
	}
}

int Xe_VBFCalcSize(struct XenosDevice *xe, const struct XenosVBFElement *fmt)
{
	switch (fmt->fmt)
	{
	case 6: // char4
		return 4;
	case 37: // float2
		return 8;
	case 38: // float4
		return 16;
	case 57: // float3
		return 12;
	default:
		Xe_Fatal(xe, "Unknown VBF %d!\n", fmt->fmt);
	}
}

int Xe_pVBFNrComponents(struct XenosDevice *xe, const struct XenosVBFElement *fmt)
{
	switch (fmt->fmt)
	{
	case 6: // char4
		return 4;
	case 37: // float2
		return 2;
	case 38: // float4
		return 4;
	case 57: // float3
		return 3;
	default:
		Xe_Fatal(xe, "Unknown VBF %d!\n", fmt->fmt);
	}
}

int Xe_VBFCalcStride(struct XenosDevice *xe, const struct XenosVBFFormat *fmt)
{
	int i;
	int total_size = 0;
	for (i=0; i<fmt->num; ++i)
		total_size += Xe_VBFCalcSize(xe, &fmt->e[i]);
	return total_size;
}

void Xe_pInvalidateGpuCache(struct XenosDevice *xe, int base, int size)
{
	rput32(0x00000a31);
		rput32(0x01000000);
	rput32(0x00010a2f);
		rput32(size); rput32(base);
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008);
}

void Xe_pInvalidateGpuCacheAll(struct XenosDevice *xe, int base, int size)
{
	rput32(0x00000a31);
		rput32(0x03000100);
	rput32(0x00010a2f);
		rput32(size); rput32(base);
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008);
}

void Xe_pUnlock(struct XenosDevice *xe, struct XenosLock *lock)
{
	if (!lock->start)
		Xe_Fatal(xe, "unlock without lock");
	if (lock->flags & XE_LOCK_WRITE)
	{
		Xe_pSyncToDevice(xe, lock->start, lock->size);
		Xe_pInvalidateGpuCache(xe, lock->phys, lock->size);
	}
	lock->start = 0;
}

void Xe_pLock(struct XenosDevice *xe, struct XenosLock *lock, void *addr, u32 phys, int size, int flags)
{
	size=(size+4095)&~4095;
    
        if (!flags)
		Xe_Fatal(xe, "flags=0");
	if (lock->start)
		Xe_Fatal(xe, "locked twice");
	if (lock->flags & XE_LOCK_READ)
	{
			/* *you* must make sure that the GPU already flushed this content. (usually, it is, though) */
		Xe_pSyncFromDevice(xe, addr, size);
	}
	lock->start = addr;
	lock->phys = phys;
	lock->size = size;
	lock->flags = flags;
}


	/* shaders are not specific to a vertex input format.
	   the vertex format specified in a vertex shader is just
	   dummy. Thus we need to patch the vfetch instructions to match
	   our defined structure. */
void Xe_ShaderApplyVFetchPatches(struct XenosDevice *xe, struct XenosShader *sh, unsigned int index, const struct XenosVBFFormat *fmt)
{
	assert(index < XE_SHADER_MAX_INSTANCES);
	assert(sh->shader_phys[index]);

	struct XenosLock lock;
	memset(&lock, 0, sizeof(lock));
	Xe_pLock(xe, &lock, sh->shader_instance[index], sh->shader_phys[index], sh->shader_phys_size, XE_LOCK_READ|XE_LOCK_WRITE);

	int stride = Xe_VBFCalcStride(xe, fmt);

	if (stride & 3)
		Xe_Fatal(xe, "your vertex buffer format is not DWORD aligned.\n");

	stride /= 4;

	struct XenosShaderHeader *hdr = sh->shader;
	struct XenosShaderData *data = sh->shader + hdr->off_shader;
	
	void *shader_code = sh->shader_instance[index];
	u32 *c = &data->unk1[2];
	int skip = *c++;
	int num_vfetch = *c;
	++c;

	c += skip * 2;
	int i;

	int fetched_to = 0;

	for (i=0; i<num_vfetch; ++i)
	{
		u32 vfetch_patch = *c++;
		int type = (vfetch_patch >> 12) & 0xF;
		int stream = (vfetch_patch >> 16) & 0xF;
		int insn = vfetch_patch & 0xFFF;
		
//		printf("raw: %08x\n", vfetch_patch);
//		printf("type=%d, stream=%d, insn=%d\n", type, stream, insn);
		u32 *vfetch = shader_code + insn * 12;
//		printf("  old vfetch: %08x %08x %08x\n", vfetch[0], vfetch[1], vfetch[2]);
//		printf("    old swizzle: %08x\n", vfetch[1] & 0xFFF);
		
		int Offset = (vfetch[2] & 0x7fffff00) >> 8;
		int DataFormat = (vfetch[1] & 0x003f0000) >> 16;
		int Stride= (vfetch[2] & 0x000000ff);
		int Signed= (vfetch[1] & 0x00001000) >> 12;
		int NumFormat = (vfetch[1] & 0x00002000) >> 13;
		int PrefetchCount= (vfetch[0] & 0x38000000) >> 27;
//		printf("  old Offset=%08x, DataFormat=%d, Stride=%d, Signed=%d, NumFormat=%d, PrefetchCount=%d\n",
//			Offset,DataFormat, Stride, Signed, NumFormat, PrefetchCount);
		
			/* let's find the element which applies for this. */
		int j;
		int offset = 0;
		for (j=0; j < fmt->num; ++j)
		{
			if ((fmt->e[j].usage == type) && (fmt->e[j].index == stream))
				break;
			offset += Xe_VBFCalcSize(xe, &fmt->e[j]);
		}
		
		offset /= 4;
		
		if (j == fmt->num)
			Xe_Fatal(xe, "shader requires input type %d_%d, which wasn't found in vertex format.\n", type, stream);

		Offset = offset;
		DataFormat = fmt->e[j].fmt;
		
		Signed = 0;
		Stride = stride;
		NumFormat = 0; // fraction

		if (DataFormat != 6)
			NumFormat = 1;
		
		int to_fetch = 0;

			/* if we need fetching... */
		if (fetched_to <= offset + ((Xe_VBFCalcSize(xe, &fmt->e[j])+3)/4))
			to_fetch = stride - fetched_to;

		if (to_fetch > 8)
			to_fetch = 8;
		to_fetch = 1; /* FIXME: prefetching doesn't always work. */

		int is_mini = 0;
		
		if (to_fetch == 0)
		{
			PrefetchCount = 0;
			is_mini = 1;
		} else
			PrefetchCount = to_fetch - 1;
		
		fetched_to += to_fetch;

			/* patch vfetch instruction */
		vfetch[0] &= ~(0x00000000|0x00000000|0x00000000|0x00000000|0x00000000|0x38000000|0x00000000);
		vfetch[1] &= ~(0x00000000|0x003f0000|0x00000000|0x00001000|0x00002000|0x00000000|0x40000000);
		vfetch[2] &= ~(0x7fffff00|0x00000000|0x000000ff|0x00000000|0x00000000|0x00000000|0x00000000);

		vfetch[2] |= Offset << 8;
		vfetch[1] |= DataFormat << 16;
		vfetch[2] |= Stride;
		vfetch[1] |= Signed << 12;
		vfetch[1] |= NumFormat << 13;
		vfetch[0] |= PrefetchCount << 27;
		vfetch[1] |= is_mini << 30;
		
//		printf("specified swizzle: %08x\n", fmt->e[j].swizzle);
		
		int comp;
		int nrcomp = Xe_pVBFNrComponents(xe, &fmt->e[j]);
		for (comp = 0; comp < 4; comp++)
		{
			int shift = comp * 3;
			int sw = (vfetch[1] >> shift) & 7; /* see original swizzle, xyzw01_? */
//			printf("comp%d sw=%c ", comp, "xyzw01?_"[sw]);
			if ((sw < 4) && (sw >= nrcomp)) /* refer to an unavailable position? */
			{
				if (sw == 3) // a/w
					sw = 5; // 1
				else
					sw = 4; // 0
			}
//			printf(" -> %c\n", "xyzw01?_"[sw]);
			vfetch[1] &= ~(7<<shift);
			vfetch[1] |= sw << shift;
		}
		

		Offset = (vfetch[2] & 0x7fffff00) >> 8;
		DataFormat = (vfetch[1] & 0x003f0000) >> 16;
		Stride= (vfetch[2] & 0x000000ff);
		Signed= (vfetch[1] & 0x00001000) >> 12;
		NumFormat = (vfetch[1] & 0x00002000) >> 13;
		PrefetchCount= (vfetch[0] & 0x38000000) >> 27;
//		printf("  new Offset=%08x, DataFormat=%d, Stride=%d, Signed=%d, NumFormat=%d, PrefetchCount=%d\n",
//			Offset,DataFormat, Stride, Signed, NumFormat, PrefetchCount);
//		printf("  new vfetch: %08x %08x %08x\n", vfetch[0], vfetch[1], vfetch[2]);
	}

	Xe_pUnlock(xe, &lock);
}

void Xe_InstantiateShader(struct XenosDevice *xe, struct XenosShader *sh, unsigned int index)
{
	assert(index < XE_SHADER_MAX_INSTANCES);
	struct XenosShaderHeader *hdr = sh->shader;
	struct XenosShaderData *data = sh->shader + hdr->off_shader;
	void *shader_code = sh->shader + data->sh_off + hdr->offset;
	
	sh->shader_phys_size = data->sh_size;
	void *p = Xe_pAlloc(xe, &sh->shader_phys[index], data->sh_size, 0x100);
	memcpy(p, shader_code, data->sh_size);
	Xe_pSyncToDevice(xe, p, data->sh_size);
	sh->shader_instance[index] = p;
}

int Xe_GetShaderLength(struct XenosDevice *xe, void *sh)
{
	struct XenosShaderHeader *hdr = sh;
	struct XenosShaderData *data = sh + hdr->off_shader;
	return data->sh_off + hdr->offset + data->sh_size;
}

void Xe_Init(struct XenosDevice *xe)
{
	memset(xe, 0, sizeof(*xe));

	xe->regs = (void*)0xec800000;

	xe->default_fb.ptr = r32(0x6110);
	xe->default_fb.wpitch = r32(0x6120) * 4;
	xe->default_fb.width = r32(0x6134);
	xe->default_fb.height = r32(0x6138);
	xe->default_fb.bypp = 4;
	xe->default_fb.base = (void*)(long)xe->default_fb.ptr;
	xe->default_fb.format = XE_FMT_BGRA | XE_FMT_8888;
	xe->default_fb.tiled = 1;
    
    xe->tex_fb=xe->default_fb;
	
	printf("[xe] Framebuffer %d x %d @ %08x\n", xe->tex_fb.width, xe->tex_fb.height, xe->tex_fb.ptr);

	u32 rb_phys=0;
	xe->rb = Xe_pAlloc(xe,&rb_phys,WRITEBACK_ZONE_SIZE,WRITEBACK_ZONE_SIZE);

	u32 rb_primary_phys = Xe_pRBAlloc(xe);

	Xe_pMasterInit(xe, rb_primary_phys);
	Xe_pEnableWriteback(xe, rb_phys + RPTR_WRITEBACK, 6);
	
	Xe_pSyncFromDevice(xe, xe->rb + RPTR_WRITEBACK, 4);
	
	Xe_pWriteReg(xe, 0x0774, rb_phys + SCRATCH_WRITEBACK);
	Xe_pWriteReg(xe, 0x0770, 0x20033);

	Xe_pWriteReg(xe, 0x15e0, 0x1234567);
	
	Xe_pGInit(xe);

	Xe_pInvalidateGpuCache(xe, 0, 0x20000000); // whole DRAM
	
	xe->clear_color=0; // color clear: black
	xe->clear_stencil_z=0xffffff00; // zbuffer / stencil clear: z to -1, stencil to 0
}

void Xe_SetRenderTarget(struct XenosDevice *xe, struct XenosSurface *rt)
{
	xe->rt = rt;
	xe->vp_xres = rt->width;
	xe->vp_yres = rt->height;

	xe->msaa_samples = 0;
	xe->edram_colorformat = 0;

	int tile_size_x = (xe->msaa_samples < 2) ? 80 : 40, tile_size_y = (xe->msaa_samples > 0) ? 8 : 16;
	if ((xe->edram_colorformat == 15) || (xe->edram_colorformat == 7) || (xe->edram_colorformat == 5))
		tile_size_x /= 2;
	int tiles_per_line = (xe->vp_xres + tile_size_x - 1) / tile_size_x;
	tiles_per_line += 1;
	tiles_per_line &= ~1;

	int tiles_height = (xe->vp_yres + tile_size_y - 1) / tile_size_y;

	// what about 64bit targets?

	xe->edram_pitch = tiles_per_line * tile_size_x;
	xe->edram_hizpitch = tiles_per_line * tile_size_x;

#if 0
 	xe->edram_color0base = 0;
	xe->edram_depthbase = tiles_per_line * tiles_height;
#else
	xe->edram_color0base = tiles_per_line * tiles_height;
	xe->edram_depthbase = 0;
#endif
}

void Xe_pSetEDRAMLayout(struct XenosDevice *xe)
{
	rput32(0x00022000);
		rput32(SurfaceInfo(xe->edram_pitch, xe->msaa_samples, xe->edram_hizpitch));  // SurfaceInfo
		rput32((xe->edram_colorformat << 16) | xe->edram_color0base);
		rput32(xe->edram_depthbase | (0<<16) ); // depth info, float Z
}

void Xe_pSetClearValues(struct XenosDevice *xe)
{
	Xe_pWriteReg(xe, 0x8c74, xe->clear_stencil_z);
	
	unsigned int clearv[2];
	
	switch (xe->edram_colorformat)
	{
		case 0:
		case 1:
			clearv[0] = clearv[1] = xe->clear_color;
			break;
		case 4:
		case 5:
			clearv[0]  = (xe->clear_color & 0xFF000000);
			clearv[0] |= (xe->clear_color & 0x00FF0000)>>8;
			clearv[0] >>= 1;
			clearv[0] |= (clearv[0] >> 8) & 0x00FF00FF;
			clearv[1]  = (xe->clear_color & 0x0000FF00)<<16;
			clearv[1] |= (xe->clear_color & 0x000000FF)<<8;
			clearv[1] >>= 1;
			clearv[1] |= (clearv[1] >> 8) & 0x00FF00FF;
			break;
		default:
			clearv[0] = clearv[1] = 0;
	}
	
	Xe_pWriteReg(xe, 0x8c78, clearv[0]);
	Xe_pWriteReg(xe, 0x8c7c, clearv[1]);
}

void Xe_ResolveInto(struct XenosDevice *xe, struct XenosSurface *surface, int source, int clear)
{
	Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, surface->width, surface->height);
	
	Xe_VBBegin(xe, 2);
	float vbdata[] = 
	{
		-.5, -.5, /* never ever dare to mess with these values. NO, you can not resolve arbitrary areas or even shapes. */
		 surface->width - .5,
		 0,
		 surface->width - .5,
		 surface->height - .5
	};
	
	Xe_VBPut(xe, vbdata, sizeof(vbdata) / 4);
	struct XenosVertexBuffer *vb = Xe_VBEnd(xe);
	Xe_VBPoolAdd(xe, vb);

	Xe_pSetEDRAMLayout(xe);

	rput32(0x00002104); 
		rput32(0x0000000f); // colormask 
	rput32(0x0005210f); 
		rput32(0x44000000); rput32(0x44000000);
		rput32(0xc3b40000); rput32(0x43b40000); 
		rput32(0x3f800000); rput32(0x00000000); 

	int msaavals[] = {0,4,6};
	int pitch;
	switch (surface->format & XE_FMT_MASK)
	{
		case XE_FMT_8888: pitch = surface->wpitch / 4; break;
		case XE_FMT_16161616: pitch = surface->wpitch / 8; break;
		default: Xe_Fatal(xe, "unsupported resolve target format");
	}
	rput32(0x00032318); 
		rput32(0x00100000 | (msaavals[xe->msaa_samples]<<4) | (clear << 8) | source ); // 300 = color,depth clear enabled!
		rput32(surface->ptr);
		rput32(xy32(pitch, surface->height));
		rput32(0x01000000 | ((surface->format&XE_FMT_MASK)<<7) | ((surface->format&~XE_FMT_MASK)>>6));

	Xe_pSetClearValues(xe);
		
	// edram copy
	
	rput32(0x00002208); rput32(0x00000006);
	rput32(0x00002208); rput32((clear&XE_CLEAR_COLOR || surface->base)?6:5);
	rput32(0x00002200); rput32(0x8777);
	rput32(0x000005c8); rput32(0x00020000);
	
	// invalidate state
	
	rput32(0xc0003b00); rput32(0x00000100);

	// load a shader
	
	rput32(0xc0102b00); rput32(0x00000000);
		rput32(0x0000000f); 

		rput32(0x10011002); rput32(0x00001200); rput32(0xc4000000);
		rput32(0x00000000); rput32(0x1003c200); rput32(0x22000000); 
		rput32(0x00080000);	rput32(0x00253b48); rput32(0x00000002); 
		rput32(0xc80f803e); rput32(0x00000000);	rput32(0xc2000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);

	rput32(0x00012180); rput32(0x00010002); rput32(0x00000000); 

	// prepare draw edram FB to real dram FB
	
	rput32(0x00002203); rput32(0x00000000);
	rput32(0x00022100); rput32(0x0000ffff); rput32(0x00000000); rput32(0x00000000); 
	rput32(0x00022204); rput32(0x00010000); rput32(0x00010000); rput32(0x00000300); 
	rput32(0x00002312); rput32(0x0000ffff); 
	rput32(0x0000200d); rput32(0x00000000);

	rput32(0x00054800); rput32((vb->phys_base) | 3); rput32(0x1000001a); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);

	rput32(0x00025000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);

	// draw command
	
	rput32(0xc0003600); rput32(0x00030088); 
	rput32(0xc0004600); rput32(0x00000006); 
	rput32(0x00002007); rput32(0x00000000); 
	
	// flush dram FB
	
	Xe_pInvalidateGpuCacheAll(xe, surface->ptr, surface->wpitch * surface->height);

	// cleanup
	
	rput32(0x0000057e); rput32(0x00010001); 
	rput32(0x00002318); rput32(0x00000000);
	rput32(0x0000231b); rput32(0x00000000);

	xe->dirty = ~0;
}

void Xe_Clear(struct XenosDevice *xe, int flags)
{
	struct XenosSurface surface = *xe->rt;
	surface.base = NULL;
	
	Xe_ResolveInto(xe, &surface, 0, flags);
}

void Xe_Resolve(struct XenosDevice *xe)
{
	Xe_ResolveInto(xe, xe->rt, XE_SOURCE_COLOR, XE_CLEAR_COLOR|XE_CLEAR_DS);
}


void VERTEX_FETCH(u32 *dst, u32 base, int len)
{
	dst[0] = base | 3;
	dst[1] = 0x10000002 | (len << 2);
}

void TEXTURE_FETCH(u32 *dst, u32 base, int width, int height, int pitch, int tiled, int format, u32 base_mip, int anisop, int filter, int uaddr, int vaddr)
{
	switch (format & XE_FMT_MASK)
	{
	case XE_FMT_8: pitch /= 32; break;
	case XE_FMT_5551: pitch /= 64; break;
	case XE_FMT_565: pitch /= 64; break;
	case XE_FMT_16161616: pitch /= 256; break;
	case XE_FMT_8888: pitch /= 128; break;
	default: abort();
	}

    dst[0] = 0x00000002 | (pitch << 22) | (tiled << 31) | (uaddr << 10) | (vaddr << 13);
	dst[1] = 0x00000000 | base | format; /* BaseAddress */
	dst[2] = (height << 13) | width;
	dst[3] = 0x00a00c14 | (anisop << 25) | (filter <<19);
	if (base_mip)
		dst[4] = 0x00000e03;
	else
		dst[4] = 0;
	dst[5] = 0x00000a00 | base_mip; /* MipAddress */
}

void Xe_pLoadShader(struct XenosDevice *xe, int base, int type, int size)
{
	rput32(0xc0012700);
		rput32(base | type); 
		rput32(size);
}

void Xe_pAlign(struct XenosDevice *xe)
{
	while ((xe->rb_secondary_wptr&3) != 3)
		rput32(0x80000000);
}

void Xe_pBlockUntilIdle(struct XenosDevice *xe)
{
	Xe_pWriteReg(xe, 0x1720, 0x20000);
}

void Xe_pStep(struct XenosDevice *xe, int x)
{
	Xe_pWriteReg(xe, 0x15e0, x);
}

void Xe_pStuff(struct XenosDevice *xe)
{
	rput32(0x00072380);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000);
}


void Xe_Fatal(struct XenosDevice *xe, const char *fmt, ...)
{
	va_list arg;
	printf("[xe] Fatal error: ");
    va_start(arg, fmt);
	vprintf(fmt, arg);
	va_end(arg);
	printf("\n");
	abort();
}

struct XenosSurface *Xe_GetFramebufferSurface(struct XenosDevice *xe)
{
	return &xe->tex_fb;
}

void Xe_SetFrameBufferSurface(struct XenosDevice *xe, struct XenosSurface *fb)
{
	if(!fb->tiled)
        Xe_Fatal(xe, "new framebuffer surface should be tiled.\n");

	if(fb->bypp!=4)
        Xe_Fatal(xe, "new framebuffer surface format should be 32bit (BGRA8888).\n");
    
    w32(0x6110,fb->ptr);
	w32(0x6120,fb->wpitch/4);
    w32(0x6134,fb->width);
    w32(0x6138,fb->height);

    xe->tex_fb.ptr = r32(0x6110);
	xe->tex_fb.wpitch = r32(0x6120) * 4;
	xe->tex_fb.width = r32(0x6134);
	xe->tex_fb.height = r32(0x6138);
	xe->tex_fb.bypp = 4;
	xe->tex_fb.base = (void*)(long)xe->tex_fb.ptr;
}

void Xe_Execute(struct XenosDevice *xe)
{
	Xe_pBlockUntilIdle(xe);
	Xe_pRBKick(xe);
}

void Xe_pDebugSync(struct XenosDevice *xe)
{
	Xe_pWriteReg(xe, 0x15e0, xe->frameidx);

	Xe_Execute(xe);
	
//	printf("waiting for frameidx %08x\n", xe->frameidx);
	int timeout = 1<<24;
	do {
		Xe_pSyncFromDevice(xe, xe->rb + SCRATCH_WRITEBACK, 4);
		if (!timeout--)
			Xe_Fatal(xe, "damn, the GPU seems to hang. There is no (known) way to recover, you have to reboot.\n");
//		udelay(1000);
	} while (*(volatile u32*)(xe->rb + SCRATCH_WRITEBACK) != xe->frameidx) ;
	xe->frameidx++;
//	printf("done\n");
}

void Xe_Sync(struct XenosDevice *xe)
{
	Xe_pDebugSync(xe);
	Xe_VBReclaim(xe);
}

int stat_alu_uploaded = 0;

void Xe_pUploadALUConstants(struct XenosDevice *xe)
{
	while (xe->alu_dirty)
	{
		int start, end;
		for (start = 0; start < 32; ++start)
			if (xe->alu_dirty & (1<<start))
				break;
		for (end = start; end < 32; ++end)
			if (!(xe->alu_dirty & (1<<end)))
				break;
			else
				xe->alu_dirty &= ~(1<<end);
		
		int base = start * 16;
		int num = (end - start) * 16 * 4;
		
		stat_alu_uploaded += num;
		Xe_pAlign(xe);
		rput32(0x00004000 | (base * 4) | ((num-1) << 16));
			rput(xe->alu_constants + base * 4, num);
	}
}

void Xe_pUploadFetchConstants(struct XenosDevice *xe)
{
	while (xe->fetch_dirty)
	{
		int start, end;
		for (start = 0; start < 32; ++start)
			if (xe->fetch_dirty & (1<<start))
				break;
		for (end = start; end < 32; ++end)
			if (!(xe->fetch_dirty & (1<<end)))
				break;
			else
				xe->fetch_dirty &= ~(1<<end);
		
		int base = start * 3;
		int num = (end - start) * 3 * 2;
		
		stat_alu_uploaded += num;
		Xe_pAlign(xe);
		rput32(0x00004800 | (base * 2) | ((num-1) << 16));
			rput(xe->fetch_constants + base * 2, num);
	}
}

void Xe_pUploadClipPlane(struct XenosDevice *xe)
{
	Xe_pAlign(xe);
	rput32(0x00172388);
		rput(xe->clipplane, 6*4);
}

void Xe_pUploadIntegerConstants(struct XenosDevice *xe)
{
	Xe_pAlign(xe);
	rput32(0x00274900);
		rput(xe->integer_constants, 10*4);
}

void Xe_pUploadControl(struct XenosDevice *xe)
{
	rput32(0x00082200);
		rput(xe->controlpacket, 9);
}

void Xe_pUploadShader(struct XenosDevice *xe)
{
	u32 program_control = 0, context_misc = 0;
	if (xe->ps)
	{
		Xe_pLoadShader(xe, xe->ps->shader_phys[0], SHADER_TYPE_PIXEL, xe->ps->shader_phys_size);
		Xe_pUploadShaderConstants(xe, xe->ps);
		program_control |= xe->ps->program_control;
		context_misc |= xe->ps->context_misc;
	}

	if (xe->vs)
	{
		Xe_pLoadShader(xe, xe->vs->shader_phys[xe->vs_index], SHADER_TYPE_VERTEX, xe->vs->shader_phys_size);
		Xe_pUploadShaderConstants(xe, xe->vs);
		program_control |= xe->vs->program_control;
		context_misc |= xe->vs->context_misc;
	}
	
	rput32(0x00022180);
		rput32(program_control);
		rput32(context_misc);
		rput32(0xFFFFFFFF);  /* interpolation mode */
}

void Xe_pInitControl(struct XenosDevice *xe)
{
	xe->controlpacket[0] = 0x00700736|0x80;  // DEPTH
	xe->controlpacket[1] = 0x00010001;  // BLEND
	xe->controlpacket[2] = 0x87000007;  // COLOR
	xe->controlpacket[3] = 0x00000000;  // HI
	xe->controlpacket[4] = 0x00080000;  // CLIP
	xe->controlpacket[5] = 0x00010006;  // MODE
	if (xe->msaa_samples)
		xe->controlpacket[5] |= 1<<15;
	xe->controlpacket[6] = 0x0000043f;  // VTE
	xe->controlpacket[7] = 0;
	xe->controlpacket[8] = 0x00000004; // EDRAM
	
	xe->stencildata[0] = 0xFFFF00;
	xe->stencildata[1] = 0xFFFF00;
	
	xe->dirty |= DIRTY_CONTROL|DIRTY_MISC;
}

void Xe_SetZFunc(struct XenosDevice *xe, int z_func)
{
	xe->controlpacket[0] = (xe->controlpacket[0]&~0x70) | (z_func<<4);
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetZWrite(struct XenosDevice *xe, int zw)
{
	xe->controlpacket[0] = (xe->controlpacket[0]&~4) | (zw<<2);
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetZEnable(struct XenosDevice *xe, int ze)
{
	xe->controlpacket[0] = (xe->controlpacket[0]&~2) | (ze<<1);
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetFillMode(struct XenosDevice *xe, int front, int back)
{
	xe->controlpacket[5] &= ~(0x3f<<5);
	xe->controlpacket[5] |= front << 5;
	xe->controlpacket[5] |= back << 8;
	xe->controlpacket[5] |= 1<<3;

	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetBlendControl(struct XenosDevice *xe, int col_src, int col_op, int col_dst, int alpha_src, int alpha_op, int alpha_dst)
{
	xe->controlpacket[1] = col_src | (col_op << 5) | (col_dst << 8) | (alpha_src << 16) | (alpha_op << 21) | (alpha_dst << 24);
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetSrcBlend(struct XenosDevice *xe, unsigned int blend)
{
	assert(blend < 32);
	xe->controlpacket[1] &= ~0x1F;
	xe->controlpacket[1] |= blend;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetDestBlend(struct XenosDevice *xe, unsigned int blend)
{
	assert(blend < 32);
	xe->controlpacket[1] &= ~(0x1F<<8);
	xe->controlpacket[1] |= blend<<8;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetBlendOp(struct XenosDevice *xe, unsigned int blendop)
{
	assert(blendop < 8);
	xe->controlpacket[1] &= ~(0x7<<5);
	xe->controlpacket[1] |= blendop<<5;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetSrcBlendAlpha(struct XenosDevice *xe, unsigned int blend)
{
	assert(blend < 32);
	xe->controlpacket[1] &= ~(0x1F<<16);
	xe->controlpacket[1] |= blend << 16;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetDestBlendAlpha(struct XenosDevice *xe, unsigned int blend)
{
	assert(blend < 32);
	xe->controlpacket[1] &= ~(0x1F<<24);
	xe->controlpacket[1] |= blend<< 24;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetBlendOpAlpha(struct XenosDevice *xe, unsigned int blendop)
{
	assert(blendop < 8);
	xe->controlpacket[1] &= ~(0x7<<21);
	xe->controlpacket[1] |= blendop<<21;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetCullMode(struct XenosDevice *xe, unsigned int cullmode)
{
	assert(cullmode < 8);
	xe->controlpacket[5] &= ~7;
	xe->controlpacket[5] |= cullmode;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetAlphaTestEnable(struct XenosDevice *xe, int enable)
{
	xe->controlpacket[2] &= ~8;
	xe->controlpacket[2] |= (!!enable) << 3;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetAlphaFunc(struct XenosDevice *xe, unsigned int func)
{
	assert(func <= 7);
	xe->controlpacket[2] &= ~7;
	xe->controlpacket[2] |= func;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetAlphaRef(struct XenosDevice *xe, float alpharef)
{
	xe->alpharef = alpharef;
	xe->dirty |= DIRTY_MISC;
}

void Xe_SetStencilFunc(struct XenosDevice *xe, int bfff, unsigned int func)
{
	assert(func <= 7);
	if (bfff & 1)
	{
		xe->controlpacket[0] &= ~(7<<8);
		xe->controlpacket[0] |= func << 8;
	}
	if (bfff & 2)
	{
		xe->controlpacket[0] &= ~(7<<20);
		xe->controlpacket[0] |= func << 20;
	}
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetStencilEnable(struct XenosDevice *xe, unsigned int enable)
{
	assert(enable <= 1);
	xe->controlpacket[0] &= ~1;
	xe->controlpacket[0] |= enable;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetStencilOp(struct XenosDevice *xe, int bfff, int fail, int zfail, int pass)
{
	assert(fail <= 7);
	assert(zfail <= 7);
	assert(pass <= 7);
	
	if (bfff & 1)
	{
		if (fail >= 0)
		{
			xe->controlpacket[0] &= ~(7<<11);
			xe->controlpacket[0] |= fail << 11;
		}
		if (pass >= 0)
		{
			xe->controlpacket[0] &= ~(7<<14);
			xe->controlpacket[0] |= pass << 14;
		}
		if (zfail >= 0)
		{
			xe->controlpacket[0] &= ~(7<<17);
			xe->controlpacket[0] |= zfail << 17;
		}
	}
	if (bfff & 2)
	{
		if (fail >= 0)
		{
			xe->controlpacket[0] &= ~(7<<23);
			xe->controlpacket[0] |= fail << 23;
		}
		if (pass >= 0)
		{
			xe->controlpacket[0] &= ~(7<<26);
			xe->controlpacket[0] |= pass << 26;
		}
		if (zfail >= 0)
		{
			xe->controlpacket[0] &= ~(7<<29);
			xe->controlpacket[0] |= zfail << 29;
		}
	}
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetStencilRef(struct XenosDevice *xe, int bfff, int ref)
{
	if (bfff & 1)
		xe->stencildata[1] = (xe->stencildata[1] & ~0xFF) | ref;

	if (bfff & 2)
		xe->stencildata[0] = (xe->stencildata[0] & ~0xFF) | ref;
	xe->dirty |= DIRTY_MISC;
}

void Xe_SetStencilMask(struct XenosDevice *xe, int bfff, int mask)
{
	if (bfff & 1)
		xe->stencildata[1] = (xe->stencildata[1] & ~0xFF00) | (mask<<8);

	if (bfff & 2)
		xe->stencildata[0] = (xe->stencildata[0] & ~0xFF00) | (mask<<8);
	xe->dirty |= DIRTY_MISC;
}

void Xe_SetStencilWriteMask(struct XenosDevice *xe, int bfff, int writemask)
{
	if (bfff & 1)
		xe->stencildata[1] = (xe->stencildata[1] & ~0xFF0000) | (writemask<<16);

	if (bfff & 2)
		xe->stencildata[0] = (xe->stencildata[0] & ~0xFF0000) | (writemask<<16);
	xe->dirty |= DIRTY_MISC;
}

void Xe_SetScissor(struct XenosDevice *xe, int enable, int left, int top, int right, int bottom)
{
	xe->scissor_enable=enable;
	if (left>=0) xe->scissor_ltrb[0]=left;
	if (top>=0) xe->scissor_ltrb[1]=top;
	if (right>=0) xe->scissor_ltrb[2]=right;
	if (bottom>=0) xe->scissor_ltrb[3]=bottom;
	xe->dirty |= DIRTY_MISC;
}

void Xe_SetClipPlaneEnables(struct XenosDevice *xe, int enables)
{
	xe->controlpacket[4] &= ~0x3f;
	xe->controlpacket[4] |= enables&0x3f;
	xe->dirty |= DIRTY_CONTROL;
}

void Xe_SetClipPlane(struct XenosDevice *xe, int idx, float * plane)
{
	assert(idx>=0 && idx<6);
	memcpy(&xe->clipplane[idx*4],plane,4*4);
	xe->dirty |= DIRTY_CLIP;
}

void Xe_InvalidateState(struct XenosDevice *xe)
{
	xe->dirty = ~0;
	xe->alu_dirty = ~0;
	xe->fetch_dirty = ~0;
	Xe_pInitControl(xe);
}

void Xe_SetShader(struct XenosDevice *xe, int type, struct XenosShader *sh, int index)
{
	struct XenosShader **s;
	int *i = 0;
	if (type == SHADER_TYPE_PIXEL)
	{
		s = &xe->ps;
	} else
	{
		s = &xe->vs;
		i = &xe->vs_index;
		assert(sh->shader_instance[index]);
	}

	if ((*s != sh) || (i && *i != index))
	{
		*s = sh;
		if (i)
			*i = index;
		xe->dirty |= DIRTY_SHADER;
	}
}

void Xe_pSetState(struct XenosDevice *xe)
{
	if (xe->dirty & DIRTY_CONTROL)
		Xe_pUploadControl(xe);

	if (xe->dirty & DIRTY_SHADER)
		Xe_pUploadShader(xe);

	if (xe->dirty & DIRTY_ALU)
		Xe_pUploadALUConstants(xe);
	
	if (xe->dirty & DIRTY_FETCH)
	{
		Xe_pUploadFetchConstants(xe);
		rput32(0x00025000);
			rput32(0x00000000); rput32(0x00025000); rput32(0x00000000);
	}

	if (xe->dirty & DIRTY_CLIP)
		Xe_pUploadClipPlane(xe);
	
	if (xe->dirty & DIRTY_INTEGER)
		Xe_pUploadIntegerConstants(xe);

	if (xe->dirty & DIRTY_MISC)
	{
		if (xe->scissor_enable)
			Xe_pSetSurfaceClip(xe, 0, 0, xe->scissor_ltrb[0], xe->scissor_ltrb[1], xe->scissor_ltrb[2], xe->scissor_ltrb[3]);
		else
			Xe_pSetSurfaceClip(xe, 0, 0, 0, 0, xe->vp_xres, xe->vp_yres);
		
		Xe_pSetEDRAMLayout(xe);
		rput32(0x0000200d);
			rput32(0x00000000);
		rput32(0x00012100);
			rput32(0x00ffffff);
			rput32(0x00000000);
		rput32(0x00002104);
			rput32(0x0000000f);
		rput32(0x0008210c);
			rput32(xe->stencildata[0]);
			rput32(xe->stencildata[1]);
			rputf(xe->alpharef); /* this does not work. */
			rputf(xe->vp_xres / 2.0);
			rputf(xe->vp_xres / 2.0);
			rputf(-xe->vp_yres / 2.0);
			rputf(xe->vp_yres / 2.0);
			rputf(1.0);
			rputf(0.0);

		int vals[] = {0, 2 | (4 << 13), 4 | (6 << 13)};
		rput32(0x00002301);
			rput32(vals[xe->msaa_samples]);
		rput32(0x00002312);
			rput32(0x0000ffff);
	}
	
	xe->dirty = 0;
}

void Xe_SetTexture(struct XenosDevice *xe, int index, struct XenosSurface *tex)
{
	if (tex!=NULL)
        TEXTURE_FETCH(xe->fetch_constants + index * 6, tex->ptr, tex->width - 1, tex->height - 1, tex->wpitch, tex->tiled, tex->format, tex->ptr_mip, 2, tex->use_filtering, tex->u_addressing, tex->v_addressing);
	else
		memset(xe->fetch_constants + index * 6,0,24);

	Xe_DirtyFetch(xe, index + index * 3, 3);
}

void Xe_SetClearColor(struct XenosDevice *xe, u32 clearcolor)
{
	xe->clear_color = clearcolor;
}

struct XenosVertexBuffer *Xe_CreateVertexBuffer(struct XenosDevice *xe, int size)
{
	struct XenosVertexBuffer *vb = malloc(sizeof(struct XenosVertexBuffer));
	memset(vb, 0, sizeof(struct XenosVertexBuffer));
	printf("--- alloc new vb, at %p, size %d\n", vb, size);
	vb->base = Xe_pAlloc(xe, &vb->phys_base, size, 0x1000);
	vb->size = 0;
	vb->space = size;
	vb->next = 0;
	vb->vertices = 0;
//	printf("alloc done, at %p %x\n", vb->base, vb->phys_base);
	return vb;
}

void Xe_DestroyVertexBuffer(struct XenosDevice *xe, struct XenosVertexBuffer *vb)
{
    Xe_pFree(xe,vb->base);
    free(vb);
}

struct XenosVertexBuffer *Xe_VBPoolAlloc(struct XenosDevice *xe, int size)
{
	struct XenosVertexBuffer **vbp = &xe->vb_pool;
	
	while (*vbp)
	{
		struct XenosVertexBuffer *vb = *vbp;
//		printf("use %d %d\n",vb->space,size);
		if (vb->space >= size)
		{
			*vbp = vb->next;
			vb->next = 0;
			vb->size = 0;
			vb->vertices = 0;
			return vb;
		}
		vbp = &vb->next;
	}

	return Xe_CreateVertexBuffer(xe, size);
}

void Xe_VBPoolAdd(struct XenosDevice *xe, struct XenosVertexBuffer *vb)
{
	struct XenosVertexBuffer **vbp = xe->vb_pool_after_frame ? &xe->vb_pool_after_frame->next : &xe->vb_pool_after_frame;
	while (*vbp)
		vbp = &(*vbp)->next;

	*vbp = vb;
}

void Xe_VBReclaim(struct XenosDevice *xe)
{
	struct XenosVertexBuffer **vbp = xe->vb_pool ? &xe->vb_pool->next : &xe->vb_pool;
	while (*vbp)
		vbp = &(*vbp)->next;
	
	*vbp = xe->vb_pool_after_frame;
	xe->vb_pool_after_frame = 0;
}

void Xe_VBBegin(struct XenosDevice *xe, int pitch)
{
	if (xe->vb_head || xe->vb_current)
		Xe_Fatal(xe, "FATAL: VertexBegin without VertexEnd! (head %08x, current %08x)\n", xe->vb_head, xe->vb_current);
	xe->vb_current_pitch = pitch;
}

void Xe_VBPut(struct XenosDevice *xe, void *data, int len)
{
	if (len % xe->vb_current_pitch)
		Xe_Fatal(xe, "FATAL: VertexPut with non-even len\n");
	
	while (len)
	{
		int remaining = xe->vb_current ? (xe->vb_current->space - xe->vb_current->size) / 4 : 0;
		
		remaining -= remaining % xe->vb_current_pitch;
		
//		printf("rem %d len %d\n",remaining,len);
		
		if (remaining > len)
			remaining = len;
		
		if (!remaining)
		{
			struct XenosVertexBuffer **n = xe->vb_head ? &xe->vb_current->next : &xe->vb_head;
			xe->vb_current = Xe_VBPoolAlloc(xe, VBPOOL_NUM_TRIANGLES*3*xe->vb_current_pitch);
			*n = xe->vb_current;
			continue;
		}
		
		memcpy(xe->vb_current->base + xe->vb_current->size, data, remaining * 4);
		xe->vb_current->size += remaining * 4;
		xe->vb_current->vertices += remaining / xe->vb_current_pitch;
		data += remaining * 4;
		len -= remaining;
	}
}

struct XenosVertexBuffer *Xe_VBEnd(struct XenosDevice *xe)
{
	struct XenosVertexBuffer *res;
	res = xe->vb_head;
	
	while (xe->vb_head)
	{
		Xe_pSyncToDevice(xe, xe->vb_head->base, xe->vb_head->space);
		Xe_pInvalidateGpuCache(xe, xe->vb_head->phys_base, xe->vb_head->space + 0x1000);
		xe->vb_head = xe->vb_head->next;
	}

	xe->vb_head = xe->vb_current = 0;

	return res;
}

void Xe_Draw(struct XenosDevice *xe, struct XenosVertexBuffer *vb, struct XenosIndexBuffer *ib)
{
	Xe_pStuff(xe);
	
	if (vb->lock.start)
		Xe_Fatal(xe, "cannot draw locked VB");
	if (ib && ib->lock.start)
		Xe_Fatal(xe, "cannot draw locked IB");

	while (vb)
	{
		Xe_SetStreamSource(xe, 0, vb, 0, 0);
		Xe_pSetState(xe);

		rput32(0x00002007);
		rput32(0x00000000);

		Xe_pSetIndexOffset(xe, 0);
		if (!ib)
		{
			Xe_pDrawNonIndexed(xe, vb->vertices, XE_PRIMTYPE_TRIANGLELIST);
		} else
			Xe_pDrawIndexedPrimitive(xe, XE_PRIMTYPE_TRIANGLELIST, ib->indices, ib->phys_base, ib->indices, ib->fmt);

		xe->tris_drawn += vb->vertices / 3;
		vb = vb->next;
	}
}

int Xe_pCalcVtxCount(struct XenosDevice *xe, int primtype, int primcnt)
{
	switch (primtype)
	{
	case XE_PRIMTYPE_POINTLIST: return primcnt;
	case XE_PRIMTYPE_LINELIST: return primcnt * 2;
	case XE_PRIMTYPE_LINESTRIP: return 1 + primcnt;
	case XE_PRIMTYPE_TRIANGLELIST: return primcnt * 3;
	case XE_PRIMTYPE_TRIANGLESTRIP:  /* fall trough */
	case XE_PRIMTYPE_TRIANGLEFAN: return 2 + primcnt;
	case XE_PRIMTYPE_RECTLIST: return primcnt * 3; 
	case XE_PRIMTYPE_QUADLIST: return primcnt * 4;
	default:
		Xe_Fatal(xe, "unknown primitive type");
	}
}

void Xe_DrawIndexedPrimitive(struct XenosDevice *xe, int type, int base_index, int min_index, int num_vertices, int start_index, int primitive_count)
{
	int cnt;

	assert(xe->ps); assert(xe->vs);

	Xe_pStuff(xe); /* fixme */
	Xe_pSetState(xe);
	rput32(0x00002007);
	rput32(0x00000000);

	Xe_pSetIndexOffset(xe, base_index);
	cnt = Xe_pCalcVtxCount(xe, type, primitive_count);
	int bpi = 2 << xe->current_ib->fmt;
	Xe_pDrawIndexedPrimitive(xe, type, cnt, xe->current_ib->phys_base + bpi * start_index, cnt, xe->current_ib->fmt);
}

void Xe_DrawPrimitive(struct XenosDevice *xe, int type, int start, int primitive_count)
{
	int cnt;
	
	assert(xe->ps); assert(xe->vs);

	Xe_pStuff(xe); /* fixme */
	Xe_pSetState(xe);
	rput32(0x00002007);
	rput32(0x00000000);

	Xe_pSetIndexOffset(xe, start); /* ?? */
	cnt = Xe_pCalcVtxCount(xe, type, primitive_count);
	Xe_pDrawNonIndexed(xe, cnt, type);
}

void Xe_SetStreamSource(struct XenosDevice *xe, int index, struct XenosVertexBuffer *vb, int offset, int stride)
{
	if (vb->lock.start)
		Xe_Fatal(xe, "cannot use locked VB");

	xe->current_vb = vb;
	VERTEX_FETCH(xe->fetch_constants + (95 + index) * 2, vb->phys_base + offset, vb->space - offset);
	Xe_DirtyFetch(xe, 95 + index, 1);
}

void Xe_SetIndices(struct XenosDevice *xe, struct XenosIndexBuffer *ib)
{
	xe->current_ib = ib;
}

struct XenosIndexBuffer *Xe_CreateIndexBuffer(struct XenosDevice *xe, int length, int format)
{
	struct XenosIndexBuffer *ib = malloc(sizeof(struct XenosIndexBuffer));
	memset(ib, 0, sizeof(struct XenosIndexBuffer));
	ib->base = Xe_pAlloc(xe, &ib->phys_base, length, 32);
	ib->size = length;
	ib->indices = 0;
	ib->fmt = format;
	return ib;
}

void Xe_DestroyIndexBuffer(struct XenosDevice *xe, struct XenosIndexBuffer *ib)
{
    Xe_pFree(xe,ib->base);
    free(ib);
}

void *Xe_VB_Lock(struct XenosDevice *xe, struct XenosVertexBuffer *vb, int offset, int size, int flags)
{
	Xe_pLock(xe, &vb->lock, vb->base + offset, vb->phys_base + offset, size, flags);
	return vb->base + offset;
}

void Xe_VB_Unlock(struct XenosDevice *xe, struct XenosVertexBuffer *vb)
{
	Xe_pUnlock(xe, &vb->lock);
}

void *Xe_IB_Lock(struct XenosDevice *xe, struct XenosIndexBuffer *ib, int offset, int size, int flags)
{
	Xe_pLock(xe, &ib->lock, ib->base + offset, ib->phys_base + offset, size, flags);
	return ib->base + offset;
}

void Xe_IB_Unlock(struct XenosDevice *xe, struct XenosIndexBuffer *ib)
{
	Xe_pUnlock(xe, &ib->lock);
}

void Xe_SetVertexShaderConstantF(struct XenosDevice *xe, int start, const float *data, int count)
{
//	printf("SetVertexShaderConstantF\n");
	memcpy(xe->alu_constants + start * 4, data, count * 16);
	Xe_DirtyAluConstant(xe, start, count);
//	while (count--)
//	{
//		printf("%.3f %.3f %.3f %.3f\n", data[0], data[1], data[2], data[3]);
//		data += 4;
//	}
}

void Xe_SetPixelShaderConstantF(struct XenosDevice *xe, int start, const float *data, int count)
{
	start += 256;
//	printf("SetPixelShaderConstantF (%d+)\n", start);
	memcpy(xe->alu_constants + start * 4, data, count * 16);
	Xe_DirtyAluConstant(xe, start, count);
//	while (count--)
//	{
//		printf("%.3f %.3f %.3f %.3f\n", data[0], data[1], data[2], data[3]);
//		data += 4;
//	}
}

void Xe_SetVertexShaderConstantB(struct XenosDevice *xe, int index, int value)
{
    int bit=1<<(index&0x1f);
    int block=index>>5;

    if (value)
        xe->integer_constants[block] |= bit;
    else
        xe->integer_constants[block] &= ~bit;

    xe->dirty |= DIRTY_INTEGER;
}

void Xe_SetPixelShaderConstantB(struct XenosDevice *xe, int index, int value)
{
    index += 128;
    
    u32 bit=1<<(index&0x1f);
    u32 block=index>>5;

    if (value)
        xe->integer_constants[block] |= bit;
    else
        xe->integer_constants[block] &= ~bit;

    xe->dirty |= DIRTY_INTEGER;
}

struct XenosSurface *Xe_CreateTexture(struct XenosDevice *xe, unsigned int width, unsigned int height, unsigned int levels, int format, int tiled)
{
	struct XenosSurface *surface = malloc(sizeof(struct XenosSurface));
	memset(surface, 0, sizeof(struct XenosSurface));
	int bypp = 0;
	
	switch (format & XE_FMT_MASK)
	{
	case XE_FMT_8: bypp = 1; break;
	case XE_FMT_5551: bypp = 2; break;
	case XE_FMT_565: bypp = 2; break;
	case XE_FMT_8888: bypp = 4; break;
	case XE_FMT_16161616: bypp = 8; break;
	}
	assert(bypp);
	
	int wpitch = (width * bypp + 127) &~127;
	int hpitch = (height + 31) &~31;
	
	surface->width = width;
	surface->height = height;
	surface->wpitch = wpitch;
	surface->hpitch = hpitch;
	surface->tiled = tiled;
	surface->format = format;
	surface->ptr_mip = 0;
	surface->bypp = bypp;
    surface->base = Xe_pAlloc(xe, &surface->ptr, hpitch * wpitch, 4096);

    surface->use_filtering = 1;
    surface->u_addressing = XE_TEXADDR_WRAP;
    surface->v_addressing = XE_TEXADDR_WRAP;

	return surface;
}

void Xe_DestroyTexture(struct XenosDevice *xe, struct XenosSurface *surface)
{
    Xe_pFree(xe,surface->base);
    free(surface);
}

void *Xe_Surface_LockRect(struct XenosDevice *xe, struct XenosSurface *surface, int x, int y, int w, int h, int flags)
{
#if 0
	if (surface == xe->rt) /* current render target? sync. */
	{
		Xe_Resolve(xe);
		Xe_Sync(xe);
	}
#endif
	if (!w)
		w = surface->width;
	if (!h)
		h = surface->height;

	int offset = y * surface->wpitch + x * surface->bypp;
	int size = h * surface->wpitch;
    
    if (surface->height != surface->hpitch) {
        //TODO: find a better way to handle this
        offset = 0;
        size = surface->hpitch * surface->wpitch;
    }

	Xe_pLock(xe, &surface->lock, surface->base + offset, surface->ptr + offset, size, flags);
	return surface->base + offset;
}

void Xe_Surface_Unlock(struct XenosDevice *xe, struct XenosSurface *surface)
{
	Xe_pUnlock(xe, &surface->lock);
}


int Xe_IsVBlank(struct XenosDevice *xe)
{
//	printf("%08x\n", r32(0x6534));
	return r32(0x6534) & 0x1000;
}
