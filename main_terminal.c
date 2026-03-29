#include "level_parser.h"
#include "sokoban.h"
#include "vendor/argtable3.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

int nanosleep(const struct timespec *req, struct timespec *rem);
#endif

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ANSI color codes
#define COLOR_RESET "\033[0m"
#define COLOR_WALL "\033[44;94m"           // Bright blue foreground
#define COLOR_PLAYER "\033[96m"            // Bright cyan foreground
#define COLOR_BOX "\033[93m"               // Bright yellow foreground
#define COLOR_GOAL "\033[43m"              // Dark yellow background
#define COLOR_PLAYER_ON_GOAL "\033[43;96m" // Dark yellow background, bright cyan foreground
#define COLOR_BOX_ON_GOAL "\033[43;93m"    // Dark yellow background, yellow foreground
#define COLOR_ICE "\033[46;97m"            // Dark cyan background, dark white foreground
#define COLOR_PLAYER_ON_ICE "\033[46;96m"  // Dark cyan background, bright cyan foreground
#define COLOR_BOX_ON_ICE "\033[46;93m"     // Dark cyan background, bright yellow foreground
#define COLOR_KEY "\033[33m"               // Dark yellow foreground
#define COLOR_KEY_ON_ICE "\033[46;33m"     // Dark cyan background, dark yellow foreground
#define COLOR_SLIDER "\033[95m"            // Bright magenta foreground
#define COLOR_SLIDER_ON_ICE "\033[46;95m"  // Dark cyan background, bright magenta foreground
#define COLOR_SLIDER_ON_GOAL "\033[43;95m" // Dark yellow background, bright magenta foreground
#define COLOR_LOCK "\033[1;30;43m"         // Grey foreground, Dark yellow background
#define COLOR_TITLE "\033[96m"             // Bright cyan foreground
#define COLOR_STATUS "\033[93m"            // Bright yellow foreground
#define DISPLAY_KEY_ON_GOAL "\xE1\xB8\xB3"
#define DISPLAY_KEY_ON_ICE "\xE1\xB8\xB5"
#define DISPLAY_SLIDER_ON_GOAL "\xE1\xB9\xBE"
#define DISPLAY_SLIDER_ON_ICE "\xE1\xB9\xBC"

// milliseconds to wait before running a "tic" during game events
#define TIC_DURATION_MS 100

typedef struct {
  size_t *levels;
  size_t count;
  size_t capacity;
} LevelSequence;

typedef struct {
  bool enabled;
  int drawn_rows;
} BoardRenderState;

// Initializes an empty list of selected level indexes.
static void init_level_sequence(LevelSequence *sequence) {
  sequence->levels = NULL;
  sequence->count = 0;
  sequence->capacity = 0;
}

// Releases memory owned by a level selection list.
static void clear_level_sequence(LevelSequence *sequence) {
  free(sequence->levels);
  sequence->levels = NULL;
  sequence->count = 0;
  sequence->capacity = 0;
}

// Appends one zero-based level index to the selection list.
static bool append_level(LevelSequence *sequence, size_t level_index) {
  size_t new_capacity = 0;
  size_t *new_levels = NULL;

  if (sequence->count < sequence->capacity) {
    sequence->levels[sequence->count++] = level_index;
    return true;
  }

  new_capacity = (sequence->capacity == 0) ? 8 : sequence->capacity * 2;
  new_levels = (size_t *)realloc(sequence->levels, new_capacity * sizeof(*new_levels));
  if (new_levels == NULL) {
    return false;
  }

  sequence->levels = new_levels;
  sequence->capacity = new_capacity;
  sequence->levels[sequence->count++] = level_index;
  return true;
}

// Advances past whitespace in a level selection string.
static void skip_level_spec_spaces(const char **cursor) {
  while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
    (*cursor)++;
  }
}

// Parses one 1-based level number from a level selection string.
static bool parse_level_number(const char **cursor, size_t *out_number, char *error, size_t error_size) {
  char *end = NULL;
  unsigned long long value = 0;

  if (!isdigit((unsigned char)**cursor)) {
    snprintf(error, error_size, "expected a level number near '%s'", *cursor);
    return false;
  }

  errno = 0;
  value = strtoull(*cursor, &end, 10);
  if (errno == ERANGE) {
    snprintf(error, error_size, "level number is too large");
    return false;
  }
  if (value == 0) {
    snprintf(error, error_size, "level numbers are 1-based");
    return false;
  }

  *cursor = end;
  *out_number = (size_t)value;
  return true;
}

