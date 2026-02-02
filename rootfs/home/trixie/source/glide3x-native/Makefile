# Makefile for software Glide3x DLL
#
# Cross-compile for i386-windows using MinGW

CC = i686-w64-mingw32-gcc
CXX = i686-w64-mingw32-g++
WINDRES = i686-w64-mingw32-windres

CFLAGS = -O2 -Wall -Wextra -msse2
CFLAGS += -I./src -I./simde/simde
CFLAGS += -DWIN32 -D_WIN32 -DNDEBUG -DGLIDE3X_EXPORTS

LDFLAGS = -shared -Wl,--enable-stdcall-fixup
LDFLAGS += -static-libgcc -static-libstdc++
LIBS = -lgdi32 -lddraw -ldxguid

# Source files - broken out into modules
# Note: glide3x.c is the monolithic file, now replaced by broken-out modules
SRCS = src/glide3x_state.c \
       src/glide3x_init.c \
       src/glide3x_context.c \
       src/glide3x_buffer.c \
       src/glide3x_lfb.c \
       src/glide3x_combine.c \
       src/glide3x_blend.c \
       src/glide3x_depth.c \
       src/glide3x_alpha.c \
       src/glide3x_draw.c \
       src/glide3x_texture.c \
       src/glide3x_fog.c \
       src/glide3x_misc.c \
       src/glide3x_get.c \
       src/voodoo_emu.c \
       src/display_ddraw.c \
       src/glide3x_screenshot.c

# Output
BUILD_DIR = build
TARGET = $(BUILD_DIR)/glide3x.dll
DEFFILE = src/glide3x.def
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all clean install test test-lfb tests

all: $(TARGET) tests

$(TARGET): $(OBJS) $(DEFFILE)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(DEFFILE) $(LIBS)
	@echo "Built $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TEST_TARGET) $(TEST_LFB_TARGET)

# Install to Wine prefix
install: $(TARGET)
	@if [ -z "$(WINEPREFIX)" ]; then \
		echo "Set WINEPREFIX to install"; \
		exit 1; \
	fi
	cp $(TARGET) $(WINEPREFIX)/drive_c/windows/syswow64/
	@echo "Installed to $(WINEPREFIX)/drive_c/windows/syswow64/"

# Test build (native, for checking compilation)
test-native:
	gcc -c -o /dev/null src/glide3x.c $(CFLAGS) -DTEST_NATIVE
	@echo "Native compilation check passed"

# Test executable
TEST_TARGET = test_glide.exe
TEST_SRCS = test/test_glide.c

$(TEST_TARGET): $(TARGET) $(TEST_SRCS)
	$(CC) -O0 -g -Wall -Wextra -I./src -o $@ $(TEST_SRCS) $(TARGET)
	@echo "Built $@"

test: $(TEST_TARGET)
	@echo "Run with: wine ./test_glide.exe"

# LFB stride test (Diablo 2 ZARDBLIZ bug reproduction)
TEST_LFB_TARGET = test_lfb_stride.exe
TEST_LFB_SRCS = test/test_lfb_stride.c

$(TEST_LFB_TARGET): $(TARGET) $(TEST_LFB_SRCS)
	$(CC) -O0 -g -Wall -Wextra -I./src -o $@ $(TEST_LFB_SRCS) $(TARGET)
	@echo "Built $@"

test-lfb: $(TEST_LFB_TARGET)
	@echo "Run with: wine ./test_lfb_stride.exe"
	@echo "This test reproduces the D2 title screen stride bug"

# Build all test executables
tests: $(TEST_TARGET) $(TEST_LFB_TARGET)
	@echo "All tests built: $(TEST_TARGET) $(TEST_LFB_TARGET)"
