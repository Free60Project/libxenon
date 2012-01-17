#ifndef __drivers_ppc_atomic_h
#define __drivers_ppc_atomic_h

#ifdef __cplusplus
extern "C" {
#endif

void atomic_inc(unsigned int * v);
void atomic_dec(unsigned int * v);
void atomic_clearset(unsigned int * v, unsigned int andc, unsigned int or_);
// Compares the value in dest to cmp, if equal places exc in dest
// Returns value in dest before the swap
unsigned int atomic_compareswap(unsigned int * dest, unsigned int exc, unsigned int cmp);
void lock(unsigned int * lock);
void unlock(unsigned int * lock);

#ifdef __cplusplus
};
#endif

#endif
