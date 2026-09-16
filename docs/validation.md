# Validation boundary

The public package is a source distribution. Preparing its patches does not
establish build compatibility, successful BPF attachment, or performance gains.

## Packaging checks

Completed during preparation on 2026-09-16:

| Check | Result |
| --- | --- |
| Baseline checkout | Official commit `61dcc93722ecb418bb5f2e00923f05b4b8051dd1` confirmed |
| Ordered application | All three patches passed `git apply --check --index --whitespace=error-all` and applied |
| Reverse restoration | All patches reversed; validation checkout returned to a clean index and worktree |
| Apply helper | Applied and staged the complete series successfully |
| Rejection guards | Dirty checkout and unrelated clean Git baseline rejected without modifying them |
| Shell checks | ShellCheck passed the patch scripts, policy scripts and mode command |
| Launcher fixture | Argument boundaries, marker, UID rejection and slice preparation passed with mock commands |
| Package inventory | No compiled policy, generated BTF header, private development paths or full vendor tree included |
| Independent review | Native RM include/build wiring, script guards, provenance and documentation reviewed statically |

The driver series modifies 28 paths. These results cover source packaging and
static checks only; they are not a driver build or runtime acceptance result.

The checks can be repeated against a pristine baseline checkout:

```sh
scripts/check-series.sh /path/to/nvidia-615.71.09
```

This verifies source application, not compilation or kernel execution.

## Remaining verification

The following have not been performed for this public package:

- Compilation and linking from the patched official NVIDIA checkout.
- Compilation of the bundled BPF policy and execution of its C test fixtures.
- Driver installation, boot, module load/unload, and DKMS integration.
- BPF CO-RE relocation, verifier acceptance, attachment, and detachment.
- Effective scheduling or memory-policy behavior on GPU workloads.
- Comparative performance, frame-time, latency, stability, or power testing.

Development included user-reported build attempts and runtime troubleshooting
in a separate installer-extracted driver layout. Those observations do not
validate this public upstream-layout patch series. In particular, changing the
service capability set does not establish that a subsequent attachment passed.

## Target-system acceptance

Build the driver and userspace components with the target kernel and toolchain.
Check module versions and BTF, then test attachment and detachment during a
maintenance window. Test native, Wine, and Proton workloads as applicable, as
well as mode persistence and systemd restart behavior. Recreate application GPU
contexts between policy comparisons.

Use identical workloads and compare frame-time p95/p99, 1% lows, desktop
responsiveness, and competing compute throughput. Include an unmodified-driver
baseline and an observe-only policy run. Report hardware, kernel, driver,
userspace libraries, policy mode, and workload settings with results.

Human review and validation of the AI-assisted changes remain required. No
security audit, universal compatibility, or universal optimization result is
claimed.
