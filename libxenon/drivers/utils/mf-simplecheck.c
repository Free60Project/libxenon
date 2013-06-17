#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "mf-runtime.h"


struct __mf_options
{
  /* Emit a trace message for each call. */
  unsigned trace_mf_calls;

  /* Collect and emit statistics. */
  unsigned collect_stats;

  /* Set up a SIGUSR1 -> __mf_report handler. */
  unsigned sigusr1_report;

  /* Execute internal checking code. */
  unsigned internal_checking;

  /* Age object liveness periodically. */
  unsigned tree_aging;

  /* Adapt the lookup cache to working set. */
  unsigned adapt_cache;

  /* Print list of leaked heap objects on shutdown. */
  unsigned print_leaks;

#ifdef HAVE___LIBC_FREERES
  /* Call __libc_freeres before leak analysis. */
  unsigned call_libc_freeres;
#endif

  /* Detect reads of uninitialized objects. */
  unsigned check_initialization;

  /* Print verbose description of violations. */
  unsigned verbose_violations;

  /* Abbreviate duplicate object descriptions.  */
  unsigned abbreviate;

  /* Emit internal tracing message. */
  unsigned verbose_trace;

  /* Wipe stack/heap objects upon unwind.  */
  unsigned wipe_stack;
  unsigned wipe_heap;

  /* Maintain a queue of this many deferred free()s,
     to trap use of freed memory. */
  unsigned free_queue_length;

  /* Maintain a history of this many past unregistered objects. */
  unsigned persistent_count;

  /* Pad allocated extents by this many bytes on either side. */
  unsigned crumple_zone;

  /* Maintain this many stack frames for contexts. */
  unsigned backtrace;

  /* Ignore read operations even if mode_check is in effect.  */
  unsigned ignore_reads;

  /* Collect register/unregister timestamps.  */
  unsigned timestamps;

#ifdef LIBMUDFLAPTH
  /* Thread stack size.  */
  unsigned thread_stack;
#endif

  /* Major operation mode */
#define mode_nop 0      /* Do nothing.  */
#define mode_populate 1 /* Populate tree but do not check for violations.  */
#define mode_check 2    /* Populate and check for violations (normal).  */
#define mode_violate 3  /* Trigger a violation on every call (diagnostic).  */
  unsigned mudflap_mode;

  /* How to handle a violation. */
#define viol_nop 0   /* Return control to application. */
#define viol_segv 1  /* Signal self with segv. */
#define viol_abort 2 /* Call abort (). */
#define viol_gdb 3   /* Fork a debugger on self */
  unsigned violation_mode;

  /* Violation heuristics selection. */
  unsigned heur_stack_bound; /* allow current stack region */
  unsigned heur_proc_map;  /* allow & cache /proc/self/map regions.  */
  unsigned heur_start_end; /* allow _start .. _end */
  unsigned heur_std_data; /* allow & cache stdlib data */
};

enum __mf_state_enum { active, reentrant, in_malloc }; 

/* ------------------------------------------------------------------------ */
/* Required globals.  */

#define LOOKUP_CACHE_MASK_DFL 1023
#define LOOKUP_CACHE_SIZE_MAX 65536 /* Allows max CACHE_MASK 0xFFFF */
#define LOOKUP_CACHE_SHIFT_DFL 2

struct __mf_cache __mf_lookup_cache [LOOKUP_CACHE_SIZE_MAX];
uintptr_t __mf_lc_mask = LOOKUP_CACHE_MASK_DFL;
unsigned char __mf_lc_shift = LOOKUP_CACHE_SHIFT_DFL;
#define LOOKUP_CACHE_SIZE (__mf_lc_mask + 1)

struct __mf_options __mf_opts;
int __mf_starting_p = 1;

enum __mf_state_enum __mf_state_1 = reentrant;

/* ------------------------------------------------------------------------ */

#define MAX_CHECKS 256

struct check_s{
    unsigned char * addr;
    int size;
    int read;
    int write;
    int halt;    
};

static int check_count=0;
static struct check_s checks[MAX_CHECKS];

void mf_check(void * addr,int size,int check_reads, int check_writes, int halt_on_access)
{
    if(check_count>=MAX_CHECKS-1)
    {
        printf("[mf-simplecheck] Too many checks!\n");
        return;
    }
    
    checks[check_count].addr=addr;
    checks[check_count].size=size;
    checks[check_count].halt=halt_on_access;
    checks[check_count].read=check_reads;
    checks[check_count].write=check_writes;
    
    check_count++;
    
    printf("[mf-simplecheck] Will check %p, size=%d for %s %s\n",addr,size,check_reads?"read":"",check_writes?"write":"");
}

void __mf_init ()
{
    printf("[mf-simplecheck] Inited\n");
}
                
void __mf_check (void *ptr, size_t sz, int type, const char *location)
{
    int i;
    unsigned int p=(unsigned int)ptr;
    for(i=0;i<check_count;++i)
    {
        unsigned int cp=(unsigned int)checks[i].addr;
        if (p>=cp && p<cp+checks[i].size &&
            ((type==__MF_CHECK_READ && checks[i].read) || (type==__MF_CHECK_WRITE && checks[i].write)))
        {
            printf("[mf-simplecheck] %s to %p, size=%d, from %s\n",(type==__MF_CHECK_READ)?"Read":"Write",ptr,sz,location);
            if (checks[i].halt)
            {
                asm volatile("sc");
            }
        }
    }    
    
}

void __mf_register (void *ptr, size_t sz, int type, const char *name)
{
//    printf("[mf-simplecheck] Register %s(%p), size=%d\n",name,ptr,sz);
}

void __mf_unregister (void *ptr, size_t sz, int type)
{
//    printf("[mf-simplecheck] Unregister %p, size=%d\n",ptr,sz);
}
