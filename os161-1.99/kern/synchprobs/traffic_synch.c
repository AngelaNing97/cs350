#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct lock *intersection;
static struct cv *northcv, *southcv, *eastcv, *westcv;
volatile int intersectionCount;
volatile int waits[4];
volatile unsigned int signal;


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  intersectionCount = 0;
  signal = 4;
  for (int i = 0; i < 4; i++) {
    waits[i] = 0;
  }

  intersection = lock_create("intersection");
  eastcv = cv_create("eastcv");
  southcv = cv_create("southcv");
  westcv = cv_create("westcv");
  northcv = cv_create("northcv");


  if (intersection == NULL || eastcv == NULL || westcv == NULL || southcv == NULL || northcv == NULL) {
    panic("could not create cv or locks");
  }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersection != NULL);
  KASSERT(eastcv != NULL);
  KASSERT(westcv != NULL);
  KASSERT(northcv != NULL);
  KASSERT(southcv != NULL);

  lock_destroy(intersection);
  cv_destroy(eastcv);
  cv_destroy(westcv);
  cv_destroy(southcv);
  cv_destroy(northcv);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination;  //avoid compiler complaint about unused parameter 
  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);
  lock_acquire(intersection);
  if (signal == 4) { //no one is waiting or in the intersection
    intersectionCount++;
    signal = origin;
  } else if (signal == origin) {
    intersectionCount++;
  } else if (origin == (signal+1)%4 && destination == signal) {
    intersectionCount++;
  } else {
    waits[origin]++;
    if (origin == 0) {
      cv_wait(northcv, intersection);
    } else if (origin == 1) {
      cv_wait(eastcv, intersection);
    } else if (origin == 2) {
      cv_wait(southcv, intersection);
    } else if (origin == 3) {
      cv_wait(westcv, intersection);
    }
    waits[origin]--;
    intersectionCount++;
  }
  lock_release(intersection);
}

/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  (void) origin;
  (void)destination;  //avoid compiler complaint about unused parameter 

  lock_acquire(intersection);
  intersectionCount--;
  int currentWait = 0;
  int index = -1; //the index with maximum waits

  for (int i = 0; i < 4; i++) {
    if (waits[i] > currentWait) {
      currentWait = waits[i];
      index = i;
    }
  }
  // switch the lights if the intersection is now clear
  if (intersectionCount == 0) {
    if (index < 0) { //no one is waiting
      signal = 4;
    } else {
      int nextdir = (signal+1)%4;
      if (waits[nextdir] > 0) {
        signal = nextdir;
      } else {
        signal = index;
      }

      if (signal == 0) {
        cv_broadcast(northcv, intersection);
      } else if (signal == 1) {
        cv_broadcast(eastcv, intersection);
      }  else if (signal == 2) {
        cv_broadcast(southcv, intersection);
      }  else if (signal == 3) {
        cv_broadcast(westcv, intersection);
      }
    }
  }
  lock_release(intersection);
}
