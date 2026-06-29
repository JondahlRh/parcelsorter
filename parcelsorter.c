#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <rtai.h>
#include <rtai_fifos.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

// address of the rtai module
#define RTAI_ADDRESS 0xC000

// definition for bitmmasks of belts, ejectors and barcode scanner in order
#define NUMBER_OF_BELTS 2
const int BELTS[NUMBER_OF_BELTS] = {0x01, 0x02};
#define NUMBER_OF_EJECTORS 3
const int EJECTORS[NUMBER_OF_EJECTORS] = {0x08, 0x10, 0x20};
const int BARCODE_SCANNER = 0x80;

// definition for bitmmasks of light barriers in order
#define NUMBER_OF_LIGHT_BARRIERS 5
const int LIGHT_BARRIERS[NUMBER_OF_LIGHT_BARRIERS] = {0x01, 0x40, 0x04, 0x08,
                                                      0x10};

// fifo definitions
#define FIFO_SIZE 1024
#define FIFO_NUMBER 2

// rtai bitmuster state
int internalRtaiBitmuster = 0x00;
SEM sem_internalRtaiBitmuster;

// light barrier state
int internalLightBarriersData = 0xFF;
SEM sem_internalLightBarriersData;

// semaphores for triggering tasks
SEM sem_triggerParcelMovement;
SEM sem_triggerParcelEjection;

// parcel tracking data
#define NUMBER_OF_PARCEL_REGIONS 8
int parcelTrackingData[NUMBER_OF_PARCEL_REGIONS];
SEM sem_parcelTrackingData;

/**
 * Helper function to convert a bitmask to a string
 */
void bitmaskToString(char* buffer, int value) {
  int i;
  for (i = 0; i < 8; i++) {
    buffer[7 - i] = ((value >> i) & 1) ? '1' : '0';
  }
  buffer[8] = '\0';
}

/**
 * Helper function to convert a array of integers to a string
 */
void intArrayToString(char* buffer, int* value, int valueLength) {
  int i;
  for (i = 0; i < valueLength; i++) {
    buffer[i] = value[i] + '0';
  }
  buffer[valueLength] = '\0';
}

/**
 * Function to set a new value for the rtai bitmuster
 */
inline void setRtaiBitmuster(int newValue) { outb(newValue, RTAI_ADDRESS); }

/**
 * Function to set all of the provided bits in the rtai bitmuster to 1
 */
void activate(int value) {
  rt_sem_wait(&sem_internalRtaiBitmuster);

  internalRtaiBitmuster = internalRtaiBitmuster | value;
  setRtaiBitmuster(internalRtaiBitmuster);

  rt_sem_signal(&sem_internalRtaiBitmuster);
}

/**
 * Function to set all of the provided bits in the rtai bitmuster to 0
 */
void deactivate(int value) {
  rt_sem_wait(&sem_internalRtaiBitmuster);

  internalRtaiBitmuster = internalRtaiBitmuster & ~value;
  setRtaiBitmuster(internalRtaiBitmuster);

  rt_sem_signal(&sem_internalRtaiBitmuster);
}

/**
 * Function to toggle all of the provided bits in the rtai bitmuster
 */
void toggle(int value) {
  rt_sem_wait(&sem_internalRtaiBitmuster);

  internalRtaiBitmuster = internalRtaiBitmuster ^ value;
  setRtaiBitmuster(internalRtaiBitmuster);

  rt_sem_signal(&sem_internalRtaiBitmuster);
}

/**
 * Function to add a new parcel to the parcel tracking data
 */
void addNewParcel(int ejectionIndex) {
  rt_sem_wait(&sem_parcelTrackingData);

  parcelTrackingData[0] = ejectionIndex;

  rt_sem_signal(&sem_parcelTrackingData);
}

/**
 * Function to read the current state of the light barriers
 */
inline int readLightBarriers(void) { return inb(RTAI_ADDRESS + 4); }

RT_TASK rttask_readAndUpdateLightBarriers;
/**
 * Task: Reads the light barriers and updates the internal state
 */
