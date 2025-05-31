#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#define MEEMO_VERSION "0.0.1"

/* Constants*/
#define MAX_STR_LEN_MAPS_COL 256
#define INITIAL_IOVEC_ARRAY_CAP 64
#define MAX_MAPS_LINES 2048
#define IOV_MAX_BATCH 1024
// #define IOVEC_MAX_STR 128 // Replaced by IOVEC_STR_MAX_LEN
#define IOVEC_STR_MAX_LEN 128 // Max length for string representation of one iovec
#define DIA_ENTRY_STR_MAX_LEN 150 // Max length for one formatted DIA entry line (e.g., "[idx] = <iovec_str>\n")

/* UI */
#define SEARCH_STR "\033[1;33m(search)\033[0m "
#define WRITE_STR "\033[1;33m(write)\033[0m "
#define MOVE_CURSOR(row, col) printf("\033[%d;%dH", (row), (col))
#define SHOW_CURSOR "\033[?25h"
#define CLEAR_SCREEN "\033[2J"

/* Type definitions */
typedef enum {
    TYPE_INT_32,
    TYPE_INT_64,
    TYPE_UINT_32,
    TYPE_UINT_64,
    TYPE_CHAR,
    TYPE_NONE,
    TYPE_UINT_32_UNKNOWN_INIT, // For first 'u' command
    TYPE_UINT_32_HIGHER,       // For 'h' command
    TYPE_UINT_32_LOWER,        // For 'l' command
} searchDataType;

/* Dynamic Iovec Array */
typedef struct {
    struct iovec* data;
    size_t size;
    size_t capacity;
} dia;

/* 
    Double buffered frame buffer.
    Use double buffering to prevent flicker.
    Write in the back, if different from front, update.
*/
typedef struct frameBuffer {
    int width;
    int height;
    char* front;
    char* back;
} frameBuffer;

/* Represents the current state of a search */
typedef struct searchState {
    dia* local_dia;
    dia* remote_dia;
    dia* next_remote_dia;
    dia* snapshot_dia; // For higher/lower searches, stores a snapshot of local_dia
    searchDataType type;
    void* searched;
    pid_t pid;
    int search_cnt;
} searchState;

/* Global variables */
struct winsize ws;
struct termios orig_termios;
volatile sig_atomic_t fb_need_resize = 0;
struct sigaction sa;

/* Error handling */
void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

int enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        goto raw_e;
    }

    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // disable echo + canonical mode
    raw.c_cc[VMIN] = 0;               // non-blocking read
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        goto raw_e;
    }
    return 0;

raw_e:
    errno = ENOTTY;
    return -1;
}

/* 
    Use select to check stdin in a non-blocking way. 
    Use set of fd but only fills it with stdin.
*/
int keypress(void) {
    struct timeval tv = {0, 0}; /* make select non-blocking */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

/* Put a single char in back buffer at (x,y) so that at draw time it will only be displayed if != front*/
void fb_putchar(frameBuffer* fb, int x, int y, char ch) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        /* not an error, do not render if not possible to fit everything */
        return;
    }
    fb->back[(y * fb->width) + x] = ch;
}

/*  Clears the frameBuffer from start_row to end_row */
void fb_clear_rows(frameBuffer* fb, int start_row, int end_row) {
    // Ensure row indices are within bounds
    if (start_row < 0) start_row = 0;
    if (end_row >= fb->height) end_row = fb->height - 1; // Ensure end_row is a valid index
    if (start_row > end_row || start_row >= fb->height) return; // Nothing to clear or start_row is out of bounds

    for (int r = start_row; r <= end_row; ++r) {
        for (int c = 0; c < fb->width; ++c) {
            fb_putchar(fb, c, r, ' ');
        }
    }
}

/*Will remove raw_mode, and get input at a specific location in the ui.*/
char* get_input_in_cmdbar(char* ps1) {
    int input_row = ws.ws_row - 1;
    int input_col_ps1 = 0;
    int input_col_cmd = 10;
    char* buffer = malloc(256 * sizeof(char));

    disable_raw_mode();

    MOVE_CURSOR(input_row, input_col_ps1);
    printf("%s", ps1);

    MOVE_CURSOR(input_row, input_col_cmd);
    fgets(buffer, 256 * sizeof(buffer), stdin);

    enable_raw_mode();

    // Clear the line
    MOVE_CURSOR(input_row, input_col_ps1);
    printf("\033[2K");

    return buffer;
}