// Expands an inclusive 1-based level range into the selection list.
static bool append_level_range(LevelSequence *sequence, size_t first_level, size_t last_level) {
  for (size_t level = first_level; level <= last_level; level++) {
    if (!append_level(sequence, level - 1)) {
      return false;
    }
  }
  return true;
}

// Parses a --level selector such as "1,3-5,8-".
static bool parse_level_spec(const char *spec, size_t total_levels, LevelSequence *sequence, char *error, size_t error_size) {
  const char *cursor = spec;
  bool expecting_token = true;

  if (spec == NULL || spec[0] == '\0') {
    snprintf(error, error_size, "level selector cannot be empty");
    return false;
  }

  while (true) {
    size_t first_level = 0;
    size_t last_level = 0;

    skip_level_spec_spaces(&cursor);
    if (*cursor == '\0') {
      if (expecting_token) {
        snprintf(error, error_size, "incomplete level selector");
        return false;
      }
      return true;
    }

    if (!parse_level_number(&cursor, &first_level, error, error_size)) {
      return false;
    }

    skip_level_spec_spaces(&cursor);
    last_level = first_level;
    if (*cursor == '-') {
      cursor++;
      skip_level_spec_spaces(&cursor);

      if (*cursor == '\0' || *cursor == ',') {
        last_level = total_levels;
      } else if (!parse_level_number(&cursor, &last_level, error, error_size)) {
        return false;
      }
    }

    if (first_level > total_levels) {
      snprintf(error, error_size, "level %zu is outside this file (only %zu levels available)", first_level, total_levels);
      return false;
    }
    if (last_level > total_levels) {
      snprintf(error, error_size, "level %zu is outside this file (only %zu levels available)", last_level, total_levels);
      return false;
    }
    if (last_level < first_level) {
      snprintf(error, error_size, "invalid descending range %zu-%zu", first_level, last_level);
      return false;
    }

    if (!append_level_range(sequence, first_level, last_level)) {
      snprintf(error, error_size, "out of memory while storing selected levels");
      return false;
    }

    skip_level_spec_spaces(&cursor);
    if (*cursor == '\0') {
      return true;
    }
    if (*cursor != ',') {
      snprintf(error, error_size, "unexpected character '%c' in level selector", *cursor);
      return false;
    }

    cursor++;
    expecting_token = true;
  }
}

// Counts playable levels in a pack file and formats parse errors for the CLI.
static bool count_levels_in_path(const char *level_path, size_t *out_total_levels, char *error, size_t error_size) {
  FILE *file = fopen(level_path, "r");
  bool counted = false;

  if (file == NULL) {
    snprintf(error, error_size, "could not open '%s': %s", level_path, strerror(errno));
    return false;
  }

  counted = count_sok_levels_in_file(file, out_total_levels);
  fclose(file);
  if (!counted) {
    snprintf(error, error_size, "could not parse '%s' while counting levels", level_path);
    return false;
  }
  if (*out_total_levels == 0) {
    snprintf(error, error_size, "no playable levels were found in '%s'", level_path);
    return false;
  }

  return true;
}

// Builds the full ordered list of levels the terminal frontend should play.
static bool build_level_sequence(const char *level_path, const arg_str_t *level_specs, LevelSequence *sequence, char *error, size_t error_size) {
  char detail[256];
  size_t total_levels = 0;

  if (!count_levels_in_path(level_path, &total_levels, error, error_size)) {
    return false;
  }

  if (level_specs->count == 0) {
    if (!append_level_range(sequence, 1, total_levels)) {
      snprintf(error, error_size, "out of memory while storing selected levels");
      return false;
    }
    return true;
  }

  for (int i = 0; i < level_specs->count; i++) {
    if (!parse_level_spec(level_specs->sval[i], total_levels, sequence, detail, sizeof(detail))) {
      snprintf(error, error_size, "invalid --level '%s': %s", level_specs->sval[i], detail);
      return false;
    }
  }

  return true;
}

