# Compiler and flags
CC = gcc
EMCC = emcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
EMCCFLAGS = -s EXPORTED_FUNCTIONS="['_sokoban_init_web', '_sokoban_init_web_level', '_sokoban_count_levels_web', '_sokoban_get_level_title_web', '_sokoban_get_level_description_web', '_sokoban_get_pack_metadata_web', '_sokoban_handle_input', '_sokoban_plan_tap_path_web', '_sokoban_is_event_ongoing', '_sokoban_process_event', '_sokoban_get_rows', '_sokoban_get_cols', '_sokoban_get_move_count_web', '_sokoban_get_tile', '_sokoban_get_initial_rows_web', '_sokoban_get_initial_cols_web', '_sokoban_get_initial_tile_web', '_sokoban_is_game_won']" -s EXPORTED_RUNTIME_METHODS="['cwrap']"
TERMINAL_LIBS = -lm

# Source files
COMMON_SRCS = sokoban.c level_parser.c
TERMINAL_SRCS = $(COMMON_SRCS) main_terminal.c vendor/argtable3.c
WEB_SRCS = $(COMMON_SRCS) main_emsdk.c

# Object files
TERMINAL_OBJS = $(TERMINAL_SRCS:.c=.o)

# Executable names
TERMINAL_EXE = sokterm
WEB_JS = web/res/sokoban.js

.PHONY: all terminal web serve format clean

all: terminal web

terminal: $(TERMINAL_EXE)

web: $(WEB_JS)


%.o: %.c sokoban.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TERMINAL_EXE): $(TERMINAL_SRCS)
	$(CC) $(CFLAGS) $^ -o $(TERMINAL_EXE) $(TERMINAL_LIBS)

$(WEB_JS): $(WEB_SRCS)
	$(EMCC) $(CFLAGS) $(EMCCFLAGS) $^ -o $(WEB_JS)

# Run a local web server (Python 3)
# To use Python 2, replace `python3 -m http.server` with `python -m SimpleHTTPServer`
serve: web
	@echo "Starting web server at http://localhost:8000/web/"
	@python3 -m http.server

format:
	clang-format --style=file -i *.c *.h
	perl -0pi -e 's/[ \t]+(\n)/$$1/g' web/*.js

clean:
	rm -f $(TERMINAL_EXE) $(TERMINAL_OBJS) $(WEB_JS) web/sokoban.wasm