dia* init_dia(size_t initial_capacity) {
    dia* arr = malloc(sizeof(dia));
    arr->size = 0;
    arr->capacity = initial_capacity;
    arr->data = calloc(arr->capacity, sizeof(struct iovec)); // Removed C-style cast
    if (!arr->data) {
        die("Failed to allocate memory for a dynamic iovec array.");
    }
    return arr;
}

/*
    Add an iovec to the the DIA.
    Uses a simple dynamic array approach where we x2 everytime we need more space.
*/
void add_iovec(dia* dia, struct iovec value) {
    if (dia->size == dia->capacity) {
        dia->capacity *= 2;
        struct iovec* new_data =
            realloc(dia->data, dia->capacity * sizeof(struct iovec));
        if (!new_data) {
            die("Failed to reallocate memory for the dynamic iovec array");
        }
        dia->data = new_data;
    }

    dia->data[dia->size++] = value;
}

/*
    Meemo uses iovec to store data found.
    Cannot pass more than 1024 iovec to process_vm_readv.
    So we batch them.
*/
ssize_t batch_process_vm_readv(pid_t pid, struct iovec* liovec, size_t ln,
                               struct iovec* riovec, size_t rn) {
    ssize_t total_read = 0;
    size_t offset = 0;

    while (offset < ln && offset < rn) {
        size_t batch =
            (ln - offset < IOV_MAX_BATCH) ? (ln - offset) : IOV_MAX_BATCH;
        if (rn - offset < batch) {
            batch = rn - offset;
        }

        ssize_t nread = process_vm_readv(pid, liovec + offset, batch,
                                         riovec + offset, batch, 0);

        if (nread == -1) {
            perror("Error during batch_process_vm_readv (process_vm_readv call failed)");
            break;
        }
        total_read += nread;
        offset += batch;
    }

    return total_read;
}

/*
    Takes in a remote DIA and reads it in a local DIA.
    The local dia iovec's are allocated inside.
*/
ssize_t read_from_remote_dia(pid_t pid, dia* local_dia, dia* remote_dia) {
    size_t i = 0;
    for (; i < remote_dia->size; i++) {
        void* buffer = malloc(remote_dia->data[i].iov_len);
        if (!buffer) {
            die("Failure to read from remote dia");
        }
        struct iovec local_iov = {.iov_base = buffer,
                                  .iov_len = remote_dia->data[i].iov_len};

        add_iovec(local_dia, local_iov);
    }

    ssize_t nread =
        batch_process_vm_readv(pid, local_dia->data, local_dia->size,
                               remote_dia->data, remote_dia->size);
    return nread;
}

ssize_t write_to_remote_dia(pid_t pid, dia* local_dia, dia* remote_dia) {

    ssize_t nwrite = process_vm_writev(pid, local_dia->data, local_dia->size,
                                       remote_dia->data, remote_dia->size, 0);
    if (nwrite == -1) {
        perror("Error during write_to_remote_dia (process_vm_writev call failed)");
    }
    return nwrite;
}

/*
    Access the memory using unsigned char *
    This is always safe even if unaligned to uint32_t.
    For every region creates the pointer,
    then scans the entirety of the region.
    After the first search a region == one found searched.
    Scan the local and every find in it put the corresponding
    remote object in the next_remote.
*/
void search_step_for_uint32_dia(const dia* local, const dia* remote,
                                dia* next_remote_iov_array, size_t len,
                                const uint32_t* searched) {
    // For each region
    for (size_t n_regions = 0; n_regions < len; n_regions++) {
        const unsigned char* base_l =
            (const unsigned char*)local->data[n_regions].iov_base;
        const unsigned char* base_r =
            (const unsigned char*)remote->data[n_regions].iov_base;

        // Skip regions smaller than the search type
        if (local->data[n_regions].iov_len < sizeof(uint32_t)) {
            continue;
        }

        // Scan all the region with a memcpy
        for (size_t offset = 0;
             offset <= local->data[n_regions].iov_len - sizeof(uint32_t);
             offset++) {
            uint32_t val;
            memcpy(&val, base_l + offset, sizeof(uint32_t));
            if (val == *searched) {
                uintptr_t base = (uintptr_t)base_r + offset;
                struct iovec next_remote_iov = {.iov_base = (void*)base,
                                                .iov_len = sizeof(uint32_t)};

                add_iovec(next_remote_iov_array, next_remote_iov);
            }
        }
    }
}

