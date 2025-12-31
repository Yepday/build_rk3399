# ELF 文件与 Rockchip 固件格式深度对比教程

## 引言

在嵌入式 Linux 开发中，理解可执行文件格式至关重要。本教程将：

1. **深入讲解 ELF 文件格式**（编译器的标准输出）
2. **剖析 Rockchip 固件格式**（BootROM 的输入要求）
3. **对比三大打包工具**：`boot_merger`、`loaderimage`、`trust_merger`
4. **揭示 Rockchip 格式与 ELF 的相似性**（为什么它们看起来像 ELF？）

通过本教程，你将理解为什么 Rockchip 固件格式借鉴了 ELF 的设计思想，以及打包工具如何在两者之间架起桥梁。

---

## 第一部分：ELF 文件格式基础

### 1.1 什么是 ELF？

**ELF (Executable and Linkable Format)** 是类 Unix 系统的标准可执行文件格式，由 GCC/Clang 编译器直接生成。

**为什么需要 ELF？**

编译器编译源代码后，需要告诉操作系统：
- 代码段放在哪里？（`.text` section）
- 数据段放在哪里？（`.data` section）
- 入口点地址是什么？（`main()` 函数的地址）
- 需要多少内存？（代码 + 数据 + BSS）

ELF 文件就是这样一个**携带元数据的可执行文件容器**。

### 1.2 ELF 文件结构

ELF 文件分为三个主要部分：

```
+---------------------------------------+
| ELF Header (52/64 字节)                |  ← 文件元信息
|   - Magic: 0x7F 'E' 'L' 'F'           |
|   - Class: 32-bit / 64-bit            |
|   - Entry Point: 入口地址              |
|   - Program Header Table Offset       |
|   - Section Header Table Offset       |
+---------------------------------------+
| Program Header Table (程序头表)        |  ← 告诉加载器如何加载
|   - Segment 0: PT_LOAD (代码段)        |
|       Offset: 0x1000                  |
|       VirtAddr: 0x00010000            |
|       FileSiz: 0x0a580                |
|   - Segment 1: PT_LOAD (数据段)        |
|       Offset: 0xc000                  |
|       VirtAddr: 0x0001b000            |
|       FileSiz: 0x00520                |
+---------------------------------------+
| Section Header Table (节头表)          |  ← 告诉链接器如何链接
|   - .text: 代码节                      |
|   - .rodata: 只读数据节                |
|   - .data: 可写数据节                  |
|   - .bss: 未初始化数据节               |
|   - .symtab: 符号表                    |
|   - .strtab: 字符串表                  |
+---------------------------------------+
| 实际数据 (Sections/Segments)           |
|   - 代码段数据                         |
|   - 数据段数据                         |
|   - 调试信息                           |
+---------------------------------------+
```

### 1.3 关键概念：Section vs Segment

**Section（节）**：
- **编译阶段**的概念
- 按功能分类：`.text`（代码）、`.data`（数据）、`.bss`（未初始化）
- 用于链接和调试

**Segment（段）**：
- **运行阶段**的概念
- 多个 Section 合并成一个 Segment
- `PT_LOAD` 类型表示需要加载到内存

**示例**：

```bash
$ readelf -l bl31.elf

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00010000 0x00010000 0x0a580 0x0a580 R E 0x1000
  # ↑ 这个段包含 .text + .rodata 两个 Section

  LOAD           0x00c000 0x0001b000 0x0001b000 0x00520 0x00c20 RW  0x1000
  # ↑ 这个段包含 .data + .bss 两个 Section
```

**为什么分两种？**

- **编译/链接阶段**：使用 Section 精细管理代码组织
- **加载/运行阶段**：使用 Segment 批量加载到内存（减少系统调用）

### 1.4 实战：分析 BL31 ELF 文件

假设我们有 `rk3399_bl31_v1.28.elf`，使用工具查看：

```bash
# 1. 查看 ELF 头部
$ readelf -h rk3399_bl31_v1.28.elf

ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64           ← 64 位
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           AArch64         ← ARM64 架构
  Entry point address:               0x10000         ← 启动地址
  Start of program headers:          64 (bytes into file)
  Start of section headers:          43792 (bytes into file)

# 2. 查看程序头表（加载信息）
$ readelf -l rk3399_bl31_v1.28.elf

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00010000 0x00010000 0x0a580 0x0a580 R E 0x1000
  LOAD           0x00c000 0x0001b000 0x0001b000 0x00520 0x00c20 RW  0x1000
  LOAD           0x00d000 0xff8c0000 0xff8c0000 0x00800 0x00800 RW  0x1000

 Section to Segment mapping:
  Segment Sections...
   00     .text .rodata           ← 代码段包含这两个 Section
   01     .data .bss              ← 数据段包含这两个 Section
   02     .coherent_ram           ← SRAM 段（片上 SRAM）

# 3. 查看节头表（详细组织）
$ readelf -S rk3399_bl31_v1.28.elf

Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [ 1] .text             PROGBITS         0000000000010000  00001000
       000000000000a200  0000000000000000  AX       0     0     64
       # ↑ 代码节：可执行 (X)

  [ 2] .rodata           PROGBITS         000000000001a200  0000b200
       0000000000000380  0000000000000000   A       0     0     16
       # ↑ 只读数据节

  [ 3] .data             PROGBITS         000000000001b000  0000c000
       0000000000000520  0000000000000000  WA       0     0     16
       # ↑ 可写数据节

  [ 4] .bss              NOBITS           000000000001b520  0000c520
       0000000000000700  0000000000000000  WA       0     0     64
       # ↑ 未初始化数据（不占文件空间，只占内存空间）
```

**关键观察**：

1. **多段加载地址**：
   - Segment 0: 0x00010000 (DDR)
   - Segment 1: 0x0001b000 (DDR)
   - Segment 2: 0xff8c0000 (SRAM，不连续！)

2. **文件大小 vs 内存大小**：
   - Segment 1: FileSiz=0x520, MemSiz=0xc20
   - 差值 0x700 = `.bss` 节的大小（运行时清零）

3. **为什么 trust_merger 需要 ELF？**
   - 如果转成 `.bin`，会丢失段的地址信息
   - 只能保留一个连续区域，无法处理 SRAM 段

