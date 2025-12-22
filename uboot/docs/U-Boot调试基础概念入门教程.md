# U-Boot 调试基础概念入门教程

## 目录
1. [从一个简单的程序说起](#从一个简单的程序说起)
2. [什么是符号表（Symbol Table）](#什么是符号表symbol-table)
3. [什么是 ELF 文件](#什么是-elf-文件)
4. [什么是地址（Address）](#什么是地址address)
5. [什么是 U-Boot 重定位（Relocation）](#什么是-u-boot-重定位relocation)
6. [什么是 addr2line 工具](#什么是-addr2line-工具)
7. [什么是调试信息（Debug Info）](#什么是调试信息debug-info)
8. [实战：完整的调试流程](#实战完整的调试流程)
9. [总结与速查表](#总结与速查表)

---

## 从一个简单的程序说起

### 最简单的 C 程序

让我们从一个最简单的 "Hello World" 程序开始：

```c
// hello.c
#include <stdio.h>

void say_hello(void) {
    printf("Hello, World!\n");
}

int main(void) {
    say_hello();
    return 0;
}
```

### 编译过程

当我们编译这个程序时，会发生什么？

```bash
# 编译生成可执行文件
gcc -g -o hello hello.c

# 查看文件类型
file hello
# 输出：hello: ELF 64-bit LSB executable, x86-64, version 1 (SYSV)
```

这个 `hello` 文件就是一个 **ELF 文件**（我们稍后详细讲解）。

### 运行程序

```bash
./hello
# 输出：Hello, World!
```

看起来很简单，但背后发生了很多事情：

1. **加载器**（Loader）将程序从磁盘加载到内存
2. **操作系统**为程序分配内存空间
3. **CPU** 开始执行程序的第一条指令
4. 程序调用 `main()` 函数
5. `main()` 调用 `say_hello()` 函数
6. `say_hello()` 调用 `printf()` 函数
7. 程序退出

### 关键问题

现在假设程序崩溃了：

```bash
./hello
Segmentation fault (core dumped)
```

作为开发者，你想知道：
- **在哪里崩溃**？（哪个函数？哪一行代码？）
- **为什么崩溃**？（访问了非法内存？除以零？）

这就是为什么需要**符号表**、**调试信息**和 **addr2line** 工具！

---

## 什么是符号表（Symbol Table）

### 概念解释

**符号表**（Symbol Table）就像一本**字典**，记录了程序中所有函数和变量的名字及其对应的内存地址。

```
函数名/变量名  →  内存地址
say_hello      →  0x00001234
main           →  0x00001280
printf         →  0x00001300
```

### 为什么需要符号表？

#### 场景 1：CPU 只认识地址，不认识函数名

当程序崩溃时，CPU 告诉你：
```
崩溃地址：0x00001234
```

但你看到 `0x00001234` 毫无意义，你需要知道这是哪个函数！

**符号表的作用：**
```
查找 0x00001234 → 找到 "say_hello" 函数
```

#### 场景 2：调试器需要符号表

GDB 调试器为什么能显示函数名？因为它读取了符号表：

```bash
(gdb) bt
#0  say_hello () at hello.c:4
#1  main () at hello.c:9
```

### 如何查看符号表？

#### 方法 1：使用 nm 命令

```bash
nm hello
```

**输出示例：**
```
0000000000001149 T main
0000000000001135 T say_hello
                 U printf@@GLIBC_2.2.5
0000000000004010 B __bss_start
0000000000004010 b completed.8060
                 w __cxa_finalize@@GLIBC_2.2.5
0000000000001050 t deregister_tm_clones
```

**字段解释：**
- `0000000000001149` - 内存地址（十六进制）
- `T` - 符号类型：
  - `T` = Text（代码段，全局函数）
  - `t` = Text（代码段，局部函数）
  - `D` = Data（数据段，全局变量）
  - `B` = BSS（未初始化数据段）
  - `U` = Undefined（外部引用，如 printf）
- `main` - 函数名

#### 方法 2：使用 readelf 命令

```bash
readelf -s hello
```

**输出示例：**
```
Symbol table '.symtab' contains 67 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
    50: 0000000000001135    20 FUNC    GLOBAL DEFAULT   14 say_hello
    51: 0000000000001149    45 FUNC    GLOBAL DEFAULT   14 main
    52: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
```

**字段解释：**
- `Value` - 地址：`0x1135`
- `Size` - 大小：`20` 字节（say_hello 函数占 20 字节）
- `Type` - 类型：`FUNC`（函数）
- `Bind` - 绑定：`GLOBAL`（全局可见）
- `Name` - 名称：`say_hello`

### U-Boot 的符号表

U-Boot 编译后会生成 `u-boot.sym` 文件：

```bash
cat uboot/u-boot.sym
```

**输出示例：**
```
00000000 t _start
00000020 t reset
00200000 T _start
00200040 T relocate_code
00210abc T board_init_f
00210b20 T board_init_r
00220100 T do_bootm
00220234 T do_bootm_linux
```

**解读：**
- `board_init_f` 函数的地址是 `0x00210abc`
- `do_bootm_linux` 函数的地址是 `0x00220234`
- 函数大小 = 下一个函数地址 - 当前函数地址
  - `board_init_f` 大小 = `0x00210b20 - 0x00210abc` = `0x64` (100 字节)

---

## 什么是 ELF 文件

### ELF 是什么？

**ELF** = **Executable and Linkable Format**（可执行与可链接格式）

它是 Linux 系统中可执行文件、目标文件和共享库的标准格式。

### ELF 文件的结构

```
┌─────────────────────────────────┐
│   ELF Header (文件头)            │  ← 描述文件类型、架构、入口地址
├─────────────────────────────────┤
│   Program Header Table          │  ← 描述运行时内存布局
├─────────────────────────────────┤
│   .text Section (代码段)         │  ← 存放函数的机器码
├─────────────────────────────────┤
│   .data Section (数据段)         │  ← 存放全局变量
├─────────────────────────────────┤
│   .bss Section (未初始化数据段)   │  ← 存放未初始化的全局变量
├─────────────────────────────────┤
│   .rodata Section (只读数据段)    │  ← 存放字符串常量
├─────────────────────────────────┤
│   .symtab Section (符号表)        │  ← 存放函数名和地址的对应关系
├─────────────────────────────────┤
│   .debug_* Sections (调试信息)    │  ← 存放源代码行号等调试信息
├─────────────────────────────────┤
│   Section Header Table          │  ← 描述所有 Section 的元数据
└─────────────────────────────────┘
```

### 查看 ELF 文件头

```bash
readelf -h hello
```

**输出示例：**
```
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x1050         ← 程序入口地址
  Start of program headers:          64 (bytes into file)
  Start of section headers:          13816 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
```

**关键信息：**
- `Entry point address: 0x1050` - 程序启动时第一条指令的地址
- `Machine: X86-64` - 适用于 x86-64 架构（ARM 平台会显示 ARM 或 AArch64）

### 查看 ELF 的段（Sections）

```bash
readelf -S hello
```

**输出示例：**
```
Section Headers:
  [Nr] Name              Type             Address           Offset    Size
  [ 0]                   NULL             0000000000000000  00000000  0000000000000000
  [13] .text             PROGBITS         0000000000001050  00001050  00000000000001a5  ← 代码段
  [14] .rodata           PROGBITS         0000000000002000  00002000  000000000000000f  ← 只读数据
  [23] .data             PROGBITS         0000000000004000  00003000  0000000000000010  ← 数据段
  [24] .bss              NOBITS           0000000000004010  00003010  0000000000000008  ← 未初始化数据
  [26] .symtab           SYMTAB           0000000000000000  00003018  0000000000000660  ← 符号表
  [27] .strtab           STRTAB           0000000000000000  00003678  00000000000001f8  ← 字符串表
  [28] .debug_info       PROGBITS         0000000000000000  00003870  00000000000000d6  ← 调试信息
```

**重点关注：**
- `.text` - 存放函数的机器码（如 `say_hello`、`main` 的汇编代码）
- `.symtab` - 符号表（函数名和地址的映射）
- `.debug_info` - 调试信息（源文件名、行号等）

### U-Boot 的 ELF 文件

```bash
readelf -h uboot/u-boot
```

**输出示例：**
```
ELF Header:
  Class:                             ELF64
  Machine:                           AArch64            ← ARM 64 位架构
  Entry point address:               0x200000           ← U-Boot 启动地址
```

---

## 什么是地址（Address）

### 三种地址概念

在嵌入式系统中，需要区分三种地址：

#### 1. 物理地址（Physical Address）

**定义：** 硬件内存芯片的实际地址

```
RK3399 内存布局（物理地址）：
0x00000000 - 0x0000FFFF   BootROM (芯片内部固化的启动代码)
0x00010000 - 0x0003FFFF   SRAM (片上静态内存，192KB)
0x00200000 - 0x003FFFFF   SPL 加载区域
0xF8000000 - 0xFBFFFFFF   DRAM (外部 DDR 内存，4GB)
```

**特点：**
- 由硬件决定，无法更改
- CPU 直接访问的地址

#### 2. 链接地址（Link Address）

**定义：** 编译器链接时指定的地址

```c
// U-Boot 链接脚本 (arch/arm/cpu/armv8/u-boot.lds)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
    . = 0x00200000;    ← 链接地址（编译时指定）
    .text : {
        *(.text*)
    }
    ...
}
```

**示例：**
```bash
# 编译时，函数被分配到链接地址
board_init_f 编译后地址: 0x00210abc
do_bootm_linux 编译后地址: 0x00220234
```

**特点：**
- 编译时确定
- 写在 ELF 文件中
- 符号表中记录的就是链接地址

#### 3. 运行地址（Runtime Address / Load Address）

**定义：** 程序实际运行时的内存地址

```
编译时：board_init_f 链接到 0x00210abc
运行时：board_init_f 实际加载到 0xFFF10abc  ← 发生了重定位！
```

**为什么会不同？** → 这就引出了 **重定位** 的概念！

---

## 什么是 U-Boot 重定位（Relocation）

### 为什么需要重定位？

#### 问题场景

假设 U-Boot 编译时链接地址是 `0x00200000`：

```
编译时内存布局：
0x00200000 ┌───────────────┐
           │   U-Boot      │  ← 链接地址
0x0025FFFF └───────────────┘
```

但在运行时，内核也需要这块内存：

```
运行时内存冲突：
0x00200000 ┌───────────────┐
           │   U-Boot      │  ← 已经加载
0x0025FFFF └───────────────┘
           │               │
           │   需要加载    │
           │   Linux Kernel│  ← 内核也想用这块地址！
           │               │
           └───────────────┘
```

**冲突！** U-Boot 和内核抢同一块内存。

#### 解决方案：重定位

U-Boot 将自己移动到 DRAM 的**高地址**：

```
重定位后的内存布局：
0x00200000 ┌───────────────┐
           │  (空闲)        │  ← 留给内核使用
0x0025FFFF └───────────────┘
           │               │
           │  (空闲)        │
           │               │
0xFFF00000 ┌───────────────┐
           │   U-Boot      │  ← 重定位到高地址
0xFFF5FFFF └───────────────┘
           │  Stack & Heap  │
0xFFFFFFFF └───────────────┘
```

### 重定位过程详解

#### 第 1 步：计算重定位地址

U-Boot 启动后，在 `board_init_f()` 函数中计算：

```c
// common/board_f.c
static int setup_dest_addr(void)
{
    // 1. 获取 DRAM 顶部地址
    gd->ram_top = board_get_usable_ram_top(gd->mon_len);

    // 2. 计算重定位目标地址
    gd->relocaddr = gd->ram_top;              // 从 DRAM 顶部开始
    gd->relocaddr -= gd->mon_len;              // 减去 U-Boot 大小
    gd->relocaddr &= ~(4096 - 1);              // 4KB 对齐
    gd->relocaddr -= CONFIG_SYS_MALLOC_LEN;    // 预留堆空间
    gd->relocaddr -= sizeof(struct global_data); // 预留全局数据

    // 3. 计算重定位偏移量
    gd->reloc_off = gd->relocaddr - CONFIG_SYS_TEXT_BASE;

    debug("Relocation Offset is: %08lx\n", gd->reloc_off);

    return 0;
}
```

**实际计算示例：**
```
假设：
- DRAM 大小 = 4GB (0x100000000)
- U-Boot 大小 = 0x60000 (384KB)
- 链接地址 (CONFIG_SYS_TEXT_BASE) = 0x00200000

计算过程：
1. ram_top        = 0x100000000
2. relocaddr      = 0x100000000 - 0x60000 = 0xFFFA0000
3. relocaddr (对齐) = 0xFFFA0000 & ~0xFFF = 0xFFFA0000
4. reloc_off      = 0xFFFA0000 - 0x00200000 = 0xFFC00000

结论：
- U-Boot 将被重定位到 0xFFFA0000
- 重定位偏移量是 0xFFC00000
```

#### 第 2 步：拷贝代码和数据

```c
// arch/arm/lib/relocate.S
ENTRY(relocate_code)
    // r0 = 新地址 (gd->relocaddr)
    // r1 = 旧地址 (CONFIG_SYS_TEXT_BASE)
    // r2 = 拷贝大小

copy_loop:
    ldmia   r1!, {r10-r11}     // 从旧地址读取数据
    stmia   r0!, {r10-r11}     // 写入新地址
    cmp     r0, r2             // 是否拷贝完成？
    blo     copy_loop          // 继续循环

    // ... 后续修正重定位表
ENDPROC(relocate_code)
```

**内存变化：**
```
拷贝前：
0x00200000 ┌──────────────┐
           │  U-Boot 代码  │  ← 原始位置
0x0025FFFF └──────────────┘

拷贝后：
0x00200000 ┌──────────────┐
           │  U-Boot 代码  │  ← 旧副本（即将被覆盖）
0x0025FFFF └──────────────┘
           ...
0xFFFA0000 ┌──────────────┐
           │  U-Boot 代码  │  ← 新副本（正在运行）
0xFFFFFFF └──────────────┘
```

#### 第 3 步：修正地址引用

**问题：** 代码中有硬编码的地址怎么办？

```c
// 编译时生成的代码（链接地址）
board_init_f:
    bl   0x00210b20   ← 调用 board_init_r，地址是编译时的
```

**重定位后：** `board_init_r` 已经移动到 `0xFFFA0000 + (0x00210b20 - 0x00200000)`

**解决方案：** 修正重定位表（`.rel.dyn` 段）

```c
// arch/arm/lib/relocate.S (续)
fixup_loop:
    ldr     r0, [r2]        // 读取需要修正的地址
    add     r0, r0, r4      // 加上重定位偏移量
    str     r0, [r2], #8    // 写回修正后的地址
    cmp     r2, r3
    blo     fixup_loop
```

**修正示例：**
```
原始地址：0x00210b20
+ 偏移量： 0xFFC00000
= 新地址： 0xFFE10B20
```

#### 第 4 步：跳转到新位置继续执行

```c
// common/board_f.c
void board_init_f_r(void)
{
    // ... 初始化完成
    board_init_r(NULL, gd->relocaddr);  ← 跳转到新地址
}
```

### 重定位对调试的影响

#### 启动日志示例

```
U-Boot 2017.09 (Dec 16 2025 - 10:23:45)

Model: Rockchip RK3399 Evaluation Board
DRAM:  4 GiB
Relocation Offset is: 3df44000   ← 关键信息！
WARNING: Caches not enabled
...
```

**关键点：** 记住这个 `Relocation Offset`，调试时需要用！

#### 地址转换公式

```
运行时地址 = 链接时地址 + 重定位偏移量

示例：
board_init_f 链接地址: 0x00210abc
+ 重定位偏移量:        0x3df44000
= 运行时地址:          0x3F654abc
```

**反向计算（调试时常用）：**
```
链接时地址 = 运行时地址 - 重定位偏移量

示例（崩溃地址查询）：
崩溃地址:     0xfff59abc
- 重定位偏移:  0x3ff57000
= 原始地址:    0xc0002abc  ← 在符号表中查找这个地址
```

### 验证重定位

```bash
# 1. 查看链接地址
grep "board_init_f" uboot/u-boot.sym
# 输出：00210abc T board_init_f

# 2. 在 U-Boot 串口中查看运行地址
=> md.q 0x3F654abc 1
3f654abc: a9be7bfd910003fd    ............

# 3. 对比链接地址的内容
=> md.q 0x00210abc 1
00210abc: a9be7bfd910003fd    ............

# 内容相同 ✓ 重定位成功！
```

---

## 什么是 addr2line 工具

### 工具简介

**addr2line** 是 GNU Binutils 工具集的一部分，用于将**内存地址**转换为**源代码位置**（文件名和行号）。

### 基本用法

```bash
# 语法
addr2line -e <elf_file> <address>

# 示例
aarch64-linux-gnu-addr2line -e uboot/u-boot 0x00210abc
```

**输出：**
```
common/board_f.c:1023
```

**解读：**
- 地址 `0x00210abc` 对应源文件 `common/board_f.c` 的第 `1023` 行

### 工作原理

#### 第 1 步：读取 ELF 文件中的调试信息

编译时需要加上 `-g` 选项：

```bash
# 编译时生成调试信息
gcc -g -o hello hello.c
```

这会在 ELF 文件中嵌入 **DWARF** 调试信息：

```bash
readelf --debug-dump=line hello | head -20
```

**输出示例：**
```
Contents of the .debug_line section:

  Offset:                      0x0
  Length:                      138
  DWARF Version:               4
  Prologue Length:             33
  Minimum Instruction Length:  1
  Maximum Ops per Instruction: 1
  Initial value of 'is_stmt':  1
  Line Base:                   -5
  Line Range:                  14

 The Directory Table:
  /home/user/project

 The File Name Table:
  Entry Dir     Time    Size    Name
  1     1       0       0       hello.c

 Line Number Statements:
  [0x00001135]  Extended opcode 2: set Address to 0x1135
  [0x00001135]  Special opcode 6: advance Address by 0 to 0x1135 and Line by 3 to 4
  [0x00001139]  Special opcode 64: advance Address by 4 to 0x1139 and Line by 1 to 5
  ...
```

**关键映射：**
```
地址 0x1135  →  hello.c 第 4 行
地址 0x1139  →  hello.c 第 5 行
```

#### 第 2 步：地址查找

addr2line 读取 DWARF 信息，查找地址范围：

```
输入地址: 0x1135

查找 .debug_line：
  0x1135 - 0x1138  →  hello.c:4
  0x1139 - 0x113c  →  hello.c:5

匹配: 0x1135 在 [0x1135, 0x1138] 范围内
返回: hello.c:4
```

### 常用选项

#### 1. `-f` 显示函数名

```bash
aarch64-linux-gnu-addr2line -e u-boot -f 0x00210abc
```

**输出：**
```
board_init_f
common/board_f.c:1023
```

#### 2. `-i` 显示内联函数

```bash
aarch64-linux-gnu-addr2line -e u-boot -i 0x00210abc
```

**输出：**
```
setup_dest_addr
common/board_f.c:234
board_init_f
common/board_f.c:1023
```

**解读：** `setup_dest_addr` 被内联到 `board_init_f` 中。

#### 3. `-C` 解码 C++ 符号

```bash
# C++ 函数名会被 mangle（混淆）
nm hello_cpp | grep foo
# 输出：_Z3foov

# 使用 -C 选项解码
addr2line -e hello_cpp -C -f 0x1234
# 输出：
# foo()
# hello.cpp:10
```

### 批量查询

```bash
# 使用管道批量查询
cat addresses.txt
# 内容：
# 0x00210abc
# 0x00210b20
# 0x00220100

cat addresses.txt | while read addr; do
    echo "=== $addr ==="
    aarch64-linux-gnu-addr2line -e u-boot -f $addr
done
```

### U-Boot 中的应用

#### 场景：SPL 崩溃调试

```
U-Boot SPL 2017.09
data abort
pc : [<0002e9f8>]
```

**查询命令：**
```bash
cd uboot/
aarch64-linux-gnu-addr2line -e spl/u-boot-spl -f 0x0002e9f8
```

**输出：**
```
board_init_f
common/spl/spl.c:423
```

---

## 什么是调试信息（Debug Info）

### 调试信息的种类

#### 1. DWARF 格式（主流）

**全称：** Debugging With Attributed Record Formats

**存储位置：** ELF 文件的 `.debug_*` 段

```bash
readelf -S u-boot | grep debug
```

**输出：**
```
[28] .debug_aranges    PROGBITS         0000000000000000  00123000  00002340
[29] .debug_info       PROGBITS         0000000000000000  00125340  0012a456
[30] .debug_abbrev     PROGBITS         0000000000000000  0024f796  00015678
[31] .debug_line       PROGBITS         0000000000000000  00264e0e  0008abcd
[32] .debug_str        PROGBITS         0000000000000000  002ef9db  00034567
[33] .debug_loc        PROGBITS         0000000000000000  00323f42  0009abcd
[34] .debug_ranges     PROGBITS         0000000000000000  003bda0f  00012345
```

**各段说明：**
- `.debug_info` - 变量类型、函数原型等
- `.debug_line` - 地址与源代码行号的映射
- `.debug_abbrev` - 缩写表（压缩调试信息）
- `.debug_str` - 字符串表（源文件名等）
- `.debug_loc` - 变量在寄存器/内存中的位置
- `.debug_ranges` - 地址范围信息

#### 2. 符号表（Symbol Table）

**存储位置：** `.symtab` 和 `.strtab` 段

```bash
readelf -s u-boot | grep board_init_f
```

**输出：**
```
  1234: 00210abc   256 FUNC    GLOBAL DEFAULT   13 board_init_f
```

### 编译选项对调试信息的影响

#### `-g` 生成调试信息

```bash
# 不加 -g
gcc -o hello hello.c
ls -lh hello
# 输出：-rwxr-xr-x 1 user user 16K Dec 16 10:00 hello

# 加 -g
gcc -g -o hello hello.c
ls -lh hello
# 输出：-rwxr-xr-x 1 user user 24K Dec 16 10:01 hello  ← 增加了 8KB 调试信息
```

#### `-O2` 优化对调试的影响

**问题：** 优化会改变代码结构，导致调试困难

```c
// 原始代码
void foo() {
    int a = 1;
    int b = 2;
    int c = a + b;
    printf("%d\n", c);
}

// 编译时 -O0（无优化）
foo:
    push   {r7}
    sub    sp, sp, #12
    mov    r3, #1        ← a = 1
    str    r3, [sp, #8]
    mov    r3, #2        ← b = 2
    str    r3, [sp, #4]
    ldr    r2, [sp, #8]  ← 读取 a
    ldr    r3, [sp, #4]  ← 读取 b
    add    r3, r2, r3    ← c = a + b
    ...

// 编译时 -O2（优化）
foo:
    push   {r7, lr}
    movs   r1, #3        ← 直接计算出结果 c = 3
    ldr    r0, =.LC0
    bl     printf
    pop    {r7, pc}
```

**影响：**
- 优化后变量 `a`、`b` 消失，无法在调试器中查看
- 代码行号可能不准确（指令重排）
- 内联函数会合并到调用者

**U-Boot 配置：**
```bash
# 查看 U-Boot 编译选项
grep CONFIG_DEBUG_INFO .config
# 输出：CONFIG_DEBUG_INFO=y  ← 启用调试信息

grep CONFIG_CC_OPTIMIZE .config
# 输出：CONFIG_CC_OPTIMIZE_FOR_SIZE=y  ← 优化以减小体积
```

#### `strip` 移除调试信息

```bash
# 移除符号表和调试信息
strip -s hello

# 对比大小
ls -lh hello*
# 输出：
# -rwxr-xr-x 1 user user 24K Dec 16 10:01 hello
# -rwxr-xr-x 1 user user 14K Dec 16 10:02 hello (stripped)  ← 减少了 10KB
```

**注意：** 生产环境的固件通常会 strip，调试时需要保留原始 ELF 文件！

### U-Boot 的调试信息配置

```bash
# 启用调试信息
make menuconfig
# 进入：Compiler options → Enable debugging information (CONFIG_DEBUG_INFO)

# 或修改 .config
echo "CONFIG_DEBUG_INFO=y" >> .config

# 重新编译
make clean
./make.sh rk3399
```

---

## 实战：完整的调试流程

### 场景设定

**问题：** OrangePi RK3399 启动失败，U-Boot 崩溃

**串口日志：**
```
U-Boot 2017.09-ge934607-dirty (Dec 16 2025 - 15:32:18)

Model: Rockchip RK3399 Evaluation Board
DRAM:  4 GiB
Relocation Offset is: 3df44000
MMC:   dwmmc@fe320000: 1
*** Warning - bad CRC, using default environment

In:    serial@ff1a0000
Out:   serial@ff1a0000
Err:   serial@ff1a0000
Net:   eth0: ethernet@fe300000
Hit any key to stop autoboot:  0

Starting kernel ...

"Synchronous Abort" handler, esr 0x96000045
ELR:     ffd4c234
LR:      ffd4c200
x0 : 0000000012345678 x1 : 0000000000000000
x2 : 0000000000000000 x3 : 0000000000000000
x4 : 00000000f8f3fb88 x5 : 0000000000000000
x6 : 00000000ffd9a234 x7 : 0000000000000000
x8 : 00000000ffd6c8d0 x9 : 0000000000000008
x10: 0000000000000010 x11: 0000000000001020
x12: 000000000001d4c0 x13: 0000000000001d20
x14: 00000000f8f3f890 x15: 0000000000000002
x16: 00000000ffd8b5d8 x17: 0000000000000000
x18: 00000000f8f3fdb0 x19: 0000000000000000
x20: 00000000f8f3fc30 x21: 0000000000000000
x22: 0000000000000000 x23: 0000000000000000
x24: 0000000000000000 x25: 0000000000000000
x26: 0000000000000000 x27: 0000000000000000
x28: 0000000000000000 x29: 0000000000000000

Resetting CPU ...

resetting ...
```

### 调试步骤

#### 步骤 1：提取关键信息

```
崩溃地址 (ELR): 0xffd4c234
调用者 (LR):   0xffd4c200
重定位偏移:     0x3df44000
```

**关键寄存器：**
- `ELR` (Exception Link Register) - 异常发生时的程序计数器（相当于 ARM32 的 PC）
- `LR` (Link Register) - 返回地址（调用者）
- `x0` - 第一个参数 = `0x12345678`（看起来像魔数，非法地址！）

#### 步骤 2：计算链接地址

```bash
# 运行地址转链接地址
python3 << EOF
elr = 0xffd4c234
reloc_off = 0x3df44000
link_addr = elr - reloc_off
print(f"ELR 链接地址: 0x{link_addr:08x}")

lr = 0xffd4c200
link_lr = lr - reloc_off
print(f"LR 链接地址: 0x{link_lr:08x}")
EOF
```

**输出：**
```
ELR 链接地址: 0xc0408234
LR 链接地址: 0xc0408200
```

**或使用 make.sh 自动计算：**
```bash
cd uboot/
./make.sh 0xffd4c234-0x3df44000
```

#### 步骤 3：查询符号表

```bash
cd uboot/
grep "c0408234" u-boot.sym
```

**输出：**
```
c0408200 T run_command
c0408234 T run_command_list
```

**解读：**
- 崩溃发生在 `run_command_list` 函数内
- 调用者是 `run_command` 函数（地址 0xc0408200）

#### 步骤 4：使用 addr2line 定位源代码

```bash
aarch64-linux-gnu-addr2line -e u-boot -f 0xc0408234
```

**输出：**
```
run_command_list
common/cli.c:234
```

#### 步骤 5：查看源代码

```bash
vim +234 common/cli.c
```

**代码内容：**
```c
// common/cli.c:234
int run_command_list(const char *cmd, int len, int flag)
{
    int need_buff = 1;
    char *buff = (char *)cmd;  // ← 第 234 行
    int rcode = 0;

    if (len == -1) {
        len = strlen(cmd);  // ← 如果 cmd 是非法指针，这里会崩溃！
        if (!len)
            return 0;
    }
    ...
}
```

#### 步骤 6：分析根因

**问题：** 参数 `cmd` (x0 寄存器) = `0x12345678` 是非法地址

**查找调用者：**
```bash
aarch64-linux-gnu-addr2line -e u-boot -f 0xc0408200
```

**输出：**
```
run_command
common/cli.c:198
```

**查看调用者代码：**
```c
// common/cli.c:198
int run_command(const char *cmd, int flag)
{
    if (!cmd || !*cmd)
        return -1;

    return run_command_list(cmd, -1, flag);  // ← 传递了非法的 cmd
}
```

**继续查找：** 谁调用了 `run_command`？

```bash
# 搜索代码中的调用点
cd uboot/
grep -rn "run_command" --include="*.c" | grep -v "^common/cli.c"
```

**找到可疑代码：**
```c
// board/rockchip/evb_rk3399/evb-rk3399.c:89
char *bootcmd = (char *)0x12345678;  // ← 硬编码的魔数！

int board_late_init(void)
{
    run_command(bootcmd, 0);  // ← 使用了非法指针
    return 0;
}
```

#### 步骤 7：修复代码

```c
// board/rockchip/evb_rk3399/evb-rk3399.c:89
- char *bootcmd = (char *)0x12345678;  // 错误的硬编码
+ char *bootcmd = env_get("bootcmd");  // 从环境变量获取

int board_late_init(void)
{
+   if (!bootcmd) {
+       printf("Warning: bootcmd not found\n");
+       return 0;
+   }
    run_command(bootcmd, 0);
    return 0;
}
```

#### 步骤 8：验证修复

```bash
# 重新编译
cd uboot/
./make.sh rk3399

# 烧录并测试
sudo dd if=u-boot.img of=/dev/mmcblk0 seek=16384 conv=fsync

# 重启设备
# 串口日志应该不再崩溃
```

---

## 总结与速查表

### 核心概念速查

| 概念 | 定义 | 用途 | 查看命令 |
|-----|------|-----|---------|
| **符号表** | 函数/变量名与地址的映射 | 地址→函数名 | `nm u-boot` 或 `cat u-boot.sym` |
| **ELF 文件** | Linux 可执行文件格式 | 存储代码、数据、调试信息 | `readelf -h u-boot` |
| **链接地址** | 编译时分配的地址 | 符号表中记录的地址 | `grep func u-boot.sym` |
| **运行地址** | 实际运行时的地址 | 崩溃日志中的地址 | 从串口日志获取 |
| **重定位** | 代码从链接地址移动到运行地址 | 避免内存冲突 | 日志中 `Relocation Offset` |
| **重定位偏移** | 运行地址 - 链接地址 | 地址转换 | `reloc_off = 运行地址 - 链接地址` |
| **addr2line** | 地址转源代码位置工具 | 地址→文件名:行号 | `addr2line -e u-boot 0x123` |
| **调试信息** | DWARF 格式的源码映射 | 支持 addr2line 和 GDB | 编译时加 `-g` 选项 |

### 地址转换公式

```
运行地址 = 链接地址 + 重定位偏移量
链接地址 = 运行地址 - 重定位偏移量
```

**示例：**
```
已知：
- 崩溃地址（运行地址）: 0xffd4c234
- 重定位偏移量: 0x3df44000

计算：
链接地址 = 0xffd4c234 - 0x3df44000 = 0xc0408234

在符号表中查找 0xc0408234
```

### 常用命令速查

```bash
# 1. 查看符号表
nm u-boot                          # 所有符号
nm -n u-boot                       # 按地址排序
nm u-boot | grep <function_name>   # 查找特定函数
cat u-boot.sym                     # U-Boot 生成的符号表

# 2. 查看 ELF 信息
readelf -h u-boot                  # ELF 文件头
readelf -S u-boot                  # 段（Sections）
readelf -s u-boot                  # 符号表
readelf -l u-boot                  # 程序头（Program Headers）

# 3. 地址转源代码
aarch64-linux-gnu-addr2line -e u-boot 0x12345678          # 基本用法
aarch64-linux-gnu-addr2line -e u-boot -f 0x12345678       # 显示函数名
aarch64-linux-gnu-addr2line -e u-boot -i 0x12345678       # 显示内联函数
aarch64-linux-gnu-addr2line -e u-boot -f -i 0x12345678    # 组合使用

# 4. 反汇编
aarch64-linux-gnu-objdump -d u-boot                        # 反汇编所有代码
aarch64-linux-gnu-objdump -D u-boot                        # 反汇编所有段
aarch64-linux-gnu-objdump -S u-boot                        # 混合显示源代码
aarch64-linux-gnu-objdump -d -j .text u-boot               # 只反汇编 .text 段

# 5. 查看调试信息
readelf --debug-dump=info u-boot | less                    # DWARF info
readelf --debug-dump=line u-boot | less                    # 行号映射

# 6. U-Boot 特定命令
cd uboot/
./make.sh sym                                              # 查看符号表
./make.sh map                                              # 查看内存映射
./make.sh elf                                              # 反汇编 ELF
./make.sh 0xffd4c234-0x3df44000                            # 地址查询（自动计算重定位）
```

### 调试工作流程

```
1. 获取崩溃日志
   ↓
2. 提取关键信息（ELR/PC, LR, 重定位偏移）
   ↓
3. 计算链接地址 = 运行地址 - 重定位偏移
   ↓
4. 查询符号表找到函数名
   ↓
5. 使用 addr2line 定位源代码行
   ↓
6. 查看源代码分析问题
   ↓
7. 修复代码并验证
```

### 配置检查清单

编译 U-Boot 前确保：

```bash
# ✓ 启用调试信息
grep CONFIG_DEBUG_INFO .config
# 应该输出：CONFIG_DEBUG_INFO=y

# ✓ 保留符号表（不要 strip）
ls -lh u-boot u-boot.sym
# 两个文件都应该存在

# ✓ 工具链已安装
which aarch64-linux-gnu-addr2line
which aarch64-linux-gnu-objdump
which aarch64-linux-gnu-nm

# ✓ 版本匹配
aarch64-linux-gnu-gcc --version
# 使用的编译器版本应该与 addr2line 一致
```

---

## 推荐学习资源

### 在线文档
- **ELF 格式规范**: https://refspecs.linuxfoundation.org/elf/elf.pdf
- **DWARF 调试格式**: http://dwarfstd.org/
- **U-Boot 官方文档**: https://u-boot.readthedocs.io/
- **ARM Architecture Reference Manual**: ARM 官网

### 实用工具
- **binutils**: nm, objdump, readelf, addr2line
- **GDB**: GNU 调试器
- **Ghidra**: 免费的逆向工程工具（可视化分析 ELF）
- **IDA Pro**: 商业反汇编器

### 进阶主题
- **动态链接**: 共享库的加载和符号解析
- **PIE (Position Independent Executable)**: 地址无关可执行文件
- **ASLR (Address Space Layout Randomization)**: 地址空间随机化
- **JTAG 调试**: 硬件级调试

---

**文档作者**: Claude Code
**最后更新**: 2025-12-16
**适用对象**: 嵌入式 Linux/U-Boot 初学者
**前置要求**: 基础 C 语言知识、Linux 命令行操作

---

## 附录：术语表

| 术语 | 英文全称 | 中文含义 | 说明 |
|-----|---------|---------|-----|
| ELF | Executable and Linkable Format | 可执行与可链接格式 | Linux 可执行文件标准 |
| DWARF | Debugging With Attributed Record Formats | 属性记录格式调试 | 调试信息标准 |
| PC | Program Counter | 程序计数器 | CPU 当前执行指令的地址 |
| LR | Link Register | 链接寄存器 | 函数返回地址 |
| SP | Stack Pointer | 栈指针 | 栈顶地址 |
| ELR | Exception Link Register | 异常链接寄存器 | ARM64 异常发生时的 PC |
| GPT | GUID Partition Table | 全局唯一标识分区表 | 磁盘分区方案 |
| BootROM | Boot Read-Only Memory | 启动只读存储器 | 芯片内固化的启动代码 |
| SRAM | Static Random-Access Memory | 静态随机存取存储器 | 片上高速内存 |
| DRAM | Dynamic Random-Access Memory | 动态随机存取存储器 | 外部主内存（DDR） |

希望这份教程能帮助您理解 U-Boot 调试的基础概念！如有疑问，欢迎参考项目中的其他教学文档。
