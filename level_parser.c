#include "level_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER_SIZE 4096
#define UTF8_KEY_ON_ICE "\xE1\xB8\xB5"
#define UTF8_KEY_ON_ICE_LEN 3
#define UTF8_KEY_ON_GOAL "\xE1\xB8\xB3"
#define UTF8_KEY_ON_GOAL_LEN 3

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

typedef enum {
  LINE_READER_STRING,
  LINE_READER_FILE,
} LineReaderKind;

typedef enum {
  LINE_READER_RESULT_OK,
  LINE_READER_RESULT_EOF,
  LINE_READER_RESULT_ERROR,
} LineReaderResult;

typedef struct {
  LineReaderKind kind;
  union {
    struct {
      const char *cursor;
    } string;
    FILE *file;
  } source;
  char buffer[LINE_BUFFER_SIZE];
} LineReader;

// Resets a level state so parsed board rows can be filled from scratch.
static void init_level_state(LevelState *level) {
  memset(level->board, FLOOR, sizeof(level->board));
  level->rows = 0;
  level->cols = 0;
  level->player_row = 0;
  level->player_col = 0;
}

// Initializes an empty growable text buffer.
static void init_text_buffer(TextBuffer *buffer) {
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
}

// Frees the storage owned by one text buffer.
static void clear_text_buffer(TextBuffer *buffer) {
  free(buffer->data);
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
}

// Frees a list of text buffers used by a shared parsing routine.
static void clear_text_buffers(TextBuffer *const *buffers, size_t count) {
  size_t i = 0;

  for (i = 0; i < count; i++) {
    if (buffers[i] != NULL) {
      clear_text_buffer(buffers[i]);
    }
  }
}

// Ensures a text buffer has room for the requested number of bytes.
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

// Appends raw text to a growable text buffer.
static bool append_text(TextBuffer *buffer, const char *text, size_t len) {
  if (!ensure_text_capacity(buffer, buffer->size + len + 1)) {
    return false;
  }

  memcpy(buffer->data + buffer->size, text, len);
  buffer->size += len;
  buffer->data[buffer->size] = '\0';
  return true;
}

// Appends one metadata line while preserving single blank-line separators.
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

// Removes trailing blank lines from a collected metadata block.
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

