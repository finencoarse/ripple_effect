#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/file.h> 
#include <fcntl.h>    
#include <termios.h>  
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX 10 
#define MAX_REGIONS (MAX * MAX)

// --- UI/UX ANSI Color Codes ---
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"
#define INV_CURSOR  "\033[47;30m" 
#define OPP_CURSOR  "\033[41;37m" 
#define BOTH_CURSOR "\033[45;37m" 

volatile sig_atomic_t sigint_received = 0;
volatile int keep_timer_running = 1;
int elapsed_seconds = 0;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
struct termios orig_termios; 
char global_username[32] = "Player";

// --- NETWORK STRUCTS ---
typedef struct {
    char type; // 'C' (cursor), 'M' (move), 'X' (clear), 'Q' (quit), 'F' (finished/win check)
    int r;
    int c;
    int val;
    int mistakes;
    int hints;
} NetPacket;

typedef struct {
    int size;
    int game_mode; // 0=Single, 1=Coop, 2=Versus
    char host_username[32];
    int regions[MAX][MAX];
    int initial_puzzle[MAX][MAX];
    int puzzle[MAX][MAX];
    int hints_left;
    int initial_hint_quota;
} SyncPacket;

typedef struct {
    char username[32];
} InitPacket;

void handle_sigint(int sig) { sigint_received = 1; }
void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
    errno = saved_errno;
}

void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); 
    raw.c_cc[VMIN] = 0;              
    raw.c_cc[VTIME] = 5;             
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
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

void createDefaultFiles() {
    FILE *f;
    if ((f = fopen("puzzle6_1.txt", "r"))) fclose(f);
    else if ((f = fopen("puzzle6_1.txt", "w"))) {
        fprintf(f, "6\n0 7 7 8 8 9\n0 6 7 8 9 9\n2 2 2 8 9 9\n3 5 5 10 12 12\n3 4 5 10 10 12\n3 3 5 11 10 10\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n0 0 0 0 0 0\n");
        fclose(f);
    }
    if ((f = fopen("puzzle6_2.txt", "r"))) fclose(f);
    else if ((f = fopen("puzzle6_2.txt", "w"))) {
        fprintf(f, "6\n0 0 0 1 1 1\n0 0 0 1 1 1\n2 2 2 3 3 3\n2 2 2 3 3 3\n4 4 4 5 5 5\n4 4 4 5 5 5\n1 0 0 0 5 0\n0 0 6 0 0 0\n0 3 0 5 0 0\n0 0 4 0 3 0\n0 0 0 6 0 0\n0 4 0 0 0 2\n");
        fclose(f);
    }
    if ((f = fopen("puzzle8_1.txt", "r"))) fclose(f);
    else if ((f = fopen("puzzle8_1.txt", "w"))) {
        fprintf(f, "8\n0 1 1 1 15 14 14 13\n0 0 2 15 15 15 14 13\n0 2 2 2 15 11 11 13\n0 3 2 4 11 11 12 13\n5 5 4 4 4 11 10 13\n5 6 7 4 8 10 10 10\n5 6 7 8 8 8 10 9\n5 6 6 6 8 9 9 9\n4 0 0 2 0 0 0 0\n0 0 0 0 0 4 0 0\n0 0 0 0 0 0 8 0\n0 0 0 0 0 0 0 0\n0 0 0 0 4 2 0 0\n0 0 0 0 0 0 1 0\n0 0 0 1 0 0 0 0\n0 0 0 0 0 0 0 0\n");
        fclose(f);
    }
}

