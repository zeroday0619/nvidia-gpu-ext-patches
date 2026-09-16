<!-- SPDX-License-Identifier: MIT -->
# Source provenance and release notices

This is an independent adaptation of gpu_ext for NVIDIA Open GPU Kernel Modules
615.71.09, with an additional userspace scheduling policy and systemd integration.
It is not an official NVIDIA release and does not imply NVIDIA or gpu_ext
maintainer endorsement.

| Source | Revision and role |
| --- | --- |
| NVIDIA Open GPU Kernel Modules | [61dcc93722ecb418bb5f2e00923f05b4b8051dd1](https://github.com/NVIDIA/open-gpu-kernel-modules/tree/61dcc93722ecb418bb5f2e00923f05b4b8051dd1), official 615.71.09 patch base. |
| gpu_ext driver fork | [c4fd5655a08dce6eb3e027ac3ed58b0b85b9543c](https://github.com/eunomia-bpf/gpu_ext-kernel-modules/tree/c4fd5655a08dce6eb3e027ac3ed58b0b85b9543c), source of driver extension hooks adapted to this base. |
| gpu_ext | [a23fbf8b47334095814adeb440c7462d55a4ac45](https://github.com/eunomia-bpf/gpu_ext/tree/a23fbf8b47334095814adeb440c7462d55a4ac45), upstream reference revision used during the port. |
| gpu_ext scheduler policy | [39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8](https://github.com/eunomia-bpf/gpu_ext/tree/39b6d6e58a8ab44bb19e9ee2133640d4f5da66c8), three-callback policy reference for this ABI. Later policy callbacks are not implicitly supported. |

The patches contain modifications to upstream files and added extension code.
The repository does not redistribute a complete NVIDIA driver source tree,
compiled modules, NVIDIA firmware, or proprietary driver binaries. Applying the
series requires obtaining the exact upstream source separately.

The upstream default driver license includes this notice:

> Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

The complete original license and permission notice are retained in
[LICENSES/NVIDIA-COPYING](LICENSES/NVIDIA-COPYING). Individual upstream files can
carry additional or different notices; retain those notices as well.

`kernel-open/nvidia-uvm/uvm_bpf_struct_ops.c` and
`kernel-open/nvidia-uvm/uvm_bpf_struct_ops.h` were imported without individual
license headers. Their upstream repository default MIT license is recorded in
the license inventory; the absence of a header is not a claim of public-domain
status or new authorship.

AI assistance was used in developing and packaging these changes with Codex
(GPT-6). Human review and validation are required before release or deployment.
This notice does not assert that such review has occurred, certify runtime
correctness, or provide a human developer sign-off.
