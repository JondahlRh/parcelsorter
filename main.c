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
void byteToString(char buffer[], int value) {
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

void intArrayToString(char buffer[], int value[], int length) {
  int i;
  for (i = 0; i < length; i++) {
    buffer[i] = value[i] + '0';
  }
  buffer[length] = '\0';
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

/**
 * Get the value of a light barrier at provided bit if it has changed
 */
int getLightBarrier(int bit, int newData, int oldData) {
  if ((newData & bit) == (oldData & bit)) return -1;
  return (newData & bit) ? 1 : 0;
}

// parcel tracking helper functions
void addParcelToTracking(int parcel) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[0] = parcel;
  rt_sem_signal(&semParcelTrackingData);
}

void transferParcelTrackingRegion(int index) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[index] = 0;
  if ((index + 1) < NUMBER_OF_REGIONS) {
    parcelTrackingData[index + 1] = parcelTrackingData[index];
  }
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

int lightBarrierBitToIndexMap[5] = {LIGHT_BARRIER_2, LIGHT_BARRIER_3,
                                    LIGHT_BARRIER_4, LIGHT_BARRIER_5};

RT_TASK rtLightBarrierCheckTask;
void lightBarrierCheckTask(long i) {
  int newValue, oldValue, currentValue, index;
  char buffer[9];

  activate(BELT_1 + BELT_2);

  while (1) {
    newValue = readLightBarriers();

    rt_sem_wait(&semLightBarriersData);
    oldValue = lightBarriersData;
    rt_sem_signal(&semLightBarriersData);

    byteToString(buffer, newValue);
    rt_printk("light barriers new:      %s", buffer);
    byteToString(buffer, lightBarriersData);
    rt_printk("light barriers internal: %s", buffer);
    intArrayToString(buffer, parcelTrackingData, NUMBER_OF_REGIONS);
    rt_printk("parcel tracking data:    %s", buffer);

    currentValue = getLightBarrier(LIGHT_BARRIER_1, newValue, oldValue);
    if (currentValue == 0) {
      // TODO: scanner
      addParcelToTracking(9);

      deactivate(BELT_1);
    } else if (currentValue == 1) {
      activate(BELT_1);
    }

    for (index = 0; index < 5; index++) {
      currentValue =
          getLightBarrier(lightBarrierBitToIndexMap[index], newValue, oldValue);
      if (currentValue == 0) {
        transferParcelTrackingRegion(index * 2 + 1);
      } else if (currentValue == 1) {
        transferParcelTrackingRegion(index * 2 + 2);
      }

      rt_sem_wait(&semLightBarriersData);
      lightBarriersData = newValue;
      rt_sem_signal(&semLightBarriersData);

      rt_sleep(nano2count(10 * 1000 * 1000));
      rt_task_wait_period();
    }
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
