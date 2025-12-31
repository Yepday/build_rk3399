# trust_merger.c 工具详解

## 概述

`trust_merger.c` 是 Rockchip 固件打包工具链中的核心工具，负责将 ARM Trusted Firmware (BL31)、OP-TEE (BL32) 等安全组件合并成 `trust.img` 固件文件。该文件在 Rockchip 启动流程中扮演关键角色，承载着系统的信任链根基。

### 典型调用示例

在 `uboot/make.sh:1079` 中的实际调用：

```bash
${RKTOOLS}/trust_merger ${PLATFORM_SHA} ${PLATFORM_RSA} ${PLATFORM_TRUST_IMG_SIZE} ${BIN_PATH_FIXUP} \
                ${PACK_IGNORE_BL32} ${ini}
```

展开后的完整命令示例（RK3399 平台）：

```bash
trust_merger --sha 3 --rsa 2 --size 2048 2 --replace tools/rk_tools/ ./ RK3399TRUST.ini
```

参数说明：
- `--sha 3`：使用 SHA-256 小端模式
- `--rsa 2`：使用 RSA-2048 标准模式
- `--size 2048 2`：每个镜像 2048KB，生成 2 个备份副本
- `--replace tools/rk_tools/ ./`：路径替换（将配置中的旧路径替换为新路径）
- `RK3399TRUST.ini`：Trust 配置文件

---

## 一、文件整体结构

### 1.1 代码组织架构

trust_merger.c 采用经典的 C 语言模块化设计，总计约 1200 行代码，主要分为以下模块：

```
trust_merger.c
├── 头文件与宏定义 (1-72)
│   ├── 加密算法常量 (SHA/RSA 模式定义)
│   ├── 全局变量声明
│   └── 组件标识符定义
│
├── 配置解析模块 (73-461)
│   ├── parseVersion()      # 版本信息解析
│   ├── parseBL3x()          # BL30/31/32/33 组件解析
│   ├── parseOut()           # 输出路径解析
│   ├── initOpts()           # 配置初始化
│   └── fixPath()            # 路径格式修正
│
├── ELF 文件处理模块 (462-620)
│   ├── filter_elf()         # ELF 段过滤与提取
│   └── getFileSize()        # 文件大小获取
│
├── Trust 镜像合并模块 (621-907)
│   ├── bl3xHash256()        # SHA256 哈希计算
│   ├── mergetrust()         # 核心合并逻辑
│   └── fill_file()          # 填充空白数据
│
├── Trust 镜像解包模块 (908-1027)
│   ├── unpacktrust()        # 解包功能
│   └── saveDatatoFile()     # 保存组件数据
│
└── 主程序与命令行处理 (1028-1186)
    ├── main()               # 程序入口
    ├── printHelp()          # 帮助信息
    └── 参数解析逻辑
```

### 1.2 关键数据结构

**配置选项结构体** (`OPT_T`)：
```c
typedef struct {
    uint16_t major;              // 主版本号
    uint16_t minor;              // 次版本号
    bl_entry_t bl3x[BL_MAX_SEC]; // BL30/31/32/33 组件信息数组
    char outPath[MAX_LINE_LEN];  // 输出文件路径
} OPT_T;
```

**组件入口结构体** (`bl_entry_t`)：
```c
typedef struct {
    uint32_t id;              // 组件标识 ('BL30', 'BL31', 'BL32', 'BL33')
    char path[MAX_LINE_LEN];  // 二进制文件路径
    uint32_t addr;            // 加载地址（运行时地址）
    uint32_t size;            // 文件实际大小
    uint32_t offset;          // ELF 文件内偏移（用于段提取）
    uint32_t align_size;      // 对齐后的大小（512 字节对齐）
} bl_entry_t;
```

**Trust Header 结构** (`TRUST_HEADER`)：
```c
typedef struct {
    uint8_t tag[4];           // 魔数: "TRUS" (0x54525553)
    uint32_t size;            // 高 16 位：组件数量，低 16 位：签名偏移/4
    uint32_t version;         // BCD 编码的版本号
    uint32_t flags;           // 低 4 位：SHA 模式，高 4 位：RSA 模式
    uint8_t reserved[496];    // 保留字节
    uint8_t rsa_n[256];       // RSA 公钥模数
    uint8_t rsa_e[256];       // RSA 公钥指数
    uint8_t rsa_c[256];       // RSA 签名密文
} TRUST_HEADER;               // 总大小：2048 字节
```

