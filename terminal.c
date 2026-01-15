#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Global state variables */
uint8_t current_theme_index = 0;
int line_break_enabled = 1;
int time_format = TIME_FORMAT_24H;
TerminalManager terminal_manager = {0};
int terminal_layout_mode = 0;

/*
 * Initialize command queue structure
 * @param queue: Pointer to CommandQueue to initialize
 */
void init_command_queue(CommandQueue *queue) {
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->state = QUEUE_NORMAL;
    for (int i = 0; i < COMMAND_QUEUE_SIZE; i++) {
        queue->commands[i][0] = '\0';
    }
}

/*
 * Update queue state based on current count
 * @param queue: Pointer to CommandQueue to update
 */
void update_queue_state(CommandQueue *queue) {
    queue->state = (queue->count >= COMMAND_QUEUE_SIZE) ? 
                   QUEUE_FULL : QUEUE_NORMAL;
}

/*
 * Check if command queue is full
 * @param queue: Pointer to CommandQueue to check
 * @return: 1 if full, 0 otherwise
 */
int is_queue_full(CommandQueue *queue) {
    return queue->count >= COMMAND_QUEUE_SIZE;
}

/*
 * Check if command queue is empty
 * @param queue: Pointer to CommandQueue to check
 * @return: 1 if empty, 0 otherwise
 */
int is_queue_empty(CommandQueue *queue) {
    return queue->count == 0;
}

/*
 * Add command to queue
 * @param queue: Pointer to CommandQueue
 * @param cmd: Command string to add
 * @return: 1 on success, 0 on failure (queue full)
 */
int add_to_queue(CommandQueue *queue, const char *cmd) {
    if (is_queue_full(queue)) {
        queue->state = QUEUE_FULL;
        return 0;
    }
    
    strncpy(queue->commands[queue->tail], cmd, MAX_CMD_INPUT - 1);
    queue->commands[queue->tail][MAX_CMD_INPUT - 1] = '\0';
    queue->tail = (queue->tail + 1) % COMMAND_QUEUE_SIZE;
    queue->count++;
    update_queue_state(queue);
    return 1;
}

/*
 * Get next command from queue
 * @param queue: Pointer to CommandQueue
 * @param cmd: Buffer to store retrieved command
 * @return: 1 if command retrieved, 0 if queue empty
 */
int get_from_queue(CommandQueue *queue, char *cmd) {
    if (is_queue_empty(queue)) {
        return 0;
    }
    
    strncpy(cmd, queue->commands[queue->head], MAX_CMD_INPUT - 1);
    cmd[MAX_CMD_INPUT - 1] = '\0';
    queue->head = (queue->head + 1) % COMMAND_QUEUE_SIZE;
    queue->count--;
    update_queue_state(queue);
    return 1;
}

/*
 * Add command to active terminal's queue
 * @param cmd: Command string to queue
 */
void add_command_to_queue(const char *cmd) {
    Terminal* active = get_active_terminal();
    if (add_to_queue(&active->cmd_queue, cmd)) {
        char msg[128];
        snprintf(msg, sizeof(msg), 
                 "Command added to queue. Queue size: %d/%d", 
                 active->cmd_queue.count, COMMAND_QUEUE_SIZE
                );
        add_history_line(&active->history, msg, HISTORY_TYPE_NORMAL);
    } else {
        add_history_line(&active->history, 
                         "Command queue is full! Maximum 10 commands allowed.", 
                         HISTORY_TYPE_RAW
                        );
        active->input.is_locked = 1;
    }
}

/*
 * Process next command from queue if no command is running
 */
void process_command_queue() {
    Terminal* active = get_active_terminal();
    
    if (!is_command_running() && !is_queue_empty(&active->cmd_queue)) {
        char next_cmd[MAX_CMD_INPUT];
        if (get_from_queue(&active->cmd_queue, next_cmd)) {
            if (active->cmd_queue.state == QUEUE_NORMAL) {
                active->input.is_locked = 0;
            }
            execute_command(next_cmd, &active->history, &active->input);
        }
    }
}

/*
 * Check if command is currently running in active terminal
 * @return: 1 if command running, 0 otherwise
 */
int is_command_running() {
    Terminal* active = get_active_terminal();
    return active->cmd_state == CMD_STATE_RUNNING;
}

/*
 * Stop currently running command (emulate Ctrl+C)
 */
void stop_current_command() {
    Terminal* active = get_active_terminal();
    
    if (active->cmd_state == CMD_STATE_RUNNING && active->current_process > 0) {
        kill(active->current_process, SIGINT);
        add_history_line(&active->history, 
                         "Command interrupted (SIGINT sent)", 
                         HISTORY_TYPE_NORMAL
                        );
        active->cmd_state = CMD_STATE_READY;
        active->current_process = 0;
    } else {
        add_history_line(&active->history, 
                         "No command is currently running", 
                         HISTORY_TYPE_NORMAL
                        );
    }
}

/*
 * Remove ANSI escape codes from string
 * @param str: String to clean
 */
