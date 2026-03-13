#include "level_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER_SIZE 4096

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} TextBuffer;

typedef struct {
  size_t first_valid_start_line;
  size_t target_start_line;
  size_t target_end_line;
  size_t next_valid_start_line;
  bool found_target;
  bool found_next;
} LevelBoundaryInfo;

static void init_level_state(LevelState *level) {
  memset(level->board, FLOOR, sizeof(level->board));
  level->rows = 0;
  level->cols = 0;
  level->player_row = 0;
  level->player_col = 0;
}

static void init_text_buffer(TextBuffer *buffer) {
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
}

static void clear_text_buffer(TextBuffer *buffer) {
  free(buffer->data);
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
}

static bool ensure_text_capacity(TextBuffer *buffer, size_t needed_size) {
  size_t new_capacity = 0;
  char *new_data = NULL;

  if (needed_size <= buffer->capacity) {
    return true;
  }

  new_capacity = (buffer->capacity == 0) ? 64 : buffer->capacity;
  while (new_capacity < needed_size) {
    new_capacity *= 2;
  }

  new_data = (char *)realloc(buffer->data, new_capacity);
  if (new_data == NULL) {
    return false;
  }

  buffer->data = new_data;
  buffer->capacity = new_capacity;
  return true;
}

static bool append_text(TextBuffer *buffer, const char *text, size_t len) {
  if (!ensure_text_capacity(buffer, buffer->size + len + 1)) {
    return false;
  }

  memcpy(buffer->data + buffer->size, text, len);
  buffer->size += len;
  buffer->data[buffer->size] = '\0';
  return true;
}

static bool append_metadata_line(TextBuffer *buffer, const char *line, size_t len) {
  if (len == 0) {
    if (buffer->size == 0) {
      return true;
    }
    if (buffer->size >= 2 && buffer->data[buffer->size - 1] == '\n' && buffer->data[buffer->size - 2] == '\n') {
      return true;
    }
    return append_text(buffer, "\n", 1);
  }

  if (!append_text(buffer, line, len)) {
    return false;
  }
  return append_text(buffer, "\n", 1);
}

static void trim_trailing_blank_lines(TextBuffer *buffer) {
  while (buffer->size > 0 && buffer->data[buffer->size - 1] == '\n') {
    buffer->size--;
  }
  while (buffer->size > 0 && buffer->data[buffer->size - 1] == '\n') {
    buffer->size--;
  }
  if (buffer->data != NULL) {
    buffer->data[buffer->size] = '\0';
  }
}

static bool is_board_char(char ch) {
  switch (ch) {
    case FLOOR:
    case WALL:
    case PLAYER:
    case BOX:
    case GOAL:
    case PLAYER_ON_GOAL:
    case BOX_ON_GOAL:
    case ICE:
    case PLAYER_ON_ICE:
    case BOX_ON_ICE:
    case 'p':
    case 'P':
    case 'b':
    case 'B':
    case '-':
    case '_':
      return true;
    default:
      return false;
  }
}

static char normalize_board_char(char ch) {
  switch (ch) {
    case 'p':
      return PLAYER;
    case 'P':
      return PLAYER_ON_GOAL;
    case 'b':
      return BOX;
    case 'B':
      return BOX_ON_GOAL;
    case '-':
    case '_':
      return FLOOR;
    default:
      return ch;
  }
}

static bool is_board_line(const char *line, size_t len) {
  if (len == 0) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    if (!is_board_char(line[i])) {
      return false;
    }
  }
  return true;
}

static bool is_comment_line(const char *line, size_t len) {
  return (len >= 1 && line[0] == ';') || (len >= 2 && line[0] == ':' && line[1] == ':');
}

static bool starts_with_ignore_case(const char *line, size_t len, const char *prefix) {
  size_t i = 0;

  while (prefix[i] != '\0') {
    if (i >= len) {
      return false;
    }
    if (tolower((unsigned char)line[i]) != tolower((unsigned char)prefix[i])) {
      return false;
    }
    i++;
  }
  return true;
}

