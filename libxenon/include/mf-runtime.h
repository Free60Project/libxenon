/* Implementation header for mudflap runtime library.
   Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004, 2005, 2009 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Public libmudflap declarations -*- C -*- */

#ifndef MF_RUNTIME_H
#define MF_RUNTIME_H

typedef void *__mf_ptr_t;
typedef unsigned int __mf_uintptr_t __attribute__ ((__mode__ (__pointer__)));
typedef __SIZE_TYPE__ __mf_size_t;

/* Global declarations used by instrumentation.  When _MUDFLAP is
   defined, these have been auto-declared by the compiler and we
   should not declare them again (ideally we *would* declare them
   again, to verify that the compiler's declarations match the
   library's, but the C++ front end has no mechanism for allowing
   the re-definition of a structure type).  */

#ifndef _MUDFLAP
struct __mf_cache { __mf_uintptr_t low; __mf_uintptr_t high; };
extern struct __mf_cache __mf_lookup_cache [];
extern __mf_uintptr_t __mf_lc_mask;
extern unsigned char __mf_lc_shift;
#endif

/* Codes to describe the type of access to check: __mf_check arg 3 */

#define __MF_CHECK_READ 0
#define __MF_CHECK_WRITE 1


/* Codes to describe a region of memory being registered: __mf_*register arg 3 */

#define __MF_TYPE_NOACCESS 0
#define __MF_TYPE_HEAP 1
#define __MF_TYPE_HEAP_I 2
#define __MF_TYPE_STACK 3
#define __MF_TYPE_STATIC 4
#define __MF_TYPE_GUESS 5


/* The public mudflap API */

#ifdef __cplusplus
extern "C" {
#endif

extern void __mf_check (void *ptr, __mf_size_t sz, int type, const char *location)
       __attribute((nothrow));
extern void __mf_register (void *ptr, __mf_size_t sz, int type, const char *name)
       __attribute((nothrow));
extern void __mf_unregister (void *ptr, __mf_size_t sz, int type)
       __attribute((nothrow));
extern unsigned __mf_watch (void *ptr, __mf_size_t sz);
extern unsigned __mf_unwatch (void *ptr, __mf_size_t sz);
extern void __mf_report ();
extern int __mf_set_options (const char *opts);


void mf_check(void * addr,int size,int check_reads, int check_writes, int halt_on_access);


#ifdef __cplusplus
}
#endif

#endif /* MF_RUNTIME_H */
