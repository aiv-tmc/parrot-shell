#include "terminal.h"

#include <ncurses.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

/* Handle non-interactive mode commands */
static void handle_non_interactive_mode(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    /* Handle command line arguments for non-interactive mode */
    if (argc > 1) {
        handle_non_interactive_mode(argc, argv);
        return 0;
    }

    /* Initialize ncurses for interactive mode */
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);
    timeout(100);

    /* Initialize terminal system */
    init_terminal_manager();
    init_colors();
    
    show_welcome_message(&get_active_terminal()->history);

    /* Main event loop */
    while (1) {
        Terminal* active = get_active_terminal();
        
        update_real_time_display();
        
        if (handle_input(&active->input, &active->history)) break;
        
        process_command_queue();
    }

    /* Cleanup resources */
    for (int i = 0; i < terminal_manager.terminal_count; i++) {
        free_history_buffer(&terminal_manager.terminals[i].history);
        free_input_state(&terminal_manager.terminals[i].input);
    }
    free(terminal_manager.terminals);
    
    endwin();
    return 0;
}

/*
 * Handle non-interactive mode commands
 * @param argc: Argument count
 * @param argv: Argument vector
 */
static void handle_non_interactive_mode(int argc, char *argv[]) {
    if (argc == 1 || strcmp(argv[1], "manual") == 0) {
        printf("Parrot Terminal %s\n", PARROT_VERSION);
        printf("==========================================\n");
        printf("Interactive mode keyboard shortcuts:\n");
        printf("  Shift+T: New terminal\n");
        printf("  Shift+W: Close terminal\n");
        printf("  Alt+1-9: Switch terminals\n");
        printf("  Alt+/-: Next/Prev terminal\n");
        printf("  Alt+Arrows: Switch between split panes\n");
        printf("  Arrow Keys: Scroll terminal history\n");
        printf("  Shift+Up/Down: Command history\n");
        printf("\nCommand Queue Features:\n");
        printf("  - Commands auto-queue when another is running\n");
        printf("  - Queue size: 10 commands maximum\n");
        printf("  - Terminal locks when queue is full (red clock)\n");
        printf("  - Input shows #### when locked\n");
        printf("Type 'parrot' to start interactive mode\n");
    } else printf("Unknown command: %s\n", argv[1]);
}
