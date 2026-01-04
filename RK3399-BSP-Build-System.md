# RK3399 BSP 构建系统详解

本文档详细讲解 Orange Pi RK3399 BSP 构建系统的架构、源码下载机制、固件组成和镜像打包流程。

---

## 一、整体架构（两阶段构建）

```
┌─────────────────────────────────────────────────────────────────┐
│                      Stage 1: SDK下载                           │
│                   (OrangePi_Build仓库)                          │
│  Build_OrangePi.sh → general.sh → git clone 5个子仓库           │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Stage 2: 实际构建                             │
│               (OrangePiRK3399/下载的SDK)                        │
│                    build.sh → 构建镜像                          │
└─────────────────────────────────────────────────────────────────┘
```

**阶段1** 由本仓库（OrangePi_Build）负责，主要功能是从GitHub下载5个子仓库到本地。

**阶段2** 在下载的SDK目录（OrangePiRK3399/）中进行，执行实际的编译和打包工作。

---

## 二、源码下载机制

### 2.1 配置定义 (`general.sh` 第4-10行)

```bash
PLATFORM="OrangePiRK3399"
KERNEL=("OrangePiRK3399_kernel" "master")           # 仓库名 + 分支
UBOOT=("OrangePiRK3399_uboot" "master")
SCRIPTS=("OrangePiRK3399_scripts" "orangepi-rk3399_v1.4")
EXTERNAL=("OrangePiRK3399_external" "orangepi-rk3399_v1.4")
TOOLCHAIN=("toolchain" "aarch64-linux-gnu-6.3")     # 交叉编译工具链
ORANGEPI_GIT="https://github.com/orangepi-xunlong"
```

### 2.2 下载流程 (`download_code()` 第81-142行)

```bash
# 组装5个仓库的下载地址和本地目录
SDK_GIT=(kernel, uboot, scripts, external, toolchain)
SDK_DIR=(kernel, uboot, scripts, external, toolchain)

# 浅克隆，节省带宽
git clone --depth=1 "${SDK_GIT[i]}" "${PLAT_DIR}/${SDK_DIR[i]}" --branch "${SDK_BR[i]}"
```

### 2.3 下载的目录结构

```
OrangePiRK3399/
├── kernel/      # Linux内核源码 (linux 4.4.179)
├── uboot/       # U-Boot源码
├── scripts/     # 构建脚本
├── external/    # 外部包、固件、预编译库
├── toolchain/   # 交叉编译工具链 (gcc-linaro-6.3.1)
└── build.sh     # 符号链接 → scripts/build.sh
```

---

## 三、RK3399平台特有配置

### 3.1 平台变量定义 (`build.sh` 第142-150行)

```bash
"OrangePiRK3399")
    BOARD="4"                      # Orange Pi 4
    ARCH="arm64"                   # 64位ARM架构
    CHIP="RK3399"                  # SoC型号
    TOOLS=$ROOT/toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
    KERNEL_NAME="linux4.4.179"     # 内核版本
```

### 3.2 支持的开发板

| 开发板 | BOARD值 | SoC |
|--------|---------|-----|
| Orange Pi 4 | "4" | RK3399 |
| Orange Pi RK3399 | "rk3399" | RK3399 |

---

## 四、RK3399固件组成（BSP核心）

RK3399使用**多级启动**架构，固件存放在 `external/rkbin/` 目录。

### 4.1 rkbin目录结构

```
rkbin/
├── bin/rk33/                    # 二进制固件（Rockchip提供，闭源）
│   ├── rk3399_bl31_v1.28.elf    # ARM Trusted Firmware (ATF/BL31)
│   ├── rk3399_bl32_v1.18.bin    # OP-TEE (可信执行环境)
│   ├── rk3399_ddr_800MHz_v1.22.bin  # DDR初始化固件
│   └── rk3399_miniloader_v1.22.bin  # 一级加载器
├── RKBOOT/                      # loader打包配置
│   └── RK3399MINIALL.ini        # loader.bin打包规则
├── RKTRUST/                     # trust固件打包配置
│   └── RK3399TRUST.ini          # trust.img打包规则
└── tools/                       # 打包工具
    ├── trust_merger             # 合并trust固件
    ├── boot_merger              # 合并loader
    └── loaderimage              # 打包uboot镜像
```