**组件数据结构** (`COMPONENT_DATA`)：
```c
typedef struct {
    uint32_t LoadAddr;        // 组件加载地址
    uint8_t reserved[4];      // 保留字节
    uint32_t HashData[8];     // SHA256 哈希值（32 字节）
} COMPONENT_DATA;             // 总大小：40 字节
```

**组件信息结构** (`TRUST_COMPONENT`)：
```c
typedef struct {
    uint32_t ComponentID;     // 组件标识 ('BL30'/'BL31'/'BL32'/'BL33')
    uint32_t StorageAddr;     // 存储地址（以 512 字节为单位）
    uint32_t ImageSize;       // 镜像大小（以 512 字节为单位）
} TRUST_COMPONENT;            // 总大小：12 字节
```

---

## 二、设计思路与架构理念

### 2.1 核心设计理念

#### 1. 安全启动链的数据承载者

Trust 镜像是 Rockchip 安全启动流程中的关键环节，trust_merger 的设计围绕以下安全目标：

- **完整性保护**：通过 SHA256 哈希确保每个组件未被篡改
- **签名验证准备**：预留 RSA 签名区域（需外部工具签名）
- **多备份机制**：默认生成 2 个副本（通过 `g_trust_max_num` 控制），提高容错性
- **灵活的加密配置**：支持 SHA160/SHA256、RSA1024/RSA2048/RSA-PSS 等多种模式

#### 2. 跨平台兼容性设计

通过全局配置变量适配不同 SoC：

```c
static uint8_t gRSAmode = RSA_SEL_2048;  // RK3399/RK3328 使用 RSA-2048
static uint8_t gSHAmode = SHA_SEL_256;   // 大多数平台使用 SHA-256 小端
static uint32_t g_trust_max_size = 2MB;  // 默认镜像大小 2MB
```

特殊平台适配示例：
- **RK3368**：使用 `SHA_SEL_256_RK`（大端模式）
- **RK3326/PX30/RK3308**：使用 `RSA_SEL_2048_PSS`（PKCS#1 V2.1 PSS 签名）

#### 3. ELF 文件智能处理

ARM Trusted Firmware 和 OP-TEE 通常编译为 ELF 格式，包含多个 PT_LOAD 段。trust_merger 能够：

- 自动识别 ELF 文件（魔数检测：`0x7F 'E' 'L' 'F'`）
- 提取所有可加载段（PT_LOAD 类型）
- 处理 32 位和 64 位 ELF 文件
- 读取段的虚拟地址、文件偏移和大小

这避免了手动使用 `objcopy` 转换的繁琐流程。

#### 4. 灵活的路径管理

支持三种路径配置方式：

1. **路径替换** (`--replace`/`--repalce`)：
   ```bash
   --replace tools/rk_tools/ ./
   # 将配置文件中的 "tools/rk_tools/bin/rk33/xxx" 替换为 "./bin/rk33/xxx"
   ```

2. **前缀添加** (`--prepath`)：
   ```bash
   --prepath ../external/rkbin/
   # 为所有相对路径添加前缀
   ```

3. **自动格式化**：反斜杠转正斜杠，移除回车换行符

### 2.2 Trust 镜像布局设计

生成的 trust.img 文件结构：

