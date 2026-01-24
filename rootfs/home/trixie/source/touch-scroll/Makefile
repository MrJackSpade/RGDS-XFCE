# Touch-Scroll Build System
#
# This Makefile controls the build process for the touch-scroll utility.
# It compiles all source files in 'src/' and 'src/gestures/' and links them
# against libevdev.

# Compiler flags:
# -Wall -Wextra: Enable warning messages for better code quality.
# -O2: Optimize for performance.
# -I./src: Add 'src' directory to include path.
# pkg-config: Automatically retrieve include flags for libevdev.
CFLAGS = -Wall -Wextra -O2 -I./src $(shell pkg-config --cflags libevdev)

# Linker flags:
# pkg-config: Automatically retrieve library flags to link against libevdev.
LDFLAGS = $(shell pkg-config --libs libevdev)

# Directory definitions
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Source file discovery
# Finds all .c files in src/ and src/gestures/
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/gestures/*.c)

# Object file generation
# Converts source paths (src/foo.c) to object paths (build/obj/foo.o)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Target executable path
TARGET = $(BUILD_DIR)/touch-mouse

.PHONY: all clean

# Default target: build the executable
all: $(TARGET)

# Link step: combines all object files into the final executable
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile step: compiles individually source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Cleanup: removes the build directory and all artifacts
clean:
	rm -rf $(BUILD_DIR)