void strip_escape_codes(char* str) {
    char* src = str;
    char* dst = str;
    int in_escape = 0;
    
    while (*src) {
        if (*src == '\033') {
            in_escape = 1;
            src++;
        } else if (in_escape && *src == 'm') {
            in_escape = 0;
            src++;
        } else if (in_escape) {
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/*
 * Initialize colors with default theme
 */
void init_colors() {
    start_color();
    use_default_colors();
    
    // Initialize all color pairs for terminal interface
    init_pair(COLOR_TEXT, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_PROMPT, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_ERROR, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_DIRECTORY, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_TIME, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_TIME_QUEUE_FULL, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_USER, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_FILE, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_LOGO, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_HEADER, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_HEADER_BG, COLOR_BLACK, COLOR_BLACK);
    init_pair(COLOR_HEADER_SEP, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_TERMINAL_TAB_ACTIVE, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_TERMINAL_TAB_INACTIVE, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_TERMINAL_TAB_HIGHLIGHT, COLOR_YELLOW, COLOR_BLUE);
    
    bkgd(COLOR_PAIR(COLOR_TEXT));
}

/*
 * Draw locked input indicator with hash symbols
 * @param width: Number of hash symbols to draw
 */
void draw_locked_input(int width) {
    attron(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
    for (int i = 0; i < width; i++) {
        printw("#");
    }
    attroff(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
}

/*
 * Draw complete terminal interface with history, tabs, and prompt
 * @param history: History buffer to display
 * @param input: Current input state
 * @param show_cursor: Whether to show cursor (1) or not (0)
 */
void draw_interface(HistoryBuffer *history, InputState *input, int show_cursor) {
    clear();
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    draw_terminal_tabs();
    
    int content_width = max_x;
    int history_height = max_y - 2;
    int start_line = history->count - history_height - history->scroll_offset;
    if (start_line < 0) start_line = 0;
    
    /* Display history lines with proper highlighting */
    for (int i = start_line; i < history->count; i++) {
        int screen_line = i - start_line + 2;
        if (screen_line >= history_height + 2) break;
        
        move(screen_line, 0);
        clrtoeol();
        
        const char *line_text = history->lines[i];
        
        if (history->line_types[i] == HISTORY_TYPE_RAW) {
            attron(COLOR_PAIR(COLOR_TEXT));
            printw("%s", line_text);
            attroff(COLOR_PAIR(COLOR_TEXT));
        } else {
            int line_len = strlen(line_text);
            
            if (line_len > content_width) {
                char truncated_line[content_width + 1];
                strncpy(truncated_line, line_text, content_width);
                truncated_line[content_width] = '\0';
                highlight_text(truncated_line, history->line_types[i]);
            } else {
                highlight_text(line_text, history->line_types[i]);
            }
        }
    }
    
    /* Display prompt line with real-time clock */
    move(max_y - 1, 0); 
    clrtoeol();
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[32];
    
    strftime(time_buf, sizeof(time_buf), "[%H:%M:%S]:", t);
    
    Terminal* active = get_active_terminal();
    
    /* Color time based on queue state */
    if (active->cmd_queue.state == QUEUE_FULL) {
        attron(COLOR_PAIR(COLOR_TIME_QUEUE_FULL) | A_BOLD | A_BLINK);
    } else {
        attron(COLOR_PAIR(COLOR_TIME) | A_BOLD);
    }
    printw("%s ", time_buf);
    if (active->cmd_queue.state == QUEUE_FULL) {
        attroff(COLOR_PAIR(COLOR_TIME_QUEUE_FULL) | A_BOLD | A_BLINK);
    } else {
        attroff(COLOR_PAIR(COLOR_TIME) | A_BOLD);
    }
    
    /* Show command state indicator */
    if (active->cmd_state == CMD_STATE_RUNNING) {
        attron(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
        printw("[RUNNING] ");
        attroff(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
    } else if (!is_queue_empty(&active->cmd_queue)) {
        attron(COLOR_PAIR(COLOR_DIRECTORY) | A_BOLD);
        printw("[QUEUED:%d/%d] ", 
               active->cmd_queue.count, 
               COMMAND_QUEUE_SIZE
              );
        attroff(COLOR_PAIR(COLOR_DIRECTORY) | A_BOLD);
    }
    
    int prompt_len = strlen(time_buf) + 1;
    if (active->cmd_state == CMD_STATE_RUNNING) prompt_len += 10;
    else if (!is_queue_empty(&active->cmd_queue)) prompt_len += 12;
    
    int available_width = content_width - prompt_len - 2;
    
    if (input->is_locked) {
        draw_locked_input(available_width);
    } else {
        if (input->input_len <= available_width) {
            highlight_text_with_files(input->input);
        } else {
            if (input->display_start + available_width > input->input_len) {
                input->display_start = input->input_len - available_width;
            }
            if (input->display_start < 0) input->display_start = 0;
            
            char display_part[available_width + 1];
            strncpy(display_part, 
                    input->input + input->display_start, 
                    available_width
                   );
            display_part[available_width] = '\0';
            highlight_text_with_files(display_part);
        }
    }
    
    /* Position cursor appropriately */
    if (show_cursor && !input->is_locked) {
        int cursor_display_pos = input->cursor_pos - input->display_start;
        if (cursor_display_pos < 0) {
            input->display_start = input->cursor_pos;
            cursor_display_pos = 0;
        } else if (cursor_display_pos >= available_width) {
            input->display_start = input->cursor_pos - available_width + 1;
            cursor_display_pos = available_width - 1;
        }
        move(max_y - 1, prompt_len + cursor_display_pos);
    } else if (input->is_locked) {
        move(max_y - 1, prompt_len + available_width);
    }
    
    refresh();
}

/*
 * Update display with current state
 */
void update_real_time_display() {
    Terminal* active = get_active_terminal();
    draw_interface(&active->history, &active->input, 1);
}

/*
 * Update input lock state based on queue status
 * @param input: InputState to update
 * @param queue: CommandQueue to check
 */
void update_input_lock_state(InputState *input, CommandQueue *queue) {
    input->is_locked = (queue->state == QUEUE_FULL);
}

/*
 * Initialize terminal manager and first terminal
 */
void init_terminal_manager(void) {
    terminal_manager.terminals = malloc(sizeof(Terminal) * MAX_TERMINALS);
    if (!terminal_manager.terminals) {
        fprintf(stderr, "Critical error: Failed to allocate memory for terminals\n");
        exit(1);
    }
    
    terminal_manager.terminal_count = 1;
    terminal_manager.active_terminal = 0;
    
    init_history_buffer(&terminal_manager.terminals[0].history);
    init_input_state(&terminal_manager.terminals[0].input);
    init_command_queue(&terminal_manager.terminals[0].cmd_queue);
    terminal_manager.terminals[0].id = 0;
    terminal_manager.terminals[0].split_with = -1;
    terminal_manager.terminals[0].cmd_state = CMD_STATE_READY;
    terminal_manager.terminals[0].current_process = 0;
    getcwd(terminal_manager.terminals[0].current_directory, 
           sizeof(terminal_manager.terminals[0].current_directory)
          );
}

/*
 * Get pointer to currently active terminal
 * @return: Pointer to active Terminal
 */
Terminal* get_active_terminal(void) {
    return &terminal_manager.terminals[terminal_manager.active_terminal];
}

/*
 * Create new terminal tab
 */
void create_new_terminal(void) {
    if (terminal_manager.terminal_count >= MAX_TERMINALS) return;
    
    int new_id = terminal_manager.terminal_count;
    Terminal* new_term = &terminal_manager.terminals[new_id];
    
    init_history_buffer(&new_term->history);
    init_input_state(&new_term->input);
    init_command_queue(&new_term->cmd_queue);
    new_term->id = new_id;
    new_term->split_with = -1;
    new_term->cmd_state = CMD_STATE_READY;
    new_term->current_process = 0;
    
    Terminal* active = get_active_terminal();
    strncpy(new_term->current_directory, 
            active->current_directory, 
            sizeof(new_term->current_directory)
           );
    
    terminal_manager.terminal_count++;
    show_welcome_message(&new_term->history);
    chdir(new_term->current_directory);
}

/*
 * Create split terminal in specified direction
 * @param split_direction: SPLIT_HORIZONTAL or SPLIT_VERTICAL
 */
void create_split_terminal(int split_direction) {
    if (terminal_manager.terminal_count >= MAX_TERMINALS) return;
    
    int active_id = terminal_manager.active_terminal;
    int new_id = terminal_manager.terminal_count;
    
    init_history_buffer(&terminal_manager.terminals[new_id].history);
    init_input_state(&terminal_manager.terminals[new_id].input);
    init_command_queue(&terminal_manager.terminals[new_id].cmd_queue);
    terminal_manager.terminals[new_id].id = new_id;
    terminal_manager.terminals[new_id].split_with = active_id;
    terminal_manager.terminals[new_id].split_direction = split_direction;
    terminal_manager.terminals[new_id].cmd_state = CMD_STATE_READY;
    terminal_manager.terminals[new_id].current_process = 0;
    
    strncpy(terminal_manager.terminals[new_id].current_directory,
            terminal_manager.terminals[active_id].current_directory,
            sizeof(terminal_manager.terminals[new_id].current_directory)
           );
    
    terminal_manager.terminals[active_id].split_with = new_id;
    terminal_manager.terminals[active_id].split_direction = split_direction;
    
    terminal_manager.terminal_count++;
    show_welcome_message(&terminal_manager.terminals[new_id].history);
    switch_terminal(new_id);
}

/*
 * Switch to specified terminal by ID
 * @param terminal_id: ID of terminal to switch to
 */
void switch_terminal(int terminal_id) {
    if (terminal_id >= 0 && terminal_id < terminal_manager.terminal_count) {
        Terminal* current = get_active_terminal();
        getcwd(current->current_directory, sizeof(current->current_directory));
        
        terminal_manager.active_terminal = terminal_id;
        
        Terminal* new_active = get_active_terminal();
        chdir(new_active->current_directory);
    }
}

/*
 * Switch to next terminal in sequence
 */
void next_terminal(void) {
    int next = (terminal_manager.active_terminal + 1) % terminal_manager.terminal_count;
    switch_terminal(next);
}

/*
 * Switch to previous terminal in sequence
 */
void prev_terminal(void) {
    int prev = (terminal_manager.active_terminal - 1 + terminal_manager.terminal_count) % 
                terminal_manager.terminal_count;
    switch_terminal(prev);
}

/*
 * Close currently active terminal
 */
void close_current_terminal(void) {
    if (terminal_manager.terminal_count <= 1) return;
    
    int active_id = terminal_manager.active_terminal;
    
    if (terminal_manager.terminals[active_id].cmd_state == CMD_STATE_RUNNING) {
        stop_current_command();
    }
    
    int split_with = terminal_manager.terminals[active_id].split_with;
    if (split_with != -1) {
        terminal_manager.terminals[split_with].split_with = -1;
    }
    
    free_history_buffer(&terminal_manager.terminals[active_id].history);
    free_input_state(&terminal_manager.terminals[active_id].input);
    
    /* Shift terminals array to fill gap */
    for (int i = active_id; i < terminal_manager.terminal_count - 1; i++) {
        terminal_manager.terminals[i] = terminal_manager.terminals[i + 1];
        terminal_manager.terminals[i].id = i;
        
        if (terminal_manager.terminals[i].split_with != -1) {
            if (terminal_manager.terminals[i].split_with > active_id) {
                terminal_manager.terminals[i].split_with--;
            }
        }
    }
    
    terminal_manager.terminal_count--;
    
    if (terminal_manager.active_terminal >= terminal_manager.terminal_count) {
        terminal_manager.active_terminal = terminal_manager.terminal_count - 1;
    }
}

/*
 * Display welcome message with logo and help
 * @param history: History buffer to add message to
 */
void show_welcome_message(HistoryBuffer *history) {
    char version_msg[128];
    snprintf(version_msg, sizeof(version_msg), 
             "Welcome to Parrot Terminal Version %s", 
             PARROT_VERSION
            );
    add_history_line(history, version_msg, HISTORY_TYPE_RAW);
    add_history_line(history, "==========================================", HISTORY_TYPE_RAW);
    add_history_line(history, "Type 'exit' to quit", HISTORY_TYPE_RAW);
    add_history_line(history, "Shift+T: New terminal, Shift+W: Close terminal", HISTORY_TYPE_RAW);
    add_history_line(history, "Alt+1-9: Switch terminals, Alt+/-: Next/Prev terminal", HISTORY_TYPE_RAW);
    add_history_line(history, "Alt+Arrows: Switch between split panes", HISTORY_TYPE_RAW);
    add_history_line(history, "Arrows: Scroll terminal history", HISTORY_TYPE_RAW);
    add_history_line(history, "Shift+Up/Down: Command history", HISTORY_TYPE_RAW);
    add_history_line(history, "", HISTORY_TYPE_RAW);
}

/*
 * Initialize history buffer with default capacity
 * @param buf: HistoryBuffer to initialize
 */
void init_history_buffer(HistoryBuffer *buf) {
    buf->capacity = 500;
    buf->count = 0;
    buf->scroll_offset = 0;
    buf->lines = malloc(buf->capacity * sizeof(char*));
    buf->line_types = malloc(buf->capacity * sizeof(int));
    buf->timestamps = malloc(buf->capacity * sizeof(time_t));
    
    if (!buf->lines || !buf->line_types || !buf->timestamps) {
        fprintf(stderr, "Critical error: Failed to allocate history buffer\n");
        exit(2);
    }
}

/*
 * Add line to history buffer
 * @param buf: HistoryBuffer to add to
 * @param text: Text to add
 * @param line_type: Type of line (normal, command, raw)
 */
void add_history_line(HistoryBuffer *buf, const char *text, int line_type) {
    if (buf->count >= buf->capacity) {
        buf->capacity *= 2;
        buf->lines = realloc(buf->lines, buf->capacity * sizeof(char*));
        buf->line_types = realloc(buf->line_types, buf->capacity * sizeof(int));
        buf->timestamps = realloc(buf->timestamps, buf->capacity * sizeof(time_t));
        
        if (!buf->lines || !buf->line_types || !buf->timestamps) {
            fprintf(stderr, "Critical error: Failed to reallocate history buffer\n");
            exit(3);
        }
    }
    
    if (line_break_enabled) {
        buf->lines[buf->count] = strdup(text);
    } else {
        char *cleaned = malloc(strlen(text) + 1);
        char *dst = cleaned;
        const char *src = text;
        while (*src) {
            *dst++ = (*src == '\n') ? ' ' : *src;
            src++;
        }
        *dst = '\0';
        buf->lines[buf->count] = cleaned;
    }
    
    buf->line_types[buf->count] = line_type;
    buf->timestamps[buf->count] = time(NULL);
    buf->count++;
}

/*
 * Free all resources associated with history buffer
 * @param buf: HistoryBuffer to free
 */
void free_history_buffer(HistoryBuffer *buf) {
    for (int i = 0; i < buf->count; i++) {
        free(buf->lines[i]);
    }
    free(buf->lines);
    free(buf->line_types);
    free(buf->timestamps);
}

/*
 * Initialize input state structure
 * @param input: InputState to initialize
 */
void init_input_state(InputState *input) {
    input->input[0] = '\0';
    input->cursor_pos = 0;
    input->input_len = 0;
    input->display_start = 0;
    input->cmd_history_count = 0;
    input->cmd_history_pos = -1;
    input->is_locked = 0;
    
    for (int i = 0; i < MAX_CMD_HISTORY; i++) {
        input->cmd_history[i] = NULL;
    }
}

/*
 * Add command to command history
 * @param input: InputState containing command history
 * @param cmd: Command string to add
 */
void add_to_cmd_history(InputState *input, const char *cmd) {
    if (input->cmd_history_count >= MAX_CMD_HISTORY) {
        free(input->cmd_history[0]);
        for (int i = 1; i < MAX_CMD_HISTORY; i++) {
            input->cmd_history[i-1] = input->cmd_history[i];
        }
        input->cmd_history_count--;
    }
    
    input->cmd_history[input->cmd_history_count] = strdup(cmd);
    input->cmd_history_count++;
    input->cmd_history_pos = input->cmd_history_count;
}

/*
 * Free input state resources
 * @param input: InputState to free
 */
void free_input_state(InputState *input) {
    for (int i = 0; i < input->cmd_history_count; i++) {
        free(input->cmd_history[i]);
    }
}

/*
 * Shorten path for display (replace home with ~, truncate middle components)
 * @param path: Full path to shorten
 * @param output: Buffer for shortened path
 * @param output_size: Size of output buffer
 */
void shorten_path(char *path, char *output, size_t output_size) {
    if (strlen(path) == 0) {
        strncpy(output, path, output_size);
        return;
    }
    
    /* Replace home directory with ~ */
    const char* home = getenv("HOME");
    char modified_path[PATH_MAX];
    if (home && strncmp(path, home, strlen(home)) == 0) {
        snprintf(modified_path, sizeof(modified_path), 
                 "~%s", 
                 path + strlen(home)
                );
    } else {
        strncpy(modified_path, path, sizeof(modified_path));
    }
    
    char *components[64];
    int count = 0;
    char *path_copy = strdup(modified_path);
    char *token = strtok(path_copy, "/");
    
    while (token != NULL && count < 63) {
        components[count++] = token;
        token = strtok(NULL, "/");
    }
    
    output[0] = '\0';
    
    if (modified_path[0] == '/') {
        strncat(output, "/", output_size - 1);
    }
    
    for (int i = 0; i < count; i++) {
        if (i == count - 1) {
            if (strlen(components[i]) > 12) {
                char shortened[16];
                strncpy(shortened, components[i], 12);
                shortened[12] = '\0';
                strcat(shortened, "...");
                strncat(output, shortened, output_size - strlen(output) - 1);
            } else {
                strncat(output, components[i], output_size - strlen(output) - 1);
            }
        } else {
            if (strlen(components[i]) > 0) {
                char first_letter[2] = {components[i][0], '\0'};
                strncat(output, first_letter, output_size - strlen(output) - 1);
                if (i < count - 1) {
                    strncat(output, "/", output_size - strlen(output) - 1);
                }
            }
        }
    }
    
    free(path_copy);
}

/*
 * Get prompt information (time and directory)
 * @param time_buf: Buffer for time string
 * @param time_size: Size of time buffer
 * @param dir_buf: Buffer for directory string
 * @param dir_size: Size of directory buffer
 */
void get_prompt_info(char *time_buf, size_t time_size, 
                     char *dir_buf, size_t dir_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(time_buf, time_size, "%H:%M:%S", t);
    
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    shorten_path(cwd, dir_buf, dir_size);
}

/*
 * Check if file exists at given path
 * @param path: Path to check
 * @return: 1 if file exists, 0 otherwise
 */
int is_existing_file(const char *path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

/*
 * Highlight text with file path recognition
 * @param text: Text to highlight
 */
void highlight_text_with_files(const char *text) {
    char *copy = strdup(text);
    char *token = strtok(copy, " ");
    int pos = 0;
    
    while (token != NULL) {
        /* Check for file paths */
        if (is_existing_file(token) || 
            strstr(token, "/") != NULL || 
            strstr(token, "./") == token ||
            strstr(token, "../") == token ||
            strstr(token, "~/") == token) {
            
            if (access(token, F_OK) == 0) {
                struct stat st;
                if (stat(token, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        attron(COLOR_PAIR(COLOR_DIRECTORY) | A_BOLD);
                    } else {
                        attron(COLOR_PAIR(COLOR_FILE) | A_UNDERLINE);
                    }
                } else {
                    attron(COLOR_PAIR(COLOR_FILE));
                }
            } else {
                attron(COLOR_PAIR(COLOR_FILE) | A_DIM);
            }
            
            printw("%s", token);
            attroff(COLOR_PAIR(COLOR_FILE) | A_UNDERLINE | A_BOLD | A_DIM);
            attroff(COLOR_PAIR(COLOR_DIRECTORY) | A_BOLD);
        } else {
            /* Check for error keywords */
            if (strstr(token, "error") || strstr(token, "Error") || 
                strstr(token, "ERROR") || strstr(token, "No such") ||
                strstr(token, "Permission denied") || strstr(token, "command not found") ||
                strstr(token, "fail") || strstr(token, "Fail") || strstr(token, "FAIL")) {
                attron(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
            } else {
                attron(COLOR_PAIR(COLOR_TEXT));
            }
            printw("%s", token);
            attroff(COLOR_PAIR(COLOR_ERROR) | A_BOLD);
            attroff(COLOR_PAIR(COLOR_TEXT));
        }
        
        pos += strlen(token);
        token = strtok(NULL, " ");
        if (token != NULL) {
            printw(" ");
            pos++;
        }
    }
    
    free(copy);
}

/*
 * Highlight text based on line type
 * @param text: Text to highlight
 * @param line_type: Type of line (normal, command, raw)
 */
void highlight_text(const char *text, int line_type) {
    if (line_type == HISTORY_TYPE_COMMAND) {
        if (text[0] == '[' && strchr(text, ']')) {
            char *time_end = strchr(text, ']');
            int time_len = time_end - text + 1;
            
            attron(COLOR_PAIR(COLOR_TIME));
            printw("%.*s", time_len, text);
            attroff(COLOR_PAIR(COLOR_TIME));
            
            attron(COLOR_PAIR(COLOR_TEXT));
            printw("%s", time_end + 1);
            attroff(COLOR_PAIR(COLOR_TEXT));
        } else {
            attron(COLOR_PAIR(COLOR_TEXT));
            printw("%s", text);
            attroff(COLOR_PAIR(COLOR_TEXT));
        }
    } else {
        highlight_text_with_files(text);
    }
}

/*
 * Draw terminal tabs with new visual design
 */
void draw_terminal_tabs(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    /* Draw tab background */
    attron(COLOR_PAIR(COLOR_HEADER_BG));
    for (int i = 0; i < max_x; i++) {
        mvaddch(0, i, ' ');
    }
    attroff(COLOR_PAIR(COLOR_HEADER_BG));
    
    /* Calculate dynamic tab width */
    int tab_width = max_x / terminal_manager.terminal_count;
    if (tab_width < 15) tab_width = 15;
    
    for (int i = 0; i < terminal_manager.terminal_count; i++) {
        int start_x = i * tab_width;
        if (start_x >= max_x) break;
        
        int width = (i == terminal_manager.terminal_count - 1) ? 
                   (max_x - start_x) : tab_width;
        
        /* Get directory for tab */
        char* cwd = terminal_manager.terminals[i].current_directory;
        char dir_buf[PATH_MAX];
        shorten_path(cwd, dir_buf, sizeof(dir_buf));
        
        /* Format tab text */
        char tab_text[128];
        snprintf(tab_text, sizeof(tab_text), 
                 " [%d] %s ", 
                 i + 1, 
                 dir_buf
                );
        
        /* Truncate if too long */
        int text_len = strlen(tab_text);
        if (text_len > width - 4) {
            tab_text[width - 7] = '.';
            tab_text[width - 6] = '.';
            tab_text[width - 5] = '.';
            tab_text[width - 4] = ' ';
            tab_text[width - 3] = '\0';
        }
        
        if (i == terminal_manager.active_terminal) {
            /* Active tab - black text on cyan background */
            attron(COLOR_PAIR(COLOR_TERMINAL_TAB_ACTIVE) | A_BOLD);
            mvaddch(0, start_x, ACS_VLINE);
            mvaddch(0, start_x + width - 1, ACS_VLINE);
            
            for (int x = start_x + 1; x < start_x + width - 1; x++) {
                mvaddch(0, x, ' ');
            }
            
            int text_start = start_x + (width - strlen(tab_text)) / 2;
            if (text_start < start_x) text_start = start_x + 1;
            
            mvprintw(0, text_start, "%s", tab_text);
            attroff(COLOR_PAIR(COLOR_TERMINAL_TAB_ACTIVE) | A_BOLD);
        } else {
            /* Inactive tab - white text on black background */
            attron(COLOR_PAIR(COLOR_TERMINAL_TAB_INACTIVE));
            mvaddch(0, start_x, ACS_VLINE);
            mvprintw(0, start_x + 1, "%s", tab_text);
            
            for (int x = start_x + 1 + strlen(tab_text); x < start_x + width - 1; x++) {
                mvaddch(0, x, ' ');
            }
            
            mvaddch(0, start_x + width - 1, ACS_VLINE);
            attroff(COLOR_PAIR(COLOR_TERMINAL_TAB_INACTIVE));
        }
    }
    
    /* Draw separator line */
    attron(COLOR_PAIR(COLOR_HEADER_SEP) | A_BOLD);
    for (int i = 0; i < max_x; i++) {
        mvaddch(1, i, ACS_HLINE);
    }
    
    mvaddch(1, 0, ACS_LTEE);
    mvaddch(1, max_x - 1, ACS_RTEE);
    attroff(COLOR_PAIR(COLOR_HEADER_SEP) | A_BOLD);
}

/*
 * Execute command with proper process management
 * @param cmd: Command string to execute
 * @param history: History buffer for output
 * @param input: Input state for command history
 */
void execute_command(const char *cmd, HistoryBuffer *history, InputState *input) {
    if (strlen(cmd) == 0) return;
    
    Terminal* active = get_active_terminal();
    
    /* Handle stop command */
    if (strcmp(cmd, "stop") == 0) {
        stop_current_command();
        return;
    }
    
    /* Handle manual command */
    if (strcmp(cmd, "manual") == 0) {
        add_history_line(history, "Parrot Terminal Usage:", HISTORY_TYPE_RAW);
        add_history_line(history, "======================", HISTORY_TYPE_RAW);
        add_history_line(history, "Shift+T: Create new terminal", HISTORY_TYPE_RAW);
        add_history_line(history, "Shift+W: Close current terminal", HISTORY_TYPE_RAW);
        add_history_line(history, "Alt+1-9: Switch to terminal 1-9", HISTORY_TYPE_RAW);
        add_history_line(history, "Alt+/-: Switch to next/previous terminal", HISTORY_TYPE_RAW);
        add_history_line(history, "Alt+Arrows: Switch between split panes", HISTORY_TYPE_RAW);
        add_history_line(history, "Arrow Keys: Scroll terminal history", HISTORY_TYPE_RAW);
        add_history_line(history, "Shift+Up/Down: Navigate command history", HISTORY_TYPE_RAW);
        add_history_line(history, "Type 'stop' to interrupt running command", HISTORY_TYPE_RAW);
        add_history_line(history, "Type 'exit' to quit", HISTORY_TYPE_RAW);
        add_history_line(history, "Note: Commands queue automatically when another is running", HISTORY_TYPE_RAW);
        add_history_line(history, "Queue size: 10 commands max", HISTORY_TYPE_RAW);
        return;
    }
    
    /* Check if another command is running */
    if (is_command_running()) {
        add_command_to_queue(cmd);
        return;
    }
    
    /* Add to command history */
    if (input->cmd_history_count == 0 || 
        strcmp(cmd, input->cmd_history[input->cmd_history_count - 1]) != 0) {
        add_to_cmd_history(input, cmd);
    }
    
    /* Handle cd command specially */
    if (strncmp(cmd, "cd ", 3) == 0) {
        const char* dir = cmd + 3;
        char clean_dir[PATH_MAX];
        strncpy(clean_dir, dir, sizeof(clean_dir) - 1);
        clean_dir[sizeof(clean_dir) - 1] = '\0';
        
        char *start = clean_dir;
        while (*start == ' ') start++;
        char *end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\n')) {
            *end = '\0';
            end--;
        }
        
        if (strcmp(start, "~") == 0) {
            const char* home = getenv("HOME");
            if (home) strcpy(clean_dir, home);
        } else if (strncmp(start, "~/", 2) == 0) {
            const char* home = getenv("HOME");
            if (home) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), 
                         "%s%s", 
                         home, 
                         start + 1
                        );
                strcpy(clean_dir, full_path);
            }
        }
        
        if (chdir(clean_dir) != 0) {
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), 
                     "cd: %s: %s", 
                     clean_dir, 
                     strerror(errno)
                    );
            add_history_line(history, error_msg, HISTORY_TYPE_NORMAL);
        } else {
            getcwd(active->current_directory, sizeof(active->current_directory));
        }
        return;
    } else if (strcmp(cmd, "cd") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            if (chdir(home) != 0) {
                char error_msg[128];
                snprintf(error_msg, sizeof(error_msg), 
                         "cd: %s: %s", 
                         home, 
                         strerror(errno)
                        );
                add_history_line(history, error_msg, HISTORY_TYPE_NORMAL);
            } else {
                getcwd(active->current_directory, sizeof(active->current_directory));
            }
        }
        return;
    }
    
    /* Add timestamped command to history */
    char timestamped_cmd[512];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[32];
    
    strftime(time_buf, sizeof(time_buf), "[%H:%M:%S]", t);
    
    snprintf(timestamped_cmd, sizeof(timestamped_cmd), 
             "%s %s", 
             time_buf, 
             cmd
            );
    add_history_line(history, timestamped_cmd, HISTORY_TYPE_COMMAND);
    
    /* Check for interactive applications */
    int is_interactive = 0;
    const char* interactive_commands[] = {
        "vim", "nvim", "nano", "ranger", "parrot", "htop", "top", "sudo", "ssh", "man", "less", "more", NULL
    };
    
    for (int i = 0; interactive_commands[i] != NULL; i++) {
        if (strcmp(cmd, interactive_commands[i]) == 0 || 
            strncmp(cmd, interactive_commands[i], 
                    strlen(interactive_commands[i])) == 0) {
            is_interactive = 1;
            break;
        }
    }
    
    if (is_interactive) {
        add_history_line(history, "Starting interactive application...", HISTORY_TYPE_NORMAL);
        add_history_line(history, "Note: Use Ctrl+Z to suspend and 'fg' to return", HISTORY_TYPE_NORMAL);
        
        def_prog_mode();
        endwin();
        
        resetty();
        
        int result = system(cmd);
        
        reset_prog_mode();
        refresh();
        clear();
        
        if (result != 0) {
            char result_msg[128];
            snprintf(result_msg, sizeof(result_msg), 
                     "Command returned with exit code: %d", 
                     result
                    );
            add_history_line(history, result_msg, HISTORY_TYPE_NORMAL);
        }
        
        add_history_line(history, "Returned to Parrot Terminal", HISTORY_TYPE_NORMAL);
        return;
    }
    
    /* Execute regular command with pipe for output capture */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), 
                 "Failed to create pipe: %s", 
                 strerror(errno)
                );
        add_history_line(history, error_msg, HISTORY_TYPE_NORMAL);
        return;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), 
                 "Failed to fork process: %s", 
                 strerror(errno)
                );
        add_history_line(history, error_msg, HISTORY_TYPE_NORMAL);
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        
        exit(127);
    } else {
        active->cmd_state = CMD_STATE_RUNNING;
        active->current_process = pid;
        
        close(pipefd[1]);
        
        char buffer[1024];
        ssize_t bytes_read;
        FILE* pipe_read = fdopen(pipefd[0], "r");
        
        if (pipe_read) {
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0';
                
                char* line = strtok(buffer, "\n");
                while (line != NULL) {
                    char clean_line[1024];
                    strncpy(clean_line, line, sizeof(clean_line) - 1);
                    clean_line[sizeof(clean_line) - 1] = '\0';
                    strip_escape_codes(clean_line);
                    add_history_line(history, clean_line, HISTORY_TYPE_NORMAL);
                    line = strtok(NULL, "\n");
                }
            }
            fclose(pipe_read);
        } else {
            close(pipefd[0]);
        }
        
        int status;
        waitpid(pid, &status, 0);
        
        active->cmd_state = CMD_STATE_READY;
        active->current_process = 0;
        
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            char status_msg[128];
            snprintf(status_msg, sizeof(status_msg), 
                     "Command exited with status: %d", 
                     WEXITSTATUS(status)
                    );
            add_history_line(history, status_msg, HISTORY_TYPE_NORMAL);
        } else if (WIFSIGNALED(status)) {
            char signal_msg[128];
            snprintf(signal_msg, sizeof(signal_msg), 
                     "Command terminated by signal: %d", 
                     WTERMSIG(status)
                    );
            add_history_line(history, signal_msg, HISTORY_TYPE_NORMAL);
        }
        
        process_command_queue();
    }
}

