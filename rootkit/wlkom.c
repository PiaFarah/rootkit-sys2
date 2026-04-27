#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wlkom");
MODULE_DESCRIPTION("WLKOM - Wild Linux Kernel Object Module");
MODULE_VERSION("1.0");

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
