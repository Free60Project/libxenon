#include <xenos/edram.h>

#include "xe.h"
#include "xe_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <time/time.h>
#include <string.h>
#include <assert.h>
#include <xetypes.h>

extern void xenos_write32(int reg, uint32_t val);
extern uint32_t xenos_read32(int reg);

extern u32 xenos_id;

u32 edram_id=0,edram_rev=0;

static uint32_t edram_read(int addr)
{
	uint32_t res;
	xenos_write32(0x3c44, addr);
	while (xenos_read32(0x3c4c));
	res = xenos_read32(0x3c48);
	while (xenos_read32(0x3c4c));
	xenos_write32(0x3c44, addr);
	while (xenos_read32(0x3c4c));
	res = xenos_read32(0x3c48);
	while (xenos_read32(0x3c4c));
	return res;
}

static void edram_write(int addr, unsigned int value)
{
	while (xenos_read32(0x3c4c));
	xenos_write32(0x3c40, addr);
	while (xenos_read32(0x3c4c));
	xenos_write32(0x3c48, value);
	while (xenos_read32(0x3c4c));

	xenos_write32(0x3c40, 0x1A);
	while (xenos_read32(0x3c4c));
	xenos_write32(0x3c40, value);
	while (xenos_read32(0x3c4c));
}

static void edram_rmw(int addr, int bit, int mask, int val)
{
	edram_write(addr, (edram_read(addr) & ~mask) | ((val << bit) & mask));
}

void edram_p3(int *res)
{
	int chip, phase;
	
	for (chip = 0; chip < 9; ++chip)
		res[chip] = 0;

	for (phase = 0; phase < 4; ++phase)
	{
		edram_write(0x5002, 0x15555 * phase);
		int v = edram_read(0x5005);
		for (chip = 0; chip < 9; ++chip)
		{
			int bit = (v >> chip) & 1;
			
			res[chip] |= bit << phase;
		}
	}
}

void edram_p4(int *res)
{
	int chip, phase;

	for (chip = 0; chip < 6; ++chip)
		res[chip] = 0;

	for (phase = 0; phase < 4; ++phase)
	{
		xenos_write32(0x3c5c, 0x555 * phase);
		uint32_t r = xenos_read32(0x3c68);

		for (chip = 0; chip < 6; ++chip)
		{
			int bit = (r >> chip) & 1;

			res[chip] |= bit << phase;
		}
	}
}

