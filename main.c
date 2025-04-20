#include <bits/posix1_lim.h>
#include <bits/types/struct_iovec.h>
#include <complex.h>
#include <stddef.h>
#include <sys/select.h>
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#define MEEMO_VERSION "0.0.1"

#define MAX_LINES 500
#define MAX_STR_LEN 500
#define INITIAL_IOVEC_ARRAY_CAP 128
#define WRITE_ASK 10
#define IOV_MAX_BATCH 1024

/*String max for print*/
#define IOVEC_MAX_STR 32

/*UI Strings*/
#define SUBCMD_PROMPT "\033[1;33mMeemo>\033[0m "

/* Search State*/
typedef struct SearchState SearchState;
typedef struct FrameBuffer FrameBuffer;

void handle_cmd(FrameBuffer* fb, SearchState* sstate, char cmd);

/* Terminal */
#define CLEAR_SCREEN "\033[2J"
#define MOVE_CURSOR(row, col) printf("\033[%d;%dH", (row), (col))
#define SHOW_CURSOR "\033[?25h"

struct winsize ws;
struct sigaction sa;

/*Store the original terminal settings. Will be restored at the exit.*/
struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr: failed to read terminal attributes");
        exit(EXIT_FAILURE);
    }

    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // disable echo + canonical mode
    raw.c_cc[VMIN] = 0;               // non-blocking read
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr: failed to apply raw mode");
        exit(EXIT_FAILURE);
    } 
}

int keypress(void) {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int process_input(FrameBuffer* fb, SearchState* sstate) {
    if (keypress()) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            handle_cmd(fb, sstate, c);
        }
    }
    return 0;
}

void update_terminal_size(void) {
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
        exit(1);
    }
}

typedef struct FrameBuffer {
    int width;
    int height;
    char* front;
    char* back;
} FrameBuffer;

FrameBuffer init_fb(int width, int height) {
    FrameBuffer fb;
    fb.width = width;
    fb.height = height;
    size_t size = (long)fb.width * fb.height;
    fb.front = calloc(size, sizeof(char));
    fb.back = calloc(size, sizeof(char));
    return fb;
}

void free_fb(FrameBuffer* fb) {
    fb->width = 0;
    fb->height = 0;
    free(fb->front);
    free(fb->back);
}

/* Put the char in back buffer at (x,y) so that at draw time it will only be displayed if != front*/
void fb_putchar(FrameBuffer* fb, int x, int y, char ch) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        return;
    }
    fb->back[(y * fb->width) + x] = ch;
}

/* Put a string with possibility of using \n.*/
void fb_putstr(FrameBuffer* fb, int x, int y, const char* str) {
    int cx = x;
    int cy = y;

    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy++;
        } 
        else {
            fb_putchar(fb, cx, cy, *str);
            cx++;
            if (cx >= fb->width) {  // Wrap line if needed
                cx = x;
                cy++;
            }
        }

        if (cy >= fb->height) {
            break;
        }
        str++;
    }
}

/* Draw only the char that are different from front.*/
void fb_swap(FrameBuffer* fb) {
    for (int i = 0; i < fb->width * fb->height; ++i) {
        if (fb->front[i] != fb->back[i]) {
            int x = i % fb->width;
            int y = i / fb->width;
            printf("\033[?25l");  // Hide the cursor
            printf("\033[%d;%dH%c", y + 1, x + 1,
                   fb->back[i]);  // Cursor to home TODO
            fb->front[i] = fb->back[i];
        }
    }
    fflush(stdout);
}

void add_header(FrameBuffer* fb) {
    static const char* title = "Meemo " MEEMO_VERSION;
    size_t i = 0;
    for (; i < strlen(title); ++i) {
        fb_putchar(fb, (int)i, 0, title[i]);
    }
    for (; (int)i < fb->width; ++i) {
        fb_putchar(fb, (int)i, 0, '=');
    }
}

void add_footer(FrameBuffer* fb) {
    static const char* cmds = " s: search | p: print | w: write | q: quit ";
    size_t i = 0;
    for (; i < strlen(cmds); i++) {
        fb_putchar(fb, (int)i, fb->height - 1, cmds[i]);
    }
    for (; (int)i < fb->width; i++) {
        fb_putchar(fb, (int)i, fb->height - 1, '=');
    }
}

volatile sig_atomic_t fb_need_resize = 0;

static void sigwinchHandler(int sig) {
    (void)sig;
    fb_need_resize = 1;
}

/* Dynamic Iovec Array */
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

/*
    Add an iovec to the the DIA.
    Uses a simple dynamic array approach where we x2 everytime we need more space.
*/
void add_iovec(DIA* dia, struct iovec value) {
    if (dia->size == dia->capacity) {
        dia->capacity *= 2;
        struct iovec* new_data =
            realloc(dia->data, dia->capacity * sizeof(struct iovec));
        if (!new_data) {
            // TODO
            perror("Failed to reallocate memory");
            exit(EXIT_FAILURE);
        }
        dia->data = new_data;
    }

    dia->data[dia->size++] = value;
}

