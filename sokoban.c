#include "sokoban.h"
#include "level_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many history items would be allocated each time */
#define HISTORY_ALLOCATION_INCREMENT 16
#define MAX_PATH_CELLS (MAX_ROWS * MAX_COLS)

#define IS_WALL(x, y) (x < 0 || x >= state->rows || y < 0 || y >= state->cols || state->board[x][y] == WALL)
#define IS_BOX(x, y) ((state->board[x][y] == BOX) || (state->board[x][y] == BOX_ON_GOAL) || (state->board[x][y] == BOX_ON_ICE))
#define IS_PLAYER(x, y) ((state->board[x][y] == PLAYER) || (state->board[x][y] == PLAYER_ON_GOAL) || (state->board[x][y] == PLAYER_ON_ICE))
#define IS_ICE(x, y) ((state->board[x][y] == ICE) || (state->board[x][y] == PLAYER_ON_ICE) || (state->board[x][y] == BOX_ON_ICE))

#define REMOVE_BOX(x, y) state->board[x][y] = ((state->board[x][y] == BOX_ON_GOAL) ? GOAL : ((state->board[x][y] == BOX_ON_ICE) ? ICE : FLOOR))
#define REMOVE_PLAYER(x, y) state->board[x][y] = ((state->board[x][y] == PLAYER_ON_GOAL) ? GOAL : ((state->board[x][y] == PLAYER_ON_ICE) ? ICE : FLOOR))

#define ADD_BOX(x, y) state->board[x][y] = ((state->board[x][y] == GOAL) ? BOX_ON_GOAL : ((state->board[x][y] == ICE) ? BOX_ON_ICE : BOX))
#define ADD_PLAYER(x, y) state->board[x][y] = ((state->board[x][y] == GOAL) ? PLAYER_ON_GOAL : ((state->board[x][y] == ICE) ? PLAYER_ON_ICE : PLAYER))

// Grows the move history buffer when a new move would exceed its capacity.
static bool ensure_history_capacity(MoveHistory *history, size_t needed_size) {
  if (needed_size <= history->capacity) {
    return true;
  }

  size_t new_capacity = history->capacity;
  while (new_capacity < needed_size) {
    new_capacity += HISTORY_ALLOCATION_INCREMENT;
  }

  char *new_moves = (char *)realloc(history->moves, new_capacity * sizeof(char));
  if (new_moves == NULL) {
    return false;
  }

  history->moves = new_moves;
  history->capacity = new_capacity;
  return true;
}

// Checks whether a tile is an empty non-sliding space that pathfinding may cross.
static bool is_path_floor_tile(char tile) {
  return tile == FLOOR || tile == GOAL;
}

// Checks whether a tile currently contains the player.
static bool is_player_tile(char tile) {
  return tile == PLAYER || tile == PLAYER_ON_GOAL || tile == PLAYER_ON_ICE;
}

// Appends one encoded move to the history buffer.
static void append_move(MoveHistory *history, char move) {
  history->moves[history->size++] = move;
}

// Encodes a movement delta as the web/frontend input character for that step.
static char encode_input_step(int dr, int dc) {
  if (dr == -1) return 'w';
  if (dr == 1) return 's';
  if (dc == -1) return 'a';
  if (dc == 1) return 'd';
  return '\0';
}

// Encodes a move direction and push flag into the compact history format.
static char encode_move(int dr, int dc, bool box_pushed) {
  if (dr == -1) return box_pushed ? 'U' : 'u';
  if (dr == 1) return box_pushed ? 'D' : 'd';
  if (dc == -1) return box_pushed ? 'L' : 'l';
  if (dc == 1) return box_pushed ? 'R' : 'r';
  return '\0';
}

// Decodes one stored move back into a row and column delta.
static bool decode_move(char move, int *dr, int *dc) {
  *dr = 0;
  *dc = 0;

  switch (tolower((unsigned char)move)) {
    case 'u':
      *dr = -1;
      return true;
    case 'd':
      *dr = 1;
      return true;
    case 'l':
      *dc = -1;
      return true;
    case 'r':
      *dc = 1;
      return true;
    default:
      return false;
  }
}