### 4.2 固件说明

| 固件文件 | 说明 | 来源 |
|----------|------|------|
| rk3399_ddr_*.bin | DDR内存初始化代码 | Rockchip闭源 |
| rk3399_miniloader_*.bin | 一级加载器(SPL) | Rockchip闭源 |
| rk3399_bl31_*.elf | ARM Trusted Firmware | Rockchip基于ATF定制 |
| rk3399_bl32_*.bin | OP-TEE可信执行环境 | Rockchip定制 |

### 4.3 RK3399TRUST.ini 配置解析

```ini
[VERSION]
MAJOR=1
MINOR=0

[BL31_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl31_v1.28.elf   # ATF固件路径
ADDR=0x00010000                        # 加载地址

[BL32_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl32_v1.18.bin   # OP-TEE路径
ADDR=0x08400000                        # 加载地址（DRAM基址+132M偏移）

[OUTPUT]
PATH=trust.img
```

---

## 五、启动镜像布局（GPT分区）

### 5.1 分区表定义 (`build_image.sh` 第12-19行)

```bash
local LOADER1_START=64        # idbloader起始扇区
local UBOOT_START=24576       # uboot分区起始
local UBOOT_END=32767
local TRUST_START=32768       # trust分区起始
local TRUST_END=40959
local BOOT_START=49152        # boot分区起始
local BOOT_END=114687
local ROOTFS_START=376832     # rootfs分区起始
```

### 5.2 分区布局图

```
┌────────────────────────────────────────────────────────────────┐
│  扇区偏移        分区名        内容                            │
├────────────────────────────────────────────────────────────────┤
│  64             loader1      idbloader.img (DDR+miniloader)    │
│  24576-32767    uboot        uboot.img                         │
│  32768-40959    trust        trust.img (ATF+OP-TEE)            │
│  49152-114687   boot         boot.img (kernel+dtb+ramdisk)     │
│  376832-end     rootfs       ext4根文件系统                    │
└────────────────────────────────────────────────────────────────┘
```

### 5.3 镜像打包命令 (`build_rk_image()`)

```bash
# 创建GPT分区表
parted -s $IMAGE mklabel gpt
parted -s $IMAGE unit s mkpart uboot ${UBOOT_START} ${UBOOT_END}
parted -s $IMAGE unit s mkpart trust ${TRUST_START} ${TRUST_END}
parted -s $IMAGE unit s mkpart boot ${BOOT_START} ${BOOT_END}
parted -s $IMAGE -- unit s mkpart rootfs ${ROOTFS_START} -34s

# 写入各分区内容
dd if=$BUILD/uboot/idbloader.img of=$IMAGE seek=64
dd if=$BUILD/uboot/uboot.img of=$IMAGE seek=24576
dd if=$BUILD/uboot/trust.img of=$IMAGE seek=32768
dd if=$BUILD/kernel/boot.img of=$IMAGE seek=49152
dd if=${IMAGE}2 of=$IMAGE seek=376832   # rootfs
```

---

## 六、U-Boot编译流程

### 6.1 RK3399专用构建脚本 (`uboot/make.sh`)

与其他平台不同，RK3399的U-Boot使用专门的make.sh脚本进行编译和打包。

### 6.2 编译步骤

