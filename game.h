// --- game.h ---
#ifndef GAME_H
#define GAME_H

#include <signal.h>
#include <pthread.h>
#include <termios.h>

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

// --- NETWORK STRUCTS ---
typedef struct {
    char type; 
    int r, c, val, mistakes, hints;
} NetPacket;

typedef struct {
    int size, game_mode; 
    char host_username[32];
    int regions[MAX][MAX], initial_puzzle[MAX][MAX], puzzle[MAX][MAX];
    int hints_left, initial_hint_quota;
} SyncPacket;

typedef struct { char username[32]; } InitPacket;

// --- GLOBAL VARIABLES (Using extern) ---
extern volatile sig_atomic_t sigint_received;
extern volatile int keep_timer_running;
extern int elapsed_seconds;
extern pthread_mutex_t timer_mutex;
extern struct termios orig_termios; 
extern char global_username[32];

// --- FUNCTION PROTOTYPES ---

// From ui.c
void enableRawMode();
void disableRawMode();
void printBoard(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int opp_puzzle[MAX][MAX], int hints_left, int mistakes_found, int cursor_r, int cursor_c, int opp_r, int opp_c, int game_mode, const char* opp_name, const char* status_msg);
int getMapChoice(int size);
int getHintChoice();
const char* getDifficultyString(int initial_hints);
void sanitize_username(char* name, int max_len);

// From game_logic.c
void getRegionSizes(int size, int regions[MAX][MAX], int region_sizes[]);
int isWin(int size, int puzzle[MAX][MAX]);
int isValidMove(int r, int c, int num, int size, int puzzle[MAX][MAX], int regions[MAX][MAX], int region_sizes[], char* status_msg);
int solveBoard(int size, int puzzle[MAX][MAX], int regions[MAX][MAX], int region_sizes[]);

// From network.c
int startServer();
int connectToServer(const char* ip);

// From file_io.c
void createDefaultFiles();
int loadPuzzleFromFile(const char *filename, int *size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int *saved_time, int *hints_left, int *initial_hint_quota, int *hints_used, int *mistakes_found);
void saveGameProcess(int size, int regions[MAX][MAX], int initial_puzzle[MAX][MAX], int puzzle[MAX][MAX], int current_time, int hints_left, int initial_hint_quota, int hints_used, int mistakes_found);
void saveScore(int size, int final_time, const char* difficulty, int hints_used, int mistakes_found, int game_mode, int is_win, const char* opp_name);
void viewLeaderboard();

#endif