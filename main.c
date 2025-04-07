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
#define WRITE_ASK 10

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
    printf("Base: %p", io->iov_base);
    printf("\tLen: %d", (int)io->iov_len);    
}

void print_dia(const DIA* arr) {
    printf("\nDIA of %d Elements", (int)arr->size);
    for (int i = 0; i < arr->size; i++) {  // Fixed off-by-one error
        printf("\n[%d] = ", i);
        print_iovec(&arr->data[i]);
    }
}

void search_step_for_uint32_dia(DIA* local, DIA* remote,
                                DIA* next_remote_iov_array, size_t len,
                                uint32_t* searched) {
    for (int n_regions = 0; n_regions < len; n_regions++) {
        const unsigned char* p =
            (const unsigned char*)local->data[n_regions].iov_base;
        const unsigned char* p_remote =
            (const unsigned char*)remote->data[n_regions].iov_base;

        for (size_t i = 0;
             i <= local->data[n_regions].iov_len - sizeof(uint32_t); i++) {
            uint32_t val;
            memcpy(&val, p + i, sizeof(uint32_t));  // Safe memory access
            // printf("\nComparing %d with %d at \nLOCAL: %p and at  REMOTE %p", *searched, val, p+i, p_remote + i );
            if (val == *searched) {
                // printf("\nFound  %d at %p in local, region number %d",
                //        *searched, p + i, n_regions);
                printf("\nFound  %d at %p in remote, region number %d",
                       *searched, p_remote + i, n_regions);

                // I now fill the next remote iovec so that it will be used to
                // get and read the data for the next
                struct iovec next_remote_iov = {.iov_base = p_remote + i,
                                                .iov_len = sizeof(int32_t)};

                add_iovec(next_remote_iov_array, next_remote_iov);
            }
        }
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

    ssize_t nread = process_vm_readv(pid, ldia->data, ldia->size, rdia->data,
                                     rdia->size, 0);
    if (nread <= 0) {
        switch (errno) {
            case EINVAL:
                printf("\nERROR: Invalid arguments.");
                break;
            case EFAULT:
                printf("\nERROR: Unable to access target memory.");
                break;
            case ENOMEM:
                printf("\nERROR: Memory allocation failed.");
                break;
            case EPERM:
                printf("\nERROR: Insufficient privileges.");
                break;
            case ESRCH:
                printf("\nERROR: Process does not exist.");
                break;
            default:
                printf("\nERROR: Unknown error occurred.");
        }
        return 1;
    }
    return nread;
}

int fill_remote_dia(DIA* remote, FILE* file) {
    char address_range[MAX_STR_LEN], perms[MAX_STR_LEN], pathname[MAX_STR_LEN];
    unsigned long offset;
    int dev_major, dev_minor, inode;
    int i = 0;
    while (i < MAX_LINES &&
           fscanf(file, "%s %s %lx %d:%d %d %[^\n]", address_range, perms,
                  &offset, &dev_major, &dev_minor, &inode, pathname) >= 4) {
        printf("\nFilling remote[%d]...", i);

        uintptr_t start, end;
        sscanf(address_range, "%lx-%lx", &start, &end);
        printf("\nStart: 0x%lx", start);
        printf("\nEnd: 0x%lx", end);

        ssize_t len = end - start;
        printf("\nLength: 0x%zx", len);
        // Create the iovec and add it to the dynamic array
        struct iovec temp;
        temp.iov_base = (void*)start;
        temp.iov_len = end - start;
        add_iovec(remote, temp);

        i++;
    }
    return i;
}

void print_memory_hex_from_dia(const DIA* local, const DIA* remote,
                               int bytes_per_line) {
    for (int n_regions = 0; n_regions < remote->size; n_regions++) {
        printf("\nRegion %d (size: %zd bytes):", n_regions,
               local->data[n_regions].iov_len);
        printf("\nPress to print region...");

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

void search_step_dia(DIA* local_dia, DIA* remote_dia, size_t len,
                     void* searched, SearchDataType search_type, FILE* file,
                     pid_t pid) {
    fprintf(stderr, "DEBUG: search_step_dia called\n");
    fprintf(stderr, "PID: %d, len: %zu, search_type: %d\n", pid, len,
            search_type);
    print_dia(remote_dia);
    // If searched not specified already
        char searched_str[MAX_STR_LEN];
        printf("\nProvide a searched: ");
        scanf("%s", searched_str);
        uint32_t searched_int = strtoul(searched_str, NULL, 10);
        searched = (void*)&searched_int;

    // Create the dia for the next search step
    DIA next_remote_iov_array = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);

    // Switch on type
    switch (search_type) {
        case TYPE_UINT_32:
            printf("\nStarting search for TYPE_UINT_32:  %d",
                   *(uint32_t*)searched);
            search_step_for_uint32_dia(local_dia, remote_dia,
                                       &next_remote_iov_array, len,
                                       (uint32_t*)searched);
            break;
        default:
            printf("\nUnknown type");
    }

    // Now read again only if found something
    if (next_remote_iov_array.size == 0) {
        printf("\nNo result for the searched.");
        // next step use same dias
        search_step_dia(local_dia, remote_dia,
            remote_dia->size, searched, search_type, file,
            pid);
    }

    // Present them and ask which to write
    if (next_remote_iov_array.size <= WRITE_ASK) {
        print_dia(&next_remote_iov_array);
    }
    
    printf("\nPreparing for next step...");
    DIA next_local = init_iovec_array(next_remote_iov_array.size);
    ssize_t read =
        read_from_remote_dia(pid, &next_local, &next_remote_iov_array);

    // Uncomment for printing
    printf("\nPress ENTER to continue... ");
    print_memory_hex_from_dia(&next_local, &next_remote_iov_array, 16);

    search_step_dia(&next_local, &next_remote_iov_array,
                    next_remote_iov_array.size, searched, search_type, file,
                    pid);
}

int main(int argc, char** argv) {
    char* p;
    pid_t user_input_pid = (pid_t)strtol(argv[1], &p, 10);
    printf("\nInput PID = %d", user_input_pid);
    char path[50];
    sprintf(path, "/proc/%d/maps", user_input_pid);
    printf("\nOpening %s ...", path);
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }
    // Create the two iovec for local and remote
    DIA remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
    int n = fill_remote_dia(&remote, file);
    DIA local = init_iovec_array(n);
    ssize_t nread = read_from_remote_dia(user_input_pid, &local, &remote);
    printf("\nRead %zd bytes from %d regions.", nread, n);

    // Uncomment for printing
    // printf("\nPress ENTER to continue... ");
    // getchar();
    // print_memory_hex_from_dia(&local, &remote,16);

    SearchDataType type = TYPE_UINT_32;
    search_step_dia(&local, &remote, n, NULL, type, file, user_input_pid);

    fclose(file);
    return 0;
}
