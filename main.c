#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>

// Find current mapped regions using maps
// Iterate through those regions and use process_vm_readv() to read them
// Use memchr to find portion of memory
// Store them and later perform multiple searches to restric them
// Allow the user to write them

int main(int argc, char **argv) {
  printf("Input the PID to start>  ");

  int user_input_pid = -1;
  int result = scanf("%d", &user_input_pid);

  if (result != 1) {
    printf("\nInvalid input.\n");
    return 1;
  }

  ssize_t nread;
  struct iovec local[2];
  struct iovec remote[1];
  char          buf1[10];
  char          buf2[10];
  pid_t         pid = (pid_t) user_input_pid;    /* PID of remote process */
  local[0].iov_base = buf1;
  local[0].iov_len = 10;
  local[1].iov_base = buf2;
  local[1].iov_len = 10;
  remote[0].iov_base = (void *)0x102000;
  remote[0].iov_len = 20;


  nread = process_vm_readv(pid, local, 2, remote, 1, 0);
  if (nread == -1) {
      printf("process_vm_readv failed: %s\n", strerror(errno));
  }

  return 0;
}