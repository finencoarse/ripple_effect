# Ripple Effect Puzzle Game (Terminal Edition)

> **Note:** This project serves as a test case and prototype built for a Final Project. 

A fully-featured, multiplayer, terminal-based implementation of the Japanese logic puzzle **Ripple Effect** (Hakyuu Kouka). Written entirely in C with a clean, modular architecture, this project features a custom Backtracking AI solver, background game saving, and real-time TCP network multiplayer!

## Features
* **Classic Logic Rules:** Play on 6x6 or 8x8 verified maps with full Ripple Effect rule enforcement.
* **Smart AI Engine:** Features a built-in backtracking algorithm that provides hints, checks your board for unfixable mistakes, and can auto-solve the puzzle.
* **Three Game Modes:**
  *  **Singleplayer:** Play solo at your own pace.
  *  **Co-op Multiplayer:** Work together with a friend on a shared board. Correct placements are permanently locked!
  *  **Versus Multiplayer:** Race against an opponent. Boards are rendered side-by-side in real-time. Ties are broken by fewest mistakes/hints used.
* **Leaderboard System:** Enter your username and track your best times, difficulty levels, and multiplayer win/loss records.
* **Background Auto-Save:** Quit at any time and save your progress instantly (utilizes Linux `fork()` for seamless background saving without freezing the game).

## How to Play (Ripple Effect Rules)
The grid is divided into heavily outlined regions. To win, you must fill every empty cell with a number so that:
1. Every region contains the numbers from `1` to the `size` of the region (e.g., a region of 3 cells must contain 1, 2, and 3).
2. **The Ripple Rule:** If two identical numbers appear in the same row or column, they must be separated by at least that many empty cells. *(e.g., Two `3`s in the same row must have at least three other numbers between them).*

### Controls
* **[W, A, S, D]** - Move the cursor.
* **[1 - 9]** - Place a number.
* **[0]** or **[Space]** - Clear a cell.
* **[H]** - Show valid options for the current cell.
* **[C]** - Check the board for mistakes (Uses AI to see if the board is still solvable).
* **[?]** - Ask the AI to place a correct number for you (Costs 1 Hint).
* **[Q]** - Save and Quit to Main Menu.

## File Structure
The project is organized into a modular structure for maintainability and clean compilation:

```text
RippleGame/
├── Makefile             # Automated build instructions
├── README.md            
├── include/
│   └── game.h           # Shared structs, constants, and function prototypes
├── src/
│   ├── main.c           # Core game loops, threads, and signal handling
│   ├── game_logic.c     # Puzzle math, validation rules, and AI solver
│   ├── ui.c             # Terminal UI, ANSI graphics, and layout
│   ├── network.c        # TCP Sockets, hosting, and connecting
│   └── file_io.c        # Saving/loading game states and leaderboards
└── data/                # Auto-generated runtime files
    ├── puzzleX_X.txt    # Map layout files
    ├── savegame_X.txt   # Player save states
    └── leaderboard.txt  # Global player stats
```
*(Note: Ensure the `data/` folder exists before running, as the game stores all generated puzzles and save files there).*

## Prerequisites & Compilation
This program uses POSIX libraries (Linux/Mac). If you are on Windows, you must run this inside **WSL (Windows Subsystem for Linux)**, Cygwin, or MSYS2.

### To Compile:
The project uses a `Makefile` to automatically link the source files and the `pthread` library. Simply open your terminal in the root directory and run:

```bash
make
```

### To Run:
Once compiled, the executable `game` will be created. Run it using:
```bash
./game
```

*(To clean up the compiled executable later, you can run `make clean`).*

## Multiplayer Guide
The game uses raw TCP sockets on Port 8888.
* **To play with a friend on the SAME Wi-Fi (or localhost):**
  1. Player 1 selects *Host Multiplayer Game*.
  2. Player 2 selects *Join Multiplayer Game*.
  3. Player 2 types in Player 1's local IPv4 Address (e.g., `192.168.1.5` or `10.x.x.x`). *(If testing on the same computer, type `127.0.0.1`)*.

> **Note:** For users who don't know how to find their own IPv4 address, try this in the Linux terminal:
> ```bash
> hostname -I
> ```
> or simply try:
> ```cmd
> ipconfig
> ```
> in Windows CMD.

* **To play with a friend over the Internet:**
  Because standard Wi-Fi routers block incoming connections, you will need a virtual network tool.
  1. Both players download and install Tailscale or Radmin VPN.
  2. Connect to the same virtual network.
  3. The Host gives the Joiner their new Virtual IP (e.g., a `100.x.x.x` address from Tailscale).
  4. The Joiner connects using that Virtual IP.

* *Note for Windows WSL users:* If you are hosting on WSL, Windows may block the incoming connection. You may need to run `wsl --set-sparse true` in PowerShell, or install Tailscale directly inside your Linux terminal.

## To Contribute
Clone the repository to your local machine:
```bash
git clone https://github.com/finencoarse/ripple_effect
```
Be sure to pull updates after you test out a new feature!