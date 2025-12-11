#include "mlfq.h"

void 
mlfq_task_queue_init(struct mlfq_task_queue *q)
{
    q->head = 0;
    q->tail = 0;
    for (int i = 0; i < NPROC; i++) {
        q->tasks[i] = 0;
    }
}

int
mlfq_task_queue_enq(struct mlfq_task_queue *q, struct proc *p)
{
  if (q->head == q->tail){
    return -1;
  }
  q->tasks[q->head] = p;
  q->head = (q->head + 1) % NPROC;
  return 0;
}


struct proc*
mlfq_task_queue_deq(struct mlfq_task_queue *q)
{
    if (q->head == q->tail) {
        return 0; 
    }
    struct proc *p = q->tasks[q->tail];
    q->tasks[q->tail] = 0; 
    q->tail = (q->tail + 1) % NPROC;
    return p;
}
