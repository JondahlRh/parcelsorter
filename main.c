#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

static __init int parallel_init(void) {
  rt_printk("__ init __");
  return 0;
}

static __exit void parallel_exit(void) { rt_printk("__ exit __"); }

module_init(parallel_init);
module_exit(parallel_exit);