```
+---------------------------------------------+-----------------------------------------------+
| 文件分区 (物理布局)                          | 对应的 C 结构体                                |
+---------------------------------------------+-----------------------------------------------+
| Trust Header (2048 字节)                     | TRUST_HEADER (trust_merger.h:102-111)         |
|   - Tag: "TRUS"                             | - uint32_t tag (魔数 0x54525553)              |
|   - Flags: [RSA mode][SHA mode]             | - uint32_t flags (低4位SHA, 高4位RSA)         |
|   - Size: [组件数量][签名偏移]               | - uint32_t size (高16位组件数, 低16位偏移/4)   |
+---------------------------------------------+-----------------------------------------------+
| Component Data 区域                          | COMPONENT_DATA (trust_merger.h:114-118)       |
|   - Component 0:                            | - uint32_t HashData[8] (SHA256哈希32字节)     |
|       LoadAddr: 0x00010000  (BL31加载地址)   | - uint32_t LoadAddr (组件运行地址)            |
|       HashData: [32 bytes SHA256]           | - uint32_t reserved[3]                        |
|   - Component 1:                            | 大小: 40 字节/组件                             |
|       LoadAddr: 0x08400000  (BL32加载地址)   | 位置: gBuf + sizeof(TRUST_HEADER) = 800字节处 |
|       HashData: [32 bytes SHA256]           |                                               |
+---------------------------------------------+-----------------------------------------------+
| RSA 签名区 (768 字节)                        | TRUST_HEADER 的字段:                           |
|   - RSA-N (256 bytes)                       | - uint32_t RSA_N[64] (公钥模数)               |
|   - RSA-E (256 bytes)                       | - uint32_t RSA_E[64] (公钥指数)               |
|   - RSA-C (256 bytes)                       | - uint32_t RSA_C[64] (签名密文)               |
+---------------------------------------------+-----------------------------------------------+
| Component 信息区                             | TRUST_COMPONENT (trust_merger.h:121-126)      |
|   - Component 0:                            | - uint32_t ComponentID ('BL31'/'BL32')        |
|       ComponentID: 'BL31'                   | - uint32_t StorageAddr (扇区号, 512B/扇区)    |
|       StorageAddr: 4 (扇区号)                | - uint32_t ImageSize (扇区数)                 |
|       ImageSize: 256 (扇区数)                | - uint32_t reserved                           |
|   - Component 1:                            | 大小: 16 字节/组件                             |
|       ComponentID: 'BL32'                   | 位置: gBuf + SignOffset + 256                 |
|       StorageAddr: 260                      | SignOffset = 800 + n*40 (n为组件数)           |
|       ImageSize: 512                        |                                               |
+---------------------------------------------+-----------------------------------------------+
| BL31 二进制数据 (对齐到 512B)                 | (无特定结构体, 原始二进制数据)                  |
|   [实际固件代码与数据]                        | 从 2048 字节偏移开始                           |
+---------------------------------------------+-----------------------------------------------+
| BL32 二进制数据 (对齐到 512B)                 | (无特定结构体, 原始二进制数据)                  |
|   [OP-TEE 固件代码与数据]                     | 512 字节对齐                                   |
+---------------------------------------------+-----------------------------------------------+
| 填充至 2MB 边界                              | 0x00 填充至 g_trust_max_size (默认2MB)         |
+---------------------------------------------+-----------------------------------------------+
| 备份副本 1 (完整复制)                         | 完整的 2MB 副本 (g_trust_max_num - 1 个)       |
+---------------------------------------------+-----------------------------------------------+
```

**关键设计点**：

1. **扇区单位**：StorageAddr 和 ImageSize 以 512 字节为单位（`<< 9` 位移操作）
2. **固定头部大小**：2048 字节头部保证了签名区域的固定偏移
3. **签名偏移计算**：`SignOffset = sizeof(TRUST_HEADER) + n * sizeof(COMPONENT_DATA)`
4. **数据完整性**：每个组件的哈希值存储在 Component Data 区，便于 BootROM 快速验证

---

## 三、详细处理流程

### 3.1 主流程时序图

```
main()
  │
  ├─ 1. 解析命令行参数
  │    ├─ --sha/--rsa → 设置 gSHAmode/gRSAmode
  │    ├─ --size → 设置 g_trust_max_size/g_trust_max_num
  │    ├─ --replace/--prepath → 配置路径处理
  │    └─ --ignore-bl32 → 设置 gIgnoreBL32
  │
  ├─ 2. 判断操作模式
  │    ├─ merge=true → 执行 mergetrust()
  │    └─ merge=false → 执行 unpacktrust()
  │
  └─ mergetrust()  ← 核心合并逻辑
       │
       ├─ 3. initOpts()
       │    ├─ 设置默认值（版本号、路径、组件 ID）
       │    └─ parseOpts() → 解析 .ini 配置文件
       │         ├─ parseVersion()  # [VERSION] 段
       │         ├─ parseBL3x() x4  # [BL30/31/32/33_OPTION]
       │         └─ parseOut()       # [OUTPUT] 段
       │
       ├─ 4. ELF 文件处理 (第一阶段)
       │    └─ for (BL30 → BL33):
       │         └─ filter_elf()
       │              ├─ 检测 ELF 魔数 (0x7F454C46)
       │              ├─ 区分 32 位/64 位 (file_buffer[4])
       │              ├─ 遍历程序头 (e_phnum)
       │              ├─ 提取 PT_LOAD 段 (p_type == 1)
       │              └─ 填充 bl_entry_t：
       │                   - addr = p_vaddr (虚拟地址)
       │                   - offset = p_offset (文件偏移)
       │                   - size = p_filesz (段大小)
       │                   - align_size = DO_ALIGN(size, 512)
       │
       ├─ 5. 构建 Trust Header (第二阶段)
       │    ├─ 设置魔数：memcpy(&pHead->tag, "TRUS", 4)
       │    ├─ 版本号转 BCD：pHead->version = (getBCD(major) << 8) | getBCD(minor)
       │    ├─ 加密模式：pHead->flags = (gRSAmode << 4) | gSHAmode
       │    ├─ 计算签名偏移：SignOffset = sizeof(TRUST_HEADER) + n * sizeof(COMPONENT_DATA)
       │    └─ 编码组件数量：pHead->size = (nComponentNum << 16) | (SignOffset >> 2)
       │
       ├─ 6. 填充组件信息 (第三阶段)
       │    └─ for (i = 0; i < nComponentNum; i++):
       │         ├─ pComponentData->LoadAddr = pEntry->addr
       │         ├─ pComponent->ComponentID = pEntry->id ('BL31'/'BL32')
       │         ├─ pComponent->StorageAddr = OutFileSize >> 9  # 转换为扇区号
       │         ├─ pComponent->ImageSize = align_size >> 9
       │         └─ OutFileSize += align_size
       │
       ├─ 7. 读取并哈希组件数据 (第四阶段)
       │    └─ for (i = 0; i < nComponentNum; i++):
       │         ├─ 打开组件二进制文件
       │         ├─ fseek(file, pEntry->offset, SEEK_SET)  # 定位到 ELF 段
       │         ├─ fread(gBuf, pEntry->size, 1, inFile)
       │         ├─ bl3xHash256() → 计算 SHA256
       │         │    ├─ 分块处理（每次 256KB）
       │         │    └─ 根据 gSHAmode 选择算法：
       │         │         ├─ SHA_SEL_256_RK → sha256_hash() (大端)
       │         │         └─ SHA_SEL_256 → sha256_update() (小端)
       │         ├─ 保存哈希到 pComponentData->HashData[8]
       │         └─ 复制数据到输出缓冲区
       │
       ├─ 8. 生成备份副本
       │    └─ for (n = 1; n < g_trust_max_num; n++):
       │         └─ memcpy(outBuf + n * g_trust_max_size, outBuf, g_trust_max_size)
       │
       └─ 9. 写入文件
            └─ fwrite(outBuf, g_trust_max_size * g_trust_max_num, 1, outFile)
```

### 3.2 关键函数深入解析

#### 3.2.1 `filter_elf()` - ELF 段提取

**函数签名**：
```c
bool filter_elf(uint32_t index, uint8_t *pMeta, uint32_t *pMetaNum, bool *bElf)
```

**处理逻辑**：

```c
// 1. 读取整个文件到内存
file_buffer = malloc(file_size);
fread(file_buffer, 1, file_size, file);

// 2. 检测 ELF 魔数
if (*((uint32_t *)file_buffer) != ELF_MAGIC) {  // 0x464C457F
    *bElf = false;  // 普通二进制文件
    return true;
}

// 3. 判断 32 位还是 64 位 ELF
if (file_buffer[4] == 2) {  // 64 位
    pElfHeader64 = (Elf64_Ehdr *)file_buffer;

    // 4. 遍历程序头表
    for (i = 0; i < pElfHeader64->e_phnum; i++) {
        pElfProgram64 = (Elf64_Phdr *)(file_buffer +
                        pElfHeader64->e_phoff +        // 程序头表偏移
                        i * pElfHeader64->e_phentsize);// 单个程序头大小

        // 5. 仅处理 PT_LOAD 段（可加载段）
        if (pElfProgram64->p_type == 1) {
            pEntry->addr = pElfProgram64->p_vaddr;      // 虚拟地址
            pEntry->offset = pElfProgram64->p_offset;   // 文件内偏移
            pEntry->size = pElfProgram64->p_filesz;     // 段大小
            pEntry->align_size = DO_ALIGN(size, 512);   // 对齐到 512 字节
            (*pMetaNum)++;  // 增加段计数
        }
    }
}
```

**为什么需要 ELF 支持？**

ARM Trusted Firmware (BL31) 的 ELF 文件通常包含多个段：

```
$ readelf -l bl31.elf

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00010000 0x00010000 0x0a580 0x0a580 R E 0x1000  ← 代码段
  LOAD           0x00c000 0x0001b000 0x0001b000 0x00520 0x00c20 RW  0x1000  ← 数据段
```

trust_merger 会提取所有 PT_LOAD 段，保持它们的虚拟地址，确保运行时内存布局正确。

#### 3.2.2 `bl3xHash256()` - 分块哈希计算

**为什么要分块处理？**

组件文件可能较大（BL31 约 130KB，BL32 约 260KB），一次性加载到内存计算哈希可能导致：

1. 内存分配失败（嵌入式环境）
2. 缓存失效（大块内存操作）

