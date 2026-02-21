/* modified for libxenon */

/* KallistiOS 1.2.0

   fs_iso9660.c
   Copyright (C)2000,2001,2003 Dan Potter
   Copyright (C)2001 Andrew Kieschnick
   Copyright (C)2002 Bero

*/

/*

This module implements an ISO9660 file system for reading from a CDR or CD
in the DC's GD-Rom drive.

Rock Ridge support has now been implemented, thanks to Andrew Kieschnick
who donated the code. Thanks to Bero for the Joliet support here.

This FS is considerably simplified from what you'd find in a bigger kernel
like Linux or BSD, since we have the pleasure of working with only a single
device capable of ISO9660 at once =). So there are a number of things in here
that are global variables that might otherwise not be.

Some thanks are in order here to Marcus Comstedt for providing an ISO9660
implementation that was easy enough to understand without downloading the
full spec =). Thanks also in order to the creators of the BSD and Linux
ISO9660 systems, as these were used as references as well.

*/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wbool-compare"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"

#include <iso9660/iso9660.h>
#include <xetypes.h>
#include <ppc/atomic.h>
#include <diskio/disc_io.h>
#include <fcntl.h>
#include <sys/dirent.h>
#include <debug.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>

#define MAX_FN_LEN 256
#define DBG_NOTICE "[iso9660] Notice: "
#define DBG_ERROR "[iso9660] Error: "

#define dbglog(sev,prm...) printf(sev prm);

#define O_DIR 0x100000
#define MAX_ISO_FILES 8

static DISC_INTERFACE * iso9660_dev = NULL;

int iso_reset();
static int init_percd();
static int percd_done;

/********************************************************************************/
/* Low-level Joliet utils */

/* Joliet UCS is big endian */
static void utf2ucs(char * ucs, const char * utf) {
	int c;
	
	do {
		c = *utf++;
		if (c <= 0x7f) {
		} else if (c < 0xc0) {
			c = (c & 0x1f) << 6;
			c |= (*utf++) & 0x3f;
		} else {
			c = (c & 0x0f) << 12;
			c |= ((*utf++) & 0x3f) << 6;
			c |= (*utf++) & 0x3f;
		}
		*ucs++ = c >> 8;
		*ucs++ = c & 0xff;
	} while (c);
}

static void ucs2utfn(char * utf, const char * ucs, size_t len) {
	int c;
	
	len = len / 2;
	while (len) {
		len--;
		c = (*ucs++) << 8;
		c |= *ucs++;
		if (c == ';') break;
		if (c <= 0x7f) {
			*utf++ = c;
		} else if (c <= 0x7ff) {
			*utf++ = 0xc0 | (c >> 6);
			*utf++ = 0x80 | (c & 0x3f);
		} else {
			*utf++ = 0xe0 | (c >> 12);
			*utf++ = 0x80 | ((c >> 6) & 0x3f);
			*utf++ = 0x80 | (c & 0x3f);
		}
	}
	*utf = 0;
}

static int ucscompare(const char * isofn, const char * normalfn, int isosize) {
	int i, c0, c1 = 0;

	/* Compare ISO name */
	for (i=0; i<isosize; i+=2) {
		c0 = ((int)isofn[i] << 8) | ((int)isofn[i+1]);
		c1 = ((int)normalfn[i] << 8) | ((int)normalfn[i+1]);

		if (c0 == ';') break;

		/* Otherwise, compare the chars normally */
		if (tolower(c0) != tolower(c1))
			return -1;
	}

	c1 = ((int)normalfn[i] << 8) | (normalfn[i+1]);

	/* Catch ISO name shorter than normal name */
	if (c1 != '/' && c1 != '\0')
		return -1;
	else
		return 0;
}

static int isjoliet(char * p) {
	if (p[0] == '%' && p[1] == '/') {
		switch (p[2]) {
		case '@': return 1;
		case 'C': return 2;
		case 'E': return 3;
		}
	}
	return 0;
}

static int joliet;

/********************************************************************************/
/* Low-level ISO utils */

/* ISO Directory entry */
typedef struct {
	u8	length;			/* 711 */
	u8	ext_attr_length;	/* 711 */
	u8	extent[8];		/* 733 */
	u8	size[8];		/* 733 */
	u8	date[7];		/* 7x711 */
	u8	flags;
	u8	file_unit_size;		/* 711 */
	u8	interleave;		/* 711 */
	u8	vol_sequence[4];	/* 723 */
	u8	name_len;		/* 711 */
	char	name[1];
} iso_dirent_t;

