#include <stddef.h>
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

typedef struct {
  struct iovec * data;
  size_t size;
  size_t capacity;
}dynamic_iovec_array;

#define INITIAL_IOVEC_ARRAY_CAP 128

dynamic_iovec_array init_iovec_array(size_t initial_capacity){
  dynamic_iovec_array arr;
  arr.size = 0;
  arr.capacity = initial_capacity;
  arr.data = (struct iovec *) calloc(arr.capacity, sizeof(struct iovec));
  if (!arr.data) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
  }
  return arr;   
}

void add_iovec(dynamic_iovec_array *arr, struct iovec value) {
  if (arr->size == arr->capacity) {
    arr->capacity *= 2;  
    struct iovec *new_data = realloc(arr->data, arr->capacity * sizeof(struct iovec));
    if (!new_data) {
      perror("Failed to reallocate memory");
      exit(EXIT_FAILURE);
    }
    arr->data = new_data;
  }
    
  arr->data[arr->size++] = value;
}

void free_iovec_array(dynamic_iovec_array * arr) {
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

void print_iovec(const struct iovec *io) {
  printf("\n\nIOVEC BASE %p", io->iov_base);
  printf("\nIOVEC LEN %d", (int)io->iov_len);
}

void print_dia(const dynamic_iovec_array *arr) {
  printf("\nDIA of %d Elements", (int)arr->size);
  for (int i = 0; i < arr->size; i++) {  // Fixed off-by-one error
    print_iovec(&arr->data[i]);
  }
}


void search_step_for_uint32(const void* local_mem, const void* remote_mem, dynamic_iovec_array * next_remote_iov_array, size_t len,
                            uint32_t* searched) {
  const unsigned char* p = (const unsigned char*)local_mem;
  const unsigned char* p_remote = (const unsigned char*)remote_mem;

  for (size_t i = 0; i <= len - sizeof(uint32_t); i++) {
    uint32_t val;
    memcpy(&val, p + i, sizeof(uint32_t));  // Safe memory access
    // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
    if (val == *searched) {
      printf("\nFound  %d at %p in local.", *searched, p + i);
      printf("\nFound  %d at %p in remote.", *searched, p_remote + i);

      // I now fill the next remote iovec so that it will be used to
      // get and read the data for the next 
      struct iovec next_remote_iov = {
        .iov_base = p_remote+i,
        .iov_len = sizeof(int32_t)
      };

    // Add to the next_remote_iov array
    printf("\nAdding");
    getchar();
    add_iovec(next_remote_iov_array, next_remote_iov);

      return;
    }
  }
  printf("Not Found  %d\n", *searched);
}

void search_step_for_int32(const void* local_mem, const void* remote_mem, dynamic_iovec_array * next_remote_iov_array, size_t len,
                           int32_t* searched) {
  const unsigned char* p = (const unsigned char*)local_mem;
  const unsigned char* p_remote = (const unsigned char*)remote_mem;
  for (size_t i = 0; i <= len - sizeof(int32_t); i++) {
    int32_t val;
    memcpy(&val, p + i, sizeof(int32_t));  // Safe memory access
    // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
    if (val == *searched) {
      printf("\nFound  %d at %p in local.", *searched, p + i);
      printf("\nFound  %d at %p in remote.", *searched, p_remote + i);

      // I now fill the next remote iovec so that it will be used to
      // get and read the data for the next 
      struct iovec next_remote_iov = {
        .iov_base = (void *) p_remote+i,
        .iov_len = sizeof(int32_t)
      };

    // Add to the next_remote_iov array
    printf("\nAdding");
    getchar();
    add_iovec(next_remote_iov_array, next_remote_iov);

      return;
    }
  }
  printf("Not Found  %d\n", *searched);
}

void search_step_for_int64(const void* local_mem, size_t len,
                           int64_t* searched) {
  const unsigned char* p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(int64_t); i++) {
    int64_t val;
    memcpy(&val, p + i, sizeof(int64_t));  // Safe memory access
    // printf("Comparing %d with  %d at %p\n",val,  *searched, p+i);
    if (val == *searched) {
      printf("Found  %ld at %p\n", *searched, p + i);
      getchar();
      return;
    }
  }
  printf("Not Found  %ld\n", *searched);
}

