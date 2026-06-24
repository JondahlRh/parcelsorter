#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <rtai.h>
#include <rtai_fifos.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

//! TMP
int countingNumber = 0;

// rtai definitions and variables
#define RTAI_ADDRESS 0xC000
int rtaiBitmuster = 0x00;
SEM semRtaiBitmuster;

#define BELT_1 0x01
#define BELT_2 0x02
#define EJECTOR_1 0x08
#define EJECTOR_2 0x10
#define EJECTOR_3 0x20
#define BARCODE_SCANNER 0x80

#define NUMBER_OF_EJECTORS 3
int ejectorsIndexMap[NUMBER_OF_EJECTORS] = {EJECTOR_1, EJECTOR_2, EJECTOR_3};

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

int lightBarriersData = 0xff;
SEM semLightBarriersData;

#define NUMBER_OF_LIGHT_BARRIERS 5
int lightBarriersIndexMap[NUMBER_OF_LIGHT_BARRIERS] = {
    LIGHT_BARRIER_2, LIGHT_BARRIER_3, LIGHT_BARRIER_4, LIGHT_BARRIER_5};

// fifo definitions and variables
#define FIFO_SIZE 1024
#define FIFO_NUMBER 2
int fifoValue;
SEM semFifoValue;

/**
 * Convert a byte to a string (for debugging purposes)
 */
void byteToString(char buffer[], int value) {
  int i;
  for (i = 0; i < 8; i++) {
    buffer[7 - i] = ((value >> i) & 1) ? '1' : '0';
  }
  buffer[8] = '\0';
}

/**
 * Convert an integer array to a string (for debugging purposes)
 */
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
 * - -1: has not change
 * - 0: changed and now active
 * - 1: changed and now inactive
 */
int getLightBarrierValueIfChanged(int bit, int newData, int oldData) {
  if ((newData & bit) == (oldData & bit)) return -1;
  return (newData & bit) ? 1 : 0;
}

/**
 * Add parcel at first position with the id of the exjection position
 * @param parcel: id of the exjection position
 */
void addParcelToTracking(int parcel) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[0] = parcel;
  rt_sem_signal(&semParcelTrackingData);
}

/**
 * Get parcel tracking data at provided index
 */
int getParcelTrackingDataAtIndex(int index) {
  int output;

  rt_sem_wait(&semParcelTrackingData);
  output = parcelTrackingData[index];
  rt_sem_signal(&semParcelTrackingData);

  return output;
}

/**
 * Reset parcel tracking data at provided index
 */
void resetParcelTrackingDataAtIndex(int index) {
  rt_sem_wait(&semParcelTrackingData);
  parcelTrackingData[index] = 0;
  rt_sem_signal(&semParcelTrackingData);
}

/**
 * Transfer parcel tracking data from one region to the next
 */
void transferParcelTrackingRegion(int index) {
  rt_sem_wait(&semParcelTrackingData);
  if ((index + 1) < NUMBER_OF_REGIONS) {
    parcelTrackingData[index + 1] = parcelTrackingData[index];
  }
  parcelTrackingData[index] = 0;
  rt_sem_signal(&semParcelTrackingData);
}

/**
 * Check if ejection save at provided index (check index before and after)
 */
bool isEjectionSaveAtIndex(int index) {
  int valueBefore, valueAfter;

  rt_sem_wait(&semParcelTrackingData);
  valueBefore = parcelTrackingData[index - 1];
  valueAfter = parcelTrackingData[index + 1];
  rt_sem_signal(&semParcelTrackingData);

  return valueBefore == 0 && valueAfter == 0;
}

/**
 * Get barcode value
 */
int getBarcodeValue(void) {
  int value;

  rt_sem_wait(&semFifoValue);
  value = fifoValue;
  rt_sem_signal(&semFifoValue);

  return value;
}

/**
 * Reset barcode value to 0
 */
void resetBarcodeValue(void) {
  rt_sem_wait(&semFifoValue);
  fifoValue = 0;
  rt_sem_signal(&semFifoValue);
}

/**
 * Map barcode value to ejector id
 */
int mapBarcodeValueToEjector(int value) {
  if (value == 1) return 1;
  if (value == 4) return 2;
  if (value == 9) return 3;
  return 0;
}