static bool is_solution_section_start(const char *line, size_t len) {
  return starts_with_ignore_case(line, len, "best solution") || starts_with_ignore_case(line, len, "solution") ||
         starts_with_ignore_case(line, len, "saved game") || starts_with_ignore_case(line, len, "save game");
}

static bool append_buffer(TextBuffer *buffer, const TextBuffer *suffix) {
  if (suffix->data == NULL || suffix->size == 0) {
    return true;
  }
  return append_text(buffer, suffix->data, suffix->size);
}

static bool append_metadata_block(TextBuffer *metadata, const TextBuffer *block) {
  if (block->data == NULL || block->size == 0) {
    return true;
  }
  if (metadata->size > 0 && !append_metadata_line(metadata, "", 0)) {
    return false;
  }
  return append_buffer(metadata, block);
}

static void move_text_buffer(TextBuffer *destination, TextBuffer *source) {
  clear_text_buffer(destination);
  *destination = *source;
  source->data = NULL;
  source->size = 0;
  source->capacity = 0;
}

static bool flush_post_board_blocks(TextBuffer *metadata, TextBuffer *tail_block, size_t *tail_lines, TextBuffer *current_block, size_t *current_lines,
                                    bool drop_tail_if_single_line) {
  if (tail_block->size > 0 && !(drop_tail_if_single_line && *tail_lines == 1)) {
    if (!append_metadata_block(metadata, tail_block)) {
      return false;
    }
  }
  if (current_block->size > 0) {
    if (!append_metadata_block(metadata, current_block)) {
      return false;
    }
  }

  clear_text_buffer(tail_block);
  clear_text_buffer(current_block);
  *tail_lines = 0;
  *current_lines = 0;
  return true;
}

static bool finalize_metadata_buffer(TextBuffer *buffer, char **out_metadata) {
  trim_trailing_blank_lines(buffer);

  if (buffer->data == NULL) {
    buffer->data = (char *)malloc(1);
    if (buffer->data == NULL) {
      return false;
    }
    buffer->data[0] = '\0';
    buffer->size = 0;
    buffer->capacity = 1;
  }

  *out_metadata = buffer->data;
  return true;
}

static bool append_level_line(LevelState *level, const char *line, size_t len) {
  if (level->rows >= MAX_ROWS || len > MAX_COLS) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    char tile = normalize_board_char(line[i]);

    switch (tile) {
      case PLAYER:
      case PLAYER_ON_GOAL:
      case PLAYER_ON_ICE:
        level->player_row = level->rows;
        level->player_col = (int)i;
        /* FALLTHROUGH */
      case WALL:
      case GOAL:
      case ICE:
      case BOX:
      case BOX_ON_GOAL:
      case BOX_ON_ICE:
        level->board[level->rows][i] = tile;
        break;
      default:
        level->board[level->rows][i] = FLOOR;
        break;
    }
  }

  for (size_t i = len; i < MAX_COLS; i++) {
    level->board[level->rows][i] = FLOOR;
  }

  if (level->cols < (int)len) {
    level->cols = (int)len;
  }
  level->rows++;
  return true;
}

static bool is_valid_level_state(const LevelState *level) {
  for (int row = 0; row < level->rows; row++) {
    for (int col = 0; col < level->cols; col++) {
      if (level->board[row][col] == PLAYER || level->board[row][col] == PLAYER_ON_GOAL || level->board[row][col] == PLAYER_ON_ICE) {
        return true;
      }
    }
  }

  return false;
}

bool parse_sok_level_from_file(FILE *file, size_t level_index, LevelState *out_level) {
  char line[LINE_BUFFER_SIZE];
  LevelState candidate;
  size_t current_index = 0;
  bool in_level = false;
  bool candidate_valid = true;

  if (file == NULL || out_level == NULL) {
    return false;
  }

  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strcspn(line, "\r\n");
    bool board_line = is_board_line(line, len);

    if (line[len] == '\0' && !feof(file)) {
      int ch = 0;
      while ((ch = fgetc(file)) != EOF && ch != '\n') {}
      return false;
    }

    if (board_line) {
      if (!in_level) {
        init_level_state(&candidate);
        in_level = true;
        candidate_valid = true;
      }

      if (candidate_valid && !append_level_line(&candidate, line, len)) {
        candidate_valid = false;
      }
      continue;
    }

    if (in_level) {
      if (candidate_valid && is_valid_level_state(&candidate)) {
        if (current_index == level_index) {
          *out_level = candidate;
          return true;
        }
        current_index++;
      }
      in_level = false;
    }
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    if (current_index == level_index) {
      *out_level = candidate;
      return true;
    }
  }

  return false;
}

