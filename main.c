#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#define MAX_ROWS 64
#define MAX_COLS 64

// Define constants for the characters
#define WALL '#'
#define PLAYER '@'
#define PLAYER_ON_GOAL '+'
#define BOX '$'
#define BOX_ON_GOAL '*'
#define GOAL '.'
#define FLOOR ' '

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_WALL    "\033[44;94m"  // Bright blue foreground
#define COLOR_PLAYER  "\033[96m"     // Bright cyan foreground
#define COLOR_BOX     "\033[93m"     // Bright yellow foreground
#define COLOR_GOAL    "\033[43m"     // Dark yellow background
#define COLOR_PLAYER_ON_GOAL "\033[43;96m" // Dark yellow background, bright cyan foreground
#define COLOR_BOX_ON_GOAL "\033[43;93m" // Dark yellow background, yellow foreground

#define IS_WALL(x,y) \
  (x < 0 || x >= state->rows || y < 0 || y >= state->cols || state->board[x][y] == WALL)
#define IS_BOX(x,y) \
  ((state->board[x][y] == BOX) || (state->board[x][y] == BOX_ON_GOAL))
#define IS_PLAYER(x,y) \
  ((state->board[x][y] == PLAYER) || (state->board[x][y] == PLAYER_ON_GOAL))
#define REMOVE_BOX(x,y) \
  state->board[x][y] = ((state->board[x][y] == BOX_ON_GOAL)? GOAL : FLOOR)
#define REMOVE_PLAYER(x,y) \
  state->board[x][y] = ((state->board[x][y] == PLAYER_ON_GOAL)? GOAL : FLOOR)
#define ADD_BOX(x,y) \
  state->board[x][y] = ((state->board[x][y] == GOAL)? BOX_ON_GOAL : BOX)
#define ADD_PLAYER(x,y) \
  state->board[x][y] = ((state->board[x][y] == GOAL)? PLAYER_ON_GOAL : PLAYER)

typedef struct {
    char *moves;
    size_t size;
    size_t capacity;
} MoveHistory;

typedef struct {
    char board[MAX_ROWS][MAX_COLS];
    int rows, cols;
    int player_row, player_col;
    MoveHistory history;
} GameState;

void init_move_history(MoveHistory *history) {
    history->capacity = 10;
    history->size = 0;
    history->moves = (char *)malloc(history->capacity * sizeof(char));
}

void add_move(MoveHistory *history, char move) {
    if (history->size >= history->capacity) {
        history->capacity *= 2;
        history->moves = (char *)realloc(history->moves, history->capacity * sizeof(char));
    }
    history->moves[history->size++] = move;
}

void pop_move(MoveHistory *history) {
    if (history->size > 0) {
        history->size--;
    }
}

void clear_move_history(MoveHistory *history) {
    free(history->moves);
    history->moves = NULL;
    history->size = 0;
    history->capacity = 0;
}

void clear_screen() {
#ifdef _WIN32
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hStdOut, &csbi);
    DWORD count;
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    COORD homeCoords = { 0, 0 };
    FillConsoleOutputCharacter(hStdOut, ' ', cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hStdOut, homeCoords);
#else
    printf("\033[H\033[J");
#endif
}

void print_board(GameState *state) {
    clear_screen();
    for (int i = 0; i < state->rows; i++) {
        for (int j = 0; j < state->cols; j++) {
            switch (state->board[i][j]) {
                case WALL:
                    printf("%s%c%s", COLOR_WALL, WALL, COLOR_RESET);
                    break;
                case PLAYER:
                    printf("%s%c%s", COLOR_PLAYER, PLAYER, COLOR_RESET);
                    break;
                case BOX:
                    printf("%s%c%s", COLOR_BOX, BOX, COLOR_RESET);
                    break;
                case GOAL:
                    printf("%s%c%s", COLOR_GOAL, GOAL, COLOR_RESET);
                    break;
                case PLAYER_ON_GOAL:
                    printf("%s%c%s", COLOR_PLAYER_ON_GOAL, PLAYER_ON_GOAL, COLOR_RESET);
                    break;
                case BOX_ON_GOAL:
                    printf("%s%c%s", COLOR_BOX_ON_GOAL, BOX_ON_GOAL, COLOR_RESET);
                    break;
                default:
                    printf("%c", state->board[i][j]);
                    break;
            }
        }
        printf("\n");
    }
}

bool is_game_won(GameState *state) {
    for (int i = 0; i < state->rows; i++) {
        for (int j = 0; j < state->cols; j++) {
            if (state->board[i][j] == GOAL || state->board[i][j] == PLAYER_ON_GOAL) {
                return false;
            }
        }
    }
    return true;
}