typedef struct filestruct_s
{
	int fd;
} FILE_STRUCT;

/* Util function to reverse the byte order of a u32 */
static u32 ntohl_32(const void *data) {
	const u8 *d = (const u8*)data;
	return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | (d[3] << 0);
} 

/* This seems kinda silly, but it's important since it allows us
   to do unaligned accesses on a buffer */
static u32 htohl_32(const void *data) {
	const u8 *d = (const u8*)data;
	return (d[0] << 0) | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
}

/* Read red-book section 7.1.1 number (8 bit) */
/* static u8 iso_711(const u8 *from) { return (*from & 0xff); } */

/* Read red-book section 7.3.3 number (32 bit LE / 32 bit BE) */
static u32 iso_733(const u8 *from) { return htohl_32(from); }


/********************************************************************************/
/* Low-level block cacheing routines. This implements a simple queue-based 
   LRU/MRU cacheing system. Whenever a block is requested, it will be placed
   on the MRU end of the queue. As more blocks are loaded than can fit in
   the cache, blocks are deleted from the LRU end. */

/* Holds the data for one cache block, and a pointer to the next one.
   As sectors are read from the disc, they are added to the front of
   this cache. As the cache fills up, sectors are removed from the end
   of it. */
typedef struct {
	s32	sector;			/* CD sector */
	u8	data[2048];		/* Sector data */
} cache_block_t;

/* List of cache blocks (ordered least recently used to most recently) */
#define NUM_CACHE_BLOCKS 16
static cache_block_t *icache[NUM_CACHE_BLOCKS];		/* inode cache */
static cache_block_t *dcache[NUM_CACHE_BLOCKS];		/* data cache */

/* Cache modification mutex */
static unsigned int * cache_mutex;

/* Clears all cache blocks */
static void bclear_cache(cache_block_t **cache) {
	int i;
	
	lock(cache_mutex);
	for (i=0; i<NUM_CACHE_BLOCKS; i++)
		cache[i]->sector = -1;
	unlock(cache_mutex);
}

/* Graduate a block from its current position to the MRU end of the cache */
static void bgrad_cache(cache_block_t **cache, int block) {
	int		i;
	cache_block_t	*tmp;
	
	/* Don't try it with the end block */
	if (block < 0 || block >= (NUM_CACHE_BLOCKS-1)) return;
	
	/* Make a copy and scoot everything down */
	tmp = cache[block];
	for (i=block; i<(NUM_CACHE_BLOCKS - 1); i++)
		cache[i] = cache[i+1];
	cache[NUM_CACHE_BLOCKS-1] = tmp;
}

/* Pulls the requested sector into a cache block and returns the cache
   block index. Note that the sector in question may already be in the
   cache, in which case it just returns the containing block. */
static void iso_break_all();
static int bread_cache(cache_block_t **cache, u32 sector) {
	int i, j, rv;

	rv = -1;	
	lock(cache_mutex);

	/* Look for a pre-existing cache block */
	for (i=NUM_CACHE_BLOCKS-1; i>=0; i--) {
		if (cache[i]->sector == sector) {
			bgrad_cache(cache, i);
			rv = NUM_CACHE_BLOCKS - 1;
			goto bread_exit;
		}
	}
	
	/* If not, look for an open cache slot; if we find one, use it */
	for (i=0; i<NUM_CACHE_BLOCKS; i++) {
		if (cache[i]->sector == -1) break;
	}
	
	/* If we didn't find one, kick an LRU block out of cache */
	if (i >= NUM_CACHE_BLOCKS) { i = 0; }
	
	/* Load the requested block */
	j = iso9660_dev->readSectors(sector, 1, cache[i]->data);
	if (j < 0) {
		
		if (j==DISKIO_ERROR_NO_MEDIA) {
			unlock(cache_mutex);
			iso_reset();
		} else
			dbglog(DBG_ERROR, "fs_iso9660: can't read_sectors for %d: %d\n", (unsigned int)sector, j);
		
		rv = -1;
		goto bread_exit;
	}
	cache[i]->sector = sector;
	
	/* Move it to the most-recently-used position */
	bgrad_cache(cache, i);
	rv = NUM_CACHE_BLOCKS - 1;

	/* Return the new cache block index */
bread_exit:
	unlock(cache_mutex);
	return rv;
}

/* read data block */
static int bdread(u32 sector) {
	return bread_cache(dcache, sector);
}

/* read inode block */
static int biread(u32 sector) {
	return bread_cache(icache, sector);
}

/* Clear both caches */
static void bclear() {
	bclear_cache(dcache);
	bclear_cache(icache);
}

