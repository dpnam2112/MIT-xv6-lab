#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/pstat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void iotask(int pid){
  int fd = open("bench_io", O_CREATE | O_RDWR);
  if (fd < 0){
    printf("error: failed to create a file\n");
    return;
  }
  char buf[20] = "hello world";
  int ITERATIONS = 100;
  for (int i = 0; i < ITERATIONS; i++){
    write(fd, buf, strlen(buf));
  }
  printf("info: pid=%d done io tasks\n", pid);
  close(fd);
}

void cputask(int pid){
  int COUNT = 500000000;
  int cp = COUNT / 4; // checkpoint
  int cp2 = COUNT / 2;
  int cp3 = cp * 3;
  for (int i = 0; i < COUNT; i++){
    if (i == cp || i == cp2 || i == cp3){
      // just to ensure compiler won't do dirty trick
      printf("info: pid=%d checkpoint=%d\n", pid, i);
    }
  }
  printf("info: pid=%d done %d iterations\n", pid, COUNT);
}

void printpstat(struct pstat* pstat){
  printf("==   PSTAT   ==\n");
  printf("pid = %d\n", pstat->pid);
  printf("stime = %u\n", (int) pstat->stime);
  printf("etime = %d\n", (int) pstat->etime);
  printf("rptime = %d\n", (int) pstat->rptime);
  printf("qtime = %d\n", (int) pstat->qtime);
  printf("== END PSTAT ==\n");
}

int
main(int argc, char *argv[]){
  if(argc < 3){
    fprintf(2, "usage: bench [io|cpu] num_of_tasks\n");
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
        cputask(pid);
      } else {
        iotask(pid);
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

  exit(0);
}
