# RK3399 Loader 构建流程深度解析

## 目录

1. [构建流程总览](#1-构建流程总览)
2. [构建入口：build.sh](#2-构建入口buildsh)
3. [U-Boot 编译：make.sh](#3-u-boot-编译makesh)
4. [Loader 打包：boot_merger](#4-loader-打包boot_merger)
5. [配置文件：RK3399MINIALL.ini](#5-配置文件rk3399miniallini)
6. [完整构建流程示例](#6-完整构建流程示例)
7. [源码级别的打包实现](#7-源码级别的打包实现)

---

## 1. 构建流程总览

### 1.1 整体架构

```
用户执行
   ↓
./build.sh (OrangePiRK3399)
   ↓
选择平台和编译选项
   ↓
uboot/make.sh rk3399  ←───────────────┐
   ↓                                  │
编译 U-Boot 源码 (make)               │
   ├─ u-boot.bin                     │
   ├─ u-boot.dtb                     │
   └─ bl31.elf (来自 rkbin)            │
   ↓                                  │
打包 uboot.img (loaderimage)          │
   ↓                                  │
打包 trust.img (trust_merger)         │
   ↓                                  │
打包 loader.bin (boot_merger) ←───────┘
   ├─ 读取: RK3399MINIALL.ini
   ├─ 组件1: rk3399_ddr_800MHz_v1.22.bin
   ├─ 组件2: rk3399_miniloader_v1.19.bin
   └─ 输出: rk3399_loader_v1.22.119.bin
   ↓
生成 idbloader.img (mkimage + cat)
   ↓
复制到 output/uboot/ 目录
```

### 1.2 关键文件和工具

| 文件/工具 | 位置 | 作用 |
|----------|------|------|
| `build.sh` | `scripts/build.sh` | 总构建脚本入口 |
| `make.sh` | `uboot/make.sh` | U-Boot 编译和打包脚本 |
| `boot_merger` | `external/rkbin/tools/boot_merger` | Loader 打包工具（二进制） |
| `loaderimage` | `external/rkbin/tools/loaderimage` | U-Boot 镜像打包工具 |
| `trust_merger` | `external/rkbin/tools/trust_merger` | Trust 镜像打包工具 |
| `mkimage` | `uboot/tools/mkimage` | SD 卡启动镜像工具 |
| `RK3399MINIALL.ini` | `external/rkbin/RKBOOT/RK3399MINIALL.ini` | Loader 打包配置文件 |
| `rk3399_ddr_*.bin` | `external/rkbin/bin/rk33/` | DDR 初始化二进制（预编译） |
| `rk3399_miniloader_*.bin` | `external/rkbin/bin/rk33/` | Miniloader 二进制（预编译） |
| `rk3399_usbplug_*.bin` | `external/rkbin/bin/rk33/` | USB 下载工具二进制（预编译） |

---

## 2. 构建入口：build.sh

### 2.1 RK3399 平台选择

文件位置：`scripts/build.sh:142-161`

```bash
"OrangePiRK3399")
    OPTION=$(whiptail --title "Orange Pi Build System" \
            --menu "$MENUSTR" 15 60 5 --cancel-button Exit --ok-button Select \
            "0"  "OrangePi 4" \
            "1"  "OrangePi rk3399" \
            3>&1 1>&2 2>&3)

    case "${OPTION}" in
        "0") BOARD="4" ;;
        "1") BOARD="rk3399" ;;
        *)
        echo -e "\e[1;31m Pls select correct board \e[0m"
        exit 0 ;;
    esac

    ARCH="arm64"
    CHIP="RK3399"
    TOOLS=$ROOT/toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
    KERNEL_NAME="linux4.4.179"
    ;;
```

**关键变量设置：**

- `ARCH=arm64` - 64位ARM架构
- `CHIP=RK3399` - 芯片型号
- `TOOLS=` - 交叉编译工具链路径

### 2.2 U-Boot 编译触发

文件位置：`scripts/lib/compilation.sh:40-46`

```bash
"OrangePiRK3399")
    ./make.sh rk3399                          # 执行 U-Boot make 脚本
    cp -rf uboot.img $UBOOT_BIN               # 复制 uboot.img
    cp -rf trust.img $UBOOT_BIN               # 复制 trust.img
    cp -rf rk3399_loader_v1.22.119.bin $UBOOT_BIN  # 复制 loader bin
    cp -rf idbloader.img $UBOOT_BIN           # 复制 idbloader.img
    ;;
```

**说明：**

- `make.sh rk3399` 是 RK3399 平台编译的核心命令
- 所有输出文件最终复制到 `output/uboot/` 目录

---

## 3. U-Boot 编译：make.sh

### 3.1 脚本执行流程

文件位置：`uboot/make.sh:830-839`

```bash
# 主执行流程
prepare                      # 准备环境，解析参数
select_toolchain             # 选择交叉编译工具链
select_chip_info             # 解析芯片信息（从 .config）
fixup_platform_configure     # 修正平台配置
sub_commands                 # 执行子命令（如果有）
make CROSS_COMPILE=${TOOLCHAIN_GCC} all --jobs=${JOB} ${OUTOPT}  # 编译 U-Boot
pack_uboot_image             # 打包 uboot.img
pack_loader_image            # 打包 loader bin
pack_trust_image             # 打包 trust.img
finish                       # 完成
```

### 3.2 芯片信息解析

文件位置：`uboot/make.sh:337-400`

```bash
select_chip_info()
{
    # 从 .config 读取芯片型号
    # 正则表达式匹配：PX30, PX3SE, RK????, RV????
    local chip_reg='^CONFIG_ROCKCHIP_[R,P][X,V,K][0-9ESX]{1,5}'
    RKCHIP=`egrep -o ${chip_reg} ${OUTDIR}/.config`

    # 提取芯片名称
    RKCHIP=${RKCHIP##*_}  # 例如: RK3399

    # 设置默认值
    RKCHIP_LABEL=${RKCHIP}        # 显示标签
    RKCHIP_LOADER=${RKCHIP}       # Loader INI 文件前缀
    RKCHIP_TRUST=${RKCHIP}        # Trust INI 文件前缀
}
```

**输出：**
- `RKCHIP=RK3399`
- `RKCHIP_LOADER=RK3399`
- `RKCHIP_TRUST=RK3399`

### 3.3 U-Boot 编译

```bash
# 实际执行的命令
make CROSS_COMPILE=aarch64-linux-gnu- all --jobs=8

# 生成的关键文件
u-boot.bin         # U-Boot 主程序二进制
u-boot.dtb         # 设备树
u-boot             # ELF 格式可执行文件
u-boot.map         # 链接映射文件
```

### 3.4 打包 uboot.img

文件位置：`uboot/make.sh:528-565`

```bash
pack_uboot_image()
{
    local UBOOT_LOAD_ADDR UBOOT_MAX_KB UBOOT_KB HEAD_KB=2

    # 1. 检查 u-boot.bin 大小
    UBOOT_KB=`ls -l u-boot.bin | awk '{print $5}'`
    if [ "$PLATFORM_UBOOT_IMG_SIZE" = "" ]; then
        UBOOT_MAX_KB=1046528  # 默认最大 1022 KB
    fi

    # 2. 读取加载地址（从 autoconf.mk 或 .config）
    UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" \
                     ${OUTDIR}/include/autoconf.mk|tr -d '\r'`
    # 对于 RK3399，通常是: 0x00200000

    # 3. 调用 loaderimage 打包
    ${RKTOOLS}/loaderimage --pack --uboot ${OUTDIR}/u-boot.bin \
        uboot.img ${UBOOT_LOAD_ADDR} ${PLATFORM_UBOOT_IMG_SIZE}

    echo "pack uboot okay! Input: ${OUTDIR}/u-boot.bin"
}
```

**loaderimage 打包格式：**
```
uboot.img 结构：
┌────────────────────────────┐ 0x00000000
│ Header (2048 bytes)        │
│ - Magic: 0x0FF0AA55        │
│ - Size: u-boot.bin 大小    │
│ - Load Addr: 0x00200000    │
│ - CRC32 校验和              │
│ - SHA256 哈希               │
├────────────────────────────┤ 0x00000800
│ u-boot.bin 内容             │
│ (已对齐到 2048 字节)        │
└────────────────────────────┘
```

### 3.5 打包 loader bin（重点！）

文件位置：`uboot/make.sh:654-696`

```bash
pack_loader_image()
{
    local mode=$1 files ini=${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini

    # 1. 检查 INI 配置文件是否存在
    # 对于 RK3399: ini = external/rkbin/RKBOOT/RK3399MINIALL.ini
    if [ ! -f $ini ]; then
        echo "pack loader failed! Can't find: $ini"
        return
    fi

    # 2. 删除之前生成的 loader bin
    ls *_loader_*.bin >/dev/null 2>&1 && rm *_loader_*.bin

    # 3. 进入 rkbin 目录
    cd ${RKBIN}

    # 4. 调用 boot_merger 工具打包
    # BIN_PATH_FIXUP = "--replace tools/rk_tools/ ./"
    ${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} $ini
    echo "pack loader okay! Input: $ini"

    # 5. 移动生成的 loader bin 到当前目录
    cd - && mv ${RKBIN}/*_loader_*.bin ./
    # 生成: rk3399_loader_v1.22.119.bin

    # 6. 额外生成 idbloader.img（用于 SD 卡启动）
    local temp=`grep FlashData= ${ini} | cut -f 2 -d "="`
    local flashData=${temp/tools\/rk_tools\//}  # bin/rk33/rk3399_ddr_800MHz_v1.22.bin

    temp=`grep FlashBoot= ${ini} | cut -f 2 -d "="`
    local flashBoot=${temp/tools\/rk_tools\//}  # bin/rk33/rk3399_miniloader_v1.19.bin

    typeset -l localChip
    localChip=$RKCHIP  # rk3399 (转小写)

    # 使用 mkimage 生成 SD 卡启动头部
    ${RKTOOLS}/mkimage -n ${localChip} -T rksd \
        -d ${RKBIN}/${flashData} idbloader.img

    # 追加 miniloader 到 idbloader.img
    cat ${RKBIN}/${flashBoot} >> idbloader.img
}
```

**执行示例：**
```bash
$ cd external/rkbin
$ ./tools/boot_merger --replace tools/rk_tools/ ./ RKBOOT/RK3399MINIALL.ini

# 输出文件
rk3399_loader_v1.22.119.bin    # 完整的 Loader bin
```

### 3.6 打包 trust.img

文件位置：`uboot/make.sh:763-818`

```bash
pack_trust_image()
{
    local mode=$1 files ini

    # 删除旧的 trust 镜像
    ls trust*.img >/dev/null && rm trust*.img

    # ARM64 使用 trust_merger 工具
    if grep -Eq ''^CONFIG_ARM64=y'' ${OUTDIR}/.config ; then
        # 对于 RK3399: ini = external/rkbin/RKTRUST/RK3399TRUST.ini
        ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TRUST.ini

        if [ "$FILE" != "" ]; then
            ini=$FILE;
        fi

        # 调用 trust_merger 打包
        __pack_64bit_trust_image ${ini}
    fi
}

__pack_64bit_trust_image()
{
    local ini=$1

    if [ ! -f ${ini} ]; then
        echo "pack trust failed! Can't find: ${ini}"
        return
    fi

    cd ${RKBIN}
    ${RKTOOLS}/trust_merger ${PLATFORM_SHA} ${PLATFORM_RSA} \
        ${PLATFORM_TRUST_IMG_SIZE} ${BIN_PATH_FIXUP} \
        ${PACK_IGNORE_BL32} ${ini}

    cd - && mv ${RKBIN}/trust*.img ./
    echo "pack trust okay! Input: ${ini}"
}
```

**trust.img 内容：**
```
trust.img 组件：
┌────────────────────────────┐
│ Header                     │
├────────────────────────────┤
│ BL31 (ARM Trusted Firmware)│  - ATF 固件
├────────────────────────────┤
│ BL32 (OP-TEE OS)           │  - 可选的 TEE 组件
├────────────────────────────┤
│ BL33 (U-Boot)              │  - 已包含在 uboot.img
└────────────────────────────┘
```

---

## 4. Loader 打包：boot_merger

### 4.1 boot_merger 工具说明

`boot_merger` 是 Rockchip 提供的**闭源二进制工具**，用于将多个组件打包成最终的 Loader bin 文件。

**工具位置：**
```
external/rkbin/tools/boot_merger
```

**功能：**
- 读取 INI 配置文件
- 合并多个二进制组件（DDR Init、Miniloader、USBPlug 等）
- 生成 Loader Header（包含魔数、组件信息、校验和）
- 输出最终的 `rk3399_loader_vX.XX.XXX.bin`

### 4.2 boot_merger 源码分析

虽然工具是闭源的，但项目中有源码：`uboot/tools/rockchip/boot_merger.c`

**核心数据结构：**

```c
// Loader Header 结构（前 2048 字节）
#define ENTRY_ALIGN (2048)

typedef struct {
    uint32_t magic;          // 魔数: "BOOT" = 0x544F4F42
    uint16_t header_size;    // Header 大小
    uint16_t version;        // 版本号
    uint32_t reserved;       // 保留字段
    uint16_t merge_version;  // 合并版本
    uint16_t year;           // 年份
    uint16_t month;          // 月份
    uint8_t  day;            // 日期
    uint8_t  hour;           // 小时
    uint8_t  minute;         // 分钟
    uint8_t  second;         // 秒
    char     chip_tag[8];    // 芯片标识: "!C033" (RK3399)
    // ... 组件信息数组
    // ... 组件名称（Unicode）
    // ... 签名/校验和
} loader_header_t;

typedef struct {
    uint16_t type;           // 组件类型: 0x0139, 0x0239, ...
    uint16_t reserved;       // 保留
    uint32_t size;           // 组件大小
    uint32_t offset;         // 组件偏移（相对文件开头）
    char     name[64];       // 组件名称（Unicode UTF-16LE）
} component_entry_t;
```

**打包流程（伪代码）：**

```c
int pack_loader(const char *ini_file)
{
    loader_header_t header = {0};
    component_entry_t entries[MAX_COMPONENTS];
    uint8_t *output_buf;
    int num_components = 0;

    // 1. 解析 INI 文件
    parse_ini_file(ini_file, &header, entries, &num_components);

    // 2. 构建 Header
    header.magic = 0x544F4F42;  // "BOOT"
    header.header_size = 0x0066;
    header.version = get_version_from_ini();
    header.year = current_year();
    header.month = current_month();
    strcpy(header.chip_tag, "!C033");  // RK3399

    // 3. 填充组件元信息
    uint32_t current_offset = ENTRY_ALIGN;  // 2048
    for (int i = 0; i < num_components; i++) {
        entries[i].offset = current_offset;
        entries[i].size = get_file_size(entries[i].path);
        current_offset += align_size(entries[i].size, 512);
    }

    // 4. 写入 Header（2048 字节）
    memcpy(output_buf, &header, sizeof(header));
    memcpy(output_buf + HEADER_META_OFFSET, entries, sizeof(entries));

    // 5. 写入组件二进制数据
    current_offset = ENTRY_ALIGN;
    for (int i = 0; i < num_components; i++) {
        uint8_t *data = read_file(entries[i].path);
        memcpy(output_buf + current_offset, data, entries[i].size);
        current_offset += align_size(entries[i].size, 512);
    }

    // 6. 计算校验和
    uint32_t crc = CRC_32(output_buf, current_offset);
    memcpy(output_buf + CRC_OFFSET, &crc, 4);

    // 7. 可选的 RC4 加密
    if (enableRC4) {
        P_RC4(output_buf + ENTRY_ALIGN, current_offset - ENTRY_ALIGN);
    }

    // 8. 写入输出文件
    write_file(output_path, output_buf, current_offset);

    return 0;
}
```

### 4.3 CRC32 和 RC4 加密

**CRC32 校验和：**
```c
uint32_t CRC_32(uint8_t *pData, uint32_t ulSize)
{
    uint32_t i;
    uint32_t nAccum = 0;
    for (i = 0; i < ulSize; i++) {
        nAccum = (nAccum << 8) ^ gTable_Crc32[(nAccum >> 24) ^ (*pData++)];
    }
    return nAccum;
}
```

**RC4 加密（可选）：**
```c
void P_RC4(uint8_t *buf, uint32_t len)
{
    uint8_t S[256], K[256], temp;
    uint8_t key[16] = { 124, 78, 3, 4, 85, 5, 9, 7,
                        45, 44, 123, 56, 23, 13, 23, 17 };

    // KSA (Key-Scheduling Algorithm)
    for (i = 0; i < 256; i++) {
        S[i] = (uint8_t) i;
        K[i] = key[i % 16];
    }

    j = 0;
    for (i = 0; i < 256; i++) {
        j = (j + S[i] + K[i]) % 256;
        swap(S[i], S[j]);
    }

    // PRGA (Pseudo-Random Generation Algorithm)
    i = j = 0;
    for (x = 0; x < len; x++) {
        i = (i + 1) % 256;
        j = (j + S[i]) % 256;
        swap(S[i], S[j]);
        t = (S[i] + S[j]) % 256;
        buf[x] = buf[x] ^ S[t];
    }
}
```

---

## 5. 配置文件：RK3399MINIALL.ini

### 5.1 完整内容

文件位置：`external/rkbin/RKBOOT/RK3399MINIALL.ini`

```ini
[CHIP_NAME]
NAME=RK330C          # 芯片内部代号

[VERSION]
MAJOR=1              # 主版本号
MINOR=19             # 次版本号

[CODE471_OPTION]
NUM=1
Path1=bin/rk33/rk3399_ddr_800MHz_v1.22.bin
Sleep=1

[CODE472_OPTION]
NUM=1
Path1=bin/rk33/rk3399_usbplug_v1.19.bin

[LOADER_OPTION]
NUM=2                # 组件数量
LOADER1=FlashData    # 第1个组件标签
LOADER2=FlashBoot    # 第2个组件标签
FlashData=bin/rk33/rk3399_ddr_800MHz_v1.22.bin      # DDR 初始化
FlashBoot=bin/rk33/rk3399_miniloader_v1.19.bin      # Miniloader

[OUTPUT]
PATH=rk3399_loader_v1.22.119.bin   # 输出文件名（版本号规则：MAJOR.MINOR）
```

### 5.2 配置项详解

#### [CHIP_NAME] 节

```ini
NAME=RK330C
```
- `RK330C` 是 RK3399 的**内部芯片代号**
- 这个值会被写入 Loader Header 的 `chip_tag` 字段（转换为 `!C033`）

#### [VERSION] 节

```ini
MAJOR=1
MINOR=19
```
- 决定输出文件名：`rk3399_loader_v1.22.119.bin`
  - `1` = MAJOR
  - `22` = DDR Init 的版本号（从文件名提取）
  - `119` = 19 + 100（MINOR + 100）

#### [CODE471_OPTION] 节

```ini
NUM=1
Path1=bin/rk33/rk3399_ddr_800MHz_v1.22.bin
Sleep=1
```
- `CODE471` 是 Rockchip 内部的组件类型代码
- `Path1` 指向 DDR 初始化二进制文件
- `Sleep=1` 表示执行后延迟 1ms

**DDR Init 的作用：**
- 初始化 DDR3/LPDDR4 内存控制器
- 设置内存时序参数（800MHz）
- 执行内存训练（Training）
- 在 SRAM 中运行，完成后被丢弃

#### [CODE472_OPTION] 节

```ini
NUM=1
Path1=bin/rk33/rk3399_usbplug_v1.19.bin
```
- `CODE472` 是 USB 下载模式的组件类型代码
- 用于 USB 烧录固件时的 Maskrom 模式

#### [LOADER_OPTION] 节（核心！）

```ini
NUM=2
LOADER1=FlashData
LOADER2=FlashBoot
FlashData=bin/rk33/rk3399_ddr_800MHz_v1.22.bin
FlashBoot=bin/rk33/rk3399_miniloader_v1.19.bin
```

**说明：**
- `NUM=2` 表示有 2 个主要组件
- `FlashData` 和 `FlashBoot` 是组件的逻辑标签
- 实际路径指向 `external/rkbin/bin/rk33/` 目录下的预编译二进制

**为什么 FlashData 指向 DDR Init？**
- 因为 DDR 初始化是第一步，必须在访问 Flash 之前完成
- 它负责初始化内存，为后续的 Miniloader 提供运行环境

**FlashBoot 的作用：**
- `rk3399_miniloader_v1.19.bin` 是 Miniloader
- 负责初始化 eMMC/SD 卡
- 加载 U-Boot 和 Trust
- 提供 USB 烧录功能（Rockusb 协议）

#### [OUTPUT] 节

```ini
PATH=rk3399_loader_v1.22.119.bin
```
- 指定输出文件名
- 版本号规则：
  - `v1` = MAJOR 版本
  - `22` = DDR Init 文件名中的版本号
  - `119` = MINOR + 100

### 5.3 组件文件来源

这些二进制文件是 **Rockchip 官方预编译的闭源组件**：

```bash
$ ls -lh external/rkbin/bin/rk33/

# DDR 初始化组件（不同内存频率）
rk3399_ddr_333MHz_v1.20.bin
rk3399_ddr_666MHz_v1.22.bin
rk3399_ddr_800MHz_v1.22.bin    ← 使用这个（800MHz）
rk3399_ddr_933MHz_v1.20.bin

# Miniloader 组件
rk3399_miniloader_v1.15.bin
rk3399_miniloader_v1.19.bin    ← 使用这个
rk3399_miniloader_v1.26.bin

# USB 下载工具
rk3399_usbplug_v1.19.bin
rk3399_usbplug_v1.27.bin

# BL31 (ARM Trusted Firmware)
rk3399_bl31_v1.35.elf
rk3399_bl31_v1.36.elf

# BL32 (OP-TEE OS)
rk3399bl32_v2.01.bin
rk3399bl32_v2.10.bin
```

**注意：**
- 这些文件**不是**从源码编译的
- 来自 Rockchip 官方的 `rkbin` 仓库
- 包含专有的硬件初始化代码和算法

---

## 6. 完整构建流程示例

### 6.1 手动执行完整构建

```bash
# 1. 进入 U-Boot 目录
cd uboot

# 2. 配置 RK3399
make rk3399_defconfig

# 3. 编译 U-Boot
make CROSS_COMPILE=aarch64-linux-gnu- all -j8

# 生成文件：
# - u-boot.bin
# - u-boot.dtb
# - u-boot (ELF)

# 4. 打包 uboot.img
../external/rkbin/tools/loaderimage --pack --uboot ./u-boot.bin \
    uboot.img 0x00200000

# 5. 打包 trust.img
cd ../external/rkbin
./tools/trust_merger --replace tools/rk_tools/ ./ \
    RKTRUST/RK3399TRUST.ini

# 6. 打包 loader bin（关键步骤！）
./tools/boot_merger --replace tools/rk_tools/ ./ \
    RKBOOT/RK3399MINIALL.ini

# 生成文件：rk3399_loader_v1.22.119.bin

# 7. 生成 idbloader.img（SD 卡启动用）
cd ../../uboot
../external/rkbin/tools/mkimage -n rk3399 -T rksd \
    -d ../external/rkbin/bin/rk33/rk3399_ddr_800MHz_v1.22.bin \
    idbloader.img

# 追加 miniloader
cat ../external/rkbin/bin/rk33/rk3399_miniloader_v1.19.bin >> idbloader.img

# 8. 查看生成的文件
ls -lh *.img *.bin
# uboot.img                      - U-Boot 镜像
# trust.img                      - Trust 镜像（ATF + OP-TEE）
# rk3399_loader_v1.22.119.bin    - Loader 完整包
# idbloader.img                  - SD 卡启动镜像
```

### 6.2 使用项目脚本构建

```bash
# 1. 进入项目根目录
cd OrangePiRK3399

# 2. 执行构建脚本（需要 root 权限）
sudo ./build.sh

# 3. 选择平台
#    → OrangePi rk3399

# 4. 选择构建选项
#    → Build Uboot (选项 2)

# 5. 等待编译完成
# 输出文件位于: output/uboot/
# - uboot.img
# - trust.img
# - rk3399_loader_v1.22.119.bin
# - idbloader.img
```

### 6.3 验证生成的 Loader

```bash
# 查看 Loader Header
hexdump -C rk3399_loader_v1.22.119.bin -n 2048 | head -20

# 应该看到：
# 00000000  42 4f 4f 54 ...    # "BOOT" 魔数
# 00000010  ... 21 43 30 33 33  # "!C033" 芯片标识
# 00000060  ... 72 00 6b 00 33 00  # "rk3399_ddr_..." (Unicode)

# 提取 DDR Init 组件
dd if=rk3399_loader_v1.22.119.bin of=ddr_extracted.bin \
   bs=1 skip=2048 count=131072

# 提取 Miniloader 组件
dd if=rk3399_loader_v1.22.119.bin of=miniloader_extracted.bin \
   bs=1 skip=133120

# 对比原始文件
md5sum ddr_extracted.bin
md5sum ../external/rkbin/bin/rk33/rk3399_ddr_800MHz_v1.22.bin
# 应该一致（除了可能的填充字节）
```

---

## 7. 源码级别的打包实现

### 7.1 boot_merger 核心代码片段

文件位置：`uboot/tools/rockchip/boot_merger.c`

```c
// 主要的打包函数
int mergeBoot(const char *path)
{
    // 1. 解析 INI 配置文件
    if (!parse_ini_file(path)) {
        return -1;
    }

    // 2. 分配输出缓冲区
    gBuf = (uint8_t *)malloc(g_merge_max_size);
    if (!gBuf) {
        return -1;
    }
    memset(gBuf, 0, g_merge_max_size);

    // 3. 写入 Loader Header
    write_loader_header();

    // 4. 写入组件元信息
    uint32_t offset = ENTRY_ALIGN;  // 2048
    for (int i = 0; i < gOpts.loaderNum; i++) {
        write_component_entry(i, offset);
        offset += align_size(gOpts.loaderSize[i], 512);
    }

    // 5. 写入组件二进制数据
    offset = ENTRY_ALIGN;
    for (int i = 0; i < gOpts.loaderNum; i++) {
        read_component_data(gOpts.loaderPath[i], gBuf + offset);
        offset += align_size(gOpts.loaderSize[i], 512);
    }

    // 6. 计算 CRC32
    uint32_t crc = CRC_32(gBuf, offset);
    memcpy(gBuf + CRC_OFFSET, &crc, sizeof(crc));

    // 7. RC4 加密（如果启用）
    if (enableRC4) {
        P_RC4(gBuf + ENTRY_ALIGN, offset - ENTRY_ALIGN);
    }

    // 8. 写入输出文件
    FILE *fp = fopen(gOpts.outputPath, "wb");
    fwrite(gBuf, 1, offset, fp);
    fclose(fp);

    free(gBuf);
    return 0;
}

// 写入 Loader Header
void write_loader_header()
{
    loader_header_t *header = (loader_header_t *)gBuf;

    // 魔数
    memcpy(&header->magic, "BOOT", 4);

    // 版本信息
    header->header_size = 0x0066;
    header->version = (gOpts.major << 8) | gOpts.minor;
    header->merge_version = 0x0103;

    // 时间戳
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    header->year = t->tm_year + 1900;
    header->month = t->tm_mon + 1;
    header->day = t->tm_mday;
    header->hour = t->tm_hour;
    header->minute = t->tm_min;
    header->second = t->tm_sec;

    // 芯片标识
    memcpy(header->chip_tag, "!C033", 5);  // RK3399
}

// 写入组件元信息
void write_component_entry(int index, uint32_t offset)
{
    component_entry_t *entry = (component_entry_t *)(gBuf + 0x20 + index * 16);

    // 组件类型
    entry->type = gOpts.loaderType[index];  // 0x0139, 0x0239, ...

    // 组件大小和偏移
    entry->size = gOpts.loaderSize[index];
    entry->offset = offset;

    // 组件名称（Unicode UTF-16LE）
    const char *name = gOpts.loaderName[index];
    uint16_t *name_unicode = (uint16_t *)(gBuf + gOpts.nameOffset[index]);
    for (int i = 0; name[i] != '\0'; i++) {
        name_unicode[i] = (uint16_t)name[i];  // ASCII -> UTF-16LE
    }
}

// 读取组件数据
void read_component_data(const char *path, uint8_t *dest)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("Failed to open: %s\n", path);
        return;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 读取数据
    fread(dest, 1, size, fp);
    fclose(fp);

    // 对齐到 512 字节（扇区大小）
    size_t padding = (512 - (size % 512)) % 512;
    if (padding > 0) {
        memset(dest + size, 0, padding);
    }
}
```

### 7.2 loaderimage 打包 uboot.img

虽然没有提供 loaderimage 的源码，但可以推断其实现：

```c
// loaderimage 伪代码实现
int pack_uboot_image(const char *uboot_bin, const char *output, uint32_t load_addr)
{
    uint8_t header[2048] = {0};
    uint8_t *uboot_data;
    size_t uboot_size;

    // 1. 读取 u-boot.bin
    FILE *fp = fopen(uboot_bin, "rb");
    fseek(fp, 0, SEEK_END);
    uboot_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uboot_data = malloc(uboot_size);
    fread(uboot_data, 1, uboot_size, fp);
    fclose(fp);

    // 2. 构建 Header
    uint32_t *h32 = (uint32_t *)header;
    h32[0] = 0x0FF0AA55;     // 魔数
    h32[1] = uboot_size;     // 大小
    h32[2] = load_addr;      // 加载地址 (0x00200000)
    h32[4] = load_addr;      // 入口点地址

    // 芯片型号
    memcpy(header + 20, "RK33", 4);

    // CRC32
    uint32_t crc = CRC_32(uboot_data, uboot_size);
    memcpy(header + 24, &crc, 4);

    // SHA256
    uint8_t sha[32];
    sha256(uboot_data, uboot_size, sha);
    memcpy(header + 28, sha, 32);

    // 3. 写入输出文件
    FILE *out = fopen(output, "wb");
    fwrite(header, 1, 2048, out);     // Header (2KB)
    fwrite(uboot_data, 1, uboot_size, out);  // U-Boot 数据

    // 对齐到 4KB
    size_t padding = (4096 - ((2048 + uboot_size) % 4096)) % 4096;
    uint8_t pad[4096] = {0};
    fwrite(pad, 1, padding, out);

    fclose(out);
    free(uboot_data);

    return 0;
}
```

### 7.3 mkimage 生成 SD 卡启动头

```c
// mkimage 针对 RK3399 的实现（简化版）
int generate_rksd_image(const char *ddr_bin, const char *output)
{
    uint8_t header[512] = {0};
    uint8_t *ddr_data;
    size_t ddr_size;

    // 1. 读取 DDR Init 数据
    FILE *fp = fopen(ddr_bin, "rb");
    fseek(fp, 0, SEEK_END);
    ddr_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    ddr_data = malloc(ddr_size);
    fread(ddr_data, 1, ddr_size, fp);
    fclose(fp);

    // 2. 构建 SD 卡启动头部（512 字节）
    // RK3399 SD 卡启动签名
    header[0] = 'R';
    header[1] = 'K';
    header[2] = '3';
    header[3] = '3';

    // 数据大小（以 512 字节为单位）
    uint16_t blocks = (ddr_size + 511) / 512;
    memcpy(header + 4, &blocks, 2);

    // 加载地址（SRAM）
    uint32_t load_addr = 0xFF8C0000;  // RK3399 SRAM 地址
    memcpy(header + 8, &load_addr, 4);

    // 3. 写入输出文件
    FILE *out = fopen(output, "wb");
    fwrite(header, 1, 512, out);       // SD 头部
    fwrite(ddr_data, 1, ddr_size, out); // DDR Init 数据

    // 对齐到 512 字节
    size_t padding = (512 - (ddr_size % 512)) % 512;
    uint8_t pad[512] = {0};
    fwrite(pad, 1, padding, out);

    fclose(out);
    free(ddr_data);

    return 0;
}
```

---

## 8. 总结

### 8.1 构建流程总结

```
用户输入                        实际操作                          生成文件
───────────────────────────────────────────────────────────────────────────
./build.sh
  └─ 选择平台: OrangePi rk3399
  └─ 选择选项: Build Uboot
                                ↓
                          uboot/make.sh rk3399
                                ↓
                          make rk3399_defconfig          → .config
                                ↓
                          make CROSS_COMPILE=... all     → u-boot.bin
                                ↓                          → u-boot.dtb
                          loaderimage --pack             → uboot.img
                                ↓
                          trust_merger ...               → trust.img
                                ↓
                          boot_merger                    → rk3399_loader_v*.bin
                            ├─ 读取 RK3399MINIALL.ini
                            ├─ 合并 rk3399_ddr_800MHz_v1.22.bin
                            └─ 合并 rk3399_miniloader_v1.19.bin
                                ↓
                          mkimage -T rksd ...            → idbloader.img
                          cat miniloader.bin >>
                                ↓
                          复制到 output/uboot/
```

### 8.2 关键组件说明

| 组件 | 大小 | 来源 | 作用 |
|------|------|------|------|
| `rk3399_ddr_800MHz_v1.22.bin` | ~130KB | Rockchip 预编译 | DDR 内存初始化（800MHz） |
| `rk3399_miniloader_v1.19.bin` | ~245KB | Rockchip 预编译 | 加载 U-Boot，支持 USB 烧录 |
| `u-boot.bin` | ~600KB | 项目编译 | U-Boot 主程序 |
| `bl31.elf` | ~80KB | Rockchip 预编译 | ARM Trusted Firmware |
| `op-tee.bin` | ~200KB | Rockchip 预编译（可选） | 可信执行环境 |

### 8.3 文件用途对比

```
┌─────────────────────────────────────────────────────────────────┐
│ 文件名                        │ 用途                             │
├─────────────────────────────────────────────────────────────────┤
│ rk3399_loader_v1.22.119.bin  │ USB 烧录工具使用（upgrade_tool） │
│                              │ 包含完整的 Loader 组件           │
├─────────────────────────────────────────────────────────────────┤
│ idbloader.img                │ SD 卡/eMMC 启动用               │
│                              │ 仅包含 DDR Init + Miniloader     │
├─────────────────────────────────────────────────────────────────┤
│ uboot.img                    │ U-Boot 主程序镜像                │
│                              │ 被 Miniloader 加载               │
├─────────────────────────────────────────────────────────────────┤
│ trust.img                    │ Trusted 组件（ATF + OP-TEE）    │
│                              │ 被 Miniloader 加载               │
└─────────────────────────────────────────────────────────────────┘
```

### 8.4 关键配置文件

```
RK3399MINIALL.ini               # Loader 打包配置
  ├─ [CHIP_NAME]: RK330C        # 芯片代号
  ├─ [VERSION]: 1.19            # 版本号
  ├─ [LOADER_OPTION]
  │   ├─ FlashData → rk3399_ddr_800MHz_v1.22.bin
  │   └─ FlashBoot → rk3399_miniloader_v1.19.bin
  └─ [OUTPUT]: rk3399_loader_v1.22.119.bin
```

### 8.5 调试技巧

```bash
# 1. 查看 Loader Header
hexdump -C rk3399_loader_v1.22.119.bin -n 2048

# 2. 提取组件
dd if=rk3399_loader_v1.22.119.bin of=ddr.bin bs=1 skip=2048 count=131072
dd if=rk3399_loader_v1.22.119.bin of=miniloader.bin bs=1 skip=133120

# 3. 查看字符串
strings miniloader.bin | grep -i "rockchip\|version"

# 4. 反汇编（需要 ARM 工具链）
aarch64-linux-gnu-objdump -D -b binary -m aarch64 miniloader.bin > miniloader.asm

# 5. 验证 CRC32
python3 -c "
import zlib
data = open('rk3399_loader_v1.22.119.bin', 'rb').read()
print(hex(zlib.crc32(data) & 0xFFFFFFFF))
"
```

---

## 附录：完整命令速查

```bash
# A. 完整构建流程（自动）
cd OrangePiRK3399
sudo ./build.sh
# → 选择 OrangePi rk3399 → Build Uboot

# B. 手动构建 U-Boot
cd uboot
make rk3399_defconfig
make CROSS_COMPILE=aarch64-linux-gnu- all -j8

# C. 手动打包镜像
# 1. uboot.img
../external/rkbin/tools/loaderimage --pack --uboot ./u-boot.bin uboot.img 0x00200000

# 2. trust.img
cd ../external/rkbin
./tools/trust_merger --replace tools/rk_tools/ ./ RKTRUST/RK3399TRUST.ini

# 3. loader bin
./tools/boot_merger --replace tools/rk_tools/ ./ RKBOOT/RK3399MINIALL.ini

# 4. idbloader.img
cd ../../uboot
../external/rkbin/tools/mkimage -n rk3399 -T rksd \
    -d ../external/rkbin/bin/rk33/rk3399_ddr_800MHz_v1.22.bin idbloader.img
cat ../external/rkbin/bin/rk33/rk3399_miniloader_v1.19.bin >> idbloader.img

# D. 验证生成的文件
ls -lh *.img *.bin
hexdump -C rk3399_loader_v1.22.119.bin -n 2048 | head -20
```

---

## RK3399 Loader 固件地址结构详解

### 一、存储设备上的地址布局(物理存储)

#### 1. eMMC/SD 卡上的扇区地址

存储设备扇区布局(每扇区 512 字节)
  ┌────────────────────────────────────────────────┐
  │ 扇区 0-63        (0x0 - 0x7E00)                │
  │ ├─ MBR/GPT 分区表                               │
  │ └─ 保留区域                                     │
  ├────────────────────────────────────────────────┤
  │ 扇区 64          (0x8000, 32KB 偏移)           │  ← BootROM 从这里开始读取
  │ ├─ loader.bin 起始位置                          │
  │ │   ├─ rk_boot_header (104 字节)               │
  │ │   ├─ Entry 数组 (元数据)                      │
  │ │   └─ 实际数据区 (DDR Init、Miniloader)        │
  │ └─ idbloader.img (同样从扇区 64 开始)          │
  ├────────────────────────────────────────────────┤
  │ 扇区 24576       (0xC00000, 12MB 偏移)         │
  │ └─ uboot 分区 (uboot.img)                      │
  ├────────────────────────────────────────────────┤
  │ 扇区 32768       (0x1000000, 16MB 偏移)        │
  │ └─ trust 分区 (trust.img)                      │
  ├────────────────────────────────────────────────┤
  │ 扇区 49152       (0x1800000, 24MB 偏移)        │
  │ └─ boot 分区 (kernel + DTB + initramfs)        │
  └────────────────────────────────────────────────┘

**关键点：**
- 扇区 64 (32KB) 是 BootROM 读取的起点
- 1 扇区 = 512 字节
- 扇区 64 = 64 × 512 = 32768 字节 = 0x8000

### 二、loader.bin 固件内部地址结构

#### 1. 文件内部偏移布局 (loader.bin)

loader.bin 内部结构 (文件偏移地址视角)

  文件偏移 0x0:
  ┌──────────────────────────────────────────────────┐
  │ rk_boot_header (104 字节)                         │  文件偏移 0x0 - 0x67
  │ ┌──────────────────────────────────────────────┐ │
  │ │ +0x00: tag          = 0x544F4F42 ("BOOT")   │ │
  │ │ +0x04: size         = 104 (0x68)            │ │
  │ │ +0x06: version      = 0x0250 (v2.50)        │ │
  │ │ +0x0A: mergerVersion                         │ │
  │ │ +0x0E: releaseTime  (7 字节)                 │ │
  │ │ +0x15: chipType     = 0x33333939 ("3399")   │ │
  │ ├──────────────────────────────────────────────┤ │
  │ │ CODE471 Entry 数组信息 (DDR Init 元数据)      │ │
  │ │ +0x19: code471Num    = 1   (Entry 数量)      │ │
  │ │ +0x1A: code471Offset = 104 (Entry 数组偏移)  │ │  ← 指向下方 Entry 数组
  │ │ +0x1E: code471Size   = 57  (每个 Entry 大小) │ │
  │ ├──────────────────────────────────────────────┤ │
  │ │ CODE472 Entry 数组信息 (USB 插件元数据)      │ │
  │ │ +0x1F: code472Num    = 1                     │ │
  │ │ +0x20: code472Offset = 161 (0x68 + 57)      │ │
  │ │ +0x24: code472Size   = 57                    │ │
  │ ├──────────────────────────────────────────────┤ │
  │ │ Loader Entry 数组信息 (Miniloader 元数据)    │ │
  │ │ +0x25: loaderNum     = 2                     │ │
  │ │ +0x26: loaderOffset  = 218 (0x68 + 57×2)    │ │  ← 指向下方 Loader Entry
  │ │ +0x2A: loaderSize    = 57                    │ │
  │ ├──────────────────────────────────────────────┤ │
  │ │ +0x2B: signFlag     = 0                      │ │
  │ │ +0x2C: rc4Flag      = 1 (禁用 RC4)           │ │
  │ │ +0x2D: reserved[57] (预留 57 字节)           │ │
  │ └──────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────┘

  文件偏移 0x68 (104):
  ┌──────────────────────────────────────────────────┐
  │ CODE471 Entry[0] (57 字节)                        │  偏移 104 - 160
  │ ┌──────────────────────────────────────────────┐ │
  │ │ +0x00: size       = 57                       │ │
  │ │ +0x01: type       = ENTRY_471 (1)            │ │
  │ │ +0x05: name       = "rk3399_ddr_800MHz..."   │ │
  │ │ +0x2D: dataOffset = 332 (0x14C)              │ │  ← 指向实际 DDR 代码
  │ │ +0x31: dataSize   = 102400 (100KB)           │ │
  │ │ +0x35: dataDelay  = 0                        │ │
  │ └──────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────┘

  文件偏移 0xA1 (161):
  ┌──────────────────────────────────────────────────┐
  │ CODE472 Entry[0] (57 字节)                        │  偏移 161 - 217
  │ ┌──────────────────────────────────────────────┐ │
  │ │ +0x2D: dataOffset = 102732 (0x1914C)         │ │  ← 指向 USB 插件代码
  │ │ +0x31: dataSize   = 8192 (8KB)               │ │
  │ └──────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────┘

  文件偏移 0xDA (218):
  ┌──────────────────────────────────────────────────┐
  │ Loader Entry[0] - FlashData (57 字节)            │  偏移 218 - 274
  │ ┌──────────────────────────────────────────────┐ │
  │ │ +0x2D: dataOffset = 110924 (0x1B13C)         │ │  ← 存储驱动代码
  │ │ +0x31: dataSize   = 16384 (16KB)             │ │
  │ └──────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────┘

  文件偏移 0x113 (275):
  ┌──────────────────────────────────────────────────┐
  │ Loader Entry[1] - FlashBoot (57 字节)            │  偏移 275 - 331
  │ ┌──────────────────────────────────────────────┐ │
  │ │ +0x2D: dataOffset = 127308 (0x1F13C)         │ │  ← Miniloader 主程序
  │ │ +0x31: dataSize   = 344064 (336KB)           │ │
  │ └──────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────┘

  文件偏移 0x14C (332):
  ┌──────────────────────────────────────────────────┐
  │ 【实际数据区开始】                                 │
  │ DDR Init 代码 (102400 字节)                       │  偏移 332 - 102731
  │ - ARM64 汇编指令                                  │
  │ - DDR 控制器初始化                                │
  │ - 内存训练代码                                    │
  └──────────────────────────────────────────────────┘

  文件偏移 0x1914C (102732):
  ┌──────────────────────────────────────────────────┐
  │ USB 插件代码 (8192 字节)                          │  偏移 102732 - 110923
  │ - USB 通信协议                                    │
  │ - Maskrom 模式烧录支持                            │
  └──────────────────────────────────────────────────┘

  文件偏移 0x1B13C (110924):
  ┌──────────────────────────────────────────────────┐
  │ FlashData - 存储驱动 (16384 字节)                │  偏移 110924 - 127307
  │ - eMMC/SD 卡驱动代码                              │
  │ - SPI Flash 驱动代码                              │
  └──────────────────────────────────────────────────┘

  文件偏移 0x1F13C (127308):
  ┌──────────────────────────────────────────────────┐
  │ FlashBoot - Miniloader (344064 字节)              │  偏移 127308 - 471371
  │ - Miniloader 主程序                               │
  │ - GPT 分区表解析                                  │
  │ - U-Boot/Trust 加载器                             │
  └──────────────────────────────────────────────────┘

文件总大小: ~470KB

### 三、内存中的加载地址(运行时地址)

#### 1. BootROM 加载到内存的地址

内存地址布局 (运行时)

  ┌──────────────────────────────────────────────────┐
  │ 0xFFFF0000 - BootROM 固化代码区                   │  ← CPU 复位向量
  │ └─ Rockchip BootROM 固件                          │
  ├──────────────────────────────────────────────────┤
  │ 0xFF8C0000 - SRAM (256KB)                         │  ← 系统 SRAM
  │ ├─ BootROM 加载 DDR Init 代码到这里                │
  │ └─ DDR Init 代码执行完后,SRAM 可被后续代码使用    │
  ├──────────────────────────────────────────────────┤
  │ 0x00000000 - DDR 内存起始地址                     │  ← DDR Init 成功后可用
  │ ├─ 0x00010000: trust.img 加载地址 (64KB 偏移)     │
  │ ├─ 0x00200000: uboot.img 加载地址 (2MB 偏移)      │
  │ ├─ 0x08400000: OP-TEE 加载地址 (132MB 偏移)       │  ← ARM32 平台
  │ └─ 更高地址: kernel、rootfs 等                     │
  └──────────────────────────────────────────────────┘

  关键地址说明：

  1. SRAM 地址 (0xFF8C0000, 256KB)
    - BootROM 将 DDR Init 代码加载到这里
    - 大小限制: DDR Init 代码必须 < 256KB
    - DDR 初始化完成后，SRAM 可被后续代码复用
  2. DDR 加载地址
  // 从 make.sh:1028 中可以看到计算方式
  DARM_BASE=0x00000000               // DRAM 基址 (CONFIG_SYS_SDRAM_BASE)
  TEE_OFFSET=0x8400000               // OP-TEE 偏移 (132MB)
  TEE_LOAD_ADDR = DARM_BASE + TEE_OFFSET = 0x8400000

UBOOT_LOAD_ADDR=0x00200000         // U-Boot 加载地址 (2MB)
TRUST_LOAD_ADDR=0x00010000         // Trust (ATF) 加载地址 (64KB)

### 四、地址计算与查询实例

#### 1. 如何使用 make.sh 查询地址

从 make.sh:479-514 可以看到地址查询功能：

  # 查询未重定位的地址

  ./make.sh 0x00200000

  # 查询重定位后的地址 (地址 - 重定位偏移)
  ./make.sh 0x00300000-0x8400000

  # 输出示例：
  # 0x200000 in u-boot.sym:
  # 00200000 T _start                    ← 符号表匹配
  # arch/arm/cpu/armv8/start.S:26        ← 源代码位置

  地址查询原理：
  # make.sh:507-510
  sed -n "/${FUNCADDR}/p" ${sym}           # 搜索符号表
${TOOLCHAIN_ADDR2LINE} -e ${elf} ${FUNCADDR}  # 转换为源码行号

### 五、地址相关的关键宏定义

从 uboot/make.sh 中可以看到这些地址的来源：

  # make.sh:781 - U-Boot 加载地址
  UBOOT_LOAD_ADDR=$(sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" \
                    ${OUTDIR}/include/autoconf.mk)
  # 典型值: 0x00200000 (2MB 偏移)

  # make.sh:1028 - OP-TEE 加载地址 (ARM32)
  DARM_BASE=$(sed -n "/CONFIG_SYS_SDRAM_BASE=/s/CONFIG_SYS_SDRAM_BASE=//p" \
              ${OUTDIR}/include/autoconf.mk)
TEE_LOAD_ADDR=$((DARM_BASE + 0x8400000))  # 132MB 偏移

### 六、地址对齐要求

从 uboot/tools/rockchip/boot_merger.c 可以看到对齐要求：

  #pragma pack(1)  // 按 1 字节对齐,避免编译器填充
  typedef struct {
      uint32_t tag;      // 偏移 0
      uint16_t size;     // 偏移 4
      // ...
  } rk_boot_header;

关键点：
- 头部结构使用 1 字节对齐
- Entry 数组每个 57 字节
- 实际数据可以 2KB 对齐(提高存储效率)

### 七、总结

RK3399 Loader 地址结构的三个层次：

1. **存储地址**: 扇区 64 (32KB) 开始
2. **文件偏移**: Header(104) → Entry 数组(104+) → 数据区(332+)
3. **内存地址**: SRAM(0xFF8C0000) → DDR(0x00000000+)

**地址关系图：**
```
存储设备 (扇区 64)
    ↓ BootROM 读取
SRAM (0xFF8C0000) ← DDR Init 执行
    ↓ DDR 初始化成功
DDR (0x00000000)
    ├─ 0x00010000: Trust (ATF)
    ├─ 0x00200000: U-Boot
    └─ 0x08400000: OP-TEE (可选)
```

**完成时间：** 2025-12-07
**作者：** Claude Code
**版本：** v1.0
**相关文档：**
- [Loader二进制与打包代码对应关系教程.md](./Loader二进制与打包代码对应关系教程.md)
- [固件打包原理深度解析.md](./固件打包原理深度解析.md)
