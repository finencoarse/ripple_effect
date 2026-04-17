#include "game.h"

void createDefaultFiles() {
    FILE *f;
    if ((f = fopen("data/puzzle6_1.txt", "r"))) fclose(f);
    else if ((f = fopen("data/puzzle6_1.txt", "w"))) {
        fprintf(f, "6\n0 7 7 8 8 9\n0 6 7 8 9 9\n2 2 2 8 9 9\n3 5 5 10 12 12\n3 4 5 10 10 12\n3 3 5 11 10 10\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n");
        fclose(f);
    }
    if ((f = fopen("data/puzzle6_2.txt", "r"))) fclose(f);
    else if ((f = fopen("data/puzzle6_2.txt", "w"))) {
        fprintf(f, "6\n0 0 0 1 1 1\n0 0 0 1 1 1\n2 2 2 3 3 3\n2 2 2 3 3 3\n4 4 4 5 5 5\n4 4 4 5 5 5\n1 0 0 0 5 0\n0 0 6 0 0 0\n0 3 0 5 0 0\n0 0 4 0 3 0\n0 0 0 6 0 0\n0 4 0 0 0 2\n");
        fclose(f);
    }
    if ((f = fopen("data/puzzle8_1.txt", "r"))) fclose(f);
    else if ((f = fopen("data/puzzle8_1.txt", "w"))) {
        fprintf(f, "8\n0 1 1 1 15 14 14 13\n0 0 2 15 15 15 14 13\n0 2 2 2 15 11 11 13\n0 3 2 4 11 11 12 13\n5 5 4 4 4 11 10 13\n5 6 7 4 8 10 10 10\n5 6 7 8 8 8 10 9\n5 6 6 6 8 9 9 9\n4 0 0 2 0 0 0 0\n0 0 0 0 0 4 0 0\n0 0 0 0 0 0 8 0\n0 0 0 0 0 0 0 0\n0 0 0 0 4 2 0 0\n0 0 0 0 0 0 1 0\n0 0 0 1 0 0 0 0\n0 0 0 0 0 0 0 0\n");
        fclose(f);
    }
if ((f = fopen("data/puzzle8_2.txt", "r"))) fclose(f);
    else if ((f = fopen("data/puzzle8_2.txt", "w"))) {
        fprintf(f, "8\n0 0 1 1 3 3 5 5\n0 2 1 4 3 6 5 7\n8 8 10 10 12 12 14 14\n8 9 10 11 12 13 14 15\n16 16 18 18 20 20 22 22\n16 17 18 19 20 21 22 23\n24 24 26 26 28 28 30 30\n24 25 26 27 28 29 30 31\n1 0 0 3 0 0 0 3\n0 1 0 0 3 0 2 0\n0 0 1 0 0 3 0 0\n2 0 0 1 0 0 3 0\n0 2 0 0 1 0 0 3\n3 0 2 0 0 1 0 0\n0 3 0 2 0 0 1 0\n0 0 3 0 2 0 0 1\n");
        fclose(f);
    }
}

int loadPuzzleFromFile(const char *filename, int *size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int *saved_time, int *hints_left, int *initial_hint_quota, int *hints_used, int *mistakes_found) {
    char full_path[256]; 
    snprintf(full_path, sizeof(full_path), "data/%s", filename);
    FILE *f = fopen(full_path, "r");
    if (!f) return 0;

    if (fscanf(f, "%d", size) != 1 || *size <= 0 || *size > MAX) { fclose(f); return 0; }
    for (int i = 0; i < *size; i++)
        for (int j = 0; j < *size; j++)
            if (fscanf(f, "%d", &regions[i][j]) != 1) { fclose(f); return 0; }

    for (int i = 0; i < *size; i++)
        for (int j = 0; j < *size; j++) {
            if (fscanf(f, "%d", &initial_puzzle[i][j]) != 1) { fclose(f); return 0; }
            puzzle[i][j] = initial_puzzle[i][j]; 
        }
    
    if (fscanf(f, "%d", saved_time) == 1) {
        for (int i = 0; i < *size; i++)
            for (int j = 0; j < *size; j++)
                if (fscanf(f, "%d", &puzzle[i][j]) != 1) { fclose(f); return 0; }
                
        if (fscanf(f, "%d", hints_left) != 1) *hints_left = 0; 
        if (fscanf(f, "%d", initial_hint_quota) != 1) *initial_hint_quota = *hints_left;
        if (fscanf(f, "%d", hints_used) != 1) *hints_used = 0;
        if (fscanf(f, "%d", mistakes_found) != 1) *mistakes_found = 0;
    } else {
        *saved_time = 0;
    }
    fclose(f);
    return 1;
}

