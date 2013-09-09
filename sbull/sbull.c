#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>

#define SBULL_NAME "sbull"
#define SBULL_MINORS 4
#define SBULL_SEC_SIZE 512
#define SBULL_SEC_NUM (200*1024)
#define KERNEL_SECTOR_SIZE 512

struct sbull_dev {
	int major;
	struct gendisk *sbull_gd;
	struct request_queue *sbull_request_queue;
	spinlock_t sbull_lock;
	u8 *data;
	size_t size;
}s_dev;

static int sbull_open(struct block_device *blk, fmode_t mode)
{
	return 0;
}

static void sbull_release(struct gendisk *gd, fmode_t mode)
{
}

static int sbull_ioctl(struct block_device *blk, fmode_t mode, 
		unsigned int cmd, unsigned long arg)
{
	/*no such cmd*/
	return -ENOTTY;
}

static void sbull_transfer(struct request *req)
{
	unsigned long offset = blk_rq_pos(req) * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = blk_rq_bytes(req) * KERNEL_SECTOR_SIZE;

	if((offset + nbytes) > (s_dev.size * SBULL_SEC_SIZE))
	{
		pr_notice("%s exceed range request, ignore...\n", SBULL_NAME);
		return;
	}

	//printk(KERN_INFO "req->buffer: %x\n", req->buffer);
	if(rq_data_dir(req)) /*copy to disk*/
		memcpy(s_dev.data + offset, req->buffer, nbytes);
	else /*copy from disk*/
		memcpy(req->buffer, s_dev.data + offset, nbytes);
}

static void sbull_xfer_bio(struct bio *bio)
{
	int i;
	char * buffer;
	struct bio_vec *bvec;
	unsigned long nbytes, offset = bio->bi_sector * KERNEL_SECTOR_SIZE;

	bio_for_each_segment(bvec, bio, i)
	{
		buffer = __bio_kmap_atomic(bio, i, KM_USER);
		nbytes = bio_cur_bytes(bio);
		if((offset + nbytes) > (s_dev.size * SBULL_SEC_SIZE))
		{
			pr_notice("%s exceed range request, ignore...\n", SBULL_NAME);
			return;
		}
		if(bio_data_dir(bio)) /*copy to disk*/
			memcpy(s_dev.data + offset, buffer, nbytes);
		else /*copy from disk*/
			memcpy(buffer, s_dev.data + offset, nbytes);
		offset += nbytes;
		__bio_kunmap_atomic(bio, KM_USER);
	}
}

static size_t sbull_xfer_request(struct request *req)
{
	struct bio *bio;
	size_t retval = 0;

	__rq_for_each_bio(bio, req)
	{
		sbull_xfer_bio(bio);
		retval += bio->bi_size;
	}
	return retval;
}

static void sbull_request(struct request_queue *queue)
{
	struct request *req;
	size_t bytes;

	req = blk_fetch_request(queue);
	while(req != NULL)
	{
		/*check if the request is fs type*/
		if(req->cmd_type != REQ_TYPE_FS)
		{
			pr_notice("%s skip non-fs request\n", SBULL_NAME);
			__blk_end_request_all(req, -EIO);
			continue;
		}
		bytes = sbull_xfer_request(req);
		if(!__blk_end_request(req, 0, bytes))
			req = blk_fetch_request(queue);
	}
}

struct block_device_operations sbull_blk_ops = {
	.open = sbull_open,
	.release = sbull_release,
	.ioctl = sbull_ioctl,
	.owner = THIS_MODULE,
};

static int __init sbull_init(void)
{
	int retval = 0;

	if((s_dev.major = register_blkdev(0, SBULL_NAME)) < 0)
	{
		pr_err("%s register block device failed\n", SBULL_NAME);
		return -EBUSY;
	}

	s_dev.size = SBULL_SEC_NUM;

	/*initialize request queue*/
	spin_lock_init(&s_dev.sbull_lock);
	s_dev.sbull_request_queue = 
		blk_init_queue(sbull_request, &s_dev.sbull_lock);
	if(!s_dev.sbull_request_queue)
	{
		pr_crit("%s initialize request queue failed\n", SBULL_NAME);
		retval = -EFAULT;
		goto err;
	}

	/*populate struct gendisk*/
	s_dev.sbull_gd = alloc_disk(SBULL_MINORS);
	if(!s_dev.sbull_gd)
	{
		pr_err("%s populate gendisk failed\n", SBULL_NAME);
		retval = -EFAULT;
		goto err;
	}
	s_dev.sbull_gd->major = s_dev.major;
	s_dev.sbull_gd->first_minor = 0;
	s_dev.sbull_gd->fops = &sbull_blk_ops;
	s_dev.sbull_gd->queue = s_dev.sbull_request_queue;
	s_dev.sbull_gd->private_data = &s_dev;
	sprintf(s_dev.sbull_gd->disk_name, "sbull");

	/*allocate memory*/
	s_dev.data = vmalloc(s_dev.size * SBULL_SEC_SIZE);
	if(!s_dev.data)
	{
		pr_err("%s allocate memory %lu failed\n", 
				SBULL_NAME, (unsigned long)s_dev.size * SBULL_SEC_SIZE);
		retval = -ENOMEM;
		goto err;
	}

	/*set device size*/
	set_capacity(s_dev.sbull_gd, 
			s_dev.size * (SBULL_SEC_SIZE / KERNEL_SECTOR_SIZE));
	pr_info("%s device initialized, capacity: %lu\n", 
			SBULL_NAME, (unsigned long)s_dev.size * SBULL_SEC_SIZE);

	add_disk(s_dev.sbull_gd);
	goto out;
err:
	unregister_blkdev(s_dev.major, SBULL_NAME);
out:
	return retval;
}

static void __exit sbull_exit(void)
{
	vfree(s_dev.data);
	del_gendisk(s_dev.sbull_gd);
	unregister_blkdev(s_dev.major, SBULL_NAME);
}

module_init(sbull_init);
module_exit(sbull_exit);

MODULE_AUTHOR("WEN Pingbo <wpb@meizu.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("a block device in memory");
MODULE_SUPPORTED_DEVICE("sbull");

