#include "game.h"

volatile sig_atomic_t sigint_received = 0;
volatile int keep_timer_running = 1;
int elapsed_seconds = 0;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
struct termios orig_termios; 
char global_username[32] = "Player";

// Signal Handlers
void handle_sigint(int sig) { sigint_received = 1; }
void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
    errno = saved_errno;
}

// Timer Thread
void* timer_thread_func(void* arg) {
    while(keep_timer_running) {
        sleep(1);
        pthread_mutex_lock(&timer_mutex);
        elapsed_seconds++;
        pthread_mutex_unlock(&timer_mutex);
    }
    return NULL;
}

// MAIN GAME LOOP
void playGame(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int solution[MAX][MAX], int hints_left, int initial_hint_quota, int hints_used, int mistakes_found, int net_sock, int game_mode, const char* opp_name) {
    int region_sizes[MAX_REGIONS]; 
    getRegionSizes(size, regions, region_sizes);
    
    int opp_puzzle[MAX][MAX];
    for(int i=0; i<size; i++) 
        for(int j=0; j<size; j++) 
            opp_puzzle[i][j] = initial_puzzle[i][j];

    pthread_t timer_thread;
    keep_timer_running = 1;
    pthread_create(&timer_thread, NULL, timer_thread_func, NULL);

    int cursor_r = 0, cursor_c = 0, opp_r = -1, opp_c = -1;
    int auto_solve_unlocked = 0, is_winner = 0, quit_requested = 0;
    
    char status_msg[256];
    if (net_sock != -1) snprintf(status_msg, 256, "[+] Connected to %.31s (%s Mode)", opp_name, game_mode == 1 ? "Co-op" : "Versus");
    else strncpy(status_msg, "WASD to move | 1-9 to place | Q to Save/Quit", 256);
    
    char c;
    enableRawMode(); 

    while(1) {
        // 1. Handle SIGINT (Ctrl+C)
        if (sigint_received) {
            snprintf(status_msg, 256, "[!] Ctrl+C detected. Save progress? (y/n): ");
            printBoard(size, regions, initial_puzzle, puzzle, opp_puzzle, hints_left, mistakes_found, cursor_r, cursor_c, opp_r, opp_c, game_mode, opp_name, status_msg);
            
            char ans;
            while(read(STDIN_FILENO, &ans, 1) != 1); 
            
            if (ans == 'y' || ans == 'Y') {
                pthread_mutex_lock(&timer_mutex); int snap = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);
                disableRawMode();
                if (net_sock != -1) { NetPacket p = {'Q', 0, 0, 0, 0, 0}; send(net_sock, &p, sizeof(p), 0); }
                saveGameProcess(size, regions, initial_puzzle, puzzle, snap, hints_left, initial_hint_quota, hints_used, mistakes_found);
                quit_requested = 1; break;
            }
            sigint_received = 0; status_msg[0] = '\0';
        }

        printBoard(size, regions, initial_puzzle, puzzle, opp_puzzle, hints_left, mistakes_found, cursor_r, cursor_c, opp_r, opp_c, game_mode, opp_name, status_msg);

        // 2. Check Win Condition
        if (isWin(size, puzzle)) {
            is_winner = 1;
            if (game_mode == 2) { 
                NetPacket pkt = {'F', 0, 0, 0, mistakes_found, hints_used};
                send(net_sock, &pkt, sizeof(NetPacket), 0);
            }
            break; 
        }

        fd_set readfds; FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;
        if (net_sock != -1) { FD_SET(net_sock, &readfds); if (net_sock > max_fd) max_fd = net_sock; }

        struct timeval tv = {0, 50000}; 
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) continue;

        // 3. Handle Network Packets
        if (net_sock != -1 && FD_ISSET(net_sock, &readfds)) {
            NetPacket pkt;
            if (recv(net_sock, &pkt, sizeof(pkt), 0) <= 0) {
                snprintf(status_msg, 256, "[!] Connection lost. Reverting to Singleplayer.");
                net_sock = -1; game_mode = 0; opp_r = -1; opp_c = -1;
            } else {
                if (game_mode == 1 && pkt.hints < hints_left && pkt.hints >= 0) hints_left = pkt.hints;
                if (pkt.type == 'C') { opp_r = pkt.r; opp_c = pkt.c; }
                else if (pkt.type == 'M') {
                    if (game_mode == 1) { 
                        puzzle[pkt.r][pkt.c] = pkt.val; 
                        if(pkt.val == solution[pkt.r][pkt.c]) initial_puzzle[pkt.r][pkt.c] = pkt.val; 
                    }
                    else opp_puzzle[pkt.r][pkt.c] = pkt.val;
                }
                else if (pkt.type == 'X') { if (game_mode == 1) puzzle[pkt.r][pkt.c] = 0; else opp_puzzle[pkt.r][pkt.c] = 0; }
                else if (pkt.type == 'F') { is_winner = 0; break; } 
                else if (pkt.type == 'Q') { net_sock = -1; game_mode = 0; opp_r = -1; opp_c = -1;}
            }
        }

        // 4. Handle Keyboard Input
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (read(STDIN_FILENO, &c, 1) == 0) continue; 
            status_msg[0] = '\0';
            int moved = 0;
            if (c == 'w' || c == 'W') { if (cursor_r > 0) { cursor_r--; moved = 1; } }
            else if (c == 's' || c == 'S') { if (cursor_r < size - 1) { cursor_r++; moved = 1; } }
            else if (c == 'a' || c == 'A') { if (cursor_c > 0) { cursor_c--; moved = 1; } }
            else if (c == 'd' || c == 'D') { if (cursor_c < size - 1) { cursor_c++; moved = 1; } }
            
            if (moved && net_sock != -1) {
                NetPacket p = {'C', cursor_r, cursor_c, 0, mistakes_found, hints_left};
                send(net_sock, &p, sizeof(p), 0);
            }
            else if (c == 'q' || c == 'Q') {
                pthread_mutex_lock(&timer_mutex); int snap = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);
                disableRawMode();
                if (net_sock != -1) { NetPacket p = {'Q', 0, 0, 0, 0, 0}; send(net_sock, &p, sizeof(p), 0); close(net_sock); }
                saveGameProcess(size, regions, initial_puzzle, puzzle, snap, hints_left, initial_hint_quota, hints_used, mistakes_found);
                quit_requested = 1; break;
            }
            else if (c >= '1' && c <= '9') {
                int num = c - '0';
                if (initial_puzzle[cursor_r][cursor_c] != 0) {
                    snprintf(status_msg, 256, "[!] Cannot overwrite locked cells.");
                } else if (isValidMove(cursor_r, cursor_c, num, size, puzzle, regions, region_sizes, status_msg)) {
                    puzzle[cursor_r][cursor_c] = num;
                    if (game_mode == 1 && num == solution[cursor_r][cursor_c]) initial_puzzle[cursor_r][cursor_c] = num;
                    if (net_sock != -1) { NetPacket p = {'M', cursor_r, cursor_c, num, mistakes_found, hints_left}; send(net_sock, &p, sizeof(p), 0); }
                } else mistakes_found++;
            }
            else if (c == '0' || c == ' ' || c == 127 || c == 8) { 
                if (initial_puzzle[cursor_r][cursor_c] == 0) {
                    puzzle[cursor_r][cursor_c] = 0;
                    if (net_sock != -1) { NetPacket p = {'X', cursor_r, cursor_c, 0, mistakes_found, hints_left}; send(net_sock, &p, sizeof(p), 0); }
                }
            }
            else if (c == 'h' || c == 'H') {
                if (initial_puzzle[cursor_r][cursor_c] != 0) {
                    snprintf(status_msg, 256, "[!] Cannot provide options for a locked clue.");
                } else {
                    int max_val = region_sizes[regions[cursor_r][cursor_c]];
                    char opts[64] = ""; int count = 0;
                    for (int n = 1; n <= max_val; n++) {
                        if (isValidMove(cursor_r, cursor_c, n, size, puzzle, regions, region_sizes, NULL)) {
                            char temp[16]; snprintf(temp, 16, "%d ", n); strcat(opts, temp); count++;
                        }
                    }
                    if (count > 0) snprintf(status_msg, 256, "[Hint] Valid options here: %s", opts);
                    else snprintf(status_msg, 256, "[Hint] No valid options! Mistake exists.");
                }
            }
            else if (c == 'c' || c == 'C') {
                int temp_puzzle[MAX][MAX];
                for(int i=0; i<size; i++) for(int j=0; j<size; j++) temp_puzzle[i][j] = puzzle[i][j];
                if (solveBoard(size, temp_puzzle, regions, region_sizes)) snprintf(status_msg, 256, "[+] Check passed! Board is solvable.");
                else { mistakes_found++; snprintf(status_msg, 256, "[!] Mistakes found! Cannot be solved."); }
            }
            else if (c == '?') {
                if (!auto_solve_unlocked) {
                    disableRawMode();
                    printf(YELLOW "\n[Security] Enter password: " RESET);
                    char pwd[32];
                    if (fgets(pwd, 32, stdin)) {
                        pwd[strcspn(pwd, "\n")] = 0; 
                        if (strcmp(pwd, "admin") == 0) { auto_solve_unlocked = 1; snprintf(status_msg, 256, "[+] Auto-Solve unlocked!"); } 
                        else snprintf(status_msg, 256, "[!] Incorrect password!");
                    }
                    enableRawMode(); continue; 
                }
                if (hints_left <= 0 && hints_left != -1) snprintf(status_msg, 256, "[!] Out of hints!");
                else {
                    int placed = 0;
                    for (int i = 0; i < size && !placed; i++) {
                        for (int j = 0; j < size; j++) {
                            if (puzzle[i][j] == 0) {
                                puzzle[i][j] = solution[i][j]; cursor_r = i; cursor_c = j; placed = 1;
                                if (game_mode == 1) initial_puzzle[i][j] = solution[i][j];
                                snprintf(status_msg, 256, "[Auto-Solve] Placed correct %d here!", puzzle[i][j]);
                                if (hints_left > 0) hints_left--; hints_used++;
                                if (net_sock != -1) { NetPacket pkt = {'M', cursor_r, cursor_c, puzzle[cursor_r][cursor_c], mistakes_found, hints_left}; send(net_sock, &pkt, sizeof(NetPacket), 0); }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    keep_timer_running = 0;
    pthread_join(timer_thread, NULL);
    disableRawMode(); 
    pthread_mutex_lock(&timer_mutex); int final_t = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);

    if (!quit_requested) {
        if (is_winner) printf(GREEN BOLD "\n*** CONGRATULATIONS! YOU WON IN %02d:%02d! ***\n" RESET, final_t/60, final_t%60);
        else printf(RED BOLD "\n*** GAME OVER! %.31s WON THE MATCH. ***\n" RESET, opp_name);
        saveScore(size, final_t, getDifficultyString(initial_hint_quota), hints_used, mistakes_found, game_mode, is_winner, opp_name);
    } else {
        printf(CYAN "\n[System] Game progress saved successfully.\n" RESET);
    }
    if (net_sock != -1) close(net_sock);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);   
    signal(SIGCHLD, handle_sigchld); 
    createDefaultFiles(); 

    printf("\033[2J\033[H" CYAN BOLD "=====================================\n     WELCOME TO RIPPLE EFFECT        \n=====================================\n" RESET);
    printf("Enter Username: ");
    if (fgets(global_username, 32, stdin)) sanitize_username(global_username, 32);
    if (strlen(global_username) == 0) strcpy(global_username, "Player");

    int size, saved_t, hints, quota, used, mistakes;
    int regs[MAX][MAX], init[MAX][MAX], puz[MAX][MAX];
    char input[256]; int choice;

    if (argc == 2) {
        printf(YELLOW "\nAttempting to load file: %s\n" RESET, argv[1]);
        if(loadPuzzleFromFile(argv[1], &size, regs, init, puz, &saved_t, &hints, &quota, &used, &mistakes)) {
            int sol[MAX][MAX]; memcpy(sol, puz, sizeof(sol));
            int ts[MAX_REGIONS]; getRegionSizes(size, regs, ts); solveBoard(size, sol, regs, ts);
            pthread_mutex_lock(&timer_mutex); elapsed_seconds = saved_t; pthread_mutex_unlock(&timer_mutex);
            playGame(size, regs, init, puz, sol, hints, quota, used, mistakes, -1, 0, "");
        } else {
            printf(RED "[!] Could not load %s.\n" RESET, argv[1]);
        }
    }

    while(1) {
        printf(CYAN BOLD "\n=====================================\n");
        printf("         RIPPLE EFFECT PUZZLE        \n");
        printf("=====================================\n" RESET);
        printf("1. Play 6x6 (New Game)\n");
        printf("2. Play 8x8 (New Game)\n");
        printf("3. Load Saved 6x6 Game\n");
        printf("4. Load Saved 8x8 Game\n");
        printf("5. View Leaderboard\n");
        printf("6. Host Multiplayer Game\n");
        printf("7. Join Multiplayer Game\n");
        printf("0. Exit Application\n");
        printf("-------------------------------------\n");
        printf("Select an option: ");
        
        if (!fgets(input, 256, stdin) || sscanf(input, "%d", &choice) != 1) continue;
        if (choice == 0) break;

        if (choice == 1 || choice == 2) {
            int s = (choice == 1) ? 6 : 8;
            int step = 1, m_id = 0;
            
            // State Machine for Singleplayer Menu
            while(step > 0 && step <= 2) {
                if (step == 1) {
                    m_id = getMapChoice(s);
                    if (m_id == 0) step = 0; else step = 2;
                } else if (step == 2) {
                    quota = getHintChoice();
                    if (quota == -2) step = 1; else break;
                }
            }
            if (step == 0) continue; // User pressed 0 to exit back to Main Menu
            
            hints = quota; used = 0; mistakes = 0;
            char fn[32]; snprintf(fn, 32, "puzzle%d_%d.txt", s, m_id);
            if(loadPuzzleFromFile(fn, &size, regs, init, puz, &saved_t, &hints, &quota, &used, &mistakes)) {
                int sol[MAX][MAX]; memcpy(sol, puz, sizeof(sol));
                int ts[MAX_REGIONS]; getRegionSizes(size, regs, ts); solveBoard(size, sol, regs, ts);
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                playGame(size, regs, init, puz, sol, hints, quota, used, mistakes, -1, 0, "");
            }
            
        } else if (choice == 3 || choice == 4) {
            char fn[32]; snprintf(fn, 32, "savegame_%d.txt", (choice == 3 ? 6 : 8));
            if(loadPuzzleFromFile(fn, &size, regs, init, puz, &saved_t, &hints, &quota, &used, &mistakes)) {
                int sol[MAX][MAX]; memcpy(sol, puz, sizeof(sol));
                int ts[MAX_REGIONS]; getRegionSizes(size, regs, ts); solveBoard(size, sol, regs, ts);
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = saved_t; pthread_mutex_unlock(&timer_mutex);
                playGame(size, regs, init, puz, sol, hints, quota, used, mistakes, -1, 0, "");
            } else printf(RED "\n[!] Save not found.\n" RESET);
            
        } else if (choice == 5) {
            viewLeaderboard();
            
        } else if (choice == 6) { 
            int step = 1, b_size = 0, map_id = 0, g_mode = 0;
            
            // State Machine for Multiplayer Host Menu
            while (step > 0 && step <= 4) {
                if (step == 1) {
                    printf(CYAN "Select Board Size (6 or 8) [0 to Go Back]: " RESET);
                    if (!fgets(input, 256, stdin) || sscanf(input, "%d", &b_size) != 1) continue;
                    if (b_size == 0) step = 0;
                    else if (b_size == 6 || b_size == 8) step = 2;
                    else printf(RED "[!] Invalid size.\n" RESET);
                }
                else if (step == 2) {
                    map_id = getMapChoice(b_size);
                    if (map_id == 0) step = 1; else step = 3;
                }
                else if (step == 3) {
                    quota = getHintChoice();
                    if (quota == -2) step = 2; else step = 4;
                }
                else if (step == 4) {
                    printf(CYAN "Select Mode (1 = Co-op, 2 = Versus) [0 to Go Back]: " RESET);
                    if (!fgets(input, 256, stdin) || sscanf(input, "%d", &g_mode) != 1) continue;
                    if (g_mode == 0) step = 3;
                    else if (g_mode == 1 || g_mode == 2) break; // Finished config
                    else printf(RED "[!] Invalid mode.\n" RESET);
                }
            }
            if (step == 0) continue; // User pressed 0 to exit back to Main Menu
            
            hints = quota; used = 0; mistakes = 0;
            char fn[32]; snprintf(fn, 32, "puzzle%d_%d.txt", b_size, map_id);
            if(loadPuzzleFromFile(fn, &size, regs, init, puz, &saved_t, &hints, &quota, &used, &mistakes)) {
                int net_sock = startServer();
                if (net_sock != -1) {
                    SyncPacket sp; sp.size = size; sp.hints_left = hints; sp.initial_hint_quota = quota;
                    sp.game_mode = g_mode; strcpy(sp.host_username, global_username);
                    memcpy(sp.regions, regs, sizeof(sp.regions)); memcpy(sp.initial_puzzle, init, sizeof(sp.initial_puzzle)); memcpy(sp.puzzle, puz, sizeof(sp.puzzle));
                    send(net_sock, &sp, sizeof(SyncPacket), 0);
                    
                    InitPacket ip_pkt; recv(net_sock, &ip_pkt, sizeof(InitPacket), MSG_WAITALL);
                    sanitize_username(ip_pkt.username, 32);
                    
                    int sol[MAX][MAX]; memcpy(sol, puz, sizeof(sol));
                    int ts[MAX_REGIONS]; getRegionSizes(size, regs, ts); solveBoard(size, sol, regs, ts);
                    pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                    playGame(size, regs, init, puz, sol, hints, quota, used, mistakes, net_sock, g_mode, ip_pkt.username);
                }
            }
            
        } else if (choice == 7) { 
            char ip[64];
            while(1) {
                printf(CYAN "Enter Host IP [0 to Go Back]: " RESET);
                if (fgets(ip, 64, stdin)) {
                    ip[strcspn(ip, "\n")] = 0; 
                    if (strcmp(ip, "0") == 0) break; // Exit loop, return to Main Menu
                    
                    int net_sock = connectToServer(ip);
                    if (net_sock != -1) {
                        SyncPacket sp;
                        if (recv(net_sock, &sp, sizeof(SyncPacket), MSG_WAITALL) > 0) {
                            sanitize_username(sp.host_username, 32);
                            InitPacket ip_pkt; strcpy(ip_pkt.username, global_username);
                            send(net_sock, &ip_pkt, sizeof(InitPacket), 0);
                            
                            int sol[MAX][MAX]; memcpy(sol, sp.puzzle, sizeof(sol));
                            int ts[MAX_REGIONS]; getRegionSizes(sp.size, sp.regions, ts); solveBoard(sp.size, sol, sp.regions, ts);
                            pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                            playGame(sp.size, sp.regions, sp.initial_puzzle, sp.puzzle, sol, sp.hints_left, sp.initial_hint_quota, 0, 0, net_sock, sp.game_mode, sp.host_username);
                        }
                        break; // End joining session logic after game finishes
                    }
                }
            }
        }
    }
    return 0;
}