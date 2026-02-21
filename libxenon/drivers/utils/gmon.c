/*-
 * Copyright (c) 1983, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if !defined(lint) && defined(LIBC_SCCS)
static char sccsid[] = "@(#)gmon.c	8.1 (Berkeley) 6/4/93";
#endif

#include <sys/param.h>
#include <sys/time.h>
#include <arch/lib.h>
#include "gmon.h"
//#include <sys/sysctl.h>

#include <ppc/timebase.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <debug.h>
#include <stdlib.h>

char *minbrk;

struct gmonparam _gmonparam = { GMON_PROF_ON };

static int	s_scale;
/* see profil(2) where this is describe (incorrectly) */
#define		SCALE_1_TO_1	0x10000L

#define ERR(s) write(2, s, sizeof(s))

//#define HISTFRACTION 2
//#define HISTCOUNTER unsigned short
//#define HASHFRACTION 1

// #define GUPROF 1

static int	cputime_bias = 0;
static u_long prev_count= 0;

/*
 * Return the time elapsed since the last call.  The units are machine-
 * dependent.
 * XXX: this is not SMP-safe. It should use per-CPU variables; %tick can be
 * used though.
 */
int
cputime(void)
{
	u_long count;
	int delta;
	
	count = mftb();
	delta = (int)(count - prev_count);
	prev_count = count;
	return (delta);
}

void	moncontrol (int);

