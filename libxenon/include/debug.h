#ifndef __include_debug_h
#define __include_debug_h

#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <xenon_uart/xenon_uart.h>

#define d_xstr(s) d_str(s)
#define d_str(s) #s

#define BP {printf("[Breakpoint] in function %s, line %d, file %s\n",__FUNCTION__,__LINE__,__FILE__);getch();}
#define TR {printf("[Trace] in function %s, line %d\n",__FUNCTION__,__LINE__);}

#define TRI(x) {printf("[Trace int] %s = %d(%p) at %s(%d)\n",d_xstr(x),x,x,__FUNCTION__,__LINE__);}
#define TRF(x) {printf("[Trace flt] %s = %f at %s(%d)\n",d_xstr(x),x,__FUNCTION__,__LINE__);}
#define TRS(x) {printf("[Trace str] %s = %s at %s(%d)\n",d_xstr(x),x,__FUNCTION__,__LINE__);}
       
void buffer_dump(void * buf, int size);
void stack_trace(int max_depth);
void data_breakpoint(void * address, int on_read, int on_write); // checks 8 bytes at address, throws Exception vector 0x300 on breakpoint

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif
