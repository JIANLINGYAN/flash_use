CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -O0 -I./core -I./port_win -I./app -DDEBUG

# Core source files
CORE_SOURCES = core/fast_flash_core.c core/fast_flash_log.c
CORE_HEADERS = core/fast_flash_types.h core/fast_flash_core.h core/fast_flash_log.h

# Windows port files
PORT_SOURCES = port_win/flash_adapter_win.c
PORT_HEADERS = port_win/flash_adapter_win.h

# Application layer files - New Fast Flash based
APP_SOURCES = \
	app/health_data_manager.c \
	app/rs_motion_platform_adapter.c \
	app/rs_motion_storage_adapter.c \
	app/rs_motion_manager_new.c \
	app/rs_motion_win_platform_adapter.c

APP_SOURCES_WIN = app/rs_motion_win_platform_adapter.c

APP_HEADERS = \
	app/health_data_manager.h \
	app/rs_motion_platform_adapter.h \
	app/rs_motion_storage_adapter.h \
	app/rs_motion_manager_new.h \
	app/rs_motion_win_platform_adapter.h

# Other RS Motion files (preserved)
RS_MOTION_HEADERS = \
	app/rs_motion_adapter.h \
	app/rs_motion_fast_flashdb.h \
	app/rs_motion_flashdb.h \
	app/rs_motion_manager.h \
	app/rs_motion.h

# Test files
CORE_TEST_SOURCES = port_win/test_fast_flash.c
HEALTH_TEST_SOURCES = health_tests/test_health_manager.c
RS_MOTION_TEST_SOURCES = app/test_rs_motion_win.c

# Target definitions
TARGET = fast_flash_test
HEALTH_TEST = health_test
RS_MOTION_TEST = rs_motion_test

# All sources for each target
CORE_TEST_SOURCES_FULL = $(CORE_SOURCES) $(PORT_SOURCES) $(CORE_TEST_SOURCES)
CORE_TEST_HEADERS_FULL = $(CORE_HEADERS) $(PORT_HEADERS)

HEALTH_TEST_SOURCES_FULL = $(CORE_SOURCES) $(PORT_SOURCES) app/health_data_manager.c $(HEALTH_TEST_SOURCES)
HEALTH_TEST_HEADERS_FULL = $(CORE_HEADERS) $(PORT_HEADERS) app/health_data_manager.h

RS_MOTION_TEST_SOURCES_FULL = $(CORE_SOURCES) $(PORT_SOURCES) $(APP_SOURCES) $(RS_MOTION_TEST_SOURCES)
RS_MOTION_TEST_HEADERS_FULL = $(CORE_HEADERS) $(PORT_HEADERS) $(APP_HEADERS) $(RS_MOTION_HEADERS)

# Default target - build all tests
all: $(TARGET) $(HEALTH_TEST) $(RS_MOTION_TEST)

# Build the core test executable
$(TARGET): $(CORE_TEST_SOURCES_FULL) $(CORE_TEST_HEADERS_FULL)
	$(CC) $(CFLAGS) -o $(TARGET) $(CORE_TEST_SOURCES_FULL)
	@echo "Core test built successfully: $(TARGET).exe"

# Build the health test executable
$(HEALTH_TEST): $(HEALTH_TEST_SOURCES_FULL) $(HEALTH_TEST_HEADERS_FULL)
	$(CC) $(CFLAGS) -o $(HEALTH_TEST) $(HEALTH_TEST_SOURCES_FULL)
	@echo "Health test built successfully: $(HEALTH_TEST).exe"

# Build the RS Motion test executable
$(RS_MOTION_TEST): $(RS_MOTION_TEST_SOURCES_FULL) $(RS_MOTION_TEST_HEADERS_FULL)
	$(CC) $(CFLAGS) -o $(RS_MOTION_TEST) $(RS_MOTION_TEST_SOURCES_FULL)
	@echo "RS Motion test built successfully: $(RS_MOTION_TEST).exe"

