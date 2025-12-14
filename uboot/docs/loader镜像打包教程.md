# Loader 镜像打包教程

## 命令简介

```bash
./make.sh loader        # 打包单个默认 loader
./make.sh loader-all    # 打包所有支持的 loader 变体
```

Loader 是 Rockchip 平台的**第一阶段引导程序**，负责初始化 DRAM、加载 U-Boot 等任务。

---

## Loader 架构理解

### Rockchip 启动流程

```
上电
  ↓
BootROM（芯片内置，不可修改）
  ↓
Loader（loader.bin）
  ├─ DDR Init（初始化内存）
  ├─ Miniloader（极简加载器）
  └─ 加载 U-Boot
      ↓
U-Boot（uboot.img）
  └─ 加载 Linux 内核
      ↓
Linux 系统启动
```

### Loader 组成部分

Loader 镜像由多个组件组成（定义在 INI 配置文件中）：

```
[LOADER_COMPONENTS]
├─ FlashData  : DDR 初始化代码（ddr.bin）
├─ FlashBoot  : Miniloader 代码（miniloader.bin）
└─ [可选] 其他初始化代码
```

---

## 打包流程详解

### 1. 查找配置文件（make.sh:656-665）

```bash
# 默认配置文件路径
ini=${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini

# 例如 RK3399:
# ${RKBIN}/RKBOOT/RK3399MINIALL.ini

# 用户可以指定自定义配置文件
if [ "$FILE" != "" ]; then
    ini=$FILE
fi

# 检查文件是否存在
if [ ! -f $ini ]; then
    echo "pack loader failed! Can't find: $ini"
    return
fi
```

**配置文件示例（RK3399MINIALL.ini）：**
```ini
[CHIP_NAME]
NAME=RK339A

[VERSION]
MAJOR=2
MINOR=50

[CODE471_OPTION]
NUM=1
Path1=bin/rk33/rk3399_ddr_800MHz_v1.25.bin

[CODE472_OPTION]
NUM=1
Path1=bin/rk33/rk3399_usbplug_v1.27.bin

[LOADER_OPTION]
NUM=2
LOADER1=FlashData
LOADER2=FlashBoot
FlashData=bin/rk33/rk3399_ddr_800MHz_v1.25.bin
FlashBoot=bin/rk33/rk3399_miniloader_v1.26.bin

[OUTPUT]
PATH=rk3399_loader_v1.25.126.bin
```

---

### 2. 清理旧文件（make.sh:667）

```bash
# 删除之前生成的所有 loader 文件
ls *_loader_*.bin >/dev/null 2>&1 && rm *_loader_*.bin
```

**为什么要清理？**
- 避免混淆新旧版本
- loader 文件名包含版本号，每次可能不同
- 确保使用最新生成的文件

---

### 3. 执行打包（make.sh:668-684）

#### 单个 Loader 打包

```bash
cd ${RKBIN}

# 使用 boot_merger 工具打包
${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} $ini

cd - && mv ${RKBIN}/*_loader_*.bin ./
```

**boot_merger 工具的作用：**
1. 读取 INI 配置文件
2. 组合 DDR 初始化代码和 Miniloader
3. 添加 Rockchip 镜像头
4. 生成最终的 loader.bin

#### 打包所有变体（loader-all）