**实现细节**：

```c
#define SHA256_CHECK_SZ (256 * 1024)  // 每次处理 256KB

static bool bl3xHash256(uint8_t *pHash, uint8_t *pData, uint32_t nDataSize)
{
    sha256_context ctx;
    uint32_t nHashSize, nHasHashSize = 0;

    sha256_starts(&ctx);  // 初始化 SHA256 上下文

    while (nDataSize > 0) {
        // 分块计算，避免内存压力
        nHashSize = (nDataSize >= SHA256_CHECK_SZ) ? SHA256_CHECK_SZ : nDataSize;
        sha256_update(&ctx, pData + nHasHashSize, nHashSize);
        nHasHashSize += nHashSize;
        nDataSize -= nHashSize;
    }

    sha256_finish(&ctx, pHash);  // 输出最终哈希值（32 字节）
    return true;
}
```

**RK3368 特殊处理**：

```c
if (gSHAmode == SHA_SEL_256_RK) {  // 大端模式
    sha256_ctx ctx;  // 不同的实现
    sha256_begin(&ctx);
    sha256_hash(&ctx, pData + nHasHashSize, nHashSize);
    sha256_end(&ctx, pHash);
}
```

RK3368 的 BootROM 使用大端字节序解析哈希值，需要特殊处理。

#### 3.2.3 `mergetrust()` - 核心合并逻辑

**第二阶段：Trust Header 构建**

```c
pHead = (TRUST_HEADER *)gBuf;

// 1. 设置魔数
memcpy(&pHead->tag, TRUST_HEAD_TAG, 4);  // "TRUS"

// 2. 版本号编码（BCD 格式）
// 例如：1.24 → 0x0124
pHead->version = (getBCD(gOpts.major) << 8) | getBCD(gOpts.minor);

// 3. 加密模式编码
// 低 4 位：SHA 模式 (0=None, 1=SHA160, 2=SHA256-BE, 3=SHA256-LE)
// 高 4 位：RSA 模式 (0=None, 1=RSA1024, 2=RSA2048, 3=RSA2048-PSS)
pHead->flags = (gRSAmode << 4) | gSHAmode;

// 4. 计算签名偏移
SignOffset = sizeof(TRUST_HEADER) + nComponentNum * sizeof(COMPONENT_DATA);
//           = 2048 + n * 40

// 5. 编码组件数量和签名偏移
// 高 16 位：组件数量（2 个组件 = 0x0002）
// 低 16 位：签名偏移/4（SignOffset = 2128 → 0x0214）
pHead->size = (nComponentNum << 16) | (SignOffset >> 2);
```

**BCD 编码原理**：

```c
static inline uint32_t getBCD(uint16_t value)
{
    // 将十进制数值转换为 BCD 码
    // 例如：24 → 0x24
    tmp[i] = (((value / 10) % 10) << 4) | (value % 10);
    // 24 → (2 << 4) | 4 = 0x24
}
```

**第三阶段：组件信息填充**

```c
OutFileSize = TRUST_HEADER_SIZE;  // 起始为 2048 字节

for (i = 0; i < nComponentNum; i++) {
    // 1. 填充组件数据区（加载地址 + 哈希）
    pComponentData->LoadAddr = pEntry->addr;  // 例如：0x00010000 (BL31)

    // 2. 填充组件信息区（ID + 存储位置 + 大小）
    pComponent->ComponentID = pEntry->id;  // 'BL31' = 0x424C3331

    // 3. 存储地址转换为扇区号（512 字节 = 1 扇区）
    // OutFileSize = 2048 → StorageAddr = 2048 >> 9 = 4
    pComponent->StorageAddr = (OutFileSize >> 9);

    // 4. 镜像大小转换为扇区数
    // align_size = 131072 → ImageSize = 131072 >> 9 = 256
    pComponent->ImageSize = (pEntry->align_size >> 9);

    OutFileSize += pEntry->align_size;
}
```

**为什么使用扇区号？**

BootROM 从 Flash 读取数据时以扇区（512 字节）为单位，使用扇区号可以：

1. 减少存储空间（16 位扇区号可表示 32MB 地址范围）
2. 提高解析效率（位移操作比乘除法快）
3. 保持与 Rockchip BootROM 的兼容性

### 3.3 配置文件解析详解

#### 3.3.1 .ini 文件格式

以 `RK3399TRUST.ini` 为例：