// Restores the board, player, and event state to the saved initial snapshot.
static void restore_initial_state(GameState *state) {
  memcpy(state->board, state->initial_state.board, sizeof(state->board));
  state->rows = state->initial_state.rows;
  state->cols = state->initial_state.cols;
  state->player_row = state->initial_state.player_row;
  state->player_col = state->initial_state.player_col;
  state->event.type = EVENT_NONE;
  state->event.x = 0;
  state->event.y = 0;
  state->event.dr = 0;
  state->event.dc = 0;
}

// Copies a parsed level into the active game state.
static void apply_level_state(GameState *state, const LevelState *level) {
  memcpy(state->board, level->board, sizeof(state->board));
  state->rows = level->rows;
  state->cols = level->cols;
  state->player_row = level->player_row;
  state->player_col = level->player_col;
  state->event.type = EVENT_NONE;
  state->event.x = 0;
  state->event.y = 0;
  state->event.dr = 0;
  state->event.dc = 0;
}

// Replays the recorded history from the initial state up to the requested move count.
static bool replay_history(GameState *state, size_t move_count) {
  restore_initial_state(state);
  state->history.size = 0;

  for (size_t i = 0; i < move_count; i++) {
    int dr = 0;
    int dc = 0;
    char move = state->history.moves[i];

    if (!decode_move(move, &dr, &dc)) {
      return false;
    }
    if (!move_player(state, dr, dc)) {
      return false;
    }
    while (state->event.type != EVENT_NONE) {
      process_event(state);
    }
  }
  return true;
}

// Initializes move history storage for a new game state.
void init_move_history(MoveHistory *history) {
  history->size = 0;
  history->capacity = HISTORY_ALLOCATION_INCREMENT;
  history->moves = (char *)malloc(history->capacity * sizeof(char));
  if (history->moves == NULL) {
    history->capacity = 0;
  }
}

// Releases any memory owned by the move history.
void clear_move_history(MoveHistory *history) {
  if (history->moves != NULL) {
    free(history->moves);
    history->moves = NULL;
  }
  history->size = 0;
  history->capacity = 0;
}

// Saves the current board as the reset point for the active level.
void remember_initial_state(GameState *state) {
  memcpy(state->initial_state.board, state->board, sizeof(state->board));
  state->initial_state.rows = state->rows;
  state->initial_state.cols = state->cols;
  state->initial_state.player_row = state->player_row;
  state->initial_state.player_col = state->player_col;
}

// Loads the first playable level from an open file.
bool load_level(GameState *state, FILE *file) {
  return load_level_at_index(state, file, 0);
}

// Loads a specific level from an open file into the active game state.
bool load_level_at_index(GameState *state, FILE *file, size_t level_index) {
  LevelState level;

  if (file == NULL) {
    return false;
  }
  if (!parse_sok_level_from_file(file, level_index, &level)) {
    return false;
  }

  apply_level_state(state, &level);
  return true;
}

// Loads the first playable level from an in-memory text buffer.
bool load_level_from_string(GameState *state, const char *level_data) {
  return load_level_from_string_at_index(state, level_data, 0);
}

// Loads a specific level from an in-memory text buffer.
bool load_level_from_string_at_index(GameState *state, const char *level_data, size_t level_index) {
  LevelState level;

  if (level_data == NULL) {
    return false;
  }
  if (!parse_sok_level_from_string(level_data, level_index, &level)) {
    return false;
  }

  apply_level_state(state, &level);
  return true;
}

