#ifndef __xenon_soc_xenon_io_h
#define __xenon_soc_xenon_io_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t ld(volatile void *addr)
{
	uint64_t l;
	asm volatile ("ld %0, 0(%1)" : "=r" (l) : "b" (addr));
	return l;
}


static inline void  std(volatile void *addr, uint64_t v)
{
	asm volatile ("std %1, 0(%0)" : : "b" (addr), "r" (v));
	asm volatile ("eieio");
	asm volatile ("isync");
}

static inline uint32_t lwz(volatile void *addr)
{
    uint32_t i;
    asm volatile ("lwz %0, 0(%1)" : "=r" (i) : "b" (addr));
    return i;
}

static inline void stw(volatile void *addr, uint32_t v)
{
        asm volatile ("stw %1, 0(%0)" : : "b" (addr), "r" (v));
        asm volatile ("eieio");
        asm volatile ("isync");
}

#ifdef __cplusplus
};
#endif

#endif
