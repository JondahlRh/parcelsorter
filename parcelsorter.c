#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <rtai.h>
#include <rtai_fifos.h>
#include <rtai_mbx.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

// base definitions
#define RT_STACK_SIZE 4096
#define RT_HIGH_PRIORITY 0
#define RT_MEDIUM_PRIORITY 1
#define RT_LOW_PRIORITY 2

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

// parcel tracking data
#define NUMBER_OF_PARCEL_REGIONS 8
int parcelTrackingData[NUMBER_OF_PARCEL_REGIONS];
SEM sem_parcelTrackingData;

// mailboxes
#define MAILBOX_SIZE 1024
MBX mailbox_moveParcel;
MBX mailbox_ejectParcel;

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
void addNewParcelToTrackingData(int scannerValue) {
  int ejectionId;

  switch (scannerValue) {
    case 1:
      ejectionId = 1;
      break;
    case 4:
      ejectionId = 2;
      break;
    case 9:
      ejectionId = 3;
      break;
    default:
      ejectionId = 0;
      break;
  }

  rt_sem_wait(&sem_parcelTrackingData);
  parcelTrackingData[0] = ejectionId;
  rt_sem_signal(&sem_parcelTrackingData);
}

/**
 * Function to reset the parcel tracking data at index
 */
void resetParcelTrackingDataAtIndex(int index) {
  rt_sem_wait(&sem_parcelTrackingData);

  parcelTrackingData[index] = 0;

  rt_sem_signal(&sem_parcelTrackingData);
}

/**
 * Function to check if the parcel at index is safe to eject
 */
bool isEjectionSaveAtIndex(int index) {
  int valueBefore, valueAfter;

  rt_sem_wait(&sem_parcelTrackingData);
  valueBefore = parcelTrackingData[index - 1];
  valueAfter = parcelTrackingData[index + 1];
  rt_sem_signal(&sem_parcelTrackingData);

  return valueBefore == 0 && valueAfter == 0;
}

/**
 * Function to read the current state of the light barriers
 */
inline int readLightBarriers(void) { return inb(RTAI_ADDRESS + 4); }

RT_TASK rttask_readAndUpdateLightBarriers;
/**
 * Task: Reads the light barriers and updates the internal state
 */
void task_readAndUpdateLightBarriers(long i) {
  int newValue, oldValue, parcelTrackingIndex, idx;
  char buffer[9];

  while (true) {
    // read light barriers und update internal state
    // when nothing has changed, wait and continue
    newValue = readLightBarriers();

    rt_sem_wait(&sem_internalLightBarriersData);
    oldValue = internalLightBarriersData;
    internalLightBarriersData = newValue;
    rt_sem_signal(&sem_internalLightBarriersData);

    if (newValue == oldValue) {
      rt_task_wait_period();
      continue;
    };

    // ckeck first light barrier
    if ((newValue & LIGHT_BARRIERS[0]) != (oldValue & LIGHT_BARRIERS[0])) {
      if ((newValue & LIGHT_BARRIERS[0]) == 0) {
        // parcel entered first light barrier
        activate(BARCODE_SCANNER);
        deactivate(BELTS[0]);
      } else {
        // parcel left first light barrier
        deactivate(BARCODE_SCANNER);
        activate(BELTS[1]);
      }
    }

    // check other light barriers
    for (idx = 1; idx < NUMBER_OF_LIGHT_BARRIERS; idx++) {
      if ((newValue & LIGHT_BARRIERS[idx]) ==
          (oldValue & LIGHT_BARRIERS[idx])) {
        // light barrier has not changed, continue to next light barrier
        continue;
      }

      // trigger move parcel task
      parcelTrackingIndex = idx * 2 + (newValue & LIGHT_BARRIERS[idx]) - 1;
      rt_mbx_send(&mailbox_moveParcel, &parcelTrackingIndex, sizeof(int));
    }

    rt_task_wait_period();
  }
}

RT_TASK rttask_moveParcel;
/**
 * Task: Move parcel to the next region
 */
void task_moveParcel(long i) {
  int parcelTrackingIndex, parcelEjectionId;

  while (true) {
    rt_mbx_receive(&mailbox_moveParcel, &parcelTrackingIndex, sizeof(int));

    // get parcel data and move it to the next region
    rt_sem_wait(&sem_parcelTrackingData);
    parcelEjectionId = parcelTrackingData[parcelTrackingIndex];
    if ((parcelTrackingIndex + 1) < NUMBER_OF_PARCEL_REGIONS) {
      parcelTrackingData[parcelTrackingIndex + 1] = parcelEjectionId;
    }
    parcelTrackingData[parcelTrackingIndex] = 0;
    rt_sem_signal(&sem_parcelTrackingData);

    // check if parcel is at a ejection region and it matches the parcel data
    if (parcelTrackingIndex % 2 == 0 &&
        parcelEjectionId == parcelTrackingIndex / 2) {
      // trigger eject parcel task
      rt_mbx_send(&mailbox_ejectParcel, &parcelEjectionId, sizeof(int));
    }
  }
}

