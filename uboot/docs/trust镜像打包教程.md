# Trust 镜像打包教程

## 命令简介

```bash
./make.sh trust        # 打包单个默认 trust
./make.sh trust-all    # 打包所有支持的 trust 变体
```

Trust 镜像包含 **ARM TrustZone** 的安全固件，运行在**安全世界（Secure World）**，负责安全启动、密钥管理、加密等任务。

---

## TrustZone 架构理解

### ARM TrustZone 双世界模型

```
┌─────────────────────────────────────┐
│         Normal World (非安全)        │
│                                     │
│  Linux Kernel                       │
│  U-Boot                             │
│  用户应用                            │
│                                     │
└──────────────┬──────────────────────┘
               │ SMC 调用
               ↓
┌─────────────────────────────────────┐
│         Secure World (安全)          │
│                                     │
│  ATF (ARM Trusted Firmware)         │
│  OP-TEE (Trusted OS)                │
│  密钥存储、加密引擎                   │
│                                     │
└─────────────────────────────────────┘
```

### Trust 镜像的作用

1. **ATF (ARM Trusted Firmware)** - BL31
   - 处理器电源管理（PSCI）
   - 安全监控模式（Secure Monitor）
   - 处理 SMC（Secure Monitor Call）

2. **OP-TEE (Trusted Execution Environment)** - BL32
   - 安全操作系统
   - 密钥管理
   - 安全存储
   - 加密操作

3. **其他安全组件**
   - BL30: PMU 固件（电源管理单元）
   - BL32_EXT: 扩展的 TEE 应用

---

## 打包流程详解

### 架构判断（make.sh:772）

Trust 打包流程会根据 CPU 架构选择不同的方法：

```bash
# 检查是否为 ARM64 架构
if grep -Eq ''^CONFIG_ARM64=y'|'^CONFIG_ARM64_BOOT_AARCH32=y'' ${OUTDIR}/.config ; then
    # ARM64 使用 trust_merger 工具
    __pack_64bit_trust_image ${ini}
else
    # ARM32 使用 loaderimage 工具
    __pack_32bit_trust_image ${ini}
fi
```

---

## ARM64 Trust 打包（64位）

### 1. 查找配置文件（make.sh:773-778）

```bash
# 默认配置文件
ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TRUST.ini

# 例如 RK3399:
# ${RKBIN}/RKTRUST/RK3399TRUST.ini

# 用户可指定自定义配置
if [ "$FILE" != "" ]; then
    ini=$FILE
fi
```

**配置文件示例（RK3399TRUST.ini）：**
```ini
[VERSION]
MAJOR=1
MINOR=0

[BL30_OPTION]
SEC=0
PATH=bin/rk33/rk3399_bl30_v1.00.bin
ADDR=0x00040000

[BL31_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl31_v1.35.elf
ADDR=0x00010000

[BL32_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl32_v2.01.bin
ADDR=0x08400000

[BL33_OPTION]
SEC=0

[OUTPUT]
PATH=trust.img
```

---

### 2. 打包处理（make.sh:780-793）

#### 单个 Trust 打包

```bash
# 进入 rkbin 目录
cd ${RKBIN}

# 使用 trust_merger 工具打包
${RKTOOLS}/trust_merger \
    ${PLATFORM_SHA} \              # SHA 算法选项
    ${PLATFORM_RSA} \              # RSA 签名选项
    ${PLATFORM_TRUST_IMG_SIZE} \   # 镜像大小限制
    ${BIN_PATH_FIXUP} \            # 路径修正参数
    ${PACK_IGNORE_BL32} \          # 可选：忽略 BL32
    ${ini}                          # 配置文件

# 移动生成的镜像
cd - && mv ${RKBIN}/trust*.img ./
```

#### 打包所有变体（trust-all）

```bash
if [ "${mode}" = 'all' ]; then
    # 查找所有 TRUST 配置文件
    files=`ls ${RKBIN}/RKTRUST/${RKCHIP_TRUST}TRUST*.ini`

    # 遍历每个配置
    for ini in $files
    do
        __pack_64bit_trust_image ${ini}
    done
fi
```

**什么时候有多个变体？**
- 不同安全等级：标准、增强安全
- 不同 TEE OS：OP-TEE、Trusty
- 特殊功能版本：支持加密启动、安全播放等

---

### 3. 平台特定配置（make.sh:406-443）

某些平台需要特殊的打包参数：

