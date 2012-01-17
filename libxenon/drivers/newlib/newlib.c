#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <debug.h>
#include <sys/time.h>

#include <fat/fat_rb.h>
#include <fat/file_rb.h>
#include <newlib/vfs.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/atomic.h>
#include <threads/threads.h>

#include <usb/usbmain.h>

struct mount_s;

static struct vfs_dir_s dd_array[MAXDD] = {};

size_t vfs_default_lseek(struct vfs_file_s *file, size_t offset, int whence)
{
	switch (whence)
	{
	case 0:
		file->offset = offset;
		break;
	case 1:
		file->offset += offset;
		break;
	case 2:
		file->offset = file->filesize + offset;
		break;
	}
	if (file->offset < 0)
		file->offset = 0;
	if (file->offset >= file->filesize)
		file->offset = file->filesize;
	return file->offset;
}


			/* /dev/console */
void (*stdout_hook)(const char *text, int len) = 0;

extern void putch(unsigned char c);

ssize_t vfs_console_write(struct vfs_file_s *file, const void *src, size_t len)
{
	if (stdout_hook)
		stdout_hook(src, len);
	size_t i;
	for (i = 0; i < len; ++i)
		putch(((const char*)src)[i]);
        
	return len;
}

int vfs_console_isatty(struct vfs_file_s *file)
{
	return 1;
}

struct vfs_fileop_s vfs_console_ops = {.write = vfs_console_write, .isatty = vfs_console_isatty};

int vfs_console_open(struct vfs_file_s *file, struct mount_s *mount, const char *filename, int oflags, int perm)
{
	if (!strcmp(filename, "console"))
	{
		file->ops = &vfs_console_ops;
		return 0;
	}
	return -ENOENT;
}

struct vfs_mountop_s vfs_console_mount_ops = {.open = vfs_console_open};

static struct mount_s mounts[MAXMOUNT] = {
	{0, "/dev/", &vfs_console_mount_ops},
};

struct mount_s *mount(const char *mountpoint, struct vfs_mountop_s *ops, struct bdev * device)
{
	int i;
	for (i = 0; i < MAXMOUNT; ++i)
	{
		if (!mounts[i].ops)
		{
			strcpy(mounts[i].mountpoint, mountpoint);
			mounts[i].index = i;
			mounts[i].ops = ops;

			if (mounts[i].ops->mount) mounts[i].ops->mount(&mounts[i],device);
						
			return &mounts[i];
		}
	}
	return 0;
}

void umount(struct mount_s *mount)
{
	if (mounts->ops->umount) mount->ops->umount(mount);
	mount->ops=NULL;
}

static struct vfs_file_s fd_array[MAXFD] = { 
	[0] = {.ops = &vfs_console_ops},
	[1] = {.ops = &vfs_console_ops},
	[2] = {.ops = &vfs_console_ops},
};

extern unsigned char heap_begin;
unsigned char *heap_ptr;

void * sbrk(ptrdiff_t incr)
{
	unsigned char *res;
	if (!heap_ptr)
		heap_ptr = &heap_begin;
	res = heap_ptr;
	heap_ptr += incr;
	return res;
}

/*
 * file functions
 */

static inline struct vfs_file_s *is_valid_and_open_fd(int fd)
{
	return (fd >= 0 && fd < MAXFD && fd_array[fd].ops) ? &fd_array[fd] : 0;
}

ssize_t write(int fildes, const void *buf, size_t nbyte)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	ssize_t res = file->ops->write ? file->ops->write(file, buf, nbyte) : -EPERM;
	if (res < 0)
	{
		errno = -res;
		return -1;
	} else
		return res;
}

int close(int fildes)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	if (file->ops->close)
		file->ops->close(file);

	memset(file, 0, sizeof(*file));

	return 0;
}

int fstat(int fildes, struct stat *buf)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	if (file->ops->fstat)
		return file->ops->fstat(file, buf);
	else
	{
		buf->st_mode = S_IFCHR;
		buf->st_blksize = 0;
		return 0;
	}
}

int isatty(int fildes)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	if (file->ops->isatty)
		return file->ops->isatty(file);
	else
		return 0;
}

off_t lseek(int fildes, off_t offset, int whence)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	if (file->ops->lseek)
		return file->ops->lseek(file, offset, whence);
	else
	{
		errno = ESPIPE;
		return -1;
	}
}

ssize_t read(int fildes, void *buf, size_t nbyte)
{
	struct vfs_file_s *file = is_valid_and_open_fd(fildes);
	
	if (!file)
	{
		errno = EBADF;
		return -1;
	}
	
	if (file->ops->read)
		return file->ops->read(file, buf, nbyte);
	else
	{
		errno = EINVAL;
		return -1;
	}
}

