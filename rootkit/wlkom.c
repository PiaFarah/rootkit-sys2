#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>


static int __init wlkom_init(void)
{
    printk(KERN_INFO "wlkom: loaded\n");
    return 0;
}

static void __exit wlkom_exit(void)
{
    printk(KERN_INFO "wlkom: unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WLKOM - Wild Linux Kernel Object Module");
MODULE_AUTHOR("NMT");