```bash
# RK3308/PX30/RK3326/RK1808 使用 RSA-PKCS1 V2.1
if [ $RKCHIP = "PX30" -o $RKCHIP = "RK3326" -o \
     $RKCHIP = "RK3308" -o $RKCHIP = "RK1808" ]; then
    PLATFORM_RSA="--rsa 3"
fi

# RK3368 使用 Rockchip 大端序 SHA256
if [ $RKCHIP = "RK3368" ]; then
    PLATFORM_SHA="--sha 2"
fi

# 镜像大小限制（RK3308 有特殊限制）
if [ $RKCHIP = "RK3308" ]; then
    if grep -q '^CONFIG_ARM64_BOOT_AARCH32=y' ${OUTDIR}/.config ; then
        PLATFORM_TRUST_IMG_SIZE="--size 512 2"   # AArch32: 510KB
    else
        PLATFORM_TRUST_IMG_SIZE="--size 1024 2"  # AArch64: 1022KB
    fi
fi
```

**加密算法说明：**
| 平台 | RSA 类型 | SHA 类型 | 原因 |
|------|---------|---------|------|
| RK3399 | PKCS1 V1.5 (默认) | SHA256 | 标准配置 |
| RK3308 | PKCS1 V2.1 | SHA256 | 增强安全 |
| RK3368 | PKCS1 V1.5 | 大端序 SHA256 | 兼容性 |

---

### 4. 输出结果（ARM64）

成功后生成：
```
trust.img  ← 包含 ATF + OP-TEE 的安全固件
```

终端输出：
```
pack trust okay! Input: /path/to/RKTRUST/RK3399TRUST.ini
```

---

## ARM32 Trust 打包（32位）

### 1. 查找配置文件（make.sh:797-802）

```bash
# ARM32 使用 TOS（Trusted OS）配置
ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TOS.ini

# 例如 RK3288:
# ${RKBIN}/RKTRUST/RK3288TOS.ini

if [ "$FILE" != "" ]; then
    ini=$FILE
fi
```

**配置文件示例（RK3288TOS.ini）：**
```ini
[TOS_BIN_PATH]
TOSTA=bin/rk32/rk3288_tee_ta_v1.26.bin

[OUTPUT]
OUTPUT=./trust.img
ADDR=0x68400000
```

---

### 2. 解析配置（make.sh:707-719）

```bash
# 解析 TOS 二进制路径
TOS=`sed -n "/TOS=/s/TOS=//p" ${ini} | tr -d '\r'`
TOS_TA=`sed -n "/TOSTA=/s/TOSTA=//p" ${ini} | tr -d '\r'`

# 解析输出文件名
TEE_OUTPUT=`sed -n "/OUTPUT=/s/OUTPUT=//p" ${ini} | tr -d '\r'`
if [ "$TEE_OUTPUT" = "" ]; then
    TEE_OUTPUT="./trust.img"  # 默认名称
fi

# 解析加载地址偏移
TEE_OFFSET=`sed -n "/ADDR=/s/ADDR=//p" ${ini} | tr -d '\r'`
if [ "$TEE_OFFSET" = "" ]; then
    TEE_OFFSET=0x8400000  # 默认 132MB 偏移
fi
```

---

### 3. 计算加载地址（make.sh:721-726）

```bash
# 获取 DRAM 基地址
DARM_BASE=`sed -n "/CONFIG_SYS_SDRAM_BASE=/s/CONFIG_SYS_SDRAM_BASE=//p" \
           ${OUTDIR}/include/autoconf.mk | tr -d '\r'`

# 计算 TEE 加载地址 = DRAM 基地址 + 偏移
TEE_LOAD_ADDR=$((DARM_BASE + TEE_OFFSET))

# 转换为十六进制
TEE_LOAD_ADDR=$(echo "obase=16;${TEE_LOAD_ADDR}" | bc)
```

**为什么是 132MB (0x8400000) 偏移？**
- OP-TEE 需要运行在高地址
- 避免与 Linux 内核冲突
- 预留足够的安全内存区域

**示例计算：**
```
DRAM_BASE = 0x60000000 (RK3288)
TEE_OFFSET = 0x08400000 (132MB)
TEE_LOAD_ADDR = 0x60000000 + 0x08400000 = 0x68400000
```

---

### 4. 执行打包（make.sh:732-739）

```bash
# 兼容旧路径格式
TOS=$(echo ${TOS} | sed "s/tools\/rk_tools\//\.\//g")
TOS_TA=$(echo ${TOS_TA} | sed "s/tools\/rk_tools\//\.\//g")

# 打包（优先使用 TOSTA）
if [ $TOS_TA ]; then
    ${RKTOOLS}/loaderimage --pack --trustos \
        ${RKBIN}/${TOS_TA} \           # 输入文件
        ${TEE_OUTPUT} \                # 输出文件
        ${TEE_LOAD_ADDR} \             # 加载地址
        ${PLATFORM_TRUST_IMG_SIZE}     # 大小限制
elif [ $TOS ]; then
    ${RKTOOLS}/loaderimage --pack --trustos \
        ${RKBIN}/${TOS} \
        ${TEE_OUTPUT} \
        ${TEE_LOAD_ADDR} \
        ${PLATFORM_TRUST_IMG_SIZE}
else
    echo "Can't find any tee bin"
    exit 1
fi
```

