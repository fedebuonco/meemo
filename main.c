#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_LINES 500
#define MAX_STR_LEN 500

void not_implemented() {
  fprintf(stderr, "Error: Not implemented\n");
  exit(EXIT_FAILURE);
}

void search_mem_for_uint32(const void* local_mem, size_t len, uint32_t * searched) {  
  const unsigned char *p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(uint32_t); i++) {
      uint32_t val;
      memcpy(&val, p + i, sizeof(uint32_t)); // Safe memory access
      // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
      if (val == *searched) {
        printf("Found  %d at %p\n", *searched, p+i);
        return;
      }
  }
  printf("Not Found  %d\n",*searched);
}

void search_mem_for_int32(const void* local_mem, size_t len, int32_t * searched) {  
  const unsigned char *p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(int32_t); i++) {
      int32_t val;
      memcpy(&val, p + i, sizeof(int32_t)); // Safe memory access
      // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
      if (val == *searched) {
        printf("Found  %d at %p\n", *searched, p+i);
        return;
      }
  }
  printf("Not Found  %d\n",*searched);
}

void search_mem_for_int64(const void* local_mem, size_t len, int64_t * searched) {  
  const unsigned char *p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(int64_t); i++) {
      int64_t val;
      memcpy(&val, p + i, sizeof(int64_t)); // Safe memory access
      // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
      if (val == *searched) {
        printf("Found  %ld at %p\n", *searched, p+i);
        return;
      }
  }
  printf("Not Found  %ld\n",*searched);
}

void search_mem_for_uint64(const void* local_mem, size_t len, uint64_t * searched) {  
  const unsigned char *p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(uint64_t); i++) {
      uint64_t val;
      memcpy(&val, p + i, sizeof(uint64_t)); // Safe memory access
      // printf("Comparing %lu with  %lu at %p\n",val,  *searched, p+i);
      if (val == *searched) {
        printf("Found  %lu at %p\n", *searched, p+i);
        return;
      }
  }
  printf("Not Found  %lu\n",*searched);
}

typedef enum {
  TYPE_INT_32,
  TYPE_INT_64,
  TYPE_UINT_32,
  TYPE_UINT_64,
  TYPE_CHAR
} SearchDataType;

void search_start(const void* local_mem, size_t len, void* searched,
                  SearchDataType search_type) {
  // Add dynamic struct for saving state and pass it to the search
  // Each search will further filter this state
  // at each iteration we give the ability to the user to stop the search and investigate
  // the current findings
  switch (search_type) {
    case TYPE_INT_32:
      printf("Starting search for TYPE_INT_32 : %d\n", *(int32_t*)searched);
      search_mem_for_int32(local_mem, len, (int32_t *) searched);
      break;
    case TYPE_INT_64:
      printf("Starting search for TYPE_INT_64:  %ld\n", *(int64_t *)searched);
      search_mem_for_int64(local_mem, len, (int64_t *) searched);
      break;
    case TYPE_UINT_32:
      printf("Starting search for TYPE_UINT_32:  %d\n", *(uint32_t *)searched);
      search_mem_for_uint32(local_mem, len, (uint32_t *) searched);
      break;
    case TYPE_UINT_64:
      printf("Starting search for TYPE_UINT_64:  %lu\n", *(uint64_t*)searched);
      search_mem_for_uint64(local_mem, len, (uint64_t *) searched);
      break;
    default:
      printf("Unknown type\n");
  }
}

void print_memory_hex(const void* local_mem, const void* remote_mem, size_t len,
                      int bytes_per_line) {
  const unsigned char* p = (const unsigned char*)local_mem;
  const unsigned char* p_remote = (const unsigned char*)remote_mem;

  char current_line[bytes_per_line + 1];

  for (size_t i = 0; i < len; i++) {
    if (i % bytes_per_line == 0) {
      if (i != 0) {  // Skip first line
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

int fill_remote_iovec(struct iovec* remote, FILE* file) {
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
    remote[i].iov_base = (void*)start;
    remote[i].iov_len = end - start;

    i++;
  }
  return i;
}

int main(int argc, char** argv) {
  char* p;
  pid_t user_input_pid = (pid_t)strtol(argv[1], &p, 10);
  printf("Input PID = %d\n", user_input_pid);
  char path[50];
  int rn = sprintf(path, "/proc/%d/maps", user_input_pid);
  printf("Opening %s ...\n", path);
  FILE* file = fopen(path, "r");
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
    // Search
    SearchDataType type = TYPE_UINT_32;
    uint32_t searched = 80085;
    search_start(local[i].iov_base, local[i].iov_len, (void *)  &searched, type);
    getchar();
    type = TYPE_UINT_64;
    uint64_t searched_64 = 1337;
    search_start(local[i].iov_base, local[i].iov_len, (void *)  &searched_64, type);
    getchar();
    print_memory_hex(local[i].iov_base, remote[i].iov_base, local[i].iov_len,
                     16);
  }
  fclose(file);
  return 0;
}
