#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/pstat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void iotask(){
  int fd = open("bench_io", O_CREATE | O_RDWR);
  if (fd < 0){
    printf("error: failed to create a file\n");
    return;
  }
  char buf[20] = "hello world";
  write(fd, buf, strlen(buf));
  write(fd, buf, strlen(buf));
  close(fd);
}

void cputask(){
  int COUNT = 5000000;
  int checkpoint = COUNT / 2;
  for (int i = 0; i < COUNT; i++){
    if (i == checkpoint){
      // just to ensure compiler won't do dirty trick
      printf("checkpoint at %d\n", i);
    }
  }
  printf("done %d iterations\n", COUNT);
}

void printpstat(struct pstat* pstat){
  printf("==   PSTAT   ==\n");
  printf("pid = %d\n", pstat->pid);
  printf("stime = %u\n", (int) pstat->stime);
  printf("etime = %d\n", (int) pstat->etime);
  printf("rptime = %d\n", (int) pstat->rptime);
  printf("== END PSTAT ==\n");
}

int
main(){
  const int NTASKS = 20;
  int pids[NTASKS];

  for (int i = 0; i < NTASKS; i++){
    pids[i] = fork();
    if (pids[i] == 0){
      goto worker;
    }
    continue;
worker:
    cputask();
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
