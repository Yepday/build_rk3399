# U-Boot 镜像打包教程

## 命令简介

```bash
./make.sh uboot
```

该命令用于将编译好的 `u-boot.bin` 文件打包成 Rockchip 平台可启动的 `uboot.img` 镜像文件。

---

## 打包流程详解

### 1. 前置条件检查

**必需文件：**
- `u-boot.bin` - 编译生成的 U-Boot 二进制文件
- `.config` - U-Boot 配置文件（包含加载地址等关键信息）
- `loaderimage` 工具 - Rockchip 专用的镜像打包工具

**关键配置项：**
- `CONFIG_SYS_TEXT_BASE` - U-Boot 加载到内存的基地址

---

### 2. 打包步骤分析

#### 步骤 1：检查文件大小（make.sh:532-545）

```bash
# 获取 u-boot.bin 文件大小（单位：字节）
UBOOT_KB=`ls -l u-boot.bin | awk '{print $5}'`

# 确定最大允许大小
if [ "$PLATFORM_UBOOT_IMG_SIZE" = "" ]; then
    UBOOT_MAX_KB=1046528    # 默认约 1022KB
else
    UBOOT_MAX_KB=计算值     # 平台特定大小
fi

# 大小检查
if [ $UBOOT_KB -gt $UBOOT_MAX_KB ]; then
    echo "ERROR: u-boot.bin 超过最大限制"
    exit 1
fi
```

**为什么要检查大小？**
- Rockchip SoC 对 U-Boot 镜像大小有限制
- 不同芯片的内存布局不同（如 RK3308 限制更严格）
- 超过限制会导致启动失败

**各平台大小限制：**
| 芯片型号 | 最大大小 | 说明 |
|---------|---------|------|
| 通用平台 | 1022KB | 默认限制 |
| RK3308 (AArch32) | 510KB | 内存更紧张 |
| RK3308 (AArch64) | 1022KB | 64位模式 |
| RK1808 | 1022KB | AI芯片 |

---

#### 步骤 2：获取加载地址（make.sh:548-551）

```bash
# 从 autoconf.mk 读取（优先）
UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" \
                  ${OUTDIR}/include/autoconf.mk | tr -d '\r'`

# 如果为空，从 .config 读取（备用）
if [ ! $UBOOT_LOAD_ADDR ]; then
    UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" \
                      ${OUTDIR}/.config | tr -d '\r'`
fi
```

**加载地址示例：**
- RK3399: `0x00200000` (2MB 偏移)
- RK3328: `0x00200000`
- RK3308: `0x00600000` (6MB 偏移)

**为什么需要加载地址？**
- BootROM 需要知道将 U-Boot 加载到内存的什么位置
- 地址必须与编译时的链接地址一致
- 错误的地址会导致代码无法执行

---

#### 步骤 3：执行打包（make.sh:553）

```bash
${RKTOOLS}/loaderimage --pack --uboot \
    ${OUTDIR}/u-boot.bin \      # 输入文件
    uboot.img \                 # 输出文件
    ${UBOOT_LOAD_ADDR} \        # 加载地址
    ${PLATFORM_UBOOT_IMG_SIZE}  # 平台特定大小参数
```

**loaderimage 工具的作用：**
1. 添加 Rockchip 专用的镜像头（Header）
2. 镜像头包含：
   - 魔数（Magic Number）：标识 Rockchip 镜像
   - 大小信息：镜像总大小
   - 加载地址：U-Boot 在内存中的位置
   - CRC 校验：数据完整性校验
3. 可能进行对齐和填充处理

**镜像头结构示意：**
```
+------------------+
| Magic (4 bytes)  |  识别标识
+------------------+
| Size (4 bytes)   |  镜像大小
+------------------+
| Load Addr (4B)   |  加载地址
+------------------+
| CRC (4 bytes)    |  校验和
+------------------+
| ... Header ...   |  其他头信息
+------------------+
| U-Boot Binary    |  实际代码
+------------------+
```

---

#### 步骤 4：清理冗余文件（make.sh:556-562）

```bash
# 删除 u-boot.img（Kbuild 生成的，不带 Rockchip 头）
if [ -f ${OUTDIR}/u-boot.img ]; then
    rm ${OUTDIR}/u-boot.img
fi

# 删除 u-boot-dtb.img（同样不带 Rockchip 头）
if [ -f ${OUTDIR}/u-boot-dtb.img ]; then
    rm ${OUTDIR}/u-boot-dtb.img