void free_iovec_array(DIA* arr) {
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

char* string_iovec(const struct iovec* io) {
    char* iovec_str = malloc(IOVEC_MAX_STR);
    if (!iovec_str) return NULL;
    int written = snprintf(iovec_str, IOVEC_MAX_STR, "Base: %p Len: %d", io->iov_base, (int)io->iov_len);
    if (written < 0 || written >= IOVEC_MAX_STR) {
        free(iovec_str);
        return NULL;
    }
    return iovec_str;
}

char* string_dia(const DIA* arr, size_t max_elem) {
    size_t min = max_elem <= arr->size ? max_elem : arr->size;

    // Need to store enough potentially for all the displayed elements
    char* dia_str = malloc(min * 50);
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
        int written = snprintf(p,50, "\n[%zu] = %s", i, iovec_str);
        free(iovec_str);
        if (written < 0 || written >= 50) {
            free(dia_str);
            return NULL;
        }
        p+=written;
    }

    *p = '\0';
    return dia_str;
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
void search_step_for_uint32_dia(DIA* local, DIA* remote,
                                DIA* next_remote_iov_array, size_t len,
                                uint32_t* searched) {
    // For each region
    for (size_t n_regions = 0; n_regions < len; n_regions++) {
        const unsigned char* base_l =
            (const unsigned char*)local->data[n_regions].iov_base;
        const unsigned char* base_r =
            (const unsigned char*)remote->data[n_regions].iov_base;

        // Scan all the region with a memcpy
        for (size_t offset = 0;
             offset <= local->data[n_regions].iov_len - sizeof(uint32_t);
             offset++) {
            uint32_t val;
            memcpy(&val, base_l + offset, sizeof(uint32_t));
            if (val == *searched) {
                uintptr_t base = (uintptr_t)base_r + offset;
                struct iovec next_remote_iov = {.iov_base = (void*)base,
                                                .iov_len = sizeof(int32_t)};

                add_iovec(next_remote_iov_array, next_remote_iov);
            }
        }
    }
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
            //TODO
            break;
        }
        total_read += nread;
        offset += batch;
    }

    return total_read;
}

typedef enum {
    TYPE_INT_32,
    TYPE_INT_64,
    TYPE_UINT_32,
    TYPE_UINT_64,
    TYPE_CHAR
} SearchDataType;

typedef struct SearchState {
    DIA* local;
    DIA* remote;
    DIA* next_local;
    DIA* next_remote;
    SearchDataType type;
    void* searched;
    pid_t pid;
    FILE* file;
    int search_cnt;
} SearchState;

/*
    Takes in a remote DIA and reads it in a local DIA.
    The local dia iovec's are allocated inside.
*/
ssize_t read_from_remote_dia(pid_t pid, DIA* ldia, DIA* rdia) {
    for (size_t i = 0; i < rdia->size; i++) {
        void* buffer = malloc(rdia->data[i].iov_len);
        if (!buffer) {
            // TODO how to exit? Probably need to show it better than this.
            exit(EXIT_FAILURE);
        }
        struct iovec local_iov = {.iov_base = buffer,
                                  .iov_len = rdia->data[i].iov_len};

        add_iovec(ldia, local_iov);
    }

    ssize_t nread = batch_process_vm_readv(pid, ldia->data, ldia->size,
                                           rdia->data, rdia->size);
    return nread;
}

/*  Clears from start_row to end_row */
void fb_clear_rows(FrameBuffer* fb, int start_row, int end_row) {
    int i = (start_row * fb->width);
    for (; i <= end_row * fb->width; i++) {
        fb_putchar(fb, i % fb->width, i / fb->width, ' ');
    }
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
    int i = 0;

    while (i < MAX_LINES &&
           fscanf(file, "%s %s %lx %d:%d %d %[^\n]", address_range, perms,
                  &offset, &dev_major, &dev_minor, &inode, pathname) >= 4) {

        uintptr_t start, end;
        sscanf(address_range, "%lx-%lx", &start, &end);

        struct iovec temp;
        temp.iov_base = (void*)start;
        temp.iov_len = end - start;
        add_iovec(remote, temp);

        i++;
    }
    return i;
}

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
    free_iovec_array(sstate->next_remote);

    sstate->local = init_iovec_array(sstate->remote->size);

    sstate->next_remote = NULL;
    sstate->next_local = NULL;
}

/* 
    Perform a search on sstate->remote and prepare the next step.
    Finally advances the state.
*/
size_t search_step_dia(SearchState* sstate) {
    // next_remote will be filled for the next step
    sstate->next_remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
    switch (sstate->type) {
        case TYPE_UINT_32:
            read_from_remote_dia(sstate->pid, sstate->local, sstate->remote);
            search_step_for_uint32_dia(
                sstate->local, sstate->remote, sstate->next_remote,
                sstate->remote->size, (uint32_t*)sstate->searched);
            break;
        default:
            // TODO add more types
            break;
    }

    size_t found = sstate->next_remote->size;
    if (found == 0) {
        reset_current_state(sstate);
    } else {
        advance_state(sstate);
    }
    return found;
}

