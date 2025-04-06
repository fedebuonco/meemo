#include <assert.h>
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
#define INITIAL_IOVEC_ARRAY_CAP 128

typedef struct {
  struct iovec* data;
  size_t size;
  size_t capacity;
} DIA;

DIA init_iovec_array(size_t initial_capacity) {
  DIA arr;
  arr.size = 0;
  arr.capacity = initial_capacity;
  arr.data = (struct iovec*)calloc(arr.capacity, sizeof(struct iovec));
  if (!arr.data) {
    perror("Failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  return arr;
}

void add_iovec(DIA* arr, struct iovec value) {
  if (arr->size == arr->capacity) {
    arr->capacity *= 2;
    struct iovec* new_data =
        realloc(arr->data, arr->capacity * sizeof(struct iovec));
    if (!new_data) {
      perror("Failed to reallocate memory");
      exit(EXIT_FAILURE);
    }
    arr->data = new_data;
  }

  arr->data[arr->size++] = value;
}

void free_iovec_array(DIA* arr) {
  free(arr->data);
  arr->data = NULL;
  arr->size = 0;
  arr->capacity = 0;
}

void print_iovec(const struct iovec* io) {
  printf("\n\nIOVEC BASE %p", io->iov_base);
  printf("\nIOVEC LEN %d", (int)io->iov_len);
}

void print_dia(const DIA* arr) {
  printf("\nDIA of %d Elements", (int)arr->size);
  for (int i = 0; i < arr->size; i++) {  // Fixed off-by-one error
    print_iovec(&arr->data[i]);
  }
}

void search_step_for_uint32(const void* local_mem, const void* remote_mem,
                            DIA* next_remote_iov_array, size_t len,
                            uint32_t* searched) {
  const unsigned char* p = (const unsigned char*)local_mem;
  const unsigned char* p_remote = (const unsigned char*)remote_mem;

  for (size_t i = 0; i <= len - sizeof(uint32_t); i++) {
    uint32_t val;
    memcpy(&val, p + i, sizeof(uint32_t));  // Safe memory access
    if (val == *searched) {
      printf("\nFound  %d at %p in local.", *searched, p + i);
      printf("\nFound  %d at %p in remote.", *searched, p_remote + i);

      // I now fill the next remote iovec so that it will be used to
      // get and read the data for the next
      struct iovec next_remote_iov = {.iov_base = p_remote + i,
                                      .iov_len = sizeof(int32_t)};

      add_iovec(next_remote_iov_array, next_remote_iov);
    }
  }
  printf("Not Found  %d\n", *searched);
}

void search_step_for_uint32_dia(DIA* local_mem, DIA* remote_mem,
                                DIA* next_remote_iov_array, size_t len,
                                uint32_t* searched) {
  for (int nr = 0; nr <= len; nr++) {
    const unsigned char* p = (const unsigned char*)local_mem->data;
    const unsigned char* p_remote = (const unsigned char*)remote_mem->data;

    for (size_t i = 0; i <= -sizeof(uint32_t); i++) {
      uint32_t val;
      memcpy(&val, p + i, sizeof(uint32_t));  // Safe memory access
      printf("\nComparing  %d\n", val);
      if (val == *searched) {
        printf("\nFound  %d at %p in local.", *searched, p + i);
        printf("\nFound  %d at %p in remote.", *searched, p_remote + i);

        // I now fill the next remote iovec so that it will be used to
        // get and read the data for the next
        struct iovec next_remote_iov = {.iov_base = p_remote + i,
                                        .iov_len = sizeof(int32_t)};

        add_iovec(next_remote_iov_array, next_remote_iov);
      }
    }
    printf("Not Found  %d\n", *searched);
  }
}

typedef enum {
  TYPE_INT_32,
  TYPE_INT_64,
  TYPE_UINT_32,
  TYPE_UINT_64,
  TYPE_CHAR
} SearchDataType;

ssize_t read_from_remote_dia(pid_t pid, DIA* ldia, DIA* rdia) {
  // Use remote size to allocate local
  for (int i = 0; i < rdia->size; i++) {
    void* buffer = malloc(rdia->data[i].iov_len);
    if (!buffer) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
    struct iovec local_iov = {.iov_base = buffer,
                              .iov_len = rdia->data[i].iov_len};

    add_iovec(ldia, local_iov);
  }

  ssize_t nread =
      process_vm_readv(pid, ldia->data, ldia->size, rdia->data, rdia->size, 0);
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

ssize_t read_from_remote(pid_t pid, struct iovec* lvec, size_t ln,
                         struct iovec* rvec, size_t rn) {
  ssize_t nread = process_vm_readv(pid, lvec, ln, rvec, rn, 0);
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

void search_step_dia(DIA* local_dia, DIA* remote_dia, size_t len,
                     void* searched, SearchDataType search_type) {
  // If searched not specified already
  if (searched == NULL) {
    char searched_str[MAX_STR_LEN];
    printf("\nProvide a searched: ");
    scanf("%s", searched_str);
    uint32_t searched_int = strtoul(searched_str, NULL, 10);
    searched = (void*)&searched_int;
    assert(searched_int == 80085);
  }
  // Create the dia for the next search step
  DIA next_remote_iov_array = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
  switch (search_type) {
    case TYPE_UINT_32:
      printf("\nStarting search for TYPE_UINT_32:  %d\n", *(uint32_t*)searched);
      search_step_for_uint32_dia(local_dia, remote_dia, &next_remote_iov_array,
                                 len, (uint32_t*)searched);
      break;
    default:
      printf("Unknown type\n");
  }
  // Now read again
  if (next_remote_iov_array.size == 0) {
    return;
  }
  print_dia(&next_remote_iov_array);
  DIA next_local_array = init_iovec_array(next_remote_iov_array.size);
  for (int i = 0; i < next_remote_iov_array.size; i++) {
    void* buffer = malloc(next_remote_iov_array.data[i].iov_len);
    if (!buffer) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
    // Initialize the local iovec entry
    struct iovec local_iov = {.iov_base = buffer,
                              .iov_len = next_remote_iov_array.data[i].iov_len};

    add_iovec(&next_local_array, local_iov);
  }
  search_step_dia(&next_local_array, &next_remote_iov_array,
                  next_remote_iov_array.size, searched, search_type);
}

void search_step(const void* local_mem, const void* remote_mem, size_t len,
                 void* searched, SearchDataType search_type) {
  if (searched == NULL) {
    char searched_str[MAX_STR_LEN];
    printf("\nProvide a searched: ");
    scanf("%s", searched_str);
    uint32_t searched_int = strtoul(searched_str, NULL, 10);
    searched = (void*)&searched_int;
  }
  DIA next_remote_iov_array = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
  switch (search_type) {
    case TYPE_UINT_32:
      printf("\nStarting search for TYPE_UINT_32:  %d\n", *(uint32_t*)searched);
      search_step_for_uint32(local_mem, remote_mem, &next_remote_iov_array, len,
                             (uint32_t*)searched);
      break;
    default:
      printf("Unknown type\n");
  }
  // Now read again
  if (next_remote_iov_array.size == 0) {
    return;
  }
  print_dia(&next_remote_iov_array);
  DIA next_local_array = init_iovec_array(next_remote_iov_array.size);
  for (int i = 0; i < next_remote_iov_array.size; i++) {
    void* buffer = malloc(next_remote_iov_array.data[i].iov_len);
    if (!buffer) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
    // Initialize the local iovec entry
    struct iovec local_iov = {.iov_base = buffer,
                              .iov_len = next_remote_iov_array.data[i].iov_len};

    add_iovec(&next_local_array, local_iov);
  }
  search_step(next_local_array.data, next_remote_iov_array.data,
              next_remote_iov_array.size, searched, search_type);
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

void print_memory_hex_from_dia(const DIA* local, const DIA* remote,
                               int bytes_per_line) {
  for (int n_regions = 0; n_regions < remote->size; n_regions++) {
    printf("\nRegion %d (size: %zd bytes):", n_regions,
           local->data[n_regions].iov_len);
    printf("\nPress to print region...");
    getchar();

    const unsigned char* p =
        (const unsigned char*)local->data[n_regions].iov_base;
    const unsigned char* p_remote =
        (const unsigned char*)remote->data[n_regions].iov_base;

    char current_line[bytes_per_line + 1];

    for (size_t i = 0; i < local->data[n_regions].iov_len; i++) {
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
    size_t remainder = local->data[n_regions].iov_len % bytes_per_line;
    if (remainder > 0) {
      for (size_t i = 0; i < (bytes_per_line - remainder); i++) {
        printf("   ");  // 3 spaces to align with "%02x "
      }
      printf("  %s", current_line);
    }

    printf("\n");
  }
}

int fill_remote_iovec(DIA* remote, FILE* file) {
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
    temp.iov_base = (void*)start;
    temp.iov_len = end - start;
    add_iovec(remote, temp);

    i++;
  }
  return i;
}

int main(int argc, char** argv) {
  char* p;
  pid_t user_input_pid = (pid_t)strtol(argv[1], &p, 10);
  printf("Input PID = %d\n", user_input_pid);
  char path[50];
  sprintf(path, "/proc/%d/maps", user_input_pid);
  printf("Opening %s ...\n", path);
  FILE* file = fopen(path, "r");
  if (!file) {
    perror("Error opening file");
    return 1;
  }
  // Create the two iovec for local and remote
  DIA remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
  int n = fill_remote_iovec(&remote, file);
  DIA local = init_iovec_array(n);
  ssize_t nread = read_from_remote_dia(user_input_pid, &local, &remote);
  printf("Read %zd bytes from %d regions.\n", nread, n);

  // Uncomment for printing
  // printf("Press ENTER to continue... \n");
  // getchar();
  // print_memory_hex_from_dia(&local, &remote,16);

  SearchDataType type = TYPE_UINT_32;
  search_step_dia(&local, &remote, n, NULL, type);

  fclose(file);
  return 0;
}
