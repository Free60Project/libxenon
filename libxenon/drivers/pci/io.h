#ifndef __PCI_IO_H
#define __PCI_IO_H

#include <stdint.h>

static inline __attribute__((always_inline)) uint32_t read32(long addr)
{
	return __builtin_bswap32(*(volatile uint32_t*)addr);
}

static inline __attribute__((always_inline)) uint32_t read32n(long addr)
{
	return *(volatile uint32_t*)addr;
}

static inline __attribute__((always_inline)) void write32(long addr, uint32_t val)
{
	*(volatile uint32_t*)addr = __builtin_bswap32(val);
}

static inline __attribute__((always_inline)) void write32n(long addr, uint32_t val)
{
	*(volatile uint32_t*)addr = val;
}

#endif