---

## 第二部分：Rockchip 固件格式剖析

### 2.1 为什么需要自定义格式？

**ELF 文件不能直接用于 BootROM**，原因：

1. **BootROM 固化在芯片中，不支持 ELF 解析**
   - 解析 ELF 需要几千行代码
   - BootROM 只有 32KB SRAM，放不下解析器

2. **安全启动需求**
   - 需要 RSA 签名验证
   - 需要 SHA 哈希校验
   - ELF 格式没有预留签名字段

3. **多副本容错机制**
   - Flash 可能有坏块
   - 需要多个备份（2~4 个副本）

4. **Rockchip 特定元数据**
   - 芯片类型识别（RK3399/RK3328）
   - 启动模式（SD 卡/eMMC/SPI）
   - DDR 频率配置

**解决方案**：设计一种**类似 ELF 但更简洁的固件格式**。

### 2.2 Rockchip 固件格式的共同特征

虽然有三种打包工具，但它们的输出格式遵循相同的设计模式：

```
Rockchip 固件通用结构：
┌─────────────────────────────────┐
│ Magic Number (4~8 字节)          │  ← 识别固件类型
│   - "BOOT" / "LOADER" / "TRUS"  │
├─────────────────────────────────┤
│ Header (固定大小)                 │  ← 元数据
│   - 版本号                        │
│   - 芯片类型                      │
│   - 组件数量                      │
│   - 加载地址                      │
│   - 大小/偏移                     │
│   - CRC/SHA 校验                  │
├─────────────────────────────────┤
│ Entry/Component Table            │  ← 组件信息表（类似 Program Header）
│   - Entry 0: 名称、偏移、大小     │
│   - Entry 1: ...                 │
├─────────────────────────────────┤
│ RSA Signature (可选)              │  ← 安全启动签名
├─────────────────────────────────┤
│ Data 0 (对齐到 512B/2KB)          │  ← 实际组件数据（类似 Segment）
├─────────────────────────────────┤
│ Data 1 (对齐)                     │
├─────────────────────────────────┤
│ ... 填充至固定大小 ...            │
└─────────────────────────────────┘
│ Backup Copy 1 (完整复制)          │  ← 备份副本
└─────────────────────────────────┘
```

**与 ELF 的对比**：

| 特性 | ELF | Rockchip 固件 |
|------|-----|--------------|
| **Magic** | `0x7F 'E' 'L' 'F'` | `"BOOT"` / `"LOADER"` / `"TRUS"` |
| **Header** | ELF Header (52/64B) | 自定义头 (102B~2048B) |
| **加载信息** | Program Header Table | Entry/Component Table |
| **数据** | Segments | 对齐的二进制数据块 |
| **校验** | 无（由 OS 验证） | CRC32 + SHA256 |
| **签名** | 无 | RSA-1024/2048 |
| **备份** | 无 | 2~4 个副本 |

**核心相似性**：都是**"头部 + 表 + 数据"**的结构！

---

## 第三部分：三大打包工具详解与对比

### 3.1 工具概览

| 工具 | 输入 | 输出 | 用途 | 对应启动阶段 |
|------|------|------|------|-------------|
| **boot_merger** | DDR init.bin + miniloader.bin | `loader.bin` / `idbloader.img` | 合并第一阶段引导 | BootROM → DDR init → Miniloader |
| **loaderimage** | u-boot.bin / trust.bin | `uboot.img` / `trust.img` | 添加 Rockchip 头 | Miniloader → U-Boot |
| **trust_merger** | bl31.elf + bl32.bin | `trust.img` | 合并安全固件 | U-Boot → ATF → OP-TEE |

**启动流程**：

```
BootROM (固化在芯片)
  ↓ 读取 loader.bin (boot_merger 生成)
DDR Init (初始化内存)
  ↓
Miniloader (最小化引导)
  ↓ 读取 uboot.img (loaderimage 生成)
U-Boot (bootloader)
  ↓ 读取 trust.img (trust_merger 生成)
ARM Trusted Firmware (BL31)
  ↓
OP-TEE (BL32) / Linux Kernel
```

---

### 3.2 boot_merger 详解

#### 功能
将 **DDR 初始化代码（FlashData）** 和 **Miniloader（FlashBoot）** 合并成 `loader.bin`。

#### 输入文件（来自 INI 配置）

```ini
# RK3399MINIALL.ini
[CHIP_NAME]
NAME=RK330C

[VERSION]
MAJOR=2
MINOR=50

[CODE471_OPTION]
NUM=1
Path1=bin/rk33/rk3399_ddr_800MHz_v1.30.bin  ← DDR 初始化代码

[CODE472_OPTION]
NUM=1
Path1=bin/rk33/rk3399_usbplug_v1.30.bin     ← USB 恢复模式代码（可选）

[LOADER_OPTION]
NUM=2
LOADER1=FlashData                            ← Entry 名称
LOADER2=FlashBoot
Path1=bin/rk33/rk3399_ddr_800MHz_v1.30.bin  ← 与 CODE471 相同
Path2=bin/rk33/rk3399miniloaderall_v1.26.bin ← Miniloader 主程序

[OUTPUT]
PATH=rk3399_loader_v1.26.bin
```

#### 输出格式：loader.bin

```c
// boot_merger.h:151-170
typedef struct {
    uint32_t        tag;                // 魔数: 0x544F4F42 ("BOOT" 小端)
    uint16_t        size;               // Header 大小: 102 字节
    uint32_t        version;            // 固件版本号 (BCD 码)
    uint32_t        mergerVersion;      // 打包工具版本: 0x01030000
    rk_time         releaseTime;        // 发布时间 (年月日时分秒)
    uint32_t        chipType;           // 芯片类型: 0x33393933 (RK3399)

    // FlashData (DDR init) 信息
    uint8_t         code471Num;         // 数量: 1
    uint32_t        code471Offset;      // 偏移: 4 扇区 (2048 字节)
    uint8_t         code471Size;        // 大小: 以 512 字节为单位

    // USB 恢复模式信息（通常不用）
    uint8_t         code472Num;         // 数量: 0 或 1
    uint32_t        code472Offset;      // 偏移
    uint8_t         code472Size;        // 大小

    // Loader (Miniloader) 信息
    uint8_t         loaderNum;          // 数量: 2 (FlashData + FlashBoot)
    uint32_t        loaderOffset;       // Entry 表偏移: 102 字节
    uint8_t         loaderSize;         // Entry 表大小

    // 安全配置
    uint8_t         signFlag;           // RSA 签名标志
    uint8_t         rc4Flag;            // RC4 加密标志
    uint8_t         reserved[57];       // 保留字节
} rk_boot_header;  // 总大小: 102 字节
```

