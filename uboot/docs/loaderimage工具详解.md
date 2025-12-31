# Rockchip loaderimage 工具详解教程

## 文档概述

本文档详细讲解 Rockchip `loaderimage` 工具的工作原理和执行流程。该工具是 RK3399 固件打包体系中的核心组件，负责为 U-Boot 和 Trust OS 添加 Rockchip 专用头部，使其能够被 BootROM 识别和加载。

**适用人群**：嵌入式开发者、固件工程师、对 ARM 启动流程感兴趣的学习者

**源码位置**：`uboot/tools/rockchip/loaderimage.c`

---

## 一、工具的核心目的

### 1.1 问题背景

想象一个快递包裹的场景：

- **商品（原始文件）**：`u-boot.bin` 或 `trust.bin` - 编译生成的纯二进制代码
- **快递公司（BootROM）**：Rockchip 芯片内置的启动程序（硬件固化，无法修改）
- **问题**：BootROM 不认识"裸奔"的二进制文件

BootROM 需要看到特定格式的"快递单"才能正确处理：

| 快递单信息           | 对应字段              | 作用                     |
|---------------------|----------------------|-------------------------|
| 收件人地址           | `loader_load_addr`   | 告诉 BootROM 加载到 DRAM 的哪个地址 |
| 包裹类型（识别码）   | `magic`              | "LOADER  " 或 "TOS     " |
| 重量（大小）         | `loader_load_size`   | 二进制文件的字节数       |
| 防伪标签             | `hash` (SHA256)      | 安全启动时验证完整性     |
| 快递单号（校验码）   | `crc32`              | 快速检测数据是否损坏     |

### 1.2 解决方案

`loaderimage` 工具的作用就是**给二进制文件"加包装"**：

```
原始文件（520KB）         添加头部（2KB）              最终镜像
┌─────────────┐          ┌──────────────┐           ┌──────────────┐
│ u-boot.bin  │  =====>  │ Rockchip头部 │  =====>  │ uboot.img    │
│ (纯代码)    │          │ + u-boot.bin │           │ (4个副本)    │
└─────────────┘          │ + 填充       │           └──────────────┘
                         └──────────────┘
                          (单个副本1MB)              (总大小4MB)
```

---

## 二、工具的三种工作模式

### 2.1 模式1：打包（PACK）- 最常用

**用途**：将原始 bin 文件添加 Rockchip 头部生成 .img 镜像

```bash
# 命令格式
loaderimage --pack --uboot <input.bin> <output.img> [load_addr] [--size <KB> <num>]

# 实际示例（来自 make.sh）
./tools/loaderimage --pack --uboot ./out/u-boot/u-boot.bin uboot.img 0x00200000 --size 1024 4
```

**工作流程**：

```
输入：u-boot.bin（520KB 的纯二进制）
       ↓
[步骤1] 读取文件到内存
       ↓
[步骤2] 构造 2048 字节的 Rockchip 头部
       ├─ 魔数："LOADER  "（8字节，末尾2个空格）
       ├─ 加载地址：0x00200000（BootROM 将代码加载到此 DRAM 地址）
       ├─ 文件大小：532480 字节
       ├─ CRC32 校验码：对数据计算得出
       └─ SHA256 哈希：对数据+元信息计算（用于安全启动）
       ↓
[步骤3] 将头部和数据合并，填充到 1MB
       ↓
[步骤4] 写入 4 个副本（提高可靠性，防止 Flash 坏块）
       ↓
输出：uboot.img（4MB，包含4个完全相同的1MB副本）
```

### 2.2 模式2：解包（UNPACK）- 调试用

**用途**：从 .img 镜像中提取原始 bin 文件（用于逆向分析或验证）

```bash
loaderimage --unpack --uboot uboot.img u-boot-extracted.bin
```

**工作流程**：