/*
    Frees a dia structure and optionally its iov_base members.
    Set to 1 if iov_base pointers were malloc'd for this dia.
    Set to 0 if iov_base pointers are managed elsewhere.
*/
void free_dia(dia* arr, int free_iov_bases) {
    if (!arr) {
        return;
    }

    if (free_iov_bases) {
        for (size_t i = 0; i < arr->size; i++) {
            if (arr->data[i].iov_base) {
                free(arr->data[i].iov_base);
                arr->data[i].iov_base = NULL;  // Good practice
            }
        }
    }
    if (arr->data) {
        free(arr->data);
        arr->data = NULL;
    }
    arr->size = 0;
    arr->capacity = 0;
    free(arr);
}

void advance_state(searchState* sstate) {
    free_dia(sstate->remote_dia, 0);
    free_dia(sstate->local_dia, 1);

    sstate->remote_dia = sstate->next_remote_dia;
    sstate->local_dia = init_dia(sstate->remote_dia->size);

    sstate->next_remote_dia = NULL;
}

void reset_current_state(searchState* sstate) {
    free_dia(sstate->local_dia, 1);
    free_dia(sstate->next_remote_dia, 0);

    sstate->local_dia = init_dia(sstate->remote_dia->size);

    sstate->next_remote_dia = NULL;
}

/* 
    Perform a search on sstate->remote and prepare the next step.
    Finally advances the state.
*/
size_t search_step_dia(searchState* sstate) {
    // next_remote will be filled for the next step
    sstate->next_remote_dia = init_dia(INITIAL_IOVEC_ARRAY_CAP);
    switch (sstate->type) {
        case TYPE_UINT_32:
            read_from_remote_dia(sstate->pid, sstate->local_dia,
                                 sstate->remote_dia);
            search_step_for_uint32_dia(
                sstate->local_dia, sstate->remote_dia, sstate->next_remote_dia,
                sstate->remote_dia->size, (uint32_t*)sstate->searched);
            break;
        default:
            // TODO add more types
            break;
    }

    size_t found = sstate->next_remote_dia->size;
    if (found == 0) {
        reset_current_state(sstate);
    } else {
        advance_state(sstate);
    }
    return found;
}

char* string_iovec(const struct iovec* io) {
    char* iovec_str = malloc(IOVEC_STR_MAX_LEN); // Use new constant
    if (!iovec_str) {
        return NULL;
    }
    int written = snprintf(iovec_str, IOVEC_STR_MAX_LEN, "Base: %p Len: %d", // Use new constant
                           io->iov_base, (int)io->iov_len);
    if (written < 0 || written >= IOVEC_STR_MAX_LEN) { // Use new constant
        free(iovec_str);
        return NULL;
    }
    return iovec_str;
}

char* string_dia(const dia* arr, size_t max_elem) {
    size_t min = max_elem <= arr->size ? max_elem : arr->size;

    // Need to store enough potentially for all the displayed elements
    char* dia_str = malloc(min * DIA_ENTRY_STR_MAX_LEN); // Use new constant
    if (!dia_str) {
        return NULL;
    }

    char* p = dia_str;
    for (size_t i = 0; i < min; i++) {
        char* iovec_str = string_iovec(&arr->data[i]);
        if (!iovec_str) {
            free(dia_str);
            return NULL;
        }
        // Use new constant for buffer size
        int written = snprintf(p, DIA_ENTRY_STR_MAX_LEN, "\n[%zu] = %s", i, iovec_str);
        free(iovec_str);
        if (written < 0 || written >= DIA_ENTRY_STR_MAX_LEN) { // Use new constant
            free(dia_str);
            return NULL;
        }
        p += written;
    }

    if (min < arr->size) {
        // Use new constant for buffer size
        int written = snprintf(p, DIA_ENTRY_STR_MAX_LEN, "\nAnd %zu more. Refine the search.",
                               arr->size - min);
        if (written < 0 || written >= DIA_ENTRY_STR_MAX_LEN) { // Use new constant
            free(dia_str);
            return NULL;
        }
        p += written;
    }

    *p = '\0';
    return dia_str;
}

char* string_search_state(searchState* sstate) {
    if (sstate == NULL) {
        return NULL;
    }
    return string_dia(sstate->remote_dia, ws.ws_row - 8);
}

