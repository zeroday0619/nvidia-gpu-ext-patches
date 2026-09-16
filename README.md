# NVIDIA gpu_ext patch set

Source patches for the NVIDIA **615.71.09** open GPU kernel modules, plus a
userspace GPU scheduling policy with native, Wine, and Proton discovery,
systemd integration, and six selectable modes.

This is an experimental, unofficial port of
[gpu_ext](https://github.com/eunomia-bpf/gpu_ext). It is not an NVIDIA-supported
release. The presets express scheduling preferences; no universal performance
improvement is claimed. Review the [validation boundary](docs/validation.md)
before building or deploying.

## Contents

| Path | Purpose |
| --- | --- |
| `patches/615.71.09/` | Ordered driver patches and exact baseline metadata |
| `scripts/check-series.sh` | Check the series against the pristine baseline |
| `scripts/apply-series.sh` | Check, then apply the series to that baseline |
| `userspace/` | BPF policy, process classification, mode CLI, tests, and systemd files |
| [Driver guide](docs/driver.md) | Baseline, prerequisites, patch application, and manual build |
| [Usage guide](docs/usage.md) | Userspace installation and operating modes |
| [Policy reference](userspace/README.md) | Detection, rules, timing, and rollback |
| [Validation](docs/validation.md) | Completed checks and remaining verification |
| [Publishing](docs/publishing.md) | Connect the prepared local repository to a user-created remote |

## Get started

Use a fresh checkout of the official driver repository. Run these commands from
this patch repository; replace the checkout path with its actual location.

```sh
git clone --branch 615.71.09 --single-branch \
  https://github.com/NVIDIA/open-gpu-kernel-modules.git ../nvidia-615.71.09
scripts/check-series.sh ../nvidia-615.71.09
scripts/apply-series.sh ../nvidia-615.71.09
```

The scripts require the exact baseline commit and a pristine Git working tree.
They do not build, install, or load anything. Continue with the
[driver guide](docs/driver.md), then the [userspace guide](docs/usage.md).

## AI-Generated Code Notice

Parts of this project were created with assistance from AI tools (e.g. large language models). All AI-assisted contributions were reviewed and adapted by maintainers before inclusion. If you need provenance for specific changes, please refer to the Git history and commit messages.