/* returns 1 if the move took place, 0 otherwise */
unsigned int move_player(GameState *state, int dr, int dc) {
    int new_row = state->player_row + dr;
    int new_col = state->player_col + dc;
    if (IS_WALL(new_row, new_col)) {
        return 0; // player move is blocked
    }
    bool box_pushed = false;
    if (IS_BOX(new_row, new_col)) {
        int box_row = new_row + dr;
        int box_col = new_col + dc;
        if (IS_WALL(box_row, box_col) || IS_BOX(box_row, box_col)) {
            return 0; // box push is blocked
        }
        ADD_BOX(box_row, box_col);
        box_pushed = true;
    }
    REMOVE_PLAYER(state->player_row, state->player_col);

    if (state->board[new_row][new_col] == GOAL || state->board[new_row][new_col] == BOX_ON_GOAL) {
        state->board[new_row][new_col] = PLAYER_ON_GOAL;
    } else if (state->board[new_row][new_col] == FLOOR || state->board[new_row][new_col] == BOX) {
        state->board[new_row][new_col] = PLAYER;
    }

    state->player_row = new_row;
    state->player_col = new_col;
    // Record the move, using uppercase if a box was pushed
    if (box_pushed) {
        if (dr == -1) add_move(&state->history, 'U');
        else if (dr == 1) add_move(&state->history, 'D');
        else if (dc == -1) add_move(&state->history, 'L');
        else if (dc == 1) add_move(&state->history, 'R');
    } else {
        if (dr == -1) add_move(&state->history, 'u');
        else if (dr == 1) add_move(&state->history, 'd');
        else if (dc == -1) add_move(&state->history, 'l');
        else if (dc == 1) add_move(&state->history, 'r');
    }
    if (box_pushed && is_game_won(state)) {
        print_board(state);
        printf("Congratulations! You won!\n");
        printf("Total steps taken: %zu\n", state->history.size);
        printf("Move history: %.*s\n", (int)state->history.size, state->history.moves);
        exit(EXIT_SUCCESS);
    }
    return 1;
}

void undo_move(GameState *state) {
    if (state->history.size == 0) {
        return;
    }

    char last_move = state->history.moves[state->history.size - 1];
    pop_move(&state->history);
    // Reverse the last move
    int dr = 0, dc = 0;
    bool box_pushed = isupper(last_move);
    last_move = tolower(last_move);
    switch (last_move) {
        case 'u': dr = 1; break;
        case 'd': dr = -1; break;
        case 'l': dc = 1; break;
        case 'r': dc = -1; break;
    }
    int new_row = state->player_row + dr;
    int new_col = state->player_col + dc;

    if (state->board[new_row][new_col] == WALL) {
        return;
    }
    REMOVE_PLAYER(state->player_row, state->player_col);

    if (box_pushed) {
        int box_row = state->player_row - dr;
        int box_col = state->player_col - dc;
        REMOVE_BOX(box_row, box_col);
        ADD_BOX(state->player_row, state->player_col);
    }
    ADD_PLAYER(new_row, new_col);
    state->player_row = new_row;
    state->player_col = new_col;
}

void load_level(GameState *state, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    state->rows = 0;
    state->cols = 0;
    char line[MAX_COLS + 2]; // +2 for newline and null terminator
    while (fgets(line, sizeof(line), file)) {
        int j = 0;
        while (line[j] != '\n' && line[j] != '\0') {
            switch(line[j]) {
              case PLAYER:
              case PLAYER_ON_GOAL:
                state->player_row = state->rows;
                state->player_col = j;

              case WALL:
              case GOAL:
              case BOX:
              case BOX_ON_GOAL:
                state->board[state->rows][j] = line[j];
                break;

              default:
                state->board[state->rows][j] = FLOOR;
            }
            j++;
        }
        if (state->cols < j) state->cols = j;

        for(; j < MAX_COLS; j++) {
            state->board[state->rows][j] = ' '; // initialize the remainder
        }
        state->rows++;
    }
    fclose(file);
}

#ifdef _WIN32
int getch_noblock() {
    if (_kbhit()) {
        return _getch();
    }
    return -1;
}
#endif

void enable_raw_mode() {
#ifndef _WIN32
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

void disable_raw_mode() {
#ifndef _WIN32
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <level_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    GameState state;
    init_move_history(&state.history);
    load_level(&state, argv[1]);

#ifdef _WIN32
    // Enable Windows virtual terminal processing
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#else
    enable_raw_mode();
#endif

    print_board(&state); // Initial draw

    while (true) {
        int input = getch_noblock();
        if (input == 'q') {
            break;
        }

        unsigned int updated = 0;
        switch (input) {
            case 'w':
            case 'k':
#ifdef _WIN32
            case 72: // Up arrow key code for Windows
#endif
                updated = move_player(&state, -1, 0);
                break;
            case 'a':
            case 'h':
#ifdef _WIN32
            case 75: // Left arrow key code for Windows
#endif
                updated = move_player(&state, 0, -1);
                break;
            case 's':
            case 'j':
#ifdef _WIN32
            case 80: // Down arrow key code for Windows
#endif
                updated = move_player(&state, 1, 0);
                break;
            case 'd':
            case 'l':
#ifdef _WIN32
            case 77: // Right arrow key code for Windows
#endif
                updated = move_player(&state, 0, 1);
                break;
            case 'r':
                clear_move_history(&state.history);
                load_level(&state, argv[1]);
                updated = 1;
                break;
            case 'u':
                undo_move(&state);
                updated = 1;
                break;
            default:
                continue;
        }
        if (updated) {
            print_board(&state); // Redraw only when there's a change
        }
    }

#ifndef _WIN32
    disable_raw_mode();
#endif

    printf("Total steps taken: %zu\n", state.history.size);
    printf("Move history: %.*s\n", (int)state.history.size, state.history.moves);
    clear_move_history(&state.history);
    return EXIT_SUCCESS;
}