```ini
[VERSION]
MAJOR=1              # 主版本号
MINOR=0              # 次版本号

[BL30_OPTION]
SEC=0                # 0=禁用，1=启用

[BL31_OPTION]
SEC=1                # 启用 BL31 (ARM Trusted Firmware)
PATH=bin/rk33/rk3399_bl31_v1.28.elf  # ELF 文件路径
ADDR=0x00010000      # 加载地址（64KB 偏移）

[BL32_OPTION]
SEC=1                # 启用 BL32 (OP-TEE)
PATH=bin/rk33/rk3399_bl32_v1.18.bin  # 二进制文件路径
ADDR=0x08400000      # 加载地址（132MB 偏移）

[BL33_OPTION]
SEC=0                # U-Boot 不包含在 trust.img 中

[OUTPUT]
PATH=trust.img       # 输出文件路径
```

#### 3.3.2 `parseBL3x()` 解析逻辑

```c
static bool parseBL3x(FILE *file, int bl3x_id)
{
    bl_entry_t *pbl3x = &gOpts.bl3x[bl3x_id];

    // 1. 解析 SEC 字段
    fscanf(file, OPT_SEC "=%d", &sec);

    // 2. BL32 特殊处理
    if (gIgnoreBL32 && (bl3x_id == BL32_SEC)) {
        // 命令行 --ignore-bl32 强制禁用
        sec = 0;
    }
    pbl3x->sec = sec;

    // 3. 解析 PATH 字段
    fscanf(file, OPT_PATH "=%s", buf);
    fixPath(buf);  // 路径格式化
    strcpy(pbl3x->path, buf);

    // 4. 解析 ADDR 字段（16 进制）
    fscanf(file, OPT_ADDR "=%s", buf);
    pbl3x->addr = strtoul(buf, NULL, 16);  // 转换为无符号长整型

    return true;
}
```

#### 3.3.3 `fixPath()` 路径处理

**场景 1：路径替换** (`--replace tools/rk_tools/ ./`)

```c
if (gLegacyPath && gNewPath) {
    start = strstr(path, gLegacyPath);  // 查找 "tools/rk_tools/"
    if (start) {
        // 原路径：tools/rk_tools/bin/rk33/bl31.elf
        // 替换后：./bin/rk33/bl31.elf
        end = start + strlen(gLegacyPath);
        strcpy(tmp, end);         // 备份后半部分 "bin/rk33/bl31.elf"
        *start = '\0';            // 截断
        strcat(path, gNewPath);   // 拼接 "./"
        strcat(path, tmp);        // 拼接后半部分
    }
}
```

**场景 2：前缀添加** (`--prepath ../external/rkbin/`)

```c
else if (gPrePath && strncmp(path, gPrePath, strlen(gPrePath))) {
    // 原路径：bin/rk33/bl31.elf
    // 添加前缀后：../external/rkbin/bin/rk33/bl31.elf
    strcpy(tmp, path);
    strcpy(path, gPrePath);
    strcat(path, tmp);
}
```

**场景 3：格式化**

```c
for (i = 0; i < len; i++) {
    if (path[i] == '\\')
        path[i] = '/';        // Windows 路径转 Unix 格式
    else if (path[i] == '\r' || path[i] == '\n')
        path[i] = '\0';       // 移除行尾字符
}
```

---

## 四、平台适配与特殊处理

### 4.1 不同 SoC 的加密配置

| 平台                     | SHA 模式            | RSA 模式          | 镜像大小 |
|-------------------------|---------------------|-------------------|---------|
| RK3399/RK3328           | SHA-256 LE (3)      | RSA-2048 (2)      | 2MB     |
| RK3368                  | SHA-256 BE (2)      | RSA-2048 (2)      | 2MB     |
| RK3326/PX30/RK3308      | SHA-256 LE (3)      | RSA-2048-PSS (3)  | 1MB     |
| RK3288                  | SHA-160 (1)         | RSA-2048 (2)      | 2MB     |

**实际调用示例**：

```bash
# RK3399
trust_merger --sha 3 --rsa 2 --size 2048 2 RK3399TRUST.ini

# RK3308 (PSS 签名模式)
trust_merger --sha 3 --rsa 3 --size 1024 2 RK3308TRUST.ini

# RK3368 (大端哈希)
trust_merger --sha 2 --rsa 2 --size 2048 2 RK3368TRUST.ini
```

### 4.2 OP-TEE (BL32) 的可选处理

某些应用场景不需要 OP-TEE（如不使用 TEE 安全环境），可通过以下方式禁用：

**方法 1：命令行参数**

```bash
trust_merger --ignore-bl32 RK3399TRUST.ini
```

**方法 2：修改配置文件**

```ini
[BL32_OPTION]
SEC=0  # 改为 0
```

