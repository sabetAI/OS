#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <current.h>
#include <array.h>
#include <queue.h>

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
static struct semaphore *intersectionSem;

typedef struct Vehicle
{
  int id;
  struct thread *owner;
  Direction origin;
  Direction destination;
} Vehicle;

volatile int ctr = 0;
static struct lock *x_lock;
static struct cv *x_occupied;

struct array *vehicles;

Vehicle *create_vehicle(Direction origin, Direction destination);
bool has_conflict(Vehicle *v1);
static bool right_turn(Vehicle *v);

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */

Vehicle *create_vehicle(Direction origin, Direction destination){
    Vehicle *v = kmalloc(sizeof(Vehicle));
    if (v == NULL){
        panic("Create Vehicle failed to allocate memory");
    }

    v->id = ctr;
    v->owner = curthread;
    v->origin = origin;
    v->destination = destination;

    return v;
}


bool has_conflict(Vehicle *v1){
  /* compare newly-added vehicle to each other vehicles in in the intersection */
  for(uint32_t i = 0; i < array_num(vehicles); ++i) {
    Vehicle *v2 = array_get(vehicles, i);
    if ((v2->owner == NULL) || (v1->owner == v2->owner)) continue;
    /* no conflict if both vehicles have the same origin */
    if (v1->origin == v2->origin) continue;
    /* no conflict if vehicles go in opposite directions */
    if ((v1->origin == v2->destination) &&
        (v1->destination == v2->origin)) continue;
    /* no conflict if one makes a right turn and 
       the other has a different destination */
    if ((right_turn(v1) || right_turn(v2)) &&
	(v1->destination != v2->destination)) continue;
    /* Houston, we have a problem! */
    /* print info about the two vehicles found to be in conflict,
       then cause a kernel panic */

    return true;
  }

  return false; 
}
    
    
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  
  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }

  x_lock = lock_create("intersection lock");
  x_occupied = cv_create("intersection cv");
  
  vehicles = array_create();
  array_init(vehicles); 

  return;
}


bool right_turn(Vehicle *v) {
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
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
  KASSERT(intersectionSem != NULL);

  array_destroy(vehicles);

  lock_destroy(x_lock);
  cv_destroy(x_occupied);

  sem_destroy(intersectionSem);
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
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);

  lock_acquire(x_lock);
  
  ++ctr;
  Vehicle *v_new = create_vehicle(origin, destination); 
  
  while(has_conflict(v_new)){
    cv_wait( x_occupied, x_lock );
  }
    
  KASSERT(lock_do_i_hold(x_lock)); // is this necessary?
  array_add(vehicles, v_new, NULL);

  lock_release(x_lock);
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
  /* replace this default implementation with your own implementation */

  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);
  V(intersectionSem);

  lock_acquire(x_lock);
  
  for (uint32_t i = 0; i < array_num(vehicles); ++i){
    Vehicle *v = array_get(vehicles, i);
    if (v->owner == curthread){
        array_remove(vehicles, i);
    }
  }

  cv_broadcast(x_occupied, x_lock);
  lock_release(x_lock);
}
