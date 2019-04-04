/*
* CSC 452 - Project 3
* Author: Hanna Veldhuizen
*/
#include <unistd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/types.h>
#include<sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/unistd.h>

struct csc452_sem {
  int value;
  struct task_node *head;
  struct task_node *tail;
};

// time the program starts
time_t* start_time;
// cars coming north
int *north_queue;
// cars coming south
int* south_queue;
// number of cars currently in the north queue
int* north_cars;
// number of cars currently in the south queue
int* south_cars;
// total number of cars to come through, used as the car ID number
int* total_cars;
// pointer to north queue
int* north_ptr;
// pointer to south queue
int* south_ptr;
// asleep flag, 1 for asleep 0 for awake
int* asleep;

/* North and South queues, mutex for mutual exclusion. Semaphores to be shared among processes */
struct memory_region {
  struct csc452_sem north;
  struct csc452_sem south;
  struct csc452_sem mutex;
};

struct memory_region* shared_memory;

void down(struct csc452_sem *sem) {
  syscall(387, sem);
}

void up(struct csc452_sem *sem) {
  syscall(388, sem);
}

/*
* setup() -- This method maps memory to be shared between consumer and producer processes. We have two
*/
void setup() {
  void *ptr = mmap(NULL, sizeof(struct memory_region), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  shared_memory = ptr;

  // NORTH AND SOUTH SEMAPHORES
  // nothing in full and empty, so we initialize to 10 for empty (bc we have 10 open spaces)
  // and 0 for full (bc we have consumed 0). head and tail are null
  (*shared_memory).north.value = 0;
  (*shared_memory).north.head = NULL;
  (*shared_memory).north.tail = NULL;

  (*shared_memory).south.value = 0;
  (*shared_memory).south.head = NULL;
  (*shared_memory).south.tail = NULL;

  // mutual exclusion
  (*shared_memory).mutex.value = 1;
  (*shared_memory).mutex.head = NULL;
  (*shared_memory).mutex.tail = NULL;

  void* north_mem = mmap(NULL, sizeof(int)*(12), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  north_ptr = (int*) north_mem;
  north_cars = (int*) north_mem + 1;
  north_queue = (int*) north_mem + 2;

  void* south_mem = mmap(NULL, sizeof(int)*(12), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  south_ptr = (int*) south_mem;
  south_cars = (int*) south_mem + 1;
  south_queue = (int*) south_mem + 2;

  *north_ptr = *north_queue;
  *south_ptr = *;
  *north_cars = 0;
  *south_cars = 0;

  void* total_cars_mem = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  total_cars = (int*) total_cars_mem;
  *total_cars = 0;

  void* asleep_mem = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  asleep = (int*) asleep_mem;
  *asleep = 1;

  void* time_mem = mmap(NULL, sizeof(time_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  start_time = (time_t*) time_mem;
  *start_time = time(NULL);

  printf("The flagperson is now asleep.\n");
}

int main(int argc, char* argv[]) {
  // map memory and initialize all start values
  setup();

  srand(time(NULL));

  // producers
  if (fork()==0) {
    // north
    while(1) {
      // keep producing cars from the north until the RNG is greater than 8
      while (rand() % 10 < 8) {
        // lock crit region
        down(&(*shared_memory).north);
        down(&(*shared_memory).mutex);

        // increment total number of cars that have come through
        *total_cars = *total_cars + 1;
        *north_cars = *north_cars + 1;

        // add to the queue
        north_queue[*north_ptr] = *total_cars;

        time_t now;
        now = time(NULL);
        printf("Car %d coming from the N direction arrived in the queue at time %ld.\n", *total_cars, now - *start_time);

        // increment pointer for the north array
        *north_ptr = *north_ptr + 1;

        up(&(*shared_memory).mutex);
        up(&(*shared_memory).north);
      }

      sleep(20);
    }
  }

  // south producer, same thing as above but with south values
  if (fork() == 0) {
    int car;
    while(1) {
      while (rand() % 10 < 8) {
        // lock crit region
        down(&(*shared_memory).south);
        down(&(*shared_memory).mutex);

        // increment total number of cars that have come through
        *total_cars = *total_cars + 1;
        *south_cars = *south_cars + 1;

        // add to the queue
        south_queue[*south_ptr] = *total_cars;

        time_t now;
        now = time(NULL);

        printf("Car %d coming from the S direction arrived in the queue at time %ld.\n", *total_cars, now - *start_time);

        // increment pointer for the north array
        *south_ptr = *south_ptr + 1;

        up(&(*shared_memory).mutex);
        up(&(*shared_memory).south);
      }

      // once no car comes, sleep for 20 seconds
      sleep(20);
    }
  }


  // consumers
  if (fork() == 0) {
    while(1){
      // while cars are coming north, let them through until there are no more or there are 10 cars coming south
      while (*north_cars > 0) {
        down(&(*shared_memory).north);
        down(&(*shared_memory).mutex);

        time_t now;
        now = time(NULL);
        if (*asleep == 1) {
          printf("The flagperson is now awake.\n");
          printf("Car %d coming from the N direction, blew their horn at time %ld.\n", *total_cars, now - *start_time);
          *asleep = 0;
        }

        int car = north_queue[*north_ptr];
        *north_cars = *north_cars - 1;
        *north_ptr = *north_ptr - 1;

        up(&(*shared_memory).mutex);
        up(&(*shared_memory).north);

        // sleep for 2 seconds after letting a car go
        sleep(2);

        printf("Car %d coming from the S direction, left the construction zone at time %ld.\n", car, now - *start_time);

        if (*south_cars == 0 && *north_cars == 0) {
          printf("The flagperson is now asleep.\n");
          *asleep = 1;
        }

        if(*south_cars >= 10){
          break;
        }
      }

      while (*south_cars > 0) {
        down(&(*shared_memory).south);
        down(&(*shared_memory).mutex);

        int car = south_queue[*south_ptr];
        *south_cars = *south_cars - 1;
        *south_ptr = *south_ptr - 1;

        time_t now;
        now = time(NULL);
        if (*asleep == 1) {
          printf("The flagperson is now awake.");
          printf("Car %d coming from the S direction, blew their horn at time %ld.\n", car, now - *start_time);
          *asleep = 0;
        }

        up(&(*shared_memory).mutex);
        up(&(*shared_memory).south);

        // sleep for 2 seconds after letting a car go
        sleep(2);

        printf("Car %d coming from the N direction, left the construction zone at time %ld.\n", car, now - *start_time);

        if (*south_cars == 0 && *north_cars == 0) {
          printf("The flagperson is now asleep.\n");
          *asleep = 1;
        }

        if(*north_cars >= 10){
          break;
        }
      }
    }
  }


  wait(NULL);
  return 0;
}
