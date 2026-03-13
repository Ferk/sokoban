#include "level_parser.h"
#include "sokoban.h"
#include <emscripten/emscripten.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Global game state object
GameState game_state;
static bool web_history_initialized = false;

static bool ensure_web_history_initialized(void) {
  if (!web_history_initialized) {
    init_move_history(&game_state.history);
    web_history_initialized = true;
  }

  return game_state.history.moves != NULL;
}

static bool load_web_level(const char *level_data, size_t level_index) {
  if (level_data == NULL || !ensure_web_history_initialized()) {
    return false;
  }
  if (!load_level_from_string_at_index(&game_state, level_data, level_index)) {
    return false;
  }

  remember_initial_state(&game_state);
  game_state.history.size = 0;
  return true;
}

void sokoban_reset_web(void) {
  reset_game(&game_state);
}

// Exported functions for JavaScript to call
EMSCRIPTEN_KEEPALIVE
void sokoban_init_web(const char *level_data) {
  if (!load_web_level(level_data, 0)) {
    fprintf(stderr, "Error parsing web level data.\n");
  }
}

EMSCRIPTEN_KEEPALIVE
bool sokoban_init_web_level(const char *level_data, int level_index) {
  if (level_index < 0) {
    return false;
  }
  return load_web_level(level_data, (size_t)level_index);
}

EMSCRIPTEN_KEEPALIVE
int sokoban_count_levels_web(const char *level_data) {
  size_t count = 0;

  if (!count_sok_levels_in_string(level_data, &count) || count > (size_t)INT_MAX) {
    return -1;
  }
  return (int)count;
}

EMSCRIPTEN_KEEPALIVE
bool sokoban_handle_input(char input) {
  int dr = 0, dc = 0;
  bool updated = false;

  switch (input) {
    case 'w':
    case 'k':
      dr = -1;
      dc = 0;
      break;
    case 'a':
    case 'h':
      dr = 0;
      dc = -1;
      break;
    case 's':
    case 'j':
      dr = 1;
      dc = 0;
      break;
    case 'd':
    case 'l':
      dr = 0;
      dc = 1;
      break;
    case 'u':
      undo_move(&game_state);
      updated = true;
      break;
    case 'r':
      sokoban_reset_web();
      updated = true;
      break;
  }

  if (!updated && (dr != 0 || dc != 0)) {
    updated = move_player(&game_state, dr, dc);
  }
  return updated;
}

EMSCRIPTEN_KEEPALIVE
bool sokoban_is_event_ongoing(void) {
  return (game_state.event.type != EVENT_NONE);
}

EMSCRIPTEN_KEEPALIVE
bool sokoban_process_event(void) {
  return process_event(&game_state);
}

EMSCRIPTEN_KEEPALIVE
int sokoban_get_rows(void) {
  return game_state.rows;
}

EMSCRIPTEN_KEEPALIVE
int sokoban_get_cols(void) {
  return game_state.cols;
}

EMSCRIPTEN_KEEPALIVE
char sokoban_get_tile(int row, int col) {
  return get_tile(&game_state, row, col);
}

EMSCRIPTEN_KEEPALIVE
bool sokoban_is_game_won(void) {
  return is_game_won(&game_state);
}