const char* getDifficultyString(int initial_hints) {
    if (initial_hints == -1) return "Easy";
    if (initial_hints == 10) return "Normal";
    if (initial_hints == 5) return "Hard";
    if (initial_hints == 0) return "Extreme";
    return "Custom";
}

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
        printf(YELLOW "      --- YOUR BOARD ---                          --- %s'S BOARD ---\n" RESET, opp_name);
    }

    // Top Numbers
    printf("    ");
    for(int j = 0; j < size; j++) printf(BOLD "%d   " RESET, j + 1);
    if (game_mode == 2) {
        printf("      ");
        for(int j = 0; j < size; j++) printf(BOLD "%d   " RESET, j + 1);
    }
    printf("\n");

    for (int i = 0; i < size; i++) {
        // Horizontal Grid Lines
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

        // Cell Values
        printf(" " BOLD "%d " RESET, i + 1); 
        for (int j = 0; j < size; j++) {
            if (j == 0 || regions[i][j] != regions[i][j-1]) printf(BLUE "|" RESET); 
            else printf(" "); 
            
            int is_cursor = (i == cursor_r && j == cursor_c);
            int is_opp_cursor = (game_mode == 1 && i == opp_r && j == opp_c); // Coop opponent cursor

            if (is_cursor && is_opp_cursor) printf(BOTH_CURSOR);
            else if (is_cursor) printf(INV_CURSOR);
            else if (is_opp_cursor) printf(OPP_CURSOR);
            
            if (puzzle[i][j] == 0) printf(" . ");
            else if (initial_puzzle[i][j] != 0) {
                if (!is_cursor && !is_opp_cursor) printf(YELLOW BOLD);
                printf(" %d ", puzzle[i][j]); 
            } else {
                if (!is_cursor && !is_opp_cursor) printf(GREEN BOLD);
                printf(" %d ", puzzle[i][j]);       
            }
            if (is_cursor || is_opp_cursor || initial_puzzle[i][j] != 0 || puzzle[i][j] != 0) printf(RESET); 
        }
        printf(BLUE "|" RESET); 
        
        // Opponent Board (Versus)
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
                    if (!is_opp_vs_cursor) printf(RED BOLD); // Render opp progress as red
                    printf(" %d ", opp_puzzle[i][j]);       
                }
                if (is_opp_vs_cursor || initial_puzzle[i][j] != 0 || opp_puzzle[i][j] != 0) printf(RESET); 
            }
            printf(BLUE "|" RESET); 
        }
        printf("\n");
    }

    // Bottom Border
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

void getRegionSizes(int size, int regions[MAX][MAX], int region_sizes[]) {
    for(int i = 0; i < MAX_REGIONS; i++) region_sizes[i] = 0; 
    for(int r = 0; r < size; r++)
        for(int c = 0; c < size; c++) 
            if (regions[r][c] < MAX_REGIONS) region_sizes[regions[r][c]]++;
}

int isWin(int size, int puzzle[MAX][MAX]) {
    for (int i = 0; i < size; i++)
        for (int j = 0; j < size; j++)
            if (puzzle[i][j] == 0) return 0;
    return 1;
}

int isValidMove(int r, int c, int num, int size, int puzzle[MAX][MAX], int regions[MAX][MAX], int region_sizes[], char* status_msg) {
    int reg = regions[r][c];
    if (num > region_sizes[reg]) {
        if(status_msg) sprintf(status_msg, "[!] Incorrect: Number %d > region capacity (%d).", num, region_sizes[reg]);
        return 0;
    }
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            if (regions[i][j] == reg && (i != r || j != c)) {
                if (puzzle[i][j] == num) {
                    if(status_msg) sprintf(status_msg, "[!] Incorrect: Number %d already exists in this region.", num);
                    return 0;
                }
            }
        }
    }
    for (int j = 0; j < size; j++) {
        if (j != c && puzzle[r][j] == num) {
            int dist = (c > j) ? (c - j) : (j - c);
            if (dist <= num) {
                if(status_msg) sprintf(status_msg, "[!] Incorrect: Ripple Rule! Row distance must be > %d.", num);
                return 0;
            }
        }
    }
    for (int i = 0; i < size; i++) {
        if (i != r && puzzle[i][c] == num) {
            int dist = (r > i) ? (r - i) : (i - r);
            if (dist <= num) {
                if(status_msg) sprintf(status_msg, "[!] Incorrect: Ripple Rule! Col distance must be > %d.", num);
                return 0;
            }
        }
    }
    return 1;
}

