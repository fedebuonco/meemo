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
#define IOV_MAX_BATCH 1024

#define DEBUG_LEVEL 2  // 0=NONE, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG

#define LOG_ERROR 1
#define LOG_WARN 2
#define LOG_INFO 3
#define LOG_DEBUG 4

#define DEBUG_PRINT(level, x)       \
    do {                            \
        if ((level) <= DEBUG_LEVEL) \
            printf x;               \
    } while (0)

typedef struct {
    struct iovec* data;
    size_t size;
    size_t capacity;
} DIA;

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
                DEBUG_PRINT(
                    LOG_DEBUG,
                    ("\nComparing %d with %d at \nLOCAL: %p and at  REMOTE %p",
                     *searched, val, p + i, p_remote + i));
                // I now fill the next remote iovec so that it will be used to
                // get and read the data for the next
                struct iovec next_remote_iov = {.iov_base = p_remote + i,
                                                .iov_len = sizeof(int32_t)};

                add_iovec(next_remote_iov_array, next_remote_iov);
            }
        }
    }
}

ssize_t batch_process_vm_readv(pid_t pid, struct iovec* liovec, size_t ln,
                               struct iovec* riovec, size_t rn) {
    ssize_t total_read = 0;
    size_t offset = 0;

    while (offset < ln && offset < rn) {
        size_t batch =
            (ln - offset < IOV_MAX_BATCH) ? (ln - offset) : IOV_MAX_BATCH;
        if (rn - offset < batch)
            batch = rn - offset;

        printf("\nReading batch: offset=%zu, batch=%zu", offset, batch);

        ssize_t nread = process_vm_readv(pid, liovec + offset, batch,
                                         riovec + offset, batch, 0);

        if (nread == -1) {
            fprintf(stderr, "\nprocess_vm_readv failed at offset %zu: %s",
                    offset, strerror(errno));
            break;
        }

        printf("\nBytes read in batch: %zd", nread);
        total_read += nread;
        offset += batch;
    }

    printf("\nTotal bytes read: %zd", total_read);
    return total_read;
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

    DEBUG_PRINT(LOG_DEBUG, ("\nReading from PID: %d", pid));
    DEBUG_PRINT(LOG_DEBUG, ("\nReading from regions: %zu into regions:%zu",
                            rdia->size, ldia->size));
    size_t total_iov_len_remote = 0;
    size_t total_iov_len_local = 0;
    for (size_t i = 0; i < rdia->size; i++) {
        total_iov_len_remote += rdia->data[i].iov_len;
        total_iov_len_local += ldia->data[i].iov_len;
    }

    DEBUG_PRINT(LOG_DEBUG,
                ("\ntotal_iov_len_remote: %zu and total_iov_len_local: %zu",
                 total_iov_len_remote, total_iov_len_local));

    ssize_t nread = batch_process_vm_readv(pid, ldia->data, ldia->size,
                                           rdia->data, rdia->size);
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
    int dev_major, dev_minor, inode;
    unsigned long offset;
    
    uint64_t total_bytes =0;
    int i = 0;

    while (i < MAX_LINES &&
           fscanf(file, "%s %s %lx %d:%d %d %[^\n]", address_range, perms,
                  &offset, &dev_major, &dev_minor, &inode, pathname) >= 4) {

        DEBUG_PRINT(LOG_INFO, ("\nFilling remote[%d]...", i));

        uintptr_t start, end;
        sscanf(address_range, "%lx-%lx", &start, &end);
        DEBUG_PRINT(LOG_DEBUG, ("\nStart: 0x%lx", start));
        DEBUG_PRINT(LOG_DEBUG, ("\nEnd: 0x%lx", end));

        ssize_t len = end - start;
        DEBUG_PRINT(LOG_INFO, ("\nLength: 0x%zx", len));

        struct iovec temp;
        temp.iov_base = (void*)start;
        temp.iov_len = end - start;
        total_bytes+=temp.iov_len;
        add_iovec(remote, temp);

        i++;
    }
    printf("\nFound %d regions for a total of %lu bytes", i, total_bytes);

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
    free_iovec_array(sstate->remote);
    free_iovec_array(sstate->local);

    sstate->remote = sstate->next_remote;
    sstate->local = init_iovec_array(sstate->remote->size);

    sstate->next_remote = NULL;
    sstate->next_local = NULL;
}

