#ifndef SCULL_H
#define SCULL_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/list.h>

#include <asm/uaccess.h>
#include <asm/page.h>

#define DEVICE_NAME "scull"

#define SCULL_MAGIC 243
/*device major and minor number*/
int scull_major = 0;
int scull_minor = 0;
/*the number of device of scull*/
int scull_count = 4;

/*ioctl number*/
#define SCULL_GET_MAJOR _IOR(SCULL_MAGIC, 0, int)
#define SCULL_GET_MINOR _IOR(SCULL_MAGIC, 1, int)
#define SCULL_SET_MAJOR _IOW(SCULL_MAGIC, 2, int)
#define SCULL_SET_MINOR _IOW(SCULL_MAGIC, 3, int)

struct data_node {
	void *data;
	struct list_head list;
};

struct scull_dev {
	struct data_node data_header;
	size_t page_size; /*single page size*/
	size_t page_count; /*sum of pages*/
	size_t cur_size; /*sum of bytes*/
	struct cdev scull_cdev;
};

/*scull file operations*/
ssize_t scull_read(struct file *, char __user *, size_t, loff_t *);
loff_t scull_llseek(struct file *, loff_t, int);
ssize_t scull_write(struct file *, const char __user *, size_t, loff_t *);
long scull_unlocked_ioctl(struct file *, unsigned int, unsigned long);
int scull_open(struct inode *, struct file *);
int scull_release(struct inode *, struct file *);
#endif