/********************************************************************************/
/* Higher-level ISO9660 primitives */

/* Root FS session location (in sectors) */
static u32 session_base = 0;

/* Root directory extent and size in bytes */
static u32 root_extent = 0, root_size = 0;

/* Root dirent */
static iso_dirent_t root_dirent;


/* Per-disc initialization; this is done every time it's discovered that
   a new CD has been inserted. */
static int init_percd() {
	int		i, blk;

	//dbglog(DBG_NOTICE, "fs_iso9660: disc change detected\n");
	
	/* Start off with no cached blocks and no open files*/
	iso_reset();
	
	/* Check for joliet extensions */
	joliet = 0;
	for (i=1; i<=3; i++) {
		blk = biread(session_base + i + 16);
		if (blk < 0) return blk;
		if (memcmp((char *)icache[blk]->data, "\02CD001", 6) == 0) {
			joliet = isjoliet((char *)icache[blk]->data+88);
			dbglog(DBG_NOTICE, "  (joliet level %d extensions detected)\n", joliet);
			if (joliet) break;
		}
	}

	/* If that failed, go after standard/RockRidge ISO */
	if (!joliet) {
		/* Grab and check the volume descriptor */	
		blk = biread(session_base + 16);
		if (blk < 0) return i;
		if (memcmp((char*)icache[blk]->data, "\01CD001", 6)) {
			dbglog(DBG_ERROR, "fs_iso9660: disc is not iso9660\r\n");
			return -1;
		}
	}

	/* Locate the root directory */
	memcpy(&root_dirent, icache[blk]->data+156, sizeof(iso_dirent_t));
	root_extent = iso_733(root_dirent.extent);
	root_size = iso_733(root_dirent.size);
	
	return 0;
}

/* Compare an ISO9660 filename against a normal filename. This takes into
   account the version code on the end and is not case sensitive. Also
   takes into account the trailing period that some CD burning software
   adds. */
static int fncompare(const char *isofn, int isosize, const char *normalfn) {
	//printf("Comparing %s against %s\n", isofn, normalfn);
	int i;

	/* Compare ISO name */
	for (i=0; i<isosize; i++) {
		/* Weed out version codes */
		if (isofn[i] == ';') break;

		/* Deal with crap '.' at end of filenames */
		if (isofn[i] == '.' &&
				(i == (isosize-1) || isofn[i+1] == ';'))
			break;

		/* Otherwise, compare the chars normally */
		if (tolower(isofn[i]) != tolower(normalfn[i]))
			return -1;
	}

	/* Catch ISO name shorter than normal name */
	if (normalfn[i] != '/' && normalfn[i] != '\0')
		return -1;
	else
		return 0;
}

/* Locate an ISO9660 object in the given directory; this can be a directory or
   a file, it works fine for either one. Pass in:
   
   fn:		object filename (relative to the passed directory)
   dir:		0 if looking for a file, 1 if looking for a dir
   dir_extent:	directory extent to start with
   dir_size:	directory size (in bytes)
   
   It will return a pointer to a transient dirent buffer (i.e., don't
   expect this buffer to stay around much longer than the call itself).
 */
static iso_dirent_t *find_object(const char *fn, int dir, u32 dir_extent, u32 dir_size) {
	int		i, c;
	iso_dirent_t	*de;

	/* RockRidge */
	int		len;
	char	*pnt;
	char	rrname[MAX_FN_LEN];
	int		rrnamelen;
	int		size_left;

	/* We need this to be signed for our while loop to end properly */
	size_left = (int)dir_size;

	/* Joliet */
	char		* ucsname = rrname;

	/* If this is a Joliet CD, then UCSify the name */
	if (joliet)
		utf2ucs(ucsname, fn);
	
	while (size_left > 0) {
		c = biread(dir_extent);
		if (c < 0) return NULL;
		
		for (i=0; i<2048 && i<size_left; ) {
			/* Locate the current dirent */
			de = (iso_dirent_t *)(icache[c]->data + i);
			if (!de->length) break;

			/* Try the Joliet filename if the CD is a Joliet disc */
			if (joliet) {
				if (!ucscompare(de->name, ucsname, de->name_len)) {
					if (!((dir << 1) ^ de->flags))
						return de;
				}
			} else {
				/* Assume no Rock Ridge name */
				rrnamelen = 0;
		
				/* Check for Rock Ridge NM extension */
				len = de->length - sizeof(iso_dirent_t)
					+ sizeof(de->name) - de->name_len;
				pnt = (char *)de + sizeof(iso_dirent_t)
					- sizeof(de->name) + de->name_len;
				if ((de->name_len & 1) == 0) {
					pnt++; len--;
				}
				while ((len >= 4) && ((pnt[3] == 1) || (pnt[3] == 2))) {
					if (strncmp(pnt, "NM", 2) == 0) {
						rrnamelen = pnt[2] - 5;
						strncpy(rrname, pnt+5, rrnamelen);
						rrname[rrnamelen] = 0;
					}
					len -= pnt[2];
					pnt += pnt[2];
				}
			
				/* Check the filename against the requested one */
				if (rrnamelen > 0) {
					char *p = strchr(fn, '/');
					int fnlen;
				
					if (p)
						fnlen = p - fn;
					else
						fnlen = strlen(fn);

					if (!strncasecmp(rrname, fn, fnlen)) {
						if (!((dir << 1) ^ de->flags))
							return de;
					}
				} else {
					if (!fncompare(de->name, de->name_len, fn)) {
						if (!((dir << 1) ^ de->flags))
							return de;
					}
				}
			}
			
			i += de->length;
		}
		
		dir_extent++;
		size_left -= 2048;
	}
	
	return NULL;
}