```bash
# 1. 配置并编译u-boot
make rk3399_defconfig
make CROSS_COMPILE=aarch64-linux-gnu-

# 2. 打包uboot.img（使用loaderimage工具）
${RKTOOLS}/loaderimage --pack --uboot u-boot.bin uboot.img ${LOAD_ADDR}

# 3. 打包idbloader.img (DDR初始化 + Miniloader)
${RKTOOLS}/mkimage -n rk3399 -T rksd -d ${DDR_BIN} idbloader.img
cat ${MINILOADER} >> idbloader.img

# 4. 打包trust.img（使用trust_merger工具）
${RKTOOLS}/trust_merger RK3399TRUST.ini  # 合并BL31+BL32
```

### 6.3 编译产物

| 文件 | 说明 | 用途 |
|------|------|------|
| idbloader.img | DDR初始化 + 一级加载器 | SD/eMMC启动必需 |
| uboot.img | U-Boot主体 | 二级引导程序 |
| trust.img | ATF + OP-TEE | 安全固件 |
| rk3399_loader_*.bin | 完整loader | USB烧录模式使用 |

### 6.4 工具链配置 (`make.sh` 第40-42行)

```bash
GCC_ARM64=aarch64-linux-gnu-
TOOLCHAIN_ARM64=../toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/
```

---

## 七、Kernel编译流程

### 7.1 编译命令 (`compilation.sh` 第112-118行)

```bash
"OrangePiRK3399")
    # 使用板级defconfig
    make ARCH=${ARCH} CROSS_COMPILE=$TOOLS ${BOARD}_linux_defconfig

    # 直接生成boot.img（内核+DTB+ramdisk打包）
    make ARCH=${ARCH} CROSS_COMPILE=$TOOLS rk3399-orangepi-${BOARD}.img

    # 编译内核模块
    make ARCH=${ARCH} CROSS_COMPILE=$TOOLS modules

    # 拷贝最终boot.img
    cp $LINUX/boot.img $BUILD/kernel
```

### 7.2 RK3399内核特点

- **defconfig**: `4_linux_defconfig` 或 `rk3399_linux_defconfig`
- **boot.img**: 由kernel Makefile直接生成，集成了DTB和ramdisk
- **设备树**: `rk3399-orangepi-4.dtb`
- **内核版本**: Linux 4.4.179 (BSP内核)

---

## 八、Rootfs构建流程

### 8.1 基础rootfs来源 (`distributions.sh` 第377-394行)

```bash
# Ubuntu 18.04 (bionic) base tarball
ROOTFS="https://mirrors.tuna.tsinghua.edu.cn/ubuntu-cdimage/ubuntu-base/releases/bionic/release/ubuntu-base-18.04.5-base-arm64.tar.gz"

# Debian使用debootstrap
METHOD="debootstrap"
```

### 8.2 构建步骤

```bash
prepare_env()          # 准备环境，下载/解压base tarball
prepare_rootfs_server()  # 安装基础包，创建用户
prepare_rootfs_desktop() # 安装桌面环境(LXDE)
server_setup()         # 通用服务器配置
desktop_setup()        # 桌面特有配置（RK3399 GPU/GStreamer）
```

### 8.3 RK3399特有配置 (`distributions.sh` 第206-215行)

```bash
# 1. 安装eMMC烧录工具
cp $BUILD/uboot/*.img $DEST/usr/local/lib/install_to_emmc/
cp $BUILD/kernel/boot.img $DEST/usr/local/lib/install_to_emmc/

# 2. 复制GPU固件（Mali T860）
cp -rf $EXTER/firmware/* $DEST/system/etc/firmware

# 3. 音频配置
cp -rf $EXTER/asound.state $DEST/var/lib/alsa/

# 4. 清空fstab（GPT分区不需要传统挂载配置）
echo "" > $DEST/etc/fstab
```

### 8.4 Server版配置 (`distributions.sh` 第580-584行)

```bash
if [ $PLATFORM = "OrangePiRK3399" ]; then
    echo "" > $DEST/etc/fstab
    echo "ttyFIQ0" >> $DEST/etc/securetty    # RK3399专用串口
    sed -i '/^TimeoutStartSec=/s/5min/15sec/' $DEST/lib/systemd/system/networking.service
    setup_resize-helper                       # 首次启动自动扩展分区
fi
```

