#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


#define true 1
#define false 0

const int MAX = 280;

int left_pipe[2] = {-1, -1};
int right_pipe[2] = {-1, -1};


void
log_receive(int pid, int num) {
#ifdef LOG
  printf("[DEBUG] process %d receives number %d.\n", pid, num);
#endif
}

void
log_send(int pid, int child, int num) {
#ifdef LOG
  printf("[DEBUG] process %d sends number %d to %d.\n", pid, num, child);
#endif
}


void
log_fork(int parent, int child) {
#ifdef LOG
  printf("[DEBUG] process %d create a new process, pid=%d\n", parent, child);
#endif
}

int
is_prime(int n) {
  // return 1 if n is prime, 0 otherwise.
  // although there is a more efficient way, but this is enough for this problem.
  if (n < 3) {
    return n == 2;
  }

  for (int i = 2; i < n; i++) {
    if (n % i == 0) {
      return 0;
    }
  }

  return 1;
}


// left pipe is an array of two file descriptors
void worker() {
  int num = 0;
  int n;
  int printed = false;
  int right_neighbor_pid = -1;

  // no need tow write to the left pipe

  // forward numbers received from the left neighbor to the right neighbor
  while (left_pipe[0] >= 0 && (n = read(left_pipe[0], &num, sizeof(int))) > 0) {
    if (n != 4) {
      printf("ERROR: number of bytes read must be 4.");
      printf("Waiting for the child process to finish...");
      int status;
      wait(&status);
      printf("Child process finished. Status code: %d.", status);
      exit(1);
    }

    log_receive(getpid(), num);
    if (!printed && is_prime(num)) {
      // receive a prime number from the left neighbor for the first time
      printf("prime %d\n", num);
      // reader is not needed for right pipe, since the data flow is from
      // left to right.
      int res = pipe(right_pipe);
      if (res != 0) {
        printf("ERROR: error while creating new pipe, pid: %d\n", getpid());
        printf("Waiting for the child process to finish...\n");
        int status;
        wait(&status);
        printf("Child process finished. Status code: %d.\n", status);
        exit(1);
      }

      int pid = fork();
      if (pid == 0) {
        // sleep(1);
        // right neighbor process
        close(left_pipe[0]);

        left_pipe[0] = right_pipe[0];
        left_pipe[1] = right_pipe[1];

        // close left writer
        int res = close(left_pipe[1]);
        if (res != 0) {
          printf("ERROR: error when closing write end of the left pipe of the new worker. PID: %d\n", getpid());
        }

        continue;
      } else {
        // the process that called the fork syscall
        log_fork(getpid(), right_neighbor_pid);
        // close read end of the right pipe
        int res = close(right_pipe[0]);
        if (res != 0) {
          printf("ERROR: error when closing read end of the right pipe of process with PID: %d\n", getpid());
          printf("Waiting for the child process to finish...\n");
          int status;
          wait(&status);
          printf("Child process finished. Status code: %d.\n", status);
          exit(1);
        }

        printed = true;
        right_neighbor_pid = pid;
      }
    } else if (printed) {
      // only send data to the right neighbor after the first prime received is printed
      write(right_pipe[1], &num, sizeof(int));
      log_send(getpid(), right_neighbor_pid, num);
    } 
  }

  if (left_pipe[0] < 0) {
    printf("ERROR: invalid file descriptor for the read end of the left pipe, PID: %d\n", getpid());
    exit(1);
  }

  if (n < 0) {
    printf("ERROR: error while reading from the left pipe, PID: %d, read's status: %d\n", getpid(), n);
    exit(1);
  }
}

int
main(int argc, char *argv[]) {
  printf("2\n");
  pipe(right_pipe);
  int pid = fork();

  if (pid == 0) {
    // sleep(1);
    // new process execution goes here
    left_pipe[0] = right_pipe[0];
    left_pipe[1] = right_pipe[1];

    // close write end of the left pipe
    close(left_pipe[1]);
    worker();
  } else {
    // process calling fork syscall

    // close read end of the right pipe
    close(right_pipe[0]);
    for (int i = 3; i <= MAX; i++) {
      log_send(getpid(), pid, i);
      write(right_pipe[1], &i, sizeof(int));
    }
  }

  close(right_pipe[1]);

  int child_status = 0;
  wait(&child_status);

  if (child_status != 0) {
    printf("ERROR: Child process returns status code of %d, current pid: %d\n", child_status, getpid());
    exit(1);
  }

  exit(0);
}