**Entry 表结构**：

```c
// boot_merger.h:172-179
typedef struct {
    uint8_t         size;               // Entry 大小: 57 字节
    rk_entry_type   type;               // 类型: 1=471, 2=472, 4=Loader
    uint16_t        name[20];           // 名称: "FlashData" / "FlashBoot" (Unicode)
    uint32_t        dataOffset;         // 数据偏移 (扇区号)
    uint32_t        dataSize;           // 数据大小 (字节)
    uint32_t        dataDelay;          // 启动延迟 (毫秒)
} rk_boot_entry;  // 57 字节
```

**完整布局**：

```
+---------------------------------------+
| rk_boot_header (102 字节)              |
|   tag: 0x544F4F42 ("BOOT")            |
|   chipType: 0x33393933 (RK3399)       |
|   loaderNum: 2                        |
|   loaderOffset: 102                   |
+---------------------------------------+
| rk_boot_entry[0] (57 字节)             |  ← Entry 表开始
|   type: 4 (LOADER)                    |
|   name: "FlashData"                   |
|   dataOffset: 4 (扇区)                |
|   dataSize: 232448 (字节)             |
+---------------------------------------+
| rk_boot_entry[1] (57 字节)             |
|   type: 4 (LOADER)                    |
|   name: "FlashBoot"                   |
|   dataOffset: 458 (扇区)              |
|   dataSize: 184320                    |
+---------------------------------------+
| 填充至 2048 字节                       |
+---------------------------------------+
| DDR init 数据 (232KB, 对齐到 2KB)      |  ← dataOffset=4 指向这里
|   (rk3399_ddr_800MHz_v1.30.bin)      |
+---------------------------------------+
| Miniloader 数据 (180KB, 对齐到 2KB)    |  ← dataOffset=458 指向这里
|   (rk3399miniloaderall_v1.26.bin)   |
+---------------------------------------+
```

**与 ELF 的相似性**：

| ELF | boot_merger 输出 |
|-----|-----------------|
| ELF Header | rk_boot_header |
| Program Header Table | rk_boot_entry[] (Entry 表) |
| Segment 0 (代码段) | FlashData 数据 |
| Segment 1 (数据段) | FlashBoot 数据 |
| VirtAddr (加载地址) | dataOffset (存储偏移) |

**BootROM 的加载流程**：

```c
// 伪代码：BootROM 如何解析 loader.bin
rk_boot_header *header = (rk_boot_header *)flash_read(0);

// 1. 验证魔数
if (header->tag != 0x544F4F42) {
    goto_usb_download_mode();
}

// 2. 验证芯片类型
if (header->chipType != MY_CHIP_TYPE) {
    error("Wrong chip type!");
}

// 3. 读取 Entry 表
rk_boot_entry *entries = (rk_boot_entry *)(flash_addr + header->loaderOffset);

// 4. 加载 FlashData (DDR init)
rk_boot_entry *ddr_init = &entries[0];
memcpy(SRAM_ADDR, flash_read(ddr_init->dataOffset << 9), ddr_init->dataSize);
run_code(SRAM_ADDR);  // 初始化 DDR

// 5. 加载 FlashBoot (Miniloader)
rk_boot_entry *miniloader = &entries[1];
memcpy(DDR_ADDR, flash_read(miniloader->dataOffset << 9), miniloader->dataSize);
jump_to(DDR_ADDR);  // 跳转到 Miniloader
```

**为什么像 ELF？**
- 都有固定的头部识别格式
- 都有表格描述组件的位置和大小
- 都通过偏移量定位数据
- 区别：ELF 用虚拟地址，Rockchip 用存储偏移（扇区号）

---

### 3.3 loaderimage 详解

#### 功能
给 **u-boot.bin** 或 **trust.bin** 添加 Rockchip 专用头部，生成 `uboot.img` / `trust.img`。

#### 输入文件
- `u-boot.bin`：U-Boot bootloader 的原始二进制文件
- 或 `trust.bin`：ARM Trusted Firmware 的原始二进制文件

#### 输出格式：uboot.img

```c
// loaderimage.c:72-95
typedef struct tag_second_loader_hdr {
    /* 基础信息区 (32 字节) */
    uint8_t magic[8];           // 魔数: "LOADER  " (U-Boot) 或 "TOS     " (Trust)
    uint32_t version;           // 版本号: 用于 Rollback 保护
    uint32_t reserved0;         // 保留
    uint32_t loader_load_addr;  // 加载地址: 0x00200000 (U-Boot) / 0x08400000 (Trust)
    uint32_t loader_load_size;  // 镜像大小: 实际 bin 文件大小

    /* 校验信息区 (32 字节) */
    uint32_t crc32;             // CRC32 校验值
    uint32_t hash_len;          // 哈希长度: 32 (SHA256)
    uint8_t hash[32];           // SHA256 哈希值

    /* 填充区 (960 字节) */
    uint8_t reserved[960];      // 对齐到 1024 字节

    /* RSA 签名区 (264 字节) */
    uint32_t signTag;           // 签名标记: 0x4E474953 ("SIGN" 小端)
    uint32_t signlen;           // RSA 签名长度: 256 (RSA2048)
    uint8_t rsaHash[256];       // RSA 签名数据

    /* 尾部填充 (760 字节) */
    uint8_t reserved2[760];     // 填充至 2048 字节
} second_loader_hdr;  // 总大小: 2048 字节
```

**完整布局**：

