# Simple kernel build

K = kernel
BUILD = build

KINCLUDE = $(K)/include
KARCH = $(K)/arch/riscv
KCORE = $(K)/core
KDRV = $(K)/drivers
KLIB = $(K)/lib
KLD = $(K)/ld
KFS = $(K)/fs

CPUS ?= 4
RAM ?= 1536M
LLM_CPUS := 1

U = user
UBUILD = $(BUILD)/user

# Toolchain auto-detect
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-gcc -print-libgcc-file-name > /dev/null 2>&1; \
	then echo "riscv64-unknown-elf-"; \
	elif riscv64-linux-gnu-gcc -print-libgcc-file-name > /dev/null 2>&1; \
	then echo "riscv64-linux-gnu-"; \
	else echo "***"; fi)
endif

ifeq ($(TOOLPREFIX),***)
$(error "Couldn't find a RISC-V toolchain in PATH")
endif

REALCC = $(TOOLPREFIX)gcc
CC = python3 tools/ccwrap.py $(REALCC)
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
GDB = $(TOOLPREFIX)gdb

# QEMU auto-detect
ifndef QEMU
QEMU := $(shell if which qemu-system-riscv64 > /dev/null; \
	then echo qemu-system-riscv64; \
	else echo "***"; fi)
endif

ifeq ($(QEMU),***)
$(error "qemu-system-riscv64 not found in PATH")
endif

QEMUOPTS = -machine virt \
	-bios none \
	-kernel kernel.elf \
	-m $(RAM) \
	-smp $(CPUS) \
	-nographic \
	-global virtio-mmio.force-legacy=false
QEMUOPTS_LLM = -machine virt \
	-bios none \
	-kernel kernel.elf \
	-m $(RAM) \
	-smp $(LLM_CPUS) \
	-nographic \
	-global virtio-mmio.force-legacy=false
QEMUFSOPTS = -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

QEMUGDB = -S -gdb tcp::26000

# C flags
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb
CFLAGS += -mcmodel=medany -mno-relax
CFLAGS += -ffreestanding -fno-common -nostdlib
CFLAGS += -fno-pie -no-pie
CFLAGS += -MMD -MP
CFLAGS += -I$(KINCLUDE)

# Debug logger level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
LOG_LEVEL ?= 3
CFLAGS += -DLOG_LEVEL=$(LOG_LEVEL)

UCFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb
UCFLAGS += -mcmodel=medany -mno-relax
UCFLAGS += -ffreestanding -fno-common -nostdlib
UCFLAGS += -fno-pie -no-pie
UCFLAGS += -MMD -MP
UCFLAGS += -I$(KINCLUDE) -I$(U)
ULDFLAGS = -Wl,--build-id=none
LLMRUN_UCFLAGS = $(UCFLAGS) -O3 -funroll-loops
LLMRUN_ULDFLAGS = $(ULDFLAGS) -Wl,-s

LDFLAGS = -z max-page-size=4096 --no-warn-rwx-segments
COMDBDIR = $(BUILD)/compdb
COMDB = $(BUILD)/compile_commands.json

# Kernel sources
SRCS = \
	$(KARCH)/entry.S \
	$(KARCH)/kernelvec.S \
	$(KARCH)/trampoline.S \
	$(KARCH)/timervec.S \
	$(KARCH)/swtch.S \
	$(KARCH)/start.c \
	$(KDRV)/plic.c \
	$(KDRV)/uart.c \
	$(KDRV)/virtio_disk.c \
	$(KLIB)/string.c \
	$(KLIB)/printf.c \
	$(KCORE)/trap.c \
	$(KCORE)/cpu.c \
	$(KCORE)/intr.c \
	$(KCORE)/spinlock.c \
	$(KCORE)/sleeplock.c \
	$(KCORE)/kalloc.c \
	$(KCORE)/vm.c \
	$(KCORE)/console.c \
	$(KCORE)/file.c \
	$(KCORE)/proc.c \
	$(KCORE)/main.c \
	$(KCORE)/syscall.c \
	$(KCORE)/pipe.c \
	$(KCORE)/bio.c \
	$(KFS)/fs.c \

KOBJS = $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(SRCS)))
KOBJS += $(patsubst %.S,$(BUILD)/%.o,$(filter %.S,$(SRCS)))

UPROGS = \
	init \
	sh \
	hello \
	quiet \
	stressio \
	stsched \
	stressdisk \
	pid \
	uptime \
	sleep \
	killer \
	kill \
	pingpong \
	fstat \
	forktest \
	zombie \
	echo \
	cat \
	wc \
	grep \
	ls \
	find \
	xargs \
	fstest \
	mkdir \
	rm \
	ln \
	touch \
	logtest \
	sid \
	test_hello_id \
	runscript \
	rs_status \
	gpu_stats \
	gpudemo 
