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
static ParsedLevelInfo web_level_info = {0};
static char web_tap_path[MAX_ROWS * MAX_COLS + 1];

// Replaces the cached web metadata, taking ownership of the parsed strings.
static void set_web_level_info(ParsedLevelInfo *info) {
  free_parsed_level_info(&web_level_info);
  if (info != NULL) {
    web_level_info = *info;
    info->title = NULL;
    info->description = NULL;
    info->pack_metadata = NULL;
  }
}

// Lazily initializes move history storage for the singleton web game state.
static bool ensure_web_history_initialized(void) {
  if (!web_history_initialized) {
    init_move_history(&game_state.history);
    web_history_initialized = true;
  }

  return game_state.history.moves != NULL;
}

// Loads one level and its display metadata for the web frontend.
static bool load_web_level(const char *level_data, size_t level_index) {
  ParsedLevelInfo parsed_level_info = {0};

  if (level_data == NULL || !ensure_web_history_initialized()) {
    return false;
  }
  if (!load_level_from_string_at_index(&game_state, level_data, level_index)) {
    return false;
  }

  if (parse_sok_level_info_from_string(level_data, level_index, &parsed_level_info)) {
    set_web_level_info(&parsed_level_info);
  } else {
    set_web_level_info(NULL);
  }

  remember_initial_state(&game_state);
  game_state.history.size = 0;
  return true;
}

// Resets the current web level to its initial board state.
void sokoban_reset_web(void) {
  reset_game(&game_state);
}

// Exported functions for JavaScript to call
EMSCRIPTEN_KEEPALIVE
// Initializes the web frontend with the first level in a pack.
void sokoban_init_web(const char *level_data) {
  if (!load_web_level(level_data, 0)) {
    fprintf(stderr, "Error parsing web level data.\n");
  }
}

EMSCRIPTEN_KEEPALIVE
// Initializes the web frontend with a specific level from a pack.
bool sokoban_init_web_level(const char *level_data, int level_index) {
  if (level_index < 0) {
    return false;
  }
  return load_web_level(level_data, (size_t)level_index);
}

EMSCRIPTEN_KEEPALIVE
// Counts how many playable levels exist in the supplied pack text.
int sokoban_count_levels_web(const char *level_data) {
  size_t count = 0;

  if (!count_sok_levels_in_string(level_data, &count) || count > (size_t)INT_MAX) {
    return -1;
  }
  return (int)count;
}

EMSCRIPTEN_KEEPALIVE
// Returns the cached title for the currently loaded level.
const char *sokoban_get_level_title_web(void) {
  return (web_level_info.title != NULL) ? web_level_info.title : "";
}

EMSCRIPTEN_KEEPALIVE
// Returns the cached description for the currently loaded level.
const char *sokoban_get_level_description_web(void) {
  return (web_level_info.description != NULL) ? web_level_info.description : "";
}

EMSCRIPTEN_KEEPALIVE
// Returns the cached pack-level metadata for the current file.
const char *sokoban_get_pack_metadata_web(void) {
  return (web_level_info.pack_metadata != NULL) ? web_level_info.pack_metadata : "";
}

EMSCRIPTEN_KEEPALIVE
// Applies one web input command to the active game state.
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
    case 'u': {
      size_t previous_move_count = game_state.history.size;

      undo_move(&game_state);
      updated = (game_state.history.size != previous_move_count);
      break;
    }
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
// Plans the automated tap path or adjacent box push for a requested tile.
const char *sokoban_plan_tap_path_web(int row, int col) {
  size_t move_count = 0;

  web_tap_path[0] = '\0';
  if (!plan_player_action_to_tile(&game_state, row, col, web_tap_path, sizeof(web_tap_path), &move_count)) {
    return "";
  }
  return web_tap_path;
}

EMSCRIPTEN_KEEPALIVE
// Reports whether an animated board event is still in progress.
bool sokoban_is_event_ongoing(void) {
  return (game_state.event.type != EVENT_NONE);
}

EMSCRIPTEN_KEEPALIVE
// Advances the current animated board event by one step.
bool sokoban_process_event(void) {
  return process_event(&game_state);
}

EMSCRIPTEN_KEEPALIVE
// Returns the current board height.
int sokoban_get_rows(void) {
  return game_state.rows;
}

EMSCRIPTEN_KEEPALIVE
// Returns the current board width.
int sokoban_get_cols(void) {
  return game_state.cols;
}

EMSCRIPTEN_KEEPALIVE
// Returns the initial board height for the loaded level.
int sokoban_get_initial_rows_web(void) {
  return game_state.initial_state.rows;
}

EMSCRIPTEN_KEEPALIVE
// Returns the initial board width for the loaded level.
int sokoban_get_initial_cols_web(void) {
  return game_state.initial_state.cols;
}

EMSCRIPTEN_KEEPALIVE
// Returns the current number of recorded player moves in the loaded level.
int sokoban_get_move_count_web(void) {
  if (game_state.history.size > (size_t)INT_MAX) {
    return INT_MAX;
  }
  return (int)game_state.history.size;
}

EMSCRIPTEN_KEEPALIVE
// Returns one tile from the current board state.
char sokoban_get_tile(int row, int col) {
  return get_tile(&game_state, row, col);
}

EMSCRIPTEN_KEEPALIVE
// Returns one tile from the level's initial board state.
char sokoban_get_initial_tile_web(int row, int col) {
  if (row >= 0 && row < game_state.initial_state.rows && col >= 0 && col < game_state.initial_state.cols) {
    return game_state.initial_state.board[row][col];
  }
  return '\0';
}

EMSCRIPTEN_KEEPALIVE
// Reports whether the current level has been solved.
bool sokoban_is_game_won(void) {
  return is_game_won(&game_state);
}
