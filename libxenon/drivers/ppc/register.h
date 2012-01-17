#ifndef __ppc_register_h
#define __ppc_register_h

#include "xenonsprs.h"

#define __stringify(rn) #rn

#define mfmsr()   ({unsigned long long rval; \
      asm volatile("mfmsr %0" : "=r" (rval)); rval;})
#define mtmsr(v)  asm volatile("mtmsr %0" : : "r" (v))

#define mfdec()   ({unsigned int rval; \
      asm volatile("mfdec %0" : "=r" (rval)); rval;})
#define mtdec(v)  asm volatile("mtdec %0" : : "r" (v))

#define mfhdec()   ({unsigned int rval; \
      asm volatile("mfhdec %0" : "=r" (rval)); rval;})
#define mthdec(v)  asm volatile("mthdec %0" : : "r" (v))

#define mfspr(rn) ({unsigned long long rval; \
      asm volatile("mfspr %0," __stringify(rn) \
             : "=r" (rval)); rval;})

#define mfsprg0() ({unsigned long long rval; \
      asm volatile("mfsprg0 %0" : "=r" (rval)); rval;})
#define mfsprg1() ({unsigned long long rval; \
      asm volatile("mfsprg1 %0" : "=r" (rval)); rval;})
#define mtsprg0(rn) ({unsigned long long rval; \
      asm volatile("mtsprg0 %0" : "=r" (rval)); rval;})
#define mtsprg1(rn) ({unsigned long long rval; \
      asm volatile("mtsprg1 %0" : "=r" (rval)); rval;})

#define mfspr64(rn) ({unsigned long long rval; \
      asm volatile("mfspr %0," __stringify(rn) \
             : "=r" (rval)); rval;})

#define mtspr(rn, v)  asm volatile("mtspr " __stringify(rn) ",%0" : : "r" (v))

#define mtsrr0(v) asm volatile("mtsrr0 %0" : : "r" (v))
#define mfsrr0() ({unsigned long long rval; \
        asm volatile("mfsrr0 %0" : : "r" (rval)); rval;})

#define mtsrr1(v) asm volatile("mtsrr1 %0" : : "r" (v))
#define mfsrr1() ({unsigned long long rval; \
        asm volatile("mfsrr1 %0" : : "r" (rval)); rval;})

#endif
