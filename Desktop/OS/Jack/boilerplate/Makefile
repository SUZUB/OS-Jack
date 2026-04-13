# OS-Jackfruit — Master Makefile
# Builds: engine (user-space), monitor.ko (kernel module),
#         cpu_hog, io_pulse, memory_hog (workload binaries)

KERNEL_VER  := $(shell uname -r)
KERNEL_DIR  := /lib/modules/$(KERNEL_VER)/build

CC          := gcc
CFLAGS      := -Wall -Wextra -Wpedantic -std=c11 -D_GNU_SOURCE
LDFLAGS     := -lpthread

# User-space targets
USER_BINS   := engine cpu_hog io_pulse memory_hog

# Kernel module
obj-m       := monitor.o

.PHONY: all clean userspace kmod check-headers

all: check-headers userspace kmod

# ── Header check ──────────────────────────────────────────────────────────────
check-headers:
	@if [ ! -d "$(KERNEL_DIR)" ]; then \
		echo "[ERROR] Kernel headers not found: $(KERNEL_DIR)"; \
		echo "        Run: sudo apt install linux-headers-$(KERNEL_VER)"; \
		exit 1; \
	fi

# ── User-space binaries ───────────────────────────────────────────────────────
userspace: $(USER_BINS)

engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o $@ $<

io_pulse: io_pulse.c
	$(CC) $(CFLAGS) -o $@ $<

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) -o $@ $<

# ── Kernel module ─────────────────────────────────────────────────────────────
kmod: check-headers
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean 2>/dev/null || true
	rm -f $(USER_BINS)
	rm -f *.o *.ko *.mod *.mod.c *.symvers *.order
	rm -f .*.cmd
	rm -rf .tmp_versions
