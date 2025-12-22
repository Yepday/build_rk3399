# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **streamlined learning repository** for OrangePi RK3399 embedded Linux build architecture. The full source code has been removed to reduce size from 2.9GB to 842KB, retaining only:
- Build scripts and architecture
- Configuration files
- Rockchip firmware packing tool source code
- Documentation on firmware generation

This repository is **NOT for compilation** but for understanding the build flow, firmware packing process, and embedded Linux system architecture.

## Build Commands

### Main Build Script
The primary entry point is `scripts/build.sh`, which provides an interactive menu system requiring root privileges:

```bash
sudo ./scripts/build.sh
```

**Build Options:**
- `0` - Build Release Image (full build: uboot + kernel + rootfs + image)
- `1` - Build Rootfs only
- `2` - Build Uboot only
- `3` - Build Linux kernel
- `4` - Build kernel modules only
- `5` - Update Kernel Image
- `6` - Update Module
- `7` - Update Uboot

### U-Boot Specific Commands
From the `uboot/` directory:

```bash
# Build for RK3399
./make.sh rk3399

# Pack firmware images
./make.sh uboot       # Pack uboot.img
./make.sh trust       # Pack trust.img
./make.sh loader      # Pack loader binary
./make.sh loader-all  # Pack all supported loader variants
./make.sh trust-all   # Pack all supported trust images
```

### Kernel Build
```bash
# Build kernel (from project root)
make -C kernel ARCH=arm64 CROSS_COMPILE=<toolchain> <board>_linux_defconfig
make -C kernel ARCH=arm64 CROSS_COMPILE=<toolchain> -j$(nproc) rk3399-orangepi-<board>.img
make -C kernel ARCH=arm64 CROSS_COMPILE=<toolchain> -j$(nproc) modules
```

## Architecture Overview

### Build Flow Architecture

The build system follows a modular, staged architecture:

```
scripts/build.sh (Main orchestrator)
    │
    ├─── Platform Detection (OrangePiRK3399)
    │    └─── Board Selection (4, rk3399)
    │
    ├─── Toolchain Setup
    │    └─── gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu
    │
    └─── Build Stages
         ├─── 1. U-Boot Build (scripts/lib/compilation.sh::compile_uboot)
         │    └─── Output: uboot.img, trust.img, loader.bin, idbloader.img
         │
         ├─── 2. Kernel Build (scripts/lib/compilation.sh::compile_kernel)
         │    └─── Output: boot.img (kernel + dtb + initrd)
         │
         ├─── 3. Rootfs Build (scripts/lib/distributions.sh::build_rootfs)
         │    └─── Creates ext4 filesystem with userspace
         │
         └─── 4. Image Packing (scripts/lib/build_image.sh::build_rk_image)
              └─── Output: Final flashable .img with GPT partitions
```

### Firmware Packing Architecture

The RK3399 firmware packing process is handled by custom Rockchip tools (source in `uboot/tools/rockchip/`):

**Key Packing Tools:**
- `boot_merger.c` - Merges boot partition images from .ini configuration
- `trust_merger.c` - Merges trust partition images (ATF, OP-TEE)
- `loaderimage.c` - Generates loader images with Rockchip headers
- `resource_tool.c` - Packs resource files (logos, device trees)

**Packing Workflow (uboot/make.sh):**
1. `prepare()` - Validates toolchain and rkbin repository
2. `select_chip_info()` - Determines SoC type from .config
3. `fixup_platform_configure()` - Sets RSA/SHA modes and image sizes
4. Build U-Boot with `make CROSS_COMPILE=... all`
5. `pack_uboot_image()` - Wraps u-boot.bin with Rockchip header → uboot.img
6. `pack_loader_image()` - Combines DDR init + miniloader → loader.bin + idbloader.img
7. `pack_trust_image()` - Packs ATF (BL31) + OP-TEE (BL32) → trust.img

**Why Packing is Necessary:**
- Rockchip BootROM requires specific magic numbers, chip identifiers, and load addresses
- Raw binaries (u-boot.bin) lack metadata for BootROM to parse
- Packing adds headers with: magic (0x0FF0AA55), chip type, load address, size, checksums
- Read `uboot/固件打包原理深度解析.md` for detailed packing theory

### Image Layout (GPT Partitions)

Final image structure (defined in `scripts/lib/build_image.sh::build_rk_image`):

```
Sector     Size     Partition      Content
------     ----     ---------      -------
64         32KB     [idbloader]    DDR init + Miniloader
24576      4MB      uboot          U-Boot bootloader
32768      4MB      trust          ARM Trusted Firmware + OP-TEE
49152      32MB     boot           Kernel + Device Tree + Initramfs
376832     ~GB      rootfs         Root filesystem (ext4)
```