int write_value_at_pos(searchState* sstate, size_t pos, uint32_t value) { // Return type changed to int
    if (pos >= sstate->remote_dia->size) {
        return 0; // Indicate failure: invalid position
    }

    void* write_ptr = sstate->remote_dia->data[pos].iov_base;

    dia* temp_write = init_dia(1);
    struct iovec temp = {&value, sizeof(value)};
    add_iovec(temp_write, temp);

    dia* temp_remote = init_dia(1);
    struct iovec temp_r = {write_ptr, sizeof(value)};
    add_iovec(temp_remote, temp_r);

    ssize_t nwrite = write_to_remote_dia(sstate->pid, temp_write, temp_remote);
    free_dia(temp_write, 0);
    free_dia(temp_remote, 0);

    if (nwrite == -1 || (size_t)nwrite < sizeof(value)) {
        return 0; // Indicate failure: write error or incomplete write
    }
    return 1; // Indicate success
}

/* 
    Dumb function to put a string. 
    Supports only the \n.
*/
void fb_putstr(frameBuffer* fb, int x, int y, const char* str) {
    int cx = x;
    int cy = y;

    /* cycle trouth the string */
    while (*str) {
        if (*str == '\n') { /* next row, same starting col */
            cx = x;
            cy++;
        } else {
            fb_putchar(fb, cx, cy, *str);
            cx++;
            if (cx >= fb->width) { /* wrap */
                cx = x;
                cy++;
            }
        }

        if (cy >= fb->height) { /* stay in frameBuffer */
            break;
        }
        str++;
    }
}