int edram_p2(int r3, int r4, int r5, int r6, int r7, int r8, int r9, int r10)
{
	int a = edram_read(0x5002);
	edram_write(0x5002, 0xa53ca53c);
	int b = edram_read(0x5002);
	if ((b & 0x1ffff) != 0xA53C)
		return -1;
	edram_write(0x5002, 0xfee1caa9);
	b = edram_read(0x5002);
	if ((b & 0x1ffff) != 0x1caa9)
		return -2;
	edram_write(0x5002, a);
	
	xenos_write32(0x3c54, 1);
	if (r10)
	{
		xenos_write32(0x3c54, xenos_read32(0x3c54) &~ (1<<9));
		xenos_write32(0x3c54, xenos_read32(0x3c54) &~ (1<<10));
		
//		assert(xenos_read32(0x3c54) == 0x1);
	} else
	{
		xenos_write32(0x3c54, xenos_read32(0x3c54) | (1<<9));
		xenos_write32(0x3c54, xenos_read32(0x3c54) | (1<<10));
	}
	while (xenos_read32(0x3c4c));
	
	edram_write(0x4000, 0xC0);
	if (xenos_id<0x5841) xenos_write32(0x3c90, 0);
	xenos_write32(0x3c00, xenos_read32(0x3c00) | 0x800000);
//	assert(xenos_read32(0x3c00) == 0x8900000);
	xenos_write32(0x0214, xenos_read32(0x0214) | 0x80000000);
//	assert(xenos_read32(0x0214) == 0x8000001a);
	xenos_write32(0x3c00, xenos_read32(0x3c00) | 0x400000);
	xenos_write32(0x3c00, xenos_read32(0x3c00) &~0x400000);
	xenos_write32(0x3c00, xenos_read32(0x3c00) &~0x800000);
	xenos_write32(0x0214, xenos_read32(0x0214) &~0x80000000);
//	assert(xenos_read32(0x214) == 0x1a);
	edram_write(0xffff, 1);
	while (xenos_read32(0x3c4c));
	edram_write(0x5001, 7);
	int s = 7;
	
	xenos_write32(0x3c58, xenos_read32(0x3c58) | 4);
	xenos_write32(0x3c58, xenos_read32(0x3c58) &~3);
	
//	assert(xenos_read32(0x3c58) == 0x00000ff4);
	
	if (r8)
	{
		assert(0);
		while (xenos_read32(0x3c4c));
		s = edram_read(0x5003);
		s |= 0x15555;
		edram_write(0x5003, s);
		edram_write(0x5003, s &~ 0x15555);
	}
	if (r9)
	{
		assert(0);
		while (xenos_read32(0x3c4c));
		int v = xenos_read32(0x3c60) | 0x555;
		xenos_write32(0x3c60, v);
		xenos_write32(0x3c60, v &~0x555);
	}
	if (r8)
	{
		assert(0);
		xenos_write32(0x3c54, xenos_read32(0x3c54) &~2);
		xenos_write32(0x3c90, 0x2aaaa);
		xenos_write32(0x3c6c, 0x30);
		xenos_write32(0x3c70, 0x30);
		xenos_write32(0x3c74, 0x30);
		xenos_write32(0x3c78, 0x30);
		xenos_write32(0x3c7c, 0x30);
		xenos_write32(0x3c80, 0x30);
		xenos_write32(0x3c84, 0x30);
		xenos_write32(0x3c88, 0x30);
		xenos_write32(0x3c8c, 0x30);
		xenos_write32(0x3c6c, xenos_read32(0x3c6c) &~ 0xFF);
		xenos_write32(0x3c70, xenos_read32(0x3c70) &~ 0xFF);
		xenos_write32(0x3c74, xenos_read32(0x3c74) &~ 0xFF);
		xenos_write32(0x3c78, xenos_read32(0x3c78) &~ 0xFF);
		xenos_write32(0x3c7c, xenos_read32(0x3c7c) &~ 0xFF);
		xenos_write32(0x3c80, xenos_read32(0x3c80) &~ 0xFF);
		xenos_write32(0x3c84, xenos_read32(0x3c84) &~ 0xFF);
		xenos_write32(0x3c88, xenos_read32(0x3c88) &~ 0xFF);
		xenos_write32(0x3c8c, xenos_read32(0x3c8c) &~ 0xFF);
	} else
	{
		xenos_write32(0x3c54, xenos_read32(0x3c54) | 2);
//		assert(xenos_read32(0x3c54) == 3);
	}
	if (r9)
	{
		assert(0);
		edram_rmw(0x5000, 1, 2, 0);
		edram_write(0x500c, 0x3faaa);
		edram_write(0x5006, 0x30);
		edram_write(0x5007, 0x30);
		edram_write(0x5008, 0x30);
		edram_write(0x5009, 0x30);
		edram_write(0x500a, 0x30);
		edram_write(0x500b, 0x30);
		edram_rmw(0x5006, 7, 0x3f80, 0);
		edram_rmw(0x5007, 7, 0x3f80, 0);
		edram_rmw(0x5008, 7, 0x3f80, 0);
		edram_rmw(0x5009, 7, 0x3f80, 0);
		edram_rmw(0x500a, 7, 0x3f80, 0);
		edram_rmw(0x500b, 7, 0x3f80, 0);

		edram_rmw(0x5006, 14, 0x1fc000, 0);
		edram_rmw(0x5007, 14, 0x1fc000, 0);
		edram_rmw(0x5008, 14, 0x1fc000, 0);
		edram_rmw(0x5009, 14, 0x1fc000, 0);
		edram_rmw(0x500a, 14, 0x1fc000, 0);
		edram_rmw(0x500b, 14, 0x1fc000, 0);
		
		edram_write(0x500c, 0x3f000);
	} else
	{
		edram_rmw(0x5000, 1, 2, 1);
	}
	if (r8)
	{
		/* later */
	}
		// bb584
	xenos_write32(0x3c54, xenos_read32(0x3c54) | 0x20);
	xenos_write32(0x3c54, xenos_read32(0x3c54) &~0x20);
	int i;
	
	int res_cur[9], temp[9], res_base[9];
	int valid[36]                 = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	int useful[64]                = {0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0};
	int phase_count[9]            = {4,4,4,4,4,4,4,4,4};
	int stable[9]                 = {1,1,1,1,1,1,1,1,1};
	int defaults[4]               = {11,7,15,13}; // 1011 0111 1111 1101
	int data6_old[16]             = {1,3,0,3,1,1,0,0,2,2,1,3,1,2,1,1};
	int data6_new[16]             = {0,2,3,2,0,0,3,3,1,1,0,2,0,1,0,0};
	int data7[16]                 = {0,2,3,2,0,0,3,3,1,1,0,2,0,1,0,0};

	int chip, phase;
	int res = 0;

#if 1
	for (i = 0; i < 50; ++i) /* outer */
	{
		edram_p3(temp);
#if 0
		temp[0] = 0x3;
		temp[1] = 0xd;
		temp[2] = 0x6;
		temp[3] = 0x1;
		temp[4] = 0x3;
		temp[5] = 0x1;
		temp[6] = 0xd;
		temp[7] = 0x2;
		temp[8] = 0x2;
#endif
		
		memcpy(res_cur, temp, sizeof(res_cur));
		if (!i)
			memcpy(res_base, temp, sizeof(res_cur));

		for (chip = 0; chip < 9; ++chip) /* inner */
		{
			for (phase = 0; phase < 4; phase++) /* phase */
			{
				if (valid[chip + phase * 9])
					if (!useful[res_cur[chip] + phase * 4])
					{
						valid[chip + phase * 9] = 0;
						phase_count[chip]--;
					}
			}
			if (res_base[chip] != res_cur[chip])
				stable[chip] = 0;
		}
	}
#if 1
	for (chip = 0; chip < 9; ++chip)
	{
		if (!stable[chip] && phase_count[chip] == 1)
		{
			for (phase = 0; phase < 4; ++phase)
			{
				if (valid[chip + phase * 9])
				{
					res_cur[chip] = defaults[phase];
				}
			}
		}
	}
#endif
		// bb734

	int *data6;
    data6 = (xenos_id==0x5821 && edram_rev>=0x10) || (xenos_id!=0x5821 && edram_rev>=0x20) ? data6_new : data6_old;
	
	for (chip = 0; chip < 9; ++chip)
	{
//		printf("%d  | ", data6[res_cur[chip]]);
		res |= data6[res_cur[chip]] << (chip * 2);
	}
	printf("final cfg: %08x\n", res);
//	assert(res == 0x2fcb);
#else
	int cur[9], rp[9];
	memset(rp, 0xff, sizeof rp);
	for (i = 0; i < 10; ++i)
	{
		edram_p3(cur);
		int j;
		for (j = 0; j < 9; ++j)
			rp[j] &= cur[j];
	}
	
	res = 0;
	
	for (i = 0; i < 9; ++i)
	{
		for (j = 0; j < 4; ++j)
			if (rp[i] & (1<<j))
				break;
		if (j == 4)
			printf("ed chip %d, inv\n", i);
		j--; j&=3;
		res |= j << (i * 2);
	}
#endif

	edram_write(0x5002, res);

	xenos_write32(0x3c54, xenos_read32(0x3c54) | 0x20);
	xenos_write32(0x3c54, xenos_read32(0x3c54) &~0x20);
	
	edram_rmw(0x5000, 2, 4, 1);
	edram_rmw(0x5000, 2, 4, 0);
	edram_rmw(0x5000, 3, 8, 1);
	edram_rmw(0x5000, 3, 8, 0);

	xenos_write32(0x3c58, xenos_read32(0x3c58) | 4);
	xenos_write32(0x3c58, xenos_read32(0x3c58) | 3);

	s = 4;
	edram_write(0x5001, s);
	edram_rmw(0x5000, 0, 1, 1);
	if (r8)
		xenos_write32(0x3c54, xenos_read32(0x3c54) &~2);
	else
		xenos_write32(0x3c54, xenos_read32(0x3c54) | 2);
	edram_rmw(0x5000, 1, 2, r9 ? 0 : 1);
	if (r9)
	{
		/* not yet */
	}
		/* bbf38 */
	edram_rmw(0x5000, 5, 0x20, 1);
	edram_rmw(0x5000, 5, 0x20, 0);
	for (i = 0; i < 9; ++i)
		phase_count[i] = 4;
	for (i = 0; i < 9; ++i)
		stable[i] = 1;
	for (i = 0; i < 9; ++i)
		valid[i] = valid[i + 9] = valid[i + 18] = valid[i + 27] = 1;

#if 1	
	// bbfd4
	int ht[6];
	for (i = 0; i < 50; ++i)
	{
		edram_p4(ht);
#if 0
		ht[0] = 3;
		ht[1] = 3;
		ht[2] = 6;  // 01 10 01 10 10 10
		ht[3] = 3;  // 01 11 10 10 11 10
		ht[4] = 0xc;
		ht[5] = 6;
#endif
		memcpy(res_cur, ht, sizeof(ht));
		if (!i)
			memcpy(res_base, ht, sizeof(ht));
		for (chip = 0; chip < 6; ++chip)
		{
			for (phase = 0; phase < 4; ++phase)
			{
				if (valid[chip + phase * 9] && !useful[res_cur[chip] + phase * 4])
				{
					valid[chip + phase * 9] = 0;
					phase_count[chip]--;
				}
			}
			if (res_cur[chip] != res_base[chip])
				stable[chip] = 0;
		}
	}
#if 1
	for (chip = 0; chip < 6; ++chip)
	{
		if (!stable[chip] && phase_count[chip] == 1)
		{
			for (phase = 0; phase < 4; ++phase)
				if (valid[chip + phase * 9])
					res_cur[chip] = defaults[phase];
		}
	}
#endif

	res = 0;
	for (chip = 0; chip < 6; ++chip)
		res |= data7[res_cur[chip]] << (chip*2);
	printf("final cfg: %08x\n", res);
//	assert(res == 0x7ae);
#else
	memset(rp, 0xff, sizeof rp);
	for (i = 0; i < 10; ++i)
	{
		edram_p4(cur);
		int j;
		for (j = 0; j < 6; ++j)
			rp[j] &= cur[j];
	}
	
	res = 0;
	
	for (i = 0; i < 6; ++i)
	{
		for (j = 0; j < 4; ++j)
			if (rp[i] & (1<<j))
				break;
		if (j == 4)
			printf("gp chip %d, inv\n", i);
		j--; j&=3;
		res |= j << (i * 2);
	}
#endif

    xenos_write32(0x3c5c, res);

	edram_rmw(0x5000, 5, 0x20, 1);
	edram_rmw(0x5000, 5, 0x20, 0);
	xenos_write32(0x3c54, xenos_read32(0x3c54) | 4);
	xenos_write32(0x3c54, xenos_read32(0x3c54) &~4);
	if (r6 >= 0x10)
	{
		xenos_write32(0x3cb4, 0xbbbbbb);
	} else
	{
		assert(0);
		xenos_write32(0x3c54, xenos_read32(0x3c54) | 8);
		xenos_write32(0x3c54, xenos_read32(0x3c54) &~8);
	}
	
	xenos_write32(0x3c58, xenos_read32(0x3c58) | 3);
	xenos_write32(0x3c58, xenos_read32(0x3c58) | 4);
	edram_rmw(0x5001, 0, 3, 3);
	edram_rmw(0x5001, 2, 4, 1);

	edram_rmw(0x5001, 0, 3, 3);
	edram_rmw(0x5001, 2, 4, 1);
	xenos_write32(0x3c58, xenos_read32(0x3c58) &~3);
	xenos_write32(0x3c58, xenos_read32(0x3c58) | 4);

	if (r6 >= 0x10 && !r7)
	{
		xenos_write32(0x3c94, (xenos_read32(0x3c94) &~0xFF) | 0x13);
		xenos_write32(0x3c94, xenos_read32(0x3c94) &~0x80000000);
		xenos_write32(0x3c94, xenos_read32(0x3c94) | 0x80000000);
//		assert(xenos_read32(0x3c94) == 0x80000013);
	} else
	{
		edram_rmw(0x5000, 0, 1, 1);
		edram_rmw(0x5000, 8, 0x100, 1);

		xenos_write32(0x3c54, xenos_read32(0x3c54) | 1);
		xenos_write32(0x3c54, xenos_read32(0x3c54) | 0x100);
		
		int cnt = 20;
		while (cnt--)
		{
			udelay(5000);
			if (xenos_read32(0x3c94) & 0x80000000)
			{
				printf("ping test: %08x\n", xenos_read32(0x3c94));
				break;
			}
		}
		
		if (!cnt)
		{
			edram_rmw(0x5000, 0, 1, 0);
			edram_rmw(0x5000, 8, 0x100, 0);
		
			xenos_write32(0x3c54, xenos_read32(0x3c54) &~1);
			xenos_write32(0x3c54, xenos_read32(0x3c54) &~0x100);

			printf("ping test timed out\n");
			return 1;
		}
		printf("ping test okay\n");
	}

	xenos_write32(0x3c54, xenos_read32(0x3c54) &~0x100);
	xenos_write32(0x3c54, xenos_read32(0x3c54) &~1);
//	assert(xenos_read32(0x3c54) == 2);
	
	edram_rmw(0x5000, 8, 0x100, 0);
	edram_rmw(0x5000, 0, 1, 0);
	edram_write(0xf, 1);
	edram_write(0x100f, 1);
	edram_write(0xf, 0);
	edram_write(0x100f, 0);
	edram_write(0xffff, 1);
	edram_rmw(0x5001, 2, 4, 0);

	xenos_write32(0x3c58, xenos_read32(0x3c58) &~4);
	xenos_write32(0x3c58, (xenos_read32(0x3c58) &~ 0xF0) | 0xA0);
	xenos_write32(0x3c58, xenos_read32(0x3c58) &~ 0xF00);
	
	assert(xenos_read32(0x3c58) == 0xa0);

	xenos_write32(0x3c00, (xenos_read32(0x3c00) &~ 0xFF) | (9<<3));
	xenos_write32(0x3c00, xenos_read32(0x3c00) | 0x100);
	//assert((xenos_read32(0x3c00) &~0x600) == 0x08100148);
	
	while ((xenos_read32(0x3c00) & 0x600) < 0x400);

	return 0;
}