```
输入：uboot.img（4MB 镜像）
       ↓
[步骤1] 读取前 2048 字节（Rockchip 头部）
       ↓
[步骤2] 解析头部，获取 loader_load_size（实际数据大小）
       ↓
[步骤3] 读取后续 loader_load_size 字节（跳过头部）
       ↓
输出：u-boot-extracted.bin（520KB 的纯二进制，与原始文件相同）
```

### 2.3 模式3：查看信息（INFO）

**用途**：显示镜像头部的关键信息

```bash
loaderimage --info uboot.img
```

**输出示例**：

```
The image info:
Rollback index is 0          # 版本号（用于防回滚攻击）
Load Addr is 0x00200000      # 加载地址
```

---

## 三、Rockchip 镜像头部结构详解

### 3.1 头部总览（2048 字节）

```c
typedef struct tag_second_loader_hdr {
    /* ===== 基础信息区（32字节） ===== */
    uint8_t  magic[8];           // [0-7]   魔数："LOADER  " 或 "TOS     "
    uint32_t version;            // [8-11]  版本号（Rollback 保护）
    uint32_t reserved0;          // [12-15] 保留
    uint32_t loader_load_addr;   // [16-19] 加载到 DRAM 的地址
    uint32_t loader_load_size;   // [20-23] 实际代码大小（字节）

    /* ===== 校验信息区（32字节） ===== */
    uint32_t crc32;              // [24-27] CRC32 校验码
    uint32_t hash_len;           // [28-31] 哈希长度（20=SHA1, 32=SHA256）
    uint8_t  hash[32];           // [32-63] SHA256 哈希值

    /* ===== 填充区（960字节） ===== */
    uint8_t reserved[960];       // [64-1023] 对齐到 1024 字节

    /* ===== RSA 签名区（264字节） ===== */
    uint32_t signTag;            // [1024-1027] 签名标记：0x4E474953 ("SIGN")
    uint32_t signlen;            // [1028-1031] RSA 签名长度
    uint8_t  rsaHash[256];       // [1032-1287] RSA 签名数据

    /* ===== 尾部填充（760字节） ===== */
    uint8_t reserved2[760];      // [1288-2047] 填充至 2048 字节
} second_loader_hdr;
```

### 3.2 关键字段说明

#### magic（魔数）

- **U-Boot 镜像**：`"LOADER  "`（末尾有 2 个空格，共 8 字节）
- **Trust OS 镜像**：`"TOS     "`（末尾有 5 个空格，共 8 字节）
- **作用**：BootROM 扫描 Flash 时，通过魔数识别这是一个有效的启动镜像

#### loader_load_addr（加载地址）

- **U-Boot 默认地址**：`0x00200000`（DRAM 的 2MB 位置）
- **Trust OS 默认地址**：`0x08400000`（避免与 U-Boot 冲突）
- **作用**：BootROM 会将镜像数据（不包括头部）加载到此地址，然后跳转执行

#### CRC32 vs SHA256

| 校验类型 | 计算速度 | 安全性 | 用途 |
|---------|---------|-------|------|
| CRC32   | 快      | 低    | 快速检测传输错误、Flash 坏块 |
| SHA256  | 慢      | 高    | 安全启动时的数字指纹，防篡改 |

#### RSA 签名区

- **启用条件**：开启安全启动（Secure Boot）时使用
- **工作流程**：
  1. 打包时：用私钥对 SHA256 哈希签名 → 存入 `rsaHash`
  2. 启动时：BootROM 用公钥验证签名 → 验证通过才执行代码
- **未启用时**：此区域全为 0

---

## 四、实战案例：命令执行全流程追踪

### 4.1 命令示例（来自 make.sh:787）

```bash
${RKTOOLS}/loaderimage --pack --uboot ${OUTDIR}/u-boot.bin uboot.img ${UBOOT_LOAD_ADDR} ${PLATFORM_UBOOT_IMG_SIZE}
```

**变量替换后**：

```bash
./tools/loaderimage --pack --uboot ./out/u-boot/u-boot.bin uboot.img 0x00200000 --size 1024 4
```

**参数含义**：

