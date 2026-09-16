/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The scheduler ABI is derived from eunomia-bpf/gpu_ext,
 * extension/gpu_sched_set_timeslices.h at commit
 * 39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8.
 */

#ifndef GPU_SCHED_GAMING_H
#define GPU_SCHED_GAMING_H

/* These layouts match the three-callback NVIDIA 615.71.09 port. */
struct nv_gpu_task_init_ctx {
    __u64 tsg_id;
    __u32 engine_type;
    __u64 default_timeslice;
    __u32 default_interleave;
    __u32 runlist_id;
};

struct nv_gpu_bind_ctx {
    __u64 tsg_id;
    __u32 runlist_id;
    __u32 channel_count;
    __u64 timeslice_us;
    __u32 interleave_level;
    __u32 allow;
};

struct nv_gpu_task_destroy_ctx {
    __u64 tsg_id;
};

struct nv_gpu_sched_ops {
    int (*on_task_init)(struct nv_gpu_task_init_ctx *ctx);
    int (*on_bind)(struct nv_gpu_bind_ctx *ctx);
    int (*on_task_destroy)(struct nv_gpu_task_destroy_ctx *ctx);
};

_Static_assert(__builtin_offsetof(struct nv_gpu_task_init_ctx, engine_type) == 8,
               "task-init engine_type ABI");
_Static_assert(__builtin_offsetof(struct nv_gpu_task_init_ctx, default_timeslice) == 16,
               "task-init default_timeslice ABI");
_Static_assert(__builtin_offsetof(struct nv_gpu_task_init_ctx, runlist_id) == 28,
               "task-init runlist_id ABI");
_Static_assert(sizeof(struct nv_gpu_task_init_ctx) == 32,
               "task-init input ABI size");
_Static_assert(__builtin_offsetof(struct nv_gpu_bind_ctx, timeslice_us) == 16,
               "bind timeslice_us ABI");
_Static_assert(__builtin_offsetof(struct nv_gpu_bind_ctx, interleave_level) == 24,
               "bind interleave_level ABI");
_Static_assert(sizeof(struct nv_gpu_bind_ctx) == 32, "bind input ABI size");
_Static_assert(sizeof(struct nv_gpu_task_destroy_ctx) == 8,
               "task-destroy input ABI size");
_Static_assert(sizeof(struct nv_gpu_sched_ops) == 24, "scheduler callbacks ABI size");

#define NV_INTERLEAVE_LEVEL_LOW    0
#define NV_INTERLEAVE_LEVEL_MEDIUM 1
#define NV_INTERLEAVE_LEVEL_HIGH   2

#define GPU_GAMING_MAX_PROCESSES 4096
#define GPU_GAMING_PROCESS_INTERLEAVE 1U
#define GPU_GAMING_PROCESS_TIMESLICE  2U

struct gpu_gaming_process {
    __u64 start_time_ticks;
    __u64 application_id;
    __u32 interleave;
    __u32 flags;
    __u64 timeslice_us;
};

_Static_assert(sizeof(struct gpu_gaming_process) == 32, "process policy map ABI size");
_Static_assert(__builtin_offsetof(struct gpu_gaming_process, timeslice_us) == 24,
               "process policy timeslice ABI");

enum gpu_gaming_stat {
    GPU_GAMING_STAT_MATCHED = 0,
    GPU_GAMING_STAT_INTERLEAVE_REQUESTED,
    GPU_GAMING_STAT_TIMESLICE_REQUESTED,
    GPU_GAMING_STAT_SETTER_ERRORS,
    GPU_GAMING_STAT_BIND_MATCHED,
    GPU_GAMING_STAT_BIND_INTERLEAVE_MATCHED,
    GPU_GAMING_STAT_COUNT,
};

#ifndef BPF_NO_KFUNC_PROTOTYPES
#ifndef __ksym
#define __ksym __attribute__((section(".ksyms")))
#endif
#ifndef __weak
#define __weak __attribute__((weak))
#endif

extern int bpf_nv_gpu_set_timeslice(struct nv_gpu_task_init_ctx *context,
                                   __u64 timeslice_us) __weak __ksym;
extern int bpf_nv_gpu_set_interleave(struct nv_gpu_task_init_ctx *context,
                                    __u32 interleave_level) __weak __ksym;
#endif

#endif