fi
```

**为什么要删除？**
- 这些是 U-Boot 标准构建过程生成的镜像
- **不包含** Rockchip 专用的头信息
- 直接烧录会导致 BootROM 无法识别
- 避免用户混淆，使用错误的镜像文件

---

### 3. 输出结果

成功后会在当前目录生成：
```
uboot.img  ← 最终可用的 U-Boot 镜像
```

终端输出：
```
pack uboot okay! Input: ./u-boot.bin
```

---

## 完整使用流程

### 标准流程

```bash
# 1. 配置并编译
./make.sh evb-rk3399

# 2. 仅重新打包 uboot（编译已完成，只需打包）
./make.sh uboot
```

### 调试流程

```bash
# 1. 编译
make CROSS_COMPILE=aarch64-linux-gnu- all -j8

# 2. 检查生成的文件
ls -lh u-boot.bin
# 输出: -rwxr-xr-x 1 user user 856K Dec 14 10:30 u-boot.bin

# 3. 查看加载地址
grep CONFIG_SYS_TEXT_BASE .config
# 输出: CONFIG_SYS_TEXT_BASE=0x00200000

# 4. 打包
./make.sh uboot

# 5. 验证镜像
ls -lh uboot.img
hexdump -C uboot.img | head -n 5  # 查看镜像头
```

---

## 常见问题

### Q1: 报错 "u-boot.bin actual: XXX bytes, max limit: XXX bytes"

**原因：** U-Boot 编译后文件过大

**解决方法：**
```bash
# 1. 检查当前大小
ls -lh u-boot.bin

# 2. 在 menuconfig 中禁用不需要的功能
make menuconfig
# 禁用选项示例：
# - CONFIG_CMD_xxx (不需要的命令)
# - CONFIG_USB_xxx (如果不用 USB)
# - CONFIG_VIDEO_xxx (如果不需要显示)

# 3. 重新编译
make CROSS_COMPILE=aarch64-linux-gnu- all -j8
./make.sh uboot
```

### Q2: 报错 "Can't find elf file"

**原因：** 没有找到编译输出目录

**解决方法：**
```bash
# 检查是否有 .config 文件
find . -name .config

# 如果有多个，指定输出目录
./make.sh uboot O=output_dir
```

### Q3: 生成的 uboot.img 无法启动

**可能原因：**
1. **加载地址错误**
   ```bash
   # 检查 .config 中的地址
   grep CONFIG_SYS_TEXT_BASE .config

   # 对比芯片手册中的内存布局
   ```

2. **镜像损坏**
   ```bash
   # 重新打包
   rm uboot.img
   ./make.sh uboot
   ```

3. **工具链不匹配**
   ```bash
   # 检查工具链版本
   aarch64-linux-gnu-gcc --version

   # 使用正确的工具链重新编译
   ```

---

## 镜像烧录

### 使用 upgrade_tool (Windows/Linux)

```bash
# 进入 Maskrom 或 Loader 模式
sudo upgrade_tool di -u uboot.img

# 或者烧录到指定分区
sudo upgrade_tool di -p uboot uboot.img
```

### 使用 rkdeveloptool (Linux)

```bash
# 写入 uboot 分区
sudo rkdeveloptool wl 0x4000 uboot.img

# 或者通过分区名
sudo rkdeveloptool write-partition uboot uboot.img
```

### 使用 SD 卡烧录

```bash
# 写入到 SD 卡的正确位置（偏移 0x4000 扇区）
sudo dd if=uboot.img of=/dev/sdX seek=16384

# 同步并弹出
sync
sudo eject /dev/sdX
```

---

## 进阶：镜像分析

### 查看镜像头信息

```bash
# 查看前 64 字节的十六进制
hexdump -C uboot.img | head -n 4

# 示例输出：
# 00000000  52 4b 33 39 ...  |RK39...|  ← 魔数
# 00000010  00 0d 10 00 ...  |....|    ← 大小信息
```

### 提取原始 u-boot.bin

```bash
# 跳过镜像头（通常是 2KB）
dd if=uboot.img of=extracted.bin bs=2048 skip=1
```

---

## 总结

`./make.sh uboot` 命令的核心作用是：

1. ✅ **添加 Rockchip 专用头** - 让 BootROM 识别
2. ✅ **验证大小限制** - 防止镜像过大
3. ✅ **设置加载地址** - 确保正确加载到内存
4. ✅ **生成可烧录镜像** - 直接用于设备启动

**关键点：** 必须使用此命令打包，不能直接使用 `u-boot.bin`！
