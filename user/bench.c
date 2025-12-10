#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/pstat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/schedtrace.h"

// from FreeBSD.
int
do_rand(unsigned long *ctx)
{
/*
 * Compute x = (7^5 * x) mod (2^31 - 1)
 * without overflowing 31 bits:
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * From "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
    long hi, lo, x;

    /* Transform to [1, 0x7ffffffe] range. */
    x = (*ctx % 0x7ffffffe) + 1;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    /* Transform to [0, 0x7ffffffd] range. */
    x--;
    *ctx = x;
    return (x);
}

unsigned long rand_next = 1;

int
rand(void)
{
    return (do_rand(&rand_next));
}


int
cputask(int pid, int iter)
{
  int i = 0;
  // The compiler may do dirty tricks, e.g., remove entirely this loop, 
  // since the loop is meaningless. Hence, I put the return line to ensure 
  // it won't do such tricks.
  for (; i < iter; i++);
  return i; 
}

void
iotask(int pid){
//  char filename[50];
//  make_unique_name(filename, pid);
//
//  int fd = open(filename, O_CREATE | O_RDWR);
//  if (fd < 0){
//    printf("error: failed to create a file\n");
//    return;
//  }
//
//  char buf[20] = "hello world";
  int ITERATIONS = 5;
  for (int i = 0; i < ITERATIONS; i++){
    sleep(rand() % 6 + 2);
    // do some lightweight work
    cputask(pid, 1000000000);
  }
//  close(fd);
}

void
mix_workload_task(int pid, int iter)
{
  // a real-world example of a workload mixed between computation and io:
  // - user types in complex formulas in an excel sheet (io)
  // - user hits enter (io)
  // - excel computes the result (compute)

  for (int i = 0; i < iter; i++){
    sleep(3);
    cputask(pid, 2000000);
    sleep(5);
    cputask(pid, 5000000);
  }
}


void printpstat(struct pstat* pstat){
  printf("pstat: pid=%d stime=%u etime=%d rptime=%d qtime=%d\n",
    pstat->pid,
    (int) pstat->stime,
    (int) pstat->etime,
    (int) pstat->rptime,
    (int) pstat->qtime
  );
}

const int schedtrace_size = 1000;
struct sched_trace schedtraces[1000];

int
main(int argc, char *argv[]){
  if(argc < 3){
    fprintf(2, "usage: bench [io|cpu|mixed] num_of_procs\n");
    exit(1);
  }

  char *workload = argv[1];
  char *ntask_param = argv[2];
  int NTASKS = atoi(ntask_param);
  int pids[NTASKS];

  for (int i = 0; i < NTASKS; i++){
    pids[i] = fork();
    if (pids[i] == 0){
      goto worker;
    }
    continue;
worker:
    {
      int pid = getpid();
      if (strcmp(workload, "cpu") == 0){
        int iter = 500000000;
        int res = cputask(pid, iter);
        printf("info: done %d iterations\n", res);
      } else if (strcmp(workload, "mixed") == 0){
        if (rand() % 2 == 0){
          int iter = 50000000;
          cputask(pid, iter);
        } else {
          iotask(pid);
        }
      }else {
        iotask(pid);
//        printf("info: done io tasks\n");
      }
    }
    exit(0);
  }

  for (int i = 0; i < NTASKS; i++){
    int w_status;
    int pid = wait(&w_status);
    struct pstat pstat;
    printf("process exited pid=%d\n", pid);
    getpstat(pid, &pstat);
    printpstat(&pstat);
  }

  int ntraces;
  while ((ntraces = schedtrace(schedtraces, schedtrace_size)) < 0){
    printf("error: schedtrace\n");
    exit(1);
  }

  for (struct sched_trace *trace = schedtraces; trace < schedtraces + ntraces; trace++){
    printf("trace: tick=%d pid=%d state=%d prio=%d event=%d\n", trace->tick, trace->pid, trace->pstate, trace->prio, trace->type);
  }

  printf("# traces total: %d\n", ntraces);
  exit(0);
}