/*
 * Scroll terminal history up
 * @param history: History buffer to scroll
 */
void scroll_terminal_up(HistoryBuffer *history) {
    if (history->scroll_offset < history->count - 1) {
        history->scroll_offset++;
    }
}

/*
 * Scroll terminal history down
 * @param history: History buffer to scroll
 */
void scroll_terminal_down(HistoryBuffer *history) {
    if (history->scroll_offset > 0) {
        history->scroll_offset--;
    }
}

/*
 * Switch to specific terminal by ID
 * @param terminal_id: ID of terminal to switch to
 */
void switch_to_terminal(int terminal_id) {
    if (terminal_id >= 0 && terminal_id < terminal_manager.terminal_count) {
        switch_terminal(terminal_id);
    }
}

/*
 * Split terminal horizontally
 */
void split_terminal_horizontal(void) {
    create_split_terminal(SPLIT_HORIZONTAL);
}

/*
 * Split terminal vertically
 */
void split_terminal_vertical(void) {
    create_split_terminal(SPLIT_VERTICAL);
}

/*
 * Switch between split panes
 * @param direction: Direction to switch (unused in current implementation)
 */
void switch_split_pane(int direction) {
    Terminal* active = get_active_terminal();
    if (active->split_with != -1) {
        switch_terminal(active->split_with);
    }
}

