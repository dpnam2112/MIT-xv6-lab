#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/pstat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Helper to create "bio_<pid>" without sprintf
void
make_unique_name(char *buf, int pid)
{
  strcpy(buf, "bench_io_");
  int len = strlen(buf);

  if(pid == 0){
    buf[len] = '0';
    buf[len+1] = 0;
    return;
  }

  int temp = pid;
  int digits = 0;
  while(temp > 0){
    temp /= 10;
    digits++;
  }

  buf[len + digits] = 0; 
  
  for(int i = 0; i < digits; i++){
    buf[len + digits - 1 - i] = (pid % 10) + '0';
    pid /= 10;
  }
}


int cputask(int pid, int iter){
  int i = 0;
  for (; i < iter; i++);
  return i;
}

void iotask(int pid){
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
  int ITERATIONS = 30;
  int cp = ITERATIONS / 2; // checkpoint
  for (int i = 0; i < ITERATIONS; i++){
    sleep(3);
    if (i % cp == 0){
      printf("info: iotask pid=%d\n", pid);
    }
    // do some work
    cputask(pid, 5000);
  }
  printf("info: pid=%d done io tasks\n", pid);
//  close(fd);
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
        int iter = 500000000;
        int res = cputask(pid, iter);
        printf("info: done %d iterations\n", res);
      } else {
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

  exit(0);
}