# Clean build artifacts
clean:
	@if exist $(TARGET).exe del $(TARGET).exe
	@if exist $(HEALTH_TEST).exe del $(HEALTH_TEST).exe
	@if exist $(RS_MOTION_TEST).exe del $(RS_MOTION_TEST).exe
	@if exist flash_simulation.bin del flash_simulation.bin
	@if exist *.o del *.o
	@echo "Clean completed"

# Run core tests
test: $(TARGET)
	@echo "Running core tests..."
	.\$(TARGET).exe

# Run health tests
test-health: $(HEALTH_TEST)
	@echo "Running health tests..."
	.\$(HEALTH_TEST).exe

# Run RS Motion tests
test-rs-motion: $(RS_MOTION_TEST)
	@echo "Running RS Motion tests..."
	.\$(RS_MOTION_TEST).exe

# Run all tests
test-all: $(TARGET) $(HEALTH_TEST) $(RS_MOTION_TEST)
	@echo "Running core tests..."
	.\$(TARGET).exe
	@echo ""
	@echo "Running health tests..."
	.\$(HEALTH_TEST).exe
	@echo ""
	@echo "Running RS Motion tests..."
	.\$(RS_MOTION_TEST).exe

# Debug build (same as default since DEBUG is already in CFLAGS)
debug: $(TARGET) $(HEALTH_TEST) $(RS_MOTION_TEST)

# Build individual targets
build-core: $(TARGET)
	@echo "Core test built"

build-health: $(HEALTH_TEST)
	@echo "Health test built"

build-rs-motion: $(RS_MOTION_TEST)
	@echo "RS Motion test built"

# Build individual components
core:
	$(CC) $(CFLAGS) -c $(CORE_SOURCES)
	@echo "Core library compiled"

port:
	$(CC) $(CFLAGS) -c $(PORT_SOURCES)
	@echo "Windows port compiled"

app:
	$(CC) $(CFLAGS) -c $(APP_SOURCES)
	@echo "Application layer compiled"

rs-motion-libs:
	$(CC) $(CFLAGS) -c $(APP_SOURCES)
	@echo "RS Motion libraries compiled"

# Build libraries
libs:
	@echo "Building static libraries..."
	@echo "Building fast_flash_core.lib..."
	ar rcs fast_flash_core.lib $(CORE_SOURCES:.c=.o)
	@echo "Building rs_motion.lib..."
	ar rcs rs_motion.lib $(APP_SOURCES:.c=.o)
	@echo "Libraries built"

# CMake integration
cmake:
	@echo "Using CMake for cross-platform builds..."
	@if not exist build mkdir build
	@cd build && cmake .. -G "MinGW Makefiles" && cmake --build .

cmake-clean:
	@if exist build rmdir /s /q build
	@echo "CMake build directory cleaned"

# Help target
help:
	@echo "Available targets:"
	@echo "  all                - Build all tests (default)"
	@echo "  build-core         - Build core test only"
	@echo "  build-health       - Build health test only"
	@echo "  build-rs-motion    - Build RS Motion test only"
	@echo "  test               - Run core tests"
	@echo "  test-health        - Run health tests"
	@echo "  test-rs-motion     - Run RS Motion tests"
	@echo "  test-all           - Run all tests"
	@echo "  clean              - Remove build artifacts"
	@echo "  core               - Compile core library only"
	@echo "  port               - Compile Windows port only"
	@echo "  app                - Compile application layer only"
	@echo "  rs-motion-libs     - Compile RS Motion libraries"
	@echo "  libs               - Build static libraries"
	@echo "  cmake              - Use CMake for building"
	@echo "  cmake-clean        - Clean CMake build directory"
	@echo "  debug              - Build debug versions"
	@echo "  help               - Show this help"

.PHONY: all clean test test-health test-rs-motion test-all debug core port app build-core build-health build-rs-motion rs-motion-libs libs cmake cmake-clean help