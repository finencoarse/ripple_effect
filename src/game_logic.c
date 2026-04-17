#include "game.h"
#include <stdio.h>  
 // transfer completed
void getRegionSizes(int size, int regions[MAX][MAX], int region_sizes[]) {
    for(int i = 0; i < MAX_REGIONS; i++) region_sizes[i] = 0; 
    for(int r = 0; r < size; r++) {
        for(int c = 0; c < size; c++) { 
            if (regions[r][c] < MAX_REGIONS) region_sizes[regions[r][c]]++;
        }
    }
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
                if(status_msg) sprintf(status_msg, "[!] Incorrect: Ripple Rule! Same row distance must be > %d.", num);
                return 0;
            }
        }
    }
    for (int i = 0; i < size; i++) {
        if (i != r && puzzle[i][c] == num) {
            int dist = (r > i) ? (r - i) : (i - r);
            if (dist <= num) {
                if(status_msg) sprintf(status_msg, "[!] Incorrect: Ripple Rule! Same col distance must be > %d.", num);
                return 0;
            }
        }
    }
    return 1;
}

int isWin(int size, int puzzle[MAX][MAX]) {
    for (int i = 0; i < size; i++)
        for (int j = 0; j < size; j++)
            if (puzzle[i][j] == 0) return 0;
    return 1;
}

int solveBoard(int size, int puzzle[MAX][MAX], int regions[MAX][MAX], int region_sizes[]) {
    int r = -1, c = -1;
    int is_full = 1;
    
    for (int i = 0; i < size && is_full; i++) {
        for (int j = 0; j < size; j++) {
            if (puzzle[i][j] == 0) {
                r = i; c = j;
                is_full = 0;
            }
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