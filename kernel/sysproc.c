#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "pstat.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// getpstat(int pid, struct pstat *pstart)
uint
sys_getpstat(void)
{
  int pid;
  uint64 upstat_addr;
  argint(0, &pid);
  argaddr(1, &upstat_addr);
  struct pstat *pstat;
  struct proc *p = myproc();
  pstat = proc_getpstat(pid);
  if (copyout(p->pagetable, upstat_addr, (char*) pstat, sizeof(struct pstat)) != 0){
    return 1;
  }
  return 0;
}

extern sched_tracer_t sched_tracer;

// int schedtrace(sched_trace_t*)
// a simple utility system call to get scheduler traces.
// return the number of records copied to user-space array 
uint
sys_schedtrace(void)
{
  uint64 u_trace_arr; // array of trace record in user space
  int arrsz;
  argaddr(0, &u_trace_arr);
  argint(1, &arrsz);
  if (u_trace_arr == 0){
    return -1;
  }
  int i;
  struct proc *p = myproc();
  for (i = 0; i < arrsz; i++){
    sched_trace_t trace;
    int res = sched_tracer_deq(&sched_tracer, &trace);
    if (res < 0){
      break;
    }

    if (copyout(p->pagetable, (uint64) ((sched_trace_t*) u_trace_arr + i), (char*) &trace, sizeof(sched_trace_t)) < 0){
      panic("schedtrace");
    }
  }
  return i;
}
