#include "param.h"

// configurations for multi-level feedback quueue
// these can be passed during compile time (-D flags)
#ifndef MLFQ_MAX_PRIO
#define MLFQ_MAX_PRIO 19 // lowest priority is 0
#endif

#ifndef MLFQ_RESET_QUANTUM
#define MLFQ_RESET_QUANTUM 100
#endif

#define MLFQ_PER_QUEUE_SIZE NPROC

struct mlfq_task_queue
{
  struct spinlock lk;
  struct proc *tasks[NPROC];
  int head;
  int tail;
};

void mlfq_init();
void mlfq_demote(struct proc*);
void mlfq_reset();
void mlfq_enq(struct proc*);
void mlfq_task_queue_init(struct mlfq_task_queue*);
void mlfq_task_queue_reset(struct mlfq_task_queue*);
int mlfq_task_queue_enq(struct mlfq_task_queue*, struct proc *p);
struct proc* mlfq_task_queue_deq(struct mlfq_task_queue*);

// scheduler_t mlfq_scheduler;
void  __attribute__((noreturn)) mlfq_scheduler(void);