void edram_pc(void)
{
	int i;
	
	for (i = 0; i < 6; ++i)
	{
		xenos_write32(0x3c44, 0x41);
		while (xenos_read32(0x3c4c) & 0x80000000);
	}

	for (i = 0; i < 6; ++i)
	{
		xenos_write32(0x3c44, 0x1041);
		while (xenos_read32(0x3c4c) & 0x80000000);
	}

	for (i = 0; i < 6; ++i)
		xenos_read32(0x3ca4);

	for (i = 0; i < 6; ++i)
		xenos_read32(0x3ca8);
}

int edram_compare_crc(uint32_t *crc)
{
	int i;
	
	int fail = 0;
	
	for (i = 0; i < 6; ++i)
	{
		xenos_write32(0x3c44, 0x41);
		while (xenos_read32(0x3c4c) & 0x80000000);
		if (xenos_read32(0x3c48) != *crc++)
			fail |= 1<<6;
//		printf("%08x ", xenos_read32(0x3c48));
	}

	for (i = 0; i < 6; ++i)
	{
		xenos_write32(0x3c44, 0x1041);
		while (xenos_read32(0x3c4c) & 0x80000000);
		if (xenos_read32(0x3c48) != *crc++)
			fail |= 1<<(i+6);
//		printf(" %08x", xenos_read32(0x3c48));
	}
//	printf("\n");

	for (i = 0; i < 6; ++i)
	{
		uint32_t v = xenos_read32(0x3ca4);
		if (v != *crc++)
			fail |= 1<<(i+12);
	}

	for (i = 0; i < 6; ++i)
	{
		uint32_t v = xenos_read32(0x3ca8);
		if (v != *crc++)
			fail |= 1<<(i+18);
	}
	return fail;
}