/* Locate an ISO9660 object anywhere on the disc, starting at the root,
   and expecting a fully qualified path name. This is analogous to find_object
   but it searches with the path in mind.

   fn:		object filename (relative to the passed directory)
   dir:		0 if looking for a file, 1 if looking for a dir
   dir_extent:	directory extent to start with
   dir_size:	directory size (in bytes)
   
   It will return a pointer to a transient dirent buffer (i.e., don't
   expect this buffer to stay around much longer than the call itself).
 */
static iso_dirent_t *find_object_path(const char *fn, int dir, iso_dirent_t *start) {
	char		*cur;

	/* If the object is in a sub-tree, traverse the trees looking
	   for the right directory */
	while ((cur = strchr(fn, '/'))) {
		if (cur != fn) {
			/* Note: trailing path parts don't matter since find_object
			   only compares based on the FN length on the disc. */
			start = find_object(fn, 1, iso_733(start->extent), iso_733(start->size));
			if (start == NULL) 
				return NULL;
		}
		fn = cur + 1;
	}

	/* Locate the file in the resulting directory */
	if (*fn) {
		start = find_object(fn, dir, iso_733(start->extent), iso_733(start->size));
		return start;
	}
	else {
		if (!dir)
			return NULL;
		else
			return start;
	}
}

/********************************************************************************/
/* File primitives */

/* File handles.. I could probably do this with a linked list, but I'm just
   too lazy right now. =) */
static struct {
	u32		first_extent;	/* First sector */
	int		dir;		/* >0 if a directory */
	u32		ptr;		/* Current read position in bytes */
	u32		size;		/* Length of file in bytes */
	struct dirent	dirent;		/* A static dirent to pass back to clients */
	int		broken;		/* >0 if the CD has been swapped out since open */
} fh[MAX_ISO_FILES];

/* Mutex for file handles */
static unsigned int * fh_mutex;

/* Break all of our open file descriptor. This is necessary when the disc
   is changed so that we don't accidentally try to keep on doing stuff
   with the old info. As files are closed and re-opened, the broken flag
   will be cleared. */
static void iso_break_all() {
	int i;

	lock(fh_mutex);
	for (i=0; i<MAX_ISO_FILES; i++)
		fh[i].broken = 1;
	unlock(fh_mutex);
}

/* Open a file or directory */
static int iso_open(const char *fn, int mode) {
	int		fd;
	iso_dirent_t	*de;

	if (memcmp(fn, "dvd:/", 6))
		fn += 5;
	else if (memcmp(fn, "dvd", 3) && memcmp(fn + 4, ":/", 2))
		fn += 6;

	/* Make sure they don't want to open things as writeable */
	if (mode != O_RDONLY && mode != O_DIR)
		return 0;
	
	/* Do this only when we need to (this is still imperfect) */
	if (!percd_done && init_percd() < 0)
		return 0;
	percd_done = 1;

	/* Find the file we want */
	de = find_object_path(fn, (mode & O_DIR)?1:0, &root_dirent);
	if (!de)	
		return 0;
	
	/* Find a free file handle */
	lock(fh_mutex);
	for (fd=0; fd<MAX_ISO_FILES; fd++){
		if (fh[fd].first_extent == 0) {
			fh[fd].first_extent = -1;
			break;
		}
	}
	unlock(fh_mutex);
	if (fd >= MAX_ISO_FILES)
		return 0;

	/* Fill in the file handle and return the fd */
	fh[fd].first_extent = iso_733(de->extent);
	fh[fd].dir = (mode & O_DIR)?1:0;
	fh[fd].ptr = 0;
	fh[fd].size = iso_733(de->size);
	fh[fd].broken = 0;
	
	return fd;
}

