#include "scull.h"

static struct scull_dev s_dev;

static DECLARE_WAIT_QUEUE_HEAD(read_sleep);

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.llseek = scull_llseek,
	.read = scull_read,
	.write = scull_write,
	.unlocked_ioctl = scull_unlocked_ioctl,
	.open = scull_open, 
	.release = scull_release
};

/*
 * initial cdev and add scull to kernel
 */
static void scull_setup_cdev(struct scull_dev *s_dev, int index)
{
	int err, devno = MKDEV(scull_major, scull_minor + index);

	cdev_init(&s_dev->scull_cdev, &scull_fops);
	s_dev->scull_cdev.owner = THIS_MODULE;
	s_dev->scull_cdev.ops = &scull_fops;
	err = cdev_add (&s_dev->scull_cdev, devno, 1);
	
	if(err)
		printk(KERN_NOTICE "Error %d adding scull %d\n", err, index);
}

static void scull_reset_buffer(void)
{
	struct data_node *temp;
    struct list_head *cursor;
	list_for_each(cursor, &s_dev.data_header.list)
	{
		temp = list_entry(cursor, struct data_node, list);
		if(temp != &s_dev.data_header)
		{
			free_page((unsigned long)temp->data);
			kfree(temp);
		}
	}

	s_dev.cur_size = 0;
	s_dev.page_count = 0;
}

static int __init scull_init(void)
{
	int result, dev;
	/*get the major and minor number of scull device*/
	if(scull_major) {
		dev = MKDEV(scull_major,scull_minor);
		result =register_chrdev_region(dev, scull_count, DEVICE_NAME);
	}
	else {
		result=alloc_chrdev_region(&dev, scull_minor, scull_count, DEVICE_NAME);
		scull_major = MAJOR(dev);
	}
	if(result < 0)
	{
		printk(KERN_WARNING "%s: alloc device region failed\n", \
			   DEVICE_NAME);
		return result;
	}

	INIT_LIST_HEAD(&s_dev.data_header.list);
	s_dev.data_header.data = (void *)get_zeroed_page(GFP_KERNEL);
	s_dev.page_size = PAGE_SIZE;

	s_dev.cur_size = 0;
	s_dev.page_count = 0;

	scull_setup_cdev(&s_dev, 0);

	/*register a read-only entry in proc*/
	proc_create("scull", 0, NULL, &scull_fops);
		
	return 0;
}

static void __exit scull_exit(void)
{
	int dev=MKDEV(scull_major, scull_minor);

	/*free buffer*/
	scull_reset_buffer();
	free_page((unsigned long)s_dev.data_header.data);

	/*del proc entry*/
	remove_proc_entry("scull", NULL);

	cdev_del(&s_dev.scull_cdev);
	unregister_chrdev_region(dev, scull_count);
}

static inline struct list_head *create_page(void)
{
	struct data_node *temp = 
		(struct data_node *)kmalloc(sizeof(struct data_node),
				GFP_KERNEL);
	if(temp)
		temp->data = (void *)get_zeroed_page(GFP_KERNEL);
	else
		return NULL;
	return &temp->list;
}

static struct list_head *get_write_page(struct list_head *header, 
		int pages)
{
	struct list_head *temp;
	if(pages > s_dev.page_count)
	{
		temp = create_page();
		if(temp)
		{
			list_add_tail(temp, header);
			s_dev.page_count++;
		}
		else 
			return NULL;
	}

	return header->prev;
}

static struct list_head *get_read_page(struct list_head *header, 
		int pages)
{
	int temp;
	struct list_head *retval=header;

	if(pages > s_dev.page_count)
	{
		printk(KERN_WARNING "wrong page index\n");
		return NULL;
	}

	for(temp=0; temp<pages; temp++)
		retval = retval->next;

	return retval;
}

ssize_t scull_read(struct file *fp, char __user *data, 
		size_t size, loff_t *offset)
{
	size_t copy_size, pages, page_pos;
	char *_data;
	struct list_head *page;
	struct data_node *temp;

	if(!data)
		return -EINVAL;

	wait_event_interruptible(read_sleep, s_dev.cur_size !=0);

	if(*offset > s_dev.cur_size)
		return 0;

	/*calculate the offset location*/
	pages = (long)*offset / s_dev.page_size;
	page_pos = (long)*offset % s_dev.page_size;

	page = get_read_page(&s_dev.data_header.list, pages);
	if(!page)
		return -EFAULT;

	temp = list_entry(page, struct data_node, list);
	_data = (char *)temp->data + page_pos;

	copy_size = s_dev.page_size - page_pos;
	copy_size = size > copy_size ? copy_size : size;

	if(copy_to_user(data, _data, copy_size))
		printk(KERN_INFO "read data truncated\n");
	
	*offset += copy_size;

	return copy_size;
}

loff_t scull_llseek(struct file *fp, loff_t offset, int count)
{
  return 0;
}

ssize_t scull_write(struct file *fp, const char __user *data, 
		size_t size, loff_t * offset)
{
	size_t copy_size, pages, page_pos;
	char *_data;
	struct list_head *page;
	struct data_node *temp;

	if(!data)
		return -EINVAL;

	/*reset cur_size when a new write begin*/
	if(*offset == 0)
		scull_reset_buffer();

	/*calculate the offset*/
	pages = (long)*offset / s_dev.page_size;
	page_pos = (long)*offset % s_dev.page_size;

	page = get_write_page(&s_dev.data_header.list, pages);

	if(!page)
		return -ENOMEM;

	temp = list_entry(page, struct data_node, list);
	_data = (char *)temp->data + page_pos;

	copy_size = s_dev.page_size - page_pos;
	copy_size = size > copy_size ? copy_size : size;

	if(copy_from_user(_data, data, copy_size))
		printk(KERN_INFO "write data truncated\n");

	*offset += copy_size;
	s_dev.cur_size += copy_size;
	wake_up_interruptible(&read_sleep);

   	return copy_size;
}

long scull_unlocked_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int *temp = (int *)arg;
	/*check cmd*/
	if(_IOC_TYPE(cmd) != SCULL_MAGIC)
		return -ENOTTY;

	switch(cmd)
	{
	case SCULL_GET_MAJOR:
		if(put_user(scull_major, temp))
			return -EFAULT;
		else
			return scull_major;
	case SCULL_GET_MINOR:
		if(put_user(scull_minor, temp))
			return -EFAULT;
		else
			return scull_minor;
	case SCULL_SET_MAJOR:
		if(!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if(get_user(scull_major, temp))
			return -EFAULT;
		else
			return scull_major;
	case SCULL_SET_MINOR:
		if(!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if(get_user(scull_minor, temp))
			return -EFAULT;
		else
			return scull_minor;
	default:
			return -ENOTTY;
	}
}

int scull_open(struct inode *node, struct file *fp)
{
  return 0;
}

int scull_release(struct inode *node, struct file *fp)
{
  return 0;
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("WEN Pingbo <wpb@meizu.com>");
MODULE_DESCRIPTION("a char driver in memory");
MODULE_SUPPORTED_DEVICE("scull");

module_init(scull_init);
module_exit(scull_exit);
