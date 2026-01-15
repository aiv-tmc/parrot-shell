#ifndef TERMINAL_H
#define TERMINAL_H

#include <ncurses.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/stat.h>
#include <signal.h>

/* Version and configuration constants */
#define PARROT_VERSION "v6.0.0"
#define MAX_CMD_INPUT 512
#define MAX_HISTORY 512
#define MAX_CMD_HISTORY 256
#define MAX_THEMES 5
#define MAX_LINE_LENGTH 512
#define MAX_TERMINALS 8
#define COMMAND_QUEUE_SIZE 10

/* Split modes for terminal division */
#define SPLIT_HORIZONTAL 0
#define SPLIT_VERTICAL 1

/* Time display formats */
#define TIME_FORMAT_24H 0
#define TIME_FORMAT_12H 1

/* History line types */
#define HISTORY_TYPE_NORMAL 0
#define HISTORY_TYPE_COMMAND 1  
#define HISTORY_TYPE_RAW 2

/* Split position constants */
#define MAX_SPLIT_PANES 4
#define SPLIT_TOP 2
#define SPLIT_BOTTOM 3

/* Command execution states */
#define CMD_STATE_READY 0
#define CMD_STATE_RUNNING 1
#define CMD_STATE_QUEUED 2

/* Queue state indicators */
#define QUEUE_NORMAL 0
#define QUEUE_FULL 1

/* Forward declarations */
typedef struct HistoryBuffer HistoryBuffer;
typedef struct InputState InputState;
typedef struct Terminal Terminal;
typedef struct TerminalManager TerminalManager;
typedef struct CommandQueue CommandQueue;

/*
 * Command queue structure for managing command execution order
 */
struct CommandQueue {
    char commands[COMMAND_QUEUE_SIZE][MAX_CMD_INPUT];
    int count;
    int head;
    int tail;
    int state;
};

/*
 * History buffer structure for storing terminal output
 */
struct HistoryBuffer {
    char **lines;
    int *line_types;
    time_t *timestamps;
    int count;
    int capacity;
    int scroll_offset;
};

/*
 * Input state structure for managing user input
 */
struct InputState {
    char input[MAX_CMD_INPUT];
    int cursor_pos;
    int input_len;
    int display_start;
    char *cmd_history[MAX_CMD_HISTORY];
    int cmd_history_count;
    int cmd_history_pos;
    int is_locked;
};

/*
 * Terminal structure representing individual terminal instance
 */
struct Terminal {
    HistoryBuffer history;
    InputState input;
    int id;
    int split_with;
    int split_direction;
    char current_directory[PATH_MAX];
    int pane_x, pane_y;
    int pane_width, pane_height;
    WINDOW* pane_win;
    CommandQueue cmd_queue;
    pid_t current_process;
    int cmd_state;
};

/*
 * Terminal manager structure for handling multiple terminals
 */
struct TerminalManager {
    Terminal *terminals;
    int terminal_count;
    int active_terminal;
    int split_layout;
};

/* Color pairs for terminal interface */
enum {
    COLOR_TEXT = 1,
    COLOR_PROMPT,
    COLOR_ERROR,
    COLOR_DIRECTORY,
    COLOR_TIME,
    COLOR_TIME_QUEUE_FULL,
    COLOR_USER,
    COLOR_FILE,
    COLOR_LOGO,
    COLOR_HEADER,
    COLOR_HEADER_BG,
    COLOR_HEADER_SEP,
    COLOR_TERMINAL_TAB_ACTIVE,
    COLOR_TERMINAL_TAB_INACTIVE,
    COLOR_TERMINAL_TAB_HIGHLIGHT
};

/* Global state variables */
extern uint8_t current_theme_index;
extern int line_break_enabled;
extern TerminalManager terminal_manager;
extern int terminal_layout_mode;
extern int time_format;

/* Function declarations */

/* Color and theme management */
void init_colors(void);

/* History buffer management */
void init_history_buffer(HistoryBuffer *buf);
void add_history_line(HistoryBuffer *buf, const char *text, int line_type);
void free_history_buffer(HistoryBuffer *buf);
void scroll_terminal_up(HistoryBuffer *history);
void scroll_terminal_down(HistoryBuffer *history);

/* Input handling */
void init_input_state(InputState *input);
void add_to_cmd_history(InputState *input, const char *cmd);
void free_input_state(InputState *input);
int handle_input(InputState *input, HistoryBuffer *history);
void update_input_lock_state(InputState *input, CommandQueue *queue);

/* Terminal management */
void init_terminal_manager(void);
Terminal* get_active_terminal(void);
void create_new_terminal(void);
void create_split_terminal(int split_direction);
void switch_terminal(int terminal_id);
void next_terminal(void);
void prev_terminal(void);
void close_current_terminal(void);
void switch_to_terminal(int terminal_id);
void split_terminal_horizontal(void);
void split_terminal_vertical(void);
void switch_split_pane(int direction);

/* UI rendering */
void draw_interface(HistoryBuffer *history, InputState *input, int show_cursor);
void draw_terminal_tabs(void);
void show_welcome_message(HistoryBuffer *history);
void show_welcome_logo(HistoryBuffer *history);

/* Command execution */
void execute_command(const char *cmd, HistoryBuffer *history, InputState *input);
void stop_current_command(void);
int is_command_running(void);
void add_command_to_queue(const char *cmd);
void process_command_queue(void);

/* Command queue management */
void init_command_queue(CommandQueue *queue);
int is_queue_full(CommandQueue *queue);
int is_queue_empty(CommandQueue *queue);
int add_to_queue(CommandQueue *queue, const char *cmd);
int get_from_queue(CommandQueue *queue, char *cmd);
void update_queue_state(CommandQueue *queue);

/* Utility functions */
void shorten_path(char *path, char *output, size_t output_size);
void get_prompt_info(char *time_buf, size_t time_size, 
                     char *dir_buf, size_t dir_size);
int is_existing_file(const char *path);
void highlight_text_with_files(const char *text);
void highlight_text(const char *text, int line_type);
void strip_escape_codes(char* str);
void draw_locked_input(int width);

/* Real-time updates */
void update_real_time_display(void);

#endif
