#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int trace_mark = atoi(argv[1]);
  trace(trace_mark);
  if (exec(argv[2], &argv[2]) != 0) {
    printf("error executing system call exec");
  }
}