```
+---------------------------------------+
| second_loader_hdr (2048 字节)          |
|   magic: "LOADER  "                   |
|   loader_load_addr: 0x00200000        |
|   loader_load_size: 524288 (512KB)    |
|   crc32: 0x12345678                   |
|   hash[32]: [SHA256 哈希]             |
|   signTag: 0x4E474953                 |
|   rsaHash[256]: [RSA 签名]            |
+---------------------------------------+
| u-boot.bin 原始数据 (512KB)            |
|   (从文件直接复制)                     |
+---------------------------------------+
| 填充至 1MB 边界                        |
+---------------------------------------+
| Backup Copy 1 (完整复制前面 1MB)       |
+---------------------------------------+
| Backup Copy 2 (完整复制前面 1MB)       |
+---------------------------------------+
| Backup Copy 3 (完整复制前面 1MB)       |
+---------------------------------------+
```

**命令示例**：

```bash
# 打包 U-Boot
loaderimage --pack --uboot u-boot.bin uboot.img 0x200000

# 打包 Trust OS
loaderimage --pack --trustos trust.bin trust.img 0x8400000

# 解包（提取原始 bin）
loaderimage --unpack uboot.img u-boot-extracted.bin

# 查看镜像信息
loaderimage --info uboot.img
# 输出：
# magic: LOADER
# version: 1
# loader_load_addr: 0x00200000
# loader_load_size: 524288
# crc32: 0x12345678
# hash_len: 32
```

**与 ELF 的相似性**：

| ELF | loaderimage 输出 |
|-----|-----------------|
| ELF Header | second_loader_hdr (2048B) |
| Entry Point | loader_load_addr |
| Program Header (加载信息) | 嵌入在 Header 中 |
| Segment Data | u-boot.bin 原始数据 |
| 多个 PT_LOAD 段 | 不支持（只能单段） |

**为什么比 boot_merger 简单？**

1. **只有一个数据块**：
   - boot_merger: 多个 Entry（FlashData + FlashBoot）
   - loaderimage: 单个二进制文件

2. **不需要 Entry 表**：
   - 直接在 Header 中记录加载地址和大小
   - 数据紧跟在 Header 后面（偏移 = 2048）

3. **更像"带头部的 bin 文件"**：
   - `uboot.img` = `second_loader_hdr` + `u-boot.bin`

**Miniloader 的加载流程**：

```c
// 伪代码：Miniloader 如何加载 uboot.img
second_loader_hdr *header = (second_loader_hdr *)flash_read(UBOOT_OFFSET);

// 1. 验证魔数
if (memcmp(header->magic, "LOADER  ", 8) != 0) {
    try_backup_copy();
}

// 2. 验证 CRC32
uint32_t crc = crc32_calc(flash_read(UBOOT_OFFSET + 2048), header->loader_load_size);
if (crc != header->crc32) {
    try_backup_copy();
}

// 3. 验证 SHA256（如果启用安全启动）
sha256_calc(flash_read(UBOOT_OFFSET + 2048), header->loader_load_size, hash);
if (memcmp(hash, header->hash, 32) != 0) {
    error("Hash mismatch!");
}

// 4. 验证 RSA 签名（如果启用安全启动）
if (header->signTag == 0x4E474953) {
    rsa_verify(header->hash, header->rsaHash, public_key);
}

// 5. 加载到内存并跳转
memcpy(header->loader_load_addr, flash_read(UBOOT_OFFSET + 2048), header->loader_load_size);
jump_to(header->loader_load_addr);  // 跳转到 U-Boot
```

---

### 3.4 trust_merger 详解

#### 功能
将 **BL31 (ARM Trusted Firmware)** 和 **BL32 (OP-TEE)** 合并成 `trust.img`，支持 ELF 文件的多段提取。

#### 输入文件（来自 INI 配置）

```ini
# RK3399TRUST.ini
[VERSION]
MAJOR=1
MINOR=0

[BL31_OPTION]
SEC=1                                      ← 启用
PATH=bin/rk33/rk3399_bl31_v1.28.elf        ← ELF 文件
ADDR=0x00010000                            ← 加载地址（如果非 ELF）

[BL32_OPTION]
SEC=1
PATH=bin/rk33/rk3399_bl32_v1.18.bin        ← BIN 文件
ADDR=0x08400000

[OUTPUT]
PATH=trust.img
```

#### 输出格式：trust.img

```c
// trust_merger.h (头部结构)
typedef struct {
    uint8_t tag[4];              // 魔数: "TRUS" (0x54525553)
    uint32_t size;               // 高 16 位: 组件数量, 低 16 位: 签名偏移/4
    uint32_t version;            // 版本号 (BCD 码): 0x0100
    uint32_t flags;              // 低 4 位: SHA 模式, 高 4 位: RSA 模式
    uint8_t reserved[496];       // 保留
    uint8_t rsa_n[256];          // RSA 公钥模数
    uint8_t rsa_e[256];          // RSA 公钥指数
    uint8_t rsa_c[256];          // RSA 签名密文
} TRUST_HEADER;  // 总大小: 2048 字节

// 组件数据区（紧跟 Header）
typedef struct {
    uint32_t LoadAddr;           // 加载地址: 0x00010000 / 0x08400000
    uint8_t reserved[4];
    uint32_t HashData[8];        // SHA256 哈希值 (32 字节)
} COMPONENT_DATA;  // 40 字节/组件

// 签名区（768 字节，预留）
// RSA-N (256B) + RSA-E (256B) + RSA-C (256B)

// 组件信息区（紧跟签名区）
typedef struct {
    uint32_t ComponentID;        // 组件 ID: 'BL31' / 'BL32'
    uint32_t StorageAddr;        // 存储地址 (扇区号)
    uint32_t ImageSize;          // 镜像大小 (扇区数)
} TRUST_COMPONENT;  // 12 字节/组件
```

**完整布局**：

