#ifndef SOKOBAN_H
#define SOKOBAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_ROWS 64
#define MAX_COLS 64

// Level elements
#define WALL '#'
#define FLOOR ' '
#define PLAYER '@'
#define BOX '$'
#define GOAL '.'
#define PLAYER_ON_GOAL '+'
#define BOX_ON_GOAL '*'
#define ICE '~'
#define PLAYER_ON_ICE '&'
#define BOX_ON_ICE '"'

typedef enum {
  EVENT_NONE = 0, // awaiting player input
  EVENT_ICE_MOVE, // elements sliding on ice
} GameEventType;

typedef struct {
  GameEventType type;
  int x; // position of the item receiving the event
  int y;
  int dr; // direction information, specific to event
  int dc;
} GameEvent;

typedef struct {
  char board[MAX_ROWS][MAX_COLS];
  int rows, cols;
  int player_row, player_col;
} LevelState;

typedef struct {
  char *moves;
  size_t size;
  size_t capacity;
} MoveHistory;

typedef struct {
  char board[MAX_ROWS][MAX_COLS];
  int rows, cols;
  int player_row, player_col;
  LevelState initial_state;
  MoveHistory history;
  GameEvent event;
} GameState;

void init_move_history(MoveHistory *history);
void clear_move_history(MoveHistory *history);
void remember_initial_state(GameState *state);

// Core game logic functions
bool load_level(GameState *state, FILE *file);
bool load_level_at_index(GameState *state, FILE *file, size_t level_index);
bool load_level_from_string(GameState *state, const char *level_data);
bool load_level_from_string_at_index(GameState *state, const char *level_data, size_t level_index);
bool plan_player_action_to_tile(GameState *state, int target_row, int target_col, char *moves, size_t moves_capacity, size_t *out_move_count);
void reset_game(GameState *state);
bool is_game_won(GameState *state);
bool move_player(GameState *state, int dr, int dc);
void undo_move(GameState *state);
bool process_event(GameState *state);
char get_tile(GameState *state, int row, int col);

#endif // SOKOBAN_H