/* Close a file or directory */
static void iso_close(int fd) {
	/* Check that the fd is valid */
	if (fd < MAX_ISO_FILES) {
		/* No need to lock the mutex: this is an atomic op */
		fh[fd].first_extent = 0;
	}
}

/* Read from a file */
static ssize_t iso_read(int fd, void *buf, size_t bytes) {
	int rv, toread, thissect, c;
	u8 * outbuf;

	/* Check that the fd is valid */
	if (fd >= MAX_ISO_FILES || fh[fd].first_extent == 0 || fh[fd].broken)	
		return -1;	

	rv = 0;
	outbuf = (u8 *)buf;

	/* Read zero or more sectors into the buffer from the current pos */
	while (bytes > 0) {
		/* Figure out how much we still need to read */
		toread = (bytes > (fh[fd].size - fh[fd].ptr)) ?
			fh[fd].size - fh[fd].ptr : bytes;
		if (toread == 0) break;

		/* How much more can we read in the current sector? */
		thissect = 2048 - (fh[fd].ptr % 2048);

		/* If we're on a sector boundary and we have more than one
		   full sector to read, then short-circuit the cache here
		   and use the multi-sector reads from the CD unit (this
		   should theoretically be a lot faster) */
		/* XXX This code isn't actually faster, but I'm leaving it
		   here commented out in case we could find a better use
		   for it later than speed (i.e., preventing thread context
		   switches). */
		 if (thissect == 2048 && toread >= 2048) {
			// Round it off to an even sector count
			thissect = toread / 2048;
			if (thissect>32) thissect=32; // 64K max block size
			toread = thissect * 2048;

			/*printf("cdrom: short-circuit read for %d sectors\n",
				thissect);*/

			// Do the read
			if (iso9660_dev->readSectors(fh[fd].first_extent + fh[fd].ptr/2048, thissect, outbuf) < 0)
				return -2; // Something went wrong...
		} else { 
			toread = (toread > thissect) ? thissect : toread;
		
			/* Do the read */
			c = bdread(fh[fd].first_extent + fh[fd].ptr/2048);
			if (c < 0)			
				return -3;			
			memcpy(outbuf, dcache[c]->data + (fh[fd].ptr%2048), toread);
		} 

		/* Adjust pointers */
		outbuf += toread;
		fh[fd].ptr += toread;
		bytes -= toread;
		rv += toread;
	}

	return rv;
}

/* Seek elsewhere in a file */
static off_t iso_seek(int fd, off_t offset, int whence) {
	/* Check that the fd is valid */
	if (fd>=MAX_ISO_FILES || fh[fd].first_extent==0 || fh[fd].broken)
		return -1;

	/* Update current position according to arguments */
	switch (whence) {
		case SEEK_SET:
			fh[fd].ptr = offset;
			break;
		case SEEK_CUR:
			fh[fd].ptr += offset;
			break;
		case SEEK_END:
			fh[fd].ptr = fh[fd].size + offset;
			break;
		default:
			return -1;
	}
	
	/* Check bounds */
	if (fh[fd].ptr < 0) fh[fd].ptr = 0;
	if (fh[fd].ptr > fh[fd].size) fh[fd].ptr = fh[fd].size;
	
	return fh[fd].ptr;
}

/* Tell where in the file we are */
static off_t iso_tell(int fd) {
	if (fd>=MAX_ISO_FILES || fh[fd].first_extent==0 || fh[fd].broken)
		return -1;

	return fh[fd].ptr;
}

/* Tell how big the file is */
static size_t iso_total(int fd) {
	if (fd>=MAX_ISO_FILES || fh[fd].first_extent==0 || fh[fd].broken)
		return -1;

	return fh[fd].size;
}

/* Helper function for readdir: post-processes an ISO filename to make
   it a bit prettier. */
static void fn_postprocess(char *fnin) {
	char	* fn = fnin;

	while (*fn && *fn != ';') {
		*fn = tolower(*fn);
		fn++;
	}
	*fn = 0;

	/* Strip trailing dots */
	if (fn > fnin && fn[-1] == '.') {
		fn[-1] = 0;
	}
}

/* Read a directory entry */
static struct dirent *iso_readdir(int fd) {
	int		c;
	iso_dirent_t	*de;