void task_readAndUpdateLightBarriers(void) {
  int newValue, oldValue;

  while (true) {
    // read the current state of the light barriers
    newValue = readLightBarriers();

    // read und update internal state of light barriers
    rt_sem_wait(&sem_internalLightBarriersData);
    oldValue = internalLightBarriersData;
    internalLightBarriersData = newValue;
    rt_sem_signal(&sem_internalLightBarriersData);

    // if nothing has changed, wait and continue
    if (newValue == oldValue) {
      rt_task_wait_period();
      continue;
    };

    int i;
    for (i = 1; i < NUMBER_OF_LIGHT_BARRIERS; i++) {
      if ((newData & LIGHT_BARRIERS[i]) == (oldData & LIGHT_BARRIERS[i])) {
        continue;
      }

      // TODO: trigger task to move parcel and pass the index of the barrier
      // TODO: that has changed
    }

    rt_task_wait_period();
  }
}

RT_TASK rttask_moveParcel;
/**
 * Task: Move parcel to the next region
 */
void task_moveParcel(void) {}

RT_TASK rttask_checkParcelEjection;
/**
 * Task: Check if a parcel should be ejected
 */
void task_checkParcelEjection(void) {}

RT_TASK rttask_ejectParcel;
/**
 * Task: Eject a parcel if safe
 */
void task_ejectParcel(void) {}

/**
 * Fifo: Read new Scanner Data and set the internal state
 */
void fifo_readScannerData(void) {
  char buffer[FIFO_SIZE], firstChar;
  int fifoReturnValue;

  fifoReturnValue = rtf_get(FIFO_NUMBER, buffer, FIFO_SIZE);
  if (fifoReturnValue == 0) return;

  firstChar = buffer[1];
  if (firstChar > '9' || firstChar < '0') {
    firstChar = '0';
  }

  addNewParcel(firstChar - '0');
}

static __init int parallel_init(void) {
  rt_printk("Parcelsorter: Initializing");

  rt_mount();

  setRtaiBitmuster(0x00);

  rt_task_init(&rttask_readAndUpdateLightBarriers,
               task_readAndUpdateLightBarriers, 0x00, 3000, 4, 0, 0);
  rt_task_init(&rttask_checkParcelEjection, task_checkParcelEjection, 0x00,
               3000, 2, 0, 0);

  rt_task_init(&rttask_moveParcel, task_moveParcel, 0x00, 3000, 3, 0, 0);
  rt_task_init(&rttask_ejectParcel, task_ejectParcel, 0x00, 3000, 1, 0, 0);

  rtf_create(FIFO_NUMBER, FIFO_SIZE);
  rtf_create_handler(FIFO_NUMBER, &fifo_readScannerData);

  rt_typed_sem_init(&sem_internalRtaiBitmuster, 1, RES_SEM);
  rt_typed_sem_init(&sem_internalLightBarriersData, 1, RES_SEM);
  rt_typed_sem_init(&sem_parcelTrackingData, 1, RES_SEM);

  // TODO: RTIME

  rt_set_periodic_mode();
  start_rt_timer(/* .. */);

  rt_task_make_periodic(&rttask_readAndUpdateLightBarriers, /* .. */,
                        /* .. */);
  rt_task_make_periodic(&rttask_checkParcelEjection, /* .. */,
                        /* .. */);

  rt_task_run(&rttask_moveParcel);
  rt_task_run(&rttask_ejectParcel);

  return 0;
}
static __exit void parallel_exit(void) {
  rt_printk("Parcelsorter: Exiting");

  stop_rt_timer();

  setRtaiBitmuster(0x00);

  rtf_destroy(FIFO_NUMBER);

  rt_typed_sem_destroy(&sem_internalRtaiBitmuster);
  rt_typed_sem_destroy(&sem_internalLightBarriersData);
  rt_typed_sem_destroy(&sem_parcelTrackingData);

  rt_task_delete(&rttask_readLightBarriers);
  rt_task_delete(&rttask_moveParcel);
  rt_task_delete(&rttask_checkParcel);
  rt_task_delete(&rttask_ejectParcel);

  rt_umount();
}

module_init(parallel_init);
module_exit(parallel_exit);
