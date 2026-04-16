#include "game.h"

#include <stdio.h>    
#include <string.h>   
#include <unistd.h>   
#include <termios.h>  
#include <pthread.h>  

//transfer completed
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); 
    raw.c_cc[VMIN] = 0;              
    raw.c_cc[VTIME] = 5;             
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void printBoard(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int opp_puzzle[MAX][MAX], int hints_left, int mistakes_found, int cursor_r, int cursor_c, int opp_r, int opp_c, int game_mode, const char* opp_name, const char* status_msg) {
    pthread_mutex_lock(&timer_mutex);
    int current_time = elapsed_seconds;
    pthread_mutex_unlock(&timer_mutex);
    
    printf("\033[2J\033[H"); 
    printf(CYAN BOLD "\n[ RIPPLE EFFECT | TIME: %02d:%02d | HINTS: ", current_time / 60, current_time % 60);
    
    if (hints_left == -1) printf("INF ");
    else printf("%d ", hints_left);

    printf("| MISTAKES: %d ]\n\n" RESET, mistakes_found);

    if (game_mode == 2) {
        printf(YELLOW "      --- YOUR BOARD ---          --- %s'S BOARD ---\n" RESET, opp_name);
    }

    printf("    ");
    for(int j = 0; j < size; j++) printf(BOLD "%d   " RESET, j + 1);
    if (game_mode == 2) {
        printf("      ");
        for(int j = 0; j < size; j++) printf(BOLD "%d   " RESET, j + 1);
    }
    printf("\n");

    for (int i = 0; i < size; i++) {
        printf("   "); 
        for (int j = 0; j < size; j++) {
            printf("+"); 
            if (i == 0 || regions[i][j] != regions[i-1][j]) printf(BLUE "---" RESET); 
            else printf("   "); 
        }
        printf("+");
        if (game_mode == 2) {
            printf("     ");
            for (int j = 0; j < size; j++) {
                printf("+"); 
                if (i == 0 || regions[i][j] != regions[i-1][j]) printf(BLUE "---" RESET); 
                else printf("   "); 
            }
            printf("+");
        }
        printf("\n");

        printf(" " BOLD "%d " RESET, i + 1); 
        for (int j = 0; j < size; j++) {
            if (j == 0 || regions[i][j] != regions[i][j-1]) printf(BLUE "|" RESET); 
            else printf(" "); 
            
            int is_cursor = (i == cursor_r && j == cursor_c);
            int is_opp_cursor = (game_mode == 1 && i == opp_r && j == opp_c); 
            if (is_cursor && is_opp_cursor) printf(BOTH_CURSOR);
            else if (is_cursor) printf(INV_CURSOR);
            else if (is_opp_cursor) printf(OPP_CURSOR);
            
            if (puzzle[i][j] == 0) {
                printf(" . ");
            } else if (initial_puzzle[i][j] != 0) {
                if (!is_cursor && !is_opp_cursor) printf(YELLOW BOLD);
                printf(" %d ", puzzle[i][j]); 
            } else {
                if (!is_cursor && !is_opp_cursor) printf(GREEN BOLD);
                printf(" %d ", puzzle[i][j]);       
            }
            if (is_cursor || is_opp_cursor || initial_puzzle[i][j] != 0 || puzzle[i][j] != 0) printf(RESET); 
        }
        printf(BLUE "|" RESET); 
        
        // Opponent Board rendering for Versus mode
        if (game_mode == 2) {
            printf("   " BOLD "%d " RESET, i + 1); 
            for (int j = 0; j < size; j++) {
                if (j == 0 || regions[i][j] != regions[i][j-1]) printf(BLUE "|" RESET); 
                else printf(" "); 
                
                int is_opp_vs_cursor = (i == opp_r && j == opp_c);
                if (is_opp_vs_cursor) printf(OPP_CURSOR);
                
                if (opp_puzzle[i][j] == 0) printf(" . ");
                else if (initial_puzzle[i][j] != 0) {
                    if (!is_opp_vs_cursor) printf(YELLOW BOLD);
                    printf(" %d ", opp_puzzle[i][j]); 
                } else {
                    if (!is_opp_vs_cursor) printf(RED BOLD); 
                    printf(" %d ", opp_puzzle[i][j]);       
                }
                if (is_opp_vs_cursor || initial_puzzle[i][j] != 0 || opp_puzzle[i][j] != 0) printf(RESET); 
            }
            printf(BLUE "|" RESET); 
        }
        printf("\n");
    }

    printf("   ");
    for (int j = 0; j < size; j++) printf(BLUE "+---" RESET);
    printf(BLUE "+" RESET);
    if (game_mode == 2) {
        printf("     ");
        for (int j = 0; j < size; j++) printf(BLUE "+---" RESET);
        printf(BLUE "+" RESET);
    }
    printf("\n\n");

    printf(BOLD "Controls: " RESET "[WASD] Move |[1-9] Place | [0] Clear | [H] Valid Options\n");
    printf(BOLD "          " RESET "[?] Auto-Solve | [C] Check Board | [Q] Save/Quit\n");
    printf(YELLOW "%s\n" RESET, status_msg);
    fflush(stdout); 
}

int getMapChoice(int size) {
    char input[256];
    int choice;
    while(1) {
        printf(CYAN "\n=====================================\n");
        printf("         SELECT %dx%d MAP           \n", size, size);
        printf("=====================================\n" RESET);
        printf("1. Map 1\n");
        printf("2. Map 2\n");
        printf("-------------------------------------\n");
        printf("Choice: ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) return -1;
        if (sscanf(input, "%d", &choice) == 1) {
            if (choice == 1) return 1;
            if (choice == 2) return 2;
        }
        printf(RED "[!] Invalid choice.\n" RESET);
    }
}

int getHintChoice() {
    char input[256];
    int choice;
    while(1) {
        printf(CYAN "\n=====================================\n");
        printf("         SELECT DIFFICULTY           \n");
        printf("=====================================\n" RESET);
        printf("1. Easy (Infinite Hints)\n");
        printf("2. Normal (10 Hints)\n");
        printf("3. Hard (5 Hints)\n");
        printf("4. Extreme (0 Hints)\n");
        printf("-------------------------------------\n");
        printf("Choice: ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) return -1;
        if (sscanf(input, "%d", &choice) == 1) {
            if (choice == 1) return -1;
            if (choice == 2) return 10;
            if (choice == 3) return 5;
            if (choice == 4) return 0; 
        }
        printf(RED "[!] Invalid choice.\n" RESET);
    }
}

const char* getDifficultyString(int initial_hints) {
    if (initial_hints == -1) return "Easy";
    if (initial_hints == 10) return "Normal";
    if (initial_hints == 5) return "Hard";
    if (initial_hints == 0) return "Extreme";
    return "Custom";
}
