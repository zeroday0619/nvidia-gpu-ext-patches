<!-- SPDX-License-Identifier: MIT -->
# License inventory

This repository contains a driver patch series and a userspace policy package.
It does not apply one blanket license to all upstream-derived content.

| Material | Applicable license information |
| --- | --- |
| NVIDIA driver portions represented in patches | The original per-file notices and [NVIDIA-COPYING](LICENSES/NVIDIA-COPYING) apply. That file specifies MIT for individual files except where otherwise noted, and MIT/GPLv2 dual licensing for the linked Linux kernel module. |
| Imported gpu_ext driver additions | Preserve their original per-file notices. Additions without a specific notice inherit the driver repository's default MIT terms in `NVIDIA-COPYING`. |
| Userspace C and header files with `SPDX-License-Identifier: GPL-2.0` | GNU GPL version 2 only; see [full license text](LICENSES/GPL-2.0-only.txt). The existing SPDX spelling is retained. |
| Package-authored documentation, shell scripts, Makefiles, configuration, and service units | MIT, unless a file carries another explicit license; see [license text](LICENSES/MIT.txt). |

Patch files preserve the licenses of the code they modify or include. Existing
copyright and license notices must remain intact when applying, adapting, or
redistributing the series. The default license for package-authored files does
not relicense upstream material.

The original NVIDIA license file is included verbatim. Copyright ownership is
not reassigned by this inventory. See [NOTICE.md](NOTICE.md) for source provenance.