---

### 5. 输出结果（ARM32）

成功后生成：
```
trust.img  ← 包含 OP-TEE 的安全固件
```

---

## 完整使用流程

### 标准流程

```bash
# 1. 正常编译（会自动打包 trust）
./make.sh evb-rk3399

# 2. 仅重新打包 trust
./make.sh trust

# 3. 查看生成的文件
ls -lh trust.img
```

### 打包所有 Trust 变体

```bash
# 打包所有支持的 trust 配置
./make.sh trust-all

# 查看生成的文件
ls -lh trust*.img
# 可能输出：
# trust.img           ← 标准版本
# trust_optee.img     ← OP-TEE 版本
# trust_trusty.img    ← Trusty 版本
```

### 不包含 BL32（仅 ATF）

```bash
# 设置环境变量，忽略 BL32
export TRUST_PACK_IGNORE_BL32="--ignore-bl32"

# 打包（仅包含 ATF，不包含 TEE）
./make.sh trust

# 清除环境变量
unset TRUST_PACK_IGNORE_BL32
```

**什么时候不需要 BL32？**
- 不使用安全功能
- 减小镜像大小
- 调试 ATF 时隔离问题

---

## INI 配置文件详解

### ARM64 配置（TRUST.ini）

```ini
[VERSION]
MAJOR=1
MINOR=0

# BL30: PMU 固件（电源管理）
[BL30_OPTION]
SEC=0                                    # 0=非安全, 1=安全
PATH=bin/rk33/rk3399_bl30_v1.00.bin     # 二进制路径
ADDR=0x00040000                          # 加载地址

# BL31: ARM Trusted Firmware
[BL31_OPTION]
SEC=1                                    # 必须是安全模式
PATH=bin/rk33/rk3399_bl31_v1.35.elf     # ELF 格式
ADDR=0x00010000                          # ATF 加载地址

# BL32: OP-TEE (可选)
[BL32_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl32_v2.01.bin
ADDR=0x08400000                          # TEE 运行地址

# BL33: 由 U-Boot 提供（不在 trust 中）
[BL33_OPTION]
SEC=0

[OUTPUT]
PATH=trust.img
```

**关键组件说明：**

#### BL30（电源管理单元固件）
- 控制 CPU 电源状态
- 处理休眠/唤醒
- 动态调频调压（DVFS）
- 运行在专用的 MCU 上

#### BL31（ARM Trusted Firmware）
- **PSCI**：电源状态协调接口
  - CPU_ON：启动 CPU 核心
  - CPU_OFF：关闭 CPU 核心
  - CPU_SUSPEND：挂起 CPU
- **SMC Handler**：处理来自 Normal World 的安全调用
- **GIC 配置**：中断控制器初始化

#### BL32（OP-TEE）
- 安全存储 API
- 加密操作（AES、RSA、SHA）
- 密钥生成和管理
- 安全时间服务

---

### ARM32 配置（TOS.ini）

```ini
[TOS_BIN_PATH]
TOSTA=bin/rk32/rk3288_tee_ta_v1.26.bin  # OP-TEE 二进制

[OUTPUT]
OUTPUT=./trust.img                       # 输出文件名
ADDR=0x68400000                          # 绝对加载地址
```

**ARM32 vs ARM64 的区别：**
| 特性 | ARM64 | ARM32 |
|------|-------|-------|
| ATF (BL31) | 必需 | 不适用 |
| OP-TEE (BL32) | 可选 | 必需 |
| 地址指定 | 相对偏移 | 绝对地址 |
| 打包工具 | trust_merger | loaderimage |

---

## 常见问题

### Q1: 报错 "Can't find any tee bin"

**原因：** INI 配置中没有指定 TEE 二进制文件

**解决方法：**
```bash
# 1. 检查 INI 配置
cat ../rkbin/RKTRUST/${RKCHIP}TRUST.ini

# 2. 确认 TEE 二进制存在
ls ../rkbin/bin/rk33/*bl32*.bin

# 3. 如果文件缺失，更新 rkbin
cd ../rkbin
git pull origin master
cd ../uboot
./make.sh trust
```

