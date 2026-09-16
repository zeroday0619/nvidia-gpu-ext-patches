# Userspace setup and operation

First build and load the patched driver as described in the
[driver guide](driver.md). The following commands run from this patch repository.
The build requires Clang with a BPF target, `bpftool`, a C compiler,
`pkg-config`, libbpf development files (at least 1.2), and readable kernel BTF.
Actual module struct_ops compatibility must be verified on the target kernel.

```sh
make -C userspace CC=clang
make -C userspace test CC=clang
./userspace/gpu_sched_gaming --auto-detect --uid "$(id -u)" \
  --rules userspace/applications.conf --mode auto --check-config
sudo userspace/install.sh
sudo systemctl enable --now "gpu-ext@$(id -u).service"
```

Run these commands as the intended non-root desktop or workload user. The
installer reloads systemd and preserves existing rules and mode files. It does
not enable or start the service. The final command starts the instance for the
calling user's UID. Only one instance may run because the scheduler interface
is global.

## Modes

```sh
sudo gpu-ext-mode auto
sudo gpu-ext-mode gaming
sudo gpu-ext-mode workstation
sudo gpu-ext-mode low-latency
sudo gpu-ext-mode ai
sudo gpu-ext-mode server
gpu-ext-mode status
```

`auto` selects gaming when a classified game process exists, otherwise AI when
a classified compute process exists, otherwise workstation. It does not measure
GPU utilization, focus, or activity. See the [policy reference](../userspace/README.md)
for the per-class priority table.

Detection covers known native applications and Wine/Proton processes without
requiring Steam. It cannot infer every application's purpose. Unknown programs
can use `/etc/gpu-ext/applications.conf` rules or a launch marker:

```sh
GPU_EXT_PROFILE=game /opt/my-game/game
GPU_EXT_PROFILE=compute python3 inference.py
GPU_EXT_PROFILE=interactive /opt/my-application/application
```

Discovery runs every 250 ms; unchanged environment metadata is cached for up
to two seconds. Decisions apply when new GPU channel groups are created.
Existing contexts are not reconfigured, and initial context creation can race
with discovery. Restart the workload when comparing policies. These presets
do not tune CPU scheduling, power limits, clocks, or graphics settings.

## Status and troubleshooting

```sh
systemctl status "gpu-ext@$(id -u).service" --no-pager -l
journalctl -u "gpu-ext@$(id -u).service" -n 60 --no-pager
sudo bpftool struct_ops show
```

The service includes `CAP_SYS_ADMIN` because module BTF enumeration and lookup
required it on the kernel examined during development. A missing
`bpf_struct_ops_nv_gpu_sched_ops` error can indicate missing/incompatible module
BTF or insufficient capabilities; it is not proof that the driver hooks are
absent. Check the loaded driver, module BTF, installed service unit, and libbpf
compatibility before changing the policy.

The service retries failures every five seconds. Stop it while diagnosing a
persistent attachment failure:

```sh
sudo systemctl stop "gpu-ext@$(id -u).service"
```

For rollback, disable the service and restart affected applications so that
new GPU contexts receive native defaults:

```sh
sudo systemctl disable --now "gpu-ext@$(id -u).service"
```

Detaching does not restore settings already applied to existing contexts. See
the [full policy reference](../userspace/README.md) for rule matching, manual
cgroup operation, observe-only comparisons, and configuration details.
