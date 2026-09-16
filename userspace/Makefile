# SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

OUTPUT := .output
CLANG ?= clang
BPFTOOL ?= bpftool
PKG_CONFIG ?= pkg-config
VMLINUX_BTF ?= /sys/kernel/btf/vmlinux
LIBBPF_CFLAGS := $(shell $(PKG_CONFIG) --cflags libbpf)
LIBBPF_LIBS := $(shell $(PKG_CONFIG) --libs libbpf)

.PHONY: all clean check-tools test refresh-kernel-types
all: gpu_sched_gaming

check-tools:
	@$(PKG_CONFIG) --exists 'libbpf >= 1.2'
	@command -v $(CLANG) >/dev/null
	@command -v $(BPFTOOL) >/dev/null
	@test -r $(VMLINUX_BTF)

$(OUTPUT):
	mkdir -p $@

# Virtual BTF timestamps are unreliable, so retain the header when types match.
$(OUTPUT)/vmlinux.h: refresh-kernel-types | $(OUTPUT) check-tools
	$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@.temp
	@if cmp -s $@.temp $@; then $(RM) $@.temp; else mv $@.temp $@; fi

$(OUTPUT)/gpu_sched_gaming.bpf.o: gpu_sched_gaming.bpf.c gpu_sched_gaming.h $(OUTPUT)/vmlinux.h | check-tools
	$(CLANG) -g -O2 -target bpf -I$(OUTPUT) $(LIBBPF_CFLAGS) -c $< -o $@

$(OUTPUT)/gpu_sched_gaming.skel.h: $(OUTPUT)/gpu_sched_gaming.bpf.o
	$(BPFTOOL) gen skeleton $< name gpu_sched_gaming_bpf > $@.temp
	mv $@.temp $@

gpu_sched_gaming: gpu_sched_gaming.c gaming_processes.c gaming_processes.h gpu_sched_gaming.h $(OUTPUT)/gpu_sched_gaming.skel.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -O2 -g -Wall -Wextra -I$(OUTPUT) $(LIBBPF_CFLAGS) \
	  gpu_sched_gaming.c gaming_processes.c $(LDFLAGS) $(LIBBPF_LIBS) -o $@

$(OUTPUT)/gaming_processes_test: gaming_processes_test.c gaming_processes.c gaming_processes.h gpu_sched_gaming.h | $(OUTPUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -O2 -g -Wall -Wextra $(LIBBPF_CFLAGS) \
	  gaming_processes_test.c $(LDFLAGS) $(LIBBPF_LIBS) -o $@

test: $(OUTPUT)/gaming_processes_test
	$(OUTPUT)/gaming_processes_test
	sh test-launcher.sh

clean:
	$(RM) -r $(OUTPUT) gpu_sched_gaming