### Q2: 设备启动后进入安全异常

**可能原因：**
1. **Trust 镜像损坏**
   ```bash
   # 重新生成
   rm trust.img
   ./make.sh trust

   # 验证文件大小（应该在 100KB ~ 2MB）
   ls -lh trust.img
   ```

2. **ATF 版本不匹配**
   ```bash
   # 检查 rkbin 版本
   cd ../rkbin
   git log --oneline bin/rk33/*bl31* | head

   # 尝试使用不同版本
   vim RKTRUST/RK3399TRUST.ini
   # 修改 BL31_OPTION 的 PATH
   ```

3. **加载地址错误**
   ```bash
   # 检查 INI 中的地址
   grep ADDR ../rkbin/RKTRUST/${RKCHIP}TRUST.ini

   # 对比芯片手册的内存映射
   ```

### Q3: Linux 启动时报 "PSCI not supported"

**原因：** ATF (BL31) 未正确加载

**解决方法：**
```bash
# 1. 确认 trust.img 已烧录
sudo upgrade_tool di -p trust trust.img

# 2. 检查串口日志，查找 ATF 启动信息
# 应该看到类似：
# NOTICE:  BL31: v2.5(release):v2.5
# NOTICE:  BL31: Built : 10:30:00, Jan 15 2021

# 3. 如果没有 ATF 日志，检查 U-Boot 是否正确跳转
# 在 U-Boot 中：
=> fdt addr $fdtcontroladdr
=> fdt list /firmware/optee
=> fdt list /psci
```

### Q4: OP-TEE 启动失败

**排查步骤：**
```bash
# 1. 检查 BL32 是否存在
hexdump -C trust.img | grep -i "optee"

# 2. 验证加载地址
# ARM64: 应该在 0x08400000 附近
# ARM32: 应该在 DRAM_BASE + 0x08400000

# 3. 查看 OP-TEE 启动日志（串口输出）
# 应该看到：
# D/TC:0 0 init_primary_helper:1366 Initializing ...
# I/TC: OP-TEE version: 3.x.x

# 4. 如果没有日志，尝试禁用 BL32 测试
export TRUST_PACK_IGNORE_BL32="--ignore-bl32"
./make.sh trust
```

---

## 镜像烧录

### USB 烧录

```bash
# 进入 Loader 模式
sudo upgrade_tool ul *_loader_*.bin

# 写入 trust 镜像
sudo upgrade_tool di -p trust trust.img

# 或者使用 rkdeveloptool
sudo rkdeveloptool db *_loader_*.bin
sudo rkdeveloptool wl trust trust.img
```

### 更新完整固件

```bash
# 方法1: 使用 upgrade_tool
sudo upgrade_tool uf update.img

# 方法2: 单独更新 trust
sudo upgrade_tool di -u trust.img
```

---

## 进阶：调试 Trust

### 启用 ATF 调试日志

```bash
# 1. 重新编译 ATF（需要 ATF 源码）
cd arm-trusted-firmware
make PLAT=rk3399 DEBUG=1 bl31

# 2. 替换 rkbin 中的 bl31.elf
cp build/rk3399/debug/bl31/bl31.elf ../rkbin/bin/rk33/

# 3. 重新打包
cd ../uboot
./make.sh trust
```

### 查看 OP-TEE 日志

```bash
# 串口连接，波特率 1500000
picocom -b 1500000 /dev/ttyUSB0

# 启动时查看 TEE 日志
# OP-TEE 的日志会在启动早期输出
```

### 测试 PSCI 功能

```bash
# 在 Linux 中测试 CPU 热插拔
echo 0 > /sys/devices/system/cpu/cpu1/online
echo 1 > /sys/devices/system/cpu/cpu1/online

# 查看 PSCI 版本
cat /sys/firmware/psci_version
# 应该输出: 1.1 或 0.2
```

---

## 总结

`./make.sh trust` 命令的核心作用：

1. ✅ **打包 ARM Trusted Firmware (BL31)** - 安全监控和电源管理
2. ✅ **打包 OP-TEE (BL32)** - 安全操作系统
3. ✅ **配置加密和签名** - 安全启动支持
4. ✅ **生成 trust.img** - 运行在 Secure World 的固件

**关键点：** Trust 是安全功能的基础，缺少或配置错误会导致：
- PSCI 不可用（无法多核启动）
- 安全存储失败
- DRM 播放失败
- 某些外设无法工作

**调试建议：**
1. 先确保基本启动（可以尝试 `--ignore-bl32`）
2. 再逐步启用安全功能
3. 通过串口日志定位问题
4. 参考 Rockchip 官方文档和示例配置