// Reports whether an ASCII character is accepted as part of a Sokoban board row.
static bool is_ascii_board_char(char ch) {
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
    case KEY:
    case LOCK:
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

// Maps legacy ASCII board characters to the normalized internal tile set.
static char normalize_ascii_board_char(char ch) {
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

// Decodes one board tile from a line, including UTF-8 key variants.
static bool read_board_tile(const char *line, size_t len, size_t *cursor, char *out_tile) {
  if (*cursor >= len) {
    return false;
  }

  if (len - *cursor >= UTF8_KEY_ON_GOAL_LEN && memcmp(line + *cursor, UTF8_KEY_ON_GOAL, UTF8_KEY_ON_GOAL_LEN) == 0) {
    *out_tile = KEY_ON_GOAL;
    *cursor += UTF8_KEY_ON_GOAL_LEN;
    return true;
  }

  if (len - *cursor >= UTF8_KEY_ON_ICE_LEN && memcmp(line + *cursor, UTF8_KEY_ON_ICE, UTF8_KEY_ON_ICE_LEN) == 0) {
    *out_tile = KEY_ON_ICE;
    *cursor += UTF8_KEY_ON_ICE_LEN;
    return true;
  }

  if (!is_ascii_board_char(line[*cursor])) {
    return false;
  }

  *out_tile = normalize_ascii_board_char(line[*cursor]);
  (*cursor)++;
  return true;
}

// Checks whether an entire line looks like board data.
static bool is_board_line(const char *line, size_t len) {
  size_t cursor = 0;
  size_t tile_count = 0;
  char tile = '\0';

  if (len == 0) {
    return false;
  }

  while (cursor < len) {
    if (!read_board_tile(line, len, &cursor, &tile)) {
      return false;
    }
    tile_count++;
  }
  return tile_count > 0;
}

// Identifies comment lines that should be ignored by metadata parsing.
static bool is_comment_line(const char *line, size_t len) {
  return (len >= 1 && line[0] == ';') || (len >= 2 && line[0] == ':' && line[1] == ':');
}

// Compares a line prefix case-insensitively against a fixed string.
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

// Detects sections that contain solutions rather than descriptive metadata.
static bool is_solution_section_start(const char *line, size_t len) {
  return starts_with_ignore_case(line, len, "best solution") || starts_with_ignore_case(line, len, "solution") ||
         starts_with_ignore_case(line, len, "saved game") || starts_with_ignore_case(line, len, "save game");
}

// Appends one text buffer onto another.
static bool append_buffer(TextBuffer *buffer, const TextBuffer *suffix) {
  if (suffix->data == NULL || suffix->size == 0) {
    return true;
  }
  return append_text(buffer, suffix->data, suffix->size);
}

// Appends a metadata block, inserting a blank line when needed.
static bool append_metadata_block(TextBuffer *metadata, const TextBuffer *block) {
  if (block->data == NULL || block->size == 0) {
    return true;
  }
  if (metadata->size > 0 && !append_metadata_line(metadata, "", 0)) {
    return false;
  }
  return append_buffer(metadata, block);
}

// Transfers ownership of one text buffer into another.
static void move_text_buffer(TextBuffer *destination, TextBuffer *source) {
  clear_text_buffer(destination);
  *destination = *source;
  source->data = NULL;
  source->size = 0;
  source->capacity = 0;
}

// Flushes buffered post-board metadata blocks into the final metadata output.
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

// Finalizes a metadata buffer and hands its storage to the caller.
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

// Initializes a parsed level-info bundle to empty strings.
static void init_parsed_level_info(ParsedLevelInfo *info) {
  if (info == NULL) {
    return;
  }

  info->title = NULL;
  info->description = NULL;
  info->pack_metadata = NULL;
}

// Releases all strings stored in a parsed level-info bundle.
void free_parsed_level_info(ParsedLevelInfo *info) {
  if (info == NULL) {
    return;
  }

  free(info->title);
  free(info->description);
  free(info->pack_metadata);
  init_parsed_level_info(info);
}

// Duplicates a NUL-terminated string with heap storage.
static char *duplicate_string(const char *text) {
  size_t len = 0;
  char *copy = NULL;

  if (text == NULL) {
    return NULL;
  }

  len = strlen(text);
  copy = (char *)malloc(len + 1);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, text, len + 1);
  return copy;
}

// Removes a leading prefix block from text when it matches exactly.
static char *strip_leading_text_prefix(const char *text, const char *prefix) {
  const char *suffix = text;
  size_t prefix_len = 0;

  if (text == NULL) {
    return NULL;
  }

  if (prefix != NULL && prefix[0] != '\0') {
    prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) == 0) {
      suffix += prefix_len;
      while (*suffix == '\n') {
        suffix++;
      }
    }
  }

  return duplicate_string(suffix);
}

// Strips pack and title text back out of the parsed level description field.
static bool normalize_parsed_level_info(size_t level_index, ParsedLevelInfo *info) {
  char *stripped_description = NULL;

  if (info == NULL || info->description == NULL) {
    return true;
  }

  if (level_index == 0) {
    stripped_description = strip_leading_text_prefix(info->description, info->pack_metadata);
    if (stripped_description == NULL) {
      return false;
    }
    free(info->description);
    info->description = stripped_description;
  }

  stripped_description = strip_leading_text_prefix(info->description, info->title);
  if (stripped_description == NULL) {
    return false;
  }
  free(info->description);
  info->description = stripped_description;
  return true;
}

// Prepares a line reader that streams lines from an in-memory string.
static bool init_string_line_reader(LineReader *reader, const char *text) {
  if (reader == NULL || text == NULL) {
    return false;
  }

  reader->kind = LINE_READER_STRING;
  reader->source.string.cursor = text;
  return true;
}

// Prepares a line reader that streams lines from a rewound file.
static bool init_file_line_reader(LineReader *reader, FILE *file) {
  if (reader == NULL || file == NULL) {
    return false;
  }

  rewind(file);
  reader->kind = LINE_READER_FILE;
  reader->source.file = file;
  return true;
}