int solveBoard(int size, int puzzle[MAX][MAX], int regions[MAX][MAX], int region_sizes[]) {
    int r = -1, c = -1;
    int is_full = 1;
    for (int i = 0; i < size && is_full; i++) {
        for (int j = 0; j < size; j++) {
            if (puzzle[i][j] == 0) { r = i; c = j; is_full = 0; }
        }
    }
    if (is_full) return 1; 

    int max_val = region_sizes[regions[r][c]];
    for (int n = 1; n <= max_val; n++) {
        if (isValidMove(r, c, n, size, puzzle, regions, region_sizes, NULL)) {
            puzzle[r][c] = n;
            if (solveBoard(size, puzzle, regions, region_sizes)) return 1; 
            puzzle[r][c] = 0; 
        }
    }
    return 0; 
}

int loadPuzzleFromFile(const char *filename, int *size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int *saved_time, int *hints_left, int *initial_hint_quota, int *hints_used, int *mistakes_found) {
    FILE *f = fopen(filename, "r");
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
            for (int j = 0; j < *size; j++) fscanf(f, "%d", &puzzle[i][j]);
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

void saveScore(int size, int final_time, const char* difficulty, int hints_used, int mistakes_found, int game_mode, int is_win, const char* opp_name) {
    FILE *f = fopen("leaderboard.txt", "a");
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
    FILE *f = fopen("leaderboard.txt", "r");
    if (!f) {
        printf(YELLOW "No scores yet. Complete a game to see your stats here!\n" RESET);
    } else {
        char line[256];
        while (fgets(line, sizeof(line), f)) printf("%s", line);
        fclose(f);
    }
    printf(CYAN BOLD "===============================================================================\n" RESET);
    printf("Press Enter to return to main menu...");
    char buf[256]; fgets(buf, sizeof(buf), stdin);
}

// --- NETWORK HELPER FUNCTIONS ---
int startServer() {
    int server_fd; struct sockaddr_in address; int opt = 1;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { perror(RED "[!] Socket failed" RESET); return -1; }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { perror(RED "[!] Setsockopt failed" RESET); return -1; }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8888); // Using 8888 safely

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { perror(RED "[!] Bind failed (Port busy)" RESET); return -1; }
    if (listen(server_fd, 3) < 0) { perror(RED "[!] Listen failed" RESET); return -1; }

    printf(YELLOW "Hosting on Port 8888. Waiting for opponent to join...\n" RESET);
    int addrlen = sizeof(address);
    int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    close(server_fd); return client_socket;
}

int connectToServer(const char* ip) {
    int sock = 0; struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror(RED "[!] Socket error" RESET); return -1; }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8888);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) { perror(RED "[!] Invalid address" RESET); return -1; }
    printf(YELLOW "Connecting to Host at %s...\n" RESET, ip);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) { perror(RED "[!] Connection Failed" RESET); return -1; }
    return sock;
}