**代码实现**：

```c
if (gIgnoreBL32 && (bl3x_id == BL32_SEC)) {
    if (sec == 1) {
        sec = 0;
        printf("BL32 adjust sec from 1 to 0\n");
    }
}
```

### 4.3 多备份机制

**为什么需要多备份？**

1. **Flash 坏块容错**：NAND Flash 可能存在坏块，多个副本提高成功率
2. **损坏恢复**：运行时校验失败时可回退到备份副本
3. **OTA 升级**：保留旧版本作为备份，升级失败时回滚

**实现细节**：

```c
// 1. 设置备份数量（默认 2 个）
g_trust_max_num = 2;  // 可通过 --size 参数修改

// 2. 分配包含所有副本的缓冲区
outBuf = calloc(g_trust_max_size, g_trust_max_num);
// 例如：2MB * 2 = 4MB

// 3. 复制备份副本
for (n = 1; n < g_trust_max_num; n++) {
    memcpy(outBuf + g_trust_max_size * n, outBuf, g_trust_max_size);
}

// 4. 写入文件（包含所有副本）
fwrite(outBuf, g_trust_max_size * g_trust_max_num, 1, outFile);
```

**Flash 布局示例**：

```
+------------------+
| Trust 副本 0     |  <- 偏移 0x0000000 (2MB)
+------------------+
| Trust 副本 1     |  <- 偏移 0x0200000 (2MB)
+------------------+
```

BootROM 先尝试读取副本 0，校验失败则尝试副本 1。

---

## 五、实战示例与调试技巧

### 5.1 完整的打包流程

**步骤 1：准备文件**

```bash
# 目录结构
rkbin/
├── RKTRUST/
│   └── RK3399TRUST.ini
├── bin/rk33/
│   ├── rk3399_bl31_v1.28.elf  # ARM Trusted Firmware
│   └── rk3399_bl32_v1.18.bin  # OP-TEE
└── tools/
    └── trust_merger
```

**步骤 2：执行打包**

```bash
cd rkbin
./tools/trust_merger \
    --sha 3 \
    --rsa 2 \
    --size 2048 2 \
    --verbose \
    RKTRUST/RK3399TRUST.ini
```

**步骤 3：验证输出**

```bash
# 检查文件大小（应为 4MB = 2MB * 2 个副本）
ls -lh trust.img
# -rw-r--r-- 1 user user 4.0M Jan 10 15:30 trust.img

# 使用 hexdump 查看头部
hexdump -C trust.img | head -20
# 00000000  54 52 55 53 14 02 24 01  00 00 00 00 00 00 00 00  |TRUS...$.........|
#           ^^^^^^^^ "TRUS" 魔数
#                     ^^^^^ 版本 0x0124 (1.24)
#                           ^^^^^ Flags 0x00000024 (SHA=3, RSA=2)
```

### 5.2 使用 `--verbose` 调试

启用详细日志：

```bash
trust_merger --verbose RK3399TRUST.ini
```

**输出示例**：

```
D: [filter_elf] index=1,file=bin/rk33/rk3399_bl31_v1.28.elf
D: [filter_elf] bl31: filesize = 42368, imagesize = 42496, segment=0
D: [filter_elf] bl31: filesize = 1312, imagesize = 3584, segment=1
D: [mergetrust] bl3x bin sec = 3
D: [mergetrust] trust bin sign offset = 2128
D: [mergetrust] bl31: LoadAddr = 0x00010000, StorageAddr = 4, ImageSize = 90
D: [mergetrust] bl32: LoadAddr = 0x08400000, StorageAddr = 94, ImageSize = 520
merge success(trust.img)
```

**关键信息解读**：

- **segment=0/1**：ELF 文件有 2 个 PT_LOAD 段
- **imagesize > filesize**：对齐到 512 字节
- **StorageAddr = 4**：起始扇区号（4 * 512 = 2048 字节，即头部之后）
- **ImageSize = 90**：占用扇区数（90 * 512 = 46080 字节）

### 5.3 解包 trust.img

**使用场景**：

- 逆向工程已有固件
- 验证打包结果
- 提取特定组件版本

**命令**：

```bash
trust_merger --unpack trust.img
```

**输出文件**：

```bash
ls -lh
# -rw-r--r-- 1 user user  46K Jan 10 15:35 BL31  # 提取的 BL31 组件
# -rw-r--r-- 1 user user 260K Jan 10 15:35 BL32  # 提取的 BL32 组件
```

**解包日志**：