RT_TASK rttask_ejectParcel;
/**
 * Task: Eject a parcel if safe
 */
void task_ejectParcel(long i) {
  int parcelEjectionId, maximumWaitIndex;
  bool isEjectionSave;

  while (true) {
    rt_mbx_receive(&mailbox_ejectParcel, &parcelEjectionId, sizeof(int));

    // minimum sleep before ejecting can be safe
    rt_sleep(nano2count(200 * 1000 * 1000));

    // check if ejection is safe
    // maximum wait time is 1.1 seconds, after that ejection will be ignored
    maximumWaitIndex = 11;
    while (maximumWaitIndex > 0) {
      isEjectionSave = isEjectionSaveAtIndex((parcelEjectionId + 1) * 2);
      if (isEjectionSave) break;

      maximumWaitIndex--;
      rt_sleep(nano2count(100 * 1000 * 1000));
      continue;
    }

    if (!isEjectionSave) continue;

    // eject parcel
    activate(EJECTORS[parcelEjectionId - 1]);
    rt_sleep(nano2count(500 * 1000 * 1000));
    deactivate(EJECTORS[parcelEjectionId - 1]);
  }
}

/**
 * Fifo: Read new Scanner Data and set the internal state
 */
int fifo_readScannerData(int i) {
  char buffer[FIFO_SIZE], firstChar;
  int fifoReturnValue;

  fifoReturnValue = rtf_get(FIFO_NUMBER, buffer, FIFO_SIZE);
  if (fifoReturnValue == 0) return 0;

  // set invalid values to 0
  firstChar = buffer[1];
  if (firstChar > '9' || firstChar < '0') {
    firstChar = '0';
  }

  // add new parcel to tracking data (as an int)
  addNewParcelToTrackingData(firstChar - '0');
  return 0;
}

RTIME t_baseTimer, t_startLightBarrierTask, t_timerLightBarrierTask;
static __init int parallel_init(void) {
  rt_printk("Parcelsorter: Initializing");

  rt_mount();

  setRtaiBitmuster(0x00);
  activate(BELTS[0] + BELTS[1]);

  rt_task_init(
      &rttask_readAndUpdateLightBarriers, task_readAndUpdateLightBarriers, 0,
      RT_STACK_SIZE,
      RT_HIGH_PRIORITY,  // highest priority to check for light barrier changes
      0, 0);
  rt_task_init(
      &rttask_moveParcel, task_moveParcel, 0, RT_STACK_SIZE,
      RT_MEDIUM_PRIORITY,  // medium priority to move parcels in internal state
      0, 0);
  rt_task_init(
      &rttask_ejectParcel, task_ejectParcel, 0, RT_STACK_SIZE,
      RT_LOW_PRIORITY,  // lowest  priority to eject parcels from the belts
      0, 0);

  rtf_create(FIFO_NUMBER, FIFO_SIZE);
  rtf_create_handler(FIFO_NUMBER, &fifo_readScannerData);

  rt_typed_sem_init(&sem_internalRtaiBitmuster, 1, RES_SEM);
  rt_typed_sem_init(&sem_internalLightBarriersData, 1, RES_SEM);
  rt_typed_sem_init(&sem_parcelTrackingData, 1, RES_SEM);

  rt_typed_mbx_init(&mailbox_moveParcel, MAILBOX_SIZE, FIFO_Q);
  rt_typed_mbx_init(&mailbox_ejectParcel, MAILBOX_SIZE, FIFO_Q);

  t_baseTimer = nano2count(100 * 1000 * 1000);

  rt_set_periodic_mode();
  start_rt_timer(t_baseTimer);

  t_startLightBarrierTask = rt_get_time() + nano2count(500 * 1000 * 1000);
  t_timerLightBarrierTask = nano2count(500 * 1000 * 1000);

  rt_task_make_periodic(&rttask_readAndUpdateLightBarriers,
                        t_startLightBarrierTask, t_timerLightBarrierTask);
  rt_task_resume(&rttask_moveParcel);
  rt_task_resume(&rttask_ejectParcel);

  return 0;
}
static __exit void parallel_exit(void) {
  rt_printk("Parcelsorter: Exiting");

  stop_rt_timer();

  setRtaiBitmuster(0x00);

  rtf_destroy(FIFO_NUMBER);

  rt_sem_delete(&sem_internalRtaiBitmuster);
  rt_sem_delete(&sem_internalLightBarriersData);
  rt_sem_delete(&sem_parcelTrackingData);

  rt_mbx_delete(&mailbox_moveParcel);
  rt_mbx_delete(&mailbox_ejectParcel);

  rt_task_delete(&rttask_readAndUpdateLightBarriers);
  rt_task_delete(&rttask_moveParcel);
  rt_task_delete(&rttask_ejectParcel);

  rt_umount();
}

module_init(parallel_init);
module_exit(parallel_exit);