	/* RockRidge */
	int		len;
	char	*pnt;

	if (fd>=MAX_ISO_FILES || fh[fd].first_extent==0 || !fh[fd].dir || fh[fd].broken)
		return NULL;

	/* Scan forwards until we find the next valid entry, an
	   end-of-entry mark, or run out of dir size. */
	c = -1; de = NULL;
	while(fh[fd].ptr < fh[fd].size) {
		/* Get the current dirent block */
		c = biread(fh[fd].first_extent + fh[fd].ptr/2048);
		if (c < 0) return NULL;
	
		de = (iso_dirent_t *)(icache[c]->data + (fh[fd].ptr%2048));
		if (de->length) break;

		/* Skip to the next sector */
		fh[fd].ptr += 2048 - (fh[fd].ptr%2048);
	}
	if (fh[fd].ptr >= fh[fd].size) return NULL;
	
	/* If we're at the first, skip the two blank entries */
	if (!de->name[0] && de->name_len == 1) {
		fh[fd].ptr += de->length;
		de = (iso_dirent_t *)(icache[c]->data + (fh[fd].ptr%2048));
		fh[fd].ptr += de->length;
		de = (iso_dirent_t *)(icache[c]->data + (fh[fd].ptr%2048));
		if (!de->length) return NULL;
	}

	if (joliet) {
		ucs2utfn(fh[fd].dirent.d_name, de->name, de->name_len);
	} else {
		/* Fill out the VFS dirent */
		strncpy(fh[fd].dirent.d_name, de->name, de->name_len);
		fh[fd].dirent.d_name[de->name_len] = 0;
		fn_postprocess(fh[fd].dirent.d_name);

		/* Check for Rock Ridge NM extension */
		len = de->length - sizeof(iso_dirent_t) + sizeof(de->name) - de->name_len;
		pnt = (char*)de + sizeof(iso_dirent_t) - sizeof(de->name) + de->name_len;
		if ((de->name_len & 1) == 0) {
			pnt++; len--;
		}
		while ((len >= 4) && ((pnt[3] == 1) || (pnt[3] == 2))) {
			if (strncmp(pnt, "NM", 2) == 0) {
				strncpy(fh[fd].dirent.d_name, pnt+5, pnt[2] - 5);
				fh[fd].dirent.d_name[pnt[2] - 5] = 0;
			}
			len -= pnt[2];
			pnt += pnt[2];
		}
	}
	
	//fh[fd].dirent.d_namlen = de->name_len;

	if (de->flags & 2){
		//fh[fd].dirent.d_reclen = -1;
		fh[fd].dirent.d_type = DT_DIR;
	}else{
		//fh[fd].dirent.d_reclen = iso_733(de->size);
		fh[fd].dirent.d_type = DT_REG;
	}

	fh[fd].ptr += de->length;
	
	return &fh[fd].dirent;
}

int iso_reset() {
	iso_break_all();
	bclear();
	percd_done = 0;
	return 0;
}

#if 0
/* This handler will be called during every vblank. We have to
   be careful about modifying variables that are in use in the
   foreground, so instead we'll just set a "dead" flag and next
   time someone calls in it'll get reset. */
static int iso_last_status;
static int iso_vblank_hnd;
static void iso_vblank(u32 evt) {
	int status, disc_type;

	/* Get the status. This may fail if a CD operation is in
	   progress in the foreground. */
	if (cdrom_get_status(&status, &disc_type) < 0)
		return;

	if (iso_last_status != status) {
		if (status == CD_STATUS_OPEN || status == CD_STATUS_NO_DISC)
			percd_done = 0;
		iso_last_status = status;
	}
}
#endif

/* There's only one ioctl at the moment (re-initialize caches) but you should
   always clear data and size. */
static int iso_ioctl(int hnd, void *data, size_t size) {
	iso_reset();

	return 0;
}

/* Initialize the file system */
int fs_iso9660_init() {
	int i;

	/* Reset fd's */
	memset(fh, 0, sizeof(fh));
	
	/* Mark the first as active so we can have an error FD of zero */
	fh[0].first_extent = -1;
	
	/* Init thread mutexes */
	cache_mutex = malloc(sizeof(u32));
	fh_mutex = malloc(sizeof(u32));
	*cache_mutex = 0;
	*fh_mutex = 0;

	/* Allocate cache block space */
	for (i=0; i<NUM_CACHE_BLOCKS; i++) {
		icache[i] = malloc(sizeof(cache_block_t));
		icache[i]->sector = -1;
		dcache[i] = malloc(sizeof(cache_block_t));
		dcache[i]->sector = -1;
	}

	percd_done = 0;

#if 0
	iso_last_status = -1;
#endif

	return 0;
}