```
File Size = 4194304
Header Tag:TRUS
Header version:292
Header flag:36
SrcFileNum:2
SignOffset:2128
Component 0:
ComponentID:BL31
StorageAddr:0x4
ImageSize:0x5a
LoadAddr:0x10000
Component 1:
ComponentID:BL32
StorageAddr:0x5e
ImageSize:0x208
LoadAddr:0x8400000
unpack success
```

### 5.4 常见错误诊断

#### 错误 1：`filter_elf file failed`

**原因**：

- ELF 文件损坏
- 文件路径不正确
- 文件超过 512KB 限制

**解决方法**：

```bash
# 检查文件是否存在
ls -lh bin/rk33/rk3399_bl31_v1.28.elf

# 验证 ELF 格式
file bin/rk33/rk3399_bl31_v1.28.elf
# 应输出：ELF 64-bit LSB executable, ARM aarch64...

# 检查文件大小
du -h bin/rk33/rk3399_bl31_v1.28.elf
# 应小于 512KB
```

#### 错误 2：`trust bin size overfull`

**原因**：

所有组件的总大小超过 `g_trust_max_size`（默认 2MB）

**解决方法**：

```bash
# 增加镜像大小为 4MB
trust_merger --size 4096 2 RK3399TRUST.ini
```

#### 错误 3：`parse failed`

**原因**：

配置文件格式错误

**解决方法**：

```bash
# 检查配置文件格式
cat -A RKTRUST/RK3399TRUST.ini
# 注意：不能有多余的空格、制表符

# 使用默认配置测试
trust_merger --verbose  # 会自动创建 trust_config.ini 模板
```

### 5.5 进阶技巧：签名集成

trust_merger 仅生成未签名的镜像，实际生产环境需要 RSA 签名。

**签名流程**：

```bash
# 1. 生成 RSA 密钥对（2048 位）
openssl genrsa -out private.pem 2048
openssl rsa -in private.pem -pubout -out public.pem

# 2. 打包 trust.img（预留签名区）
trust_merger --rsa 2 --sha 3 RK3399TRUST.ini

# 3. 使用 Rockchip 签名工具签名
rk_sign_tool --sign trust.img private.pem

# 4. 验证签名
rk_sign_tool --verify trust.img public.pem
```

**注意**：`rk_sign_tool` 是 Rockchip 内部工具，需要申请授权。

---

## 六、总结与扩展

### 6.1 trust_merger 的核心价值

1. **自动化固件打包**：避免手动拼接二进制文件的繁琐流程
2. **ELF 智能处理**：直接支持编译输出的 ELF 文件，无需转换
3. **跨平台兼容性**：通过参数配置适配不同 SoC
4. **完整性保护**：自动计算 SHA256 哈希，为签名验证做准备
5. **容错机制**：多备份设计提高 Flash 读取成功率

### 6.2 与其他工具的协作

在完整的 Rockchip 固件构建流程中，trust_merger 的位置：

```
编译阶段：
  └─ ARM Trusted Firmware → bl31.elf
  └─ OP-TEE → bl32.bin

打包阶段：
  ├─ boot_merger → loader.bin (DDR init + miniloader)
  ├─ loaderimage → uboot.img (U-Boot)
  └─ trust_merger → trust.img (BL31 + BL32)  ← 本工具

签名阶段：
  └─ rk_sign_tool → 添加 RSA 签名

刷写阶段：
  └─ upgrade_tool → 烧录到设备
```

### 6.3 源码学习建议

对于想深入理解固件打包原理的开发者：

1. **阅读顺序**：
   - 先看 `main()` 理解参数解析
   - 再看 `mergetrust()` 理解主流程
   - 最后看 `filter_elf()` 理解 ELF 处理

2. **调试技巧**：
   - 使用 `--verbose` 观察日志
   - 用 `hexdump` 验证输出文件
   - 对比不同配置的输出差异

3. **扩展方向**：
   - 支持更多加密算法（如 SHA-512、ECC）
   - 集成签名功能（不依赖外部工具）
   - 添加镜像压缩（减小 Flash 占用）

### 6.4 参考资料

- ARM Trusted Firmware 文档：https://trustedfirmware-a.readthedocs.io/
- OP-TEE 官方文档：https://optee.readthedocs.io/
- Rockchip 启动流程：参考本仓库 `uboot/固件打包原理深度解析.md`
- ELF 文件格式：https://refspecs.linuxfoundation.org/elf/elf.pdf

---

**文档版本**：v1.0
**作者**：根据 trust_merger.c 源码分析编写
**适用平台**：RK3399 / RK3328 / RK3368 / RK3326 / RK3308 / PX30