```
argv[0] = "./tools/loaderimage"       # 程序名
argv[1] = "--pack"                    # 工作模式：打包
argv[2] = "--uboot"                   # 镜像类型：U-Boot
argv[3] = "./out/u-boot/u-boot.bin"   # 输入文件（原始 bin）
argv[4] = "uboot.img"                 # 输出文件（最终镜像）
argv[5] = "0x00200000"                # 加载地址（2MB）
argv[6] = "--size"                    # 指定自定义大小
argv[7] = "1024"                      # 单个副本大小：1024KB = 1MB
argv[8] = "4"                         # 副本数量：4个
```

### 4.2 代码执行流程追踪

#### 阶段1：参数解析（main 函数第 174-229 行）

```c
// 变量初始化
int mode = -1, image = -1;
uint32_t in_loader_addr = -1;
uint32_t in_size = 0, in_num = 0;
char *file_in = NULL, *file_out = NULL;

for (i = 1; i < argc; i++) {
    // ===== 第1轮：处理 "--pack" =====
    if (!strcmp(argv[i], OPT_PACK)) {
        mode = MODE_PACK;  // mode = 0（打包模式）
    }

    // ===== 第2轮：处理 "--uboot" =====
    else if (!strcmp(argv[i], OPT_UBOOT)) {
        image = IMAGE_UBOOT;                  // image = 0（U-Boot 类型）
        file_in = argv[++i];                  // i=3, "./out/u-boot/u-boot.bin"
        file_out = argv[++i];                 // i=4, "uboot.img"

        // 检查下一个参数是否是加载地址（不以 "--" 开头）
        if ((argv[i + 1]) && (strncmp(argv[i + 1], "--", 2))) {
            in_loader_addr = str2hex(argv[++i]);  // i=5
            // str2hex("0x00200000") = 0x200000 = 2097152
        }
    }

    // ===== 第3轮：处理 "--size" =====
    else if (!strcmp(argv[i], OPT_SIZE)) {
        in_size = strtoul(argv[++i], NULL, 10);  // i=7, in_size=1024（KB）

        // 检查是否 64KB 对齐（Rockchip 要求）
        if (in_size % 64) {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
        in_size *= 1024;  // 转换为字节：1024KB = 1048576 字节 = 1MB

        in_num = strtoul(argv[++i], NULL, 10);  // i=8, in_num=4
    }
}
```

**解析完成后的变量状态**：

```c
mode = 0                 // MODE_PACK（打包）
image = 0                // IMAGE_UBOOT（U-Boot）
file_in = "./out/u-boot/u-boot.bin"
file_out = "uboot.img"
in_loader_addr = 0x00200000  // 2MB 位置
in_size = 1048576        // 1MB
in_num = 4               // 4 个副本
```

#### 阶段2：镜像配置（第 231-258 行）

```c
if (image == IMAGE_UBOOT) {  // 条件成立
    // 配置 U-Boot 镜像参数
    name = "uboot";
    magic = "LOADER  ";              // 8字节魔数
    version = UBOOT_VERSION_STRING;  // 编译时生成的版本字符串

    // 使用用户指定的值（因为提供了 --size 参数）
    max_size = in_size;   // 1048576（1MB）
    max_num = in_num;     // 4

    // 使用用户指定的加载地址
    loader_addr = in_loader_addr;  // 0x00200000
}
```

**配置完成后**：

```c
name = "uboot"
magic = "LOADER  "       // 注意末尾有2个空格
max_size = 1048576       // 单个副本 1MB
max_num = 4              // 4 个副本
loader_addr = 0x00200000 // 加载地址
```

#### 阶段3：打包核心流程（第 260-380 行）

##### 步骤1：分配缓冲区

```c
if (mode == MODE_PACK) {
    // 分配内存：max_size × max_num = 1MB × 4 = 4MB
    buf = calloc(max_size, max_num);  // calloc 会自动清零

    printf("\n load addr is 0x%x!\n", loader_addr);
    // 输出：load addr is 0x200000!
```