/* De-init the file system */
int fs_iso9660_shutdown() {
	int i;

	/* Dealloc cache block space */
	for (i=0; i<NUM_CACHE_BLOCKS; i++) {
		free(icache[i]);
		free(dcache[i]);
	}

	/* Free muteces */
	if (cache_mutex != NULL)
		free(cache_mutex);
	if (fh_mutex != NULL)
		free(fh_mutex);
	cache_mutex = fh_mutex = NULL;
	
	return 0;
}

static int _ISO9660_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
	FILE_STRUCT* file = (FILE_STRUCT*)fileStruct;
	file->fd = iso_open(path, flags);
	return (int)file;
}

static int _ISO9660_close_r(struct _reent *r, int fd)
{
	fd = ((FILE_STRUCT*)fd)->fd;
	iso_close(fd);
	return 0;
}

static ssize_t _ISO9660_read_r(struct _reent *r, int fd, char *ptr, size_t len)
{
	fd = ((FILE_STRUCT*)fd)->fd;
	return iso_read(fd, ptr, len);
}

static off_t _ISO9660_seek_r(struct _reent *r, int fd, off_t pos, int dir)
{
	fd = ((FILE_STRUCT*)fd)->fd;
	return iso_seek(fd, pos, dir);
}

static int _ISO9660_fstat_r(struct _reent *r, int fd, struct stat *st)
{
	fd = ((FILE_STRUCT*)fd)->fd;
	memset(st, 0, sizeof(stat));
	st->st_size = iso_total(fd);
	st->st_mode = S_IFREG;
	st->st_blksize = 65536;
	return 0;
}

static int _ISO9660_stat_r(struct _reent *r, const char *path, struct stat *st)
{
	int fd = iso_open(path, O_RDONLY);
	memset(st, 0, sizeof(stat));
	st->st_size = iso_total(fd);
	st->st_mode = S_IFREG;
	st->st_blksize = 65536;
	iso_close(fd);
	return 0;
}

static int _ISO9660_chdir_r(struct _reent *r, const char *path)
{
	/*DIR_ENTRY entry;
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return -1;
	}

	if (!entry_from_path(mdescr, &entry, path))
	{
		r->_errno = ENOENT;
		return -1;
	}
	else if (!is_dir(&entry))
	{
		r->_errno = ENOTDIR;
		return -1;
	}

	mdescr->iso_currententry = entry.path_entry;
	if (entry.children)
		free(entry.children);
*/
	return 0; //Dunno what to do about this one yet...
}

static DIR_ITER* _ISO9660_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path)
{
	/*DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);
	MOUNT_DESCR *mdescr;

	mdescr = _ISO9660_getMountDescrFromPath(path, NULL);
	if (mdescr == NULL)
	{
		r->_errno = ENODEV;
		return NULL;
	}

	if (!entry_from_path(mdescr, &state->entry, path))
	{
		r->_errno = ENOENT;
		return NULL;
	}
	else if (!is_dir(&state->entry))
	{
		r->_errno = ENOTDIR;
		return NULL;
	}

	state->index = 0;
	state->inUse = true;
	return dirState;*/
	return dirState; //Dunno what to do about this one yet...
}

static int _ISO9660_dirreset_r(struct _reent *r, DIR_ITER *dirState)
{
	/*DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	state->index = 0;*/
	return 0; //Dunno what to do about this one yet...
}

static int _ISO9660_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st)
{
	/*DIR_ENTRY *entry;
	DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	if (state->index >= state->entry.fileCount)
	{
		r->_errno = ENOENT;
		return -1;
	}

	entry = &state->entry.children[state->index++];
	strncpy(filename, entry->name, ISO_MAXPATHLEN - 1);
	stat_entry(entry, st);*/
	return 0; //Dunno what to do about this one yet...
}

static int _ISO9660_dirclose_r(struct _reent *r, DIR_ITER *dirState)
{
	/*DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT*) (dirState->dirStruct);

	if (!state->inUse)
	{
		r->_errno = EBADF;
		return -1;
	}

	state->inUse = false;
	if (state->entry.children)
		free(state->entry.children);*/
	return 0; //Dunno what to do about this one yet...
}