bool parse_sok_level_from_string(const char *text, size_t level_index, LevelState *out_level) {
  const char *line = text;
  LevelState candidate;
  size_t current_index = 0;
  bool in_level = false;
  bool candidate_valid = true;

  if (text == NULL || out_level == NULL) {
    return false;
  }

  while (*line != '\0') {
    const char *cursor = line;
    size_t len = 0;
    bool board_line = false;

    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
      cursor++;
      len++;
    }

    board_line = is_board_line(line, len);
    if (board_line) {
      if (!in_level) {
        init_level_state(&candidate);
        in_level = true;
        candidate_valid = true;
      }

      if (candidate_valid && !append_level_line(&candidate, line, len)) {
        candidate_valid = false;
      }
    } else if (in_level) {
      if (candidate_valid && is_valid_level_state(&candidate)) {
        if (current_index == level_index) {
          *out_level = candidate;
          return true;
        }
        current_index++;
      }
      in_level = false;
    }

    line = cursor;
    if (*line == '\r') {
      line++;
      if (*line == '\n') {
        line++;
      }
    } else if (*line == '\n') {
      line++;
    }
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    if (current_index == level_index) {
      *out_level = candidate;
      return true;
    }
  }

  return false;
}

bool count_sok_levels_in_string(const char *text, size_t *out_count) {
  const char *line = text;
  size_t count = 0;
  LevelState candidate;
  bool in_level = false;
  bool candidate_valid = true;

  if (text == NULL || out_count == NULL) {
    return false;
  }

  while (*line != '\0') {
    const char *cursor = line;
    size_t len = 0;
    bool board_line = false;

    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
      cursor++;
      len++;
    }

    board_line = is_board_line(line, len);
    if (board_line) {
      if (!in_level) {
        init_level_state(&candidate);
        in_level = true;
        candidate_valid = true;
      }
      if (candidate_valid && !append_level_line(&candidate, line, len)) {
        candidate_valid = false;
      }
    } else {
      if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
        count++;
      }
      in_level = false;
      candidate_valid = true;
    }

    line = cursor;
    if (*line == '\r') {
      line++;
      if (*line == '\n') {
        line++;
      }
    } else if (*line == '\n') {
      line++;
    }
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    count++;
  }

  *out_count = count;
  return true;
}

bool count_sok_levels_in_file(FILE *file, size_t *out_count) {
  char line[LINE_BUFFER_SIZE];
  size_t count = 0;
  LevelState candidate;
  bool in_level = false;
  bool candidate_valid = true;

  if (file == NULL || out_count == NULL) {
    return false;
  }

  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strcspn(line, "\r\n");
    bool board_line = is_board_line(line, len);

    if (line[len] == '\0' && !feof(file)) {
      return false;
    }

    if (board_line) {
      if (!in_level) {
        init_level_state(&candidate);
        in_level = true;
        candidate_valid = true;
      }
      if (candidate_valid && !append_level_line(&candidate, line, len)) {
        candidate_valid = false;
      }
      continue;
    }

    if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
      count++;
    }
    in_level = false;
    candidate_valid = true;
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    count++;
  }

  *out_count = count;
  return true;
}