##### 步骤2：打开文件

```c
    // 打开输入文件（只读二进制模式）
    fi = fopen("./out/u-boot/u-boot.bin", "rb");

    // 创建输出文件（写入二进制模式）
    fo = fopen("uboot.img", "wb");
```

##### 步骤3：获取文件大小

```c
    fseek(fi, 0, SEEK_END);  // 移动到文件末尾
    size = ftell(fi);        // 获取文件大小（假设 520KB = 532480 字节）
    fseek(fi, 0, SEEK_SET);  // 回到文件开头

    printf("pack file size: %d(%d KB)\n", size, size / 1024);
    // 输出：pack file size: 532480(520 KB)

    // 检查大小是否超过限制（需要留出头部的 2KB 空间）
    if (size > max_size - sizeof(second_loader_hdr)) {
        // 520KB < (1MB - 2KB)，检查通过
        perror(file_out);
        exit(EXIT_FAILURE);
    }
```

##### 步骤4：构造 Rockchip 头部

```c
    // 初始化头部结构（全部清零）
    memset(&hdr, 0, sizeof(second_loader_hdr));  // 2048 字节

    // 设置魔数（8字节）
    memcpy((char *)hdr.magic, "LOADER  ", 8);

    // 设置版本号（未指定 --version 参数，默认为 0）
    hdr.version = 0;

    // 设置加载地址
    hdr.loader_load_addr = 0x00200000;
```

##### 步骤5：读取原始数据

```c
    // 将 u-boot.bin 读入缓冲区
    // 注意：从 buf[2048] 开始写入，前 2048 字节留给头部
    fread(buf + sizeof(second_loader_hdr), size, 1, fi);

    // 此时内存布局：
    // buf[0-2047]:        空（预留给头部）
    // buf[2048-534527]:   u-boot.bin 的内容（520KB）
    // buf[534528-1MB-1]:  空（零填充）
```

##### 步骤6：对齐大小

```c
    // 将大小对齐到 4 字节（Rockchip 硬件加密引擎的要求）
    // 公式：((size + 3) >> 2) << 2
    // 520KB 已经是 4 字节对齐，无需调整
    size = (((size + 3) >> 2) << 2);
    hdr.loader_load_size = size;  // 记录到头部
```

##### 步骤7：计算 CRC32 校验码

```c
    // 对实际数据计算 CRC32（不包括头部）
    hdr.crc32 = crc32_rk(0, buf + 2048, size);
    // 假设计算结果：0x12345678

    printf("crc = 0x%08x\n", hdr.crc32);
    // 输出：crc = 0x12345678
```

##### 步骤8：计算 SHA256 哈希

```c
    sha256_context ctx;
    uint8_t hash[32];

    hdr.hash_len = 32;  // SHA256 固定输出 32 字节

    sha256_starts(&ctx);  // 初始化 SHA256 上下文

    // 依次将以下数据加入哈希计算：
    // 1. u-boot.bin 的 520KB 数据
    sha256_update(&ctx, buf + 2048, size);

    // 2. 版本号（如果版本号大于 0）
    // 由于 hdr.version = 0，此步骤跳过
    // if (hdr.version > 0)
    //     sha256_update(&ctx, &hdr.version, 8);

    // 3. 加载地址（4字节）
    sha256_update(&ctx, &hdr.loader_load_addr, 4);

    // 4. 数据大小（4字节）
    sha256_update(&ctx, &hdr.loader_load_size, 4);

    // 5. 哈希长度（4字节）
    sha256_update(&ctx, &hdr.hash_len, 4);

    // 完成计算，得到 32 字节哈希值
    sha256_finish(&ctx, hash);

    // 复制到头部结构
    memcpy(hdr.hash, hash, 32);

    // 假设哈希值：
    // hdr.hash = [0xAB, 0xCD, 0xEF, ..., 0x12]（32字节）
```

**SHA256 哈希的数据来源**：