// --- MAIN GAME LOOP ---
void playGame(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int solution[MAX][MAX], int hints_left, int initial_hint_quota, int hints_used, int mistakes_found, int net_sock, int game_mode, const char* opp_name) {
    int region_sizes[MAX_REGIONS]; getRegionSizes(size, regions, region_sizes);
    int opp_puzzle[MAX][MAX];
    for(int i=0; i<size; i++) for(int j=0; j<size; j++) opp_puzzle[i][j] = initial_puzzle[i][j];

    pthread_t timer_thread; keep_timer_running = 1;
    pthread_create(&timer_thread, NULL, timer_thread_func, NULL);

    int cursor_r = 0, cursor_c = 0;
    int opp_r = -1, opp_c = -1;
    int is_winner = 0; 
    
    char status_msg[256];
    if (net_sock != -1) sprintf(status_msg, "[+] Connected! Multiplayer Game (%s) Started.", game_mode == 1 ? "Co-op" : "Versus");
    else strcpy(status_msg, "Game Started! Use WASD to move. Press ? for Auto-Solve.");
    
    char c;
    enableRawMode(); 

    while(1) {
        printBoard(size, regions, initial_puzzle, puzzle, opp_puzzle, hints_left, mistakes_found, cursor_r, cursor_c, opp_r, opp_c, game_mode, opp_name, status_msg);

        // Win Logic Check
        if (isWin(size, puzzle)) {
            if (game_mode != 2) { is_winner = 1; break; } // Single/Coop win
            else { // Versus Win
                NetPacket pkt = {'F', 0, 0, 0, mistakes_found, hints_used};
                send(net_sock, &pkt, sizeof(NetPacket), 0);
                is_winner = 1; 
                break;
            }
        }

        fd_set readfds; FD_ZERO(&readfds); FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;
        if (net_sock != -1) { FD_SET(net_sock, &readfds); if (net_sock > max_fd) max_fd = net_sock; }

        struct timeval tv = {0, 50000}; 
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) continue;

        // NETWORK INPUT
        if (net_sock != -1 && FD_ISSET(net_sock, &readfds)) {
            NetPacket pkt;
            if (recv(net_sock, &pkt, sizeof(NetPacket), 0) <= 0) {
                sprintf(status_msg, "[!] %s disconnected.", opp_name);
                close(net_sock); net_sock = -1; game_mode = 0; // Fallback to singleplayer
            } else {
                status_msg[0] = '\0';
                if (game_mode == 1 && pkt.hints < hints_left && pkt.hints >= 0) hints_left = pkt.hints; // Sync shared hints

                if (pkt.type == 'C') { opp_r = pkt.r; opp_c = pkt.c; }
                else if (pkt.type == 'M') { 
                    if (game_mode == 1) { // Co-op
                        puzzle[pkt.r][pkt.c] = pkt.val;
                        if (pkt.val == solution[pkt.r][pkt.c]) {
                            initial_puzzle[pkt.r][pkt.c] = pkt.val; // Lock it!
                            sprintf(status_msg, "[Net] %s placed correct %d (Locked)", opp_name, pkt.val);
                        } else sprintf(status_msg, "[Net] %s placed %d", opp_name, pkt.val);
                    } else { // Versus
                        opp_puzzle[pkt.r][pkt.c] = pkt.val; 
                    }
                }
                else if (pkt.type == 'X') {
                    if (game_mode == 1) puzzle[pkt.r][pkt.c] = 0;
                    else opp_puzzle[pkt.r][pkt.c] = 0;
                }
                else if (pkt.type == 'F') { // Versus Opponent Finished
                    if (isWin(size, puzzle)) { // Rare Tie Break
                        if (mistakes_found < pkt.mistakes) is_winner = 1;
                        else if (mistakes_found > pkt.mistakes) is_winner = 0;
                        else if (hints_used <= pkt.hints) is_winner = 1;
                        else is_winner = 0;
                    } else is_winner = 0; // I didn't finish
                    break;
                }
                continue; 
            }
        }

        // LOCAL INPUT
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (read(STDIN_FILENO, &c, 1) == 0) continue; 
            status_msg[0] = '\0'; int cursor_moved = 0;

            if (c == 'w' || c == 'W') { if (cursor_r > 0) { cursor_r--; cursor_moved = 1; } }
            else if (c == 's' || c == 'S') { if (cursor_r < size - 1) { cursor_r++; cursor_moved = 1; } }
            else if (c == 'a' || c == 'A') { if (cursor_c > 0) { cursor_c--; cursor_moved = 1; } }
            else if (c == 'd' || c == 'D') { if (cursor_c < size - 1) { cursor_c++; cursor_moved = 1; } }
            
            if (cursor_moved && net_sock != -1) {
                NetPacket pkt = {'C', cursor_r, cursor_c, 0, mistakes_found, hints_left};
                send(net_sock, &pkt, sizeof(NetPacket), 0);
            }
            else if (c == 'q' || c == 'Q') break;
            else if (c >= '1' && c <= '9') {
                int num = c - '0';
                if (initial_puzzle[cursor_r][cursor_c] != 0) sprintf(status_msg, "[!] Cell is locked.");
                else if (isValidMove(cursor_r, cursor_c, num, size, puzzle, regions, region_sizes, status_msg)) {
                    puzzle[cursor_r][cursor_c] = num;
                    if (game_mode == 1 && num == solution[cursor_r][cursor_c]) {
                        initial_puzzle[cursor_r][cursor_c] = num; // COOP LOCK!
                        sprintf(status_msg, "[+] Perfect match! Cell locked.");
                    } else sprintf(status_msg, "[+] Placed %d.", num);
                    
                    if (net_sock != -1) {
                        NetPacket pkt = {'M', cursor_r, cursor_c, num, mistakes_found, hints_left};
                        send(net_sock, &pkt, sizeof(NetPacket), 0);
                    }
                } else mistakes_found++; 
            }
            else if (c == '0' || c == ' ' || c == 127 || c == 8) { 
                if (initial_puzzle[cursor_r][cursor_c] == 0) {
                    puzzle[cursor_r][cursor_c] = 0;
                    sprintf(status_msg, "[-] Cell cleared.");
                    if (net_sock != -1) {
                        NetPacket pkt = {'X', cursor_r, cursor_c, 0, mistakes_found, hints_left};
                        send(net_sock, &pkt, sizeof(NetPacket), 0);
                    }
                } else sprintf(status_msg, "[!] Cell is locked.");
            }
            else if (c == '?') {
                if (hints_left <= 0 && hints_left != -1) sprintf(status_msg, "[!] Out of hints!");
                else {
                    int r=-1, c=-1, placed=0;
                    for (int i = 0; i < size && !placed; i++) {
                        for (int j = 0; j < size; j++) {
                            if (puzzle[i][j] == 0) {
                                puzzle[i][j] = solution[i][j]; r = i; c = j; placed = 1;
                                if (game_mode == 1) initial_puzzle[i][j] = solution[i][j]; // Lock hint in Coop
                                break;
                            }
                        }
                    }
                    if (placed) {
                        if (hints_left > 0) hints_left--;
                        hints_used++;
                        sprintf(status_msg, "[Hint] AI placed %d at (%d,%d)", puzzle[r][c], r+1, c+1);
                        if (net_sock != -1) {
                            NetPacket pkt = {'M', r, c, puzzle[r][c], mistakes_found, hints_left};
                            send(net_sock, &pkt, sizeof(NetPacket), 0);
                        }
                    } else sprintf(status_msg, "[!] No empty cells to hint.");
                }
            }
        }
    }

    keep_timer_running = 0; pthread_join(timer_thread, NULL); disableRawMode(); 
    
    pthread_mutex_lock(&timer_mutex); int final_time = elapsed_seconds; pthread_mutex_unlock(&timer_mutex);
    
    if (is_winner) {
        printf(GREEN BOLD "\n*** CONGRATULATIONS! YOU WON IN %02d:%02d! ***\n" RESET, final_time / 60, final_time % 60);
    } else {
        printf(RED BOLD "\n*** GAME OVER! %s WON THE MATCH. ***\n" RESET, opp_name);
    }
    
    saveScore(size, final_time, getDifficultyString(initial_hint_quota), hints_used, mistakes_found, game_mode, is_winner, opp_name);
    printf(YELLOW "Stats successfully saved to the Leaderboard!\n\n" RESET);
    if (net_sock != -1) close(net_sock);
}

