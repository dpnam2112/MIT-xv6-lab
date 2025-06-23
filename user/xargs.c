#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"


int is_delimiter(char c) {
  return c == ' ' || c == '\n' || c == 0;
}


void log_reach_delimiter(char c) {
  if (c == ' ') {
    printf("[LOG] Reach delimiter: <space>\n");
  } else if (c == '\n') {
    printf("[LOG] Reach delimiter: <newline>\n");
  } else if (c == 0) {
    printf("[LOG] Reach delimiter: <null>\n");
  } else {
    printf("[LOG] Unknown delimiter.\n");
  }
}

// queue of characters
char buf[512];

int main(int argc, char* argv[]) {
  // Usage: xargs [command] [argument list]
  // The behavior of this implementation should be equivalent to running Unix's xargs with flag '-n' = 1
  
  if (argc < 2) {
    printf("usage: xargs [command] ...args");
    exit(1);
  }

  char* command = argv[1];
  char buf[512] = {};
  int read_count;

  // contain the parsed argument
  char arg[128];

  // indicate if an error occurred 
  int error = 0;

  // incremented every time exec() is invoked
  int child_proc_count = 0;

  // contain the remaining chars of the previous iteration
  char prev_it_remaining[128];
  int prev_it_remaining_len = 0;

  // one more slot at the end for NULL pointer
  char* child_argv[MAXARG + 1] = {command};
  int child_argv_len = 1;

  // append additional arguments from xargs to child
  // the first two items in argv are always 'xargs' and the command
  for (int i = 2; argv[i] != 0; i++) {
    child_argv[child_argv_len++] = argv[i];
  }

  // the current implementation behaves like xargs when executing with flag -n set to 1
  // i.e: [command] [argument list] [argument from stdin]
  child_argv[child_argv_len++] = arg;
  child_argv[child_argv_len] = 0;

#ifdef LOG
  printf("[LOG] Default arguments:\n");
  for (int i = 0; i < child_argv_len; i++) {
    printf("[LOG] Argument %d: %s\n", i, child_argv[i]);
  }
#endif

  while ((read_count = read(0, buf, 512)) != 0 && error == 0) {
#ifdef LOG
    printf("[LOG] read %d bytes from stdin.\n", read_count);
#endif
    int i = 0;

    while (i < read_count) {
      int arglen = 0;
      int start = i;
      while (i < read_count && !is_delimiter(buf[i])) {
        arglen++;
        i++;
      }

      // end of loop: i == read_count or is_delimiter(buf[i])

      if (is_delimiter(buf[i]) && prev_it_remaining_len == 0) {
        // iterator reaches a delimiter
        // move the argument to the array of args

#ifdef LOG
        printf("[LOG] i = %d\n", i);
        printf("[LOG] arglen = %d\n", arglen);
        log_reach_delimiter(buf[i]);
#endif
        if (arglen >= 128) {
          printf("error: argument length must not exceed 127.\n");
          error = 1;
          break;
        }

        memmove(arg, &buf[start], arglen);
        arg[arglen] = 0;

#ifdef LOG
        printf("[LOG] arguments:\n");
        for (int i = 0; i < child_argv_len; i++) {
          printf("[LOG] Argument %d: %s\n", i, child_argv[i]);
        }
#endif

        if (fork() == 0) {
          exec(command, child_argv);
        }

#ifdef LOG
        printf("[LOG] executed command %s with additional argument: %s\n", command, arg);
#endif

        child_proc_count++;
      } else if (is_delimiter(buf[i]) && prev_it_remaining_len != 0) {
        if (arglen + prev_it_remaining_len < 128) {
          printf("error: argument length must not exceed 127.\n");
          error = 1;
          break;
        }

#ifdef LOG
        log_reach_delimiter(buf[i]);
#endif

        // Concatenate the remaining part in the previous buffer-reading iteration and the part in the current one.
        memmove(arg, prev_it_remaining, prev_it_remaining_len);
        memmove(&arg[prev_it_remaining_len], &buf[start], arglen);
        arg[arglen + prev_it_remaining_len] = 0;

        if (fork() == 0) {
          exec(command, child_argv);
        }

#ifdef LOG
        printf("[LOG] executed command with additional argument: %s\n", arg);
#endif

        child_proc_count++;
        prev_it_remaining_len = 0;
      } else {
        // iterator reaches the end of buffer but still doesn't reach a delimiter
        // copy the remaining string for concatenation in the next buffer-reading iteration.
        memmove(prev_it_remaining, &buf[start], arglen);
        prev_it_remaining_len = arglen;
      }

      i++;
    }
  }

  if (read_count < 0) {
    printf("error when reading from stdin.\n");
    error = 1;
  }

  if (prev_it_remaining_len != 0) {
    memmove(arg, prev_it_remaining, prev_it_remaining_len);
    arg[prev_it_remaining_len] = 0;
  }


#ifdef LOG
  printf("Waiting for %d child processes\n", child_proc_count);
#endif

  for (int i = 0; i < child_proc_count; i++) {
    int status;

#ifdef LOG
    int pid = wait(&status);
    printf("[LOG] Process %d exited with status code %d.\n", pid, status);
#else
    wait(&status);
#endif
  }

  exit(error);
}