void edram_init_state1(void)
{
    if (xenos_id<0x5841) xenos_write32(0x214, xenos_id<0x5831 ? 0x1e : 0x15);
	xenos_write32(0x3C00, (xenos_read32(0x3c00) &~ 0x003f0000) | 0x100000);
	int v = (edram_read(0x4000) &~ 4) | 0x2A;
	edram_write(0x4000, v);
	edram_write(0x4001, xenos_id<0x5821 ? 0x2709f1 : 0x31);
	v = (v &~ 0x20) | 0xC;
	edram_write(0x4000, v);
	v &= ~0xC;
	edram_write(0x4000, v);
	edram_write(0xFFFF, 1);
	v |= 4;
	edram_write(0x4000, v);
	edram_write(0x4001, xenos_id<0x5821 ? 0x2709f1 : 0x31);
	v &= ~0x4C;
	edram_write(0x4000, v);
	edram_write(0xFFFF, 1);
	udelay(1000);
	
    edram_id = edram_read(0x2000);
    edram_rev= edram_id&0xffff;
    printf("Xenos EDRAM ID=%08x\n", edram_id);
	
	if (edram_p2(0, 0, 0, 0x11, 0, 0, 0, 1))
	{
		printf("edram_p2 failed\n");
		abort();
	}
}

int reloc[10];

void edram_72c(struct XenosDevice *xe)
{
 // 0000072c: (+0)
	rput32(0x00002000);
		rput32(0x00800050); 
 // 00000734: (+2)
	rput32(0x00002001);
		rput32(0x00000000); 
 // 0000073c: (+4)
	rput32(0x00002301);
		rput32(0x00000000); 
 // 00000744: (+6)
	rput32(0x0005210f);
		rput32(0x41000000); rput32(0x41000000); rput32(0xc0800000); rput32(0x40800000); rput32(0x3f800000); rput32(0x00000000); 
 // 00000760: (+d)
	rput32(0x00022080);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00080010); 
 // 00000770: (+11)
	rput32(0xc0192b00);
		rput32(0x00000000); rput32(0x00000018); rput32(0x30052003); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00001005); rput32(0x00001200); rput32(0xc2000000); 
		rput32(0x00001006); rput32(0x10071200); rput32(0x22000000); rput32(0x1df81000); 
		rput32(0x00253b08); rput32(0x00000004); rput32(0x00080000); rput32(0x40253908); 
		rput32(0x00000200); rput32(0xc8038000); rput32(0x00b0b000); rput32(0xc2000000); 
		rput32(0xc80f803e); rput32(0x00000000); rput32(0xc2010100); rput32(0xc8000000); 
		rput32(0x00000000); rput32(0x02000000); 
 // 000007dc: (+2c)
	rput32(0xc00d2b00);
		rput32(0x00000001); rput32(0x0000000c); rput32(0x00011002); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00001003); rput32(0x00002200); rput32(0x00000000); 
		rput32(0x50080001); rput32(0x1f1ff688); rput32(0x00004000); rput32(0xc80f8000); 
		rput32(0x00000000); rput32(0xc2000000); 
 // 00000818: (+3b)
	rput32(0x00012180);
		rput32(0x10030001); rput32(0x00000000); 
 // 00000824: (+3e)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(reloc[2]); rput32(0x10000042); 
 // 00000840: (+45)
	rput32(0x00054800);
		rput32(0x80400002); rput32(reloc[8]); rput32(0x0000e00f); rput32(0x01000c14); rput32(0x00000000); rput32(0x00000200); 
 // 0000085c: (+4c)
	rput32(0xc0003601);
		rput32(0x00040086); 


					 /* resolve */
 // 00000864: (+4e)
	rput32(0x00002318);
		rput32(0x00100000); 
 // 0000086c: (+50)
	rput32(0x00002319);
		rput32(reloc[9]); 
 // 00000874: (+52)
	rput32(0x0000231a);
		rput32(0x00080020);
 // 0000087c: (+54)
	rput32(0x0000231b);
		rput32(0x01000302); 
 // 00000884: (+56)
	rput32(0xc00d2b00);
		rput32(0x00000000); rput32(0x0000000c); rput32(0x10011002); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00000000); rput32(0x1003c200); rput32(0x22000000); 
		rput32(0x05f80000); rput32(0x00253b48); rput32(0x00000002); rput32(0xc80f803e); 
		rput32(0x00000000); rput32(0xc2000000); 
 // 000008c0: (+65)
	rput32(0x00012180);
		rput32(0x00010002); rput32(0x00000000); 
 // 000008cc: (+68)
	rput32(0x00002208);
		rput32(0x00000006); 
 // 000008d4: (+6a)
	rput32(0x00002200);
		rput32(0x00000000); 
 // 000008dc: (+6c)
	rput32(0x00002203);
		rput32(0x00000000); 
 // 000008e4: (+6e)
	rput32(0x00022100);
		rput32(0x0000ffff); rput32(0x00000000); rput32(0x00000000); 
 // 000008f4: (+72)
	rput32(0x00022204);
		rput32(0x00010000); rput32(0x00010000); rput32(0x00000300); 
 // 00000904: (+76)
	rput32(0x00002312);
		rput32(0x0000ffff); 
 // 0000090c: (+78)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(reloc[3]); rput32(0x1000001a); 
 // 00000928: (+7f)
	rput32(0xc0003601);
		rput32(0x00030088); 

				/* sync memory */
 // 00000930: (+81)
	rput32(0x00002007);
		rput32(0x00000000); 
 // 00000938: (+83)
	rput32(0x00000a31);
		rput32(0x03000100); 
 // 00000940: (+85)
	rput32(0x00010a2f);
		rput32(0x00002000); rput32(reloc[9]); 
 // 0000094c: (+88)
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008); 
 // 00000964: (+8e)
	rput32(0x00002208);
		rput32(0x00000004); 
 // 0000096c: (+90)
	rput32(0x00002206);
		rput32(0x00000000); 
}

