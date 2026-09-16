# Driver patch guide

## Exact source baseline

Apply this series to the full official NVIDIA repository, not a standalone
`kernel-open` directory extracted from the NVIDIA installer.

| Component | Revision |
| --- | --- |
| NVIDIA baseline | [615.71.09](https://github.com/NVIDIA/open-gpu-kernel-modules/tree/615.71.09), commit `61dcc93722ecb418bb5f2e00923f05b4b8051dd1` |
| gpu_ext policy source | [`a23fbf8b47334095814adeb440c7462d55a4ac45`](https://github.com/eunomia-bpf/gpu_ext/tree/a23fbf8b47334095814adeb440c7462d55a4ac45) |
| gpu_ext driver source | [`c4fd5655a08dce6eb3e027ac3ed58b0b85b9543c`](https://github.com/eunomia-bpf/gpu_ext-kernel-modules/tree/c4fd5655a08dce6eb3e027ac3ed58b0b85b9543c) |
| Compatible scheduler policy ABI | [`39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8`](https://github.com/eunomia-bpf/gpu_ext/tree/39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8) |

The port exposes six-member `gpu_mem_ops` and three-callback
`nv_gpu_sched_ops` interfaces. The scheduler policy at `a23fbf8` adds
`on_timeslice_control` and `bpf_nv_gpu_override_timeslice`; that scheduler ABI
is not implemented by this port. The bundled userspace policy targets the
three-callback interface.

## Series

1. `0001-uvm-memory-policies.patch`: UVM memory policy integration.
2. `0002-gpu-scheduling-hooks.patch`: GPU scheduler callbacks and RM integration.
3. `0003-build-prerequisites.patch`: build prerequisites and BTF handling.

`patches/615.71.09/series` is the application order. `base.env` records the
baseline. The patches modify the conventional upstream `src/nvidia` and
`kernel-open` paths; they do not vendor the NVIDIA source tree or prebuilt RM
objects.

These are plain unified diffs, not `git am` mailboxes. Use the supplied scripts
or `git apply` in series order.

From the patch repository root:

```sh
scripts/check-series.sh /path/to/nvidia-615.71.09
scripts/apply-series.sh /path/to/nvidia-615.71.09
```

Use a fresh checkout with the exact baseline HEAD and no working-tree changes.
The apply command validates the complete sequence before modifying the target.
Review the resulting diff before building.

## Build manually

Use the target kernel's build tree and matching toolchain. The target kernel
must support BPF struct_ops and enable `CONFIG_BPF_SYSCALL`, `CONFIG_BPF_JIT`,
`CONFIG_DEBUG_INFO_BTF`, and `CONFIG_DEBUG_INFO_BTF_MODULES`. Its matching
`vmlinux` must be available for module BTF generation. Another kernel's live
BTF is not a substitute.

Install the prerequisites specified by the
[official NVIDIA build documentation](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/615.71.09/README.md),
including the necessary target-kernel tools and `pahole`. The patch scripts do
not install dependencies.

Run the following from the **official NVIDIA checkout root** for an LLVM-built
kernel. Set `SYSSRC` and, if separate, `SYSOUT` when building for another kernel.

```sh
make -j"$(nproc)" modules LLVM=1 \
  CC=clang CXX=clang++ LD=ld.lld AR=llvm-ar \
  NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump
```

The upstream top-level build incorporates the patched RM sources. Do not reuse
an unpatched `nv-kernel.o_binary` from an installer extraction.

Use matching **615.71.09 userspace libraries and firmware**. Follow NVIDIA's
installation and module-loading guidance for the target distribution. This
repository does not automate driver replacement or DKMS installation.

After loading the resulting modules, both `/sys/kernel/btf/nvidia` and
`/sys/kernel/btf/nvidia_uvm` must be available. Policy attachment additionally
requires compatible kernel and libbpf support; successful compilation alone
does not establish runtime compatibility.

## Optional memory policies

Memory-policy examples are maintained in the separate pinned gpu_ext checkout.
At revision `a23fbf8b47334095814adeb440c7462d55a4ac45`, initialize its `libbpf`
and `bpftool` submodules and build selected examples there. Do not build or
install its older driver submodule. CO-RE relocation, verifier acceptance, and
memory-policy behavior against this port remain **Verification required**.

Only one memory policy and one scheduler policy can attach to their respective
global interfaces at a time. The bundled userspace service attaches only the
scheduler policy.
