// Call syscall 'sleep'
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("[USAGE] sleep [integer]. Example: 'sleep 10'\n");
    exit(1);
  }
  int cmd_arg = atoi(argv[1]);
  sleep(cmd_arg);
  exit(0);
}

