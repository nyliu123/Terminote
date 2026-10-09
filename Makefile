CC := gcc
BUILD ?= release
MODE := $(BUILD)

TARGET := terminote.exe
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
TEST_DIR := tests
FONT_DIR := resources/fonts
FONT_GENERATOR := tools/generate-ascii-banner-font.ps1
GENERATED_DIR := $(BUILD_DIR)/generated
GENERATED_FONT_INDEX := $(GENERATED_DIR)/ascii-banner-font.inc
TEST_BIN_DIR := $(BUILD_DIR)/tests/$(MODE)
TEST_COMMON_TARGET := $(TEST_BIN_DIR)/test_common.exe
TEST_UNDO_TARGET := $(TEST_BIN_DIR)/test_undo_group.exe
TEST_BANNER_TARGET := $(TEST_BIN_DIR)/test_banner.exe
TEST_INI_CONFIG_TARGET := $(TEST_BIN_DIR)/test_ini_config.exe
TEST_WIN_TEXT_TARGET := $(TEST_BIN_DIR)/test_win_text.exe
TEST_UNICODE_CONSOLE_TARGET := $(TEST_BIN_DIR)/test_unicode_console.exe

CPPFLAGS := -I$(INC_DIR) -I$(GENERATED_DIR)
CFLAGS := -std=c11 -Wall -Wextra -MMD -MP \
	-finput-charset=UTF-8 -fexec-charset=UTF-8
LDFLAGS := -static-libgcc
LDLIBS := -lgdi32 -lcomdlg32 -lshell32

ifeq ($(BUILD),release)
CFLAGS += -Os -DNDEBUG -flto -ffunction-sections -fdata-sections
LDFLAGS += -flto -Wl,--gc-sections -s
else ifeq ($(BUILD),debug)
CFLAGS += -O0 -g
else
$(error BUILD must be release or debug)
endif

# Keep assertions active when testing the optimized build.
TEST_CFLAGS := $(filter-out -DNDEBUG,$(CFLAGS))

OBJ_DIR := $(BUILD_DIR)/$(MODE)/obj
SRCS := $(sort $(wildcard $(SRC_DIR)/*.c))
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
FONT_FILES := $(sort $(wildcard $(FONT_DIR)/*.inc))

all: $(TARGET)

$(TARGET): $(OBJS) FORCE
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

FORCE:

$(GENERATED_FONT_INDEX): FORCE $(FONT_GENERATOR) $(FONT_FILES)
	powershell -NoProfile -ExecutionPolicy Bypass -File "$(FONT_GENERATOR)" \
		-FontDirectory "$(FONT_DIR)" -Output "$@"

$(OBJ_DIR)/ascii_banner.o: $(GENERATED_FONT_INDEX)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@if not exist "$(OBJ_DIR)" mkdir "$(OBJ_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_COMMON_TARGET): $(TEST_DIR)/test_common.c $(SRC_DIR)/common.c
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_UNDO_TARGET): $(TEST_DIR)/test_undo_group.c \
		$(SRC_DIR)/undo.c $(SRC_DIR)/redo.c
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BANNER_TARGET): $(TEST_DIR)/test_banner.c \
		$(SRC_DIR)/banner.c $(SRC_DIR)/brand.c $(SRC_DIR)/ascii_banner.c \
		$(GENERATED_FONT_INDEX)
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS) -lgdi32

$(TEST_INI_CONFIG_TARGET): $(TEST_DIR)/test_ini_config.c \
		$(SRC_DIR)/ini_config.c
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_WIN_TEXT_TARGET): $(TEST_DIR)/test_win_text.c $(SRC_DIR)/win_text.c
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_UNICODE_CONSOLE_TARGET): $(TEST_DIR)/test_unicode_console.c $(SRC_DIR)/win_text.c
	@if not exist "$(TEST_BIN_DIR)" mkdir "$(TEST_BIN_DIR)"
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

test: all $(TEST_COMMON_TARGET) $(TEST_UNDO_TARGET) $(TEST_BANNER_TARGET) \
		$(TEST_INI_CONFIG_TARGET) $(TEST_WIN_TEXT_TARGET) $(TEST_UNICODE_CONSOLE_TARGET)
	$(TEST_COMMON_TARGET)
	$(TEST_UNDO_TARGET)
	$(TEST_BANNER_TARGET)
	$(TEST_INI_CONFIG_TARGET)
	$(TEST_WIN_TEXT_TARGET)
	$(TEST_UNICODE_CONSOLE_TARGET)
	$(TARGET) --help >nul

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
	@if exist "$(TARGET)" del /q /f "$(TARGET)"

rebuild: clean
	$(MAKE) all

debug:
	$(MAKE) BUILD=debug all

release: clean
	$(MAKE) BUILD=release all

-include $(DEPS)

.PHONY: all clean rebuild debug release test FORCE
