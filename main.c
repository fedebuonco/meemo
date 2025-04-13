#include <assert.h>
#include <bits/posix1_lim.h>
#include <bits/types/struct_iovec.h>
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
#define DEBUG_LEVEL 4  // 0=NONE, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG

#define LOG_ERROR   1
#define LOG_WARN    2
#define LOG_INFO    3
#define LOG_DEBUG   4

#define DEBUG_PRINT(level, x) do { if ((level) <= DEBUG_LEVEL) printf x; } while (0)

typedef struct {
    struct iovec* data;
    size_t size;
    size_t capacity;
} DIA;

typedef enum { Search, Write, Quit } CMD;

DIA* init_iovec_array(size_t initial_capacity) {
    DIA* arr = malloc(sizeof(DIA));
    arr->size = 0;
    arr->capacity = initial_capacity;
    arr->data = (struct iovec*)calloc(arr->capacity, sizeof(struct iovec));
    if (!arr->data) {
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

void print_dia(const DIA* arr, size_t max_elem) {
    // if (arr == NULL || arr->data == NULL) {
    //     printf("\nEmpty");
    //     return;
    // }
    printf("\nDIA of %d Elements", (int)arr->size);
    size_t min = max_elem <= arr->size ? max_elem : arr->size;
    for (int i = 0; i < min; i++) {  // Fixed off-by-one error
        printf("\n[%d] = ", i);
        print_iovec(&arr->data[i]);
    }
    if (min < arr->size) {
        printf("\nAnd other %zu Elements", (int)arr->size - min);
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
            if (val == *searched) {
                // DEBUG_PRINT(LOG_DEBUG, ("\nComparing %d with %d at \nLOCAL: %p and at  REMOTE %p", *searched, val, p+i, p_remote + i ));
                // printf("\nFound  %d at %p in local, region number %d",
                //        *searched, p + i, n_regions);
                // printf("\nFound  %d at %p in remote, region number %d",
                //        *searched, p_remote + i, n_regions);

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

typedef struct {
    DIA* local;
    DIA* remote;
    DIA* next_local;
    DIA* next_remote;
    SearchDataType type;
    void* searched;
    pid_t pid;
    FILE* file;
} SearchState;

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

    DEBUG_PRINT(LOG_DEBUG, ("\nReading from PID: %d",pid));
    DEBUG_PRINT(LOG_DEBUG, ("\nReading from regions: %zu into regions:%zu",rdia->size, ldia->size));
    size_t total_iov_len_remote = 0;
    size_t total_iov_len_local = 0;
    for(size_t i = 0; i < rdia->size ; i++){
        total_iov_len_remote+=rdia->data[i].iov_len;
        total_iov_len_local+=ldia->data[i].iov_len;
    }
    
    DEBUG_PRINT(LOG_DEBUG, ("\ntotal_iov_len_remote: %zu and total_iov_len_local: %zu",total_iov_len_remote, total_iov_len_local));
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

ssize_t write_to_remote_dia(pid_t pid, DIA* ldia, DIA* rdia) {

    ssize_t nwrite = process_vm_writev(pid, ldia->data, ldia->size, rdia->data,
                                       rdia->size, 0);
    if (nwrite <= 0) {
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
    return nwrite;
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

void cmd_loop(SearchState* sstate);

void advance_state(SearchState* sstate) {
    // Free the remote and the local
    // Advance
    // printf("\nReplacing the current local ");
    // print_dia(sstate->local);
    // printf("\nWith the next  local ");
    // print_dia(sstate->next_local);
    // printf("\nReplacing the current remote ");
    // print_dia(sstate->remote);
    // printf("\nWith the next  remote ");
    // print_dia(sstate->next_remote);

    free_iovec_array(sstate->remote);
    free_iovec_array(sstate->local);

    sstate->remote = sstate->next_remote;
    sstate->local =  init_iovec_array(sstate->remote->size);

    sstate->next_remote = NULL;
    sstate->next_local = NULL;
}

void reset_current_state(SearchState* sstate) {
    free_iovec_array(sstate->local);

    sstate->local =  init_iovec_array(sstate->remote->size);
    
    sstate->next_remote = NULL;
    sstate->next_local = NULL;
}

void search_step_dia(SearchState* sstate) {
    // Takes in a state with remote filled.
    // local is init but no iovecs,
    // the remotes are null.
    //
    // Init the next_remote.
    // read from remote to local.
    // search on the local and update the next remote
    // create the next local
    // advance state

    fprintf(stderr, "\n\n");
    // fprintf(stderr, "DEBUG: search_step_dia called\n");
    // fprintf(stderr, "PID: %d, len: %zu, search_type: %d\n", pid, len,
    //         search_type);
    // print_dia(sstate->remote);

    // Create the dia for the next search step
    sstate->next_remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);

    // Switch on type
    switch (sstate->type) {
        case TYPE_UINT_32:
            printf("\nStarting search for TYPE_UINT_32:  %d",
                   *(uint32_t*)sstate->searched);
            ssize_t read = read_from_remote_dia(sstate->pid, sstate->local,
                                                sstate->remote);
            search_step_for_uint32_dia(
                sstate->local, sstate->remote, sstate->next_remote,
                sstate->remote->size, (uint32_t*)sstate->searched);
            break;
        default:
            printf("\nUnknown type");
    }

    // If not found anything go to next step
    if (sstate->next_remote->size == 0) {
        printf("\nNo result for the searched.");
        reset_current_state(sstate);
        cmd_loop(sstate);
    }

    // // Present them and ask which to write
    // if (sstate->next_remote->size <= WRITE_ASK) {
    //     print_dia(sstate->next_remote);
    //     getchar();
    // }

    printf("\nPreparing for next step...");
    // ssize_t read =
    //     read_from_remote_dia(sstate->pid, sstate->next_local, sstate->next_remote);

    // Uncomment for printing
    // print_dia(sstate->next_remote);

    // print_memory_hex_from_dia(&next_local, &next_remote_iov_array, 16);

    advance_state(sstate);
    cmd_loop(sstate);
}

void print_search_state(SearchState* sstate) {
    printf("\nCurrent Search State: ");
    printf("\nRemote has currently size of %zu", sstate->remote->size);
    printf("\nPrint Remote...");
    print_dia(sstate->remote, 10);
    printf("\nLocal has currently size of %zu", sstate->local->size);
    printf("\nPrint Local...");
    print_dia(sstate->local, 10);
    if (sstate->next_remote != NULL) {
        printf("\nPrint Next Remote...");
        print_dia(sstate->next_remote, 10);
    }
    if (sstate->next_local != NULL) {
        printf("\nPrint Next Local...");
        print_dia(sstate->next_local, 10);
    }
    printf("\nThe searched is %d", *(int*)sstate->searched);
    printf("\nEnd of Search State.");
}

void cmd_loop(SearchState* sstate) {
    char cmd[25];
    while (1) {
        printf("\n\nMEMMO> ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            continue;
        }
        // printf("\nInput was: %s", cmd);
        char cmd_letter = cmd[0];
        char* rest = (strlen(cmd) > 1) ? &cmd[1] : NULL;
        switch (cmd_letter) {
            case 'p':  // s value
                print_search_state(sstate);
                break;
            case 's':
                printf("\nSearching %s",
                       rest);  // If searched not specified already
                uint32_t* searched_ptr = malloc(sizeof(uint32_t));
                *searched_ptr = strtoul(rest, NULL, 10);
                sstate->searched = searched_ptr;
                search_step_dia(sstate);
                break;
            case 'w':  //w pointer value
                printf("\nProvide remote pointer: ");
                char write_ptr_str[25];
                if (fgets(write_ptr_str, sizeof(write_ptr_str), stdin) ==
                    NULL) {
                    continue;
                }
                void* write_ptr;
                sscanf(write_ptr_str, "%p", &write_ptr);
                printf("\nProvide value: ");
                char write_value[25];
                if (fgets(write_value, sizeof(write_value), stdin) == NULL) {
                    continue;
                }
                uint32_t written_int = strtoul(write_value, NULL, 10);
                DIA* temp_write = init_iovec_array(1);
                struct iovec temp = {&written_int, sizeof(written_int)};
                add_iovec(temp_write, temp);
                DIA* temp_remote = init_iovec_array(1);
                struct iovec temp_r = {write_ptr, sizeof(written_int)};
                add_iovec(temp_remote, temp_r);
                write_to_remote_dia(sstate->pid, temp_write, temp_remote);
                break;
            case 'q':  //quit
                printf("\nQuitting...");
                exit(0);
                break;
            case 'h':  //quit
                printf(
                    "\ns value_to_search\nw pointer value_to_write\nq quit.");
                break;
            default:
                printf("\nCommand not recognized");
                break;
        }
    }
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

    // ssize_t nread = read_from_remote_dia(user_input_pid, local, remote);
    // printf("\nRead %zd bytes from %d regions.", nread, n);

    // Uncomment for printing
    // printf("\nPress ENTER to continue... ");
    // getchar();
    // print_memory_hex_from_dia(&local, &remote,16);

    SearchDataType type = TYPE_UINT_32;
    DIA* remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
    int n = fill_remote_dia(remote, file);
    DIA* local = init_iovec_array(n);
    DIA* next_remote = NULL;
    DIA* next_local = NULL;

    SearchState initial_sstate = {
        local, remote, next_local,     next_remote,
        type,  NULL,   user_input_pid, file,
    };

    cmd_loop(&initial_sstate);

    fclose(file);
    return 0;
}