// Prints command-line usage information for the terminal frontend.
static void print_help(const char *program_name) {
  printf("Sokoban terminal frontend\n\n");
  printf("Usage:\n");
  printf("  %s [-h] [--noredraw] [-l <spec>]... <level-file>\n\n", program_name);

  printf("Options:\n");
  printf("  -h, --help                show this help and exit\n");
  printf("  -l, --level <spec>        play only selected 1-based levels; accepts N, A-B,\n");
  printf("                            A-, comma lists, and repeated flags\n");
  printf("  --noredraw                append each board state instead of redrawing in place\n");
  printf("  <level-file>              path to a .sok pack or single-level file\n");

  printf("\nExamples:\n");
  printf("  %s level.sok\n", program_name);
  printf("  %s --level 2-5 easy.sok\n", program_name);
  printf("  %s -l 1,4,10-12,15 easy.sok\n", program_name);
  printf("  %s -l 5- easy.sok\n", program_name);
  printf("  %s -l 1-3 -l 8 easy.sok\n", program_name);
  printf("  %s --noredraw easy.sok\n", program_name);

  printf("\nControls:\n");
  printf("  WASD / HJKL / arrow keys  move\n");
  printf("  u                         undo current level\n");
  printf("  r                         reset current level\n");
  printf("  q                         quit\n");
}

// Clears the remembered redraw state for the next board render.
static void reset_board_render_state(BoardRenderState *render_state) {
  render_state->drawn_rows = 0;
}

// Prints pack metadata and level text shown before a board starts.
static void print_level_info(const ParsedLevelInfo *info, size_t level_index) {
  bool printed_anything = false;

  if (info == NULL) {
    return;
  }

  if (level_index == 0 && info->pack_metadata != NULL && info->pack_metadata[0] != '\0') {
    printf("%s", info->pack_metadata);
    printed_anything = true;
  }

  if (info->title != NULL && info->title[0] != '\0') {
    if (printed_anything) {
      printf("\n\n");
    }
    printf("%s%s%s", COLOR_TITLE, info->title, COLOR_RESET);
    printed_anything = true;
  }

  if (info->description != NULL && info->description[0] != '\0') {
    if (printed_anything) {
      printf("\n");
    }
    printf("%s", info->description);
    printed_anything = true;
  }

  if (printed_anything) {
    printf("\n\n");
  }
}

// Clears the terminal screen on the current platform.
void clear_screen(void) {
#ifdef _WIN32
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hStdOut, &csbi);
  DWORD count;
  DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
  COORD homeCoords = {0, 0};
  FillConsoleOutputCharacter(hStdOut, ' ', cellCount, homeCoords, &count);
  SetConsoleCursorPosition(hStdOut, homeCoords);
#else
  printf("\033[H\033[J");
#endif
}

// Sleeps for a short number of milliseconds.
void delay(int milliseconds) {
#ifdef _WIN32
  Sleep(milliseconds);
#else
  nanosleep(&(struct timespec){.tv_sec = milliseconds / 1000, .tv_nsec = (milliseconds % 1000) * 1000000}, NULL);
#endif
}