void edram_974(struct XenosDevice *xe)
{
		///////////////// part 2
 // 00000974: (+0)
	rput32(0x00000a31);
		rput32(0x02000000); 
 // 0000097c: (+2)
	rput32(0x00010a2f);
		rput32(0x00001000); rput32(reloc[0]); 
 // 00000988: (+5)
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008); 
 // 000009a0: (+b)
	rput32(0x00002000);
		rput32(0x00800050); 
 // 000009a8: (+d)
	rput32(0x00002001);
		rput32(0x00000000); 
 // 000009b0: (+f)
	rput32(0x00002301);
		rput32(0x00000000); 
 // 000009b8: (+11)
	rput32(0x0005210f);
		rput32(0x41800000); rput32(0x41800000); rput32(0xc1800000); rput32(0x41800000); rput32(0x3f800000); rput32(0x00000000); 
 // 000009d4: (+18)
	rput32(0x00022080);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00200020); 
}

void edram_9e4(struct XenosDevice *xe)
{
							//////////
 // 000009e4: (+0)
	rput32(0xc0192b00);
		rput32(0x00000000); rput32(0x00000018); rput32(0x30052003); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00001005); rput32(0x00001200); rput32(0xc2000000); 
		rput32(0x00001006); rput32(0x10071200); rput32(0x22000000); rput32(0x1df81000); 
		rput32(0x00253b08); rput32(0x00000004); rput32(0x00080000); rput32(0x40253908); 
		rput32(0x00000200); rput32(0xc8038000); rput32(0x00b0b000); rput32(0xc2000000); 
		rput32(0xc80f803e); rput32(0x00000000); rput32(0xc2010100); rput32(0xc8000000); 
		rput32(0x00000000); rput32(0x02000000); 
 // 00000a50: (+1b)
	rput32(0xc00d2b00);
		rput32(0x00000001); rput32(0x0000000c); rput32(0x00011002); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00001003); rput32(0x00002200); rput32(0x00000000); 
		rput32(0x50080001); rput32(0x1f1ff688); rput32(0x00004000); rput32(0xc80f8000); 
		rput32(0x00000000); rput32(0xc2000000); 
 // 00000a8c: (+2a)
	rput32(0x00012180);
		rput32(0x10030001); rput32(0x00000000); 
 // 00000a98: (+2d)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(reloc[4]); rput32(0x10000042); 
 // 00000ab4: (+34)
	rput32(0x00054800);
		rput32(0x80400002); rput32(reloc[8]); rput32(0x0003e01f); rput32(0x01000c14); 
		rput32(0x00000000); rput32(0x00000200); 
 // 00000ad0: (+3b)
	rput32(0xc0003601);
		rput32(0x00040086); 
 // 00000ad8: (+3d)

	rput32(0x00002318);
		rput32(0x00100000); 
 // 00000ae0: (+3f)
	rput32(0x00002319);
		rput32(reloc[9]); 
 // 00000ae8: (+41)
	rput32(0x0000231a);
		rput32(0x00200020); 
 // 00000af0: (+43)
	rput32(0x0000231b);
		rput32(0x01000302); 
 // 00000af8: (+45)
	rput32(0xc00d2b00);
		rput32(0x00000000); rput32(0x0000000c); rput32(0x10011002); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00000000); rput32(0x1003c200); rput32(0x22000000); 
		rput32(0x05f80000); rput32(0x00253b48); rput32(0x00000002); rput32(0xc80f803e); 
		rput32(0x00000000); rput32(0xc2000000); 
 // 00000b34: (+54)
	rput32(0x00012180);
		rput32(0x00010002); rput32(0x00000000); 
 // 00000b40: (+57)
	rput32(0x00002208);
		rput32(0x00000006); 
 // 00000b48: (+59)
	rput32(0x00002200);
		rput32(0x00000000); 
 // 00000b50: (+5b)
	rput32(0x00002203);
		rput32(0x00000000); 
 // 00000b58: (+5d)
	rput32(0x00022100);
		rput32(0x0000ffff); rput32(0x00000000); rput32(0x00000000); 
 // 00000b68: (+61)
	rput32(0x00022204);
		rput32(0x00010000); rput32(0x00010000); rput32(0x00000300); 
 // 00000b78: (+65)
	rput32(0x00002312);
		rput32(0x0000ffff); 
 // 00000b80: (+67)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(reloc[6]); rput32(0x1000001a); 
 // 00000b9c: (+6e)
	rput32(0xc0003601);
		rput32(0x00030088); 

 // 00000ba4: (+70)
	rput32(0x00002007);
		rput32(0x00000000); 
 // 00000bac: (+72)
	rput32(0x00000a31);
		rput32(0x03000100); 
 // 00000bb4: (+74)
	rput32(0x00010a2f);
		rput32(0x00002000); rput32(reloc[9]); 
 // 00000bc0: (+77)
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008); 
 // 00000bd8: (+7d)
	rput32(0x00002208);
		rput32(0x00000004); 
 // 00000be0: (+7f)
	rput32(0x00002206);
		rput32(0x00000000); 
}

