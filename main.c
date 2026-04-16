#include "game.h"

#include <stdio.h>       
#include <stdlib.h>    
#include <string.h>     
#include <unistd.h>    
#include <signal.h>     
#include <pthread.h>    
#include <sys/wait.h>    
#include <errno.h>    
#include <sys/select.h> 
#include <sys/socket.h>

volatile sig_atomic_t sigint_received = 0;
volatile int keep_timer_running = 1;
int elapsed_seconds = 0;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
struct termios orig_termios; 
char global_username[32] = "Player";

//transfer completed
void playGame(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int solution[MAX][MAX], int hints_left, int initial_hint_quota, int hints_used, int mistakes_found, int net_sock, int game_mode, const char* opp_name) {
    int region_sizes[MAX_REGIONS]; 
    getRegionSizes(size, regions, region_sizes);
    
    int opp_puzzle[MAX][MAX];
    for(int i=0; i<size; i++) for(int j=0; j<size; j++) opp_puzzle[i][j] = initial_puzzle[i][j];

    pthread_t timer_thread;
    keep_timer_running = 1;
    pthread_create(&timer_thread, NULL, timer_thread_func, NULL);

    int cursor_r = 0, cursor_c = 0;
    int opp_r = -1, opp_c = -1;
    int auto_solve_unlocked = 0; 
    int is_winner = 0; 
    
    char status_msg[256];
    if (net_sock != -1) sprintf(status_msg, "[+] Connected! %s Match Started against %s.", game_mode == 1 ? "Co-op" : "Versus", opp_name);
    else strcpy(status_msg, "Game Started! Use WASD to move. Press H for Options or ? for Auto-Solve.");
    
    char c;
    enableRawMode(); 

    while(1) {
        if (sigint_received) {
            sprintf(status_msg, "[!] Ctrl+C Detected. Save game? (y/n): ");
            printBoard(size, regions, initial_puzzle, puzzle, opp_puzzle, hints_left, mistakes_found, cursor_r, cursor_c, opp_r, opp_c, game_mode, opp_name, status_msg);
            
            char ans;
            while(read(STDIN_FILENO, &ans, 1) != 1); 
            
            if (ans == 'y' || ans == 'Y') {
                pthread_mutex_lock(&timer_mutex); int snap_time = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);
                disableRawMode();
                if (net_sock != -1) {
                    NetPacket pkt = {'Q', 0, 0, 0, 0, 0};
                    send(net_sock, &pkt, sizeof(NetPacket), 0);
                }
                saveGameProcess(size, regions, initial_puzzle, puzzle, snap_time, hints_left, initial_hint_quota, hints_used, mistakes_found);
                break;
            }
            sigint_received = 0;
            status_msg[0] = '\0';
        }

        printBoard(size, regions, initial_puzzle, puzzle, opp_puzzle, hints_left, mistakes_found, cursor_r, cursor_c, opp_r, opp_c, game_mode, opp_name, status_msg);

        if (isWin(size, puzzle)) {
            if (game_mode != 2) { 
                is_winner = 1; 
                break; 
            } else { 
                NetPacket pkt = {'F', 0, 0, 0, mistakes_found, hints_used};
                send(net_sock, &pkt, sizeof(NetPacket), 0);
                is_winner = 1; 
                break;
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;
        
        if (net_sock != -1) {
            FD_SET(net_sock, &readfds);
            if (net_sock > max_fd) max_fd = net_sock;
        }

        struct timeval tv = {0, 50000}; 
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) continue;

        // --- HANDLE NETWORK INPUT ---
        if (net_sock != -1 && FD_ISSET(net_sock, &readfds)) {
            NetPacket pkt;
            int bytes = recv(net_sock, &pkt, sizeof(NetPacket), 0);
            if (bytes <= 0) {
                sprintf(status_msg, "[!] %s disconnected. Transitioned to Singleplayer.", opp_name);
                close(net_sock); net_sock = -1; game_mode = 0; opp_r = -1; opp_c = -1;
            } else {
                status_msg[0] = '\0';
                
                if (game_mode == 1 && pkt.hints < hints_left && pkt.hints >= 0) hints_left = pkt.hints;

                if (pkt.type == 'C') { opp_r = pkt.r; opp_c = pkt.c; }
                else if (pkt.type == 'M') { 
                    if (game_mode == 1) { 
                        puzzle[pkt.r][pkt.c] = pkt.val;
                        if (pkt.val == solution[pkt.r][pkt.c]) {
                            initial_puzzle[pkt.r][pkt.c] = pkt.val;
                            sprintf(status_msg, "[Net] %s placed correct %d (Locked)", opp_name, pkt.val);
                        } else {
                            sprintf(status_msg, "[Net] %s placed %d", opp_name, pkt.val);
                        }
                    } else { 
                        opp_puzzle[pkt.r][pkt.c] = pkt.val; 
                    }
                }
                else if (pkt.type == 'X') {
                    if (game_mode == 1) puzzle[pkt.r][pkt.c] = 0;
                    else opp_puzzle[pkt.r][pkt.c] = 0;
                }
                else if (pkt.type == 'F') { 
                    if (isWin(size, puzzle)) { 
                        if (mistakes_found < pkt.mistakes) is_winner = 1;
                        else if (mistakes_found > pkt.mistakes) is_winner = 0;
                        else if (hints_used <= pkt.hints) is_winner = 1;
                        else is_winner = 0;
                    } else {
                        is_winner = 0; 
                    }
                    break;
                }
                else if (pkt.type == 'Q') {
                    sprintf(status_msg, "[!] %s disconnected. Transitioned to Singleplayer.", opp_name);
                    close(net_sock); net_sock = -1; game_mode = 0; opp_r = -1; opp_c = -1;
                }
                continue; 
            }
        }

        // --- HANDLE LOCAL INPUT ---
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (read(STDIN_FILENO, &c, 1) == 0) continue; 

            status_msg[0] = '\0'; 
            int cursor_moved = 0;

            if (c == 'w' || c == 'W') { if (cursor_r > 0) { cursor_r--; cursor_moved = 1; } }
            else if (c == 's' || c == 'S') { if (cursor_r < size - 1) { cursor_r++; cursor_moved = 1; } }
            else if (c == 'a' || c == 'A') { if (cursor_c > 0) { cursor_c--; cursor_moved = 1; } }
            else if (c == 'd' || c == 'D') { if (cursor_c < size - 1) { cursor_c++; cursor_moved = 1; } }
            
            if (cursor_moved && net_sock != -1) {
                NetPacket pkt = {'C', cursor_r, cursor_c, 0, mistakes_found, hints_left};
                send(net_sock, &pkt, sizeof(NetPacket), 0);
            }
            else if (c == 'q' || c == 'Q') {
                pthread_mutex_lock(&timer_mutex); int snap_time = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);
                disableRawMode();
                if (net_sock != -1) {
                    NetPacket pkt = {'Q', 0, 0, 0, 0, 0};
                    send(net_sock, &pkt, sizeof(NetPacket), 0);
                    close(net_sock);
                }
                saveGameProcess(size, regions, initial_puzzle, puzzle, snap_time, hints_left, initial_hint_quota, hints_used, mistakes_found);
                break;
            }
            else if (c >= '1' && c <= '9') {
                int num = c - '0';
                if (initial_puzzle[cursor_r][cursor_c] != 0) {
                    sprintf(status_msg, "[!] Cannot overwrite original clues or locked cells.");
                } else if (isValidMove(cursor_r, cursor_c, num, size, puzzle, regions, region_sizes, status_msg)) {
                    puzzle[cursor_r][cursor_c] = num;
                    
                    if (game_mode == 1 && num == solution[cursor_r][cursor_c]) {
                        initial_puzzle[cursor_r][cursor_c] = num; 
                        sprintf(status_msg, "[+] Perfect match! Cell permanently locked.");
                    } else {
                        sprintf(status_msg, "[+] Placed %d.", num);
                    }

                    if (net_sock != -1) {
                        NetPacket pkt = {'M', cursor_r, cursor_c, num, mistakes_found, hints_left};
                        send(net_sock, &pkt, sizeof(NetPacket), 0);
                    }
                } else {
                    mistakes_found++; 
                }
            }
            else if (c == '0' || c == ' ' || c == 127 || c == 8) { 
                if (initial_puzzle[cursor_r][cursor_c] == 0) {
                    puzzle[cursor_r][cursor_c] = 0;
                    sprintf(status_msg, "[-] Cell cleared.");
                    if (net_sock != -1) {
                        NetPacket pkt = {'X', cursor_r, cursor_c, 0, mistakes_found, hints_left};
                        send(net_sock, &pkt, sizeof(NetPacket), 0);
                    }
                } else {
                    sprintf(status_msg, "[!] Cannot clear locked clues.");
                }
            }
            else if (c == 'h' || c == 'H') {
                if (initial_puzzle[cursor_r][cursor_c] != 0) {
                    sprintf(status_msg, "[!] Cannot provide options for a locked clue.");
                } else {
                    int max_val = region_sizes[regions[cursor_r][cursor_c]];
                    char opts[64] = "";
                    int count = 0;
                    
                    for (int n = 1; n <= max_val; n++) {
                        if (isValidMove(cursor_r, cursor_c, n, size, puzzle, regions, region_sizes, NULL)) {
                            char temp[16]; sprintf(temp, "%d ", n); strcat(opts, temp); count++;
                        }
                    }
                    if (count > 0) sprintf(status_msg, "[Hint] Valid options here: %s", opts);
                    else sprintf(status_msg, "[Hint] No valid options! The board currently has a mistake.");
                }
            }
            else if (c == 'c' || c == 'C') {
                int temp_puzzle[MAX][MAX];
                for(int i=0; i<size; i++) for(int j=0; j<size; j++) temp_puzzle[i][j] = puzzle[i][j];
                
                if (solveBoard(size, temp_puzzle, regions, region_sizes)) {
                    sprintf(status_msg, "[+] Check passed! Your board is solvable from here.");
                } else {
                    mistakes_found++; 
                    sprintf(status_msg, "[!] Mistakes found! Your current board configuration cannot be solved.");
                }
            }
            else if (c == '?') {
                if (!auto_solve_unlocked) {
                    disableRawMode();
                    printf(YELLOW "\n[Security] Enter password to unlock Auto-Solve: " RESET);
                    char pwd[32];
                    if (fgets(pwd, sizeof(pwd), stdin)) {
                        pwd[strcspn(pwd, "\n")] = 0; 
                        if (strcmp(pwd, "admin") == 0) { 
                            auto_solve_unlocked = 1;
                            sprintf(status_msg, "[+] Auto-Solve unlocked! Press/hold ? to use.");
                        } else {
                            sprintf(status_msg, "[!] Incorrect password! Auto-Solve locked.");
                        }
                    }
                    enableRawMode();
                    continue; 
                }
                
                if (hints_left <= 0 && hints_left != -1) {
                    sprintf(status_msg, "[!] Out of hints!");
                } else {
                    int placed = 0;
                    for (int i = 0; i < size && !placed; i++) {
                        for (int j = 0; j < size; j++) {
                            if (puzzle[i][j] == 0) {
                                puzzle[i][j] = solution[i][j];
                                cursor_r = i; cursor_c = j;
                                placed = 1;
                                
                                if (game_mode == 1) initial_puzzle[i][j] = solution[i][j];
                                
                                sprintf(status_msg, "[Auto-Solve] AI placed correct %d here!", puzzle[i][j]);
                                if (hints_left > 0) hints_left--;
                                hints_used++;
                                
                                if (net_sock != -1) {
                                    NetPacket pkt = {'M', cursor_r, cursor_c, puzzle[cursor_r][cursor_c], mistakes_found, hints_left};
                                    send(net_sock, &pkt, sizeof(NetPacket), 0);
                                }
                                break;
                            }
                        }
                    }
                    if (!placed) sprintf(status_msg, "[!] Board is full or unfixable mistake exists.");
                }
            }
        }
    }

    keep_timer_running = 0;
    pthread_join(timer_thread, NULL);
    disableRawMode(); 
    
    pthread_mutex_lock(&timer_mutex);
    int final_time = elapsed_seconds;
    pthread_mutex_unlock(&timer_mutex);
            
    if (is_winner) {
        printf(GREEN BOLD "\n*** CONGRATULATIONS! YOU WON IN %02d:%02d! ***\n" RESET, final_time / 60, final_time % 60);
    } else {
        printf(RED BOLD "\n*** GAME OVER! %s WON THE MATCH. ***\n" RESET, opp_name);
    }
            
    saveScore(size, final_time, getDifficultyString(initial_hint_quota), hints_used, mistakes_found, game_mode, is_winner, opp_name);
    printf(YELLOW "Your stats have been successfully saved to the Leaderboard!\n\n" RESET);
    if (net_sock != -1) close(net_sock);
}