static int _ISO9660_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf)
{
	// FAT clusters = POSIX blocks
	buf->f_bsize = 0x800; // File system block size. 
	buf->f_frsize = 0x800; // Fundamental file system block size. 

	//buf->f_blocks	= totalsectors; // Total number of blocks on file system in units of f_frsize. 
	buf->f_bfree = 0; // Total number of free blocks. 
	buf->f_bavail = 0; // Number of free blocks available to non-privileged process. 

	// Treat requests for info on inodes as clusters
	//buf->f_files = totalentries;	// Total number of file serial numbers. 
	buf->f_ffree = 0; // Total number of free file serial numbers. 
	buf->f_favail = 0; // Number of file serial numbers available to non-privileged process. 

	// File system ID. 32bit ioType value
	buf->f_fsid = 0; //??!!? 

	// Bit mask of f_flag values.
	buf->f_flag = ST_NOSUID // No support for ST_ISUID and ST_ISGID file mode bits
			| ST_RDONLY; // Read only file system
	// Maximum filename length.
	buf->f_namemax = 208;
	return 0;
}

static const devoptab_t dotab_iso9660 =
{
	NULL,
	sizeof(FILE_STRUCT),//Dunno?!
	_ISO9660_open_r,
	_ISO9660_close_r,
	NULL,
	_ISO9660_read_r,
	_ISO9660_seek_r,
	_ISO9660_fstat_r,
	_ISO9660_stat_r,
	NULL,
	NULL,
	_ISO9660_chdir_r,
	NULL,
	NULL,
	sizeof(FILE_STRUCT),//Dunno?!
	_ISO9660_diropen_r,
	_ISO9660_dirreset_r,
	_ISO9660_dirnext_r,
	_ISO9660_dirclose_r,
	_ISO9660_statvfs_r,
	NULL, // device ftruncate_r
	NULL, // device fsync_r
	NULL // device data
};

static int verify_iso9660_disc(const DISC_INTERFACE *di)
{
	char sector_data[2048];
	int is_joliet, i, blk = 0;

	/* Check for joliet extensions */
	is_joliet = 0;
	for (i=1; i<=3; i++) {
		blk = di->readSectors(16, 1, sector_data);
		if (blk < 0) return blk;
		if (memcmp(sector_data, "\02CD001", 6) == 0) {
			is_joliet = isjoliet(sector_data+88);
			if (is_joliet) break;
		}
	}

	/* If that failed, go after standard/RockRidge ISO */
	if (!is_joliet) {
		/* Grab and check the volume descriptor */	
		blk = di->readSectors(16, 1, sector_data);
		if (blk < 0) return i;
		if (memcmp(sector_data, "\01CD001", 6) != 0) {
			return -1;
		}
	}

	return 0;
}

bool ISO9660_Mount(const char* name, const DISC_INTERFACE *disc_interface)
{
	char *nameCopy;
	devoptab_t *devops = NULL;
	char devname[10];

	if (!name || strlen(name) > 8 || !disc_interface)
		return false;

	if (!disc_interface->startup())
		return false;

	if (!disc_interface->isInserted())
		return false;
	
	// Verify that it's actually an ISO9660 formatted disc and not another format or not inserted
	if (verify_iso9660_disc(disc_interface) != 0)
		return false;

	sprintf(devname, "%s:", name);
	if (FindDevice(devname) >= 0)
		return false;

	devops = malloc(sizeof(dotab_iso9660) + strlen(name) + 1);
	if (!devops)
		return false;

	// Use the space allocated at the end of the devoptab struct for storing the name
	nameCopy = (char*) (devops + 1);

	iso9660_dev = disc_interface;
	fs_iso9660_init();

	// Add an entry for this device to the devoptab table
	memcpy(devops, &dotab_iso9660, sizeof(dotab_iso9660));
	strcpy(nameCopy, name);
	devops->name = nameCopy;
	if (AddDevice(devops) < 0)
	{
		free(devops);
		return false;
	}

	return true;
}

static bool check_dev_name(const char* name, char *devname, size_t devname_size)
{
    size_t len;
    
	if (!name)
		return false;

	len = strlen(name);
	if (len == 0 || len > devname_size-2)
		return false;

	// append ':' if missing
	strcpy(devname, name);
	if (devname[len-1] != ':')
		strcat(devname, ":");

	return true;
}

bool ISO9660_Unmount(const char* name)
{
	char devname[11];
	if (! check_dev_name(name, devname, sizeof(devname)))
		return false;
	fs_iso9660_shutdown();	
	if (RemoveDevice(name) == -1)
		return false;
	return true;
}

const char *ISO9660_GetVolumeLabel(const char *name)
{
	char devname[11];
	if (!check_dev_name(name, devname, sizeof(devname)))
		return NULL;
	return name;
}

#pragma GCC diagnostic pop