void edram_bec(struct XenosDevice *xe)
{
					////////////////
 // 00000bec: (+0)
	rput32(0x00002000);
		rput32(0x19000640); 
 // 00000bf4: (+2)
	rput32(0x00002001);
		rput32(0x00000000); 
 // 00000bfc: (+4)
	rput32(0x00002301);
		rput32(0x00000000); 
 // 00000c04: (+6)
	rput32(0x0005210f);
		rput32(0x44480000); rput32(0x44480000); rput32(0xc44c0000); rput32(0x444c0000); rput32(0x3f800000); rput32(0x00000000); 
 // 00000c20: (+d)
	rput32(0x00022080);
		rput32(0x00000000); rput32(0x00000000); rput32(0x06600640); 
 // 00000c30: (+11)
	rput32(0xc0162b00);
		rput32(0x00000000); rput32(0x00000015); rput32(0x10011003); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00001004); rput32(0x00001200); rput32(0xc2000000); 
		rput32(0x00001005); rput32(0x10061200); rput32(0x22000000); rput32(0x0df81000); 
		rput32(0x00253b08); rput32(0x00000002); rput32(0xc8038000); rput32(0x00b06c00); 
		rput32(0x81010000); rput32(0xc80f803e); rput32(0x00000000); rput32(0xc2010100); 
		rput32(0xc8000000); rput32(0x00000000); rput32(0x02000000); 
 // 00000c90: (+29)
	rput32(0xc0522b00);
		rput32(0x00000001); rput32(0x00000051); rput32(0x00000000); rput32(0x6003c400); 
		rput32(0x12000000); rput32(0x00006009); rput32(0x600f1200); rput32(0x12000000); 
		rput32(0x00006015); rput32(0x00002200); rput32(0x00000000); rput32(0xc8030000); 
		rput32(0x00b06cc6); rput32(0x8b000002); rput32(0x4c2c0000); rput32(0x00ac00b1); 
		rput32(0x8a000001); rput32(0x08100100); rput32(0x00000031); rput32(0x02000000); 
		rput32(0x4c100000); rput32(0x0000006c); rput32(0x02000001); rput32(0xc8030001); 
		rput32(0x006c1ac6); rput32(0xcb010002); rput32(0xc8030001); rput32(0x00b00000); 
		rput32(0x8a010000); rput32(0xc8030001); rput32(0x00b0c600); rput32(0x81010000); 
		rput32(0xa8430101); rput32(0x00b00080); rput32(0x8a010000); rput32(0xc80f0001); 
		rput32(0x00c00100); rput32(0xc1010000); rput32(0xc80f0000); rput32(0x00000000); 
		rput32(0x88810000); rput32(0xc80f0000); rput32(0x01000000); rput32(0xed010000); 
		rput32(0xc80f0004); rput32(0x00aabc00); rput32(0x81000100); rput32(0xc8060000); 
		rput32(0x00166c00); rput32(0x86040100); rput32(0xc8090000); rput32(0x04c56c00); 
		rput32(0x80000100); rput32(0xc8030002); rput32(0x04b06c00); rput32(0x80040100); 
		rput32(0xc8070003); rput32(0x00bc6cb1); rput32(0x6c020000); rput32(0xc8020001); 
		rput32(0x00b06d6c); rput32(0xd1040002); rput32(0xc8010001); rput32(0x00b0b26c); 
		rput32(0xd1020302); rput32(0xc8080001); rput32(0x006d6e6c); rput32(0xd1040302); 
		rput32(0xc8040001); rput32(0x006d6d6c); rput32(0xd1020002); rput32(0xc8018000); 
		rput32(0x001a1a6c); rput32(0xd1010002); rput32(0xc8028000); rput32(0x00b01a6c); 
		rput32(0xd1010002); rput32(0xc8048000); rput32(0x00c71a6c); rput32(0xd1010002); 
		rput32(0xc8088000); rput32(0x006d1a6c); rput32(0xd1010002); 
 // 00000de0: (+7d)
	rput32(0x00012180);
		rput32(0x10010401); rput32(0x00000000); 
 // 00000dec: (+80)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(reloc[5]); rput32(0x10000022); 
 // 00000e08: (+87)
	rput32(0x000f4000);
		rput32(0x3b800000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 00000e4c: (+98)
	rput32(0x000f4400);
		rput32(0x47c35000); rput32(0x3727c5ac); rput32(0x3eaaaaab); rput32(0x43800000); 
		rput32(0x3f800000); rput32(0x40000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x3f800000); rput32(0x3f000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 00000e90: (+a9)
	rput32(0xc0003601);
		rput32(0x00040086); 
 // 00000e98: (+ab)
	rput32(0x00002201);
		rput32(0x0b0a0b0a); 
 // 00000ea0: (+ad)
	rput32(0xc0003601);
		rput32(0x00040086); 
 // 00000ea8: (+af)
	rput32(0x00002201);
		rput32(0x00010001); 
 // 00000eb0: (+b1)
	rput32(0x00002318);
		rput32(0x00300000); 
 // 00000eb8: (+b3)
	rput32(0x00002319);
		rput32(0x00000000); 
 // 00000ec0: (+b5)
	rput32(0x0000231a);
		rput32(0x06600640); 
 // 00000ec8: (+b7)
	rput32(0x0000231b);
		rput32(0x01000302); 
 // 00000ed0: (+b9)
	rput32(0xc00d2b00);
		rput32(0x00000000); rput32(0x0000000c); rput32(0x10011002); rput32(0x00001200); 
		rput32(0xc4000000); rput32(0x00000000); rput32(0x1003c200); rput32(0x22000000); 
		rput32(0x05f80000); rput32(0x00253b48); rput32(0x00000002); rput32(0xc80f803e); 
		rput32(0x00000000); rput32(0xc2000000); 
 // 00000f0c: (+c8)
	rput32(0x00012180);
		rput32(0x00010002); rput32(0x00000000); 
 // 00000f18: (+cb)
	rput32(0x00002208);
		rput32(0x00000006); 
 // 00000f20: (+cd)
	rput32(0x00002200);
		rput32(0x00000000); 
 // 00000f28: (+cf)
	rput32(0x00002203);
		rput32(0x00000000); 
 // 00000f30: (+d1)
	rput32(0x00022100);
		rput32(0x0000ffff); rput32(0x00000000); rput32(0x00000000); 
 // 00000f40: (+d5)
	rput32(0x00022204);
		rput32(0x00010000); rput32(0x00010000); rput32(0x00000300); 
 // 00000f50: (+d9)
	rput32(0x00002312);
		rput32(0x0000ffff); 
 // 00000f58: (+db)
	rput32(0x000548ba);
		rput32(0x00000003); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(reloc[7]); rput32(0x1000001a); 
 // 00000f74: (+e2)
	rput32(0xc0003601);
		rput32(0x00030088); 
 // 00000f7c: (+e4)
	rput32(0x00002007);
		rput32(0x00000000); 
 // 00000f84: (+e6)
	rput32(0x00002208);
		rput32(0x00000004); 
 // 00000f8c: (+e8)
	rput32(0x00002206);
		rput32(0x00000000); 

}

void edram_4c(struct XenosDevice *xe)
{
	 // 0000004c: (+0)
	rput32(0xc0015000);
		rput32(0xffffffff); rput32(0x00000000); 
 // 00000058: (+3)
	rput32(0xc0015100);
		rput32(0xffffffff); rput32(0xffffffff); 
 // 00000064: (+6)
	rput32(0x00022080);
		rput32(0x00000000); rput32(0x00000000); rput32(0x01e00280); 
 // 00000074: (+a)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 0000007c: (+c)
	rput32(0x00032388);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 00000090: (+11)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 00000098: (+13)
	rput32(0x0003238c);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 000000ac: (+18)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 000000b4: (+1a)
	rput32(0x00032390);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 000000c8: (+1f)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 000000d0: (+21)
	rput32(0x00032394);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 000000e4: (+26)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 000000ec: (+28)
	rput32(0x00032398);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 00000100: (+2d)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 00000108: (+2f)
	rput32(0x0003239c);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 0000011c: (+34)
	rput32(0x000005c8);
		rput32(0x00020000); 
 // 00000124: (+36)
	rput32(0x00000f01);
		rput32(0x0000200e); 
 // 0000012c: (+38)
	rput32(0x00252300);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000004); rput32(0x40000000); 
		rput32(0x3f800000); rput32(0x40000000); rput32(0x3f800000); rput32(0x000ff000); 
		rput32(0x000ff100); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x0000ffff); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x0000000e); rput32(0x00000010); 
		rput32(0x00100000); rput32(0x1f923000); rput32(0x01e00280); rput32(0x01000300); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000002); rput32(0x00000000); 
 // 000001c8: (+5f)
	rput32(0x00072380);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 000001ec: (+68)
	rput32(0x00bf4800);
		rput32(0x04000002); rput32(0x1f90b04f); rput32(0x000a61ff); rput32(0x01000c14); 
		rput32(0x00000000); rput32(0x00000a00); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x01000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x01000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
		rput32(0x00000001); rput32(0x00000000); rput32(0x00000001); rput32(0x00000000); 
 // 000004f0: (+129)
	rput32(0x00274900);
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
 // 00000594: (+152)
	rput32(0x000f2000);
		rput32(0x0a000280); rput32(0x00000000); rput32(0x000000f0); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x20002000); 
 // 000005d8: (+163)
	rput32(0x00142100);
		rput32(0x00ffffff); rput32(0x00000000); rput32(0x00000000); rput32(0x0000ffff); 
		rput32(0x0000000f); rput32(0x3f800000); rput32(0x3f800000); rput32(0x3f800000); 
		rput32(0x3f800000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00ffff00); rput32(0x00ffff00); rput32(0x00000000); rput32(0x43a00000); 
		rput32(0x43a00000); rput32(0xc3700000); rput32(0x43700000); rput32(0x3f800000); 
		rput32(0x00000000); 
 // 00000630: (+179)
	rput32(0x00042180);
		rput32(0x10130200); rput32(0x00000004); rput32(0x00010001); rput32(0x00000000); 
		rput32(0x00000000); 
 // 00000648: (+17f)
	rput32(0x000b2200);
		rput32(0x00700730); rput32(0x00010001); rput32(0x87000007); rput32(0x00000001); 
		rput32(0x00090000); rput32(0x00018006); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000004); rput32(0x00010001); rput32(0x00010001); rput32(0x00010001); 
 // 0000067c: (+18c)
	rput32(0x00142280);
		rput32(0x00080008); rput32(0x04000010); rput32(0x00000008); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000001); rput32(0x3f800000); rput32(0x3f800000); 
		rput32(0x0000000e); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); rput32(0x00000000); 
		rput32(0x00000000); 
 // 000006d4: (+1a2)
	rput32(0x00000a31);
		rput32(0x02000000); 
 // 000006dc: (+1a4)
	rput32(0x00010a2f);
		rput32(0x00001000); rput32(reloc[0]);
 // 000006e8: (+1a7)
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008); 
 // 00000700: (+1ad)
	rput32(0x00000a31);
		rput32(0x01000000); 
 // 00000708: (+1af)
	rput32(0x00010a2f);
		rput32(0x00000100); rput32(reloc[1]); 
 // 00000714: (+1b2)
	rput32(0xc0043c00);
		rput32(0x00000003); rput32(0x00000a31); rput32(0x00000000); rput32(0x80000000); rput32(0x00000008); 
}

