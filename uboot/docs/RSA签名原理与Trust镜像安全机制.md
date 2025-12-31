# RSA 签名原理与 Trust 镜像安全机制教学文档

## 文档概述

本文档从密码学原理和工程实践两个维度，深入讲解 Rockchip Trust 镜像中的 RSA 签名机制：
- **理论部分**：RSA 非对称加密算法原理、数字签名流程、PSS vs PKCS#1 v1.5 差异
- **实践部分**：`PLATFORM_RSA` 在 U-Boot 构建系统中的使用、安全启动验证流程
- **安全分析**：如何防止固件篡改、降级攻击、中间人攻击

---

## 目录
1. [RSA 密码学基础](#1-rsa-密码学基础)
2. [数字签名原理](#2-数字签名原理)
3. [PKCS#1 v1.5 vs PSS 签名模式](#3-pkcs1-v15-vs-pss-签名模式)
4. [Trust 镜像签名架构](#4-trust-镜像签名架构)
5. [PLATFORM_RSA 实现详解](#5-platform_rsa-实现详解)
6. [安全启动验证流程](#6-安全启动验证流程)
7. [安全性分析与攻击防御](#7-安全性分析与攻击防御)
8. [实战案例](#8-实战案例)

---

## 1. RSA 密码学基础

### 1.1 RSA 算法核心原理

RSA (Rivest–Shamir–Adleman) 是一种非对称加密算法，安全性基于**大整数因数分解难题**。

#### **数学基础**

1. **密钥生成**
   ```
   Step 1: 选择两个大素数 p 和 q（通常各为 1024 位）
   Step 2: 计算模数 n = p × q（2048 位）
   Step 3: 计算欧拉函数 φ(n) = (p-1) × (q-1)
   Step 4: 选择公钥指数 e（通常为 65537 = 0x10001）
          满足 1 < e < φ(n) 且 gcd(e, φ(n)) = 1
   Step 5: 计算私钥指数 d，满足 d × e ≡ 1 (mod φ(n))

   公钥：(n, e)
   私钥：(n, d)  [实际还包含 p, q 用于 CRT 加速]
   ```

2. **加密与解密**
   ```
   加密（使用公钥）：C = M^e mod n
   解密（使用私钥）：M = C^d mod n

   数学证明：M^(e×d) ≡ M^(1 + k×φ(n)) ≡ M (mod n)  [欧拉定理]
   ```

#### **为什么安全？**

```
已知：公钥 (n, e)
攻击目标：计算私钥 d

困难：需要分解 n = p × q
      ↓
   2048 位 RSA 分解需要数百万年（经典计算机）
      ↓
   即使知道 n 和 e，也无法高效计算 d
```

**实际强度对比**：
| RSA 密钥长度 | 对称加密等效强度 | 估计破解时间（经典计算机）|
|-------------|----------------|------------------------|
| 1024 位     | ~80 位         | ~1 年（已不安全）       |
| 2048 位     | ~112 位        | ~数百万年              |
| 3072 位     | ~128 位        | ~数十亿年              |
| 4096 位     | ~140 位        | ~数万亿年              |

**RK3399 使用 2048 位 RSA**，满足当前安全标准（NIST 建议至少 2048 位）。

---

### 1.2 为什么用 RSA 做签名而非加密？

在嵌入式固件场景中，**RSA 主要用于数字签名**，而非数据加密：

| 需求 | 加密 | 签名 |
|------|------|------|
| 保密性 | ✓ | ✗ |
| 完整性 | ✗ | ✓ |
| 身份认证 | ✗ | ✓ |
| 不可否认 | ✗ | ✓ |

**固件安全的核心需求**：
- ✓ **完整性验证**：确保固件未被篡改
- ✓ **身份认证**：确认固件来自可信厂商
- ✗ **保密性**：固件本身通常不需要加密（开源或可逆向）

---

## 2. 数字签名原理

### 2.1 签名与验证流程

```
┌─────────────── 签名端（开发者/厂商）─────────────────┐
│                                                      │
│  1. 原始固件 (trust.bin)                             │
│         ↓                                            │
│  2. 计算哈希值                                        │
│     Hash = SHA256(trust.bin)  [32 字节]             │
│         ↓                                            │
│  3. 使用私钥签名                                      │
│     Signature = Hash^d mod n  [256 字节, RSA-2048]  │
│         ↓                                            │
│  4. 附加签名到固件                                    │
│     trust.img = trust.bin || Signature              │
│                                                      │
└──────────────────────────────────────────────────────┘
                          ↓ [发布]
┌─────────────── 验证端（BootROM）─────────────────────┐
│                                                      │
│  1. 接收 trust.img                                   │
│         ↓                                            │
│  2. 分离签名                                          │
│     trust.bin, Signature = split(trust.img)         │
│         ↓                                            │
│  3. 使用公钥解密签名                                  │
│     Hash' = Signature^e mod n                       │
│         ↓                                            │
│  4. 重新计算哈希                                      │
│     Hash = SHA256(trust.bin)                        │
│         ↓                                            │
│  5. 比较哈希值                                        │
│     if (Hash == Hash')                              │
│         验证成功 → 继续启动                           │
│     else                                            │
│         验证失败 → 拒绝启动                           │
│                                                      │
└──────────────────────────────────────────────────────┘
```

### 2.2 为什么要先哈希再签名？

**直接对原始数据签名的问题**：
```
原始固件大小：256 KB
RSA-2048 一次只能处理：256 字节（2048 位）

直接签名需要：256 KB ÷ 256 B = 1024 次 RSA 运算
            → 签名时间：~10 秒
            → 验证时间：~3 秒（BootROM 不可接受）
```

**哈希后签名的优势**：
```
SHA-256 哈希：256 KB → 32 字节（固定长度）
RSA 签名：1 次运算即可
        → 签名时间：~10 毫秒
        → 验证时间：~3 毫秒（BootROM 可接受）
```

**安全性保证**：
- SHA-256 是**抗碰撞哈希**：找到两个不同文件产生相同哈希值的概率 < 2^-128
- 即使攻击者修改固件，哈希值会改变，签名验证失败

---

## 3. PKCS#1 v1.5 vs PSS 签名模式

### 3.1 为什么需要填充（Padding）？

**原始 RSA 签名的问题**：
```
直接签名：Signature = Hash^d mod n

问题 1：确定性（Deterministic）
       同一消息总是产生相同签名 → 易受重放攻击

问题 2：可锻造性（Malleability）
       攻击者可以构造特殊消息使签名可预测

问题 3：短消息漏洞
       Hash(32 字节) << n(256 字节) → 利用数学结构攻击
```

**解决方案**：在哈希值上添加填充（Padding），然后再签名。

---

### 3.2 PKCS#1 v1.5 签名模式（RSA_SEL_2048 = 2）

#### **填充结构**

```
签名块（256 字节，RSA-2048）：
┌──────┬──────┬─────────────────────────┬──────┬──────────────┐
│ 0x00 │ 0x01 │  0xFF ... 0xFF (填充)   │ 0x00 │ DigestInfo   │
│  1B  │  1B  │  ~202 字节              │  1B  │  51 字节     │
└──────┴──────┴─────────────────────────┴──────┴──────────────┘
                                                 ↓
                                      ┌──────────────────────┐
                                      │ Algorithm Identifier │
                                      │   (SHA-256 OID)      │
                                      │   19 字节            │
                                      ├──────────────────────┤
                                      │ Hash Value           │
                                      │   32 字节            │
                                      └──────────────────────┘
```

**DigestInfo 结构（ASN.1 DER 编码）**：
```c
// SHA-256 的 DigestInfo
0x30 0x31                      // SEQUENCE, 长度 49 字节
   0x30 0x0d                   // SEQUENCE, 长度 13 字节（算法标识符）
      0x06 0x09                // OBJECT IDENTIFIER, 长度 9
         2.16.840.1.101.3.4.2.1  // SHA-256 OID
      0x05 0x00                // NULL
   0x04 0x20                   // OCTET STRING, 长度 32 字节
      [32 字节 SHA-256 哈希值]
```

#### **优缺点**

**优点**：
- ✓ 简单高效，计算开销小
- ✓ 广泛支持，兼容性好
- ✓ 确定性签名（同一消息总是相同签名）

**缺点**：
- ✗ 填充格式固定，存在理论上的安全证明弱点
- ✗ 易受 Bleichenbacher 攻击（特定场景）
- ✗ 无随机性，无法防止重放攻击（需额外机制）

**适用场景**：RK3399、RK3288 等大多数 Rockchip 平台

---

### 3.3 PSS 签名模式（RSA_SEL_2048_PSS = 3）

PSS (Probabilistic Signature Scheme) 是 PKCS#1 v2.1 引入的改进方案。

#### **填充结构**

```
签名块（256 字节，RSA-2048）：
┌─────────────────────────────────────────┬────────┬──────┐
│          Masked DB                      │   H    │ 0xbc │
│          223 字节                        │ 32 字节│  1B  │
└─────────────────────────────────────────┴────────┴──────┘
              ↑                                ↑
              │                                │
     使用 MGF1 掩码生成                  Hash(M || salt)
```

**详细计算流程**：
```
Step 1: 生成随机盐（Salt）
        salt = random(32 字节)  ← 关键：引入随机性

Step 2: 计算哈希
        M' = 0x00 00 00 00 00 00 00 00 || mHash || salt
        H = SHA256(M')

Step 3: 生成 DB（Data Block）
        DB = 0x00 ... 0x00 || 0x01 || salt
             (填充到 223 字节)

Step 4: 生成掩码（MGF1）
        dbMask = MGF1(H, 223)
        maskedDB = DB ⊕ dbMask

Step 5: 构造签名块
        EM = maskedDB || H || 0xbc

Step 6: RSA 签名
        Signature = EM^d mod n
```

**MGF1 掩码生成函数**：
```python
def MGF1(seed, length):
    """基于 SHA-256 的掩码生成"""
    T = b''
    for counter in range(0, ceil(length / 32)):
        C = counter.to_bytes(4, 'big')
        T += SHA256(seed || C)
    return T[:length]
```

#### **优缺点**

**优点**：
- ✓ **可证明安全**：在随机预言机模型下有严格的安全性证明
- ✓ **随机性**：每次签名都不同（即使消息相同），防止重放攻击
- ✓ **抗伪造**：对抗选择消息攻击（CMA）能力更强
- ✓ **未来趋势**：NIST 推荐使用 PSS

**缺点**：
- ✗ 计算复杂度略高（需要 MGF1）
- ✗ 签名不确定（调试困难）
- ✗ 旧设备可能不支持

**适用场景**：RK3308、PX30、RK3326、RK1808（新一代安全芯片）

---

### 3.4 安全性对比

| 攻击类型 | PKCS#1 v1.5 | PSS |
|---------|-------------|-----|
| 消息伪造攻击 | 需依赖哈希函数安全性 | ✓ 可证明安全 |
| 选择消息攻击（CMA）| 理论上存在弱点 | ✓ 强抗性 |
| 重放攻击 | ✗ 需额外时间戳 | ✓ 随机盐自然防御 |
| Bleichenbacher 攻击 | ✗ 特定场景脆弱 | ✓ 免疫 |
| 长度扩展攻击 | 依赖 SHA-256 | ✓ 结构天然防御 |

**结论**：PSS 在密码学上更安全，但 PKCS#1 v1.5 在工程实践中仍然足够（前提是正确实现）。

---

## 4. Trust 镜像签名架构

### 4.1 Trust 镜像结构

```
trust.img 文件结构（~1 MB）：
┌─────────────────────────────────────────────────────┐
│  Trust Header (512 字节)                             │
│  ┌────────────────────────────────────────────────┐ │
│  │ Tag: "TRUS" (0x53555254)         [4 字节]      │ │
│  │ Version: 0x0100 (BCD)            [4 字节]      │ │
│  │ Flags: RSA | SHA                 [4 字节]      │ │
│  │   - Bit [3:0]: SHA 模式 (3 = SHA-256)         │ │
│  │   - Bit [7:4]: RSA 模式 (2/3 = RSA-2048)      │ │
│  │ Size: (ComponentNum << 16) | (SignOffset >> 2) │ │
│  │ Reserved                                       │ │
│  └────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────┤
│  Component Data Array (N × 16 字节)                 │
│  ┌────────────────────────────────────────────────┐ │
│  │ Component[0]: BL31 (ATF)                       │ │
│  │   - LoadAddr: 0x00010000                      │ │
│  │   - Size: 0x00040000 (256 KB)                 │ │
│  │   - Delay: 0                                  │ │
│  ├────────────────────────────────────────────────┤ │
│  │ Component[1]: BL32 (OP-TEE)                    │ │
│  │   - LoadAddr: 0x08400000                      │ │
│  │   - Size: 0x00100000 (1 MB)                   │ │
│  │   - Delay: 0                                  │ │
│  └────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────┤
│  RSA Signature (256 字节，RSA-2048)                  │
│  [签名覆盖范围：Header + Component Data + Binaries]  │
├─────────────────────────────────────────────────────┤
│  Component Binaries                                 │
│  ┌────────────────────────────────────────────────┐ │
│  │ BL31 Binary (ATF)                 [~60 KB]    │ │
│  ├────────────────────────────────────────────────┤ │
│  │ BL32 Binary (OP-TEE)              [~200 KB]   │ │
│  └────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

### 4.2 Flags 字段编码

```c
// trust_merger.c:756
pHead->flags = (gRSAmode << 4) | (gSHAmode << 0);
```

**位域布局**：
```
  7   6   5   4 | 3   2   1   0
+---+---+---+---+---+---+---+---+
|  RSA Mode     |   SHA Mode    |
+---+---+---+---+---+---+---+---+

RSA Mode (高 4 位)：
  0x0 = 无 RSA 签名
  0x1 = RSA-1024
  0x2 = RSA-2048 (PKCS#1 v1.5)  ← RK3399 使用
  0x3 = RSA-2048 (PSS)          ← RK3308 使用

SHA Mode (低 4 位)：
  0x0 = 无哈希
  0x1 = SHA-160
  0x2 = SHA-256 (大端，RK3368)
  0x3 = SHA-256 (小端)          ← 大多数平台使用
```

**实际示例**：
```c
// RK3399 (默认)
flags = (2 << 4) | (3 << 0) = 0x23
// 解码：RSA-2048 PKCS#1 v1.5 + SHA-256 小端

// RK3308 (指定 --rsa 3)
flags = (3 << 4) | (3 << 0) = 0x33
// 解码：RSA-2048 PSS + SHA-256 小端

// RK3368 (特殊)
flags = (2 << 4) | (2 << 0) = 0x22
// 解码：RSA-2048 PKCS#1 v1.5 + SHA-256 大端
```

---

## 5. PLATFORM_RSA 实现详解

### 5.1 构建系统流程图

```
┌──────────────────────────────────────────────────────┐
│ uboot/make.sh rk3399                                 │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ prepare()                                            │
│  └─> 检查工具链、rkbin 仓库                           │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ select_chip_info()                                   │
│  └─> 从 .config 提取 RKCHIP=RK3399                   │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ fixup_platform_configure()   ← PLATFORM_RSA 设置点   │
│  ┌────────────────────────────────────────────────┐  │
│  │ if [ $RKCHIP = "RK3308" ]; then               │  │
│  │     PLATFORM_RSA="--rsa 3"   # PSS 模式       │  │
│  │ elif [ $RKCHIP = "RK3399" ]; then             │  │
│  │     PLATFORM_RSA=""           # 默认 2048     │  │
│  │ fi                                            │  │
│  └────────────────────────────────────────────────┘  │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ make CROSS_COMPILE=... all                           │
│  └─> 编译 U-Boot，生成 u-boot.bin                    │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ pack_trust_image()                                   │
│  └─> 调用 __pack_64bit_trust_image()                 │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ trust_merger ${PLATFORM_RSA} RK3399TRUST.ini         │
│  ┌────────────────────────────────────────────────┐  │
│  │ RK3399: trust_merger RK3399TRUST.ini          │  │
│  │         → gRSAmode = 2 (默认)                 │  │
│  │                                               │  │
│  │ RK3308: trust_merger --rsa 3 RK3308TRUST.ini  │  │
│  │         → gRSAmode = 3 (PSS)                  │  │
│  └────────────────────────────────────────────────┘  │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ trust_merger.c::mergeTrust()                         │
│  ┌────────────────────────────────────────────────┐  │
│  │ 1. 读取 BL31/BL32 二进制                       │  │
│  │ 2. 构造 Trust Header                          │  │
│  │    pHead->flags = (gRSAmode << 4) | (gSHAmode)│  │
│  │ 3. 计算签名                                   │  │
│  │    - SHA-256 哈希整个镜像                     │  │
│  │    - 使用 RSA 私钥签名（开发者密钥）           │  │
│  │ 4. 写入 trust.img                             │  │
│  └────────────────────────────────────────────────┘  │
└───────────────┬──────────────────────────────────────┘
                ↓
┌──────────────────────────────────────────────────────┐
│ 输出：trust.img (包含签名)                            │
└──────────────────────────────────────────────────────┘
```

### 5.2 关键代码路径

#### **(1) 平台配置设置 (make.sh:606-619)**

```bash
fixup_platform_configure()
{
    local count plat

    # <*> 为不同平台修正 RSA/SHA 打包模式
    # RK3308/PX30/RK3326/RK1808 使用 RSA-PKCS1 V2.1 算法
    if [ $RKCHIP = "PX30" -o $RKCHIP = "RK3326" -o \
         $RKCHIP = "RK3308" -o $RKCHIP = "RK1808" ]; then
        PLATFORM_RSA="--rsa 3"      # PSS 模式
    # RK3368 使用 Rockchip 大端序 SHA256
    elif [ $RKCHIP = "RK3368" ]; then
        PLATFORM_SHA="--sha 2"      # 大端 SHA-256
    # 其他平台（RK3399、RK3288 等）使用默认配置
    # PLATFORM_RSA="" → trust_merger 默认使用 RSA-2048 PKCS#1 v1.5
    fi

    # ... 镜像大小配置 ...
}
```

**设计原理**：
- 不同芯片的 **BootROM 固件要求不同的签名格式**
- RK3308 等新芯片的 BootROM **只认 PSS 签名**
- RK3399 等旧芯片的 BootROM **只认 PKCS#1 v1.5 签名**
- 如果签名格式不匹配，**BootROM 拒绝启动**

#### **(2) Trust 打包调用 (make.sh:1058-1086)**

```bash
__pack_64bit_trust_image()
{
    local ini=$1

    # 检查 ini 文件是否存在
    if [ ! -f ${ini} ]; then
        echo "pack trust failed! Can't find: ${ini}"
        return
    fi

    # 切换到 rkbin 目录进行打包
    cd ${RKBIN}

    # 调用 trust_merger 工具
    # 关键参数：
    #   PLATFORM_SHA: SHA 哈希模式
    #   PLATFORM_RSA: RSA 加密模式 ← 核心参数
    #   PLATFORM_TRUST_IMG_SIZE: 镜像大小限制
    #   BIN_PATH_FIXUP: 路径替换
    #   PACK_IGNORE_BL32: 是否忽略 BL32
    ${RKTOOLS}/trust_merger \
        ${PLATFORM_SHA} \
        ${PLATFORM_RSA} \
        ${PLATFORM_TRUST_IMG_SIZE} \
        ${BIN_PATH_FIXUP} \
        ${PACK_IGNORE_BL32} \
        ${ini}

    # 移动生成的 trust*.img 文件
    cd - && mv ${RKBIN}/trust*.img ./
    echo "pack trust okay! Input: ${ini}"
}
```

**实际命令示例**：
```bash
# RK3399 (PLATFORM_RSA="")
/path/to/trust_merger \
    --sha 3 \
    --size 1024 2 \
    --replace tools/rk_tools/ ./ \
    /path/to/rkbin/RKTRUST/RK3399TRUST.ini

# RK3308 (PLATFORM_RSA="--rsa 3")
/path/to/trust_merger \
    --sha 3 \
    --rsa 3 \
    --size 1024 2 \
    /path/to/rkbin/RKTRUST/RK3308TRUST.ini
```

#### **(3) 参数解析 (trust_merger.c:1110-1118)**

```c
else if (!strcmp(OPT_RSA, argv[i])) {
    // RSA 模式设置
    i++;
    if (!is_digit(*(argv[i]))) {  // 检查参数是否为数字
        printHelp();
        return -1;
    }
    gRSAmode = *(argv[i]) - '0';  // 字符转数字：'3' → 3
    LOGD("rsa mode:%d\n", gRSAmode);
}
```

**全局变量默认值 (trust_merger.c:62)**：
```c
static uint8_t gRSAmode = RSA_SEL_2048;  // 默认值 2
static uint8_t gSHAmode = SHA_SEL_256;   // 默认值 3
```

**参数处理逻辑**：
```
RK3399:
  命令行无 --rsa 参数 → gRSAmode = 2 (默认值)

RK3308:
  命令行有 --rsa 3 → 解析 '3' 字符 → gRSAmode = 3
```

#### **(4) 写入镜像头 (trust_merger.c:748-756)**

```c
// Trust 头部初始化
TRUST_HEADER *pHead = (TRUST_HEADER *)gBuf;
memcpy(&pHead->tag, TRUST_HEAD_TAG, 4);  // 魔数 "TRUS"

// 版本号（BCD 编码）
pHead->version = (getBCD(gOpts.major) << 8) | getBCD(gOpts.minor);

// flags 字段：编码 RSA 和 SHA 模式
pHead->flags = 0;
pHead->flags |= (gSHAmode << 0);  // 低 4 位：SHA 模式
pHead->flags |= (gRSAmode << 4);  // 高 4 位：RSA 模式

// 计算签名偏移和组件数量
SignOffset = sizeof(TRUST_HEADER) + nComponentNum * sizeof(COMPONENT_DATA);
pHead->size = (nComponentNum << 16) | (SignOffset >> 2);

// ... 后续签名计算 ...
```

**Flags 字段实际值**：
```c
// RK3399
flags = (2 << 4) | (3 << 0) = 0x23
// 二进制：0010 0011
//         ^^^^ ^^^^
//          RSA  SHA

// RK3308
flags = (3 << 4) | (3 << 0) = 0x33
// 二进制：0011 0011
//         ^^^^ ^^^^
//          RSA  SHA
```

---

## 6. 安全启动验证流程

### 6.1 BootROM 验证逻辑

```
┌─────────────────────────────────────────────────────┐
│ 芯片上电 → BootROM 启动（固化在 ROM 中，不可修改）   │
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 1. 从 Flash/eMMC 读取 trust.img                      │
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 2. 解析 Trust Header                                 │
│    ├─> 验证魔数：Tag == "TRUS"?                      │
│    ├─> 读取 Flags 字段                               │
│    │    rsaMode = (flags >> 4) & 0xF                │
│    │    shaMode = (flags >> 0) & 0xF                │
│    └─> 读取签名偏移                                  │
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 3. 提取 RSA 签名（256 字节）                          │
│    signature = trust.img[signOffset : signOffset+256]│
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 4. 使用 BootROM 内置公钥解密签名                      │
│    [BootROM 公钥烧写在 eFuse/OTP 中，不可修改]        │
│                                                      │
│    if (rsaMode == 2):  # PKCS#1 v1.5                │
│        EM = signature^e mod n                       │
│        验证填充格式：0x00 0x01 0xFF...0xFF 0x00     │
│        提取 DigestInfo                               │
│        Hash' = DigestInfo 中的哈希值                 │
│                                                      │
│    elif (rsaMode == 3):  # PSS                      │
│        EM = signature^e mod n                       │
│        maskedDB = EM[0:223]                         │
│        H = EM[223:255]                              │
│        dbMask = MGF1(H, 223)                        │
│        DB = maskedDB ⊕ dbMask                       │
│        salt = DB 末尾 32 字节                        │
│        M' = 0x00...00 || mHash || salt              │
│        Hash' = SHA256(M')                           │
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 5. 重新计算镜像哈希                                   │
│    dataToHash = trust.img[0 : signOffset]           │
│    Hash = SHA256(dataToHash)                        │
└───────────────┬─────────────────────────────────────┘
                ↓
┌─────────────────────────────────────────────────────┐
│ 6. 比较哈希值                                         │
│    if (Hash == Hash'):                              │
│        ✓ 签名验证成功                                 │
│        ✓ 固件完整性确认                               │
│        ✓ 固件来源可信                                 │
│        → 继续加载 BL31/BL32                          │
│    else:                                            │
│        ✗ 签名验证失败                                 │
│        ✗ 固件可能被篡改                               │
│        → 停止启动，进入错误模式                        │
└─────────────────────────────────────────────────────┘
```

### 6.2 密钥管理架构

```
┌──────────────── 开发者端 ────────────────┐
│                                          │
│  1. 生成 RSA-2048 密钥对                  │
│     openssl genrsa -out private.pem 2048│
│     openssl rsa -in private.pem \       │
│                 -pubout -out public.pem │
│                                          │
│  2. 私钥签名固件                          │
│     trust_merger 使用 private.pem 签名   │
│                                          │
│  3. 私钥保护                              │
│     - 存储在 HSM（硬件安全模块）中        │
│     - 严格权限控制                        │
│     - 绝不泄露                            │
│                                          │
└──────────────────────────────────────────┘
                   │
                   ↓ [公钥分发]
┌──────────────── 芯片生产线 ──────────────┐
│                                          │
│  1. 烧写公钥到 eFuse/OTP                  │
│     - 一次性可编程存储器                  │
│     - 物理不可修改                        │
│     - BootROM 启动时自动读取             │
│                                          │
│  2. 锁定 eFuse（可选）                    │
│     - 防止后续修改                        │
│     - 启用安全启动强制模式                │
│                                          │
└──────────────────────────────────────────┘
                   │
                   ↓ [设备出厂]
┌──────────────── 终端用户设备 ────────────┐
│                                          │
│  BootROM 内置公钥                         │
│  - 验证所有固件签名                       │
│  - 拒绝未签名或签名错误的固件             │
│  - 公钥永久有效，不可撤销                 │
│                                          │
└──────────────────────────────────────────┘
```

**关键安全点**：
1. **私钥绝不进入芯片**：只有公钥烧写到设备
2. **eFuse 不可修改**：攻击者无法替换公钥
3. **BootROM 不可更新**：验证逻辑固化，不可绕过
4. **信任链起点**：公钥是整个安全启动的信任根（Root of Trust）

---

## 7. 安全性分析与攻击防御

### 7.1 威胁模型

#### **攻击者能力假设**：
- ✓ 可以修改 Flash/eMMC 中的固件（物理访问）
- ✓ 可以捕获网络传输的固件（中间人攻击）
- ✓ 可以逆向工程固件格式
- ✗ **不能**获取开发者私钥（密钥管理安全）
- ✗ **不能**修改 BootROM（固化在 ROM 中）
- ✗ **不能**修改 eFuse 中的公钥（物理不可篡改）

---

### 7.2 攻击场景分析

#### **(1) 固件篡改攻击**

**攻击步骤**：
```
1. 攻击者获取 trust.img
2. 修改 BL31 二进制，植入恶意代码
3. 将修改后的 trust.img 写回 Flash
4. 重启设备
```

**防御机制**：
```
BootROM 验证流程：
1. 计算 Hash(修改后的 BL31) → Hash_new
2. 解密原始签名 → Hash_old
3. Hash_new ≠ Hash_old
   → 签名验证失败
   → 拒绝启动

结果：✓ 攻击被阻止
```

**为什么无法绕过**：
- 攻击者没有私钥，无法生成有效签名
- SHA-256 抗碰撞：找到 Hash_new == Hash_old 的概率 < 2^-256
- RSA-2048 分解需要数百万年

---

#### **(2) 签名替换攻击**

**攻击步骤**：
```
1. 攻击者获取两个官方固件：
   - trust_v1.img（旧版本，已知漏洞）
   - trust_v2.img（新版本，已修复漏洞）
2. 尝试将 trust_v1 的签名附加到 trust_v2
```

**防御机制**：
```
签名绑定到内容：
Signature = Hash(固件内容)^d mod n

如果内容改变，哈希值改变：
Hash(v1) ≠ Hash(v2)
→ Signature_v1 验证失败

结果：✓ 攻击被阻止
```

---

#### **(3) 降级攻击**

**攻击步骤**：
```
1. 设备当前运行 trust_v2.img（安全）
2. 攻击者刷入 trust_v1.img（旧版本，有漏洞）
3. trust_v1 签名有效（官方签名）
4. BootROM 验证通过
```

**风险**：
```
✗ 签名验证无法防止降级攻击
✓ 旧版本固件签名仍然有效
```

**额外防御措施**（需在 Trust 层实现）：
```c
// 在 BL31 中增加版本检查
#define MIN_ALLOWED_VERSION 0x0200  // 最低允许版本 v2.0

int verify_version(uint32_t version) {
    if (version < MIN_ALLOWED_VERSION) {
        // 版本号存储在 eFuse 中，每次更新时写入
        return -1;  // 拒绝启动
    }
    return 0;
}
```

**结论**：RSA 签名**只保证完整性和真实性**，不保证时效性（需额外机制）。

---

#### **(4) 中间人攻击（固件更新场景）**

**攻击步骤**：
```
用户下载固件更新：
设备 ←─────[网络]─────→ 官方服务器
           ↑
        攻击者拦截
```

**防御机制**：
```
方案 1：HTTPS 传输
  ✓ TLS 加密通道
  ✓ 服务器证书验证
  ✗ 仍需验证固件签名（防止服务器被攻破）

方案 2：固件签名验证
  1. 下载 trust.img + trust.img.sig（分离签名）
  2. 设备使用内置公钥验证签名
  3. 验证通过才刷入

方案 3：双重验证
  ✓ HTTPS + 固件签名
  → 最安全
```

---

#### **(5) 侧信道攻击**

**攻击类型**：
- **功耗分析（SPA/DPA）**：监测 RSA 运算时的功耗波动
- **电磁泄露**：捕获芯片电磁辐射
- **时序攻击**：测量签名验证时间差异

**防御措施**：
```c
// 1. 常量时间实现（Constant-Time）
// 避免条件分支导致时序差异
uint32_t constant_time_compare(uint8_t *a, uint8_t *b, size_t len) {
    uint32_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];  // 不使用 if，始终执行
    }
    return diff;  // 0 表示相等
}

// 2. 蒙哥马利阶梯（Montgomery Ladder）
// RSA 模幂运算使用固定时间算法

// 3. 随机延迟（Blinding）
// 在签名验证前后添加随机延迟
```

**硬件防护**：
- 屏蔽设计（减少电磁泄露）
- 随机数发生器（TRNG）
- 安全加密协处理器

---

### 7.3 PSS 模式的额外安全优势

#### **场景：重放攻击防御**

**PKCS#1 v1.5（确定性）**：
```
同一固件每次签名结果相同：
Hash = SHA256(firmware)
Signature = Hash^d mod n  ← 固定值

风险：攻击者可以记录签名，在未来重放
（需额外时间戳机制防御）
```

**PSS（随机性）**：
```
同一固件每次签名结果不同：
salt = random(32 字节)  ← 每次不同
M' = 0x00...00 || Hash || salt
H = SHA256(M')
Signature = H^d mod n  ← 每次不同

优势：即使固件相同，签名也不同
     → 自然防御重放攻击
```

#### **场景：选择消息攻击（CMA）**

**定义**：攻击者诱导签名者对精心构造的消息签名，然后推导出其他消息的签名。

**PKCS#1 v1.5**：
```
理论上存在弱点（Bleichenbacher 1998）
需要依赖 SHA-256 的单向性
```

**PSS**：
```
可证明安全（在随机预言机模型下）
即使攻击者获得 2^64 个签名样本，
仍无法伪造新签名（假设 RSA 困难性）
```

---

## 8. 实战案例

### 8.1 为 RK3399 构建 Trust 镜像

#### **步骤 1：准备环境**

```bash
# 克隆 rkbin 仓库（包含 BL31/BL32 二进制）
cd /path/to/build_rk3399
git clone https://github.com/rockchip-linux/rkbin.git external/rkbin

# 检查 Trust 配置文件
cat external/rkbin/RKTRUST/RK3399TRUST.ini
```

**RK3399TRUST.ini 内容**：
```ini
[VERSION]
MAJOR=1
MINOR=0

[BL30_OPTION]
SEC=0

[BL31_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl31_v1.35.elf
ADDR=0x00010000
```

[BL32_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl32_v2.01.bin
ADDR=0x08400000

[BL33_OPTION]
SEC=0

[OUTPUT]
PATH=trust.img
```

#### **步骤 2：编译 U-Boot**

```bash
cd uboot

# 配置 RK3399 板子
./make.sh rk3399
# 等价于：make rk3399-orangepi_defconfig && make CROSS_COMPILE=... all
```

**关键输出**：
```
Building U-Boot...
  CC      arch/arm/lib/bootm.o
  LD      u-boot
  OBJCOPY u-boot.bin

Image Name:   U-Boot 2017.09 for rk3399-orangepi
Created:      Wed Dec 29 10:30:45 2025
Image Size:   800 KiB
Load Address: 0x00200000
Entry Point:  0x00200000
```

#### **步骤 3：打包 Trust 镜像**

```bash
# 手动调用 trust_merger
./make.sh trust

# 实际执行的命令（调试模式查看）：
# cd ../external/rkbin
# tools/trust_merger \
#     --sha 3 \
#     --size 1024 2 \
#     --replace tools/rk_tools/ ./ \
#     RKTRUST/RK3399TRUST.ini
```

**输出分析**：
```
trust_merger: Rockchip Trust Image Merger v1.32
Input file: RKTRUST/RK3399TRUST.ini
Component[0]: BL31
  File: bin/rk33/rk3399_bl31_v1.35.elf
  Load Address: 0x00010000
  Size: 0x0000E800 (59392 bytes)
Component[1]: BL32
  File: bin/rk33/rk3399_bl32_v2.01.bin
  Load Address: 0x08400000
  Size: 0x00032000 (204800 bytes)

Trust Header:
  Tag: TRUS
  Version: 0x0100
  Flags: 0x23           ← RSA-2048 PKCS#1 v1.5 + SHA-256
  ComponentNum: 2
  SignOffset: 0x200

Calculating SHA-256 hash...
Hash: 3a 5f 7b 9c ... (32 bytes)

Signing with RSA-2048...
Signature: a4 6d 3e ... (256 bytes)

Output: trust.img (Size: 1048576 bytes = 1 MB)
pack trust okay! Input: RKTRUST/RK3399TRUST.ini
```

#### **步骤 4：验证镜像**

```bash
# 使用 hexdump 查看 Trust Header
hexdump -C trust.img | head -n 32

# 输出：
00000000  54 52 55 53 00 01 00 00  23 00 00 00 00 02 00 80  |TRUS....#.......|
          ^^^^^^^^ ^^^^^^^^^^^^^^  ^^ ^^^^^^^^^^
           Tag      Version        Flags (0x23)

# 提取签名
dd if=trust.img of=signature.bin bs=1 skip=512 count=256

# 如果有公钥，可以验证签名
openssl dgst -sha256 -verify public.pem -signature signature.bin \
    <(dd if=trust.img bs=1 count=512)
```

---

### 8.2 为 RK3308 构建 PSS 签名镜像

#### **关键差异**

```bash
# RK3308 配置文件
cat external/rkbin/RKTRUST/RK3308TRUST.ini
```

**构建命令**：
```bash
cd uboot
./make.sh rk3308
./make.sh trust

# 实际执行：
# trust_merger --rsa 3 --sha 3 RKTRUST/RK3308TRUST.ini
#              ^^^^^^^^
#              PSS 模式
```

**输出对比**：
```
RK3399: Flags: 0x23 (RSA-2048 v1.5 + SHA-256)
RK3308: Flags: 0x33 (RSA-2048 PSS + SHA-256)
        ^^^^
        高 4 位 = 3
```

#### **签名格式差异**

```bash
# PKCS#1 v1.5 签名块（RK3399）
hexdump -C signature_rk3399.bin
00000000  00 01 ff ff ff ff ... ff 00 30 31 30 0d 06 09 ...
          ^^^^^ ^^^^^^^^^^^^^^^^^^ ^^
          固定头   0xFF 填充        分隔符

# PSS 签名块（RK3308）
hexdump -C signature_rk3308.bin
00000000  8a 3f 7b 9c ... (看似随机数据)
          ^^^^^^^^^^^^^^
          Masked DB（每次不同）
```

---

### 8.3 自定义 RSA 密钥（开发调试）

**警告**：生产环境必须使用 Rockchip 官方密钥或客户自有密钥，下面仅用于学习。

#### **步骤 1：生成密钥对**

```bash
# 生成 2048 位 RSA 私钥
openssl genrsa -out rk3399_private.pem 2048

# 提取公钥
openssl rsa -in rk3399_private.pem -pubout -out rk3399_public.pem

# 转换为 C 数组（用于 BootROM）
openssl rsa -in rk3399_public.pem -pubin -text -noout
```

**公钥输出示例**：
```
Modulus (256 bytes):
    00:c5:7a:3f:...
Exponent: 65537 (0x10001)
```

#### **步骤 2：修改 trust_merger 使用自定义密钥**

```c
// trust_merger.c 中修改（实际需重新编译工具）
// 注意：原始代码可能使用硬编码密钥或从 .pem 文件读取

#include <openssl/rsa.h>
#include <openssl/pem.h>

RSA *load_private_key(const char *path) {
    FILE *fp = fopen(path, "r");
    RSA *rsa = PEM_read_RSAPrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    return rsa;
}

// 在 mergeTrust() 中：
RSA *rsa = load_private_key("rk3399_private.pem");
```

#### **步骤 3：烧写公钥到 eFuse（危险操作）**

```bash
# 这是一次性不可逆操作！仅在测试芯片上执行！
# 需要专用烧写工具（如 Rockchip efuse_tool）

# 1. 将公钥转换为二进制格式
# 2. 使用 efuse_tool 烧写到指定区域
# 3. 锁定 eFuse（可选）

# 示例命令（实际工具因平台而异）：
efuse_tool write --region rsa_pubkey --data rk3399_public.bin
efuse_tool lock --region rsa_pubkey
```

---

### 8.4 调试签名验证失败

#### **常见错误 1：Flags 不匹配**

**症状**：
```
BootROM 报错：Trust verify failed (Error code: 0x03)
```

**原因**：
```
镜像 Flags: 0x33 (PSS)
芯片期望: 0x23 (PKCS#1 v1.5)
```

**解决**：
```bash
# 检查平台配置
grep "PLATFORM_RSA" uboot/make.sh

# 确保 RK3399 不设置 --rsa 参数
# 或显式设置 PLATFORM_RSA="--rsa 2"
```

#### **常见错误 2：签名无效**

**症状**：
```
BootROM 报错：Trust verify failed (Error code: 0x05)
```

**原因**：
- 使用了错误的私钥签名
- 公钥与私钥不匹配
- 镜像在签名后被修改

**调试**：
```bash
# 1. 验证密钥对是否匹配
echo "test" > test.txt
openssl dgst -sha256 -sign private.pem -out sig.bin test.txt
openssl dgst -sha256 -verify public.pem -signature sig.bin test.txt
# 应输出：Verified OK

# 2. 检查镜像是否被篡改
# 重新打包并对比哈希
./make.sh trust
sha256sum trust.img
```

#### **常见错误 3：eFuse 未烧写**

**症状**：
```
BootROM 跳过签名验证，直接启动（不安全）
```

**原因**：
- eFuse 中没有公钥
- 安全启动未启用

**检查**：
```bash
# 读取 eFuse 内容
efuse_tool read --region rsa_pubkey
# 应输出公钥数据，否则为空

# 检查安全启动标志
efuse_tool read --region secure_boot_en
# 应为 0x01（启用）
```

---

## 9. 总结与最佳实践

### 9.1 核心要点

| 概念 | 说明 |
|------|------|
| **PLATFORM_RSA** | Shell 变量，指定 trust_merger 的 RSA 模式 |
| **RSA-2048** | 当前嵌入式主流签名强度，平衡安全与性能 |
| **PKCS#1 v1.5** | 传统模式，RK3399 等旧平台使用 |
| **PSS** | 现代模式，RK3308 等新平台使用，更安全 |
| **BootROM 验证** | 信任链起点，基于 eFuse 公钥验证固件 |
| **防篡改** | RSA 签名确保固件完整性和真实性 |

### 9.2 安全检查清单

开发阶段：
- [ ] 使用至少 2048 位 RSA 密钥
- [ ] 私钥存储在 HSM 或加密存储中
- [ ] 启用编译时签名自动化
- [ ] 记录所有已签名固件的哈希值

生产阶段：
- [ ] 烧写正确的公钥到 eFuse
- [ ] 锁定 eFuse 防止修改
- [ ] 启用安全启动强制模式
- [ ] 实现固件版本防回滚机制

部署阶段：
- [ ] 使用 HTTPS 分发固件更新
- [ ] 提供签名文件供用户验证
- [ ] 监控固件验证失败事件
- [ ] 建立密钥轮换应急预案

### 9.3 进阶主题

1. **链式信任（Chain of Trust）**
   ```
   BootROM → BL31 → BL32 → U-Boot → Kernel → Rootfs
   每一级验证下一级签名
   ```

2. **密钥撤销机制**
   ```
   使用证书链而非直接公钥
   支持吊销泄露的密钥
   ```

3. **量子后密码学**
   ```
   RSA 在量子计算机下不安全
   未来迁移到 Lattice-based 签名
   ```

---

## 附录

### A. 参考资料

- [PKCS #1 v2.2: RSA Cryptography Standard](https://www.rfc-editor.org/rfc/rfc8017)
- [FIPS 186-4: Digital Signature Standard](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.186-4.pdf)
- [Rockchip Secure Boot Application Note](https://rockchip.fr/)
- [OpenSSL RSA Documentation](https://www.openssl.org/docs/man1.1.1/man1/rsa.html)

### B. 术语表

| 术语 | 英文 | 解释 |
|------|------|------|
| 信任根 | Root of Trust | 安全系统的起点，通常为 BootROM |
| eFuse | Electrical Fuse | 一次性可编程存储器 |
| PSS | Probabilistic Signature Scheme | 概率签名方案 |
| MGF1 | Mask Generation Function 1 | 掩码生成函数 |
| OTP | One-Time Programmable | 一次性可编程 |
| CMA | Chosen Message Attack | 选择消息攻击 |

### C. 代码位置索引

| 功能 | 文件路径 | 行号 |
|------|---------|------|
| PLATFORM_RSA 声明 | `uboot/make.sh` | 95 |
| RK3308 配置 | `uboot/make.sh` | 613-614 |
| trust_merger 调用 | `uboot/make.sh` | 1079 |
| RSA 模式定义 | `uboot/tools/rockchip/trust_merger.c` | 45-48 |
| 参数解析 | `uboot/tools/rockchip/trust_merger.c` | 1110-1118 |
| Flags 写入 | `uboot/tools/rockchip/trust_merger.c` | 756 |

---

**文档版本**：v1.0
**最后更新**：2025-12-29
**作者**：Claude Sonnet 4.5 (自动生成)
**许可证**：CC BY-SA 4.0