// Plans a tap/click action to a tile using direct adjacent steps or a fixed-size BFS over empty ground tiles.
bool plan_player_action_to_tile(GameState *state, int target_row, int target_col, char *moves, size_t moves_capacity, size_t *out_move_count) {
  static const int row_steps[4] = {-1, 1, 0, 0};
  static const int col_steps[4] = {0, 0, -1, 1};
  bool visited[MAX_ROWS][MAX_COLS] = {{false}};
  signed char previous_row[MAX_ROWS][MAX_COLS];
  signed char previous_col[MAX_ROWS][MAX_COLS];
  char previous_move[MAX_ROWS][MAX_COLS];
  unsigned char queue_rows[MAX_PATH_CELLS];
  unsigned char queue_cols[MAX_PATH_CELLS];
  char target_tile = '\0';
  size_t queue_head = 0;
  size_t queue_tail = 0;
  size_t move_count = 0;
  int row = 0;
  int col = 0;

  if (state == NULL || moves == NULL || out_move_count == NULL || moves_capacity == 0) {
    return false;
  }

  moves[0] = '\0';
  *out_move_count = 0;

  if (target_row < 0 || target_row >= state->rows || target_col < 0 || target_col >= state->cols) {
    return false;
  }

  target_tile = state->board[target_row][target_col];
  if (is_player_tile(target_tile)) {
    return true;
  }

  {
    int dr = target_row - state->player_row;
    int dc = target_col - state->player_col;

    if ((dr == 0 && (dc == -1 || dc == 1)) || (dc == 0 && (dr == -1 || dr == 1))) {
      char move = encode_input_step(dr, dc);

      if (move != '\0' && moves_capacity >= 2) {
        moves[0] = move;
        moves[1] = '\0';
        *out_move_count = 1;
        return true;
      }
    }
  }

  if (!is_path_floor_tile(target_tile)) {
    return false;
  }

  for (row = 0; row < MAX_ROWS; row++) {
    for (col = 0; col < MAX_COLS; col++) {
      previous_row[row][col] = -1;
      previous_col[row][col] = -1;
      previous_move[row][col] = '\0';
    }
  }

  visited[state->player_row][state->player_col] = true;
  queue_rows[queue_tail] = (unsigned char)state->player_row;
  queue_cols[queue_tail] = (unsigned char)state->player_col;
  queue_tail++;

  while (queue_head < queue_tail && !visited[target_row][target_col]) {
    int current_row = (int)queue_rows[queue_head];
    int current_col = (int)queue_cols[queue_head];
    queue_head++;

    for (size_t direction = 0; direction < 4; direction++) {
      int next_row = current_row + row_steps[direction];
      int next_col = current_col + col_steps[direction];
      char next_tile = '\0';

      if (next_row < 0 || next_row >= state->rows || next_col < 0 || next_col >= state->cols) {
        continue;
      }
      if (visited[next_row][next_col]) {
        continue;
      }

      next_tile = state->board[next_row][next_col];
      if (!is_path_floor_tile(next_tile)) {
        continue;
      }

      visited[next_row][next_col] = true;
      previous_row[next_row][next_col] = (signed char)current_row;
      previous_col[next_row][next_col] = (signed char)current_col;
      previous_move[next_row][next_col] = encode_input_step(row_steps[direction], col_steps[direction]);
      queue_rows[queue_tail] = (unsigned char)next_row;
      queue_cols[queue_tail] = (unsigned char)next_col;
      queue_tail++;
    }
  }

  if (!visited[target_row][target_col]) {
    return false;
  }

  row = target_row;
  col = target_col;
  while (row != state->player_row || col != state->player_col) {
    int prior_row = (int)previous_row[row][col];
    int prior_col = (int)previous_col[row][col];

    if (prior_row < 0 || prior_col < 0 || move_count + 1 >= moves_capacity) {
      moves[0] = '\0';
      *out_move_count = 0;
      return false;
    }

    moves[move_count++] = previous_move[row][col];
    row = prior_row;
    col = prior_col;
  }

  for (size_t i = 0; i < move_count / 2; i++) {
    char swap = moves[i];

    moves[i] = moves[move_count - 1 - i];
    moves[move_count - 1 - i] = swap;
  }

  moves[move_count] = '\0';
  *out_move_count = move_count;
  return true;
}

// Resets the active game back to its remembered initial state.
void reset_game(GameState *state) {
  restore_initial_state(state);
  state->history.size = 0;
}

// Checks whether every goal on the board has been satisfied.
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