```
+---------------------------------------+
| TRUST_HEADER (2048 字节)               |
|   tag: "TRUS"                         |
|   size: 0x00020214                    |
|         ↑ 高 16 位: 2 个组件           |
|         ↑ 低 16 位: 0x0214 (签名偏移/4)|
|   flags: 0x23 (SHA256=3, RSA2048=2)   |
+---------------------------------------+
| COMPONENT_DATA[0] (40 字节)            |  ← 组件数据区
|   LoadAddr: 0x00010000 (BL31)         |
|   HashData: [32 字节 SHA256]          |
+---------------------------------------+
| COMPONENT_DATA[1] (40 字节)            |
|   LoadAddr: 0x08400000 (BL32)         |
|   HashData: [32 字节 SHA256]          |
+---------------------------------------+
| RSA Signature (768 字节)               |  ← SignOffset=0x0850 指向这里
|   rsa_n: [256 字节]                   |
|   rsa_e: [256 字节]                   |
|   rsa_c: [256 字节]                   |
+---------------------------------------+
| TRUST_COMPONENT[0] (12 字节)           |  ← 组件信息区
|   ComponentID: 0x424C3331 ('BL31')    |
|   StorageAddr: 4 (扇区)               |
|   ImageSize: 90 (扇区)                |
+---------------------------------------+
| TRUST_COMPONENT[1] (12 字节)           |
|   ComponentID: 0x424C3332 ('BL32')    |
|   StorageAddr: 94 (扇区)              |
|   ImageSize: 520 (扇区)               |
+---------------------------------------+
| BL31 Segment 0 数据 (代码段, 42KB)     |  ← StorageAddr=4 指向这里
|   (从 bl31.elf 的 PT_LOAD 段 0 提取)  |
+---------------------------------------+
| BL31 Segment 1 数据 (数据段, 3KB)      |
|   (从 bl31.elf 的 PT_LOAD 段 1 提取)  |
+---------------------------------------+
| BL32 数据 (260KB, 对齐到 512B)         |
|   (从 bl32.bin 直接读取)              |
+---------------------------------------+
| 填充至 2MB 边界                        |
+---------------------------------------+
| Backup Copy 1 (完整复制前面 2MB)       |
+---------------------------------------+
```

**与 ELF 的相似性**：

| ELF | trust_merger 输出 |
|-----|------------------|
| ELF Header | TRUST_HEADER (2048B) |
| Program Header Table | COMPONENT_DATA[] + TRUST_COMPONENT[] |
| PT_LOAD Segment 0 | BL31 代码段数据 |
| PT_LOAD Segment 1 | BL31 数据段数据 |
| PT_LOAD Segment 2 | BL32 数据 |
| VirtAddr (加载地址) | COMPONENT_DATA::LoadAddr |
| PhysAddr (物理地址) | TRUST_COMPONENT::StorageAddr (扇区号) |

**关键特性**：

1. **支持 ELF 多段提取**：
   ```c
   // trust_merger.c:563-586
   for (i = 0; i < pElfHeader64->e_phnum; i++) {
       if (pElfProgram64->p_type == 1) {  // PT_LOAD
           pEntry->addr = pElfProgram64->p_vaddr;    // 虚拟地址
           pEntry->offset = pElfProgram64->p_offset; // 文件偏移
           pEntry->size = pElfProgram64->p_filesz;   // 段大小
       }
   }
   ```

2. **自动计算 SHA256 哈希**：
   ```c
   // trust_merger.c:850-871
   for (i = 0; i < nComponentNum; i++) {
       fread(gBuf, pEntry->size, 1, inFile);
       bl3xHash256(pHashData, gBuf, pEntry->align_size);  // 计算哈希
       pComponentData->HashData = hash;
   }
   ```

3. **分离的加载地址和存储位置**：
   - `LoadAddr`：运行时地址（0x00010000，从 ELF VirtAddr 提取）
   - `StorageAddr`：Flash 存储位置（扇区号，自动计算）

**U-Boot 的加载流程**：

```c
// 伪代码：U-Boot 如何加载 trust.img
TRUST_HEADER *header = (TRUST_HEADER *)flash_read(TRUST_OFFSET);

// 1. 验证魔数
if (memcmp(header->tag, "TRUS", 4) != 0) {
    try_backup_copy();
}

// 2. 解析组件数量和签名偏移
uint32_t num_components = (header->size >> 16) & 0xFFFF;  // 高 16 位
uint32_t sign_offset = (header->size & 0xFFFF) << 2;      // 低 16 位 * 4

// 3. 读取组件信息表
COMPONENT_DATA *comp_data = (COMPONENT_DATA *)(header + 1);
TRUST_COMPONENT *comp_info = (TRUST_COMPONENT *)((uint8_t *)header + sign_offset + 768);

// 4. 加载每个组件
for (int i = 0; i < num_components; i++) {
    // 读取组件数据
    uint32_t storage_addr = comp_info[i].StorageAddr << 9;  // 扇区 → 字节
    uint32_t image_size = comp_info[i].ImageSize << 9;
    uint8_t *data = flash_read(TRUST_OFFSET + storage_addr);

    // 验证 SHA256
    sha256_calc(data, image_size, hash);
    if (memcmp(hash, comp_data[i].HashData, 32) != 0) {
        error("Component %d hash mismatch!", i);
    }

    // 复制到加载地址
    memcpy(comp_data[i].LoadAddr, data, image_size);
}

// 5. 验证 RSA 签名（如果启用安全启动）
// ...

// 6. 跳转到 BL31
jump_to(0x00010000);  // BL31 入口点
```

---

## 第四部分：三大工具横向对比

### 4.1 功能对比表

| 特性 | boot_merger | loaderimage | trust_merger |
|------|-------------|-------------|-------------|
| **输入** | 多个 bin 文件 | 单个 bin 文件 | ELF/bin 混合 |
| **输出** | loader.bin | uboot.img / trust.img | trust.img |
| **魔数** | "BOOT" (0x544F4F42) | "LOADER" / "TOS" | "TRUS" |
| **Header 大小** | 102 字节 | 2048 字节 | 2048 字节 |
| **Entry 表** | 有（rk_boot_entry[]） | 无（嵌入 Header） | 有（COMPONENT_DATA[] + TRUST_COMPONENT[]） |
| **多段支持** | 支持（多个 Entry） | 不支持（单段） | 支持（ELF 多段） |
| **ELF 解析** | 不支持 | 不支持 | **支持** |
| **哈希算法** | CRC32 | CRC32 + SHA256 | SHA256 |
| **签名** | 可选 | 可选 | 预留 RSA 区 |
| **备份副本** | 1 个 | 4 个 (U-Boot) | 2 个 |
| **对齐单位** | 2048 字节 | 512 字节 | 512 字节 |
| **加密** | RC4（可选） | 无 | 无 |

### 4.2 数据结构对比

#### Header 对比

