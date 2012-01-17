/* 
 * File:   gdb.h
 * Author: NATHAN
 *
 * Created on December 20, 2011, 12:30 AM
 */

#ifndef GDB_H
#define	GDB_H

#ifdef	__cplusplus
extern "C" {
#endif

// Call this at any time, will only kick in after you call threading_init
// Do note, the stub expects EXCLUSIVE access of the uart
void gdb_init();

#ifdef	__cplusplus
}
#endif

#endif	/* GDB_H */