/*Main command loop, parses the various subcommands and then dispatch.*/
void handle_cmd(frameBuffer* fb, searchState* sstate, char cmd) {
    // Clear the previous content
    fb_clear_rows(fb, 2, fb->height - 1);
    switch (cmd) {
        case 's':;
            // When a new specific value search starts, clear any existing snapshot
            if (sstate->snapshot_dia) {
                free_dia(sstate->snapshot_dia, 1); // Assuming snapshot_dia owns its iovec bases
                sstate->snapshot_dia = NULL;
            }
            char* s_subcmd = get_input_in_cmdbar(SEARCH_STR);
            uint32_t search_value; // Changed from int32_t
            if (sscanf(s_subcmd, "%u", &search_value) != 1) { // Changed from %d
                static const char* err_msg = "Invalid search value. Please enter a number.";
                fb_putstr(fb, 0, 2, err_msg);
                free(s_subcmd);
                return;
            }
            free(s_subcmd);
            sstate->searched = &search_value;
            size_t found = search_step_dia(sstate);
            if (found == 0) {
                static const char* nf =
                    "Value not found in current results. Displaying current results:";
                fb_putstr(fb, 0, 2, nf);
            }
            goto print_results_label; // 's' command jumps to print

        case 'h': {
            // Pre-condition checks
            if (!sstate->snapshot_dia) {
                static const char* err_msg = "No snapshot available. Use 'u' command first.";
                fb_putstr(fb, 0, 2, err_msg);
                break; // Break from 'h', does not fall through to 'p'
            }
            if (!sstate->remote_dia || sstate->remote_dia->size == 0) {
                static const char* err_msg = "No memory regions to compare. Perform a search or reset first.";
                fb_putstr(fb, 0, 2, err_msg);
                break; // Break from 'h'
            }
            if (sstate->remote_dia->size != sstate->snapshot_dia->size) {
                static const char* err_msg = "Snapshot size mismatch. Please use 'u' again.";
                fb_putstr(fb, 0, 2, err_msg);
                // Consider freeing snapshot_dia here as it's inconsistent
                free_dia(sstate->snapshot_dia, 1);
                sstate->snapshot_dia = NULL;
                break; // Break from 'h'
            }

            dia* current_values_dia = init_dia(sstate->remote_dia->size);
            if (!current_values_dia) { // Should be handled by die() in init_dia
                static const char* err_mem = "Failed to initialize temporary DIA for current values.";
                fb_putstr(fb, 0, 2, err_mem);
                break; // Break from 'h'
            }

            ssize_t nread = read_from_remote_dia(sstate->pid, current_values_dia, sstate->remote_dia);
            if (sstate->remote_dia->size > 0 && current_values_dia->size == 0) { // Check if anything was actually read into current_values_dia
                static const char* err_read = "Failed to read current values for comparison. See stderr.";
                fb_putstr(fb, 0, 2, err_read);
                free_dia(current_values_dia, 1);
                break; // Break from 'h'
            }

            if (sstate->next_remote_dia) {
                free_dia(sstate->next_remote_dia, 0); // Free old next_remote_dia if any
                sstate->next_remote_dia = NULL;
            }
            sstate->next_remote_dia = init_dia(INITIAL_IOVEC_ARRAY_CAP);
            if (!sstate->next_remote_dia) { // Should be handled by die()
                static const char* err_mem_next = "Failed to initialize DIA for next results.";
                fb_putstr(fb, 0, 2, err_mem_next);
                free_dia(current_values_dia, 1);
                break; // Break from 'h'
            }

            for (size_t i = 0; i < sstate->remote_dia->size; ++i) {
                if (i >= current_values_dia->size) continue; // Safety: current_values might be smaller if partial read

                if (!sstate->snapshot_dia->data[i].iov_base || sstate->snapshot_dia->data[i].iov_len < sizeof(uint32_t) ||
                    !current_values_dia->data[i].iov_base || current_values_dia->data[i].iov_len < sizeof(uint32_t)) {
                    continue; // Skip if data is invalid or too small
                }

                uint32_t previous_val = *((uint32_t*)sstate->snapshot_dia->data[i].iov_base);
                uint32_t current_val = *((uint32_t*)current_values_dia->data[i].iov_base);

                if (current_val > previous_val) {
                    add_iovec(sstate->next_remote_dia, sstate->remote_dia->data[i]);
                }
            }

            sstate->type = TYPE_UINT_32_HIGHER;
            sstate->search_cnt++;
            size_t found_h = sstate->next_remote_dia->size; // Renamed to avoid conflict with 's' case 'found'
            char msg_buf[128];

            if (found_h == 0) {
                reset_current_state(sstate); // Keeps remote_dia and snapshot_dia
                snprintf(msg_buf, sizeof(msg_buf), "No values increased. (Scan %d)", sstate->search_cnt);
            } else {
                free_dia(sstate->snapshot_dia, 1); // Free old snapshot data
                sstate->snapshot_dia = NULL;

                advance_state(sstate); // remote_dia becomes next_remote_dia, local_dia is reset

                sstate->snapshot_dia = init_dia(sstate->remote_dia->size); // New snapshot for new remote_dia
                if (!sstate->snapshot_dia) {
                    snprintf(msg_buf, sizeof(msg_buf), "%zu values increased. (Scan %d) Error: Failed to re-snapshot.", found_h, sstate->search_cnt);
                } else {
                    ssize_t new_snap_nread = read_from_remote_dia(sstate->pid, sstate->snapshot_dia, sstate->remote_dia);
                    if (sstate->remote_dia->size > 0 && sstate->snapshot_dia->size == 0) { // Check if new snapshot is empty
                        snprintf(msg_buf, sizeof(msg_buf), "%zu values increased. (Scan %d) Failed to re-snapshot.", found_h, sstate->search_cnt);
                        free_dia(sstate->snapshot_dia, 1);
                        sstate->snapshot_dia = NULL;
                    } else {
                        snprintf(msg_buf, sizeof(msg_buf), "%zu values increased. (Scan %d) New snapshot taken.", found_h, sstate->search_cnt);
                    }
                }
            }
            free_dia(current_values_dia, 1);
            fb_putstr(fb, 0, 2, msg_buf);
            goto print_results_label; // 'h' command jumps to print
        }
        case 'l': {
            // Pre-condition checks (similar to 'h')
            if (!sstate->snapshot_dia) {
                static const char* err_msg = "No snapshot available. Use 'u' command first.";
                fb_putstr(fb, 0, 2, err_msg);
                break;
            }
            if (!sstate->remote_dia || sstate->remote_dia->size == 0) {
                static const char* err_msg = "No memory regions to compare. Perform a search or reset first.";
                fb_putstr(fb, 0, 2, err_msg);
                break;
            }
            if (sstate->remote_dia->size != sstate->snapshot_dia->size) {
                static const char* err_msg = "Snapshot size mismatch. Please use 'u' again.";
                fb_putstr(fb, 0, 2, err_msg);
                free_dia(sstate->snapshot_dia, 1);
                sstate->snapshot_dia = NULL;
                break;
            }

            dia* current_values_dia = init_dia(sstate->remote_dia->size);
            if (!current_values_dia) {
                static const char* err_mem = "Failed to initialize temporary DIA for current values.";
                fb_putstr(fb, 0, 2, err_mem);
                break;
            }

            ssize_t nread = read_from_remote_dia(sstate->pid, current_values_dia, sstate->remote_dia);
            if (sstate->remote_dia->size > 0 && current_values_dia->size == 0) {
                static const char* err_read = "Failed to read current values for comparison. See stderr.";
                fb_putstr(fb, 0, 2, err_read);
                free_dia(current_values_dia, 1);
                break;
            }

            if (sstate->next_remote_dia) {
                free_dia(sstate->next_remote_dia, 0);
                sstate->next_remote_dia = NULL;
            }
            sstate->next_remote_dia = init_dia(INITIAL_IOVEC_ARRAY_CAP);
            if (!sstate->next_remote_dia) {
                static const char* err_mem_next = "Failed to initialize DIA for next results.";
                fb_putstr(fb, 0, 2, err_mem_next);
                free_dia(current_values_dia, 1);
                break;
            }

            for (size_t i = 0; i < sstate->remote_dia->size; ++i) {
                if (i >= current_values_dia->size) continue;

                if (!sstate->snapshot_dia->data[i].iov_base || sstate->snapshot_dia->data[i].iov_len < sizeof(uint32_t) ||
                    !current_values_dia->data[i].iov_base || current_values_dia->data[i].iov_len < sizeof(uint32_t)) {
                    continue;
                }

                uint32_t previous_val = *((uint32_t*)sstate->snapshot_dia->data[i].iov_base);
                uint32_t current_val = *((uint32_t*)current_values_dia->data[i].iov_base);

                if (current_val < previous_val) { // Main difference: < instead of >
                    add_iovec(sstate->next_remote_dia, sstate->remote_dia->data[i]);
                }
            }

            sstate->type = TYPE_UINT_32_LOWER; // Set type to LOWER
            sstate->search_cnt++;
            size_t found = sstate->next_remote_dia->size;
            char msg_buf[128];

            if (found == 0) {
                reset_current_state(sstate);
                snprintf(msg_buf, sizeof(msg_buf), "No values decreased. (Scan %d)", sstate->search_cnt);
            } else {
                free_dia(sstate->snapshot_dia, 1);
                sstate->snapshot_dia = NULL;

                advance_state(sstate);

                sstate->snapshot_dia = init_dia(sstate->remote_dia->size);
                if (!sstate->snapshot_dia) {
                    snprintf(msg_buf, sizeof(msg_buf), "%zu values decreased. (Scan %d) Error: Failed to re-snapshot.", found, sstate->search_cnt);
                } else {
                    ssize_t new_snap_nread = read_from_remote_dia(sstate->pid, sstate->snapshot_dia, sstate->remote_dia);
                    if (sstate->remote_dia->size > 0 && sstate->snapshot_dia->size == 0) {
                        snprintf(msg_buf, sizeof(msg_buf), "%zu values decreased. (Scan %d) Failed to re-snapshot.", found, sstate->search_cnt);
                        free_dia(sstate->snapshot_dia, 1);
                        sstate->snapshot_dia = NULL;
                    } else {
                        snprintf(msg_buf, sizeof(msg_buf), "%zu values decreased. (Scan %d) New snapshot taken.", found, sstate->search_cnt);
                    }
                }
            }
            free_dia(current_values_dia, 1);
            fb_putstr(fb, 0, 2, msg_buf);
            goto print_results_label; // Jump to print case
        }
print_results_label:
        case 'p':;
            char* search_state_str = string_search_state(sstate);
            fb_putstr(fb, 0, 3, search_state_str);
            free(search_state_str);
            break;
        case 'r':;
            // Free existing dia structures
            free_dia(sstate->local_dia, 1);
            free_dia(sstate->remote_dia, 0);

            // Re-initialize remote_dia
            sstate->remote_dia = init_dia(INITIAL_IOVEC_ARRAY_CAP);
            read_maps_into_dia(sstate->remote_dia, sstate->pid);

            // Re-initialize local_dia
            sstate->local_dia = init_dia(sstate->remote_dia->size);

            // Reset other search state variables
            sstate->searched = NULL;
            sstate->type = TYPE_UINT_32; // Default type
            sstate->search_cnt = 0;

            // Free next_remote_dia if it's not NULL
            if (sstate->next_remote_dia != NULL) {
                free_dia(sstate->next_remote_dia, 0);
                sstate->next_remote_dia = NULL;
            }

            // Free snapshot_dia if it exists
            if (sstate->snapshot_dia) {
                free_dia(sstate->snapshot_dia, 1); // Assuming snapshot_dia owns its iovec bases
                sstate->snapshot_dia = NULL;
            }

            static const char* rs = "Search state reset.";
            fb_putstr(fb, 0, 2, rs);
            break;
        // Fallthrough from 's' is here
        case 'u': {
            // Pre-condition: sstate->remote_dia should exist and have entries.
            if (!sstate->remote_dia || sstate->remote_dia->size == 0) {
                static const char* err_msg = "No memory regions to snapshot. Perform a search or reset first.";
                fb_putstr(fb, 0, 2, err_msg);
                break;
            }

            // Free existing snapshot_dia if it exists
            if (sstate->snapshot_dia) {
                free_dia(sstate->snapshot_dia, 1); // free_iov_bases is 1 because snapshot_dia owns its buffers
                sstate->snapshot_dia = NULL;
            }

            // Initialize new snapshot_dia with capacity based on current remote_dia's size
            sstate->snapshot_dia = init_dia(sstate->remote_dia->size);
            if (!sstate->snapshot_dia) { // Should be handled by die() in init_dia, but good to be defensive
                static const char* err_mem = "Failed to initialize snapshot.";
                fb_putstr(fb, 0, 2, err_mem);
                break;
            }

            // Read current values from remote_dia's addresses into snapshot_dia's buffers
            // read_from_remote_dia allocates buffers for snapshot_dia->data[i].iov_base
            ssize_t nread = read_from_remote_dia(sstate->pid, sstate->snapshot_dia, sstate->remote_dia);

            // Check for read errors. batch_process_vm_readv (called by read_from_remote_dia) already calls perror.
            // If nread is 0 when we expected data, it's a full failure for this snapshot.
            if (sstate->remote_dia->size > 0 && nread == 0 && sstate->snapshot_dia->size == 0) {
                 // Check snapshot_dia->size because read_from_remote_dia populates it incrementally
                static const char* err_read = "Failed to read values for snapshot. See stderr for details.";
                fb_putstr(fb, 0, 2, err_read);
                free_dia(sstate->snapshot_dia, 1); // Clean up
                sstate->snapshot_dia = NULL;
                break;
            }
            // If nread < total expected bytes but > 0, it's a partial read. perror was called.
            // The snapshot will contain what was read. This might be acceptable.

            sstate->type = TYPE_UINT_32_UNKNOWN_INIT; // Set current search type
            sstate->search_cnt = 0; // Reset search count for h/l series

            char msg_buf[128];
            snprintf(msg_buf, sizeof(msg_buf), "Snapshot of %zu values taken. Use 'h' (higher) or 'l' (lower).", sstate->snapshot_dia->size);
            fb_putstr(fb, 0, 2, msg_buf);
            break;
        }
        case 'w':;
            char* w_subcmd = get_input_in_cmdbar(WRITE_STR);
            size_t pos;
            uint32_t value; // Changed from int32_t
            if (sscanf(w_subcmd, "%zu %u", &pos, &value) != 2) { // Changed from %d
                static const char* err_msg = "Invalid format for write. Use: <pos> <value>.";
                fb_putstr(fb, 0, 2, err_msg);
                free(w_subcmd);
                return;
            }
            free(w_subcmd);
            if (write_value_at_pos(sstate, pos, value)) {
                static const char* success_msg = "Value written successfully.";
                fb_putstr(fb, 0, 2, success_msg);
            } else {
                static const char* fail_msg = "Write operation failed.";
                fb_putstr(fb, 0, 2, fail_msg);
            }
            break;
        case 'q':  //quit
            printf(CLEAR_SCREEN);
            printf(SHOW_CURSOR);
            disable_raw_mode();
            printf("\nQuitting...");
            exit(EXIT_SUCCESS);
            break;
        default:
            // TODO Add to content that the command was not recogn
            break;
    }
}

