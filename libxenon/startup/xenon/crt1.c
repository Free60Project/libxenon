#include "xetypes.h"
#include <stdlib.h>

extern int main(int argc, char **argv);

void call_ctors(void)
{
	typedef void (*pfunc) ();
	extern pfunc __CTOR_LIST__[];
	extern pfunc __CTOR_END__[];
	pfunc *p;
	
	for (p = __CTOR_END__ - 2; p > __CTOR_LIST__; )
	{
		(*p)();
		p--;
	}
}

void c_register_frame(void){
	extern char __eh_frame_start [];
	extern void __register_frame( void * );
	__register_frame(&__eh_frame_start);
}

extern void __check_argv();

void __crtmain() {
	__check_argv();
	exit(main(__system_argv->argc, __system_argv->argv));
}