```
┌────────────────────────────────────────┐
│ SHA256 输入数据                        │
├────────────────────────────────────────┤
│ 1. u-boot.bin 实际数据 (520KB)         │
│ 2. 加载地址 (4B):    0x00200000        │
│ 3. 数据大小 (4B):    532480            │
│ 4. 哈希长度 (4B):    32                │
└────────────────────────────────────────┘
         ↓ SHA256 算法
┌────────────────────────────────────────┐
│ 输出：32 字节哈希值                    │
│ 存入 hdr.hash                          │
└────────────────────────────────────────┘
```

##### 步骤9：写入输出文件

```c
    // 显示版本信息
    printf("uboot version: %s\n", version);
    // 输出：uboot version: U-Boot 2017.09 (Jan 15 2021 - 10:30:00)

    // 将头部复制到缓冲区开头
    memcpy(buf, &hdr, sizeof(second_loader_hdr));

    // 此时完整的内存布局：
    // buf[0-2047]:        Rockchip 头部（包含所有元信息）
    // buf[2048-534527]:   u-boot.bin 的内容
    // buf[534528-1MB-1]:  零填充（使整个副本达到 1MB）

    // 写入 4 个副本到输出文件
    for (i = 0; i < 4; i++) {
        fwrite(buf, max_size, 1, fo);  // 每次写 1MB
    }
    // uboot.img 总大小：1MB × 4 = 4MB

    printf("pack %s success! \n", file_out);
    // 输出：pack uboot.img success!

    fclose(fi);  // 关闭输入文件
    fclose(fo);  // 关闭输出文件
}
```

---

## 五、最终生成的 uboot.img 文件结构

### 5.1 完整布局（4MB）

```
文件偏移        大小         内容说明
─────────────────────────────────────────────────────────
0x00000000      1MB          副本1
  ├─ 0x000000   2048字节     Rockchip 头部
  │  ├─ [0-7]   magic       "LOADER  "
  │  ├─ [8-11]  version     0x00000000
  │  ├─ [16-19] load_addr   0x00200000
  │  ├─ [20-23] load_size   0x00082000 (532480)
  │  ├─ [24-27] crc32       0x12345678
  │  ├─ [28-31] hash_len    0x00000020 (32)
  │  └─ [32-63] hash        [32字节 SHA256 值]
  │
  ├─ 0x000800   520KB        u-boot.bin 实际数据
  │                          (从编译生成的二进制代码)
  │
  └─ 0x082800   ~500KB       零填充（补齐到 1MB）

0x00100000      1MB          副本2（与副本1完全相同）

0x00200000      1MB          副本3（与副本1完全相同）

0x00300000      1MB          副本4（与副本1完全相同）

─────────────────────────────────────────────────────────
总大小：4MB (4194304 字节)
```

### 5.2 头部详细结构（副本1的前2048字节）

```
偏移         字段名                值示例            说明
───────────────────────────────────────────────────────────────
0x0000       magic[8]             "LOADER  "        魔数（2个空格）
0x0008       version              0x00000000        版本号（未指定）
0x000C       reserved0            0x00000000        保留字段
0x0010       loader_load_addr     0x00200000        DRAM 加载地址
0x0014       loader_load_size     0x00082000        实际数据大小 (532480)
0x0018       crc32                0x12345678        CRC32 校验码
0x001C       hash_len             0x00000020        哈希长度（32字节）
0x0020       hash[32]             [SHA256值]        安全启动哈希
0x0040       reserved[960]        0x00...           填充区域
0x0400       signTag              0x00000000        RSA 签名标记
0x0404       signlen              0x00000000        RSA 签名长度
0x0408       rsaHash[256]         0x00...           RSA 签名数据
0x0508       reserved2[760]       0x00...           尾部填充
───────────────────────────────────────────────────────────────
总计：2048 字节（0x800）
```

### 5.3 使用 hexdump 验证