### Module Organization

**scripts/lib/** contains functional modules:
- `general.sh` - System preparation and dependency checks
- `compilation.sh` - Kernel and U-Boot compilation logic
- `pack.sh` - Legacy H3/H6 packing (not used for RK3399)
- `distributions.sh` - Rootfs building (debootstrap/Ubuntu setup)
- `build_image.sh` - Final image assembly with dd/parted
- `platform/rk3399.sh` - RK3399-specific platform hooks

### Configuration Flow

Platform detection and configuration cascades through:
1. `scripts/build.sh` sets `PLATFORM=OrangePiRK3399` from `basename $(pwd)`
2. Sources `scripts/lib/platform/rk3399.sh` for platform-specific overrides
3. `uboot/make.sh` reads `.config` to determine:
   - `RKCHIP` (e.g., RK3399) via regex matching CONFIG_ROCKCHIP_*
   - `RKCHIP_LOADER` and `RKCHIP_TRUST` for finding .ini files in rkbin repo
4. Toolchain paths resolved from `TOOLCHAIN_ARM64` variable
5. INI files (`rkbin/RKBOOT/*.ini`, `rkbin/RKTRUST/*.ini`) specify binary blob paths

### External Dependencies (NOT Included)

To build the full project, these must be obtained:
- **Kernel source**: `git clone https://github.com/orangepi-xunlong/OrangePiRK3399_kernel.git kernel`
- **U-Boot source**: `git clone https://github.com/orangepi-xunlong/OrangePiRK3399_uboot.git uboot`
- **External tools**: `git clone https://github.com/orangepi-xunlong/OrangePiRK3399_external.git external`
- **Toolchain**: GCC Linaro 6.3.1 for aarch64
- **rkbin repository**: Required for packing (contains DDR init, ATF, OP-TEE binaries)

## Rockchip Packing Tools Deep Dive

Understanding these tools is critical for firmware modification:

**loaderimage (uboot/tools/rockchip/loaderimage.c:577)**
```c
// Adds Rockchip header to u-boot.bin
${RKTOOLS}/loaderimage --pack --uboot u-boot.bin uboot.img <load_addr> <size_params>
```
- Header format: magic (4B) + chip ID + load addr + size + CRC
- Used for both uboot.img and trust.img packing

**boot_merger (uboot/tools/rockchip/boot_merger.c)**
```bash
${RKTOOLS}/boot_merger <ini_file>
```
- Reads RKBOOT/*.ini files (e.g., RK3399MINIALL.ini)
- Combines: DDR init blob + miniloader → loader.bin
- Generates idbloader.img for SD card boot

**trust_merger (uboot/tools/rockchip/trust_merger.c:779)**
```bash
${RKTOOLS}/trust_merger --rsa <mode> --sha <mode> --size <size> <ini_file>
```
- ARM64 platforms use this (ARM32 uses loaderimage)
- Merges BL31 (ATF) + BL32 (OP-TEE) from RKTRUST/*.ini
- Supports RSA PKCS1 V2.1 signing (--rsa 3 for RK3308/PX30)

**resource_tool (uboot/tools/rockchip/resource_tool.c)**
```bash
${RKTOOLS}/resource_tool --pack <logo> <dtb> resource.img
```
- Packs boot logos and device trees into resource partition
- Used for splash screens and hardware descriptions

## Key Configuration Files

- `kernel/build.config.*` - Kernel build configurations
- `uboot/configs/*_defconfig` - U-Boot board configurations
- `rkbin/RKBOOT/*MINIALL.ini` - Loader component specifications
- `rkbin/RKTRUST/*TRUST.ini` - Trust component specifications (BL31/BL32 paths)

## Documentation Files

The repository includes valuable documentation:
- `uboot/固件打包原理深度解析.md` - Deep dive into packing theory
- `uboot/固件生成教学文档.md` - Firmware generation tutorial
- `uboot/docs/loader镜像打包教程.md` - Loader image packing guide
- `uboot/docs/trust镜像打包教程.md` - Trust image packing guide
- `uboot/docs/uboot镜像打包教程.md` - U-Boot image packing guide

## Important Notes

- **This is a skeleton project** - Source code directories exist but are empty
- **Root privileges required** - Build script enforces sudo for mount/partition operations
- **Cross-compilation only** - Designed for x86_64 host targeting ARM64
- **RK3399 specific** - Logic branches heavily on PLATFORM=OrangePiRK3399
- **rkbin dependency** - Packing fails without ../external/rkbin repository (contains proprietary blobs)
- **Chinese documentation** - Most docs are in Chinese, explaining Rockchip boot flow and packing algorithms
