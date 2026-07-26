# Build cecvold for an LG webOS TV.
#
# webOS TV userspace on this generation is almost always 32-bit ARM (armv7l).
# Confirm on the TV before building:   uname -m        (expect armv7l)
#                                      file /bin/sh
#
# 32-bit ARM hard-float (default):
#   Debian/Ubuntu toolchain:  sudo apt install gcc-arm-linux-gnueabihf
CC      ?= arm-linux-gnueabihf-gcc
CFLAGS  ?= -O2 -Wall -Wextra -static
TARGET   = cecvold

$(TARGET): cecvold.c
	$(CC) $(CFLAGS) -o $(TARGET) cecvold.c
	@echo "built: $$( file $(TARGET) )"

# If 'uname -m' on the TV reports aarch64 instead, build with:
#   make CC=aarch64-linux-gnu-gcc
# (Debian/Ubuntu: sudo apt install gcc-aarch64-linux-gnu)

clean:
	rm -f $(TARGET)

.PHONY: clean