```bash
if [ "${mode}" = 'all' ]; then
    # 查找所有匹配的 INI 文件
    files=`ls ${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL*.ini`

    # 遍历每个配置文件
    for ini in $files
    do
        if [ -f "$ini" ]; then
            ${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} $ini
            echo "pack loader okay! Input: $ini"
        fi
    done
fi
```

**什么时候有多个变体？**
- 不同 DDR 频率：800MHz、933MHz、1066MHz
- 不同 DDR 类型：DDR3、LPDDR3、LPDDR4
- 不同板子布局：不同的 DDR 颗粒配置

**示例（RK3399）：**
```
RK3399MINIALL.ini           ← 默认 800MHz DDR
RK3399MINIALL_933MHz.ini    ← 933MHz DDR
RK3399MINIALL_1066MHz.ini   ← 1066MHz DDR
```

---

### 4. 生成 idbloader.img（make.sh:686-693）

```bash
# 从 INI 文件提取路径
temp=`grep FlashData= ${ini} | cut -f 2 -d "="`
flashData=${temp/tools\/rk_tools\//}

temp=`grep FlashBoot= ${ini} | cut -f 2 -d "="`
flashBoot=${temp/tools\/rk_tools\//}

# 转换芯片名为小写
typeset -l localChip
localChip=$RKCHIP

# 生成 idbloader.img
${RKTOOLS}/mkimage -n ${localChip} -T rksd \
    -d ${RKBIN}/${flashData} idbloader.img
cat ${RKBIN}/${flashBoot} >> idbloader.img
```

**idbloader.img 是什么？**
- 用于 **SD 卡/eMMC 启动** 的镜像格式
- 包含 DDR init + Miniloader
- 可以直接 dd 写入存储设备

**与 *_loader_*.bin 的区别：**
| 文件类型 | 用途 | 格式 |
|---------|------|------|
| *_loader_*.bin | USB/Maskrom 烧录 | Rockchip 专用格式 |
| idbloader.img | SD/eMMC 启动 | 标准存储格式 |

---

### 5. 输出结果

成功后会生成：
```
rk3399_loader_v1.25.126.bin  ← USB 烧录用
idbloader.img                ← SD/eMMC 启动用
```

终端输出：
```
pack loader okay! Input: /path/to/RKBOOT/RK3399MINIALL.ini
```

---

## 完整使用流程

### 标准流程

```bash
# 1. 正常编译（会自动打包 loader）
./make.sh evb-rk3399

# 2. 仅重新打包 loader
./make.sh loader

# 3. 查看生成的文件
ls -lh *_loader_*.bin idbloader.img
```

### 自定义 DDR 配置

```bash
# 1. 复制并修改 INI 配置文件
cd ../rkbin
cp RKBOOT/RK3399MINIALL.ini RKBOOT/RK3399MINIALL_custom.ini

# 2. 编辑配置（修改 DDR 频率等）
vim RKBOOT/RK3399MINIALL_custom.ini

# 3. 使用自定义配置打包
cd ../uboot
FILE=../rkbin/RKBOOT/RK3399MINIALL_custom.ini ./make.sh loader
```

### 打包所有 DDR 变体

```bash
# 打包所有支持的 loader 配置
./make.sh loader-all

# 查看生成的文件（可能有多个）
ls -lh *_loader_*.bin
# 输出示例：
# rk3399_loader_v1.25.126.bin         ← 800MHz
# rk3399_loader_933MHz_v1.25.126.bin  ← 933MHz
# rk3399_loader_1066MHz_v1.25.126.bin ← 1066MHz
```

---

## INI 配置文件详解

### 基本结构

```ini
[CHIP_NAME]
NAME=RK339A              # 芯片识别码

[VERSION]
MAJOR=2                  # 主版本号
MINOR=50                 # 次版本号

[CODE471_OPTION]         # DRAM 初始化代码
NUM=1                    # 数量
Path1=bin/rk33/rk3399_ddr_800MHz_v1.25.bin

[CODE472_OPTION]         # USB 插件（用于 Maskrom 模式）
NUM=1
Path1=bin/rk33/rk3399_usbplug_v1.27.bin

[LOADER_OPTION]          # Loader 组成
NUM=2
LOADER1=FlashData        # DDR 初始化
LOADER2=FlashBoot        # Miniloader
FlashData=bin/rk33/rk3399_ddr_800MHz_v1.25.bin
FlashBoot=bin/rk33/rk3399_miniloader_v1.26.bin

[OUTPUT]
PATH=rk3399_loader_v1.25.126.bin  # 输出文件名
```

### 关键组件说明

#### FlashData（DDR 初始化）
```ini
FlashData=bin/rk33/rk3399_ddr_800MHz_v1.25.bin
```
- 初始化 DDR 内存控制器
- 设置 DDR 频率、时序参数
- 不同 DDR 颗粒需要不同的配置

**常见 DDR 频率：**
- 800MHz - 最稳定，兼容性好
- 933MHz - 性能提升约 15%
- 1066MHz - 最高性能，稳定性要求高

#### FlashBoot（Miniloader）
```ini
FlashBoot=bin/rk33/rk3399_miniloader_v1.26.bin
```
- 极简的引导加载器
- 负责从存储设备读取 U-Boot
- 支持 eMMC、SD、SPI 等存储介质

---

## 常见问题

### Q1: 报错 "Can't find: RKBOOT/XXX.ini"

**原因：** rkbin 仓库不存在或路径不对

**解决方法：**
```bash
# 1. 检查 rkbin 目录
ls -la ../rkbin/

# 2. 如果不存在，克隆 rkbin 仓库
cd ..
git clone https://github.com/rockchip-linux/rkbin.git

# 3. 重新打包
cd uboot
./make.sh loader
```

### Q2: 生成的 loader 无法启动设备

**可能原因和解决方法：**

1. **DDR 配置不匹配**
   ```bash
   # 检查板子使用的 DDR 类型和频率
   # 在 INI 中选择对应的 ddr.bin 文件

   # 例如：板子使用 LPDDR4-933MHz
   # 需要使用 rk3399_ddr_933MHz_*.bin
   ```

2. **rkbin 版本过旧**
   ```bash
   cd ../rkbin
   git pull origin master
   cd ../uboot
   ./make.sh loader
   ```

3. **miniloader 版本不兼容**
   ```bash
   # 查看 rkbin 更新日志
   cd ../rkbin
   git log --oneline RKBOOT/

   # 尝试使用不同版本的 miniloader
   ```

### Q3: USB 烧录时无法识别设备

**排查步骤：**
```bash
# 1. 检查设备是否进入 Maskrom 模式
lsusb | grep Rockchip
# 应该看到类似：Bus 001 Device 005: ID 2207:330c Rockchip

# 2. 确认 loader 文件完整性
ls -lh *_loader_*.bin
# 文件大小应该在 200KB ~ 1MB 之间

# 3. 使用正确的工具烧录
sudo upgrade_tool ul *_loader_*.bin
```

### Q4: SD 卡启动失败

**检查 idbloader.img 是否正确写入：**
```bash
# 1. 重新生成 idbloader.img
./make.sh loader

# 2. 写入到正确位置（扇区 64）
sudo dd if=idbloader.img of=/dev/sdX seek=64

# 3. 验证写入
sudo dd if=/dev/sdX skip=64 bs=512 count=1024 | hexdump -C | head
# 应该看到 Rockchip 的魔数

# 4. 同步并安全弹出
sync
sudo eject /dev/sdX
```

---

## 镜像烧录

### USB 烧录（Maskrom 模式）

```bash
# 进入 Maskrom 模式：
# 方法1: 按住 Recovery 按钮，插入 USB，松开按钮
# 方法2: 短接 eMMC CLK 到地，插入 USB

# 上传 loader 到 DRAM 并启动
sudo upgrade_tool ul *_loader_*.bin

# 或者写入到存储设备
sudo upgrade_tool di -p loader *_loader_*.bin
```

### SD 卡烧录

```bash
# 方法1: 单独写入 idbloader.img
sudo dd if=idbloader.img of=/dev/sdX seek=64 conv=notrunc

# 方法2: 写入完整固件（包含分区表）
sudo dd if=complete_image.img of=/dev/sdX bs=4M
sync
```

### eMMC 烧录

```bash
# 先上传 loader 到 DRAM
sudo upgrade_tool ul *_loader_*.bin

# 写入 idbloader 到 eMMC
sudo upgrade_tool di -p loader *_loader_*.bin

# 或者使用 rkdeveloptool
sudo rkdeveloptool db *_loader_*.bin
sudo rkdeveloptool wl 64 idbloader.img
```

---

## 进阶：自定义 DDR 配置

### 修改 DDR 频率

```bash
# 1. 解包 ddr.bin（需要专用工具）
# Rockchip 通常不公开 DDR 配置工具

# 2. 使用现有的不同频率版本
cd ../rkbin/bin/rk33/
ls -lh rk3399_ddr_*
# rk3399_ddr_800MHz_v1.25.bin
# rk3399_ddr_933MHz_v1.23.bin
# rk3399_ddr_1066MHz_v1.20.bin

# 3. 在 INI 中指定使用的版本
vim ../../RKBOOT/RK3399MINIALL_custom.ini
```

### 调试 DDR 初始化

```bash
# 1. 在 miniloader 中启用 DDR 调试信息
# （需要 Rockchip 内部工具）

# 2. 通过串口查看启动日志
# 连接串口线，波特率 1500000
picocom -b 1500000 /dev/ttyUSB0

# 3. 查看 DDR 训练结果
# 日志示例：
# DDRVersion V1.25
# In
# Channel 0: DDR3 800MHz
# ...
```

---

## 总结

`./make.sh loader` 命令的核心作用：

1. ✅ **组合 DDR 初始化代码** - 让 DRAM 正常工作
2. ✅ **打包 Miniloader** - 引导加载 U-Boot
3. ✅ **生成 USB 烧录镜像** - *_loader_*.bin
4. ✅ **生成 SD/eMMC 启动镜像** - idbloader.img

**关键点：** Loader 是启动的第一步，配置错误会导致设备完全无法启动！

**常见启动失败原因排序：**
1. DDR 配置不匹配（60%）
2. rkbin 版本过旧（20%）
3. 烧录位置错误（15%）
4. 硬件故障（5%）