```c
// boot_merger: 102 字节
struct {
    uint32_t tag;           // "BOOT"
    uint16_t size;          // 102
    uint32_t version;
    uint32_t chipType;      // RK3399
    uint8_t loaderNum;      // 组件数量
    uint32_t loaderOffset;  // Entry 表偏移
    // ...
};

// loaderimage: 2048 字节
struct {
    uint8_t magic[8];       // "LOADER  "
    uint32_t version;
    uint32_t load_addr;     // 加载地址
    uint32_t load_size;     // 镜像大小
    uint32_t crc32;
    uint8_t hash[32];       // SHA256
    uint8_t reserved[960];
    uint8_t rsaHash[256];   // RSA 签名
    // ...
};

// trust_merger: 2048 字节
struct {
    uint8_t tag[4];         // "TRUS"
    uint32_t size;          // 组件数量 + 签名偏移
    uint32_t version;
    uint32_t flags;         // SHA/RSA 模式
    uint8_t rsa_n[256];     // RSA 公钥
    uint8_t rsa_e[256];
    uint8_t rsa_c[256];     // RSA 签名
    // ...
};
```

#### Entry/Component 表对比

```c
// boot_merger: rk_boot_entry (57 字节)
struct {
    uint8_t size;           // 57
    rk_entry_type type;     // 1=471, 2=472, 4=Loader
    uint16_t name[20];      // Unicode 名称
    uint32_t dataOffset;    // 扇区号
    uint32_t dataSize;      // 字节数
    uint32_t dataDelay;     // 延迟
};

// loaderimage: 无 Entry 表（信息嵌入 Header）

// trust_merger: COMPONENT_DATA (40B) + TRUST_COMPONENT (12B)
struct {
    // COMPONENT_DATA (运行时信息)
    uint32_t LoadAddr;      // 加载地址
    uint32_t HashData[8];   // SHA256

    // TRUST_COMPONENT (存储信息)
    uint32_t ComponentID;   // 'BL31'/'BL32'
    uint32_t StorageAddr;   // 扇区号
    uint32_t ImageSize;     // 扇区数
};
```

### 4.3 与 ELF 格式的相似度评分

| 工具 | 相似度 | 原因 |
|------|--------|------|
| **boot_merger** | ⭐⭐⭐⭐ (4/5) | 有独立的 Entry 表，类似 Program Header Table |
| **loaderimage** | ⭐⭐ (2/5) | 只有单段，信息嵌入 Header，更像"带头部的 bin" |
| **trust_merger** | ⭐⭐⭐⭐⭐ (5/5) | 最接近 ELF，支持多段，分离加载地址和存储位置，甚至直接解析 ELF 文件 |

**为什么 trust_merger 最像 ELF？**

1. **直接处理 ELF 文件**（filter_elf 函数）
2. **保留多段信息**（每个 PT_LOAD 段成为一个组件）
3. **分离地址空间**：
   - LoadAddr（虚拟地址，类似 ELF VirtAddr）
   - StorageAddr（物理偏移，类似 ELF Offset）
4. **哈希每个段**（类似 ELF 的段校验）

---

## 第五部分：深度分析 - 为什么 Rockchip 格式像 ELF？

### 5.1 设计动机

**问题**：BootROM 无法直接运行 ELF 文件

**需求**：
1. 识别固件类型（U-Boot vs Trust vs Loader）
2. 校验完整性（CRC32/SHA256）
3. 验证签名（RSA）
4. 支持多个组件（DDR init + Miniloader）
5. 容错机制（多副本）

**解决方案**：借鉴 ELF 的设计思想，创建简化版格式

### 5.2 设计模式对比

#### ELF 的设计模式

```
读取 ELF Header → 定位 Program Header Table → 遍历 PT_LOAD 段 →
复制到内存 (memcpy) → 跳转到入口点
```

#### Rockchip 固件的设计模式

```
读取 Header (BOOT/LOADER/TRUS) → 定位 Entry/Component Table →
遍历组件 → 验证哈希 → 复制到内存 → 跳转到加载地址
```

**核心相似性**：都是**"头部 → 表 → 数据"**的三层结构！

### 5.3 为什么不直接用 ELF？

| ELF 的问题 | Rockchip 的解决方案 |
|-----------|-------------------|
| **解析复杂** (需要处理 Section/Segment/Symbol) | 简化为固定结构的 Header + Entry 表 |
| **没有校验字段** | 内置 CRC32/SHA256 |
| **没有签名字段** | 预留 RSA 签名区 |
| **没有备份机制** | 多副本设计 |
| **没有芯片识别** | 增加 chipType 字段 |
| **加载地址在 Program Header** | 分离到独立的 Component Data 区 |
| **文件大小不可控** (包含调试信息) | 只保留必要的加载信息 |

### 5.4 trust_merger 的"ELF 桥接"角色

trust_merger 是**ELF 到 Rockchip 格式的转换器**：

```
编译器输出 (ELF)
   ↓
trust_merger (桥接)
   ├─ 解析 ELF Program Headers
   ├─ 提取 PT_LOAD 段
   ├─ 计算 SHA256 哈希
   ├─ 构建 TRUST_HEADER
   ├─ 生成 COMPONENT_DATA
   └─ 写入 Rockchip 格式
   ↓
BootROM/Miniloader 可识别的固件
```

**对比**：

```c
// ELF Program Header
struct Elf64_Phdr {
    uint32_t p_type;      // PT_LOAD = 1
    uint64_t p_offset;    // 文件内偏移: 0x1000
    uint64_t p_vaddr;     // 虚拟地址: 0x00010000
    uint64_t p_paddr;     // 物理地址
    uint64_t p_filesz;    // 文件大小: 0x0a580
    uint64_t p_memsz;     // 内存大小: 0x0a580
    uint32_t p_flags;     // 权限: R+X
    uint64_t p_align;     // 对齐: 0x1000
};

// ↓ trust_merger 转换 ↓

// Rockchip COMPONENT_DATA + TRUST_COMPONENT
COMPONENT_DATA {
    LoadAddr: 0x00010000      // ← p_vaddr
    HashData: [SHA256(数据)]   // ← 新增
}
TRUST_COMPONENT {
    ComponentID: 'BL31'       // ← 新增
    StorageAddr: 4            // ← (Header大小 + 前面组件) / 512
    ImageSize: 90             // ← p_filesz / 512
}
```