// light barrier task:
// check if light barriers have changed and transfer parcel tracking data
RT_TASK rtLightBarrierTask;
void lightBarrierTask(long i) {
  int newValue, oldValue, currentValue, index;
  char buffer[9];

  activate(BELT_1 + BELT_2);

  while (1) {
    // get new light barrier data
    newValue = readLightBarriers();

    // get internal (old) light barrier data
    rt_sem_wait(&semLightBarriersData);
    oldValue = lightBarriersData;
    rt_sem_signal(&semLightBarriersData);

    // debugging
    byteToString(buffer, newValue);
    rt_printk("light barriers new:      %s", buffer);
    byteToString(buffer, lightBarriersData);
    rt_printk("light barriers internal: %s", buffer);
    intArrayToString(buffer, parcelTrackingData, NUMBER_OF_REGIONS);
    rt_printk("parcel tracking data:    %s", buffer);

    // if nothing has changed, wait and continue
    if (newValue == oldValue) {
      rt_task_wait_period();
      continue;
    }

    // check if light barrier 1 has changed
    currentValue =
        getLightBarrierValueIfChanged(LIGHT_BARRIER_1, newValue, oldValue);
    if (currentValue == 0) {
      activate(BARCODE_SCANNER);
      deactivate(BELT_1);

      while (getBarcodeValue() == 0) {
        rt_sleep(nano2count(1000 * 1000));
      }

      addParcelToTracking(mapBarcodeValueToEjector(getBarcodeValue()));
    } else if (currentValue == 1) {
      deactivate(BARCODE_SCANNER);
      resetBarcodeValue();
      activate(BELT_1);
    }

    // check if light barriers (after first) have changed
    for (index = 0; index < NUMBER_OF_LIGHT_BARRIERS; index++) {
      currentValue = getLightBarrierValueIfChanged(lightBarriersIndexMap[index],
                                                   newValue, oldValue);
      if (currentValue == -1) continue;

      transferParcelTrackingRegion(index * 2 + currentValue);
    }

    // update internal light barrier data
    rt_sem_wait(&semLightBarriersData);
    lightBarriersData = newValue;
    rt_sem_signal(&semLightBarriersData);
  }
}

// ejection task:
// for each ejector, get parcel tracking data at corresponding index and check
// if it matches the ejector id
RT_TASK rtEjectionTask;
void ejectionTask(long i) {
  int index, parcelData;

  while (1) {
    // loop all ejectors and eject parcel data matches ejector id
    for (index = 0; index < NUMBER_OF_EJECTORS; index++) {
      parcelData = getParcelTrackingDataAtIndex((index + 1) * 2);
      if (parcelData != (index + 1)) continue;

      rt_printk("ejection %d...", index + 1);
      resetParcelTrackingDataAtIndex((index + 1) * 2);

      rt_sleep(nano2count(100 * 1000 * 1000));

      while (!isEjectionSaveAtIndex((index + 1) * 2)) {
        rt_sleep(nano2count(1000 * 1000));
      }

      activate(ejectorsIndexMap[index]);
      rt_sleep(nano2count(500 * 1000 * 1000));
      deactivate(ejectorsIndexMap[index]);
    }

    rt_task_wait_period();
  }
}

// fifo handler
void fifoHandler(int i) {
  char buffer[FIFO_SIZE], firstChar;
  int returnValue, output;

  returnValue = rtf_get(FIFO_NUMBER, buffer, FIFO_SIZE);
  if (returnValue == 0) {
    rt_printk("errorcode: %d", returnValue);
    return 0;
  }

  firstChar = buffer[0];
  if (firstChar > '9' || firstChar < '0') {
    firstChar = '0';
  }

  // update fifo value
  rt_sem_wait(&semFifoValue);
  fifoValue = firstChar - '0';
  rt_sem_signal(&semFifoValue);

  return 0;
}

static __init int parallel_init(void) {
  rt_mount();

  // TODO: priorities
  rt_task_init(&rtLightBarrierTask, lightBarrierTask, 0x00, 3000, 4, 0, 0);
  rt_task_init(&rtEjectionTask, ejectionTask, 0x00, 3000, 4, 0, 0);

  rtf_create(FIFO_NUMBER, FIFO_SIZE);
  rtf_create_handler(FIFO_NUMBER, &fifoHandler);

  rt_typed_sem_init(&semRtaiBitmuster, 1, RES_SEM);
  rt_typed_sem_init(&semParcelTrackingData, 1, RES_SEM);
  rt_typed_sem_init(&semLightBarriersData, 1, RES_SEM);

  RTIME timer;
  timer = nano2count(100 * 1000 * 1000);

  rt_set_periodic_mode();
  start_rt_timer(timer);

  RTIME tstart;
  tstart = rt_get_time() + nano2count(100 * 1000 * 1000);

  rt_task_make_periodic(&rtLightBarrierTask, tstart, timer);
  rt_task_make_periodic(&rtEjectionTask, tstart, timer);

  rt_printk("__ init __");
  return 0;
}

static __exit void parallel_exit(void) {
  stop_rt_timer();

  setRtaiBitmuster(0x0);

  rtf_destroy(FIFO_NUMBER);

  rt_sem_delete(&semRtaiBitmuster);
  rt_sem_delete(&semParcelTrackingData);
  rt_sem_delete(&semLightBarriersData);

  rt_task_delete(&rtLightBarrierTask);
  rt_task_delete(&rtEjectionTask);

  rt_umount();

  rt_printk("__ exit __");
}

module_init(parallel_init);
module_exit(parallel_exit);
