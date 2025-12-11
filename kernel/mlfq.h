#include "param.h"

#ifndef MLFQ_MAX_PRIO
#define MLFQ_MAX_PRIO 19 // lowest priority is 0
#endif

#ifndef MLFQ_RESET_QUANTUM
#define MLFQ_RESET_QUANTUM 100
#endif

struct mlfq_task_queue
{
  struct proc *tasks[NPROC];
  int head;
  int tail;
};

// initialize a task queue
void mlfq_task_queue_init(struct mlfq_task_queue*);

// return 0 if success
int
mlfq_task_queue_enq(struct mlfq_task_queue*, struct proc *p);

// return the process at the tail
struct proc*
mlfq_task_queue_deq(struct mlfq_task_queue*);