// Returns the next line slice from a string cursor without allocating.
static LineReaderResult next_string_line_from_cursor(const char **cursor, const char **out_line, size_t *out_len) {
  const char *line = NULL;
  const char *end = NULL;

  if (cursor == NULL || *cursor == NULL || out_line == NULL || out_len == NULL) {
    return LINE_READER_RESULT_ERROR;
  }
  if (**cursor == '\0') {
    return LINE_READER_RESULT_EOF;
  }

  line = *cursor;
  end = line;
  while (*end != '\0' && *end != '\n' && *end != '\r') {
    end++;
  }

  *out_line = line;
  *out_len = (size_t)(end - line);
  *cursor = end;

  if (**cursor == '\r') {
    (*cursor)++;
    if (**cursor == '\n') {
      (*cursor)++;
    }
  } else if (**cursor == '\n') {
    (*cursor)++;
  }

  return LINE_READER_RESULT_OK;
}

// Returns the next line from either the active file or string reader.
static LineReaderResult next_line(LineReader *reader, const char **out_line, size_t *out_len) {
  if (reader == NULL || out_line == NULL || out_len == NULL) {
    return LINE_READER_RESULT_ERROR;
  }

  if (reader->kind == LINE_READER_STRING) {
    return next_string_line_from_cursor(&reader->source.string.cursor, out_line, out_len);
  }

  if (fgets(reader->buffer, sizeof(reader->buffer), reader->source.file) == NULL) {
    return ferror(reader->source.file) ? LINE_READER_RESULT_ERROR : LINE_READER_RESULT_EOF;
  }

  *out_len = strcspn(reader->buffer, "\r\n");
  if (reader->buffer[*out_len] == '\0' && !feof(reader->source.file)) {
    int ch = 0;

    while ((ch = fgetc(reader->source.file)) != EOF && ch != '\n') {}
    return LINE_READER_RESULT_ERROR;
  }

  *out_line = reader->buffer;
  return LINE_READER_RESULT_OK;
}

// Copies one parsed board row into the target level state.
static bool append_level_line(LevelState *level, const char *line, size_t len) {
  size_t cursor = 0;
  size_t col = 0;

  if (level->rows >= MAX_ROWS) {
    return false;
  }

  while (cursor < len) {
    char tile = '\0';

    if (col >= MAX_COLS || !read_board_tile(line, len, &cursor, &tile)) {
      return false;
    }

    switch (tile) {
      case PLAYER:
      case PLAYER_ON_GOAL:
      case PLAYER_ON_ICE:
        level->player_row = level->rows;
        level->player_col = (int)col;
        /* FALLTHROUGH */
      case WALL:
      case GOAL:
      case ICE:
      case BOX:
      case BOX_ON_GOAL:
      case BOX_ON_ICE:
      case KEY:
      case KEY_ON_GOAL:
      case KEY_ON_ICE:
      case LOCK:
        level->board[level->rows][col] = tile;
        break;
      default:
        level->board[level->rows][col] = FLOOR;
        break;
    }

    col++;
  }

  for (size_t i = col; i < MAX_COLS; i++) {
    level->board[level->rows][i] = FLOOR;
  }

  if (level->cols < (int)col) {
    level->cols = (int)col;
  }
  level->rows++;
  return true;
}

// Verifies that a parsed level contains a player tile.
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

// Parses a specific level from a generic line reader.
static bool parse_sok_level_from_reader(LineReader *reader, size_t level_index, LevelState *out_level) {
  const char *line = NULL;
  size_t len = 0;
  LevelState candidate;
  size_t current_index = 0;
  bool in_level = false;
  bool candidate_valid = true;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || out_level == NULL) {
    return false;
  }

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    bool board_line = is_board_line(line, len);

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

  if (result == LINE_READER_RESULT_ERROR) {
    return false;
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate) && current_index == level_index) {
    *out_level = candidate;
    return true;
  }

  return false;
}