---

## 九、RK3399桌面版专属配置 (`rk3399.sh`)

### 9.1 Mali GPU库安装

```bash
# Ubuntu 18.04 (bionic)
dpkg -X /packages/libmali/libmali-rk-midgard-t86x-r14p0_1.6-2_arm64.deb /tmp/libmali
cp /tmp/libmali/usr/lib/aarch64-linux-gnu/lib* /usr/lib/aarch64-linux-gnu/

# 安装定制X Server
cp -rfa /packages/xserver/xserver_for_bionic/* /
```

### 9.2 GStreamer + 硬件加速

```bash
# 编译安装 (compile_gst函数)

# 1. libdrm-rockchip: DRM驱动
unzip libdrm-rockchip-rockchip-2.4.74.zip
./autogen.sh --prefix=/usr && make && make install

# 2. MPP: Rockchip媒体处理平台（硬件编解码核心）
unzip mpp-release.zip
cd mpp-release/build/linux/aarch64
./make-Makefiles.bash && make && make install

# 3. gstreamer-rockchip: GStreamer RK插件
unzip gstreamer-rockchip.zip
./autogen.sh --prefix=/usr --enable-gst --disable-rkximage
make && make install

# 4. camera_engine_rkisp: ISP相机引擎
tar -xf camera_engine_rkisp.tar.xz
make CROSS_COMPILE=
# 安装librkisp.so, libgstrkisp.so等
```

### 9.3 首次启动分区自动扩展 (`setup_resize-helper`)

```bash
# 创建systemd服务
cat > "$DEST/lib/systemd/system/resize-helper.service" <<EOF
[Unit]
Description=Resize root filesystem to fit available disk space
After=systemd-remount-fs.service

[Service]
Type=oneshot
ExecStart=-/usr/sbin/resize-helper
ExecStartPost=/bin/systemctl disable resize-helper.service

[Install]
WantedBy=basic.target
EOF
```

---

## 十、external目录关键组件

```
external/
├── rkbin/                    # Rockchip闭源固件（最重要）
│   ├── bin/rk33/             # RK3399固件二进制
│   ├── RKBOOT/               # loader打包配置
│   ├── RKTRUST/              # trust打包配置
│   └── tools/                # 打包工具
│
├── packages/
│   ├── libmali/              # Mali GPU用户空间库 (T860)
│   │   └── libmali-rk-midgard-t86x-r14p0_1.6-2_arm64.deb
│   ├── xserver/              # 定制X Server（支持Mali）
│   │   ├── xserver_for_bionic/
│   │   ├── xserver_for_xenial/
│   │   └── xserver_for_stretch/
│   ├── source/               # 硬件加速相关源码
│   │   ├── mpp-release.zip   # Rockchip媒体处理平台
│   │   ├── gstreamer-rockchip.zip
│   │   ├── gstreamer-rockchip-extra.zip
│   │   ├── libdrm-rockchip-rockchip-2.4.74.zip
│   │   └── camera_engine_rkisp.tar.xz
│   ├── others/
│   │   ├── gstreamer/        # GStreamer预编译包
│   │   └── glmark2/          # GPU性能测试工具
│   ├── overlay/              # 覆盖到rootfs的文件
│   ├── opi_config_libs/      # Orange Pi配置工具
│   └── OPi.GPIO/             # Python GPIO库
│
├── firmware/                 # WiFi/BT固件
│   ├── *.hcd                 # 博通蓝牙固件
│   └── *.bin                 # WiFi固件
│
├── bluetooth/
│   ├── bt.sh                 # 蓝牙初始化脚本
│   └── brcm_patchram_plus/   # 博通蓝牙patch工具
│
├── asound.state              # ALSA音频配置
├── install_to_emmc           # eMMC安装脚本
└── ubuntu-base-18.04.5-base-arm64.tar.gz  # 缓存的rootfs包
```