// Draws the current board, optionally reusing the previous screen region.
void print_board(GameState *state, BoardRenderState *render_state) {
  bool redrawing = render_state->enabled && render_state->drawn_rows > 0;

  if (redrawing) {
    printf("\033[%dA", render_state->drawn_rows);
  }

  for (int i = 0; i < state->rows; i++) {
    if (redrawing) {
      printf("\r\033[2K");
    }
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
        case ICE:
          printf("%s%c%s", COLOR_ICE, ICE, COLOR_RESET);
          break;
        case PLAYER_ON_ICE:
          printf("%s%c%s", COLOR_PLAYER_ON_ICE, PLAYER_ON_ICE, COLOR_RESET);
          break;
        case BOX_ON_ICE:
          printf("%s%c%s", COLOR_BOX_ON_ICE, BOX_ON_ICE, COLOR_RESET);
          break;
        case KEY:
          printf("%s%c%s", COLOR_KEY, KEY, COLOR_RESET);
          break;
        case KEY_ON_GOAL:
          printf("%s%s%s", COLOR_GOAL, DISPLAY_KEY_ON_GOAL, COLOR_RESET);
          break;
        case KEY_ON_ICE:
          printf("%s%s%s", COLOR_KEY_ON_ICE, DISPLAY_KEY_ON_ICE, COLOR_RESET);
          break;
        case SLIDER:
          printf("%s%c%s", COLOR_SLIDER, SLIDER, COLOR_RESET);
          break;
        case SLIDER_ON_GOAL:
          printf("%s%s%s", COLOR_SLIDER_ON_GOAL, DISPLAY_SLIDER_ON_GOAL, COLOR_RESET);
          break;
        case SLIDER_ON_ICE:
          printf("%s%s%s", COLOR_SLIDER_ON_ICE, DISPLAY_SLIDER_ON_ICE, COLOR_RESET);
          break;
        case LOCK:
          printf("%s%c%s", COLOR_LOCK, LOCK, COLOR_RESET);
          break;
        default:
          printf("%c", state->board[i][j]);
          break;
      }
    }
    printf("\n");
  }

  fflush(stdout);
  render_state->drawn_rows = state->rows;
}

// Polls the keyboard for one input character without blocking.
int getch_noblock(void) {
#ifdef _WIN32
  if (_kbhit()) {
    return _getch();
  }
  return -1;
#else
  struct termios oldt, newt;
  int ch;
  int oldf;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  // Handle arrow keys (ESC [ A, ESC [ B, etc.)
  if (ch == 27) { // ESC
    // Peek at the next two characters
    int next1 = getchar();
    int next2 = getchar();
    if (next1 == 91) { // [
      switch (next2) {
        case 65: // Up
          return 'w';
        case 66: // Down
          return 's';
        case 67: // Right
          return 'd';
        case 68: // Left
          return 'a';
      }
    }
  }
  return ch;
#endif
}