int open(const char *path, int oflag, ...)
{
	int fd = 0;
	while (fd < MAXFD)
	{
		if (!fd_array[fd].ops)
			break;
		fd++;
	}
	if (fd == MAXFD)
	{
		errno = EMFILE;
		return -1;
	}
	
	int i;
	for (i = 0; i < MAXMOUNT; ++i)
	{
		const char *mpath = mounts[i].mountpoint;
		if (path && *mpath && !strncmp(path, mpath, strlen(mpath)))
			if (!mounts[i].ops->open(&fd_array[fd], &mounts[i], path + strlen(mpath), oflag, 0))
				return fd;
	}

	memset(&fd_array[fd], 0, sizeof(struct vfs_file_s));

	errno = ENOENT;
	return -1;
}

/*
 * directories functions
 */

static inline struct vfs_dir_s *is_valid_and_open_DIR(DIR * d)
{
	return (d && d->dd_fd >= 0 && d->dd_fd < MAXDD && dd_array[d->dd_fd].ops) ? &dd_array[d->dd_fd] : 0;
}

DIR* opendir(const char* dirname)
{
	int dd = 0;
	while (dd < MAXDD)
	{
		if (!dd_array[dd].ops)
			break;
		dd++;
	}
	if (dd == MAXDD)
	{
		errno = EMFILE;
		return NULL;
	}
	
	int i;
	for (i = 0; i < MAXMOUNT; ++i)
	{
		const char *mpath = mounts[i].mountpoint;
		if (dirname && *mpath && !strncmp(dirname, mpath, strlen(mpath)))
			if(!mounts[i].ops->opendir(&dd_array[dd], &mounts[i], dirname + strlen(mpath))){
				DIR * d = &dd_array[dd].dir;
				memset(d,0,sizeof(*d));
				d->dd_fd=dd;
				return d;
			}
	}

	memset(&dd_array[dd], 0, sizeof(struct vfs_dir_s));

	errno = ENOENT;
	return NULL;
}

struct dirent* readdir(DIR* dirp)
{
	struct vfs_dir_s *dir = is_valid_and_open_DIR(dirp);
	
	if (!dir)
	{
		errno = EBADF;
		return NULL;
	}
	
	if (dir->ops->readdir)
		return dir->ops->readdir(dir);
	else
	{
		errno = EINVAL;
		return NULL;
	}
}

int closedir(DIR* dirp)
{
	struct vfs_dir_s *dir = is_valid_and_open_DIR(dirp);
	
	if (!dir)
	{
		errno = EBADF;
		return -1;
	}
	
	if (dir->ops->closedir)
		dir->ops->closedir(dir);

	memset(dir, 0, sizeof(*dir));

	return 0;
}

/*
 * system functions
 */

extern void return_to_xell(unsigned int nand_addr,unsigned int phy_loading_addr);
extern void enet_quiesce();

void shutdown_drivers()
{
	// some drivers require a shutdown
	
	enet_quiesce();
	usb_shutdown();
}	

#define XELL_FOOTER_OFFSET (256*1024-16)
#define XELL_FOOTER_LENGTH 16
#define XELL_FOOTER "xxxxxxxxxxxxxxxx"

void try_return_to_xell(unsigned int nand_addr,unsigned int phy_loading_addr)
{
	if(!memcmp((void*)(nand_addr+XELL_FOOTER_OFFSET),XELL_FOOTER,XELL_FOOTER_LENGTH))
		return_to_xell(nand_addr,phy_loading_addr);
}

void _exit(int status)
{
	char s[256];
	int i,stuck=0;
        
        //threading_shutdown();
	
	sprintf(s,"\n[Exit] with code %d\n", status);
	vfs_console_write(NULL,s,strlen(s));

	for(i=0;i<6;++i)
	{
		if (xenon_is_thread_task_running(i)){
			sprintf(s,"Thread %d is still running !\n",i);
			vfs_console_write(NULL,s,strlen(s));
			stuck=1;
		}
	}
	
	shutdown_drivers();
	
	if(stuck){
		sprintf(s,"Can't reload Xell, looping...");
		vfs_console_write(NULL,s,strlen(s));
	}else{
		sprintf(s,"Reloading Xell...");
		vfs_console_write(NULL,s,strlen(s));
		xenon_set_single_thread_mode();
		
                try_return_to_xell(0xc8070000,0x1c000000); // xell-gggggg (ggboot)
		try_return_to_xell(0xc8095060,0x1c040000); // xell-2f (freeboot)
		try_return_to_xell(0xc8100000,0x1c000000); // xell-1f, xell-ggggggg
	}

	for(;;);
}

pid_t getpid(void)
{
	return 0;
}

int kill(pid_t pid, int sig)
{
	return 0;
}

int unlink(const char *file)
{
	return -1;
}

#if 1
// 22 nov 2005 
#define	RTC_BASE	1132614024UL//1005782400UL

int gettimeofday(struct timeval * tp, void * tzp) {
    unsigned char msg[16] = {0x04};
    unsigned long long msec;
    unsigned long sec;
	
	xenon_smc_send_message(msg);
	xenon_smc_receive_response(msg);

	msec = msg[1] | (msg[2] << 8) | (msg[3] << 16) | (msg[4] << 24) | ((unsigned long long) msg[5] << 32);
	
    sec = msec / 1000;
    tp->tv_sec = sec + RTC_BASE;
    msec -= sec * 1000;
    tp->tv_usec = msec * 1000;

    return 0;
}
#endif