#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_LINES 500
#define MAX_STR_LEN 500

void print_memory_hex(const void *local_mem, const void *remote_mem, size_t len, int bytes_per_line) {
  const unsigned char *p = (const unsigned char *)local_mem;
  const unsigned char *p_remote = (const unsigned char *)remote_mem;

  char current_line[bytes_per_line + 1];

  for (size_t i = 0; i < len; i++) {
      if (i % bytes_per_line == 0) {
          if (i != 0) { // Skip first line
              printf("  %s", current_line);
          }
          printf("\n%p: ", p_remote + i);
      }
      printf("%02x ", p[i]);
      // .' for non-printable TODO common?
      current_line[i % bytes_per_line] = isprint(p[i]) ? p[i] : '.';
      current_line[(i % bytes_per_line) + 1] = '\0';
  }

  // Print last ASCII 
  size_t remainder = len % bytes_per_line;
  if (remainder > 0) {
      for (size_t i = 0; i < (bytes_per_line - remainder); i++) {
          printf("   ");  // 3 spaces to align with "%02x "
      }
      printf("  %s", current_line);
  }

  printf("\n");
}

int fill_remote_iovec(struct iovec *remote, FILE *file) {
  char address_range[MAX_STR_LEN], perms[MAX_STR_LEN], pathname[MAX_STR_LEN];
  unsigned long offset;
  int dev_major, dev_minor, inode;
  int i = 0;
  while (i < MAX_LINES &&
         fscanf(file, "%s %s %lx %d:%d %d %[^\n]", address_range, perms,
                &offset, &dev_major, &dev_minor, &inode, pathname) >= 4) {
    printf("Filling remote[%d]...\n", i);

    uintptr_t start, end;
    sscanf(address_range, "%lx-%lx", &start, &end);
    printf("Start: 0x%lx\n", start);
    printf("End: 0x%lx\n", end);

    ssize_t len = end - start;
    printf("Length: 0x%zx\n", len);
    remote[i].iov_base = (void *)start;
    remote[i].iov_len = end - start;

    i++;
  }
  return i;
}

int main(int argc, char **argv) {
  char *p;
  pid_t user_input_pid = (pid_t)strtol(argv[1], &p, 10);
  printf("Input PID = %d\n", user_input_pid);
  char path[50];
  int rn = sprintf(path, "/proc/%d/maps", user_input_pid);
  printf("Opening %s ...\n", path);
  FILE *file = fopen(path, "r");
  if (!file) {
    perror("Error opening file");
    return 1;
  }
  struct iovec remote[500];
  int n = fill_remote_iovec(remote, file);
  struct iovec local[n];
  // Build local iovec structs
  for (int i = 0; i < n; i++) {
    local[i].iov_base = calloc(remote[i].iov_len, sizeof(char));
    local[i].iov_len = remote[i].iov_len;
  }

  ssize_t nread = process_vm_readv(user_input_pid, local, n, remote, n, 0);
  if (nread < 0) {
    switch (errno) {
    case EINVAL:
      printf("ERROR: Invalid arguments.\n");
      break;
    case EFAULT:
      printf("ERROR: Unable to access target memory.\n");
      break;
    case ENOMEM:
      printf("ERROR: Memory allocation failed.\n");
      break;
    case EPERM:
      printf("ERROR: Insufficient privileges.\n");
      break;
    case ESRCH:
      printf("ERROR: Process does not exist.\n");
      break;
    default:
      printf("ERROR: Unknown error occurred.\n");
    }
    return 1;
  }

  printf("Read %zd bytes from %d regions.\n", nread, n);

    // Print memory in hex
    for (int i = 0; i < n; i++) {
      printf("\nRegion %d (size: %zd bytes):", i, local[i].iov_len);
      printf("\nPress to print region...");
      getchar();
      print_memory_hex(local[i].iov_base,remote[i].iov_base, local[i].iov_len,16);
   }
  fclose(file);
  return 0;
}
