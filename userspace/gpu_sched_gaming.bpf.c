/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The struct_ops integration is derived from eunomia-bpf/gpu_ext,
 * extension/gpu_sched_set_timeslices.bpf.c at commit
 * 39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8.
 */

#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "gpu_sched_gaming.h"

/* The ELF symbol must differ from the license section's name. */
char program_license[] SEC("license") = "GPL";

const volatile __u64 gaming_cgroup_id = 0;
const volatile __u32 gaming_cgroup_level = 0;
const volatile __u32 gaming_interleave = NV_INTERLEAVE_LEVEL_HIGH;
const volatile __u64 gaming_timeslice_us = 0;
const volatile bool observe_only = false;
const volatile bool automatic_detection = false;
const volatile __u32 clock_ticks_per_second = 100;
const volatile __u32 gaming_user_id = 0;

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, GPU_GAMING_MAX_PROCESSES);
    __type(key, __u32);
    __type(value, struct gpu_gaming_process);
} game_processes SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, GPU_GAMING_STAT_COUNT);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

struct gpu_gaming_selection {
    __u64 timeslice_us;
    __u32 interleave;
    bool request_interleave;
    bool request_timeslice;
};

static __always_inline void increment_statistic(__u32 key)
{
    __u64 *value = bpf_map_lookup_elem(&stats, &key);

    if (value)
        (*value)++;
}

static __always_inline bool select_gaming_task(struct gpu_gaming_selection *selection)
{
    struct gpu_gaming_process *process;
    struct gpu_gaming_process config;
    struct task_struct *task;
    struct task_struct *leader;
    __u64 start_time_nanoseconds;
    __u64 start_time_ticks;
    __u32 process_id;
    __u32 clock_ticks;

    /* Ancestor matching includes Proton children and nested container cgroups. */
    if (gaming_cgroup_id != 0) {
        if (bpf_get_current_ancestor_cgroup_id(gaming_cgroup_level) != gaming_cgroup_id)
            return false;
        selection->interleave = gaming_interleave;
        selection->timeslice_us = gaming_timeslice_us;
        selection->request_interleave = true;
        selection->request_timeslice = gaming_timeslice_us > 0;
        return true;
    }

    if (!automatic_detection || (__u32)bpf_get_current_uid_gid() != gaming_user_id)
        return false;

    clock_ticks = clock_ticks_per_second;
    if (!clock_ticks || clock_ticks > 1000000)
        return false;

    process_id = bpf_get_current_pid_tgid() >> 32;
    process = bpf_map_lookup_elem(&game_processes, &process_id);
    if (!process)
        return false;
    config = *process;

    /* Invalid map records must not become scheduling requests. */
    if (!config.flags ||
        (config.flags & ~(GPU_GAMING_PROCESS_INTERLEAVE | GPU_GAMING_PROCESS_TIMESLICE)))
        return false;
    if ((config.flags & GPU_GAMING_PROCESS_INTERLEAVE) &&
        config.interleave > NV_INTERLEAVE_LEVEL_HIGH)
        return false;
    if ((config.flags & GPU_GAMING_PROCESS_TIMESLICE) &&
        (!config.timeslice_us || config.timeslice_us > 1000000))
        return false;

    /* TGID start time rejects stale identities after a process ID is reused. */
    task = bpf_get_current_task_btf();
    if (!task)
        return false;
    leader = BPF_CORE_READ(task, group_leader);
    if (!leader || BPF_CORE_READ_INTO(&start_time_nanoseconds, leader, start_boottime))
        return false;

    /* Splitting the conversion avoids overflow before matching /proc stat ticks. */
    start_time_ticks = (start_time_nanoseconds / 1000000000ULL) * clock_ticks;
    start_time_ticks += ((start_time_nanoseconds % 1000000000ULL) * clock_ticks) /
                        1000000000ULL;
    if (start_time_ticks != config.start_time_ticks)
        return false;

    selection->interleave = config.interleave;
    selection->timeslice_us = config.timeslice_us;
    selection->request_interleave = (config.flags & GPU_GAMING_PROCESS_INTERLEAVE) != 0;
    selection->request_timeslice = (config.flags & GPU_GAMING_PROCESS_TIMESLICE) != 0;
    return true;
}

SEC("struct_ops/on_task_init")
int BPF_PROG(gaming_task_init, struct nv_gpu_task_init_ctx *context)
{
    struct gpu_gaming_selection selection = {};
    int result;

    if (!context || !select_gaming_task(&selection))
        return 0;

    increment_statistic(GPU_GAMING_STAT_MATCHED);
    if (observe_only)
        return 0;

    /* Setter success records a request; the driver validates it afterwards. */
    if (selection.request_interleave) {
        result = bpf_nv_gpu_set_interleave(context, selection.interleave);
        if (result)
            increment_statistic(GPU_GAMING_STAT_SETTER_ERRORS);
        else
            increment_statistic(GPU_GAMING_STAT_INTERLEAVE_REQUESTED);
    }

    /* An absent override preserves the application's existing timeslice policy. */
    if (selection.request_timeslice) {
        result = bpf_nv_gpu_set_timeslice(context, selection.timeslice_us);
        if (result)
            increment_statistic(GPU_GAMING_STAT_SETTER_ERRORS);
        else
            increment_statistic(GPU_GAMING_STAT_TIMESLICE_REQUESTED);
    }

    return 0;
}

SEC("struct_ops/on_bind")
int BPF_PROG(gaming_bind, struct nv_gpu_bind_ctx *context)
{
    struct gpu_gaming_selection selection = {};

    if (!context || !select_gaming_task(&selection))
        return 0;

    increment_statistic(GPU_GAMING_STAT_BIND_MATCHED);
    if (selection.request_interleave && context->interleave_level == selection.interleave)
        increment_statistic(GPU_GAMING_STAT_BIND_INTERLEAVE_MATCHED);

    return 0;
}

SEC(".struct_ops")
struct nv_gpu_sched_ops gaming_ops = {
    .on_task_init = (void *)gaming_task_init,
    .on_bind = (void *)gaming_bind,
    .on_task_destroy = 0,
};