void reset_current_state(SearchState* sstate) {
    free_iovec_array(sstate->local);

    sstate->local = init_iovec_array(sstate->remote->size);

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

    // next_remote will be filled for the next step
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

    if (sstate->next_remote->size == 0) {
        printf("\nNo result for the searched.");
        reset_current_state(sstate);
        cmd_loop(sstate);
    } else {
        printf("\nFound %zu Results. Going to next Step...",
               sstate->next_remote->size);
        advance_state(sstate);
        cmd_loop(sstate);
    }

    //print_memory_hex_from_dia(sstate->next_local, sstate->next_remote, 16);
}

void print_search_state(SearchState* sstate, int log_level) {
    DEBUG_PRINT(log_level,
                ("\nRemote has currently size of %zu", sstate->remote->size));
    DEBUG_PRINT(log_level, ("\nPrint Remote..."));
    print_dia(sstate->remote, 10);

    if(sstate->local->size != 0){
        DEBUG_PRINT(log_level,
                    ("\nLocal has currently size of %zu", sstate->local->size));
        DEBUG_PRINT(log_level, ("\nPrint Local..."));
        print_dia(sstate->local, 10);
    }

    if (sstate->next_remote != NULL) {
        DEBUG_PRINT(log_level, ("\n\nPrint Next Remote..."));
        print_dia(sstate->next_remote, 10);
    }

    if (sstate->next_local != NULL) {
        DEBUG_PRINT(log_level, ("\n\nPrint Next Local..."));
        print_dia(sstate->next_local, 10);
    }

    DEBUG_PRINT(log_level, ("\nThe searched is %d", *(int*)sstate->searched));
}

void write_value_at_pos(SearchState* sstate, size_t pos, int32_t value){
    if (pos >= sstate->remote->size){
        DEBUG_PRINT(LOG_ERROR, ("Wrong Position, the size of remote is %zu", sstate->remote->size));
        return;
    }

    void * write_ptr = sstate->remote->data[pos].iov_base;
    printf("\nWriting at %p ...",write_ptr);
    
    DIA* temp_write = init_iovec_array(1);
    struct iovec temp = {&value, sizeof(value)};
    add_iovec(temp_write, temp);

    DIA* temp_remote = init_iovec_array(1);
    struct iovec temp_r = {write_ptr, sizeof(value)};
    add_iovec(temp_remote, temp_r);

    write_to_remote_dia(sstate->pid, temp_write, temp_remote);
}


/*Main command loop, parses the various subcommands and then dispatch.*/
void cmd_loop(SearchState* sstate) {
    char cmd[25];
    while (1) {
        printf("\n\n\033[1;33mMeemo>\033[0m ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            continue;
        }
        printf("\033[2J\033[H"); // Clear screen + move cursor to home
        // printf("\nInput was: %s", cmd);
        char cmd_letter = cmd[0];
        char* rest = (strlen(cmd) > 1) ? &cmd[1] : NULL;
        switch (cmd_letter) {
            case 'p':  // s value
                print_search_state(sstate, LOG_WARN);
                break;
            case 's':
                printf("\nSearching %s", rest);  // If searched not specified already
                int32_t search_value;
                if (sscanf( rest,"%d", &search_value) != 1){
                    DEBUG_PRINT(LOG_ERROR,("\nWrong formatting. Should be s <value>"));
                    continue;
                }
                sstate->searched = &search_value;
                search_step_dia(sstate);
                break;
            case 'w':  //w pointer value
                printf("\nWriting...");
                size_t pos;
                int32_t value;
                if (sscanf( rest,"%zu %d", &pos, &value) != 2){
                    DEBUG_PRINT(LOG_ERROR,("\nWrong formatting. Should be w <pos> <value>"));
                    continue;
                }
                DEBUG_PRINT(LOG_DEBUG,("\nWriting at pos %zu the value: %d",pos,value));
                write_value_at_pos(sstate,pos,value);
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
    if (argc != 2) {
        fprintf(stderr, "\nUsage: memmo <pid>");
        exit(1);
    }

    char* endp;
    pid_t user_input_pid = (pid_t)strtol(argv[1], &endp, 10);
    if (endp == argv[1]) {
        printf("\nInvalid PID found.");
        exit(1);
    } else if (*endp != '\0') {
        printf("\nInvalid character: %c", *endp);
    } else {
        printf("\nInput PID = %d", user_input_pid);
    }

    char path[50];
    sprintf(path, "/proc/%d/maps", user_input_pid);
    DEBUG_PRINT(LOG_INFO,("\nOpening %s ...", path));
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