static bool find_level_boundary_info(FILE *file, size_t target_index, LevelBoundaryInfo *info) {
  char line[LINE_BUFFER_SIZE];
  LevelState candidate;
  size_t candidate_start_line = 0;
  size_t candidate_end_line = 0;
  size_t current_index = 0;
  size_t line_number = 0;
  bool in_level = false;
  bool candidate_valid = true;

  if (file == NULL || info == NULL) {
    return false;
  }

  memset(info, 0, sizeof(*info));

  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strcspn(line, "\r\n");
    bool board_line = is_board_line(line, len);

    line_number++;
    if (line[len] == '\0' && !feof(file)) {
      return false;
    }

    if (board_line) {
      if (!in_level) {
        init_level_state(&candidate);
        in_level = true;
        candidate_valid = true;
        candidate_start_line = line_number;
      }
      candidate_end_line = line_number;
      if (candidate_valid && !append_level_line(&candidate, line, len)) {
        candidate_valid = false;
      }
      continue;
    }

    if (!in_level) {
      continue;
    }

    if (candidate_valid && is_valid_level_state(&candidate)) {
      if (current_index == 0) {
        info->first_valid_start_line = candidate_start_line;
      }
      if (current_index == target_index) {
        info->target_start_line = candidate_start_line;
        info->target_end_line = candidate_end_line;
        info->found_target = true;
      } else if (info->found_target && current_index == target_index + 1) {
        info->next_valid_start_line = candidate_start_line;
        info->found_next = true;
        return true;
      }
      current_index++;
    }

    in_level = false;
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    if (current_index == 0) {
      info->first_valid_start_line = candidate_start_line;
    }
    if (current_index == target_index) {
      info->target_start_line = candidate_start_line;
      info->target_end_line = candidate_end_line;
      info->found_target = true;
    } else if (info->found_target && current_index == target_index + 1) {
      info->next_valid_start_line = candidate_start_line;
      info->found_next = true;
    }
  }

  return info->found_target;
}

bool parse_sok_level_title_from_file(FILE *file, size_t level_index, char **out_title) {
  LevelBoundaryInfo info;
  TextBuffer current_block;
  TextBuffer last_block;
  char line[LINE_BUFFER_SIZE];
  size_t current_block_lines = 0;
  size_t last_block_lines = 0;
  size_t line_number = 0;

  if (file == NULL || out_title == NULL) {
    return false;
  }

  *out_title = NULL;
  if (!find_level_boundary_info(file, level_index, &info)) {
    return false;
  }

  rewind(file);
  init_text_buffer(&current_block);
  init_text_buffer(&last_block);

  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strcspn(line, "\r\n");

    line_number++;
    if (line[len] == '\0' && !feof(file)) {
      clear_text_buffer(&current_block);
      clear_text_buffer(&last_block);
      return false;
    }
    if (line_number >= info.target_start_line) {
      break;
    }
    if (is_comment_line(line, len)) {
      continue;
    }

    if (len == 0) {
      if (current_block.size > 0) {
        move_text_buffer(&last_block, &current_block);
        last_block_lines = current_block_lines;
        current_block_lines = 0;
      }
      continue;
    }

    if (!append_metadata_line(&current_block, line, len)) {
      clear_text_buffer(&current_block);
      clear_text_buffer(&last_block);
      return false;
    }
    current_block_lines++;
  }

  if (current_block.size > 0) {
    move_text_buffer(&last_block, &current_block);
    last_block_lines = current_block_lines;
  }

  clear_text_buffer(&current_block);

  if (last_block_lines != 1) {
    clear_text_buffer(&last_block);
    return true;
  }

  trim_trailing_blank_lines(&last_block);
  if (last_block.data == NULL) {
    clear_text_buffer(&last_block);
    return true;
  }

  *out_title = last_block.data;
  return true;
}

