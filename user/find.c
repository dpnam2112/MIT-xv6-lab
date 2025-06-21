#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

const char*
fmtname(const char *path)
{
  // definition is copied from 'user/ls.c'

  static char buf[DIRSIZ+1];
  const char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
find(const char* path, const char* target)
{
  // TODO: This is a simple implementation and I think it could be optimized more.
  //
  // open the directory using syscall open
  // check if the file is a directory
  // iterate over directory entry in the file
  // call find() on every directory entry except itself and its parent
  // find() print out all paths of files whose names are `name`
  
  // printf("call find, path: %s, target: %s\n", path, target);


  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("Failed to open a directory. path: %s\n", path);
    return;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    printf("Failed to call `fstat` on fd %i\n", fd);
    close(fd);
    return;
  }

  int pathlen = strlen(path);


  // directory entry in a directory file
  // a directory is just a list of entries pointing to subdirectories/files
  struct dirent de;
  switch (st.type) {
  case T_DEVICE:
  case T_FILE:
  {
    const char* fname = fmtname(path);
    if (memcmp(fname, target, strlen(target)) == 0) {
      printf("%s\n", path);
    } 
    break;
  }
  case T_DIR:
  {
    char subdir[512];
    memmove(subdir, path, pathlen);
    subdir[pathlen++] = '/';

    while (read(fd, &de, sizeof de) == sizeof(de)) {
      if (de.inum == 0 || strcmp(de.name, "..") == 0 || strcmp(de.name, ".") == 0) {
        // skip parent directory and itself 
        continue;
      }

      int subdirlen = pathlen;
      int namelen = strlen(de.name);
      memmove(&subdir[subdirlen], de.name, namelen);
      subdirlen += namelen;
      subdir[subdirlen++] = 0;

      find(subdir, target);
    }
  }
  }
  close(fd);
}

int
main(int argc, char* argv[])
{
  if (argc != 3) {
    printf("Usage: find [directory] [file_name]\n");
    exit(1);
  }

  find(argv[1], argv[2]);
}