```bash
# 查看头部（前 512 字节）
hexdump -C uboot.img | head -32

# 预期输出（示例）：
00000000  4c 4f 41 44 45 52 20 20  00 00 00 00 00 00 00 00  |LOADER  ........|
00000010  00 00 20 00 00 20 08 00  78 56 34 12 20 00 00 00  |.. .. ..xV4. ...|
00000020  ab cd ef ... (SHA256 哈希值)
```

---

## 六、为什么需要多副本？

### 6.1 Flash 坏块问题

NAND Flash 存在固有缺陷：

| 问题类型      | 发生概率       | 影响                      |
|--------------|---------------|---------------------------|
| 出厂坏块      | ~1-2%         | 某些块无法写入             |
| 运行时坏块    | 增长性        | 使用过程中逐渐增加         |
| 读写错误      | 偶发          | 数据位翻转                |

### 6.2 BootROM 扫描策略

Rockchip BootROM 的扫描机制：

```
Flash 布局（uboot 分区，起始于 sector 64）：
┌─────────────┬─────────────┬─────────────┬─────────────┐
│ 副本1 (1MB) │ 副本2 (1MB) │ 副本3 (1MB) │ 副本4 (1MB) │
│  Sector 64  │  Sector 576 │ Sector 1088 │ Sector 1600 │
└─────────────┴─────────────┴─────────────┴─────────────┘
     ↓             ↓             ↓             ↓
  扫描点1        扫描点2        扫描点3        扫描点4
```

**扫描过程**：

1. BootROM 从 sector 64 开始，每隔 512KB 扫描一次
2. 检查每个扫描点的前 8 字节是否为 "LOADER  " 魔数
3. 如果副本1损坏（坏块或 CRC 校验失败），继续扫描副本2
4. 依此类推，直到找到一个有效副本

### 6.3 冗余设计的好处

```
场景1：副本1 的 Flash 出现坏块
┌─────────────┬─────────────┬─────────────┬─────────────┐
│ 副本1 (✗)   │ 副本2 (✓)   │ 副本3 (✓)   │ 副本4 (✓)   │
└─────────────┴─────────────┴─────────────┴─────────────┘
       ↓             ↓
   跳过损坏       从这里启动 ✓

场景2：前 3 个副本都损坏（极端情况）
┌─────────────┬─────────────┬─────────────┬─────────────┐
│ 副本1 (✗)   │ 副本2 (✗)   │ 副本3 (✗)   │ 副本4 (✓)   │
└─────────────┴─────────────┴─────────────┴─────────────┘
                                              ↓
                                    最后的救命稻草 ✓
```

**可靠性提升**：

- 1个副本：可靠性 = 99%
- 4个副本：可靠性 = 1 - (0.01)^4 ≈ 99.9999%

---

## 七、安全启动流程（Secure Boot）

### 7.1 安全启动的必要性

**威胁场景**：

1. **固件篡改**：攻击者修改 U-Boot 代码植入后门
2. **降级攻击**：刷入存在漏洞的旧版本固件
3. **克隆设备**：提取固件用于假冒设备

### 7.2 Rockchip 安全启动机制

```
┌─────────────────────────────────────────────────────────┐
│ 开发阶段（固件打包）                                     │
├─────────────────────────────────────────────────────────┤
│ 1. 计算镜像的 SHA256 哈希值                              │
│    ↓                                                     │
│ 2. 用厂商的 RSA 私钥对哈希值签名                         │
│    ↓                                                     │
│ 3. 将签名数据存入 hdr.rsaHash                            │
│    ↓                                                     │
│ 4. 设置 hdr.signTag = 0x4E474953 ("SIGN")               │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ 启动阶段（BootROM 验证）                                 │
├─────────────────────────────────────────────────────────┤
│ 1. BootROM 读取镜像头部                                  │
│    ↓                                                     │
│ 2. 检查 signTag 是否为 0x4E474953                        │
│    ↓                                                     │
│ 3. 用 OTP 中的 RSA 公钥验证 rsaHash 签名                 │
│    ↓                                                     │
│ 4. 重新计算镜像的 SHA256，与头部的 hash 字段比对         │
│    ↓                                                     │
│ 5. 检查 version 字段 >= OTP 中的最低版本号（防降级）     │
│    ↓                                                     │
│ 6. 所有验证通过 → 加载代码到 DRAM 并执行                 │
│    验证失败 → 拒绝启动，设备变砖                         │
└─────────────────────────────────────────────────────────┘
```