char* string_search_state(SearchState* sstate) {
    if (sstate == NULL){
        return NULL;
    }
    //TODO notify that there are more elements
   return string_dia(sstate->remote, ws.ws_row - 5);
}

void write_value_at_pos(SearchState* sstate, size_t pos, int32_t value) {
    if (pos >= sstate->remote->size) {
        return;
    }

    void* write_ptr = sstate->remote->data[pos].iov_base;
    // printf("\nWriting at %p ...", write_ptr);

    DIA* temp_write = init_iovec_array(1);
    struct iovec temp = {&value, sizeof(value)};
    add_iovec(temp_write, temp);

    DIA* temp_remote = init_iovec_array(1);
    struct iovec temp_r = {write_ptr, sizeof(value)};
    add_iovec(temp_remote, temp_r);

    write_to_remote_dia(sstate->pid, temp_write, temp_remote);
}

/*Will remove raw_mode, and get input at a specific location in the ui.*/
char* get_input_in_cmdbar(void) {
    int input_row = ws.ws_row - 1;
    int input_col_ps1 = 0;
    int input_col_cmd = 8;  
    char* buffer = malloc(256 * sizeof(char));

    
    disable_raw_mode();
    
    MOVE_CURSOR(input_row, input_col_ps1);
    printf(SUBCMD_PROMPT);
  
    MOVE_CURSOR(input_row, input_col_cmd);
    fgets(buffer, sizeof(buffer), stdin);

    enable_raw_mode();

    // Clear the line
    MOVE_CURSOR(input_row, input_col_ps1);
    printf("\033[2K");

    return buffer;
}

/*Main command loop, parses the various subcommands and then dispatch.*/
void handle_cmd(FrameBuffer* fb, SearchState* sstate, char cmd) {
    switch (cmd) {
        case 's':;
            char* s_subcmd = get_input_in_cmdbar();
            int32_t search_value;
            if (sscanf(s_subcmd, "%d", &search_value) != 1) {
                return;
            }
            sstate->searched = &search_value;
            search_step_dia(sstate);
            // INTENTIONAL FALLTROUGH
        case 'p':
            fb_clear_rows(fb, 3, fb->height - 1);
            char* search_state = string_search_state(sstate);
            fb_putstr(fb, 0, 3, search_state);
            break;
        case 'w':;
            char* w_subcmd = get_input_in_cmdbar();
            size_t pos;
            int32_t value;
            if (sscanf(w_subcmd, "%zu %d", &pos, &value) != 2) {
                return;
            }
            write_value_at_pos(sstate, pos, value);
            break;
        case 'q':  //quit
            printf(CLEAR_SCREEN);
            printf(SHOW_CURSOR);
            disable_raw_mode();
            printf("\nQuitting...");
            exit(0);
            break;
        default:
            // TODO Add to content that the command was not recogn
            break;
    }
}

/* Create the current buffer by adding he header, content, footer*/
void draw(FrameBuffer* fb) {
    add_header(fb);
    add_footer(fb);
    fb_swap(fb);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "\nUsage: meemo <pid>");
        exit(1);
    }

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigwinchHandler;
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        exit(1);
    }

    // Parse and validate PID
    pid_t user_input_pid;
    if (sscanf(argv[1], "%d", &user_input_pid) != 1) {
        printf("\nInvalid PID input.");
        return 1;
    }
    if (user_input_pid <= 0) {
        printf("\nInvalid PID value.");
        return 1;
    }
    // printf("\nInput PID = %d", user_input_pid);

    // Read maps of process
    char path[50];
    sprintf(path, "/proc/%d/maps", user_input_pid);
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    // Setup initial Search State
    SearchDataType type = TYPE_UINT_32;
    DIA* remote = init_iovec_array(INITIAL_IOVEC_ARRAY_CAP);
    int n = fill_remote_dia(remote, file);
    DIA* local = init_iovec_array(n);
    DIA* next_remote = NULL;
    DIA* next_local = NULL;

    SearchState initial_sstate = {local,          remote, next_local,
                                  next_remote,    type,   NULL,
                                  user_input_pid, file,   0};

    // Main Render loop
    enable_raw_mode();
    update_terminal_size();

    FrameBuffer current_buffer = init_fb(ws.ws_col, ws.ws_row);
    printf(CLEAR_SCREEN);
    while (1) {

        // Check if in need of a resize
        if (fb_need_resize) {
            printf(CLEAR_SCREEN);
            update_terminal_size();
            // Free old and create new TODO realloc?
            free_fb(&current_buffer);
            current_buffer = init_fb(ws.ws_col, ws.ws_row);
            fb_need_resize = 0;
        }

        // Draw buffer for frame x
        // Input that changes the buffer for frame x+1
        // Wait
        draw(&current_buffer);
        process_input(&current_buffer, &initial_sstate);
        usleep(10000);  //TODO find acceptable refresh rate
    }

    fclose(file);
    return 0;
}
