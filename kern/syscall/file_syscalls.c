/*
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>


/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int *retval)
{
	const int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;

	unsigned int throwaway;
	char *kpath;

	int result;
	int testflag = flags | allflags;
	if(testflag != allflags)
		panic("Your flags are bogus.\n");

	struct openfile *file;
	kpath = (char *)kmalloc(sizeof(char)*31);
	copyinstr(upath,kpath,sizeof(char)*31, &throwaway);

	result = openfile_open(kpath, flags, mode, &file);	//Store pathname in upath (what's kpath for?)

	if (result==0)
		filetable_place(curproc->p_filetable, file, retval);

	kfree(kpath);


	/* 
	 * Your implementation of system call open starts here.  
	 *
	 * Check the design document design/filesyscall.txt for the steps
	 */
	//(void) upath; // suppress compilation warning until code gets written
	//(void) flags; // suppress compilation warning until code gets written
	//(void) mode; // suppress compilation warning until code gets written
	//(void) retval; // suppress compilation warning until code gets written
	//(void) allflags; // suppress compilation warning until code gets written
	//(void) kpath; // suppress compilation warning until code gets written
	//(void) file; // suppress compilation warning until code gets written

	return result;
}

/*
 * read() - read data from a file
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
       int result = 0;

       /* 
        * Your implementation of system call read starts here.  
        *
        * Check the design document design/filesyscall.txt for the steps
        */

       struct openfile *file;			//feed into openfile_get
       struct iovec iov;
       struct uio u;

       if(filetable_get(curproc->p_filetable, fd, &file) == EBADF)
    	   return EBADF;

       /***begin crit sec***/
       if (VOP_ISSEEKABLE(file->of_vnode))
			lock_acquire(file->of_offsetlock);

       if (file->of_accmode == O_WRONLY)
    	   return EBADF;

       uio_kinit(&iov,&u,buf,size,0,UIO_READ);//initialize uio
       iov.iov_ubase = buf;					//set user pointer space to buf.
       u.uio_space = proc_getas();			//correct the UIO_USERSPACE info;
       u.uio_segflg = UIO_USERSPACE;		//correct the UIO_USERSPACE flag;

       result = VOP_READ(file->of_vnode, &u);

       file->of_offset = u.uio_offset;		//update seek position

       if (lock_do_i_hold(file->of_offsetlock))
    	   lock_release(file->of_offsetlock);
      /***end crit sec***/

       filetable_put(curproc->p_filetable, fd, file);
       *retval = (int)u.uio_offset;

       //(void) fd; // suppress compilation warning until code gets written
       //(void) buf; // suppress compilation warning until code gets written
       //(void) size; // suppress compilation warning until code gets written
       //(void) retval; // suppress compilation warning until code gets written

       return result;
}

/*
 * write() - write data to a file
 */
int
sys_write(int fd, userptr_t buf, size_t size, int *retval)
{
	struct openfile *file;
	struct iovec iov;
	struct uio u;
	int result = 0;

	if(filetable_get(curproc->p_filetable, fd, &file) == EBADF)
	    	   return EBADF;

	/***begin crit sec***/
	if (VOP_ISSEEKABLE(file->of_vnode))
		lock_acquire(file->of_offsetlock);

	if (file->of_accmode == O_RDONLY)
	   return EBADF;

	uio_kinit(&iov,&u,buf,size,0,UIO_WRITE);		//initialize uio
	iov.iov_ubase = buf;							//set user pointer space to buf.
	u.uio_space = proc_getas();
	u.uio_segflg = UIO_USERSPACE;					//correct the UIO_SYSSPACE flag;

	result = VOP_WRITE(file->of_vnode, &u);

	file->of_offset = u.uio_offset;					//update seek position

	if (lock_do_i_hold(file->of_offsetlock))
	    lock_release(file->of_offsetlock);
	 /***end crit sec***/

	filetable_put(curproc->p_filetable, fd, file);
	*retval = (int)(u.uio_offset);					//Store # of bytes written

	//(void)fd;
	//(void)buf;
	//(void)size;
	//(void)retval;

	return result;

}
/*
 * close() - remove from the file table.
 */
int
sys_close(int fd)
{
	struct openfile *file;

	if( !(filetable_okfd(curproc->p_filetable, fd)))
		return EBADF;

	filetable_placeat(curproc->p_filetable, NULL, fd, &file);

	if (file != NULL)
		openfile_decref(file);



	return 0;
}

/* 
* meld () - combine the content of two files word by word into a new file
*/
int
sys_meld(userptr_t pn1, userptr_t pn2, userptr_t pn3)
{
	int result = 0;
	int fd1;	//The fds store file descriptors of the 3 files
	int fd2;
	int fd3;
	int rv1 = 4;	//The rvs store the return vals of read for pn1 and pn2
	int rv2 = 4;
	int rvx;	//rvx is a throwaway parameter used for sys_write
	userptr_t buf = (userptr_t)kmalloc(sizeof(char)*4);

	//Open the 3 files
	result = sys_open(pn1, 0, O_RDONLY, &fd1);
	if (result)
		return result;

	result = sys_open(pn2, 0, O_RDONLY, &fd2);
	if (result)
		return result;

	result = sys_open(pn3, (O_APPEND | O_CREAT), O_WRONLY, &fd3);
	if (result)
		return result;

	//This loop reads 4 bites at a time from each read file and writes
	//them to the write file until they both run out of memory
	do {
		//take up to 4 bytes from pn1 and store in pn3
		if (rv1 == 4){
			result = sys_read(fd1, buf, 4, &rv1);
			if (result){
				kfree(buf);
				return result;
			}

			result = sys_write(fd3, buf, (size_t)rv1, &rvx);
			if (result){
				kfree(buf);
				return result;
			}
		}

		//take up to 3 bytes from pn1 and store in pn3
		if (rv2 == 4){
			result = sys_read(fd2, buf, 4, &rv2);
			if (result){
				kfree(buf);
				return result;
			}

			result = sys_write(fd3, buf, (size_t)rv2, &rvx);
			if (result){
				kfree(buf);
				return result;
			}
		}
	}while(rv1 == 4 || rv2 == 4);

	result = sys_close(fd1);
	if (result){
		kfree(buf);
		return result;
	}

	result = sys_close(fd2);
	if (result)
		return result;

	result = sys_close(fd3);
	if (result){
		kfree(buf);
		return result;
	}

	//(void)pn1;
	//(void)pn2;
	//(void)pn3;
	kfree(buf);
	return 0;
}