### 7.3 Rollback 保护（防降级）

```
OTP（一次性可编程存储）中的最低版本号：3

尝试刷入的固件：
┌────────────────────────────────────┐
│ 固件A：version = 2                 │  ← BootROM 拒绝（版本过低）
├────────────────────────────────────┤
│ 固件B：version = 3                 │  ← 允许启动
├────────────────────────────────────┤
│ 固件C：version = 5                 │  ← 允许启动
└────────────────────────────────────┘

# 使用 --version 参数设置版本号
loaderimage --pack --uboot u-boot.bin uboot.img 0x200000 --version 5
```

---

## 八、常见问题与调试

### 8.1 错误：File size too large

**错误信息**：

```bash
pack file size: 1048576(1024 KB)
uboot.img: Success
```

**原因**：u-boot.bin 大小超过 `max_size - 2048` 字节

**解决方案**：

1. 增加 `--size` 参数的第一个值（单个副本大小）
2. 优化 U-Boot 配置，减少编译后的大小
3. 检查是否误包含了调试符号（使用 `strip` 命令）

### 8.2 错误：Size not aligned to 64KB

**错误信息**：

```bash
Usage: loaderimage ...
```

**原因**：`--size` 参数的第一个值不是 64 的倍数

**解决方案**：

```bash
# 错误示例
loaderimage --pack --uboot u-boot.bin uboot.img 0x200000 --size 900 4

# 正确示例（960 = 64 × 15）
loaderimage --pack --uboot u-boot.bin uboot.img 0x200000 --size 960 4
```

### 8.3 调试技巧

#### 验证镜像头部

```bash
# 使用 hexdump 查看魔数
hexdump -C uboot.img -n 16
# 预期输出：4c 4f 41 44 45 52 20 20 ("LOADER  ")

# 查看加载地址（偏移 0x10，小端序）
hexdump -C uboot.img -s 0x10 -n 4
# 0x00200000 的小端表示：00 00 20 00
```

#### 提取并对比原始文件

```bash
# 解包镜像
loaderimage --unpack --uboot uboot.img u-boot-extracted.bin

# 对比原始文件
diff u-boot.bin u-boot-extracted.bin
# 应该无差异（如果有差异，说明打包过程出错）
```

#### 验证 CRC32

```bash
# 安装 crc32 工具
apt-get install libarchive-zip-perl

# 提取数据部分（跳过 2KB 头部）
dd if=uboot.img of=data.bin bs=1 skip=2048 count=532480

# 计算 CRC32
crc32 data.bin
# 应与头部的 crc32 字段匹配
```

---

## 九、工具扩展应用

### 9.1 修改现有镜像的加载地址

```bash
# 步骤1：解包
loaderimage --unpack --uboot uboot.img u-boot.bin

# 步骤2：用新地址重新打包
loaderimage --pack --uboot u-boot.bin uboot-new.img 0x00400000
# 将加载地址改为 4MB 位置
```

### 9.2 制作紧凑型镜像（节省 Flash 空间）

```bash
# 标准镜像：4MB（4个1MB副本）
loaderimage --pack --uboot u-boot.bin uboot-standard.img 0x200000 --size 1024 4

# 紧凑型：1MB（2个512KB副本）
loaderimage --pack --uboot u-boot.bin uboot-compact.img 0x200000 --size 512 2
```

**注意**：减少副本数会降低可靠性，仅用于 Flash 空间极度受限的场景。

### 9.3 查看镜像信息（脚本化）

```bash
#!/bin/bash
# check_image.sh - 批量检查镜像信息

for img in *.img; do
    echo "=== $img ==="
    loaderimage --info $img
    echo ""
done
```