void saveGameProcess(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int current_time, int hints_left, int initial_hint_quota, int hints_used, int mistakes_found) {
    pid_t pid = fork();
    if (pid < 0) perror("Fork failed");
    else if (pid == 0) {
        char save_path[256];
        snprintf(save_path, sizeof(save_path), "data/savegame_%d.txt", size);
        int fd = open(save_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd != -1) {
            flock(fd, LOCK_EX); 
            FILE *f = fdopen(fd, "w"); 
            if (f) {
                fprintf(f, "%d\n", size);
                for (int i = 0; i < size; i++) {
                    for (int j = 0; j < size; j++) fprintf(f, "%d ", regions[i][j]);
                    fprintf(f, "\n");
                }
                for (int i = 0; i < size; i++) {
                    for (int j = 0; j < size; j++) fprintf(f, "%d ", initial_puzzle[i][j]);
                    fprintf(f, "\n");
                }
                fprintf(f, "%d\n", current_time); 
                for (int i = 0; i < size; i++) {
                    for (int j = 0; j < size; j++) fprintf(f, "%d ", puzzle[i][j]);
                    fprintf(f, "\n");
                }
                fprintf(f, "%d %d %d %d\n", hints_left, initial_hint_quota, hints_used, mistakes_found); 
                fflush(f);
            }
            flock(fd, LOCK_UN); 
            fclose(f);
        }
        exit(0); 
    } else {
        printf(GREEN "\n[System] Save initiated in background (PID: %d). Returning to menu..." RESET "\n", pid);
        sleep(1);
    }
}

void saveScore(int size, int final_time, const char* difficulty, int hints_used, int mistakes_found, int game_mode, int is_win, const char* opp_name) {
    FILE *f = fopen("data/leaderboard.txt", "a");
    if (f) {
        fprintf(f, "[%s] ", global_username);
        if (game_mode == 0) fprintf(f, "Mode: Singleplayer | ");
        else if (game_mode == 1) fprintf(f, "Mode: Co-op (with %s) | ", opp_name);
        else if (game_mode == 2) fprintf(f, "Mode: Versus (vs %s) - %s | ", opp_name, is_win ? "WIN" : "LOSS");
        
        fprintf(f, "Puzzle: %dx%d | Diff: %-7s | Time: %02d:%02d | Hints: %d | Mistakes: %d\n", 
                size, size, difficulty, final_time / 60, final_time % 60, hints_used, mistakes_found);
        fclose(f);
    }
}

void viewLeaderboard() {
    printf("\033[2J\033[H"); 
    printf(CYAN BOLD "===============================================================================\n");
    printf("                                LEADERBOARD                                    \n");
    printf("===============================================================================\n" RESET);
    FILE *f = fopen("data/leaderboard.txt", "r");
    if (!f) {
        printf(YELLOW "No scores yet. Complete a game to see your stats here!\n" RESET);
    } else {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            printf("%s", line);
        }
        fclose(f);
    }
    printf(CYAN BOLD "===============================================================================\n" RESET);
    printf("Press Enter to return to main menu...");
    char buf[256];
    fgets(buf, sizeof(buf), stdin);
}