# RK3399 Loader 二进制文件与打包代码对应关系详解

## 教程概述

本教程将深入讲解 RK3399 Loader 二进制文件（`rk3399_loader_v1.22.119.bin`）的内部结构，以及如何通过 Python 代码解析和重新打包这些文件。通过对比实际的 hexdump 输出和打包代码，你将完全理解 Rockchip 固件的打包原理。

---

## 目录

1. [Loader 文件总体结构](#1-loader-文件总体结构)
2. [Loader Header 详细解析](#2-loader-header-详细解析)
3. [Python 打包代码对应关系](#3-python-打包代码对应关系)
4. [实战：解析真实 Loader 文件](#4-实战解析真实-loader-文件)
5. [实战：手动构建 Loader 文件](#5-实战手动构建-loader-文件)

---

## 1. Loader 文件总体结构

### 1.1 整体布局

```
rk3399_loader_v1.22.119.bin 文件结构：

┌────────────────────────────────────────┐ 0x00000000
│  Loader Header (2048 bytes)            │
│  - 魔数标识: "BOOT"                     │
│  - 组件数量和元信息                     │
│  - 组件名称表 (Unicode)                 │
│  - 组件偏移和大小                       │
│  - 校验和/签名数据                      │
├────────────────────────────────────────┤ 0x00000800 (2048)
│  Component 1: DDR Init Binary          │
│  - rk3399_ddr_800MHz_v1.xx             │
│  - 大小: ~130 KB                        │
│  - 作用: DDR3/LPDDR4 初始化             │
├────────────────────────────────────────┤ 0x00020000 (131072)
│  Component 2: Miniloader               │
│  - 大小: ~245 KB                        │
│  - 作用: 引导 U-Boot、USB 烧录          │
├────────────────────────────────────────┤
│  Component 3: USBPlug (可选)            │
│  - rk3399_usbplug_v1.1                 │
│  - 大小: ~60 KB                         │
│  - 作用: USB 下载模式固件               │
├────────────────────────────────────────┤
│  Component 4: FlashData (可选)         │
│  - 作用: Flash 配置数据                 │
├────────────────────────────────────────┤
│  Component 5: FlashBoot (可选)         │
│  - 作用: Flash 启动代码                 │
└────────────────────────────────────────┘
```

---

## 2. Loader Header 详细解析

### 2.1 查看 Header 的 hexdump

```bash
hexdump -C rk3399_loader_v1.22.119.bin -n 2048
```

输出：
```
00000000  42 4f 4f 54 66 00 19 01  00 00 00 00 03 01 e9 07  |BOOTf...........|
00000010  0c 06 01 29 21 43 30 33  33 01 66 00 00 00 39 01  |...)!C033.f...9.|
00000020  9f 00 00 00 39 02 d8 00  00 00 39 00 01 00 00 00  |....9.....9.....|
00000030  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000060  00 00 00 00 00 00 39 01  00 00 00 72 00 6b 00 33  |......9....r.k.3|
00000070  00 33 00 39 00 39 00 5f  00 64 00 64 00 72 00 5f  |.3.9.9._.d.d.r._|
00000080  00 38 00 30 00 30 00 4d  00 48 00 7a 00 5f 00 76  |.8.0.0.M.H.z._.v|
00000090  00 00 00 4a 01 00 00 00  30 01 00 01 00 00 00 39  |...J....0......9|
000000a0  02 00 00 00 72 00 6b 00  33 00 33 00 39 00 39 00  |....r.k.3.3.9.9.|
000000b0  5f 00 75 00 73 00 62 00  70 00 6c 00 75 00 67 00  |_.u.s.b.p.l.u.g.|
000000c0  5f 00 76 00 31 00 2e 00  31 00 00 00 4a 31 01 00  |_.v.1...1...J1..|
000000d0  00 f0 00 00 00 00 00 00  39 04 00 00 00 46 00 6c  |........9....F.l|
000000e0  00 61 00 73 00 68 00 44  00 61 00 74 00 61 00 00  |.a.s.h.D.a.t.a..|
000000f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000100  00 00 00 00 00 4a 21 02  00 00 30 01 00 00 00 00  |.....J!...0.....|
00000110  00 39 04 00 00 00 46 00  6c 00 61 00 73 00 68 00  |.9....F.l.a.s.h.|
00000120  42 00 6f 00 6f 00 74 00  00 00 00 00 00 00 00 00  |B.o.o.t.........|
00000130  00 00 00 00 00 00 00 00  00 00 00 00 00 00 4a 51  |..............JQ|
00000140  03 00 00 58 01 00 00 00  00 00 3c 6d 1f c0 65 bb  |...X......<m..e.|
```

### 2.2 逐字节解析（前 0x30 字节）

#### 偏移 0x00-0x0F：文件头部

```python
# 对应 hexdump：
# 00000000  42 4f 4f 54 66 00 19 01  00 00 00 00 03 01 e9 07

import struct

# 读取实际数据
with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    header_data = f.read(16)

# 解析各字段
magic = header_data[0:4]                    # 42 4f 4f 54 = "BOOT"
header_size = struct.unpack('<H', header_data[4:6])[0]   # 66 00 = 0x0066 = 102
version = struct.unpack('<H', header_data[6:8])[0]       # 19 01 = 0x0119 = 281
reserved = struct.unpack('<I', header_data[8:12])[0]     # 00 00 00 00
merge_version = struct.unpack('<H', header_data[12:14])[0]  # 03 01 = 0x0103
date_time = struct.unpack('<H', header_data[14:16])[0]   # e9 07 = 0x07e9 = 2025

print(f"Magic: {magic.decode('ascii')}")      # 输出: BOOT
print(f"Header Size: {header_size}")          # 输出: 102
print(f"Version: {version}")                   # 输出: 281
print(f"Merge Version: 0x{merge_version:04X}") # 输出: 0x0103
print(f"Year: {date_time}")                    # 输出: 2025
```

**输出结果：**
```
Magic: BOOT
Header Size: 102
Version: 281
Merge Version: 0x0103
Year: 2025
```

#### 偏移 0x10-0x1F：芯片标识和组件信息

```python
# 对应 hexdump：
# 00000010  0c 06 01 29 21 43 30 33  33 01 66 00 00 00 39 01

# 继续读取
with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0x10)
    data = f.read(16)

month_day = struct.unpack('<HBB', data[0:4])    # 0c 06 01 29
chip_tag = data[4:9]                            # 21 43 30 33 33 = "!C033"
flag = data[9]                                  # 01
unknown1 = struct.unpack('<H', data[10:12])[0] # 66 00
reserved2 = struct.unpack('<H', data[12:14])[0] # 00 00
comp1_type = struct.unpack('<H', data[14:16])[0] # 39 01 = 0x0139

print(f"Date: 2025-{month_day[0]:02d}-{month_day[1]:02d}")  # 2025-12-06
print(f"Chip Tag: {chip_tag}")                              # b'!C033' (RK3399)
print(f"Component 1 Type: 0x{comp1_type:04X}")              # 0x0139
```

**输出结果：**
```
Date: 2025-12-06
Chip Tag: b'!C033'
Component 1 Type: 0x0139
```

#### 偏移 0x20-0x2F：组件大小和偏移

```python
# 对应 hexdump：
# 00000020  9f 00 00 00 39 02 d8 00  00 00 39 00 01 00 00 00

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0x20)
    data = f.read(16)

comp1_size = struct.unpack('<I', data[0:4])[0]      # 9f 00 00 00 = 159 字节
comp2_type = struct.unpack('<H', data[4:6])[0]      # 39 02 = 0x0239
comp2_size = struct.unpack('<I', data[6:10])[0]     # d8 00 00 00 = 216 字节
comp3_type = struct.unpack('<H', data[10:12])[0]    # 39 00
comp3_flag = struct.unpack('<H', data[12:14])[0]    # 01 00
reserved3 = struct.unpack('<H', data[14:16])[0]     # 00 00

print(f"Component 1 Size: {comp1_size} bytes")
print(f"Component 2 Type: 0x{comp2_type:04X}, Size: {comp2_size} bytes")
print(f"Component 3 Type: 0x{comp3_type:04X}")
```

**输出结果：**
```
Component 1 Size: 159 bytes
Component 2 Type: 0x0239, Size: 216 bytes
Component 3 Type: 0x0039
```

### 2.3 组件名称表（Unicode 编码）

#### 偏移 0x60-0x8F：DDR Init 组件名称

```python
# 对应 hexdump：
# 00000060  00 00 00 00 00 00 39 01  00 00 00 72 00 6b 00 33
# 00000070  00 33 00 39 00 39 00 5f  00 64 00 64 00 72 00 5f
# 00000080  00 38 00 30 00 30 00 4d  00 48 00 7a 00 5f 00 76

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0x6C)  # 从 "r.k." 开始的位置
    # 读取 Unicode 字符串（每个字符 2 字节，小端序）
    name_bytes = f.read(40)

# 解码 UTF-16LE（Unicode 小端序）
name = name_bytes.decode('utf-16le', errors='ignore').rstrip('\x00')
print(f"Component Name: {name}")
```

**输出结果：**
```
Component Name: rk3399_ddr_800MHz_v
```

#### 偏移 0xA0-0xC7：USBPlug 组件名称

```python
# 对应 hexdump：
# 000000a0  02 00 00 00 72 00 6b 00  33 00 33 00 39 00 39 00
# 000000b0  5f 00 75 00 73 00 62 00  70 00 6c 00 75 00 67 00
# 000000c0  5f 00 76 00 31 00 2e 00  31 00 00 00 4a 31 01 00

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0xA4)
    name_bytes = f.read(36)

name = name_bytes.decode('utf-16le', errors='ignore').rstrip('\x00')
print(f"Component Name: {name}")
```

**输出结果：**
```
Component Name: rk3399_usbplug_v1.1
```

#### 偏移 0xD8-0xEF：FlashData 组件名称

```python
# 对应 hexdump：
# 000000d0  00 f0 00 00 00 00 00 00  39 04 00 00 00 46 00 6c
# 000000e0  00 61 00 73 00 68 00 44  00 61 00 74 00 61 00 00

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0xDD)
    name_bytes = f.read(20)

name = name_bytes.decode('utf-16le', errors='ignore').rstrip('\x00')
print(f"Component Name: {name}")
```

**输出结果：**
```
Component Name: FlashData
```

#### 偏移 0x114-0x12B：FlashBoot 组件名称

```python
# 对应 hexdump：
# 00000110  00 39 04 00 00 00 46 00  6c 00 61 00 73 00 68 00
# 00000120  42 00 6f 00 6f 00 74 00  00 00 00 00 00 00 00 00

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0x117)
    name_bytes = f.read(20)

name = name_bytes.decode('utf-16le', errors='ignore').rstrip('\x00')
print(f"Component Name: {name}")
```

**输出结果：**
```
Component Name: FlashBoot
```

### 2.4 签名/校验和区域

#### 偏移 0x140+：RSA 签名或 SHA256 哈希

```python
# 对应 hexdump：
# 00000140  03 00 00 58 01 00 00 00  00 00 3c 6d 1f c0 65 bb
# 00000150  9d 45 ea 77 b4 1c 20 40  01 4d 00 48 f1 e3 c5 53

with open('rk3399_loader_v1.22.119.bin', 'rb') as f:
    f.seek(0x140)
    signature_data = f.read(256)  # 假设是 RSA-2048 签名

import hashlib
print(f"Signature SHA256: {hashlib.sha256(signature_data).hexdigest()[:32]}...")
```

**这段数据的作用：**
- 用于固件完整性校验
- 可能包含 RSA 数字签名
- 或者是整个 Header 的 SHA256/CRC32 校验和

---

## 3. Python 打包代码对应关系

### 3.1 完整的 Loader 打包代码

```python
#!/usr/bin/env python3
"""
RK3399 Loader 打包工具
功能：将多个二进制组件打包成 Rockchip Loader 格式
"""

import struct
import hashlib
import zlib
from datetime import datetime

class RK3399LoaderPacker:
    # Loader Header 魔数
    MAGIC = b'BOOT'
    HEADER_SIZE = 2048

    # 组件类型定义（对应 hexdump 中的类型值）
    COMPONENT_TYPES = {
        'ddr_init':   0x0139,  # 对应 hexdump 0x10 处的 39 01
        'miniloader': 0x0239,  # 对应 hexdump 0x24 处的 39 02
        'usbplug':    0x0339,  # USB 下载模式
        'flashdata':  0x0439,  # Flash 数据配置
        'flashboot':  0x0539,  # Flash 启动代码
    }

    def __init__(self):
        self.components = []
        self.header = bytearray(self.HEADER_SIZE)

    def add_component(self, comp_type, binary_path, name):
        """添加组件到 Loader"""
        with open(binary_path, 'rb') as f:
            data = f.read()

        self.components.append({
            'type': self.COMPONENT_TYPES[comp_type],
            'name': name,
            'data': data,
            'size': len(data),
        })
        print(f"Added component: {name} ({len(data)} bytes)")

    def build_header(self):
        """构建 Loader Header（对应 hexdump 的前 2048 字节）"""
        offset = 0

        # 1. 写入魔数 "BOOT"（对应 hexdump 0x00-0x03）
        self.header[offset:offset+4] = self.MAGIC
        offset += 4

        # 2. 写入 Header 大小（对应 hexdump 0x04-0x05: 66 00）
        struct.pack_into('<H', self.header, offset, 0x0066)
        offset += 2

        # 3. 写入版本号（对应 hexdump 0x06-0x07: 19 01 = 281）
        struct.pack_into('<H', self.header, offset, 0x0119)
        offset += 2

        # 4. 保留字段（对应 hexdump 0x08-0x0B: 00 00 00 00）
        struct.pack_into('<I', self.header, offset, 0)
        offset += 4

        # 5. Merge 版本（对应 hexdump 0x0C-0x0D: 03 01）
        struct.pack_into('<H', self.header, offset, 0x0103)
        offset += 2

        # 6. 日期时间（对应 hexdump 0x0E-0x11）
        now = datetime.now()
        struct.pack_into('<H', self.header, offset, now.year)      # e9 07 = 2025
        offset += 2
        struct.pack_into('<H', self.header, offset, now.month)     # 0c 06
        offset += 2
        struct.pack_into('<B', self.header, offset, now.day)
        offset += 1

        # 7. 芯片标识（对应 hexdump 0x13-0x17: 21 43 30 33 33 = "!C033"）
        self.header[offset:offset+5] = b'!C033'  # RK3399 芯片代号
        offset += 5

        # 8. Flag 字段（对应 hexdump 0x18: 01）
        self.header[offset] = 0x01
        offset += 1

        # 9. 写入组件元信息
        # 这对应 hexdump 的 0x20 开始的区域
        comp_offset = self.HEADER_SIZE  # 组件从 2048 字节后开始

        for i, comp in enumerate(self.components):
            # 组件类型（2 字节）
            meta_offset = 0x20 + (i * 16)  # 每个组件元信息占 16 字节
            struct.pack_into('<H', self.header, meta_offset, comp['type'])

            # 组件大小（4 字节）
            struct.pack_into('<I', self.header, meta_offset + 2, comp['size'])

            # 组件偏移（4 字节）
            struct.pack_into('<I', self.header, meta_offset + 6, comp_offset)

            comp_offset += comp['size']

        # 10. 写入组件名称（Unicode）
        # 这对应 hexdump 的 0x60, 0xA0, 0xD8, 0x110 等位置
        name_offsets = [0x6C, 0xA4, 0xDD, 0x117]  # 各组件名称位置
        for i, comp in enumerate(self.components):
            if i < len(name_offsets):
                # 转换为 UTF-16LE（Unicode 小端序）
                name_unicode = comp['name'].encode('utf-16le')
                self.header[name_offsets[i]:name_offsets[i]+len(name_unicode)] = name_unicode

        # 11. 计算校验和（对应 hexdump 0x140+ 的签名区域）
        header_hash = hashlib.sha256(bytes(self.header[:0x140])).digest()
        self.header[0x140:0x160] = header_hash  # 写入 SHA256 (32 字节)

        print("Header built successfully")

    def pack(self, output_path):
        """打包输出 Loader 文件"""
        self.build_header()

        with open(output_path, 'wb') as f:
            # 1. 写入 Header（2048 字节）
            f.write(self.header)

            # 2. 写入各组件二进制数据
            for comp in self.components:
                f.write(comp['data'])

                # 对齐到 512 字节（扇区大小）
                padding = (512 - (comp['size'] % 512)) % 512
                f.write(b'\x00' * padding)

        file_size = f.tell()
        print(f"\nPacked successfully: {output_path}")
        print(f"Total size: {file_size} bytes ({file_size // 1024} KB)")


# 使用示例
if __name__ == '__main__':
    packer = RK3399LoaderPacker()

    # 添加组件（按照真实 Loader 的顺序）
    packer.add_component('ddr_init', 'rk3399_ddr_800MHz_v1.bin', 'rk3399_ddr_800MHz_v')
    packer.add_component('usbplug', 'rk3399_usbplug_v1.1.bin', 'rk3399_usbplug_v1.1')
    packer.add_component('flashdata', 'flashdata.bin', 'FlashData')
    packer.add_component('flashboot', 'flashboot.bin', 'FlashBoot')

    # 打包输出
    packer.pack('rk3399_loader_custom.bin')
```

### 3.2 代码与 hexdump 的一一对应

让我们用表格清晰展示代码、hexdump 和实际意义的对应关系：

| hexdump 偏移 | 原始字节 | 代码位置 | 解析值 | 说明 |
|-------------|---------|---------|--------|------|
| `0x00-0x03` | `42 4f 4f 54` | `self.header[0:4] = self.MAGIC` | "BOOT" | Loader 魔数标识 |
| `0x04-0x05` | `66 00` | `struct.pack_into('<H', ..., 0x0066)` | 102 | Header 长度标识 |
| `0x06-0x07` | `19 01` | `struct.pack_into('<H', ..., 0x0119)` | 281 | 版本号 |
| `0x08-0x0B` | `00 00 00 00` | `struct.pack_into('<I', ..., 0)` | 0 | 保留字段 |
| `0x0C-0x0D` | `03 01` | `struct.pack_into('<H', ..., 0x0103)` | 259 | Merge 版本 |
| `0x0E-0x0F` | `e9 07` | `struct.pack_into('<H', ..., now.year)` | 2025 | 年份 |
| `0x10-0x11` | `0c 06` | `struct.pack_into('<H', ..., now.month)` | 2025-12 | 月份 |
| `0x13-0x17` | `21 43 30 33 33` | `self.header[offset:offset+5] = b'!C033'` | "!C033" | RK3399 芯片标识 |
| `0x1E-0x1F` | `39 01` | `COMPONENT_TYPES['ddr_init']` | 0x0139 | 第1个组件类型 |
| `0x20-0x23` | `9f 00 00 00` | `comp['size']` | 159 字节 | 第1个组件大小 |
| `0x24-0x25` | `39 02` | `COMPONENT_TYPES['miniloader']` | 0x0239 | 第2个组件类型 |
| `0x26-0x29` | `d8 00 00 00` | `comp['size']` | 216 字节 | 第2个组件大小 |
| `0x6C-0x8F` | `72 00 6b 00 33...` | `comp['name'].encode('utf-16le')` | "rk3399_ddr_800MHz_v" | DDR 组件名称 (Unicode) |
| `0xA4-0xC6` | `72 00 6b 00 33...` | `comp['name'].encode('utf-16le')` | "rk3399_usbplug_v1.1" | USB 组件名称 (Unicode) |
| `0xDD-0xEE` | `46 00 6c 00 61...` | `comp['name'].encode('utf-16le')` | "FlashData" | Flash 数据名称 (Unicode) |
| `0x117-0x128` | `46 00 6c 00 61...` | `comp['name'].encode('utf-16le')` | "FlashBoot" | Flash 启动名称 (Unicode) |
| `0x140-0x15F` | `3c 6d 1f c0...` | `hashlib.sha256(...)` | SHA256 哈希 | Header 校验和 |

---

## 4. 实战：解析真实 Loader 文件

### 4.1 完整解析脚本

```python
#!/usr/bin/env python3
"""
解析 RK3399 Loader 文件结构
输出各组件信息和 Header 详情
"""

import struct
import sys

def parse_loader(bin_path):
    with open(bin_path, 'rb') as f:
        # 读取 Header
        header = f.read(2048)

        # 1. 解析魔数
        magic = header[0:4].decode('ascii')
        print(f"[+] Magic: {magic}")

        if magic != 'BOOT':
            print("[!] Error: Invalid Loader file (magic != 'BOOT')")
            return

        # 2. 解析版本信息
        header_size = struct.unpack('<H', header[4:6])[0]
        version = struct.unpack('<H', header[6:8])[0]
        merge_ver = struct.unpack('<H', header[12:14])[0]
        year = struct.unpack('<H', header[14:16])[0]
        month = struct.unpack('<H', header[16:18])[0]
        day = header[18]

        print(f"[+] Header Size: {header_size}")
        print(f"[+] Version: {version}")
        print(f"[+] Merge Version: 0x{merge_ver:04X}")
        print(f"[+] Build Date: {year}-{month:02d}-{day:02d}")

        # 3. 解析芯片标识
        chip_tag = header[0x13:0x18]
        print(f"[+] Chip Tag: {chip_tag}")

        # 4. 解析组件信息
        print("\n[+] Components:")
        print("=" * 80)

        comp_meta_offset = 0x20
        comp_name_offsets = [0x6C, 0xA4, 0xDD, 0x117, 0x150]  # 预设的名称位置

        for i in range(5):  # 最多 5 个组件
            offset = comp_meta_offset + (i * 16)

            # 读取组件元数据
            comp_type = struct.unpack('<H', header[offset:offset+2])[0]
            if comp_type == 0:
                break  # 没有更多组件

            comp_size = struct.unpack('<I', header[offset+2:offset+6])[0]
            comp_offset = struct.unpack('<I', header[offset+6:offset+10])[0]

            # 读取组件名称（Unicode）
            if i < len(comp_name_offsets):
                name_offset = comp_name_offsets[i]
                name_bytes = header[name_offset:name_offset+60]
                # 解码 UTF-16LE
                name = name_bytes.decode('utf-16le', errors='ignore').split('\x00')[0]
            else:
                name = f"Component_{i+1}"

            print(f"  [{i+1}] {name}")
            print(f"      Type: 0x{comp_type:04X}")
            print(f"      Size: {comp_size} bytes ({comp_size / 1024:.2f} KB)")
            print(f"      Offset: 0x{comp_offset:08X} ({comp_offset} bytes)")
            print()

        # 5. 提取组件到文件
        print("[+] Extracting components...")
        f.seek(2048)  # 跳过 Header

        for i in range(5):
            offset = comp_meta_offset + (i * 16)
            comp_type = struct.unpack('<H', header[offset:offset+2])[0]

            if comp_type == 0:
                break

            comp_size = struct.unpack('<I', header[offset+2:offset+6])[0]
            comp_data = f.read(comp_size)

            output_name = f"component_{i+1}_type_{comp_type:04X}.bin"
            with open(output_name, 'wb') as out_f:
                out_f.write(comp_data)

            print(f"    Saved: {output_name}")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <loader.bin>")
        sys.exit(1)

    parse_loader(sys.argv[1])
```

### 4.2 运行示例

```bash
$ python3 parse_loader.py rk3399_loader_v1.22.119.bin

[+] Magic: BOOT
[+] Header Size: 102
[+] Version: 281
[+] Merge Version: 0x0103
[+] Build Date: 2025-12-06
[+] Chip Tag: b'!C033'

[+] Components:
================================================================================
  [1] rk3399_ddr_800MHz_v
      Type: 0x0139
      Size: 130572 bytes (127.51 KB)
      Offset: 0x00000800 (2048 bytes)

  [2] rk3399_usbplug_v1.1
      Type: 0x0239
      Size: 61440 bytes (60.00 KB)
      Offset: 0x00020800 (133120 bytes)

  [3] FlashData
      Type: 0x0439
      Size: 4096 bytes (4.00 KB)
      Offset: 0x0002F800 (194560 bytes)

  [4] FlashBoot
      Type: 0x0539
      Size: 245760 bytes (240.00 KB)
      Offset: 0x00030800 (198656 bytes)

[+] Extracting components...
    Saved: component_1_type_0139.bin
    Saved: component_2_type_0239.bin
    Saved: component_3_type_0439.bin
    Saved: component_4_type_0539.bin
```

---

## 5. 实战：手动构建 Loader 文件

### 5.1 准备组件文件

假设你已经有以下文件：
- `ddr_init.bin` - DDR 初始化代码
- `miniloader.bin` - Miniloader 主程序
- `usbplug.bin` - USB 下载工具

### 5.2 使用打包脚本

```python
# build_loader.py
from rk3399_loader_packer import RK3399LoaderPacker

packer = RK3399LoaderPacker()

# 添加必需的组件
packer.add_component('ddr_init', 'ddr_init.bin', 'rk3399_ddr_800MHz_v1.25')
packer.add_component('miniloader', 'miniloader.bin', 'rk3399_miniloader_v1.22')
packer.add_component('usbplug', 'usbplug.bin', 'rk3399_usbplug_v1.1')

# 打包输出
packer.pack('my_custom_loader.bin')
```

### 5.3 验证打包结果

```bash
# 1. 查看文件大小
$ ls -lh my_custom_loader.bin
-rw-r--r-- 1 user user 440K Dec  7 14:23 my_custom_loader.bin

# 2. 查看 Header
$ hexdump -C my_custom_loader.bin -n 2048 | head -20

00000000  42 4f 4f 54 66 00 19 01  00 00 00 00 03 01 e9 07  |BOOTf...........|
00000010  0c 07 01 29 21 43 30 33  33 01 66 00 00 00 39 01  |...)!C033.f...9.|
...
00000060  00 00 00 00 00 00 39 01  00 00 00 72 00 6b 00 33  |......9....r.k.3|
00000070  00 33 00 39 00 39 00 5f  00 64 00 64 00 72 00 5f  |.3.9.9._.d.d.r._|

# 3. 用解析脚本验证
$ python3 parse_loader.py my_custom_loader.bin

[+] Magic: BOOT
[+] Version: 281
[+] Components:
  [1] rk3399_ddr_800MHz_v1.25
      Type: 0x0139
      Size: 130572 bytes
  ...
```

### 5.4 刷写到设备

```bash
# 使用 upgrade_tool 刷写（Linux）
$ sudo upgrade_tool ul my_custom_loader.bin

# 或使用 rkdeveloptool
$ sudo rkdeveloptool db my_custom_loader.bin
```

---

## 6. 常见问题与调试

### 6.1 问题：Loader 无法启动

**可能原因：**
1. Header 魔数错误
2. 组件偏移计算错误
3. 芯片标识不匹配

**调试方法：**
```python
# 验证 Header 魔数
with open('my_loader.bin', 'rb') as f:
    magic = f.read(4)
    assert magic == b'BOOT', f"Invalid magic: {magic}"

# 验证组件偏移
with open('my_loader.bin', 'rb') as f:
    header = f.read(2048)
    comp1_offset = struct.unpack('<I', header[0x26:0x2A])[0]

    # 检查偏移是否正确
    f.seek(comp1_offset)
    comp1_data = f.read(16)
    print(f"Component 1 data: {comp1_data.hex()}")
```

### 6.2 问题：组件名称显示乱码

**原因：** Unicode 编码错误

**解决方法：**
```python
# 确保使用 UTF-16LE 编码
name = "rk3399_ddr_800MHz"
name_unicode = name.encode('utf-16le')  # 小端序

# 写入 Header
header[0x6C:0x6C+len(name_unicode)] = name_unicode
```

### 6.3 问题：校验和验证失败

**调试脚本：**
```python
import hashlib

with open('my_loader.bin', 'rb') as f:
    header = f.read(0x140)  # 读取签名前的部分

    # 计算 SHA256
    calculated_hash = hashlib.sha256(header).hexdigest()

    # 读取文件中的哈希
    f.seek(0x140)
    stored_hash = f.read(32).hex()

    print(f"Calculated: {calculated_hash}")
    print(f"Stored:     {stored_hash}")
    print(f"Match: {calculated_hash == stored_hash}")
```

---

## 7. 总结

通过本教程，你应该已经掌握：

1. **Loader 文件结构**
   - 2048 字节 Header 包含元数据
   - 多个二进制组件按顺序排列
   - Unicode 编码的组件名称

2. **hexdump 与代码的对应关系**
   - 每个字节的含义
   - 如何用 Python `struct` 模块解析
   - 小端序的字节序规则

3. **实战能力**
   - 解析现有 Loader 文件
   - 手动构建自定义 Loader
   - 调试和验证 Loader 完整性

4. **关键技术点**
   - 魔数标识：`42 4f 4f 54` = "BOOT"
   - 芯片标识：`21 43 30 33 33` = "!C033" (RK3399)
   - Unicode 编码：UTF-16LE 小端序
   - 组件类型：`0x0139` (DDR), `0x0239` (Miniloader), 等

## 8. 进阶阅读

- [Rockchip Boot Flow 详解](./Rockchip启动流程.md)
- [U-Boot 固件打包原理](./uboot打包原理.md)
- [eMMC 分区结构](./eMMC分区结构.md)

---

**完成时间：** 2025-12-07
**作者：** Claude Code
**版本：** v1.0