---

## 十一、完整构建流程图

```
用户执行 sudo ./build.sh
           │
           ▼
    ┌──────────────────┐
    │  select_distro   │  选择Ubuntu/Debian + Desktop/Server
    └────────┬─────────┘
             │
    ┌────────▼─────────┐
    │  compile_uboot   │  → uboot/make.sh rk3399
    │                  │  → 输出: idbloader.img, uboot.img, trust.img
    └────────┬─────────┘
             │
    ┌────────▼─────────┐
    │  compile_kernel  │  → make 4_linux_defconfig
    │                  │  → make rk3399-orangepi-4.img
    │                  │  → 输出: boot.img
    └────────┬─────────┘
             │
    ┌────────▼─────────┐
    │  build_rootfs    │  → 下载ubuntu-base tarball
    │                  │  → 解压并chroot配置
    │                  │  → 安装包、GPU库、GStreamer
    └────────┬─────────┘
             │
    ┌────────▼─────────┐
    │  build_rk_image  │  → 创建GPT分区表
    │                  │  → dd合并所有分区
    │                  │  → 输出最终镜像
    └──────────────────┘
             │
             ▼
    OrangePi_4_ubuntu_bionic_desktop_linux4.4.179_v1.4.img
```

---

## 十二、RK3399启动流程

```
┌─────────────┐
│  Power On   │
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────────────────────────────┐
│  BootROM (芯片内置)                                      │
│  - 从SD/eMMC/SPI Flash读取idbloader                     │
│  - 加载地址: 0x0                                         │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│  idbloader.img                                          │
│  ┌─────────────────┐  ┌─────────────────┐              │
│  │ DDR Init        │  │ Miniloader      │              │
│  │ (rk3399_ddr.bin)│→ │ (SPL)           │              │
│  └─────────────────┘  └────────┬────────┘              │
└────────────────────────────────┼────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────┐
│  trust.img (ATF + OP-TEE)                               │
│  ┌─────────────────┐  ┌─────────────────┐              │
│  │ BL31 (ATF)      │  │ BL32 (OP-TEE)   │              │
│  │ @0x00010000     │  │ @0x08400000     │              │
│  └────────┬────────┘  └─────────────────┘              │
└───────────┼─────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────┐
│  uboot.img (U-Boot)                                     │
│  - 初始化外设                                            │
│  - 加载boot.img                                          │
│  - 启动内核                                              │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│  boot.img                                               │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐                 │
│  │ Kernel  │  │ DTB     │  │ Ramdisk │                 │
│  │ Image   │  │         │  │         │                 │
│  └────┬────┘  └─────────┘  └─────────┘                 │
└───────┼─────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────┐
│  Linux Kernel                                           │
│  - 挂载rootfs分区                                        │
│  - 启动init进程                                          │
│  - 进入用户空间                                          │
└─────────────────────────────────────────────────────────┘
```

---

## 十三、关键知识点总结

1. **RK3399使用GPT分区**，不是传统的MBR分区表

2. **多级启动架构**：
   - BootROM → DDR初始化 → Miniloader → ATF → OP-TEE → U-Boot → Kernel

3. **闭源固件**存放在`rkbin/`目录，必须使用Rockchip提供的binary：
   - DDR初始化代码
   - Miniloader (SPL)
   - ATF (BL31)
   - OP-TEE (BL32)

4. **boot.img**由kernel Makefile直接生成，集成了：
   - 内核Image
   - 设备树DTB
   - Ramdisk (可选)

5. **Mali GPU (T860)**需要闭源用户空间库配合开源内核驱动

6. **硬件编解码**通过MPP (Media Process Platform)库实现：
   - VPU硬件加速
   - GStreamer插件封装
   - 支持H.264/H.265/VP8/VP9

7. **串口调试**使用`ttyFIQ0`（Fast Interrupt Request UART）