/* Check if keypress and switches on it to execute corresponding cmd. */
int process_input(frameBuffer* fb, searchState* sstate) {
    if (keypress()) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            handle_cmd(fb, sstate, c);
        }
    }
    return 0;
}

/* Use ioctl to read the current terminal size */
void update_terminal_size(void) {
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
        die("Error while updating the terminal size");
    }
}

/* Creates the frameBuffer of given size and clear both front and back*/
frameBuffer init_fb(int width, int height) {
    frameBuffer fb;
    fb.width = width;
    fb.height = height;
    size_t size = (long)fb.width * fb.height;
    fb.front = calloc(size, sizeof(char));
    fb.back = calloc(size, sizeof(char));
    if (fb.front == NULL || fb.back == NULL) {
        die("Calloc for frameBuffer init failed");
    }
    return fb;
}

void free_fb(frameBuffer* fb) {
    fb->width = 0;
    fb->height = 0;
    free(fb->front);
    free(fb->back);
}

/* 
    Drawing function.
    Check for each cell if front != back.
    If it is then use ANSI escape code to redraw it.
*/
void fb_swap(frameBuffer* fb) {
    for (int i = 0; i < fb->width * fb->height; ++i) {
        if (fb->front[i] != fb->back[i]) {
            int x = i % fb->width;
            int y = i / fb->width;
            printf("\033[?25l");  // Hide the cursor
            printf("\033[%d;%dH%c", y + 1, x + 1, fb->back[i]);
            fb->front[i] = fb->back[i];
        }
    }
    fflush(stdout);
}

