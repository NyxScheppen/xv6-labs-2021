#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2) {
    printf("error: argument required\n");
    exit(1);
  } 

  sleep(atoi(argv));
  exit(0);
}
