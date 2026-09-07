CC      ?= cc
CFLAGS  ?= -O2
LDFLAGS ?=

# -std=gnu11 with _DEFAULT_SOURCE rather than _GNU_SOURCE on purpose: _GNU_SOURCE
# makes glibc redirect sscanf and strtof to __isoc23_* symbols that require
# GLIBC_2.38, which will not load inside the Steam Linux Runtime (glibc 2.31).
override CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -fPIC -Wall -Wextra
override LDFLAGS += -shared

TARGET := libtinyeden_fov.so
SRC    := tinyeden_fov.c

# The Windows build ships as a winmm.dll proxy. The game statically imports
# winmm, so it loads before the engine and the four functions it actually
# calls are forwarded to the real one in System32.
WINCC   ?= x86_64-w64-mingw32-gcc
WINTARGET := winmm.dll
WINSRC    := tinyeden_fov_win.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

windows: $(WINTARGET)

$(WINTARGET): $(WINSRC)
	$(WINCC) -O2 -std=gnu11 -Wall -Wextra -shared -o $@ $< \
		-static-libgcc -lkernel32

# Fails the build if we accidentally pick up a symbol newer than the oldest
# runtime we care about (Steam Runtime 3 "sniper", glibc 2.31).
check: $(TARGET)
	@bad=$$(objdump -T $(TARGET) | grep -oE 'GLIBC_2\.(3[2-9]|[4-9][0-9])' | sort -u); \
	if [ -n "$$bad" ]; then \
		echo "too-new glibc symbols: $$bad"; exit 1; \
	fi; \
	echo "glibc requirements OK: $$(objdump -T $(TARGET) | grep -oE 'GLIBC_[0-9.]+' | sort -u | tr '\n' ' ')"

# The forwarded winmm entry points have to be exported under their real
# names or the game fails to start with a missing-import error.
wincheck: $(WINTARGET)
	@for f in timeBeginPeriod timeEndPeriod timeGetTime waveOutGetNumDevs; do \
		x86_64-w64-mingw32-objdump -p $(WINTARGET) | grep -qw $$f || \
			{ echo "missing export: $$f"; exit 1; }; \
	done; \
	echo "winmm exports OK"

clean:
	rm -f $(TARGET) $(WINTARGET)

.PHONY: all windows check wincheck clean