void* timer_thread_func(void* arg) {
    while(keep_timer_running) {
        sleep(1);
        pthread_mutex_lock(&timer_mutex);
        elapsed_seconds++;
        pthread_mutex_unlock(&timer_mutex);
    }
    return NULL;
}

void handle_sigint(int sig) { sigint_received = 1; }
void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
    errno = saved_errno;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);   
    signal(SIGCHLD, handle_sigchld); 
    
    createDefaultFiles(); 

    int size = 0, saved_time = 0, hints_left = 0, initial_hint_quota = 0, hints_used = 0, mistakes_found = 0;
    int regions[MAX][MAX], initial_puzzle[MAX][MAX], puzzle[MAX][MAX];
    char input[256];
    int choice;

    printf("\033[2J\033[H");
    printf(CYAN BOLD "=====================================\n");
    printf("     WELCOME TO RIPPLE EFFECT        \n");
    printf("=====================================\n" RESET);
    printf("Enter your Username: ");
    if (fgets(global_username, sizeof(global_username), stdin)) {
        global_username[strcspn(global_username, "\n")] = 0;
    }
    if (strlen(global_username) == 0) strcpy(global_username, "Player");

    if (argc == 2) {
        printf(YELLOW "\nAttempting to load file: %s\n" RESET, argv[1]);
        if(loadPuzzleFromFile(argv[1], &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
            pthread_mutex_lock(&timer_mutex); elapsed_seconds = saved_time; pthread_mutex_unlock(&timer_mutex);
            
            int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
            int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
            
            playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, -1, 0, "");
        } else {
            printf(RED "[!] Could not load %s. Falling back to main menu.\n" RESET, argv[1]);
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
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) break;
            clearerr(stdin);
            if(sigint_received) { sigint_received = 0; printf("\nUse option 0 to exit.\n"); }
            continue;
        }

        if (sscanf(input, "%d", &choice) != 1) continue;

        if (choice == 1) {
            int map_id = getMapChoice(6);
            if (map_id == -1) continue;
            initial_hint_quota = getHintChoice();
            if (initial_hint_quota == -2) continue; 
            
            hints_left = initial_hint_quota; hints_used = 0; mistakes_found = 0;
            char filename[32]; sprintf(filename, "puzzle6_%d.txt", map_id);
            
            if(loadPuzzleFromFile(filename, &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                
                int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
                
                playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, -1, 0, "");
            } else printf(RED "\n[!] Error: %s corrupted.\n" RESET, filename);
            
        } else if (choice == 2) {
            int map_id = getMapChoice(8);
            if (map_id == -1) continue;
            initial_hint_quota = getHintChoice();
            if (initial_hint_quota == -2) continue; 
            
            hints_left = initial_hint_quota; hints_used = 0; mistakes_found = 0;
            char filename[32]; sprintf(filename, "puzzle8_%d.txt", map_id);
            
            if(loadPuzzleFromFile(filename, &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                
                int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
                
                playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, -1, 0, "");
            } else printf(RED "\n[!] Error: %s corrupted.\n" RESET, filename);
            
        } else if (choice == 3) {
            if(loadPuzzleFromFile("savegame_6.txt", &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = saved_time; pthread_mutex_unlock(&timer_mutex);
                printf(GREEN "\n[+] Save loaded! Resuming...\n" RESET); sleep(1);
                
                int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
                
                playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, -1, 0, "");
            } else printf(RED "\n[!] Error: No valid 'savegame_6.txt' found.\n" RESET);
            
        } else if (choice == 4) {
            if(loadPuzzleFromFile("savegame_8.txt", &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
                pthread_mutex_lock(&timer_mutex); elapsed_seconds = saved_time; pthread_mutex_unlock(&timer_mutex);
                printf(GREEN "\n[+] Save loaded! Resuming...\n" RESET); sleep(1);
                
                int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
                
                playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, -1, 0, "");
            } else printf(RED "\n[!] Error: No valid 'savegame_8.txt' found.\n" RESET);
            
        } else if (choice == 5) {
            viewLeaderboard();
        } else if (choice == 6) {
            // HOST MULTIPLAYER
            int board_size, game_mode;
            printf(CYAN "Select Board Size for Hosting (6 or 8): " RESET);
            if (fgets(input, sizeof(input), stdin) == NULL || sscanf(input, "%d", &board_size) != 1 || (board_size != 6 && board_size != 8)) continue;
            
            int map_id = getMapChoice(board_size);
            if (map_id == -1) continue;
            initial_hint_quota = getHintChoice();
            if (initial_hint_quota == -2) continue; 
            
            printf(CYAN "Select Mode (1 = Co-op, 2 = Versus): " RESET);
            if (fgets(input, sizeof(input), stdin) == NULL || sscanf(input, "%d", &game_mode) != 1 || (game_mode != 1 && game_mode != 2)) continue;

            char filename[32]; sprintf(filename, "puzzle%d_%d.txt", board_size, map_id);
            hints_left = initial_hint_quota; hints_used = 0; mistakes_found = 0;
            
            if(loadPuzzleFromFile(filename, &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &initial_hint_quota, &hints_used, &mistakes_found)) {
                int net_sock = startServer();
                if (net_sock != -1) {
                    SyncPacket sp;
                    sp.size = size; sp.hints_left = hints_left; sp.initial_hint_quota = initial_hint_quota;
                    sp.game_mode = game_mode; strcpy(sp.host_username, global_username);
                    memcpy(sp.regions, regions, sizeof(sp.regions)); memcpy(sp.initial_puzzle, initial_puzzle, sizeof(sp.initial_puzzle)); memcpy(sp.puzzle, puzzle, sizeof(sp.puzzle));
                    send(net_sock, &sp, sizeof(SyncPacket), 0);
                    
                    InitPacket ip_pkt;
                    recv(net_sock, &ip_pkt, sizeof(InitPacket), MSG_WAITALL);
                    
                    int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                    int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);

                    pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                    playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, initial_hint_quota, hints_used, mistakes_found, net_sock, game_mode, ip_pkt.username);
                }
            } else printf(RED "\n[!] Error loading map.\n" RESET);

        } else if (choice == 7) {
            // JOIN MULTIPLAYER
            char ip[64];
            printf(CYAN "Enter Host IP Address (e.g. 127.0.0.1): " RESET);
            if (fgets(ip, sizeof(ip), stdin)) {
                ip[strcspn(ip, "\n")] = 0; 
                int net_sock = connectToServer(ip);
                if (net_sock != -1) {
                    SyncPacket sp;
                    if (recv(net_sock, &sp, sizeof(SyncPacket), MSG_WAITALL) > 0) {
                        InitPacket ip_pkt; strcpy(ip_pkt.username, global_username);
                        send(net_sock, &ip_pkt, sizeof(InitPacket), 0);
                        
                        int solution[MAX][MAX]; memcpy(solution, sp.puzzle, sizeof(solution));
                        int temp_sizes[MAX_REGIONS]; getRegionSizes(sp.size, sp.regions, temp_sizes); solveBoard(sp.size, solution, sp.regions, temp_sizes);

                        pthread_mutex_lock(&timer_mutex); elapsed_seconds = 0; pthread_mutex_unlock(&timer_mutex);
                        playGame(sp.size, sp.regions, sp.initial_puzzle, sp.puzzle, solution, sp.hints_left, sp.initial_hint_quota, 0, 0, net_sock, sp.game_mode, sp.host_username);
                    } else printf(RED "[!] Failed to sync game state with host.\n" RESET);
                }
            }
        } else if (choice == 0) {
            printf(GREEN "\nGoodbye!\n" RESET); break;
        } else {
            printf(RED "\n[!] Invalid choice.\n" RESET);
        }
    }
    return 0;
}