void
monstartup(lowpc, highpc)
	u_long lowpc;
	u_long highpc;
{
	register int o;
	char *cp;
	struct gmonparam *p = &_gmonparam;

	prev_count = mftb();
	
	/*
	 * round lowpc and highpc to multiples of the density we're using
	 * so the rest of the scaling (here and in gprof) stays in ints.
	 */
	p->lowpc = ROUNDDOWN(lowpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->highpc = ROUNDUP(highpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->textsize = p->highpc - p->lowpc;
	p->kcountsize = p->textsize / HISTFRACTION;
	p->hashfraction = HASHFRACTION;
	p->fromssize = p->textsize / HASHFRACTION;
	p->tolimit = p->textsize * ARCDENSITY / 100;
	if (p->tolimit < MINARCS)
		p->tolimit = MINARCS;
	else if (p->tolimit > MAXARCS)
		p->tolimit = MAXARCS;
	p->tossize = p->tolimit * sizeof(struct tostruct);

	cp = sbrk(p->kcountsize + p->fromssize + p->tossize);
	if (cp == (char *)-1) {
		ERR("monstartup: out of memory\n");
		return;
	}
//#ifdef notdef
	bzero(cp, p->kcountsize + p->fromssize + p->tossize);
//#endif
	p->tos = (struct tostruct *)cp;
	cp += p->tossize;
	p->kcount = (u_short *)cp;
	cp += p->kcountsize;
	p->froms = (u_short *)cp;

	minbrk = sbrk(0);
	p->tos[0].link = 0;

	o = p->highpc - p->lowpc;
	if (p->kcountsize < o) {
#ifndef hp300
		s_scale = ((float)p->kcountsize / o ) * SCALE_1_TO_1;
#else /* avoid floating point */
		int quot = o / p->kcountsize;

		if (quot >= 0x10000)
			s_scale = 1;
		else if (quot >= 0x100)
			s_scale = 0x10000 / quot;
		else if (o >= 0x800000)
			s_scale = 0x1000000 / (o / (p->kcountsize >> 8));
		else
			s_scale = 0x1000000 / ((o << 8) / p->kcountsize);
#endif
	} else
		s_scale = SCALE_1_TO_1;

	moncontrol(1);
}

void
_mcleanup()
{
	int fd;
	int fromindex;
	int endfrom;
	u_long frompc;
	int toindex;
	struct rawarc rawarc;
	struct gmonparam *p = &_gmonparam;
	struct gmonhdr gmonhdr, *hdr;
	struct clockinfo clockinfo;
#ifdef DEBUG
	int log, len;
	char buf[200];
#endif

	if (p->state == GMON_PROF_ERROR)
		ERR("_mcleanup: tos overflow\n");

	size_t size = sizeof(clockinfo);
	
#if 0	
	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	if (sysctl(mib, 2, &clockinfo, &size, NULL, 0) < 0) {
		/*
		 * Best guess
		 */
		clockinfo.profhz = hertz();
	} else if (clockinfo.profhz == 0) {
		if (clockinfo.hz != 0)
			clockinfo.profhz = clockinfo.hz;
		else
			clockinfo.profhz = hertz();
	}
#else
	//clockinfo.profhz = (PPC_TIMEBASE_FREQ);
	//clockinfo.profhz = (1000);
	clockinfo.profhz = 100;
#endif

	moncontrol(0);
	fd = open("uda:/gmon.out", O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (fd < 0) {
		perror("mcount: uda:/gmon.out");
		return;
	}
#ifdef DEBUG
	log = open("uda:/gmon.log", O_CREAT|O_TRUNC|O_WRONLY, 0664);
	if (log < 0) {
		perror("mcount: uda:/gmon.log");
		return;
	}
	len = sprintf(buf, "[mcleanup1] kcount 0x%x ssiz %d\n",
	    p->kcount, p->kcountsize);
	write(log, buf, len);
#endif
	hdr = (struct gmonhdr *)&gmonhdr;
	hdr->lpc = p->lowpc;
	hdr->hpc = p->highpc;
	hdr->ncnt = p->kcountsize + sizeof(gmonhdr);
	hdr->version = GMONVERSION;
	hdr->profrate = clockinfo.profhz;
	write(fd, (char *)hdr, sizeof *hdr);
	write(fd, p->kcount, p->kcountsize);
	endfrom = p->fromssize / sizeof(*p->froms);
	for (fromindex = 0; fromindex < endfrom; fromindex++) {
		if (p->froms[fromindex] == 0)
			continue;

		frompc = p->lowpc;
		frompc += fromindex * p->hashfraction * sizeof(*p->froms);
		for (toindex = p->froms[fromindex]; toindex != 0;
		     toindex = p->tos[toindex].link) {
#ifdef DEBUG
			len = sprintf(buf,
			"[mcleanup2] frompc 0x%x selfpc 0x%x count %d\n" ,
				frompc, p->tos[toindex].selfpc,
				p->tos[toindex].count);
			write(log, buf, len);
#endif
			rawarc.raw_frompc = frompc;
			rawarc.raw_selfpc = p->tos[toindex].selfpc;
			rawarc.raw_count = p->tos[toindex].count;
			write(fd, &rawarc, sizeof rawarc);
		}
	}
	close(fd);
}

/*
 * Control profiling
 *	profiling is what mcount checks to see if
 *	all the data structures are ready.
 */
void
moncontrol(mode)
	int mode;
{
	TR;
	struct gmonparam *p = &_gmonparam;

	if (mode) {
		/* start */
/*		profil((char *)p->kcount, p->kcountsize, (int)p->lowpc,
		    s_scale); */
		p->state = GMON_PROF_ON;
	} else {
		/* stop */
/*		profil((char *)0, 0, 0, 0); */
		p->state = GMON_PROF_OFF;
	}
}


__asm__ (
	".global _mcount		\n"
	"_mcount:				\n"
	"stwu	%sp, -0x70(%sp)	\n"
	"mflr	%r0				\n"
	"stw	%r0, 0x60(%sp)	\n"
	"std	%r3, 0x10(%sp)	\n"
	"std	%r4, 0x18(%sp)	\n"
	"std	%r5, 0x20(%sp)	\n"
	"std	%r6, 0x28(%sp)	\n"
	"std	%r7, 0x30(%sp)	\n"
	"std	%r8, 0x38(%sp)	\n"
	"std	%r9, 0x40(%sp)	\n"
	"std	%r10, 0x48(%sp)	\n"
	"mflr	%r3				\n"
	"lwz	%r4,0x74(%sp)	\n"
	"bl		internal_mcount	\n"
	"ld		%r3, 0x10(%sp)	\n"
	"ld		%r4, 0x18(%sp)	\n"
	"ld		%r5, 0x20(%sp)	\n"
	"ld		%r6, 0x28(%sp)	\n"
	"ld		%r7, 0x30(%sp)	\n"
	"ld		%r8, 0x38(%sp)	\n"
	"ld		%r9, 0x40(%sp)	\n"
	"ld		%r10, 0x48(%sp)	\n"
	"lwz	%r11, 0x60(%sp)	\n"
	"addi	%sp, %sp, 0x70	\n"
	"mtlr	%r0				\n"
	"lwz	%r0,4(%sp)		\n"
	"mtctr	%r11			\n"
	"mtlr	%r0				\n"
	"bctr					\n"
);

/*
 * mcount is called on entry to each function compiled with the profiling
 * switch set.  _mcount(), which is declared in a machine-dependent way
 * with _MCOUNT_DECL, does the actual work and is either inlined into a
 * C routine or called by an assembly stub.  In any case, this magic is
 * taken care of by the MCOUNT definition in <machine/profile.h>.
 *
 * _mcount updates data structures that represent traversals of the
 * program's call graph edges.  frompc and selfpc are the return
 * address and function address that represents the given call graph edge.
 *
 * Note: the original BSD code used the same variable (frompcindex) for
 * both frompcindex and frompc.  Any reasonable, modern compiler will
 * perform this optimization.
 */
 #if 0
_MCOUNT_DECL(frompc, selfpc)	/* _mcount; may be static, inline, etc */
	register fptrint_t frompc, selfpc;
#else
void internal_mcount(selfpc, frompc)
	register fptrint_t selfpc, frompc;
#endif
{
//#ifdef GUPROF
	u_int delta;
//#endif
	register fptrdiff_t frompci;
	register u_short *frompcindex;
	register struct tostruct *top, *prevtop;
	register struct gmonparam *p;
	register long toindex;
#ifdef KERNEL
	MCOUNT_DECL(s)
#endif

//	printf("%p %p\r\n",selfpc,frompc);
//	printf("%x %x\r\n",selfpc,frompc);
//	printf("\r\n");

	static char already_setup;
	if(!already_setup) {
		extern char bss_start[];
		already_setup = 1;
		monstartup(0x80000000, bss_start);
#ifdef USE_ONEXIT
		on_exit(_mcleanup, 0);
#else
		atexit(_mcleanup);
#endif
	}

	p = &_gmonparam;
#ifndef GUPROF			/* XXX */
	/*
	 * check that we are profiling
	 * and that we aren't recursively invoked.
	 */
	if (p->state != GMON_PROF_ON)
		return;
#endif
#ifdef KERNEL
	MCOUNT_ENTER(s);
#else
	p->state = GMON_PROF_BUSY;
#endif
	frompci = frompc - p->lowpc;

#ifdef KERNEL
	/*
	 * When we are called from an exception handler, frompci may be
	 * for a user address.  Convert such frompci's to the index of
	 * user() to merge all user counts.
	 */
	if (frompci >= p->textsize) {
		if (frompci + p->lowpc
		    >= (fptrint_t)(VM_MAXUSER_ADDRESS + UPAGES * PAGE_SIZE))
			goto done;
		frompci = (fptrint_t)user - p->lowpc;
		if (frompci >= p->textsize)
		    goto done;
	}
#endif /* KERNEL */

//#ifdef GUPROF
#if 1
//	if (p->state != GMON_PROF_HIRES){
	/*
	* Count the time since cputime() was previously called
	* against `frompc'.  Compensate for overheads.
	*
	* cputime() sets its prev_count variable to the count when
	* it is called.  This in effect starts a counter for
	* the next period of execution (normally from now until 
	* the next call to mcount() or mexitcount()).  We set
	* cputime_bias to compensate for our own overhead.
	*
	* We use the usual sampling counters since they can be
	* located efficiently.  4-byte counters are usually
	* necessary.  gprof will add up the scattered counts
	* just like it does for statistical profiling.  All
	* counts are signed so that underflow in the subtractions
	* doesn't matter much (negative counts are normally
	* compensated for by larger counts elsewhere).  Underflow
	* shouldn't occur, but may be caused by slightly wrong
	* calibrations or from not clearing cputime_bias.
	*/
	delta = cputime() - cputime_bias - p->mcount_pre_overhead;
	cputime_bias = p->mcount_post_overhead;
	//TR;
	//printf("(index) / (HISTFRACTION * sizeof(HISTCOUNTER))\r\n");
	//printf("%x / (%x * %x)\r\n",frompci,HISTFRACTION,sizeof(HISTCOUNTER));
	
	KCOUNT(p, frompci) += delta;
	
	//TR;
	//*p->cputime_count += p->cputime_overhead;
	//*p->mcount_count += p->mcount_overhead;
	//TR;
	//}
#endif
//#endif /* GUPROF */

#ifdef KERNEL
	/*
	 * When we are called from an exception handler, frompc is faked
	 * to be for where the exception occurred.  We've just solidified
	 * the count for there.  Now convert frompci to the index of btrap()
	 * for trap handlers and bintr() for interrupt handlers to make
	 * exceptions appear in the call graph as calls from btrap() and
	 * bintr() instead of calls from all over.
	 */
	if ((fptrint_t)selfpc >= (fptrint_t)btrap
	    && (fptrint_t)selfpc < (fptrint_t)eintr) {
		if ((fptrint_t)selfpc >= (fptrint_t)bintr)
			frompci = (fptrint_t)bintr - p->lowpc;
		else
			frompci = (fptrint_t)btrap - p->lowpc;
	}
#endif /* KERNEL */

	/*
	 * check that frompc is a reasonable pc value.
	 * for example:	signal catchers get called from the stack,
	 *		not from text space.  too bad.
	 */
	if (frompci >= p->textsize)
		goto done;

//	TR;
//	printf("%p %p\n",toindex,frompci);	
	
//	printf("p->hashfraction = %x\r\n",p->hashfraction);
//	printf("sizeof(*p->froms) = %x\r\n",sizeof(*p->froms));
		
	frompcindex = &p->froms[frompci / (p->hashfraction * sizeof(*p->froms))];
	toindex = *frompcindex;
	
//	TR;
//	printf("%p %p\n",toindex,frompcindex);
	
	if (toindex == 0) {
		/*
		 *	first time traversing this arc
		 */
		toindex = ++p->tos[0].link;
		if (toindex >= p->tolimit)
			/* halt further profiling */
			goto overflow;

		*frompcindex = toindex;
		top = &p->tos[toindex];
		top->selfpc = selfpc;
		top->count = 1;
		top->link = 0;
		goto done;
	}
	top = &p->tos[toindex];
	if (top->selfpc == selfpc) {
		/*
		 * arc at front of chain; usual case.
		 */
		top->count++;
		goto done;
	}
	/*
	 * have to go looking down chain for it.
	 * top points to what we are looking at,
	 * prevtop points to previous top.
	 * we know it is not at the head of the chain.
	 */
	for (; /* goto done */; ) {
		if (top->link == 0) {
//			printf("toindex : %08x %p\n",toindex,toindex);
			/*
			 * top is end of the chain and none of the chain
			 * had top->selfpc == selfpc.
			 * so we allocate a new tostruct
			 * and link it to the head of the chain.
			 */
			toindex = ++p->tos[0].link;
			if (toindex >= p->tolimit)
				goto overflow;

			top = &p->tos[toindex];
			top->selfpc = selfpc;
			top->count = 1;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}
		/*
		 * otherwise, check the next arc on the chain.
		 */
		prevtop = top;
		top = &p->tos[top->link];
		if (top->selfpc == selfpc) {
//			printf("selfpc : %08x %p\n",top->selfpc,top->selfpc);
//			printf("toindex : %08x %p\n",toindex,toindex);
			/*
			 * there it is.
			 * increment its count
			 * move it to the head of the chain.
			 */
			top->count++;
			toindex = prevtop->link;
			prevtop->link = top->link;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}

	}
done:
#ifdef KERNEL
	MCOUNT_EXIT(s);
#else
	p->state = GMON_PROF_ON;
#endif
	return;
overflow:
	p->state = GMON_PROF_ERROR;
#ifdef KERNEL
	MCOUNT_EXIT(s);
#endif
	return;
}