---

## 十、总结

### 10.1 核心要点

1. **loaderimage 的本质**：为二进制文件添加 BootROM 能识别的头部信息
2. **头部的关键作用**：
   - 魔数（识别）
   - 加载地址（定位）
   - CRC32（完整性）
   - SHA256（安全性）
3. **多副本设计**：通过冗余提高可靠性，应对 Flash 坏块
4. **安全启动**：通过 RSA 签名和版本号防篡改、防降级

### 10.2 工作流程总结

```
原始二进制（u-boot.bin）
    ↓
[loaderimage --pack]
    ├─ 添加 2048 字节头部
    ├─ 计算 CRC32 和 SHA256
    ├─ 填充到固定大小
    └─ 写入多个副本
    ↓
最终镜像（uboot.img）
    ↓
[烧写到 Flash]
    ↓
BootROM 扫描并加载
    ├─ 检查魔数
    ├─ 验证 CRC32
    ├─ 验证 SHA256（如果启用安全启动）
    └─ 加载到 DRAM 执行
```

### 10.3 相关工具链

| 工具              | 功能                              | 源码路径                     |
|-------------------|----------------------------------|------------------------------|
| loaderimage       | 打包 uboot.img / trust.img       | tools/rockchip/loaderimage.c |
| boot_merger       | 合并 DDR init + miniloader       | tools/rockchip/boot_merger.c |
| trust_merger      | 合并 BL31 + BL32                 | tools/rockchip/trust_merger.c|
| resource_tool     | 打包 logo 和 dtb                 | tools/rockchip/resource_tool.c|

### 10.4 进一步学习

- **Rockchip BootROM 启动流程**：了解芯片上电后的完整启动序列
- **U-Boot SPL**：理解 Secondary Program Loader 的作用
- **ARM Trusted Firmware**：研究 BL31 和安全世界的概念
- **设备树（DTB）**：学习硬件描述的标准格式

---

## 附录A：命令行参数速查表

```
loaderimage [模式] [类型] <input> <output> [地址] [选项]

模式：
  --pack          打包模式（bin → img）
  --unpack        解包模式（img → bin）
  --info          查看镜像信息

类型：
  --uboot         U-Boot 镜像
  --trustos       Trust OS 镜像

选项：
  --size <KB> <num>    自定义单个副本大小和副本数量
  --version <ver>      设置版本号（用于 Rollback 保护）
  --prepath <path>     输入文件路径前缀

示例：
  # 打包（使用默认配置）
  loaderimage --pack --uboot u-boot.bin uboot.img

  # 打包（指定加载地址和大小）
  loaderimage --pack --uboot u-boot.bin uboot.img 0x200000 --size 1024 4

  # 打包（设置版本号）
  loaderimage --pack --uboot u-boot.bin uboot.img 0x200000 --version 5

  # 解包
  loaderimage --unpack --uboot uboot.img u-boot.bin

  # 查看信息
  loaderimage --info uboot.img
```

---

## 附录B：相关文档索引

| 文档名称                        | 路径                                  | 内容简介                   |
|--------------------------------|---------------------------------------|---------------------------|
| 固件打包原理深度解析            | uboot/固件打包原理深度解析.md         | Rockchip 打包理论基础     |
| loader 镜像打包教程             | uboot/docs/loader镜像打包教程.md      | loader.bin 生成流程       |
| trust 镜像打包教程              | uboot/docs/trust镜像打包教程.md       | trust.img 生成流程        |
| uboot 镜像打包教程              | uboot/docs/uboot镜像打包教程.md       | uboot.img 生成流程        |
| RK3399 BL 组件深度解析          | uboot/docs/RK3399_BL组件深度解析.md   | BL31/BL32 架构分析        |

---

**文档版本**：v1.0
**最后更新**：2025-12-23
**维护者**：项目文档团队
**反馈**：如有疑问或建议，请提交 Issue