/*  Create the header */
void add_header(frameBuffer* fb) {
    static const char* title = "Meemo " MEEMO_VERSION;
    size_t i = 0;
    for (; i < strlen(title); ++i) {
        fb_putchar(fb, (int)i, 0, title[i]);
    }
    for (; (int)i < fb->width; ++i) {
        fb_putchar(fb, (int)i, 0, '=');
    }
}

/*  Create the footer */
void add_footer(frameBuffer* fb) {
    static const char* cmds =
        "s:search|u:snap|h:higher|l:lower|w:write|p:print|r:reset|q:quit"; // Updated cmds string
    size_t i = 0;
    for (; i < strlen(cmds); i++) {
        fb_putchar(fb, (int)i, fb->height - 1, cmds[i]);
    }
    for (; (int)i < fb->width; i++) {
        fb_putchar(fb, (int)i, fb->height - 1, '=');
    }
}

/* When sigwinch triggered, we are in need of a resize */
static void sigwinchHandler(int sig) {
    (void)sig;
    fb_need_resize = 1;
}

/* 
   Read the maps file of a process and stores them into a dia. 
*/
int read_maps_into_dia(dia* remote, pid_t pid) {
    char path[32];
    sprintf(path, "/proc/%d/maps", pid);
    FILE* file = fopen(path, "r");
    if (!file) {
        die("Error reading /proc/<PID>/maps file.");
    }

    char address_range[MAX_STR_LEN_MAPS_COL], perms[MAX_STR_LEN_MAPS_COL],
        pathname[MAX_STR_LEN_MAPS_COL];
    int dev_major, dev_minor, inode;
    unsigned long offset;
    int i = 0;

    while (i < MAX_MAPS_LINES &&
           fscanf(file, "%s %s %lx %d:%d %d %[^\n]", address_range, perms,
                  &offset, &dev_major, &dev_minor, &inode, pathname) >= 4) {

        /* only load those with rw-- */
        if (perms[0] != 'r' || perms[1] != 'w')
            continue;

        uintptr_t start, end;
        sscanf(address_range, "%lx-%lx", &start, &end);

        struct iovec temp;
        temp.iov_base = (void*)start;
        temp.iov_len = end - start;
        add_iovec(remote, temp);

        i++;
    }

    fclose(file);
    return i;
}