### 5.5 实战示例：对比三种格式

假设我们有相同的 U-Boot 代码（512KB），看看三种工具的输出：

#### 场景 1：使用 loaderimage

```bash
# 输入：u-boot.bin (512KB)
# 输出：uboot.img (4MB = 1MB x 4 副本)

结构：
[Header 2KB][u-boot 512KB][填充至 1MB] x 4
```

#### 场景 2：如果 boot_merger 打包 U-Boot（假设）

```bash
# 输入：u-boot.bin (512KB)
# 输出：loader.bin (512KB + 少量 Header)

结构：
[Header 102B][Entry表 57B][填充至 2KB][u-boot 512KB]
```

#### 场景 3：如果 trust_merger 打包 U-Boot（假设）

```bash
# 输入：u-boot.elf (可能有多个段)
# 输出：trust.img (2MB x 2 副本)

结构：
[Header 2KB][ComponentData 40B][ComponentInfo 12B][u-boot段 512KB][填充至 2MB] x 2
```

**观察**：
- **loaderimage**：最简单，适合单段二进制
- **boot_merger**：支持多组件（DDR + Miniloader）
- **trust_merger**：最复杂，支持 ELF 多段 + 哈希

---

## 第六部分：实战练习与调试技巧

### 6.1 练习 1：手工解析 trust.img

```bash
# 1. 生成 trust.img
cd uboot
./make.sh trust

# 2. 使用 hexdump 查看头部
hexdump -C trust.img | head -30

# 输出解读：
# 00000000  54 52 55 53 14 02 24 01  00 00 00 00 00 00 00 00  |TRUS..$.........|
#           ^^^^^^^^ "TRUS" 魔数
#                     ^^^^^ size 字段: 0x00020214
#                           ^^^^ 高 16 位: 0x0002 = 2 个组件
#                                ^^^^ 低 16 位: 0x0214 = 签名偏移/4
#                                         ^^^^^ version: 0x0124 (1.24)
#                                               ^^^^^ flags: 0x23 (SHA=3, RSA=2)

# 3. 提取组件信息
dd if=trust.img bs=1 skip=2048 count=40 | hexdump -C
# 第一个 COMPONENT_DATA:
#   LoadAddr: 0x00010000 (BL31)
#   HashData: [32 字节 SHA256]

# 4. 定位组件数据
# SignOffset = 0x0214 * 4 = 0x0850 (2128 字节)
# ComponentInfo 位置 = 2048 + 2128 + 768 = 4944 字节
dd if=trust.img bs=1 skip=4944 count=12 | hexdump -C
# 第一个 TRUST_COMPONENT:
#   ComponentID: 0x424C3331 ('BL31')
#   StorageAddr: 4 (扇区) = 2048 字节
#   ImageSize: 90 (扇区) = 46080 字节

# 5. 提取 BL31 数据
dd if=trust.img bs=512 skip=4 count=90 of=bl31_extracted.bin

# 6. 验证 SHA256
sha256sum bl31_extracted.bin
# 对比 COMPONENT_DATA[0].HashData
```

### 6.2 练习 2：对比 ELF 和 Rockchip 格式

```bash
# 1. 查看原始 ELF
readelf -l external/rkbin/bin/rk33/rk3399_bl31_v1.28.elf

# 输出：
# LOAD  0x001000 0x00010000 0x00010000 0x0a580 0x0a580 R E 0x1000
# LOAD  0x00c000 0x0001b000 0x0001b000 0x00520 0x00c20 RW  0x1000

# 2. 查看打包后的 trust.img
trust_merger --unpack trust.img

# 3. 对比提取的数据
readelf -x .text rk3399_bl31_v1.28.elf > elf_text.hex
hexdump -C BL31 | head -50 > trust_bl31.hex
diff elf_text.hex trust_bl31.hex
# 应该看到代码段内容一致！
```

### 6.3 练习 3：从零手工创建 Rockchip 固件

```python
#!/usr/bin/env python3
# 手工创建最小化的 trust.img

import struct
import hashlib

# 1. 读取组件数据
with open('bl31.bin', 'rb') as f:
    bl31_data = f.read()

# 2. 对齐到 512 字节
bl31_size = len(bl31_data)
bl31_aligned = bl31_data + b'\x00' * (512 - bl31_size % 512)

# 3. 计算 SHA256
bl31_hash = hashlib.sha256(bl31_aligned).digest()

# 4. 构建 TRUST_HEADER
header = struct.pack(
    '<4sIIH',          # Little-endian: tag, size, version, reserved
    b'TRUS',           # Magic
    (1 << 16) | 0x214, # 1 个组件, SignOffset=0x214*4
    0x0100,            # Version 1.0
    0x23               # Flags: SHA256=3, RSA2048=2
)
header += b'\x00' * (2048 - len(header))  # 填充至 2048 字节

# 5. 构建 COMPONENT_DATA
comp_data = struct.pack('<I4x32s', 0x00010000, bl31_hash)  # LoadAddr + Hash

# 6. 构建 TRUST_COMPONENT
storage_addr = 2048 // 512  # Header 之后
image_size = len(bl31_aligned) // 512
comp_info = struct.pack('<III', 0x424C3331, storage_addr, image_size)  # 'BL31'

# 7. 组装完整镜像
trust_img = header + comp_data + b'\x00' * 768 + comp_info + bl31_aligned

# 8. 写入文件
with open('trust_manual.img', 'wb') as f:
    f.write(trust_img)

print(f"Created trust_manual.img ({len(trust_img)} bytes)")
```

### 6.4 调试技巧总结

| 任务 | 工具 | 命令 |
|------|------|------|
| **查看 ELF 头部** | readelf | `readelf -h bl31.elf` |
| **查看程序头** | readelf | `readelf -l bl31.elf` |
| **查看节头** | readelf | `readelf -S bl31.elf` |
| **提取 ELF 段** | objcopy | `objcopy -O binary --only-section=.text bl31.elf text.bin` |
| **查看二进制文件** | hexdump | `hexdump -C uboot.img \| head` |
| **提取固件片段** | dd | `dd if=trust.img bs=512 skip=4 count=90 of=bl31.bin` |
| **计算 SHA256** | sha256sum | `sha256sum bl31.bin` |
| **计算 CRC32** | crc32 | `crc32 u-boot.bin` |
| **解包固件** | 工具自带 | `trust_merger --unpack trust.img` |
| **查看固件信息** | 工具自带 | `loaderimage --info uboot.img` |

