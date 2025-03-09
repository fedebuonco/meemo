#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>
#include <stdlib.h>

// Find current mapped regions using maps
// Iterate through those regions and use process_vm_readv() to read them
// Use memchr to find portion of memory
// Store them and later perform multiple searches to restric them
// Allow the user to write them
#define MAX_LINES 500
#define MAX_STR_LEN 500

long int *process_range(char *address_range) {
  long int * current_map = malloc(2 * sizeof(long int));
  char astart[20] = {0};
  char aend[20] = {0};
  int delim_index = -1;
  int i = 0;

  for (; i < strlen(address_range); i++) {
      if (address_range[i] == '-') {
          delim_index = i + 1;
          strncpy(astart, address_range, i);
          astart[i] = '\0';
          break;
      }
  }
  strncpy(aend, address_range + delim_index, i);
  aend[i+1] = '\0';
  current_map[0] = strtol(astart, NULL, 16);
  current_map[1] = strtol(aend, NULL, 16);
  return current_map;
}

int fill_remote_iovec(struct iovec  * remote, FILE *file){
  char address_range[MAX_STR_LEN], perms[MAX_STR_LEN], pathname[MAX_STR_LEN];
  unsigned long offset;
  int dev_major, dev_minor, inode;
  int i = 0;

  while (i < MAX_LINES && fscanf(file, "%49s %4s %lx %d:%d %d %49[^\n]", 
    address_range, perms, &offset, &dev_major, &dev_minor, &inode, pathname) >= 5) {
    int * current_map;
    current_map = process_range(address_range);
    printf("Start %d \n",current_map[0]);
    printf("End %d \n",current_map[1]);
    i++;
    }
    // Return the read mapped regions
    return i;
}

int main(int argc, char **argv) {
  char *p;
  pid_t user_input_pid = (pid_t) strtol(argv[1], &p, 10);
  printf("Input PID = %d\n", user_input_pid);
  char path [50];
  int rn = sprintf(path, "/proc/%d/maps", user_input_pid);
  printf("Opening %s ...\n", path);
  FILE *file = fopen(path,"r");
    if (!file) {
      perror("Error opening file");
      return 1;
  }
  struct iovec remote[50];
  fill_remote_iovec(remote, file);
  fclose(file);
  return 0;
}