---

## 十四、常用开发命令

### 14.1 单独编译U-Boot

```bash
cd OrangePiRK3399/uboot
./make.sh rk3399
# 输出: uboot.img, trust.img, idbloader.img
```

### 14.2 单独编译Kernel

```bash
cd OrangePiRK3399
sudo ./build.sh
# 选择 "3 Build Linux"
```

### 14.3 只更新Kernel到现有镜像

```bash
cd OrangePiRK3399
sudo ./build.sh
# 选择 "5 Update Kernel Image"
```

### 14.4 USB烧录模式

```bash
# 使用rkdeveloptool或upgrade_tool
rkdeveloptool db rk3399_loader_v1.22.119.bin  # 下载loader
rkdeveloptool wl 0 OrangePi_4_xxx.img         # 写入镜像
```

## 十四、常用开发命令

```
 Stage 1: SDK下载（本仓库）

  | 文件路径          | 作用                        |
  |-------------------|-----------------------------|
  | Build_OrangePi.sh | 入口脚本，调用general.sh    |
  | general.sh        | 定义仓库配置、下载5个子仓库 |

  Stage 2: 构建脚本（OrangePiRK3399/scripts/）

  | 文件路径                       | 作用                                        |
  |--------------------------------|---------------------------------------------|
  | scripts/build.sh               | 主构建脚本，平台选择、构建菜单              |
  | scripts/lib/compilation.sh     | U-Boot和Kernel编译逻辑                      |
  | scripts/lib/distributions.sh   | Rootfs构建、桌面安装、chroot配置            |
  | scripts/lib/build_image.sh     | 最终镜像打包（GPT分区、dd合并）             |
  | scripts/lib/platform/rk3399.sh | RK3399专属：GPU库、GStreamer、resize-helper |

  RK3399 U-Boot（OrangePiRK3399/uboot/）

  | 文件路径      | 作用                                          |
  |---------------|-----------------------------------------------|
  | uboot/make.sh | RK3399专用编译脚本，打包idbloader/uboot/trust |

  Rockchip固件（OrangePiRK3399/external/rkbin/）

  | 文件路径                       | 作用                               |
  |--------------------------------|------------------------------------|
  | rkbin/RKTRUST/RK3399TRUST.ini  | trust.img打包配置（BL31+BL32地址） |
  | rkbin/RKBOOT/RK3399MINIALL.ini | loader打包配置                     |
  | rkbin/bin/rk33/                | 闭源固件存放目录                   |

  快速打开命令

  # 本仓库脚本
  code general.sh

  # SDK构建脚本
  code OrangePiRK3399/scripts/build.sh
  code OrangePiRK3399/scripts/lib/compilation.sh
  code OrangePiRK3399/scripts/lib/distributions.sh
  code OrangePiRK3399/scripts/lib/build_image.sh
  code OrangePiRK3399/scripts/lib/platform/rk3399.sh

  # U-Boot打包
  code OrangePiRK3399/uboot/make.sh

  # 固件配置
  code OrangePiRK3399/external/rkbin/RKTRUST/RK3399TRUST.ini

  建议学习顺序：
  1. general.sh - 理解SDK下载机制
  2. scripts/build.sh - 理解构建入口和变量定义
  3. uboot/make.sh - 理解RK3399启动固件打包
  4. scripts/lib/build_image.sh - 理解GPT分区布局
  5. scripts/lib/distributions.sh - 理解rootfs构建
  6. scripts/lib/platform/rk3399.sh - 理解GPU/多媒体配置
```

## 参考资料

- [Rockchip Wiki](http://opensource.rock-chips.com/wiki_Main_Page)
- [U-Boot for Rockchip](https://github.com/rockchip-linux/u-boot)
- [Rockchip Kernel](https://github.com/rockchip-linux/kernel)
- [rkbin Repository](https://github.com/rockchip-linux/rkbin)