static uint32_t determine_broken(uint32_t v0)
{
/*
00006000
00018000
00410000
00082000
00700000
00180000
*/
	int res = 0;
	if ((v0 & 0x00006000) == 0x00006000)
		res |= 1;
	if ((v0 & 0x00018000) == 0x00018000)
		res |= 2;
	if ((v0 & 0x00410000) == 0x00410000)
		res |= 4;
	if ((v0 & 0x00082000) == 0x00082000)
		res |= 8;
	if ((v0 & 0x00700000) == 0x00700000)
		res |= 16;
	if ((v0 & 0x00180000) == 0x00180000)
		res |= 32;
	return res;
}

void edram_init(struct XenosDevice *xe)
{
	int tries = 4;
retry:
	if (!tries--)
		Xe_Fatal(xe, "damnit, EDRAM init failed, again and again.");
	
    edram_init_state1();

	int i;
#if 0
	printf("waiting for temperature to stabilize...\n");

	for (i = 0; i < 40; ++i)
	{
		uint16_t data[4];
		xenon_smc_query_sensors(data);
		
		printf("%f %f %f %f\n", data[0] / 256.0, data[1] / 256.0, data[2] / 256.0, data[3] / 256.0);
		delay(1);
	}
#endif	

	static u32 base;
	static void *ptr;
	if (!ptr)
		ptr = Xe_pAlloc(xe, &base, 0x4000, 0x100000);
	
			/* state 2 */
	reloc[0] = base + 0x0000;
	reloc[1] = base + 0x2000;
	reloc[2] = base + 0x2003;
	reloc[3] = base + 0x2083;
	reloc[4] = base + 0x2043;
	reloc[5] = base + 0x20b3;
	reloc[6] = base + (0x2098|3);
	reloc[7] = base + 0x20d3;
	reloc[8] = (base + 0x0000) | (0x43 << 1);
	reloc[9] = base + 0x1000;
	
	memset(ptr + 0, 1, 0x1000); /* debug only */

	memset(ptr + 0x1000, 0x99, 0x1000);

	*(u32*)(ptr + 0x108) = 0x1000;
	*(u32*)(ptr + 0x118) = 0x1;
	*(u32*)(ptr + 0x128) = 0x1100;
	*(u32*)(ptr + 0x12c) = 0x1000;
	*(u32*)(ptr + 0x13c) = 0;
	
	float reg_00[] = {
		-0.5,  7.5, 0.0, 1.0, // top left
		-0.5, -0.5, 0.0, 0.0, // bottom left
		15.5,  7.5, 1.0, 1.0, // bottom right
		15.5, -0.5, 1.0, 0.0  // top right
	};
	float reg_40[] = {
		-0.5, 31.5, 0.0, 1.0, 
		-0.5, -0.5, 0.0, 0.0, 
		31.5, 31.5, 1.0, 1.0, 
		31.5, -0.5, 1.0, 0.0
	};
	float reg_b0[] = {
		-0.5,   1631.5, 
		-0.5,     -0.5, 
		1599.5, 1631.5, 
		1599.5,   -0.5
	};
	float reg_80[] = {-.5, 7.5,  -.5,  -.5, 15.5, -.5};
	float reg_98[] = {-.5, 31.5, -.5,  -.5, 31.5, -.5};
	float reg_d0[] = {-.5, -.5, -.5, 1599.5, -.5};
	
	memcpy(ptr + 0x2000 + 0x00, reg_00, sizeof(reg_00));
	memcpy(ptr + 0x2000 + 0x40, reg_40, sizeof(reg_40));
	memcpy(ptr + 0x2000 + 0xb0, reg_b0, sizeof(reg_b0));
	memcpy(ptr + 0x2000 + 0x80, reg_80, sizeof(reg_80));
	memcpy(ptr + 0x2000 + 0x98, reg_98, sizeof(reg_98));
	memcpy(ptr + 0x2000 + 0xd0, reg_d0, sizeof(reg_d0));
	
	Xe_pSyncToDevice(xe, ptr, 0x4000);

	w32(0x3c04, r32(0x3c04) &~ 0x100);

//	assert(r32(0x3c04) == 0x200e);
	udelay(1000);
	int var_1 = 0x111111;
	w32(0x3cb4, 0x888888 | var_1);
	udelay(1000);
	edram_pc();


	edram_4c(xe);
	
	memset(ptr + 0x1000, 'Z', 0x1000);
	Xe_pSyncToDevice(xe, ptr, 0x4000);

	w32(0x3c94, (r32(0x3c94) &~0x800000FF) | 0xb);
	udelay(1000);
	w32(0x3c94, r32(0x3c94) | 0x80000000);
	udelay(1000);

	memset(ptr + 0x1000, 0, 0x1000);
	Xe_pSyncToDevice(xe, ptr, 0x4000);

	int var_2;	
	
	int fail = 0;
	for (var_2 = 0xb; var_2 < 0x13; ++var_2)
	{
			/* state 4 */
		int seed = 0x425a;
		for (i = 0; i < 0x20 * 0x20; ++i)
		{
			seed *= 0x41c64e6d;
			seed += 12345;
			
			int p1 = (seed >> 16) & 0x7fff;

			seed *= 0x41c64e6d;
			seed += 12345;
			
			int p2 = (seed >> 16) & 0x7fff;
			
			int v = (p2<<16) + p1;
			
			((int*)ptr) [i] = v;
		}
		memset(ptr + 0x1000, 0x44, 0x1000);
		Xe_pSyncToDevice(xe, ptr, 0x4000);

		w32(0x3c94, (r32(0x3c94) &~0x800000FF) | var_2);
		udelay(1000);
		w32(0x3c94, r32(0x3c94) | 0x80000000);
		udelay(1000);

		edram_pc();
//		edram_72c(xe);
		edram_974(xe);
//			printf("before 9e4\n");
		edram_9e4(xe);
//			printf("after 9e4\n");
			
		Xe_pDebugSync(xe);
		Xe_pSyncFromDevice(xe, ptr, 0x4000);

		uint32_t good_crc[] = {
			0xEBBCB7D0, 0xB7599E02, 0x0AEA2A7A, 0x2CABD6B8, 
			0xA5A5A5A5, 0xA5A5A5A5, 0xE57C27BE, 0x43FA90AA, 
			0x9D065F66, 0x360A6AD8, 0xA5A5A5A5, 0xA5A5A5A5, 
			0xA5A5A5A5, 0xEBBCB7D0, 0xB7599E02, 0x0AEA2A7A, 
			0x2CABD6B8, 0xA5A5A5A5, 0xA5A5A5A5, 0xE57C27BE,
			0x43FA90AA, 0x9D065F66, 0x360A6AD8, 0xA5A5A5A5
		};

		fail = edram_compare_crc(good_crc);
		fail = determine_broken(fail);
		
		if (fail != 0x3f)
			goto fix_xxx;

		if (!fail) goto worked; /* OMGWTF IT WORKED!!1 */
	}
//	printf("great, our base var_2 is %d, let's fix remaining problems\n", var_2);

fix_xxx:;

	int ixxx;
	for (ixxx = 0; ixxx < 6; ++ixxx)
	{
		int vxxx;
		if (!(fail & (1<<ixxx)))
		{
//			printf("not touching, should be ok\n");
			continue;
		} 
		for (vxxx = 0; vxxx < 4; ++vxxx)
		{
			var_1 &= ~(0xF<<(ixxx*4));
			var_1 |= vxxx << (ixxx * 4);

			memset(ptr + 0, 1, 0x1000); /* debug only */
			*(u32*)(ptr + 0x108) = 0x1000;
			*(u32*)(ptr + 0x118) = 0x1;
			*(u32*)(ptr + 0x128) = 0x1100;
			*(u32*)(ptr + 0x12c) = 0x1000;
			*(u32*)(ptr + 0x13c) = 0;

			Xe_pSyncToDevice(xe, ptr, 0x4000);

			udelay(1000);
			w32(0x3cb4, var_1 | 0x888888);
			udelay(1000);

			edram_pc();
			edram_72c(xe);
			Xe_pDebugSync(xe);
			Xe_pSyncFromDevice(xe, ptr, 0x4000);
			

				/* state 4 */
			int seed = 0x425a;
			for (i = 0; i < 0x20 * 0x20; ++i)
			{
				seed *= 0x41c64e6d;
				seed += 12345;
				
				int p1 = (seed >> 16) & 0x7fff;

				seed *= 0x41c64e6d;
				seed += 12345;
				
				int p2 = (seed >> 16) & 0x7fff;
				
				int v = (p2<<16) + p1;
				
				((int*)ptr) [i] = v;
			}
			memset(ptr + 0x1000, 0x44, 0x1000);
			Xe_pSyncToDevice(xe, ptr, 0x4000);

			w32(0x3c94, (r32(0x3c94) &~0x800000FF) | var_2);
			udelay(1000);
			w32(0x3c94, r32(0x3c94) | 0x80000000);
			udelay(1000);

			edram_pc();
	//		edram_72c(xe);
			edram_974(xe);
	//			printf("before 9e4\n");
			edram_9e4(xe);
	//			printf("after 9e4\n");
				
			Xe_pDebugSync(xe);
			Xe_pSyncFromDevice(xe, ptr, 0x4000);

			uint32_t good_crc[] = {
				0xEBBCB7D0, 0xB7599E02, 0x0AEA2A7A, 0x2CABD6B8, 
				0xA5A5A5A5, 0xA5A5A5A5, 0xE57C27BE, 0x43FA90AA, 
				0x9D065F66, 0x360A6AD8, 0xA5A5A5A5, 0xA5A5A5A5, 
				0xA5A5A5A5, 0xEBBCB7D0, 0xB7599E02, 0x0AEA2A7A, 
				0x2CABD6B8, 0xA5A5A5A5, 0xA5A5A5A5, 0xE57C27BE,
				0x43FA90AA, 0x9D065F66, 0x360A6AD8, 0xA5A5A5A5
			};

			fail = determine_broken(edram_compare_crc(good_crc));
//			printf("[%08x]=%08x ", var_1, fail);
			if (!(fail & (1<< ixxx)))
			{
//				printf("cool");
				break;
			}	
		}
		if (vxxx == 4)
		{
//			printf("can't fix that :(\n");
			break;
		}

//		printf("\n");
	}
	if (fail)
		goto retry;
worked:
	printf("EDRAM %08x, %08x\n", var_1, var_2);
}