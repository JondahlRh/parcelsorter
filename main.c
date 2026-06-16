#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

// rtai definitions and variables
#define RTAI_ADDRESS 0xC000
int rtaiBitmuster = 0x00;
SEM semRtaiBitmuster;

#define BELT_1 0x01
#define BELT_2 0x02
#define EJECTOR_1 0x04
#define EJECTOR_2 0x08
#define EJECTOR_3 0x10
#define BARCODE_SCANNER 0x80

// parcel tracking definitions and variables
#define NUMBER_OF_REGIONS 8
int parcelTrackingData[NUMBER_OF_REGIONS];
SEM semParcelTrackingData;

// light barrier definitions and variables
#define LIGHT_BARRIER_1 0x01
#define LIGHT_BARRIER_2 0x40
#define LIGHT_BARRIER_3 0x04
#define LIGHT_BARRIER_4 0x08
#define LIGHT_BARRIER_5 0x10

static __init int parallel_init(void) {
  rt_printk("__ init __");
  return 0;
}

static __exit void parallel_exit(void) { rt_printk("__ exit __"); }

module_init(parallel_init);
module_exit(parallel_exit);
