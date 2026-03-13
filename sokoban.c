#include "sokoban.h"
#include "level_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many history items would be allocated each time */
#define HISTORY_ALLOCATION_INCREMENT 16

#define IS_WALL(x, y) (x < 0 || x >= state->rows || y < 0 || y >= state->cols || state->board[x][y] == WALL)
#define IS_BOX(x, y) ((state->board[x][y] == BOX) || (state->board[x][y] == BOX_ON_GOAL) || (state->board[x][y] == BOX_ON_ICE))
#define IS_PLAYER(x, y) ((state->board[x][y] == PLAYER) || (state->board[x][y] == PLAYER_ON_GOAL) || (state->board[x][y] == PLAYER_ON_ICE))
#define IS_ICE(x, y) ((state->board[x][y] == ICE) || (state->board[x][y] == PLAYER_ON_ICE) || (state->board[x][y] == BOX_ON_ICE))

#define REMOVE_BOX(x, y) state->board[x][y] = ((state->board[x][y] == BOX_ON_GOAL) ? GOAL : ((state->board[x][y] == BOX_ON_ICE) ? ICE : FLOOR))
#define REMOVE_PLAYER(x, y) state->board[x][y] = ((state->board[x][y] == PLAYER_ON_GOAL) ? GOAL : ((state->board[x][y] == PLAYER_ON_ICE) ? ICE : FLOOR))

#define ADD_BOX(x, y) state->board[x][y] = ((state->board[x][y] == GOAL) ? BOX_ON_GOAL : ((state->board[x][y] == ICE) ? BOX_ON_ICE : BOX))
#define ADD_PLAYER(x, y) state->board[x][y] = ((state->board[x][y] == GOAL) ? PLAYER_ON_GOAL : ((state->board[x][y] == ICE) ? PLAYER_ON_ICE : PLAYER))

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

static void append_move(MoveHistory *history, char move) {
  history->moves[history->size++] = move;
}

static char encode_move(int dr, int dc, bool box_pushed) {
  if (dr == -1) return box_pushed ? 'U' : 'u';
  if (dr == 1) return box_pushed ? 'D' : 'd';
  if (dc == -1) return box_pushed ? 'L' : 'l';
  if (dc == 1) return box_pushed ? 'R' : 'r';
  return '\0';
}

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

void init_move_history(MoveHistory *history) {
  history->size = 0;
  history->capacity = HISTORY_ALLOCATION_INCREMENT;
  history->moves = (char *)malloc(history->capacity * sizeof(char));
  if (history->moves == NULL) {
    history->capacity = 0;
  }
}

void clear_move_history(MoveHistory *history) {
  if (history->moves != NULL) {
    free(history->moves);
    history->moves = NULL;
  }
  history->size = 0;
  history->capacity = 0;
}

void remember_initial_state(GameState *state) {
  memcpy(state->initial_state.board, state->board, sizeof(state->board));
  state->initial_state.rows = state->rows;
  state->initial_state.cols = state->cols;
  state->initial_state.player_row = state->player_row;
  state->initial_state.player_col = state->player_col;
}

bool load_level(GameState *state, FILE *file) {
  return load_level_at_index(state, file, 0);
}

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

bool load_level_from_string(GameState *state, const char *level_data) {
  return load_level_from_string_at_index(state, level_data, 0);
}

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

void reset_game(GameState *state) {
  restore_initial_state(state);
  state->history.size = 0;
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

void undo_move(GameState *state) {
  if (state->history.size == 0) {
    return;
  }

  if (!replay_history(state, state->history.size - 1)) {
    reset_game(state);
  }
}

/* Advance a game tic forwards */
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

char get_tile(GameState *state, int row, int col) {
  if (row >= 0 && row < state->rows && col >= 0 && col < state->cols) {
    return state->board[row][col];
  }
  return '\0';
}
