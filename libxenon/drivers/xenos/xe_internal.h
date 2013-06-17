#ifndef __xe_internal_h
#define __xe_internal_h

static inline int FLOAT(float f)
{
	union {
		float f;
		u32 d;
	} u = {f};
	
	return u.d;
}

#define rput32(d) *(volatile u32*)(xe->rb_secondary + xe->rb_secondary_wptr++ * 4) = (d);
#define rput(base, len) memcpy(((void*)xe->rb_secondary) + xe->rb_secondary_wptr * 4, (base), (len) * 4); xe->rb_secondary_wptr += (len);
#define rput32p(d) do { *(volatile u32*)(xe->rb_primary + xe->rb_primary_wptr++ * 4) = d; if (xe->rb_primary_wptr == RINGBUFFER_PRIMARY_SIZE) xe->rb_primary_wptr = 0; } while (0)

#define rputf(d) rput32(FLOAT(d));

#define r32(o) xe->regs[(o)/4]
#define w32(o, v) xe->regs[(o)/4] = (v)

void Xe_pSyncToDevice(struct XenosDevice *xe, volatile void *data, int len);
void Xe_pSyncFromDevice(struct XenosDevice *xe, volatile void *data, int len);
void *Xe_pAlloc(struct XenosDevice *xe, u32 *phys, int size, int align);
void Xe_pDebugSync(struct XenosDevice *xe);
void Xe_pSetState(struct XenosDevice *xe);

#endif