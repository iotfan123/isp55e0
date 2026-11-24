CC ?= gcc
WITHOUT_USB ?= 0
CFLAGS ?= -O2 -Wall -Werror

# Detect the compiler target, including cross builds; do not rely on the host OS.
TOOLCHAIN_TARGET := $(shell $(CC) -dumpmachine)
ifneq (,$(or $(findstring mingw,$(TOOLCHAIN_TARGET)),$(findstring windows,$(TOOLCHAIN_TARGET))))
PLATFORM = windows
EXEEXT = .exe
else
PLATFORM = posix
EXEEXT =
endif

TARGET = isp55e0$(EXEEXT)
BUILD_DIR = build/$(PLATFORM)-usb$(WITHOUT_USB)
SRCS = isp55e0.c transfer.c transfer_uart.c transfer_uart_$(PLATFORM).c

ifeq ($(WITHOUT_USB),0)
SRCS += transfer_usb_libusb.c
LDLIBS += -lusb-1.0
endif
CONFIG_CPPFLAGS = -DWITHOUT_USB=$(WITHOUT_USB)

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)
TEST_TARGET = $(BUILD_DIR)/test_transfer_uart$(EXEEXT)

.PHONY: all clean chips test FORCE
all: $(TARGET)

# Relink when switching USB/platform variants, even if that variant's objects
# are older than the executable left by the previous build.
$(TARGET): $(OBJS) FORCE
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_TARGET): tests/test_transfer_uart.c transfer_uart.c transfer_uart.h transfer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_CPPFLAGS) $(CFLAGS) -I. tests/test_transfer_uart.c transfer_uart.c -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

chips:
	./parse_wcfg.py > chips.h

clean:
	rm -rf build
	rm -f isp55e0 isp55e0.exe *.o

-include $(DEPS)