void search_step_for_uint64(const void* local_mem, size_t len,
                            uint64_t* searched) {
  const unsigned char* p = (const unsigned char*)local_mem;
  for (size_t i = 0; i <= len - sizeof(uint64_t); i++) {
    uint64_t val;
    memcpy(&val, p + i, sizeof(uint64_t));  // Safe memory access
    // printf("Comparing %lu with  %lu at %p\n",val,  *searched, p+i);
    if (val == *searched) {
      printf("Found  %lu at %p\n", *searched, p + i);
      getchar();
      return;
    }
  }
  printf("Not Found  %lu\n", *searched);
}

typedef enum {
  TYPE_INT_32,
  TYPE_INT_64,
  TYPE_UINT_32,
  TYPE_UINT_64,
  TYPE_CHAR
} SearchDataType;



// // Finds the value in the local memory, then uses the old remote memory to get the
// // address and creates the next step remote.
// // This next step will be ready to be used for another read into local memory.
// dynamic_iovec_array search_result_remote(const void* local_mem, size_t len, void * value, SearchDataType search_type){
  
// }

ssize_t  read_from_remote(pid_t pid, struct iovec * lvec, size_t ln, struct iovec * rvec, size_t rn){
  ssize_t nread = process_vm_readv(pid,lvec, ln, rvec, rn, 0);
  if (nread <= 0) {
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
  return nread;
}

void search_step(const void* local_mem, const void* remote_mem, size_t len, void* searched,
                 SearchDataType search_type) {
  // Add dynamic struct for saving state and pass it to the search
  // Each search will further filter this state
  // at each iteration we give the ability to the user to stop the search and investigate
  // the current findings
  dynamic_iovec_array next_remote_iov_array = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
  switch (search_type) {
    case TYPE_INT_32:
      printf("Starting search for TYPE_INT_32 : %d\n", *(int32_t*)searched);
      search_step_for_int32(local_mem, remote_mem, &next_remote_iov_array, len, (int32_t*)searched);
      break;
    case TYPE_INT_64:
      printf("Starting search for TYPE_INT_64:  %ld\n", *(int64_t*)searched);
      search_step_for_int64(local_mem, len, (int64_t*)searched);
      break;
    case TYPE_UINT_32:
      printf("Starting search for TYPE_UINT_32:  %d\n", *(uint32_t*)searched);
      search_step_for_uint32(local_mem, remote_mem, &next_remote_iov_array, len,(uint32_t*)searched);
      break;
    case TYPE_UINT_64:
      printf("Starting search for TYPE_UINT_64:  %lu\n", *(uint64_t*)searched);
      search_step_for_uint64(local_mem, len, (uint64_t*)searched);
      break;
    default:
      printf("Unknown type\n");
  }
  // Now read again
  getchar();
  print_dia(&next_remote_iov_array);
  getchar();


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

int fill_remote_iovec(dynamic_iovec_array * remote, FILE* file) {
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
    // Create the iovec and add it to the dynamic array
    struct iovec temp;
    temp.iov_base = (void*) start;
    temp.iov_len = end - start;
    add_iovec(remote,temp);
   
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
  // Create the two iovec for local and remote
  dynamic_iovec_array remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
  int n = fill_remote_iovec(&remote, file);

  dynamic_iovec_array local = init_iovec_array(n);

  // Use remote size to allocate  
  // TODO this needs to be made a function and to free after that.
  for (int i = 0; i < remote.size; i++) {
    void* buffer = malloc(remote.data[i].iov_len);
    if (!buffer) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    // Initialize the local iovec entry
    struct iovec local_iov = {
        .iov_base = buffer,
        .iov_len = remote.data[i].iov_len
    };

    // Add to the local array
    add_iovec(&local, local_iov);
}

  ssize_t nread =read_from_remote(user_input_pid, local.data, local.size,remote.data, remote.size);

  printf("Read %zd bytes from %d regions.\n", nread, n);

  // Print memory in hex
  for (int i = 0; i < n; i++) {
    printf("\nRegion %d (size: %zd bytes):", i, local.data[i].iov_len);
    printf("\nPress to print region...");
    // Search
    SearchDataType type = TYPE_UINT_32;
    uint32_t searched = 80085;
    search_step(local.data[i].iov_base, remote.data[i].iov_base, local.data[i].iov_len, (void*)&searched, type);
    print_memory_hex(local.data[i].iov_base, remote.data[i].iov_base, local.data[i].iov_len,
                     16);
  }
  fclose(file);
  return 0;
}
