#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <sys/types.h>


int
main(int argc, char *argv[])
{
  int fd[2];

  // create two file descriptors:
  // one for reading from, and,
  // one for writing into.
  pipe(fd);

  int read_fd = fd[0];
  int write_fd = fd[1];
  char signal = '0';
  char buf[1];

  pid_t pid = fork();

  if (pid == 0) {
    // Child process
    int n = read(read_fd, buf, 1);
    if (n != 1) {
      printf("[ERROR] Expect to receive a single byte from the parent process, but got %d.\n", n);
      close(read_fd);
      close(write_fd);
      exit(1);
    }

    int close_status = close(read_fd);
    if (close_status != 0) {
      printf("[ERROR] Expect read fd close status to be 0, but got: %d\n", close_status);
      exit(1);
    }

    printf("%d: received ping\n", getpid());

    n = write(write_fd, &signal, 1);
    
    if (n != 1) {
      printf("[ERROR] Expect a single byte to be written, but got %d.\n", n);
      close(write_fd);
      exit(1);
    }

    close_status = close(write_fd);
    if (close_status != 0) {
      printf("[ERROR] Expect write fd close status to be 0, but got: %d\n", close_status);
      exit(1);
    }
    exit(0);
  } else {
    // Parent process
    int n = write(write_fd, &signal, 1);
    if (n != 1) {
      printf("[ERROR] Expect %d bytes to be written, but got %d.\n", 1, n);
      close(write_fd);
      close(read_fd);
      exit(1);
    }

    int close_status = close(write_fd);
    if (close_status != 0) {
      printf("[ERROR] Expect write fd close status to be 0, but got: %d\n", close_status);
      exit(1);
    }

    // wait until the child process exits.
    int child_status;
    wait(&child_status);

    if (child_status != 0) {
      printf("[ERROR] Expect exit status %d from the child process, but got %d.\n", 0, child_status);
      close(read_fd);
      exit(1);
    }

    n = read(read_fd, buf, 1);
    if (n != 1) {
      printf("[ERROR] Expect to receive a single byte from the child, but got: %d.\n", n);
      close(read_fd);
      exit(1);
    }

    close(read_fd);
    if (close_status != 0) {
      printf("[ERROR] Expect read fd close status to be 0, but got: %d\n", close_status);
      exit(1);
    }
    printf("%d: received pong\n", getpid());
    exit(0);
  }
}