// MAIN MENU LOOP
int main() {
    createDefaultFiles(); 
    int size=0, saved_time=0, hints_left=0, quota=0, hints_used=0, mistakes_found=0, game_mode=0;
    int regions[MAX][MAX], initial_puzzle[MAX][MAX], puzzle[MAX][MAX];
    char input[256]; int choice;

    printf(CYAN BOLD "=====================================\n");
    printf("     WELCOME TO RIPPLE EFFECT        \n");
    printf("=====================================\n" RESET);
    printf("Enter your Username: ");
    if (fgets(global_username, sizeof(global_username), stdin)) global_username[strcspn(global_username, "\n")] = 0;
    if (strlen(global_username) == 0) strcpy(global_username, "Player");

    while(1) {
        printf(CYAN BOLD "\n=====================================\n" RESET);
        printf("1. Play Singleplayer\n2. View Leaderboard\n3. Host Multiplayer Game\n4. Join Multiplayer Game\n0. Exit\nChoice: ");
        if (!fgets(input, sizeof(input), stdin)) break;
        if (sscanf(input, "%d", &choice) != 1) continue;

        if (choice == 1) { // Singleplayer Fast Track
            size=6; game_mode=0; quota=-1; hints_left=-1;
            if(loadPuzzleFromFile("puzzle6_1.txt", &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &quota, &hints_used, &mistakes_found)) {
                int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes);
                solveBoard(size, solution, regions, temp_sizes);
                elapsed_seconds = 0; playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, quota, 0, 0, -1, 0, "");
            }
        } 
        else if (choice == 2) viewLeaderboard();
        else if (choice == 3) { // HOST
            printf(CYAN "Select Mode (1 = Co-op, 2 = Versus): " RESET);
            if (!fgets(input, sizeof(input), stdin) || sscanf(input, "%d", &game_mode) != 1 || (game_mode != 1 && game_mode != 2)) continue;
            
            size=6; quota=10; hints_left=10;
            if(loadPuzzleFromFile("puzzle6_1.txt", &size, regions, initial_puzzle, puzzle, &saved_time, &hints_left, &quota, &hints_used, &mistakes_found)) {
                int net_sock = startServer();
                if (net_sock != -1) {
                    SyncPacket sp; sp.size = size; sp.game_mode = game_mode; strcpy(sp.host_username, global_username); sp.hints_left = hints_left; sp.initial_hint_quota = quota;
                    memcpy(sp.regions, regions, sizeof(sp.regions)); memcpy(sp.initial_puzzle, initial_puzzle, sizeof(sp.initial_puzzle)); memcpy(sp.puzzle, puzzle, sizeof(sp.puzzle));
                    send(net_sock, &sp, sizeof(SyncPacket), 0);
                    
                    InitPacket ip_pkt; recv(net_sock, &ip_pkt, sizeof(InitPacket), MSG_WAITALL);
                    
                    int solution[MAX][MAX]; memcpy(solution, puzzle, sizeof(solution));
                    int temp_sizes[MAX_REGIONS]; getRegionSizes(size, regions, temp_sizes); solveBoard(size, solution, regions, temp_sizes);
                    elapsed_seconds = 0; playGame(size, regions, initial_puzzle, puzzle, solution, hints_left, quota, 0, 0, net_sock, game_mode, ip_pkt.username);
                }
            }
        } 
        else if (choice == 4) { // JOIN
            printf(CYAN "Enter Host IP (e.g. 127.0.0.1): " RESET);
            if (fgets(input, sizeof(input), stdin)) {
                input[strcspn(input, "\n")] = 0;
                int net_sock = connectToServer(input);
                if (net_sock != -1) {
                    SyncPacket sp;
                    if (recv(net_sock, &sp, sizeof(SyncPacket), MSG_WAITALL) > 0) {
                        InitPacket ip_pkt; strcpy(ip_pkt.username, global_username);
                        send(net_sock, &ip_pkt, sizeof(InitPacket), 0);
                        
                        int solution[MAX][MAX]; memcpy(solution, sp.puzzle, sizeof(solution));
                        int temp_sizes[MAX_REGIONS]; getRegionSizes(sp.size, sp.regions, temp_sizes); solveBoard(sp.size, solution, sp.regions, temp_sizes);
                        elapsed_seconds = 0; playGame(sp.size, sp.regions, sp.initial_puzzle, sp.puzzle, solution, sp.hints_left, sp.initial_hint_quota, 0, 0, net_sock, sp.game_mode, sp.host_username);
                    }
                }
            }
        } 
        else if (choice == 0) break;
    }
    return 0;
}