bool parse_sok_level_metadata_from_file(FILE *file, size_t level_index, char **out_metadata) {
  LevelBoundaryInfo info;
  TextBuffer pending;
  TextBuffer metadata;
  TextBuffer post_board_tail;
  TextBuffer post_board_current;
  char line[LINE_BUFFER_SIZE];
  size_t post_board_tail_lines = 0;
  size_t post_board_current_lines = 0;
  size_t line_number = 0;
  bool pending_in_block = false;
  bool pending_committed = false;
  bool suppress_notes = false;

  if (file == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  if (!find_level_boundary_info(file, level_index, &info)) {
    return false;
  }

  rewind(file);
  init_text_buffer(&pending);
  init_text_buffer(&metadata);
  init_text_buffer(&post_board_tail);
  init_text_buffer(&post_board_current);

  while (fgets(line, sizeof(line), file) != NULL) {
    size_t len = strcspn(line, "\r\n");

    line_number++;

    if (line[len] == '\0' && !feof(file)) {
      clear_text_buffer(&pending);
      clear_text_buffer(&metadata);
      clear_text_buffer(&post_board_tail);
      clear_text_buffer(&post_board_current);
      return false;
    }

    if (line_number >= info.target_start_line && line_number <= info.target_end_line) {
      continue;
    }
    if (info.found_next && line_number >= info.next_valid_start_line) {
      break;
    }

    if (is_comment_line(line, len)) {
      continue;
    }

    if (line_number < info.target_start_line) {
      if (level_index == 0) {
        if (!append_metadata_line(&pending, line, len)) {
          clear_text_buffer(&pending);
          clear_text_buffer(&metadata);
          clear_text_buffer(&post_board_tail);
          clear_text_buffer(&post_board_current);
          return false;
        }
        continue;
      }

      if (len == 0) {
        pending_in_block = false;
        continue;
      }

      if (!pending_in_block) {
        clear_text_buffer(&pending);
        pending_in_block = true;
      }

      if (!append_metadata_line(&pending, line, len)) {
        clear_text_buffer(&pending);
        clear_text_buffer(&metadata);
        return false;
      }
      continue;
    }

    if (!pending_committed) {
      bool committed = false;

      if (level_index == 0) {
        committed = append_buffer(&metadata, &pending);
      } else {
        committed = append_metadata_block(&metadata, &pending);
      }
      if (!committed) {
        clear_text_buffer(&pending);
        clear_text_buffer(&metadata);
        clear_text_buffer(&post_board_tail);
        clear_text_buffer(&post_board_current);
        return false;
      }

      clear_text_buffer(&pending);
      pending_committed = true;
    }

    if (is_solution_section_start(line, len)) {
      if (!flush_post_board_blocks(&metadata, &post_board_tail, &post_board_tail_lines, &post_board_current, &post_board_current_lines, false)) {
        clear_text_buffer(&pending);
        clear_text_buffer(&metadata);
        clear_text_buffer(&post_board_tail);
        clear_text_buffer(&post_board_current);
        return false;
      }
      suppress_notes = true;
      continue;
    }
    if (suppress_notes) {
      continue;
    }

    if (len == 0) {
      if (post_board_current.size > 0) {
        if (post_board_tail.size > 0 && !append_metadata_block(&metadata, &post_board_tail)) {
          clear_text_buffer(&pending);
          clear_text_buffer(&metadata);
          clear_text_buffer(&post_board_tail);
          clear_text_buffer(&post_board_current);
          return false;
        }
        clear_text_buffer(&post_board_tail);
        move_text_buffer(&post_board_tail, &post_board_current);
        post_board_tail_lines = post_board_current_lines;
        post_board_current_lines = 0;
      }
      continue;
    }

    if (!append_metadata_line(&post_board_current, line, len)) {
      clear_text_buffer(&pending);
      clear_text_buffer(&metadata);
      clear_text_buffer(&post_board_tail);
      clear_text_buffer(&post_board_current);
      return false;
    }
    post_board_current_lines++;
  }

  if (!pending_committed) {
    bool committed = false;

    if (level_index == 0) {
      committed = append_buffer(&metadata, &pending);
    } else {
      committed = append_metadata_block(&metadata, &pending);
    }
    if (!committed) {
      clear_text_buffer(&pending);
      clear_text_buffer(&metadata);
      clear_text_buffer(&post_board_tail);
      clear_text_buffer(&post_board_current);
      return false;
    }
  }

  clear_text_buffer(&pending);

  if (!suppress_notes && !flush_post_board_blocks(&metadata, &post_board_tail, &post_board_tail_lines, &post_board_current, &post_board_current_lines,
                                                  info.found_next && post_board_current.size == 0 && post_board_tail_lines == 1)) {
    clear_text_buffer(&metadata);
    clear_text_buffer(&post_board_tail);
    clear_text_buffer(&post_board_current);
    return false;
  }

  clear_text_buffer(&post_board_tail);
  clear_text_buffer(&post_board_current);

  return finalize_metadata_buffer(&metadata, out_metadata);
}
