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

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

# Fails the build if we accidentally pick up a symbol newer than the oldest
# runtime we care about (Steam Runtime 3 "sniper", glibc 2.31).
check: $(TARGET)
	@bad=$$(objdump -T $(TARGET) | grep -oE 'GLIBC_2\.(3[2-9]|[4-9][0-9])' | sort -u); \
	if [ -n "$$bad" ]; then \
		echo "too-new glibc symbols: $$bad"; exit 1; \
	fi; \
	echo "glibc requirements OK: $$(objdump -T $(TARGET) | grep -oE 'GLIBC_[0-9.]+' | sort -u | tr '\n' ' ')"

clean:
	rm -f $(TARGET)

.PHONY: all check clean