---

## 第七部分：总结与进阶

### 7.1 核心要点回顾

1. **ELF 文件是编译器的标准输出**，包含代码、数据和元信息（加载地址、段划分）

2. **Rockchip 固件格式借鉴了 ELF 的设计思想**：
   - Header → ELF Header
   - Entry/Component Table → Program Header Table
   - Data Blocks → Segments

3. **三大打包工具各有侧重**：
   - `boot_merger`：多组件合并（DDR + Miniloader）
   - `loaderimage`：单组件 + 完整性校验
   - `trust_merger`：ELF 多段处理 + 哈希

4. **trust_merger 最接近 ELF 格式**：
   - 直接解析 ELF Program Headers
   - 提取 PT_LOAD 段
   - 保留虚拟地址信息

5. **Rockchip 格式的核心优势**：
   - 简化的解析流程（BootROM 只需几百行代码）
   - 内置校验和签名字段
   - 多副本容错机制

### 7.2 学习路径建议

**阶段 1：理解 ELF 格式**
- 阅读 ELF 规范：https://refspecs.linuxfoundation.org/elf/elf.pdf
- 使用 `readelf` 分析编译输出
- 手工解析 ELF 头部（Python/C）

**阶段 2：分析 Rockchip 固件**
- 使用 `hexdump` 查看固件头部
- 对比三种工具的输出格式
- 使用 `--unpack` 提取组件并验证

**阶段 3：源码阅读**
- 阅读 `boot_merger.c`（简单的多组件合并）
- 阅读 `loaderimage.c`（单组件 + 校验）
- 深入 `trust_merger.c`（ELF 解析 + 哈希）

**阶段 4：实战项目**
- 修改打包工具支持新的芯片型号
- 实现自定义的固件验证工具
- 编写 BootROM 模拟器（解析和加载固件）

### 7.3 常见问题 FAQ

**Q1：为什么 BL31 必须用 ELF 而不能用 BIN？**

A：BL31 有多个不连续的段（DDR 代码段 + DDR 数据段 + SRAM 段），如果转成 BIN 会丢失段的地址信息。trust_merger 通过解析 ELF 保留了所有段的加载地址。

**Q2：loaderimage 和 trust_merger 有什么区别？**

A：
- `loaderimage`：给单个 bin 文件添加 Rockchip 头（简单封装）
- `trust_merger`：合并多个组件并支持 ELF 多段提取（复杂合并）

两者都能生成 `trust.img`，但 `trust_merger` 功能更强大。

**Q3：如何验证固件的完整性？**

A：
```bash
# 1. 提取组件数据
dd if=trust.img bs=512 skip=4 count=90 of=bl31.bin

# 2. 计算 SHA256
sha256sum bl31.bin

# 3. 提取 COMPONENT_DATA 中的 HashData
dd if=trust.img bs=1 skip=2048 count=40 | hexdump -C
# 对比 HashData 字段（偏移 4）

# 4. 两者应该一致！
```

**Q4：能否用 objcopy 替代这些打包工具？**

A：不能完全替代，因为：
- `objcopy` 只能转换 ELF → BIN，但不添加 Rockchip 头部
- 需要手工添加 CRC/SHA/RSA 字段
- 无法生成多副本

但可以结合使用：
```bash
# 先用 objcopy 提取段
objcopy -O binary bl31.elf bl31.bin

# 再用打包工具添加头部
loaderimage --pack --trustos bl31.bin trust.img 0x10000
```

**Q5：如何添加自定义的安全组件（如 BL34）？**

A：修改 `trust_merger.c`：

1. 在 `trust_merger.h` 定义 BL34：
   ```c
   #define BL34_SEC 4
   #define SEC_BL34 "[BL34_OPTION]"
   ```

2. 在 `parseOpts()` 添加解析逻辑：
   ```c
   else if (!strcmp(buf, SEC_BL34)) {
       bl34ok = parseBL3x(file, BL34_SEC);
   }
   ```

3. 在 INI 文件添加配置：
   ```ini
   [BL34_OPTION]
   SEC=1
   PATH=bin/rk33/rk3399_bl34_custom.bin
   ADDR=0x10000000
   ```

### 7.4 扩展资源

**官方文档**：
- Rockchip Wiki: https://opensource.rock-chips.com/
- ARM Trusted Firmware: https://trustedfirmware-a.readthedocs.io/
- OP-TEE Documentation: https://optee.readthedocs.io/

**社区资源**：
- Rockchip 开发者论坛
- U-Boot 邮件列表
- Linux Kernel 邮件列表（ARM64）

**工具源码**：
- 本项目：`uboot/tools/rockchip/`
- Rockchip rkbin 仓库：https://github.com/rockchip-linux/rkbin

---

## 结语

通过本教程，你应该已经理解了：

1. ✅ **ELF 文件格式的核心概念**（Header + Program Headers + Segments）
2. ✅ **Rockchip 固件格式的设计思想**（借鉴 ELF 但简化适配 BootROM）
3. ✅ **三大打包工具的功能和输出格式**（boot_merger / loaderimage / trust_merger）
4. ✅ **为什么 Rockchip 格式看起来像 ELF**（相同的"头部-表-数据"架构）
5. ✅ **如何分析和调试固件文件**（hexdump / readelf / 提取工具）

**关键洞察**：Rockchip 固件格式不是从零发明的，而是站在 ELF 这个成熟格式的肩膀上，针对嵌入式 BootROM 的需求进行了优化和简化。理解了 ELF，就能更好地理解 Rockchip 固件；掌握了打包工具，就能自由定制启动流程。

**下一步**：尝试修改打包工具，添加调试输出，甚至实现一个简单的 BootROM 模拟器，加深对启动流程的理解！

---

**文档版本**：v1.0
**作者**：基于 Rockchip 打包工具源码分析编写
**适用平台**：RK3399 / RK3328 / RK3368 / 通用 Rockchip SoC