void setup_terminal_resize_sig(void) {
    // config signal for terminal resize
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigwinchHandler;
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        die("Failure to setup terminal resize signal");
    }
}

/* Create the current buffer by adding he header, content, footer*/
void draw(frameBuffer* fb) {
    add_header(fb);
    add_footer(fb);
    fb_swap(fb);
}

/* 
    Parse the pid and validate it.
    Setup the initial search state.
    Run the  render loop.    
*/
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "\nUsage: meemo <pid>");
        exit(EXIT_FAILURE);
    }
    pid_t input_pid;
    if (sscanf(argv[1], "%d", &input_pid) != 1 || input_pid <= 0) {
        fprintf(stderr, "\nInvalid PID. Cannot parse a valid pid value.");
        exit(EXIT_FAILURE);
    }

    setup_terminal_resize_sig();

    dia* remote = init_dia(INITIAL_IOVEC_ARRAY_CAP);
    int regions = read_maps_into_dia(remote, input_pid);
    dia* local = init_dia(regions);

    searchState initial_sstate = {local, remote, NULL, NULL, TYPE_UINT_32, // Added NULL for snapshot_dia
                                  NULL,  input_pid, 0};

    enable_raw_mode();
    update_terminal_size();

    frameBuffer current_buffer = init_fb(ws.ws_col, ws.ws_row);
    printf(CLEAR_SCREEN);

    /* Main Render loop */
    while (1) {
        if (fb_need_resize) {
            printf(CLEAR_SCREEN);
            update_terminal_size();
            free_fb(&current_buffer);
            current_buffer = init_fb(ws.ws_col, ws.ws_row);
            fb_need_resize = 0;
        }

        draw(&current_buffer); /*draw frame x*/
        process_input(&current_buffer,
                      &initial_sstate); /* modify buffer for frame x+1*/
        usleep(100000);
    }

    return 0;
}
