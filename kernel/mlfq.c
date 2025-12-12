#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "mlfq.h"

extern struct proc proc[NPROC];

// invariant:
// head points to the location where the next item would be put.
// tail points to the next item to be removed.
void 
mlfq_task_queue_reset(struct mlfq_task_queue *q)
{
  acquire(&q->lk);
  q->head = 0;
  q->tail = 0;
  for (int i = 0; i < MLFQ_PER_QUEUE_SIZE; i++) {
      q->tasks[i] = 0;
  }
  release(&q->lk);
}

void
mlfq_task_queue_init(struct mlfq_task_queue *q){
  initlock(&q->lk, "mlfq_task_queue");
  mlfq_task_queue_reset((q));
}

int
mlfq_task_queue_enq(struct mlfq_task_queue *q, struct proc *p)
{
  acquire(&q->lk);
  if (p->state != RUNNABLE){
    panic("mlfq_task_queue_enq");
  }
  int next_head = (q->head + 1) % MLFQ_PER_QUEUE_SIZE;
  if (next_head == q->tail){
    // the queue is full; cannot move the head further
    release(&q->lk);
    return -1;
  }
  q->tasks[q->head] = p;
  q->head = next_head;
  release(&q->lk);
  return 0;
}


struct proc*
mlfq_task_queue_deq(struct mlfq_task_queue *q)
{
  acquire(&q->lk);
  if (q->tail == q->head) {
    // queue is empty
    release(&q->lk);
    return 0;
  }
  struct proc *p = q->tasks[q->tail];
  q->tasks[q->tail] = 0; 
  q->tail = (q->tail + 1) % MLFQ_PER_QUEUE_SIZE;
  release(&q->lk);
  return p;
}

// lowest priority is 0
#define MLFQ_NUM_QUEUES MLFQ_MAX_PRIO + 1
struct mlfq_task_queue mlfq_task_queues[MLFQ_NUM_QUEUES];

// used during reset
// when the scheduler resets the mlfq, there might be chances that an interrupt occurs, which may
// cause race condition, e.g., a process is woken up from an I/O event and its state switches from
// SLEEPING to RUNNABLE => it's enqueued to the mlfq.
struct spinlock mlfq_reset_lk;

void
mlfq_init()
{
  initlock(&mlfq_reset_lk, "mlfq");
  for (struct mlfq_task_queue *q = mlfq_task_queues; q < mlfq_task_queues + MLFQ_NUM_QUEUES; q++){
    mlfq_task_queue_init(q);
  }
}

void
mlfq_demote(struct proc *p){
  int new_prio = p->prio - 1;
  if (new_prio < 0){
    new_prio = 0;
  }
  p->prio = new_prio;
}

void
mlfq_reset()
{
  // reset all queues
  acquire(&mlfq_reset_lk);
  for (struct mlfq_task_queue *q = mlfq_task_queues; q < mlfq_task_queues + MLFQ_NUM_QUEUES; q++){
    mlfq_task_queue_reset(q);
  }

  struct mlfq_task_queue *highest_q = &mlfq_task_queues[MLFQ_MAX_PRIO]; 
  // promote all processes to the highest priority
  for (struct proc *p = proc; p < proc + NPROC; p++){
    acquire(&p->lock);
    if (!(p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING)){
      release(&p->lock);
      continue;
    }

    p->prio = MLFQ_MAX_PRIO;
    // only enqueue the RUNNABLE tasks here to maintain the invariant
    if (p->state == RUNNABLE) {
      if (mlfq_task_queue_enq(highest_q, p) != 0){
        panic("mlfq_reset");
      }
    }
    release(&p->lock);
  }
  release(&mlfq_reset_lk);
}


// scheduler_t mlfq_scheduler
// implementation of multilevel feedback queue.
// NOTE: currently, the implementation only works for single-core cpu.
// it has not yet to be tested/implemented for multi-core cpu.
void  __attribute__((noreturn))
mlfq_scheduler(void)
{
  printf("scheduler: use policy mlfq\n");
  struct cpu *c = mycpu();
  for (;;){
    // copied from the original round robin scheduler
    intr_on();

    // reset the mlfq, i.e., promote all tasks to the highest priority,
    // after N ticks to avoid starvation for low-priority tasks
    if (ticks % MLFQ_RESET_QUANTUM == 0){
      mlfq_reset();
    }
    
    struct proc *p = 0;
    for (int prio = MLFQ_MAX_PRIO; prio > -1; prio--){
      struct mlfq_task_queue *task_q = &mlfq_task_queues[prio];
      // TODO: handle concurrency
      p = mlfq_task_queue_deq(task_q);
      if (p != 0){
        acquire(&p->lock);

        // invariant: mlfq only contains RUNNABLE tasks
        // SLEEPING, RUNNING tasks are only enqueued when their states switch to RUNNABLE.
        if (p->state != RUNNABLE){
          panic("mlfq_scheduler: p->state is not RUNNABLE");
        }

        c->proc = p;

        // a simple way to pick a quota (time limit that a task can use a cpu, at a given prio)
        // the higher priority is => the shorter quota, since interactive jobs are expected to be
        // short-bursted
        int quota = MLFQ_MAX_PRIO / (p->prio + 1);
        if (quota < 1){
          quota = 1;
        }

        // cpu continues the chosen task until the task is out of quota
        p->state = RUNNING;
        record_pstat(p);
        p->mlfq_quota = quota;
#ifdef SCHEDTRACE
          proc_recordtrace(p, SCHEDTRACE_PROC_STATE_CHANGE);
#endif
        swtch(&c->context, &p->context);
  
        c->proc = 0;
        release(&p->lock);

        // rerun from the highest priority
        break;
      }
    }

    // enter power-saving state and wait for an interrupt
    if (p == 0){
      intr_on();
      asm volatile("wfi");
    }
  }
};

void
mlfq_enq(struct proc *p)
{
  // enqueue the process to the runnable queue.
  if (p->state != RUNNABLE){
    panic("proc_runnable_hook");
  }
  struct mlfq_task_queue *q = &mlfq_task_queues[p->prio];
  acquire(&mlfq_reset_lk);
  if (mlfq_task_queue_enq(q, p) != 0){
    panic("proc_runnable_hook: failed to enqueue");
  }
  release(&mlfq_reset_lk);
}