// Enables raw terminal input on POSIX platforms.
void enable_raw_mode(void) {
#ifndef _WIN32
  struct termios raw;
  tcgetattr(STDIN_FILENO, &raw);
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

// Restores canonical terminal input on POSIX platforms.
void disable_raw_mode(void) {
#ifndef _WIN32
  struct termios raw;
  tcgetattr(STDIN_FILENO, &raw);
  raw.c_lflag |= (ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

// Loads one selected level and its display metadata for terminal play.
static bool load_terminal_level(GameState *state, const char *level_path, size_t level_index) {
  FILE *file = fopen(level_path, "r");
  ParsedLevelInfo level_info = {0};
  bool loaded = false;

  if (!file) {
    perror("Error opening file");
    return false;
  }

  if (!parse_sok_level_info_from_file(file, level_index, &level_info)) {
    fclose(file);
    return false;
  }

  print_level_info(&level_info, level_index);
  free_parsed_level_info(&level_info);

  rewind(file);
  loaded = load_level_at_index(state, file, level_index);
  fclose(file);
  if (!loaded) {
    return false;
  }

  remember_initial_state(state);
  state->history.size = 0;
  return true;
}

// Runs the terminal Sokoban frontend.
int main(int argc, char *argv[]) {
  arg_lit_t *help = arg_lit0("h", "help", "show this help and exit");
  arg_str_t *level_option = arg_strn("l", "level", "<spec>", 0, 64, "play only selected 1-based levels; accepts N, A-B, A-, comma lists, and repeated flags");
  arg_lit_t *noredraw = arg_lit0(NULL, "noredraw", "append each board state instead of redrawing in place");
  arg_file_t *level_file = arg_file1(NULL, NULL, "<level-file>", "path to a .sok pack or single-level file");
  arg_end_t *end = arg_end(20);
  void *argtable[] = {help, level_option, noredraw, level_file, end};
  BoardRenderState render_state;
  LevelSequence level_sequence;
  GameState state;
  char level_error[256];
  int exit_code = EXIT_FAILURE;
  int nerrors = 0;
  size_t current_level_index = 0;
  size_t current_sequence_index = 0;
  bool custom_selection = false;
  bool history_initialized = false;
#ifndef _WIN32
  bool raw_mode_enabled = false;
#endif

  init_level_sequence(&level_sequence);
  render_state.enabled = false;
  reset_board_render_state(&render_state);
  nerrors = arg_parse(argc, argv, argtable);

  if (help->count > 0) {
    print_help(argv[0]);
    exit_code = EXIT_SUCCESS;
    goto cleanup;
  }

  if (nerrors > 0) {
    arg_print_errors(stderr, end, argv[0]);
    fprintf(stderr, "\nTry '%s --help' for more information.\n", argv[0]);
    goto cleanup;
  }

  custom_selection = (level_option->count > 0);
  render_state.enabled = (noredraw->count == 0);
  if (!build_level_sequence(level_file->filename[0], level_option, &level_sequence, level_error, sizeof(level_error))) {
    fprintf(stderr, "%s\n", level_error);
    fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    goto cleanup;
  }

  init_move_history(&state.history);
  history_initialized = true;

  current_level_index = level_sequence.levels[current_sequence_index];
  if (!load_terminal_level(&state, level_file->filename[0], current_level_index)) {
    fprintf(stderr, "Error parsing level file: %s\n", level_file->filename[0]);
    goto cleanup;
  }

#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  GetConsoleMode(hOut, &dwMode);
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, dwMode);
#else
  enable_raw_mode();
  raw_mode_enabled = true;
#endif

  print_board(&state, &render_state);

  while (true) {
    int input = getch_noblock();
    if (input == 'q') {
      break;
    }

    bool updated = false;
    switch (input) {
      case 'w':
      case 'k':
#ifdef _WIN32
      case 72:
#endif
        updated = move_player(&state, -1, 0);
        break;
      case 'a':
      case 'h':
#ifdef _WIN32
      case 75:
#endif
        updated = move_player(&state, 0, -1);
        break;
      case 's':
      case 'j':
#ifdef _WIN32
      case 80:
#endif
        updated = move_player(&state, 1, 0);
        break;
      case 'd':
      case 'l':
#ifdef _WIN32
      case 77:
#endif
        updated = move_player(&state, 0, 1);
        break;
      case 'r':
        reset_game(&state);
        updated = true;
        break;
      case 'u':
        undo_move(&state);
        updated = true;
        break;
      default:
        continue;
    }
    if (updated) {
      print_board(&state, &render_state);
    }

    // If there's an ongoing event, process it and redraw each gametic
    // accordingly until resolved
    while (state.event.type != EVENT_NONE) {
      // printf("event! %dx%d (%d,%d)\n", state.event.x, state.event.y,
      // state.event.dr, state.event.dc);
      delay(TIC_DURATION_MS);
      if (process_event(&state)) {
        print_board(&state, &render_state);
      }
    }

    if (is_game_won(&state)) {
      printf("%sLevel %zu complete!%s\n", COLOR_STATUS, current_level_index + 1, COLOR_RESET);
      printf("%sTotal steps taken: %zu%s\n", COLOR_STATUS, state.history.size, COLOR_RESET);
      printf("%sMove history: %.*s%s\n", COLOR_STATUS, (int)state.history.size, state.history.moves, COLOR_RESET);
      delay(2000);
      current_sequence_index++;
      printf("\n");
      if (current_sequence_index >= level_sequence.count) {
        printf(custom_selection ? "Congratulations! You completed the selected levels.\n" : "Congratulations! You completed all levels.\n");
        break;
      }
      current_level_index = level_sequence.levels[current_sequence_index];
      reset_board_render_state(&render_state);
      if (!load_terminal_level(&state, level_file->filename[0], current_level_index)) {
        fprintf(stderr, "Error loading level %zu from %s\n", current_level_index + 1, level_file->filename[0]);
        goto cleanup;
      }
      print_board(&state, &render_state);
    }
  }

  exit_code = EXIT_SUCCESS;

cleanup:

#ifndef _WIN32
  if (raw_mode_enabled) {
    disable_raw_mode();
  }
#endif

  if (history_initialized) {
    clear_move_history(&state.history);
  }
  clear_level_sequence(&level_sequence);
  arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
  return exit_code;
}