// Advances the current sliding event for a player or box on ice.
bool move_on_ice(GameState *state) {
  bool redraw = false;

  int new_x = state->event.x + state->event.dr;
  int new_y = state->event.y + state->event.dc;

  if (IS_WALL(new_x, new_y)) {
    // hit a wall! stop
    state->event.type = EVENT_NONE;
  } else if (IS_BOX(new_x, new_y)) {
    // hit a box! event transfers to the box for next tic
    // (it may chain if there's a stack of boxes!)
    state->event.x = new_x;
    state->event.y = new_y;
    new_x = state->event.x + state->event.dr;
    new_y = state->event.y + state->event.dc;
  } else {
    // Move the player or the box
    if (IS_PLAYER(state->event.x, state->event.y)) {
      REMOVE_PLAYER(state->event.x, state->event.y);
      ADD_PLAYER(new_x, new_y);

      state->player_row = new_x;
      state->player_col = new_y;
    } else if (IS_BOX(state->event.x, state->event.y)) {
      REMOVE_BOX(state->event.x, state->event.y);
      ADD_BOX(new_x, new_y);
    }
    redraw = true;

    if (!IS_ICE(new_x, new_y)) {
      // no longer slippery
      state->event.type = EVENT_NONE;
    } else {
      // update position for next event
      state->event.x = new_x;
      state->event.y = new_y;
    }
  }
  return redraw;
}

// Attempts to move the player and starts any resulting ice event.
bool move_player(GameState *state, int dr, int dc) {
  int new_row = state->player_row + dr;
  int new_col = state->player_col + dc;
  int box_row = 0;
  int box_col = 0;
  bool box_pushed = false;
  char move = '\0';

  if (IS_WALL(new_row, new_col)) {
    return false;
  }

  if (IS_BOX(new_row, new_col)) {
    box_row = new_row + dr;
    box_col = new_col + dc;
    if (IS_WALL(box_row, box_col) || IS_BOX(box_row, box_col)) {
      return false;
    }
    box_pushed = true;
  }

  move = encode_move(dr, dc, box_pushed);
  if (move == '\0') {
    return false;
  }
  if (!ensure_history_capacity(&state->history, state->history.size + 1)) {
    return false;
  }

  if (box_pushed) {
    REMOVE_BOX(new_row, new_col);
    ADD_BOX(box_row, box_col);
    if (state->board[box_row][box_col] == BOX_ON_ICE) {
      state->event.type = EVENT_ICE_MOVE;
      state->event.x = box_row;
      state->event.y = box_col;
      state->event.dr = dr;
      state->event.dc = dc;
      move_on_ice(state);
    }
  }

  REMOVE_PLAYER(state->player_row, state->player_col);
  ADD_PLAYER(new_row, new_col);
  state->player_row = new_row;
  state->player_col = new_col;

  if (state->board[new_row][new_col] == PLAYER_ON_ICE) {
    state->event.type = EVENT_ICE_MOVE;
    state->event.x = new_row;
    state->event.y = new_col;
    state->event.dr = dr;
    state->event.dc = dc;
    move_on_ice(state);
  }

  append_move(&state->history, move);
  return true;
}

// Rewinds the board by replaying history up to the previous move.
void undo_move(GameState *state) {
  if (state->history.size == 0) {
    return;
  }

  if (!replay_history(state, state->history.size - 1)) {
    reset_game(state);
  }
}

// Advances the active board event by one game tick.
bool process_event(GameState *state) {

  bool redraw = false;
  switch (state->event.type) {
    case EVENT_ICE_MOVE:
      redraw = move_on_ice(state);
      break;
    case EVENT_NONE:
      break;
    default:
      perror("Unknown event type");
      state->event.type = EVENT_NONE;
  }
  return redraw;
}

// Returns the tile at the requested board position, or '\0' if out of bounds.
char get_tile(GameState *state, int row, int col) {
  if (row >= 0 && row < state->rows && col >= 0 && col < state->cols) {
    return state->board[row][col];
  }
  return '\0';
}
