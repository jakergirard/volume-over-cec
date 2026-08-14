# Build cecvold for LG webOS TVs.
#
# The ipk is universal: it ships both a 64-bit (aarch64) and a 32-bit (armv7l)
# static binary, and app/cecvold (a tiny sh launcher) execs the one matching
# `uname -m` at runtime.
#
# Toolchains (Debian/Ubuntu):
#   sudo apt install gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf
CFLAGS ?= -O2 -Wall -Wextra -static

all: cecvold.aarch64 cecvold.armv7l

cecvold.aarch64: cecvold.c
	aarch64-linux-gnu-gcc $(CFLAGS) -o $@ cecvold.c
	@echo "built: $$( file $@ )"

cecvold.armv7l: cecvold.c
	arm-linux-gnueabihf-gcc $(CFLAGS) -o $@ cecvold.c
	@echo "built: $$( file $@ )"

# Stage both binaries next to the launcher for ares-package.
app: all
	cp cecvold.aarch64 cecvold.armv7l app/
	chmod +x app/cecvold app/cecvold.aarch64 app/cecvold.armv7l

clean:
	rm -f cecvold.aarch64 cecvold.armv7l app/cecvold.aarch64 app/cecvold.armv7l

.PHONY: all app clean
