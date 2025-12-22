# U-Boot make.sh 构建系统完整教程

## 目录
- [1. 概述](#1-概述)
- [2. 核心设计思想](#2-核心设计思想)
- [3. 构建流程详解](#3-构建流程详解)
- [4. 固件打包原理](#4-固件打包原理)
- [5. 使用示例](#5-使用示例)
- [6. 高级功能](#6-高级功能)
- [7. 常见问题](#7-常见问题)

---

## 1. 概述

### 1.1 脚本作用

`make.sh` 是 Rockchip U-Boot 的**统一构建和打包工具**，它将复杂的 U-Boot 编译和固件打包流程封装成简单的命令行接口。

**主要功能：**
- 自动选择合适的工具链（ARM32/ARM64）
- 编译 U-Boot 源代码
- 打包生成 Rockchip 格式的固件镜像（uboot.img、loader.bin、trust.img）
- 提供调试和符号查询工具

### 1.2 生成的镜像文件

| 镜像文件 | 作用 | 内容 |
|---------|------|------|
| `uboot.img` | U-Boot 主镜像 | u-boot.bin + Rockchip 头部 |
| `loader.bin` / `idbloader.img` | 引导加载器 | DDR 初始化代码 + Miniloader |
| `trust.img` | 可信执行环境 | ATF (BL31) + OP-TEE (BL32) |
| `u-boot.itb` | FIT 格式镜像 | U-Boot + ATF/TEE（可选） |

### 1.3 依赖关系

```
make.sh
  ├── U-Boot 源代码（当前目录）
  ├── rkbin 仓库（../external/rkbin/）
  │   ├── tools/（打包工具：loaderimage、boot_merger、trust_merger）
  │   ├── RKBOOT/（loader 配置文件 *.ini）
  │   └── RKTRUST/（trust 配置文件 *.ini）
  └── 交叉编译工具链
      ├── ARM32: gcc-linaro-6.3.1 arm-linux-gnueabihf-
      └── ARM64: gcc-linaro-6.3.1 aarch64-linux-gnu-

```

---

## 2. 核心设计思想

### 2.1 模块化设计

脚本采用**函数式编程**，每个函数负责一个独立的任务：

```
准备阶段        工具链选择      芯片识别        平台配置
  ↓               ↓              ↓              ↓
prepare()  →  select_toolchain()  →  select_chip_info()  →  fixup_platform_configure()
                                                                      ↓
                                                                 sub_commands()
                                                                      ↓
                                                             ┌────────┴────────┐
                                                             ↓                 ↓
                                                         编译 U-Boot         子命令
                                                             ↓
                                          ┌─────────────────┼─────────────────┐
                                          ↓                 ↓                 ↓
                                   pack_uboot_image()  pack_loader_image()  pack_trust_image()
                                          ↓
                                      finish()
```

### 2.2 平台抽象

脚本通过**配置变量**实现平台无关性：

```bash
# 芯片识别（自动从 .config 提取）
RKCHIP=RK3399              # 芯片型号
RKCHIP_LOADER=RK3399       # loader ini 文件前缀
RKCHIP_TRUST=RK3399        # trust ini 文件前缀

# 平台特定配置（根据芯片自动设置）
PLATFORM_RSA="--rsa 3"     # RK3308/PX30 使用 RSA-PKCS1 V2.1
PLATFORM_SHA="--sha 2"     # RK3368 使用大端序 SHA256
PLATFORM_UBOOT_IMG_SIZE="--size 1024 2"  # 镜像大小限制
```

### 2.3 工具链抽象

自动识别架构并选择工具链：

```bash
if grep -q '^CONFIG_ARM64=y' .config; then
    TOOLCHAIN_GCC=.../aarch64-linux-gnu-
else
    TOOLCHAIN_GCC=.../arm-linux-gnueabihf-
fi
```

---

## 3. 构建流程详解

### 3.1 完整流程图

```
用户执行命令
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 1: prepare() - 准备工作                          │
│  ├─ 解析命令行参数（BOARD、O=<dir>）                 │
│  ├─ 查找或生成 .config 文件                          │
│  ├─ 验证 rkbin 仓库路径                              │
│  └─ 执行 make <board>_defconfig（如果需要）          │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 2: select_toolchain() - 选择工具链               │
│  ├─ 读取 .config 中的 CONFIG_ARM64 标志              │
│  ├─ 选择 ARM32 或 ARM64 工具链                       │
│  └─ 设置 TOOLCHAIN_GCC、TOOLCHAIN_OBJDUMP 等         │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 3: select_chip_info() - 识别芯片型号             │
│  ├─ 正则匹配 CONFIG_ROCKCHIP_* 配置项               │
│  ├─ 提取芯片型号（RK3399、RK3328、PX30 等）         │
│  ├─ 处理芯片变体（RK3368H、RV110X 等）              │
│  └─ 设置 RKCHIP_LOADER 和 RKCHIP_TRUST               │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 4: fixup_platform_configure() - 平台配置         │
│  ├─ RSA/SHA 模式：RK3308/PX30 → --rsa 3             │
│  ├─ 镜像大小：RK1808 → --size 1024 2                │
│  └─ AArch32 模式：修改 RKCHIP_LABEL 后缀             │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 5: sub_commands() - 处理子命令（如果有）         │
│  └─ 如果是 elf/loader/trust/uboot 等命令，直接执行   │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 6: make all - 编译 U-Boot                        │
│  └─ make CROSS_COMPILE=<toolchain> all -j<cores>     │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 7: pack_uboot_image() - 打包 U-Boot 镜像         │
│  ├─ 检查 u-boot.bin 文件大小                         │
│  ├─ 读取 CONFIG_SYS_TEXT_BASE 加载地址              │
│  └─ loaderimage --pack --uboot → uboot.img          │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 8: pack_loader_image() - 打包 Loader 镜像        │
│  ├─ boot_merger <ini> → loader.bin                   │
│  └─ mkimage -T rksd → idbloader.img                  │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 9: pack_trust_image() - 打包 Trust 镜像          │
│  ├─ ARM64: trust_merger <ini> → trust.img           │
│  └─ ARM32: loaderimage --trustos <ini> → trust.img  │
└───────────────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────────────┐
│ 步骤 10: finish() - 显示完成信息                      │
└───────────────────────────────────────────────────────┘
```

### 3.2 关键步骤详解

#### 步骤 1: prepare() - 准备工作

**核心逻辑：**
1. **解析输出目录**
   ```bash
   # 支持两种方式指定输出目录：
   ./make.sh evb-rk3399 O=rockdev   # 方式1：显式指定
   ./make.sh                        # 方式2：自动查找 .config
   ```

2. **处理 defconfig**
   ```bash
   # 如果是新的板子配置，执行 defconfig
   if [ -f configs/${BOARD}_defconfig ]; then
       make ${BOARD}_defconfig ${OUTOPT}
   fi
   ```

3. **验证 rkbin 仓库**
   ```bash
   # rkbin 仓库包含打包工具和二进制 blob
   if [ ! -d ${RKBIN_TOOLS} ]; then
       echo "Can't find rkbin repository!"
       exit 1
   fi
   ```

#### 步骤 3: select_chip_info() - 芯片识别

**识别算法：**

```bash
# 1. 正则表达式匹配芯片配置
chip_reg='^CONFIG_ROCKCHIP_[R,P][X,V,K][0-9ESX]{1,5}'
RKCHIP=`egrep -o ${chip_reg} .config`

# 2. 提取芯片型号
RKCHIP=${RKCHIP##*_}  # 去除前缀 CONFIG_ROCKCHIP_

# 3. 处理特殊芯片变体
grep '^CONFIG_ROCKCHIP_RK3368=y' .config && RKCHIP=RK3368H
grep '^CONFIG_ROCKCHIP_RV1108=y' .config && RKCHIP=RV110X
```

**支持的芯片系列：**
- **RK 系列**: RK3399, RK3399PRO, RK3368, RK3328, RK3326, RK3308, RK3288, RK3128X, RK1808
- **PX 系列**: PX30, PX3SE, PX5
- **RV 系列**: RV1108 (RV110X)

#### 步骤 4: fixup_platform_configure() - 平台配置

**配置规则表：**

| 芯片 | RSA 模式 | SHA 模式 | UBoot 大小 | Trust 大小 | 说明 |
|------|---------|---------|-----------|-----------|------|
| RK3308 | `--rsa 3` | - | `--size 1024 2` | `--size 1024 2` | RSA-PKCS1 V2.1 |
| PX30 | `--rsa 3` | - | - | - | RSA-PKCS1 V2.1 |
| RK3326 | `--rsa 3` | - | - | - | RSA-PKCS1 V2.1 |
| RK1808 | `--rsa 3` | - | `--size 1024 2` | `--size 1024 2` | RSA-PKCS1 V2.1 |
| RK3368 | - | `--sha 2` | - | - | 大端序 SHA256 |
| 其他 | - | - | - | - | 使用默认配置 |

**AArch32 模式处理：**
```bash
# ARM64 CPU 以 AArch32 模式启动时，修改标签
if grep -q '^CONFIG_ARM64_BOOT_AARCH32=y' .config; then
    RKCHIP_LABEL="${RKCHIP}AARCH32"
    RKCHIP_TRUST="${RKCHIP}AARCH32"
fi
```

---

## 4. 固件打包原理

### 4.1 Rockchip 固件结构

Rockchip 固件由多个镜像组成，每个镜像都有特定的 **Rockchip 头部格式**：

```
┌─────────────────────────────────────────────────────┐
│ Rockchip 镜像头部 (Header)                          │
├─────────────────────────────────────────────────────┤
│ Magic Number (4 bytes)        │ 0x0FF0AA55          │
│ Chip Type (4 bytes)            │ RK33 (RK3399)       │
│ Load Address (4 bytes)         │ 0x00200000          │
│ Data Size (4 bytes)            │ 实际数据大小        │
│ CRC32 Checksum (4 bytes)       │ 数据校验和          │
│ Reserved                       │ ...                 │
├─────────────────────────────────────────────────────┤
│ 实际数据 (u-boot.bin / ATF / OP-TEE)                │
└─────────────────────────────────────────────────────┘
```

**为什么需要打包？**
- BootROM 需要识别芯片类型和加载地址
- 验证镜像完整性（CRC 校验）
- 支持加密和签名（RSA/SHA）

### 4.2 uboot.img 打包流程

**流程图：**
```
u-boot.bin (原始二进制)
    ↓
读取 CONFIG_SYS_TEXT_BASE → 0x00200000 (加载地址)
    ↓
检查文件大小 → 不超过 1022KB
    ↓
loaderimage --pack --uboot u-boot.bin uboot.img 0x00200000
    ↓
添加 Rockchip 头部
    ├─ Magic: 0x0FF0AA55
    ├─ Chip: RK33
    ├─ Load Addr: 0x00200000
    ├─ Size: 实际大小
    └─ CRC: 校验和
    ↓
uboot.img (最终镜像)
```

**关键代码：**
```bash
# 1. 读取加载地址
UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" \
    ${OUTDIR}/include/autoconf.mk`

# 2. 打包
${RKTOOLS}/loaderimage --pack --uboot ${OUTDIR}/u-boot.bin uboot.img \
    ${UBOOT_LOAD_ADDR} ${PLATFORM_UBOOT_IMG_SIZE}
```

### 4.3 loader.bin / idbloader.img 打包流程

**Loader 组成：**
```
┌──────────────────────────────────────┐
│ FlashData (DDR 初始化代码)           │  ← bin/rk33/rk3399_ddr_800MHz_v*.bin
├──────────────────────────────────────┤
│ FlashBoot (Miniloader)               │  ← bin/rk33/rk3399_miniloader_v*.bin
└──────────────────────────────────────┘
        ↓
    boot_merger (根据 MINIALL.ini 合并)
        ↓
    loader.bin (用于 eMMC/SPI Flash)
        ↓
    mkimage -T rksd (添加 SD 卡启动头部)
        ↓
    idbloader.img (用于 SD 卡启动)
```

**MINIALL.ini 配置示例：**
```ini
[CHIP_NAME]
NAME=RK3399

[VERSION]
MAJOR=1
MINOR=15

[CODE471_OPTION]
NUM=1
Path1=bin/rk33/rk3399_ddr_800MHz_v1.25.bin  # DDR 初始化
Sleep=1

[CODE472_OPTION]
NUM=1
Path1=bin/rk33/rk3399_miniloader_v1.26.bin  # Miniloader
```

**关键代码：**
```bash
# 1. 合并 DDR init + Miniloader
${RKTOOLS}/boot_merger ${RKBIN}/RKBOOT/${RKCHIP}MINIALL.ini

# 2. 生成 SD 卡启动镜像
${RKTOOLS}/mkimage -n rk3399 -T rksd -d <FlashData> idbloader.img
cat <FlashBoot> >> idbloader.img
```

### 4.4 trust.img 打包流程

**ARM64 平台（使用 trust_merger）：**

```
RKTRUST/*.ini 配置文件
    ↓
┌─────────────────────────────────┐
│ [BL31_OPTION]                   │
│ PATH=bin/rk33/rk3399_bl31.elf   │  ← ARM Trusted Firmware (BL31)
├─────────────────────────────────┤
│ [BL32_OPTION]                   │
│ PATH=bin/rk33/rk3399_bl32.bin   │  ← OP-TEE (BL32)
└─────────────────────────────────┘
    ↓
trust_merger --sha <mode> --rsa <mode> <ini>
    ↓
合并并添加 Rockchip 头部
    ↓
trust.img
    ├─ BL31 (EL3 Secure Monitor)
    └─ BL32 (Trusted OS - OP-TEE)
```

**ARM32 平台（使用 loaderimage）：**

```
RKTRUST/*TOS.ini 配置文件
    ↓
TOS=bin/rk33/rk322xh_tee.bin  ← Trusted OS
    ↓
计算加载地址 = DRAM_BASE + 0x8400000 (132MB offset)
    ↓
loaderimage --pack --trustos <tee.bin> trust.img <load_addr>
    ↓
trust.img
```

**关键代码（ARM64）：**
```bash
${RKTOOLS}/trust_merger \
    ${PLATFORM_SHA} \      # SHA 模式
    ${PLATFORM_RSA} \      # RSA 模式
    ${PLATFORM_TRUST_IMG_SIZE} \  # 大小限制
    --replace tools/rk_tools/ ./ \
    ${RKBIN}/RKTRUST/${RKCHIP}TRUST.ini
```

### 4.5 打包工具详解

| 工具 | 用途 | 输入 | 输出 |
|------|------|------|------|
| **loaderimage** | 添加 Rockchip 头部 | u-boot.bin / tee.bin | uboot.img / trust.img |
| **boot_merger** | 合并 loader 组件 | RKBOOT/*.ini | loader.bin |
| **trust_merger** | 合并 trust 组件 | RKTRUST/*.ini | trust.img |
| **mkimage** | 生成 SD 卡启动镜像 | DDR init bin | idbloader.img |

---

## 5. 使用示例

### 5.1 完整编译流程

**场景：为 RK3399 开发板编译完整固件**

```bash
cd uboot/

# 方法1：指定板子配置（会执行 defconfig）
./make.sh evb-rk3399

# 方法2：指定输出目录
./make.sh evb-rk3399 O=output

# 方法3：使用已存在的 .config
./make.sh
```

**生成的文件：**
```
uboot/
├── uboot.img           ← U-Boot 主镜像
├── loader.bin          ← eMMC/SPI Flash 引导加载器
├── idbloader.img       ← SD 卡引导加载器
└── trust.img           ← 可信执行环境镜像
```

### 5.2 只打包镜像（不编译）

**场景：修改了 rkbin 中的 blob，需要重新打包**

```bash
# 只打包 uboot.img
./make.sh uboot

# 只打包 loader.bin
./make.sh loader

# 只打包 trust.img
./make.sh trust

# 打包所有支持的 loader 变体
./make.sh loader-all

# 打包所有支持的 trust 变体
./make.sh trust-all
```

### 5.3 多板子并行编译

**场景：为不同的板子编译固件**

```bash
# 编译 evb-rk3399，输出到 out_evb/
./make.sh evb-rk3399 O=out_evb

# 编译 firefly-rk3399，输出到 out_firefly/
./make.sh firefly-rk3399 O=out_firefly

# 目录结构：
# uboot/
# ├── out_evb/
# │   ├── .config
# │   └── u-boot.bin
# └── out_firefly/
#     ├── .config
#     └── u-boot.bin
```

### 5.4 自定义 ini 文件

**场景：使用自定义的 loader 或 trust 配置**

```bash
# 使用自定义 loader 配置
FILE=custom_loader.ini ./make.sh loader

# 使用自定义 trust 配置
FILE=custom_trust.ini ./make.sh trust
```

---

## 6. 高级功能

### 6.1 调试功能

**启用调试选项：**

```bash
# 查看所有调试选项
./make.sh debug

# 调试选项列表：
# 1. lib/initcall.c debug() -> printf()
# 2. common/board_r.c 和 common/board_f.c debug() -> printf()
# 3. 全局定义 DEBUG 宏
# 4. 启用 CONFIG_ROCKCHIP_DEBUGGER
# 5. 启用 CONFIG_ROCKCHIP_CRC
# 6. 启用 CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP
# 7. 启用 CONFIG_ROCKCHIP_CRASH_DUMP
# 8. 设置 CONFIG_BOOTDELAY=5（启动延迟5秒）
# 9. ARMv7 start.S 打印 'UUUU...'
# 10. ARMv8 start.S 打印 'UUUU...'
# 11. 固件启动流程 debug() -> printf()
# 12. 启动阶段时序报告

# 启用特定调试选项
./make.sh debug-4  # 启用 Rockchip 调试器
./make.sh debug-8  # 设置启动延迟为5秒
```

**调试选项说明：**

| 选项 | 作用 | 使用场景 |
|------|------|---------|
| `debug-1` | initcall 调试输出 | 排查初始化流程问题 |
| `debug-2` | board 初始化调试输出 | 排查板级初始化问题 |
| `debug-3` | 全局 DEBUG 宏 | 启用所有 debug() 输出 |
| `debug-4` | Rockchip 调试器 | 使用 Rockchip 专有调试功能 |
| `debug-7` | 崩溃转储 | 记录系统崩溃信息 |
| `debug-9/10` | 启动代码警告 | 验证代码是否执行（串口输出 'UUUU...'） |
| `debug-12` | 启动时序报告 | 分析启动性能瓶颈 |

### 6.2 符号查询和反汇编

**6.2.1 查看 ELF 反汇编**

```bash
# 反汇编 u-boot ELF（默认 -D 选项）
./make.sh elf

# 使用不同的 objdump 选项
./make.sh elf-S   # 显示源代码和汇编混合
./make.sh elf-d   # 只显示文本段反汇编
```

**6.2.2 查看内存映射和符号表**

```bash
# 查看 u-boot.map（内存布局）
./make.sh map

# 查看 u-boot.sym（符号表）
./make.sh sym
```

**6.2.3 地址转符号查询**

**场景：系统崩溃在地址 0x00212abc，需要找到对应的函数**

```bash
# 方法1：查询未重定位的地址
./make.sh 0x00212abc

# 输出示例：
# 00212abc T board_init_f
# /path/to/source/common/board_f.c:1023

# 方法2：查询重定位后的地址（需要减去重定位偏移）
# 假设重定位偏移为 0x3ef48000
./make.sh 0x3f15aabc-0x3ef48000

# 计算： 0x3f15aabc - 0x3ef48000 = 0x00212abc
# 输出与方法1相同
```

**地址查询原理：**
```bash
# 1. 将十六进制地址转换为小写
# 2. 如果指定了重定位偏移，则进行减法
# 3. 在 u-boot.sym 中搜索地址
# 4. 使用 addr2line 转换为源代码位置
```

### 6.3 SPL/TPL 支持

**场景：使用 SPL (Secondary Program Loader) 启动**

```bash
# 打包 TPL + SPL loader
./make.sh spl

# 查看 SPL 反汇编
FILE=spl ./make.sh elf

# 查看 TPL 反汇编
FILE=tpl ./make.sh elf
```

### 6.4 ITB 镜像打包

**场景：使用 FIT (Flattened Image Tree) 格式**

```bash
# 打包 u-boot.itb（U-Boot + ATF/TEE）
./make.sh itb

# ITB 格式优势：
# - 包含多个组件（kernel、dtb、ramdisk、ATF 等）
# - 支持压缩和校验
# - 支持多配置选择
```

---

## 7. 常见问题

### 7.1 编译问题

**Q1: 提示 "Can't find toolchain"**

**原因：** 工具链路径不正确

**解决：**
```bash
# 1. 检查工具链是否存在
ls ../toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/

# 2. 修改 make.sh 中的工具链路径
TOOLCHAIN_ARM64=../toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/

# 3. 或者设置环境变量
export PATH=<toolchain_path>/bin:$PATH
```

**Q2: 提示 "Can't find rkbin repository"**

**原因：** rkbin 仓库路径不正确

**解决：**
```bash
# 1. 克隆 rkbin 仓库
cd ../external
git clone https://github.com/rockchip-linux/rkbin.git

# 2. 或修改 make.sh 中的路径
RKBIN_TOOLS=../external/rkbin/tools
```

**Q3: 编译失败 "CONFIG_ROCKCHIP_* not found"**

**原因：** .config 文件缺失或损坏

**解决：**
```bash
# 重新生成 .config
make <board>_defconfig

# 或者
./make.sh <board>
```

### 7.2 打包问题

**Q4: 提示 "pack uboot failed! u-boot.bin actual: XXX bytes, max limit: YYY bytes"**

**原因：** u-boot.bin 超过大小限制

**解决：**
```bash
# 1. 查看当前大小
ls -lh u-boot.bin

# 2. 优化配置（禁用不需要的功能）
make menuconfig
# 禁用：CMD_*, FS_*, NET_* 等不需要的功能

# 3. 如果平台支持，修改大小限制
# 在 fixup_platform_configure() 中调整
PLATFORM_UBOOT_IMG_SIZE="--size 2048 2"  # 增加到 2MB
```

**Q5: 提示 "Can't find: RKBOOT/*.ini" 或 "RKTRUST/*.ini"**

**原因：** ini 配置文件缺失

**解决：**
```bash
# 1. 检查 rkbin 仓库是否完整
ls ../external/rkbin/RKBOOT/RK3399MINIALL.ini
ls ../external/rkbin/RKTRUST/RK3399TRUST.ini

# 2. 如果缺失，更新 rkbin 仓库
cd ../external/rkbin
git pull

# 3. 或使用自定义 ini 文件
FILE=custom.ini ./make.sh loader
```

**Q6: loader.bin 或 trust.img 打包失败**

**原因：** rkbin 中的二进制 blob 缺失

**解决：**
```bash
# 1. 检查 ini 文件中指定的 blob 是否存在
cat ../external/rkbin/RKBOOT/RK3399MINIALL.ini
# 查看 Path1= 指定的文件

# 2. 确保所有 blob 文件存在
ls ../external/rkbin/bin/rk33/rk3399_ddr_*.bin
ls ../external/rkbin/bin/rk33/rk3399_miniloader_*.bin
ls ../external/rkbin/bin/rk33/rk3399_bl31_*.elf
ls ../external/rkbin/bin/rk33/rk3399_bl32_*.bin
```

### 7.3 使用问题

**Q7: 如何查看支持的板子列表？**

```bash
# 方法1：查看 configs/ 目录
ls configs/*_defconfig

# 方法2：执行不带参数的 make.sh
./make.sh

# 输出示例：
# Can't find: configs/xxx_defconfig
# ******** Rockchip Support List *************
# configs/evb-rk3399_defconfig
# configs/firefly-rk3399_defconfig
# ...
```

**Q8: 如何验证生成的镜像是否正确？**

```bash
# 1. 检查文件是否生成
ls -lh uboot.img loader.bin trust.img idbloader.img

# 2. 使用 file 命令查看文件类型
file uboot.img
# 输出：uboot.img: data

# 3. 使用 hexdump 查看 Rockchip 头部
hexdump -C uboot.img | head -n 4
# 前4字节应该是：55 aa f0 0f（Rockchip magic number）

# 4. 检查 CRC（如果启用了 CONFIG_ROCKCHIP_CRC）
./make.sh debug-5  # 启用 CRC 检查
```

**Q9: 如何为不同的芯片编译？**

```bash
# 1. 查找对应的 defconfig
ls configs/ | grep rk3328

# 2. 编译对应的配置
./make.sh evb-rk3328

# 3. 脚本会自动识别芯片型号并设置相应的打包参数
```

### 7.4 调试技巧

**Q10: 如何调试 U-Boot 启动失败？**

**步骤：**
```bash
# 1. 启用启动延迟（方便中断）
./make.sh debug-8

# 2. 启用调试输出
./make.sh debug-1  # initcall 调试
./make.sh debug-2  # board 初始化调试

# 3. 启用启动阶段时序报告
./make.sh debug-12

# 4. 重新编译
make CROSS_COMPILE=<toolchain> all

# 5. 通过串口查看启动日志
# 连接串口（115200 8N1）
minicom -D /dev/ttyUSB0 -b 115200

# 6. 如果没有任何输出，启用早期调试
./make.sh debug-10  # ARMv8 start.S 打印 'UUUU...'
```

**Q11: 如何定位系统崩溃位置？**

**步骤：**
```bash
# 1. 记录崩溃地址（从串口日志获取）
# 例如：PC is at 0x3f15aabc

# 2. 查询符号
./make.sh 0x3f15aabc-0x3ef48000  # 减去重定位偏移

# 3. 查看源代码
# 根据输出的文件名和行号定位问题

# 4. 如果需要查看汇编代码
./make.sh elf
# 在 less 中搜索 "/3f15aabc"
```

---

## 8. 高级主题

### 8.1 自定义平台配置

**场景：添加新的芯片支持**

```bash
# 1. 在 RKCHIP_INI_DESC 中添加配置
RKCHIP_INI_DESC=(
    "CONFIG_TARGET_MY_BOARD  MYLABEL  MYLOADER  MYTRUST"
)

# 2. 在 fixup_platform_configure() 中添加特殊配置
if [ $RKCHIP = "MYNEWCHIP" ]; then
    PLATFORM_RSA="--rsa 3"
    PLATFORM_UBOOT_IMG_SIZE="--size 1024 2"
fi

# 3. 准备 ini 文件
# 创建 rkbin/RKBOOT/MYLOADERMINIALL.ini
# 创建 rkbin/RKTRUST/MYTRUSTTRUST.ini
```

### 8.2 修改打包参数

**场景：调整镜像大小限制**

```bash
# 编辑 fixup_platform_configure() 函数
if [ $RKCHIP = "RK3399" ]; then
    PLATFORM_UBOOT_IMG_SIZE="--size 2048 2"  # 改为 2MB
    PLATFORM_TRUST_IMG_SIZE="--size 2048 2"
fi
```

### 8.3 多阶段打包流程

**场景：自定义打包流程**

```bash
# 1. 只编译，不打包
make CROSS_COMPILE=<toolchain> all

# 2. 手动打包 uboot
./tools/loaderimage --pack --uboot u-boot.bin uboot.img 0x00200000

# 3. 手动打包 loader
cd ../external/rkbin
./tools/boot_merger --replace tools/rk_tools/ ./ RKBOOT/RK3399MINIALL.ini

# 4. 手动打包 trust
./tools/trust_merger --replace tools/rk_tools/ ./ RKTRUST/RK3399TRUST.ini
```

---

## 9. 最佳实践

### 9.1 开发工作流

**推荐流程：**

```bash
# 1. 首次编译
./make.sh <board>

# 2. 修改代码
vim board/rockchip/<board>/<file>.c

# 3. 增量编译（无需重新打包）
make CROSS_COMPILE=<toolchain> -j$(nproc)

# 4. 重新打包镜像
./make.sh uboot
./make.sh loader
./make.sh trust

# 5. 烧写测试
# 使用 upgrade_tool（Linux）或 AndroidTool（Windows）
```

### 9.2 版本管理

**建议：**

```bash
# 1. 记录编译时的版本信息
git log --oneline -1 > version.txt
echo "Built on $(date)" >> version.txt

# 2. 保存生成的镜像
mkdir -p releases/$(date +%Y%m%d)
cp uboot.img loader.bin trust.img releases/$(date +%Y%m%d)/

# 3. 记录配置
cp .config releases/$(date +%Y%m%d)/config.txt
```

### 9.3 清理和重建

```bash
# 清理编译输出（保留 .config）
make clean

# 完全清理（删除 .config）
make distclean

# 重新编译
./make.sh <board>
```

---

## 10. 总结

### 10.1 核心概念

| 概念 | 说明 |
|------|------|
| **U-Boot** | 通用引导加载程序 |
| **Loader** | Rockchip 第一阶段引导（DDR init + Miniloader） |
| **Trust** | 可信执行环境（ATF + OP-TEE） |
| **rkbin** | Rockchip 二进制 blob 仓库 |
| **Rockchip 头部** | 包含 magic、chip、load addr、size、CRC 的头部结构 |

### 10.2 关键文件

| 文件 | 作用 |
|------|------|
| `make.sh` | 统一构建和打包工具 |
| `.config` | U-Boot 配置文件 |
| `RKBOOT/*.ini` | Loader 打包配置 |
| `RKTRUST/*.ini` | Trust 打包配置 |
| `u-boot.bin` | U-Boot 原始二进制 |
| `uboot.img` | U-Boot 最终镜像（带 Rockchip 头部） |

### 10.3 构建流程精髓

```
准备 → 选择工具链 → 识别芯片 → 配置平台 → 编译 → 打包
```

每个步骤都是**独立可测试**的，支持**增量构建**和**模块化开发**。

---

## 11. 参考资料

### 11.1 相关文档

- `固件打包原理深度解析.md` - 打包工具的详细原理
- `loader镜像打包教程.md` - Loader 打包详解
- `trust镜像打包教程.md` - Trust 打包详解
- `uboot镜像打包教程.md` - U-Boot 打包详解

### 11.2 工具源代码

- `tools/rockchip/loaderimage.c:577` - loaderimage 工具实现
- `tools/rockchip/boot_merger.c` - boot_merger 工具实现
- `tools/rockchip/trust_merger.c:779` - trust_merger 工具实现

### 11.3 外部资源

- [Rockchip U-Boot 官方文档](https://opensource.rock-chips.com/wiki_U-Boot)
- [rkbin 仓库](https://github.com/rockchip-linux/rkbin)
- [U-Boot 官方文档](https://docs.u-boot.org/)

---

**文档版本：** v1.0
**最后更新：** 2025-12-16
**适用版本：** Rockchip U-Boot (RK3399, RK3328, PX30 等)