/*
 * Handle all user input and keyboard shortcuts
 * @param input: Current input state
 * @param history: History buffer for output
 * @return: 1 if exit requested, 0 otherwise
 */
int handle_input(InputState *input, HistoryBuffer *history) {
    int ch;
    
    ch = getch();
    
    Terminal* active = get_active_terminal();
    
    /* Handle input when terminal is locked (queue full) */
    if (input->is_locked) {
        switch (ch) {
            case 20: // Shift+T - new terminal
                create_new_terminal();
                break;
                
            case 23: // Shift+W - close terminal
                close_current_terminal();
                break;
                
            case 27: // ESC sequences (Alt+keys)
                {
                    int next_ch = getch();
                    if (next_ch == -1) {
                        break;
                    }
                    
                    if (next_ch >= '1' && next_ch <= '9') {
                        switch_to_terminal(next_ch - '1');
                    } else if (next_ch == '=' || next_ch == '+') {
                        next_terminal();
                    } else if (next_ch == '-') {
                        prev_terminal();
                    } else if (next_ch == 91) { // [
                        next_ch = getch();
                        switch (next_ch) {
                            case 65: // Alt+Up
                            case 66: // Alt+Down  
                            case 67: // Alt+Right
                            case 68: // Alt+Left
                                switch_split_pane(next_ch);
                                break;
                        }
                    }
                }
                break;
                
            case KEY_UP: // Scroll up
                scroll_terminal_up(history);
                break;
                
            case KEY_DOWN: // Scroll down
                scroll_terminal_down(history);
                break;
        }
        return 0;
    }
    
    switch (ch) {
        case 20: // Shift+T - new terminal
            create_new_terminal();
            break;
            
        case 23: // Shift+W - close terminal
            close_current_terminal();
            break;
            
        case KEY_UP: // Scroll up
            scroll_terminal_up(history);
            break;
            
        case KEY_DOWN: // Scroll down
            scroll_terminal_down(history);
            break;
            
        case KEY_SR: // Shift+Up - command history previous
            if (input->cmd_history_pos > 0) {
                input->cmd_history_pos--;
                strcpy(input->input, input->cmd_history[input->cmd_history_pos]);
                input->input_len = strlen(input->input);
                input->cursor_pos = input->input_len;
            }
            break;
            
        case KEY_SF: // Shift+Down - command history next
            if (input->cmd_history_pos < input->cmd_history_count - 1) {
                input->cmd_history_pos++;
                strcpy(input->input, input->cmd_history[input->cmd_history_pos]);
                input->input_len = strlen(input->input);
                input->cursor_pos = input->input_len;
            } else if (input->cmd_history_pos == input->cmd_history_count - 1) {
                input->cmd_history_pos = input->cmd_history_count;
                input->input[0] = '\0';
                input->input_len = 0;
                input->cursor_pos = 0;
            }
            break;
            
        case 27: // ESC sequences (Alt+keys)
            {
                int next_ch = getch();
                if (next_ch == -1) {
                    break;
                }
                
                if (next_ch >= '1' && next_ch <= '9') {
                    switch_to_terminal(next_ch - '1');
                } else if (next_ch == '=' || next_ch == '+') {
                    next_terminal();
                } else if (next_ch == '-') {
                    prev_terminal();
                } else if (next_ch == 91) { // [
                    next_ch = getch();
                    switch (next_ch) {
                        case 65: // Alt+Up
                        case 66: // Alt+Down  
                        case 67: // Alt+Right
                        case 68: // Alt+Left
                            switch_split_pane(next_ch);
                            break;
                    }
                }
            }
            break;
            
        case '\n': // Enter - execute command
            if (input->input_len > 0) {
                input->input[input->input_len] = '\0';
                
                if (strcmp(input->input, "exit") == 0) {
                    return 1;
                }
                
                execute_command(input->input, history, input);
                
                input->input[0] = '\0';
                input->input_len = 0;
                input->cursor_pos = 0;
                input->display_start = 0;
                input->cmd_history_pos = input->cmd_history_count;
            }
            break;
            
        case KEY_BACKSPACE:
        case 127: // Backspace
            if (input->cursor_pos > 0) {
                memmove(&input->input[input->cursor_pos - 1], 
                       &input->input[input->cursor_pos], 
                       input->input_len - input->cursor_pos + 1
                      );
                input->cursor_pos--;
                input->input_len--;
            }
            break;
            
        case KEY_LEFT:
            if (input->cursor_pos > 0) input->cursor_pos--;
            break;
            
        case KEY_RIGHT:
            if (input->cursor_pos < input->input_len) input->cursor_pos++;
            break;
            
        case KEY_HOME:
            input->cursor_pos = 0;
            input->display_start = 0;
            break;
            
        case KEY_END:
            input->cursor_pos = input->input_len;
            break;
            
        case KEY_DC: // Delete
            if (input->cursor_pos < input->input_len) {
                memmove(&input->input[input->cursor_pos], 
                       &input->input[input->cursor_pos + 1], 
                       input->input_len - input->cursor_pos
                      );
                input->input_len--;
            }
            break;
            
        default:
            if (ch >= 32 && ch <= 126 && input->input_len < MAX_CMD_INPUT - 1) {
                if (input->cursor_pos < input->input_len) {
                    memmove(&input->input[input->cursor_pos + 1], 
                           &input->input[input->cursor_pos], 
                           input->input_len - input->cursor_pos + 1
                          );
                }
                input->input[input->cursor_pos] = ch;
                input->cursor_pos++;
                input->input_len++;
            }
            break;
    }
    
    return 0;
}
