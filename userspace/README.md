# GPU application scheduling profiles

This userspace policy targets the three-callback gpu_ext interface in the
NVIDIA 615.71.09 port. It adds native, Wine, and Proton application discovery,
six scheduling modes, and systemd integration. The driver does not need another
source change for this policy.

These are GPU runlist scheduling presets. They do not change CPU scheduling,
clocks, power limits, memory eviction, graphics settings, or network latency.
The public package has not been compiled or attached to a driver as part of
packaging. BPF verifier acceptance and performance remain **Verification required**.
See [validation status](../docs/validation.md) for the verification boundary.

## Modes

The table gives the requested interleave level for each application class.
High, normal, and low correspond to NVIDIA levels 2, 1, and 0. All presets retain
the native timeslice. High interleave requests more frequent placement in a
contended runlist; it is not an FPS target or a latency guarantee.

| Mode | Game | Desktop compositor | Interactive app | Compute/AI | Background |
| --- | --- | --- | --- | --- | --- |
| gaming | High | High | Normal | Low | Low |
| workstation | Normal | High | High | Normal | Low |
| low-latency | High | High | High | Low | Low |
| ai | Normal | High | Normal | High | Low |
| server | Low | Normal | Normal | High | Normal |

`auto` is the default. Each discovery pass selects `gaming` when a classified
game process exists, otherwise `ai` when a classified compute process exists,
otherwise `workstation`. Selection is based on process presence, not measured
GPU utilization, window focus, or input activity. Explicit high/normal/low rules
do not themselves trigger automatic class selection.

## Build and install

The existing `clang`, `bpftool`, C compiler, libbpf development files and
`pkg-config` are required. The build uses `/sys/kernel/btf/vmlinux` to generate
the kernel type header. It does not download or install dependencies.

From this directory:

```sh
make CC=clang CLANG=clang
make test CC=clang
./gpu_sched_gaming --auto-detect --uid "$(id -u)" \
    --rules applications.conf --mode auto --check-config
sudo ./install.sh
sudo systemctl enable --now "gpu-ext@$(id -u).service"
```

The installer copies the binary, mode command and service files, preserves
existing application rules and saved mode files, and reloads systemd. It does
not start or enable the service. Only the final installation requires sudo.

The service operates in the host PID, cgroup, and time namespaces. It targets
the specified non-root UID. Other users and root processes remain unaffected.
Only **one** instance or other GPU scheduler policy can attach at a time because
the driver has one global `nv_gpu_sched_ops` slot. The service retries every
five seconds if the NVIDIA BTF or interface is unavailable. Do not enable
multiple UID instances simultaneously.

On the kernel examined during development, libbpf's module BTF enumeration
and descriptor lookup require
`CAP_SYS_ADMIN` in addition to the BPF loading capabilities. The service includes
it for this reason. Without it, libbpf can report that
`bpf_struct_ops_nv_gpu_sched_ops` is missing even when the type is present in
`/sys/kernel/btf/nvidia`.

## Switch modes

```sh
sudo gpu-ext-mode auto
sudo gpu-ext-mode gaming
sudo gpu-ext-mode workstation
sudo gpu-ext-mode low-latency
sudo gpu-ext-mode ai
sudo gpu-ext-mode server
gpu-ext-mode status
```

Under sudo, the target UID defaults to `SUDO_UID`. Root sessions must specify
the intended UID explicitly, for example `gpu-ext-mode server 1000`.
The selected mode is saved in `/etc/gpu-ext/modes/UID.conf`. The command restarts
the instance and waits for readiness. Readiness is sent only after successful
BPF attachment and initial discovery. Runtime application and effective-mode
changes appear in the service journal:

```sh
journalctl -fu "gpu-ext@$(id -u).service"
sudo bpftool struct_ops show
```

## Detection and application rules

Automatic classification uses known Steam game metadata, executable locations
under `steamapps/common` or the user's `Games`/`games` directory, Wine/Proton
process information, and known compositor/browser/media/compute names.
Runtime tools and Wine support processes are excluded. Standalone Wine software
without game metadata is classified as interactive, not assumed to be a game.
Python processes are not globally treated as AI jobs.

Unknown native applications retain the driver's normal behavior. Any launcher
or application can be assigned a class through an environment marker:

```sh
GPU_EXT_PROFILE=game /opt/my-game/game
GPU_EXT_PROFILE=compute python3 inference.py
GPU_EXT_PROFILE=interactive /opt/design-app/application
GPU_EXT_PROFILE=background ffmpeg ...
GPU_EXT_PROFILE=ignore /opt/application
```

The marker is inherited by descendants. It influences the next discovery pass;
it does not synchronously register a process before its first GPU operation.
For Steam, Lutris or another launcher's command prefix, `gpu-ext-run` supplies
the game marker and a systemd user scope. The Steam launch option is:

```text
gpu-ext-run %command%
```

The scope preserves the calling environment and literal argument boundaries.
This wrapper also uses polling for detection and does not eliminate the initial
context race.

Edit `/etc/gpu-ext/applications.conf` for explicit rules, then restart the
service. The first matching rule wins, followed by the environment marker,
then automatic detection. Classes and fixed priorities are both supported:

```text
game comm MyGame.exe
game exe /opt/native-game/bin/game
compute exe /opt/inference/bin/worker
interactive comm blender
background comm ffmpeg
ignore directory /home/example/Games/test-tools
```

Selectors are exact executable path, exact `comm`, or an executable directory
prefix with a path-component boundary. Paths may contain spaces. There is no
shell expansion or wildcard matching. Use canonical paths. `comm` is at most
15 bytes. For Wine, `exe` identifies the Linux Wine loader; use the observed
Windows process `comm` when selecting a particular application.

## Application timing and rollback

The driver invokes this policy when a GPU channel group is created. It does
not reconfigure already existing contexts. Discovery runs every 250 ms and
caches unchanged process environment metadata for up to two seconds. A game
can create its initial context before discovery registers it. Automatic mode
changes update future decisions, not current GPU allocations. Native control
requests can also change scheduling settings later.

For deterministic matching at context creation, stop the automatic instance
and use explicit cgroup mode before launching a workload:

```sh
sudo systemctl stop "gpu-ext@$(id -u).service"
gaming_cgroup=$(./prepare-slice.sh)
sudo ./gpu_sched_gaming --cgroup "$gaming_cgroup" --interleave high
```

Launch the game with `gpu-ext-run` in another terminal or through Steam. The
policy matches the pre-existing slice and its descendants without discovery.
This is a manual alternative; it cannot run alongside the automatic instance.

Stop the service to detach, then restart affected GPU applications to recreate
contexts with native defaults:

```sh
sudo systemctl disable --now "gpu-ext@$(id -u).service"
```

For comparison, run the loader manually with `--observe-only` after stopping
the service. It collects counters without requesting scheduling changes.
Compare identical workloads after recreating GPU contexts: frame-time p95/p99,
1% lows, desktop responsiveness, and competing job throughput. No benchmark
result or universal performance improvement is claimed.

## Validation status and sources

See [validation status](../docs/validation.md). The supplied C fixtures cover
parsing, classification, mode selection, and overrides. Running `make test`
compiles those fixtures.

- [Compatible gpu_ext scheduler ABI](https://github.com/eunomia-bpf/gpu_ext/blob/39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8/extension/gpu_sched_set_timeslices.h).
- [NVIDIA interleave semantics](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/615.71.09/src/common/sdk/nvidia/inc/ctrl/ctrla06c.h).
- [systemd scope and environment semantics](https://github.com/systemd/systemd/blob/main/man/systemd-run.xml).
