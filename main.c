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
#define NUMBER_OF_REGIONS 7
int parcelTrackingData[NUMBER_OF_REGIONS];
SEM semParcelTrackingData;

// light barrier definitions and variables
int lightBarriersData = 0xff;
SEM semLightBarriersData;

#define LIGHT_BARRIER_1 0x01
#define LIGHT_BARRIER_2 0x40
#define LIGHT_BARRIER_3 0x04
#define LIGHT_BARRIER_4 0x08
#define LIGHT_BARRIER_5 0x10

// general helper functions
/**
 * Convert a binary number to a string (for debugging purposes)
 */
void bitmusterToString(char buffer[], int value) {
  int i;
  for (i = 0; i < 8; i++) {
    if ((value >> i) & 1) {
      buffer[7 - i] = '1';
    } else {
      buffer[7 - i] = '0';
    }
  }

  buffer[8] = '\0';
}

// rtai helper functions
inline void setRtaiBitmuster(int newValue) { outb(newValue, RTAI_ADDRESS); }

void activate(int value) {
  rt_sem_wait(&semRtaiBitmuster);

  rtaiBitmuster = rtaiBitmuster | value;
  setRtaiBitmuster(rtaiBitmuster);

  rt_sem_signal(&semRtaiBitmuster);
}

void deactivate(int value) {
  rt_sem_wait(&semRtaiBitmuster);

  rtaiBitmuster = rtaiBitmuster & ~value;
  setRtaiBitmuster(rtaiBitmuster);

  rt_sem_signal(&semRtaiBitmuster);
}

void toggle(int value) {
  rt_sem_wait(&semRtaiBitmuster);

  rtaiBitmuster = rtaiBitmuster ^ value;
  setRtaiBitmuster(rtaiBitmuster);

  rt_sem_signal(&semRtaiBitmuster);
}

// light barrier helper functions
inline int readLightBarriers(void) { return inb(RTAI_ADDRESS + 4); }

// parcel tracking helper functions
void addParcelToTracking(int parcel) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[0] = parcel;
  rt_sem_signal(&semParcelTrackingData);
}

void initParcelTracking(void) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[0] = 0;
  rt_sem_signal(&semParcelTrackingData);
}

void removeParcelFromTracking(void) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[NUMBER_OF_REGIONS - 1] = 0;
  rt_sem_signal(&semParcelTrackingData);
}

void transferParcelTrackingRegion(int index) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[index + 1] = parcelTrackingData[index];
  rt_sem_signal(&semParcelTrackingData);
}

// test task
// RT_TASK rtTestTask;
// void testTask(long i) {
//   while (1) {
//     rt_printk("test task running...");
//     rt_sleep(nano2count(1000 * 1000 * 1000));
//     rt_task_wait_period();
//   }
// }

RT_TASK rtLightBarrierCheckTask;
void lightBarrierCheckTask(long i) {
  int newValue, singleNewValue, singleOldValue;
  char buffer[9];

  activate(BELT_1 + BELT_2);

  while (1) {
    newValue = readLightBarriers();

    bitmusterToString(buffer, newValue);
    rt_printk("light barriers new:      %s", buffer);
    bitmusterToString(buffer, lightBarriersData);
    rt_printk("light barriers internal: %s", buffer);

    rt_printk("parcel tracking data:    %d%d%d%d%d%d%d", parcelTrackingData[0],
              parcelTrackingData[1], parcelTrackingData[2],
              parcelTrackingData[3], parcelTrackingData[4],
              parcelTrackingData[5], parcelTrackingData[6]);

    rt_sem_wait(&semLightBarriersData);

    singleNewValue = newValue & LIGHT_BARRIER_1;
    singleOldValue = lightBarriersData & LIGHT_BARRIER_1;
    if (singleNewValue != singleOldValue) {
      // rt_printk("light barrier 1 changed");

      if (singleNewValue == 0) {
        addParcelToTracking(9);
        deactivate(BELT_1);
      }
    }

    singleNewValue = newValue & LIGHT_BARRIER_2;
    singleOldValue = lightBarriersData & LIGHT_BARRIER_2;
    if (singleNewValue != singleOldValue) {
      // rt_printk("light barrier 2 changed %d", singleNewValue);

      if (singleNewValue == 0) {
        transferParcelTrackingRegion(0);
      } else {
        transferParcelTrackingRegion(1);
        activate(BELT_1);
      }
    }

    singleNewValue = newValue & LIGHT_BARRIER_3;
    singleOldValue = lightBarriersData & LIGHT_BARRIER_3;
    if (singleNewValue != singleOldValue) {
      // rt_printk("light barrier 3 changed");

      if (singleNewValue == 0) {
        transferParcelTrackingRegion(2);
      } else {
        transferParcelTrackingRegion(3);
      }
    }

    singleNewValue = newValue & LIGHT_BARRIER_4;
    singleOldValue = lightBarriersData & LIGHT_BARRIER_4;
    if (singleNewValue != singleOldValue) {
      // rt_printk("light barrier 4 changed");

      if (singleNewValue == 0) {
        transferParcelTrackingRegion(4);
      } else {
        transferParcelTrackingRegion(5);
      }
    }

    singleNewValue = newValue & LIGHT_BARRIER_5;
    singleOldValue = lightBarriersData & LIGHT_BARRIER_5;
    if (singleNewValue != singleOldValue) {
      // rt_printk("light barrier 5 changed");

      if (singleNewValue == 0) {
        transferParcelTrackingRegion(6);
      } else {
        removeParcelFromTracking();
      }
    }

    lightBarriersData = newValue;

    rt_sem_signal(&semLightBarriersData);

    rt_sleep(nano2count(10 * 1000 * 1000));
    rt_task_wait_period();
  }
}

static __init int parallel_init(void) {
  rt_mount();

  rt_task_init(&rtLightBarrierCheckTask, lightBarrierCheckTask, 0x00, 3000, 4,
               0, 0);
  // rt_task_init(&rtTestTask, testTask, 0x00, 3000, 4, 0, 0);

  rt_typed_sem_init(&semRtaiBitmuster, 1, RES_SEM);
  rt_typed_sem_init(&semParcelTrackingData, 1, RES_SEM);
  rt_typed_sem_init(&semLightBarriersData, 1, RES_SEM);

  rt_set_periodic_mode();
  start_rt_timer(0);

  RTIME tstart = rt_get_time() + nano2count(10 * 1000 * 1000);
  rt_task_make_periodic(&rtLightBarrierCheckTask, tstart,
                        nano2count(240000000));
  // rt_task_make_periodic(&rtTestTask, tstart, nano2count(240000000));

  rt_printk("__ init __");
  return 0;
}

static __exit void parallel_exit(void) {
  stop_rt_timer();

  setRtaiBitmuster(0x0);

  rt_sem_delete(&semRtaiBitmuster);
  rt_sem_delete(&semParcelTrackingData);
  rt_sem_delete(&semLightBarriersData);

  rt_task_delete(&rtLightBarrierCheckTask);
  // rt_task_delete(&rtTestTask);

  rt_umount();

  rt_printk("__ exit __");
}

module_init(parallel_init);
module_exit(parallel_exit);