// Parses a specific level from a file-backed level pack.
bool parse_sok_level_from_file(FILE *file, size_t level_index, LevelState *out_level) {
  LineReader reader;

  if (file == NULL || out_level == NULL || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return parse_sok_level_from_reader(&reader, level_index, out_level);
}

// Parses a specific level from an in-memory level pack.
bool parse_sok_level_from_string(const char *text, size_t level_index, LevelState *out_level) {
  LineReader reader;

  if (text == NULL || out_level == NULL || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return parse_sok_level_from_reader(&reader, level_index, out_level);
}

// Counts playable levels exposed by a generic line reader.
static bool count_sok_levels_from_reader(LineReader *reader, size_t *out_count) {
  const char *line = NULL;
  size_t len = 0;
  size_t count = 0;
  LevelState candidate;
  bool in_level = false;
  bool candidate_valid = true;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || out_count == NULL) {
    return false;
  }

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    bool board_line = is_board_line(line, len);

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

  if (result == LINE_READER_RESULT_ERROR) {
    return false;
  }

  if (in_level && candidate_valid && is_valid_level_state(&candidate)) {
    count++;
  }

  *out_count = count;
  return true;
}

// Counts playable levels in an in-memory level pack.
bool count_sok_levels_in_string(const char *text, size_t *out_count) {
  LineReader reader;

  if (text == NULL || out_count == NULL || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return count_sok_levels_from_reader(&reader, out_count);
}

// Counts playable levels in a file-backed level pack.
bool count_sok_levels_in_file(FILE *file, size_t *out_count) {
  LineReader reader;

  if (file == NULL || out_count == NULL || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return count_sok_levels_from_reader(&reader, out_count);
}

// Locates the line ranges that bound a requested level within a stream.
static bool find_level_boundary_info_from_reader(LineReader *reader, size_t target_index, LevelBoundaryInfo *info) {
  const char *line = NULL;
  LevelState candidate;
  size_t candidate_start_line = 0;
  size_t candidate_end_line = 0;
  size_t current_index = 0;
  size_t line_number = 0;
  size_t len = 0;
  bool in_level = false;
  bool candidate_valid = true;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || info == NULL) {
    return false;
  }

  memset(info, 0, sizeof(*info));

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    bool board_line = is_board_line(line, len);

    line_number++;
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

  if (result == LINE_READER_RESULT_ERROR) {
    return false;
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

// Locates a level's line boundaries within an in-memory level pack.
static bool find_level_boundary_info_in_string(const char *text, size_t target_index, LevelBoundaryInfo *info) {
  LineReader reader;

  if (text == NULL || info == NULL || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return find_level_boundary_info_from_reader(&reader, target_index, info);
}

// Locates a level's line boundaries within a file-backed level pack.
static bool find_level_boundary_info(FILE *file, size_t target_index, LevelBoundaryInfo *info) {
  LineReader reader;

  if (file == NULL || info == NULL || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return find_level_boundary_info_from_reader(&reader, target_index, info);
}

// Extracts the single-line title block that appears before a level board.
static bool parse_sok_level_title_from_reader(LineReader *reader, size_t target_start_line, char **out_title) {
  TextBuffer current_block;
  TextBuffer last_block;
  const char *line = NULL;
  size_t len = 0;
  size_t current_block_lines = 0;
  size_t last_block_lines = 0;
  size_t line_number = 0;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || out_title == NULL) {
    return false;
  }

  *out_title = NULL;
  init_text_buffer(&current_block);
  init_text_buffer(&last_block);

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    line_number++;
    if (line_number >= target_start_line) {
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

  if (result == LINE_READER_RESULT_ERROR) {
    clear_text_buffer(&current_block);
    clear_text_buffer(&last_block);
    return false;
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

// Extracts a level title from a file-backed level pack.
bool parse_sok_level_title_from_file(FILE *file, size_t level_index, char **out_title) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (file == NULL || out_title == NULL) {
    return false;
  }

  *out_title = NULL;
  if (!find_level_boundary_info(file, level_index, &info) || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return parse_sok_level_title_from_reader(&reader, info.target_start_line, out_title);
}

// Extracts a level title from an in-memory level pack.
bool parse_sok_level_title_from_string(const char *text, size_t level_index, char **out_title) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (text == NULL || out_title == NULL) {
    return false;
  }

  *out_title = NULL;
  if (!find_level_boundary_info_in_string(text, level_index, &info) || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return parse_sok_level_title_from_reader(&reader, info.target_start_line, out_title);
}

// Extracts the descriptive metadata that belongs to one level.
static bool parse_sok_level_metadata_from_reader(LineReader *reader, size_t level_index, const LevelBoundaryInfo *info, char **out_metadata) {
  TextBuffer pending;
  TextBuffer metadata;
  TextBuffer post_board_tail;
  TextBuffer post_board_current;
  TextBuffer *buffers[] = {&pending, &metadata, &post_board_tail, &post_board_current};
  const char *line = NULL;
  size_t len = 0;
  size_t post_board_tail_lines = 0;
  size_t post_board_current_lines = 0;
  size_t line_number = 0;
  bool pending_in_block = false;
  bool pending_committed = false;
  bool suppress_notes = false;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || info == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  init_text_buffer(&pending);
  init_text_buffer(&metadata);
  init_text_buffer(&post_board_tail);
  init_text_buffer(&post_board_current);

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    line_number++;

    if (line_number >= info->target_start_line && line_number <= info->target_end_line) {
      continue;
    }
    if (info->found_next && line_number >= info->next_valid_start_line) {
      break;
    }

    if (is_comment_line(line, len)) {
      continue;
    }

    if (line_number < info->target_start_line) {
      if (level_index == 0) {
        if (!append_metadata_line(&pending, line, len)) {
          clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
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
        clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
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
        clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
        return false;
      }

      clear_text_buffer(&pending);
      pending_committed = true;
    }

    if (is_solution_section_start(line, len)) {
      if (!flush_post_board_blocks(&metadata, &post_board_tail, &post_board_tail_lines, &post_board_current, &post_board_current_lines, false)) {
        clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
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
          clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
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
      clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
      return false;
    }
    post_board_current_lines++;
  }

  if (result == LINE_READER_RESULT_ERROR) {
    clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
    return false;
  }

  if (!pending_committed) {
    bool committed = false;

    if (level_index == 0) {
      committed = append_buffer(&metadata, &pending);
    } else {
      committed = append_metadata_block(&metadata, &pending);
    }
    if (!committed) {
      clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
      return false;
    }
  }

  clear_text_buffer(&pending);

  if (info->found_next && post_board_current_lines == 1) {
    clear_text_buffer(&post_board_current);
    post_board_current_lines = 0;
  }
  if (info->found_next && post_board_current_lines == 0 && post_board_tail_lines == 1) {
    clear_text_buffer(&post_board_tail);
    post_board_tail_lines = 0;
  }

  if (!suppress_notes &&
      !flush_post_board_blocks(&metadata, &post_board_tail, &post_board_tail_lines, &post_board_current, &post_board_current_lines, false)) {
    clear_text_buffers(buffers + 1, 3);
    return false;
  }

  clear_text_buffer(&post_board_tail);
  clear_text_buffer(&post_board_current);

  return finalize_metadata_buffer(&metadata, out_metadata);
}

// Extracts level metadata from a file-backed level pack.
bool parse_sok_level_metadata_from_file(FILE *file, size_t level_index, char **out_metadata) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (file == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  if (!find_level_boundary_info(file, level_index, &info) || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return parse_sok_level_metadata_from_reader(&reader, level_index, &info, out_metadata);
}

// Extracts level metadata from an in-memory level pack.
bool parse_sok_level_metadata_from_string(const char *text, size_t level_index, char **out_metadata) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (text == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  if (!find_level_boundary_info_in_string(text, level_index, &info) || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return parse_sok_level_metadata_from_reader(&reader, level_index, &info, out_metadata);
}

// Extracts metadata that belongs to the pack header before the first level.
static bool parse_sok_pack_metadata_from_reader(LineReader *reader, const LevelBoundaryInfo *info, char **out_metadata) {
  TextBuffer metadata;
  TextBuffer current_block;
  TextBuffer last_block;
  TextBuffer *buffers[] = {&metadata, &current_block, &last_block};
  const char *line = NULL;
  size_t len = 0;
  size_t line_number = 0;
  size_t current_block_lines = 0;
  size_t last_block_lines = 0;
  LineReaderResult result = LINE_READER_RESULT_OK;

  if (reader == NULL || info == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  init_text_buffer(&metadata);
  init_text_buffer(&current_block);
  init_text_buffer(&last_block);

  while ((result = next_line(reader, &line, &len)) == LINE_READER_RESULT_OK) {
    line_number++;
    if (line_number >= info->first_valid_start_line) {
      break;
    }
    if (is_comment_line(line, len)) {
      continue;
    }

    if (len == 0) {
      if (current_block.size > 0) {
        if (last_block.size > 0 && !append_metadata_block(&metadata, &last_block)) {
          clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
          return false;
        }
        clear_text_buffer(&last_block);
        move_text_buffer(&last_block, &current_block);
        last_block_lines = current_block_lines;
        current_block_lines = 0;
      }
      continue;
    }

    if (!append_metadata_line(&current_block, line, len)) {
      clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
      return false;
    }
    current_block_lines++;
  }

  if (result == LINE_READER_RESULT_ERROR) {
    clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
    return false;
  }

  if (current_block.size > 0) {
    if (last_block.size > 0 && !append_metadata_block(&metadata, &last_block)) {
      clear_text_buffers(buffers, sizeof(buffers) / sizeof(buffers[0]));
      return false;
    }
    clear_text_buffer(&last_block);
    move_text_buffer(&last_block, &current_block);
    last_block_lines = current_block_lines;
  }

  clear_text_buffer(&current_block);

  if (last_block.size > 0 && last_block_lines != 1) {
    if (!append_metadata_block(&metadata, &last_block)) {
      clear_text_buffer(&metadata);
      clear_text_buffer(&last_block);
      return false;
    }
  }
  clear_text_buffer(&last_block);

  return finalize_metadata_buffer(&metadata, out_metadata);
}

// Extracts pack-level metadata from an in-memory level pack.
bool parse_sok_pack_metadata_from_string(const char *text, char **out_metadata) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (text == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  if (!find_level_boundary_info_in_string(text, 0, &info) || !init_string_line_reader(&reader, text)) {
    return false;
  }

  return parse_sok_pack_metadata_from_reader(&reader, &info, out_metadata);
}

// Extracts pack-level metadata from a file-backed level pack.
bool parse_sok_pack_metadata_from_file(FILE *file, char **out_metadata) {
  LevelBoundaryInfo info;
  LineReader reader;

  if (file == NULL || out_metadata == NULL) {
    return false;
  }

  *out_metadata = NULL;
  if (!find_level_boundary_info(file, 0, &info) || !init_file_line_reader(&reader, file)) {
    return false;
  }

  return parse_sok_pack_metadata_from_reader(&reader, &info, out_metadata);
}

// Parses the title, description, and pack metadata for one level from a file.
bool parse_sok_level_info_from_file(FILE *file, size_t level_index, ParsedLevelInfo *out_info) {
  ParsedLevelInfo info;

  if (file == NULL || out_info == NULL) {
    return false;
  }

  init_parsed_level_info(out_info);
  init_parsed_level_info(&info);
  if (!parse_sok_level_title_from_file(file, level_index, &info.title) || !parse_sok_level_metadata_from_file(file, level_index, &info.description) ||
      !parse_sok_pack_metadata_from_file(file, &info.pack_metadata) || !normalize_parsed_level_info(level_index, &info)) {
    free_parsed_level_info(&info);
    return false;
  }

  *out_info = info;
  return true;
}

// Parses the title, description, and pack metadata for one level from memory.
bool parse_sok_level_info_from_string(const char *text, size_t level_index, ParsedLevelInfo *out_info) {
  ParsedLevelInfo info;

  if (text == NULL || out_info == NULL) {
    return false;
  }

  init_parsed_level_info(out_info);
  init_parsed_level_info(&info);
  if (!parse_sok_level_title_from_string(text, level_index, &info.title) ||
      !parse_sok_level_metadata_from_string(text, level_index, &info.description) || !parse_sok_pack_metadata_from_string(text, &info.pack_metadata) ||
      !normalize_parsed_level_info(level_index, &info)) {
    free_parsed_level_info(&info);
    return false;
  }

  *out_info = info;
  return true;
}
