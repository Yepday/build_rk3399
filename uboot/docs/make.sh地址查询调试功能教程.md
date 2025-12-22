# U-Boot make.sh 地址查询调试功能完整教程

## 目录
1. [功能概述](#功能概述)
2. [为什么需要这个功能](#为什么需要这个功能)
3. [前置知识](#前置知识)
4. [使用方法](#使用方法)
5. [工作原理详解](#工作原理详解)
6. [实战案例](#实战案例)
7. [配合其他调试命令](#配合其他调试命令)
8. [常见问题与解答](#常见问题与解答)

---

## 功能概述

`make.sh` 的地址查询功能是一个**内存地址到源代码位置的转换工具**，用于在 U-Boot 启动失败、崩溃或异常时，快速定位问题代码的具体位置。

**核心能力：**
- 将十六进制内存地址转换为源代码文件名和行号
- 显示地址对应的函数符号名称
- 自动处理 U-Boot 重定位后的地址
- 支持 TPL、SPL 和 U-Boot proper 的调试

**典型使用场景：**
```bash
# U-Boot 崩溃时输出类似这样的信息：
"data abort pc : [<0002e9f8>]"
"prefetch abort lr : [<fff59ab0>]"

# 使用地址查询功能快速定位：
./make.sh 0x0002e9f8
# 输出：board/rockchip/evb_rk3399/evb-rk3399.c:156
```

---

## 为什么需要这个功能

### 问题场景

在嵌入式 U-Boot 开发和调试中，经常遇到以下问题：

#### 1. 启动崩溃无法定位
```
U-Boot SPL 2017.09 (Dec 16 2025 - 10:23:45)
Trying to boot from MMC1
data abort
pc : [<00020abc>]          lr : [<00020a90>]
sp : 00000000  ip : 00000000     fp : 00000000
```
**问题**：只有地址 `0x00020abc`，不知道是哪个函数、哪一行代码出了问题。

#### 2. 重定位后地址不匹配
```
U-Boot 2017.09 (Dec 16 2025 - 10:23:45)
DRAM:  4 GiB
Relocation Offset is: 3ff57000
WARNING: Caches not enabled
...
prefetch abort
pc : [<fff5c234>]          lr : [<fff5c200>]
relocation offset is: 3ff57000
```
**问题**：崩溃地址 `0xfff5c234` 是重定位后的地址，需要减去偏移量 `0x3ff57000` 才能在符号表中找到。

#### 3. 多阶段启动调试复杂
- TPL 崩溃在 `0x00010000` 附近
- SPL 崩溃在 `0x00200000` 附近
- U-Boot proper 崩溃在 `0xfff00000` 附近

每个阶段的符号表和 ELF 文件不同，手动查找非常困难。

### 传统调试方法的痛点

**方法 1：手动查找符号表**
```bash
# 步骤繁琐，容易出错
grep "00020abc" u-boot.sym
aarch64-linux-gnu-addr2line -e u-boot 0x00020abc
```

**方法 2：使用 GDB（不现实）**
- 嵌入式设备通常没有 JTAG 调试器
- 早期启动阶段（SPL/TPL）无法使用 GDB
- 生产环境只能通过串口日志调试

### 地址查询功能的优势

✅ **一条命令解决** - 不需要记忆复杂的工具链命令
✅ **自动处理重定位** - 无需手动计算地址偏移
✅ **智能识别阶段** - 自动查找对应的 TPL/SPL/U-Boot 符号表
✅ **快速定位代码** - 直接输出 `文件名:行号`

---

## 前置知识

### 1. U-Boot 内存布局与重定位

#### 编译时链接地址（Link Address）
U-Boot 编译时指定的默认加载地址：
```c
// include/configs/rk3399_common.h
#define CONFIG_SYS_TEXT_BASE  0x00200000  // U-Boot 链接地址
```

#### 运行时重定位（Relocation）
为了不覆盖内核和设备树，U-Boot 会将自己重定位到 DRAM 高地址：
```
编译时地址：0x00200000 - 0x002fffff
运行时地址：0xfff00000 - 0xffffffff  (假设 DRAM 顶部)
重定位偏移：0xfff00000 - 0x00200000 = 0xffd00000
```

**重定位前后地址对应关系：**
```
函数 board_init_f 编译时地址：0x00210abc
重定位后运行地址：0x00210abc + 0xffd00000 = 0xfff10abc
```

### 2. 符号表文件（u-boot.sym）

符号表记录了函数和变量的地址：
```bash
$ cat u-boot.sym | grep board_init_f
00210abc T board_init_f
00210b00 T board_init_r
```
- `00210abc` - 函数地址（链接时地址）
- `T` - 符号类型（T=全局函数，t=局部函数，D=全局变量）
- `board_init_f` - 函数名

### 3. addr2line 工具

GCC 工具链自带的地址转行号工具：
```bash
$ aarch64-linux-gnu-addr2line -e u-boot 0x00210abc
common/board_f.c:1023
```
将地址转换为源代码位置。

### 4. U-Boot 启动日志中的关键信息

#### 未重定位阶段（SPL/TPL）
```
U-Boot SPL 2017.09
data abort
pc : [<00020abc>]  <- 这个地址是链接时地址，直接查询
```

#### 重定位后阶段（U-Boot proper）
```
U-Boot 2017.09
Relocation Offset is: 3ff57000  <- 记住这个偏移量
...
prefetch abort
pc : [<fff59abc>]  <- 这个地址需要减去偏移量
```

---

## 使用方法

### 基本语法

```bash
./make.sh <address>                    # 查询未重定位地址
./make.sh <address>-<relocation_offset> # 查询重定位后地址
./make.sh <address> [spl|tpl]          # 查询 SPL/TPL 地址
```

### 场景 1：查询未重定位地址（SPL/TPL 崩溃）

**步骤 1：获取崩溃地址**
```
U-Boot SPL 2017.09 (Dec 16 2025 - 10:23:45)
Trying to boot from MMC1
data abort
pc : [<0002e9f8>]          lr : [<0002e9f0>]
```

**步骤 2：执行查询命令**
```bash
cd uboot/
./make.sh 0x0002e9f8
```

**步骤 3：查看输出结果**
```
0002e9f8 T board_init_f
common/spl/spl.c:423
```
结果显示：
- 函数名：`board_init_f`
- 源文件：`common/spl/spl.c`
- 行号：`423`

### 场景 2：查询重定位后地址（U-Boot proper 崩溃）

**步骤 1：获取崩溃地址和重定位偏移**
```
U-Boot 2017.09 (Dec 16 2025 - 10:23:45)
DRAM:  4 GiB
Relocation Offset is: 3ff57000   <- 记住这个值
...
prefetch abort
pc : [<fff59abc>]          lr : [<fff59ab0>]
relocation offset is: 3ff57000
```

**步骤 2：执行查询命令**
```bash
cd uboot/
./make.sh 0xfff59abc-0x3ff57000
```

**步骤 3：脚本自动计算原始地址**
```
计算过程：
  重定位地址：0xfff59abc
  - 偏移量：  0x3ff57000
  -------------------------
  原始地址：  0xc0002abc
```

**步骤 4：查看输出结果**
```
c0002abc T do_bootm_linux
arch/arm/lib/bootm.c:256
```

### 场景 3：查询 TPL/SPL 特定符号表

如果项目同时编译了 TPL/SPL/U-Boot：
```bash
# 查询 SPL 地址
./make.sh 0x00020abc spl

# 查询 TPL 地址
./make.sh 0x00010abc tpl
```

脚本会自动查找对应的符号表：
- `spl/u-boot-spl.sym`
- `tpl/u-boot-tpl.sym`

### 场景 4：批量查询调用栈

当崩溃日志包含完整调用栈时：
```
Call trace:
[<fff59abc>] do_bootm_linux+0x12c
[<fff5a100>] do_bootm+0x234
[<fff5a200>] bootm_run_states+0x56
```

逐个查询：
```bash
./make.sh 0xfff59abc-0x3ff57000
./make.sh 0xfff5a100-0x3ff57000
./make.sh 0xfff5a200-0x3ff57000
```

---

## 工作原理详解

### 源码位置
文件：`uboot/make.sh:475-514`

### 代码流程分析

#### 第 1 步：解析地址格式
```bash
# 输入：0xfff59abc-0x3ff57000
RELOC_OFF=${FUNCADDR#*-}      # 提取 '-' 后的内容 → 0x3ff57000
FUNCADDR=${FUNCADDR%-*}        # 提取 '-' 前的内容 → 0xfff59abc
```

**支持的格式：**
- `0x12345678` - 带 0x 前缀
- `12345678` - 不带前缀
- `0X12345678` - 大写 X
- `0xfff59abc-0x3ff57000` - 带重定位偏移

#### 第 2 步：十六进制规范化
```bash
# 检查是否为有效的十六进制（只包含 0-9, a-f, A-F, x, X, -）
if [ -z $(echo ${FUNCADDR} | sed 's/[0-9,a-f,A-F,x,X,-]//g') ]; then
    # 处理带 '0x' 或 '0X' 前缀的地址
    if [ `echo ${FUNCADDR} | sed -n "/0[x,X]/p" | wc -l` -ne 0 ]; then
        # 十六进制 → 十进制
        FUNCADDR=`echo $FUNCADDR | awk '{ print strtonum($0) }'`
        # 十进制 → 小写十六进制
        FUNCADDR=`echo "obase=16;${FUNCADDR}"|bc |tr '[A-Z]' '[a-z]'`
    fi
fi
```

**转换示例：**
```
输入：0xFFF59ABC  → 转换 → 输出：fff59abc
输入：0x0002E9F8  → 转换 → 输出：2e9f8
```

#### 第 3 步：重定位地址计算
```bash
if [ "${FUNCADDR}" != "${RELOC_OFF}" ]; then
    # 十六进制转十进制
    FUNCADDR=`echo $((16#${FUNCADDR}))`     # fff59abc → 4294113980
    RELOC_OFF=`echo $((16#${RELOC_OFF}))`   # 3ff57000 → 1073000448

    # 减法计算原始地址
    FUNCADDR=$((FUNCADDR-RELOC_OFF))        # 4294113980 - 1073000448 = 3221113532

    # 转回十六进制
    FUNCADDR=$(echo "obase=16;${FUNCADDR}"|bc |tr '[A-Z]' '[a-z]')  # → c0002abc
fi
```

**数学公式：**
```
原始地址 = 重定位后地址 - 重定位偏移量
0xc0002abc = 0xfff59abc - 0x3ff57000
```

#### 第 4 步：符号表查找
```bash
# 在符号表中搜索匹配的地址
sed -n "/${FUNCADDR}/p" ${sym}
```

**符号表示例：**
```bash
$ cat u-boot.sym
...
c0002a80 T do_bootm
c0002abc T do_bootm_linux
c0002b00 T do_bootm_vxworks
...
```

查询结果：
```
c0002abc T do_bootm_linux
```

#### 第 5 步：源码位置定位
```bash
# 使用 addr2line 工具将地址转换为源码位置
${TOOLCHAIN_ADDR2LINE} -e ${elf} ${FUNCADDR}
```

**addr2line 工作原理：**
- 读取 ELF 文件中的 DWARF 调试信息
- 查找地址对应的源文件和行号
- 输出格式：`文件路径:行号`

**输出示例：**
```
arch/arm/lib/bootm.c:256
```

### 关键变量说明

| 变量 | 说明 | 示例值 |
|-----|------|--------|
| `FUNCADDR` | 查询的内存地址 | `0xfff59abc` |
| `RELOC_OFF` | 重定位偏移量 | `0x3ff57000` |
| `sym` | 符号表文件路径 | `u-boot.sym` 或 `spl/u-boot-spl.sym` |
| `elf` | ELF 可执行文件路径 | `u-boot` 或 `spl/u-boot-spl` |
| `TOOLCHAIN_ADDR2LINE` | addr2line 工具路径 | `aarch64-linux-gnu-addr2line` |

### 工具链配置

在 `make.sh:41-58` 中定义：
```bash
# ARM32 架构工具
ADDR2LINE_ARM32=arm-linux-gnueabihf-addr2line
TOOLCHAIN_ARM32=../prebuilts/gcc/linux-x86/arm/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/bin

# ARM64 架构工具（RK3399 使用）
ADDR2LINE_ARM64=aarch64-linux-gnu-addr2line
TOOLCHAIN_ARM64=../toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/
```

---

## 实战案例

### 案例 1：调试 SPL MMC 初始化失败

#### 问题描述
OrangePi RK3399 从 SD 卡启动时，SPL 阶段崩溃：

```
U-Boot SPL 2017.09-ge934607-dirty (Dec 16 2025 - 14:32:10)
Trying to boot from MMC1
spl: mmc init failed with error: -110
data abort
pc : [<000233a8>]          lr : [<00023390>]
reloc pc : [<6001e3a8>]    lr : [<6001e390>]
sp : 5ffffd30  ip : 00000000     fp : 00000000
r10: 00000000  r9 : 5ffffe40     r8 : 5ffffd58
r7 : 00000001  r6 : 00028c68     r5 : 00000000  r4 : 5ffffdb8
r3 : 00000000  r2 : 00000000     r1 : 00000001  r0 : 5ffffdb8
Flags: nZCv  IRQs off  FIQs off  Mode SVC_32
```

#### 调试步骤

**步骤 1：识别关键信息**
- 崩溃地址：`pc = 0x000233a8`
- 错误代码：`-110` (TIMEOUT)
- 崩溃阶段：SPL（未重定位，直接查询）

**步骤 2：查询崩溃地址**
```bash
cd uboot/
./make.sh 0x000233a8 spl
```

**步骤 3：分析输出**
```
000233a8 T spl_mmc_load_image
common/spl/spl_mmc.c:342

# 查看源代码
vim +342 common/spl/spl_mmc.c
```

**代码片段：**
```c
// common/spl/spl_mmc.c:342
int spl_mmc_load_image(struct spl_image_info *spl_image,
                       struct spl_boot_device *bootdev)
{
    struct mmc *mmc;
    int err;

    mmc = find_mmc_device(bootdev->boot_device);
    if (!mmc) {
        printf("spl: mmc device not found\n");
        return -ENODEV;
    }

    err = mmc_init(mmc);  // ← 第 342 行，崩溃点
    if (err) {
        printf("spl: mmc init failed with error: %d\n", err);
        return err;  // ← 这里发生 data abort
    }
    ...
}
```

**步骤 4：查询调用者地址**
```bash
./make.sh 0x00023390 spl  # lr (link register)
```

**输出：**
```
00023390 T spl_boot_device
common/spl/spl.c:567
```

**步骤 5：根因分析**
- `mmc_init()` 返回 `-110` (ETIMEDOUT)
- SD 卡初始化超时，可能原因：
  1. SD 卡接触不良
  2. 电源不稳定
  3. 时钟配置错误
  4. 设备树配置错误

**步骤 6：解决方案**
```c
// 增加调试信息
err = mmc_init(mmc);
if (err) {
    printf("spl: mmc init failed: err=%d, ocr=%x, bus_width=%d\n",
           err, mmc->ocr, mmc->bus_width);
    // 不要立即 return，尝试其他启动设备
    return err;
}
```

---

### 案例 2：调试 U-Boot 重定位后内存访问异常

#### 问题描述
U-Boot 重定位后访问非法内存地址：

```
U-Boot 2017.09-ge934607-dirty (Dec 16 2025 - 15:45:23)

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
prefetch abort
pc : [<ffd4c234>]          lr : [<ffd4c200>]
reloc pc : [<c0008234>]    lr : [<c0008200>]
sp : f8f3fb38  ip : 00000000     fp : 00000000
r10: 00000000  r9 : f8f3fea8     r8 : ffd6c8d0
r7 : 00000000  r6 : ffd9a234     r5 : 00000000  r4 : 00000000
r3 : 00000000  r2 : 00000000     r1 : 00000000  r0 : 12345678
Flags: nzcv  IRQs off  FIQs off  Mode SVC_32
Resetting CPU ...
```

#### 调试步骤

**步骤 1：识别关键信息**
- 崩溃地址：`pc = 0xffd4c234`
- 重定位偏移：`0x3df44000`
- 异常类型：prefetch abort（指令预取异常，说明跳转到非法地址）
- 寄存器 r0：`0x12345678`（明显的非法地址）

**步骤 2：查询崩溃地址**
```bash
cd uboot/
./make.sh 0xffd4c234-0x3df44000
```

**自动计算过程：**
```
重定位地址：0xffd4c234
- 偏移量：  0x3df44000
-------------------------
原始地址：  0xc0408234
```

**步骤 3：分析输出**
```
c0408234 T run_command_list
common/cli.c:234

# 查看源代码
vim +234 common/cli.c
```

**代码片段：**
```c
// common/cli.c:234
int run_command_list(const char *cmd, int len, int flag)
{
    int need_buff = 1;
    char *buff = (char *)cmd;  // ← 第 234 行
    int rcode = 0;

    if (len == -1) {
        len = strlen(cmd);  // ← 如果 cmd 是非法地址，这里会崩溃
        if (!len)
            return 0;
    }
    ...
}
```

**步骤 4：查询调用者地址**
```bash
./make.sh 0xffd4c200-0x3df44000
```

**输出：**
```
c0408200 T run_command
common/cli.c:198
```

**查看调用者代码：**
```c
// common/cli.c:198
int run_command(const char *cmd, int flag)
{
    if (!cmd || !*cmd)
        return -1;

    return run_command_list(cmd, -1, flag);  // ← 传入了非法的 cmd 指针
}
```

**步骤 5：追踪 cmd 参数来源**
```bash
# 查看寄存器 r0（ARM 调用约定：第一个参数）
# r0 = 0x12345678 <- 明显的魔数，说明变量未初始化

# 搜索代码中的魔数
cd uboot/
grep -r "0x12345678" .
```

**搜索结果：**
```c
// board/rockchip/evb_rk3399/evb-rk3399.c:89
char *bootcmd = (char *)0x12345678;  // ← 硬编码的非法地址！

int board_late_init(void)
{
    ...
    run_command(bootcmd, 0);  // ← 使用非法地址
    ...
}
```

**步骤 6：根因分析**
- 开发者在调试代码中硬编码了魔数 `0x12345678`
- 忘记删除或正确初始化 `bootcmd` 变量
- 导致 `run_command()` 访问非法内存

**步骤 7：修复代码**
```c
// board/rockchip/evb_rk3399/evb-rk3399.c:89
char *bootcmd = env_get("bootcmd");  // 正确从环境变量获取

int board_late_init(void)
{
    if (bootcmd && *bootcmd)
        run_command(bootcmd, 0);
    return 0;
}
```

---

### 案例 3：调试函数调用栈

#### 问题描述
U-Boot 启动时卡死，需要分析完整的调用栈：

```
U-Boot 2017.09 (Dec 16 2025 - 16:20:15)

DRAM:  4 GiB
Relocation Offset is: 3ff57000
...
Call trace:
[<ffd5a234>] fdt_path_offset+0x1c
[<ffd5b100>] fdt_find_node_by_path+0x28
[<ffd6c400>] rockchip_get_mmc_dev+0x34
[<ffd6c500>] mmc_get_env_dev+0x12
[<ffd7d800>] env_init+0x56
[<ffd7e000>] board_init_r+0x234
```

#### 批量查询调用栈

**步骤 1：提取所有地址**
```bash
# 手动提取或使用脚本
addresses=(
    0xffd5a234
    0xffd5b100
    0xffd6c400
    0xffd6c500
    0xffd7d800
    0xffd7e000
)
reloc_off=0x3ff57000
```

**步骤 2：批量查询**
```bash
cd uboot/
for addr in "${addresses[@]}"; do
    echo "=== 查询地址: $addr ==="
    ./make.sh ${addr}-${reloc_off}
    echo
done
```

**步骤 3：输出结果**
```
=== 查询地址: 0xffd5a234 ===
c000321c T fdt_path_offset
lib/libfdt/fdt_ro.c:456

=== 查询地址: 0xffd5b100 ===
c00040f0 T fdt_find_node_by_path
lib/libfdt/fdt.c:234

=== 查询地址: 0xffd6c400 ===
c00153f0 T rockchip_get_mmc_dev
arch/arm/mach-rockchip/board.c:567

=== 查询地址: 0xffd6c500 ===
c00154f0 T mmc_get_env_dev
board/rockchip/evb_rk3399/evb-rk3399.c:123

=== 查询地址: 0xffd7d800 ===
c00267f0 T env_init
env/mmc.c:789

=== 查询地址: 0xffd7e000 ===
c0026ff0 T board_init_r
common/board_r.c:1012
```

**步骤 4：重构调用链**
```
board_init_r (common/board_r.c:1012)
  └─> env_init (env/mmc.c:789)
      └─> mmc_get_env_dev (board/rockchip/evb_rk3399/evb-rk3399.c:123)
          └─> rockchip_get_mmc_dev (arch/arm/mach-rockchip/board.c:567)
              └─> fdt_find_node_by_path (lib/libfdt/fdt.c:234)
                  └─> fdt_path_offset (lib/libfdt/fdt_ro.c:456) ← 卡死在这里
```

**步骤 5：定位问题代码**
```c
// lib/libfdt/fdt_ro.c:456
int fdt_path_offset(const void *fdt, const char *path)
{
    const char *end = path + strlen(path);  // ← 如果 path 是环路指针，这里会死循环
    const char *p = path;
    int offset = 0;

    while (p < end) {  // ← 卡死在这里
        ...
    }
    ...
}
```

**步骤 6：根因分析**
- 设备树路径字符串损坏（没有 null 终止符）
- `strlen(path)` 导致死循环
- 需要检查设备树加载过程

---

## 配合其他调试命令

### 1. 查看完整符号表
```bash
./make.sh sym
```
**输出：**
```
00000000 T _start
00000020 t reset
...
c0002abc T do_bootm_linux
c0002b00 T do_bootm_vxworks
...
```

**用途：**
- 查看所有函数的地址范围
- 估算函数大小（下一个符号地址 - 当前符号地址）
- 查找附近的函数

### 2. 查看内存映射
```bash
./make.sh map
```
**输出：**
```
Memory Configuration

Name             Origin             Length             Attributes
*default*        0x0000000000000000 0xffffffffffffffff

Linker script and memory map

.text           0x0000000000200000   0x5a234
 *(.text*)
 .text          0x0000000000200000      0x234 arch/arm/cpu/armv8/start.o
                0x0000000000200000                _start
...
```

**用途：**
- 查看段地址（.text、.data、.bss）
- 验证链接脚本配置
- 分析内存布局问题

### 3. 反汇编 ELF 文件
```bash
# 默认选项 -D（反汇编所有段）
./make.sh elf

# 混合显示源代码和汇编（-S）
./make.sh elf-S

# 反汇编代码段（-d）
./make.sh elf-d
```

**输出示例（elf-S）：**
```asm
c0002abc <do_bootm_linux>:
do_bootm_linux():
arch/arm/lib/bootm.c:256
c0002abc:   a9be7bfd    stp x29, x30, [sp, #-32]!
c0002ac0:   910003fd    mov x29, sp
c0002ac4:   f9000bf3    str x19, [sp, #16]
c0002ac8:   aa0003f3    mov x19, x0
arch/arm/lib/bootm.c:257
c0002acc:   94001234    bl  c0003d9c <boot_prep_linux>
...
```

**用途：**
- 分析函数汇编代码
- 查看编译器优化结果
- 精确定位指令级问题

### 4. 组合使用示例

#### 场景：崩溃地址附近的代码分析
```bash
# 1. 查询崩溃地址
./make.sh 0xfff59abc-0x3ff57000
# 输出：c0002abc T do_bootm_linux

# 2. 查看符号表找到函数范围
./make.sh sym | grep -A5 -B5 c0002abc
# 输出：
# c0002a80 T do_bootm
# c0002abc T do_bootm_linux  <- 当前函数
# c0002b00 T do_bootm_vxworks
# 函数大小：0xc0002b00 - 0xc0002abc = 0x44 (68字节)

# 3. 反汇编查看函数代码
./make.sh elf-S | sed -n '/c0002abc/,/c0002b00/p'
# 输出：完整的 do_bootm_linux 函数汇编代码
```

---

## 常见问题与解答

### Q1：查询地址时提示 "Can't find elf file"

**问题：**
```bash
$ ./make.sh 0x0002e9f8
Can't find elf file: ./u-boot
```

**原因：**
- U-Boot 尚未编译
- 使用了错误的输出目录

**解决方案：**
```bash
# 方法 1：先编译 U-Boot
./make.sh rk3399

# 方法 2：指定输出目录
./make.sh 0x0002e9f8 O=rockdev

# 方法 3：检查 ELF 文件位置
ls -lh u-boot  # 主 U-Boot
ls -lh spl/u-boot-spl  # SPL
ls -lh tpl/u-boot-tpl  # TPL
```

---

### Q2：查询结果为空或显示 "??"

**问题：**
```bash
$ ./make.sh 0xfff59abc-0x3ff57000
??:0
```

**原因：**
1. 地址不在任何函数范围内（可能是数据段地址）
2. 编译时未启用调试符号（`-g` 选项）
3. 地址计算错误（重定位偏移量不正确）

**解决方案：**

**情况 1：检查地址是否在代码段**
```bash
./make.sh map | grep -A20 "\.text"
# 查看 .text 段的地址范围
# 例如：.text  0x00200000 - 0x0025a234

# 如果查询地址 0xc0002abc 超出范围，说明不是有效的函数地址
```

**情况 2：检查编译选项**
```bash
# 查看 U-Boot 配置
grep CONFIG_DEBUG_INFO .config

# 应该启用：
CONFIG_DEBUG_INFO=y

# 如果未启用，修改配置并重新编译：
make menuconfig
# 进入 Compiler options -> Enable debugging information
# 保存并重新编译
./make.sh rk3399
```

**情况 3：验证重定位偏移量**
```bash
# 方法 1：从启动日志中查找
grep "Relocation Offset" /dev/ttyUSB0.log

# 方法 2：从内存映射计算
./make.sh map | grep "\.text"
# 输出：.text  0x00200000
# 假设 DRAM = 4GB (0x100000000)
# 偏移量 ≈ 0x100000000 - 0x00200000 - (uboot大小) - (栈大小)

# 方法 3：使用环境变量
# 在 U-Boot 串口中输入：
printenv relocaddr
printenv textbase
# 偏移量 = relocaddr - textbase
```

---

### Q3：重定位偏移量不固定，每次启动都不同

**问题：**
```bash
# 第一次启动
Relocation Offset is: 3ff57000

# 第二次启动
Relocation Offset is: 3df44000

# 为什么不一样？
```

**原因：**
U-Boot 重定位地址由以下因素动态计算：
1. **DRAM 大小** - 不同内存配置导致顶部地址不同
2. **预留区域** - 内核、设备树、initrd 预留的内存
3. **随机化** - 某些平台启用 KASLR (Kernel Address Space Layout Randomization)

**重定位地址计算公式：**
```c
// common/board_f.c
gd->relocaddr = gd->ram_top;  // DRAM 顶部地址
gd->relocaddr -= gd->mon_len;  // 减去 U-Boot 大小
gd->relocaddr &= ~(4096 - 1);  // 4KB 对齐
gd->relocaddr -= CONFIG_SYS_MALLOC_LEN;  // 减去堆空间
gd->relocaddr -= sizeof(struct global_data);  // 减去全局数据
```

**解决方案：**
每次调试时从启动日志获取准确的偏移量：
```bash
# 启动日志示例
U-Boot 2017.09 (Dec 16 2025 - 17:10:45)

DRAM:  4 GiB
Relocation Offset is: 3df44000  <- 使用这个值
...
prefetch abort
pc : [<ffd4c234>]          lr : [<ffd4c200>]
relocation offset is: 3df44000  <- 或使用这个值

# 查询命令
./make.sh 0xffd4c234-0x3df44000
```

---

### Q4：ARM32 和 ARM64 平台的差异

**问题：**
RK3399 是 ARM64 架构，但有些工具链是 ARM32 的，如何选择？

**解决方案：**
`make.sh` 会自动检测架构并选择工具链：

```bash
# 查看 make.sh:79-92
select_toolchain()
{
    if grep -q '^CONFIG_ARM64=y' ${OUTDIR}/.config ; then
        # ARM64 平台（RK3399、RK3328 等）
        TOOLCHAIN_GCC=${GCC_ARM64}
        TOOLCHAIN_OBJDUMP=${OBJ_ARM64}
        TOOLCHAIN_ADDR2LINE=${TOOLCHAIN_ARM64}/${ADDR2LINE_ARM64}
    else
        # ARM32 平台（RK3288、RK3328 等）
        TOOLCHAIN_GCC=${GCC_ARM32}
        TOOLCHAIN_OBJDUMP=${OBJ_ARM32}
        TOOLCHAIN_ADDR2LINE=${TOOLCHAIN_ARM32}/${ADDR2LINE_ARM32}
    fi
}
```

**手动验证：**
```bash
# 检查编译架构
grep CONFIG_ARM64 .config

# 检查工具链
which aarch64-linux-gnu-addr2line  # ARM64
which arm-linux-gnueabihf-addr2line  # ARM32

# 测试工具链
aarch64-linux-gnu-addr2line --version
```

---

### Q5：如何批量处理多个地址？

**方法 1：Shell 脚本**
```bash
#!/bin/bash
# batch_query.sh

RELOC_OFF=0x3df44000
ADDRESSES=(
    0xffd4c234
    0xffd4c200
    0xffd5a100
)

cd uboot/
for addr in "${ADDRESSES[@]}"; do
    echo "=== Addr: $addr ==="
    ./make.sh ${addr}-${RELOC_OFF}
    echo
done
```

**方法 2：从日志文件提取**
```bash
#!/bin/bash
# extract_and_query.sh

LOG_FILE="/tmp/uboot_crash.log"

# 提取重定位偏移量
RELOC_OFF=$(grep "Relocation Offset is:" $LOG_FILE | awk '{print $4}')

# 提取所有地址（格式：[<0xabcd1234>]）
ADDRESSES=$(grep -oP '\[\<0x[0-9a-fA-F]+\>\]' $LOG_FILE | tr -d '[]<>')

cd uboot/
for addr in $ADDRESSES; do
    echo "=== Querying: $addr ==="
    ./make.sh ${addr}-${RELOC_OFF}
    echo
done
```

**使用方法：**
```bash
chmod +x extract_and_query.sh
./extract_and_query.sh
```

---

### Q6：地址查询功能能否用于内核调试？

**回答：**
不能直接用于 Linux 内核，但原理相同。

**内核调试需要：**
1. **内核符号表** - `System.map` 或 `vmlinux`
2. **内核反汇编** - `objdump -D vmlinux`
3. **addr2line** - 同样的工具

**内核崩溃示例：**
```
Unable to handle kernel paging request at virtual address ffffffc012345678
...
PC is at __do_softirq+0x12c/0x234
LR is at irq_exit+0x78/0xa0
```

**查询命令：**
```bash
# 使用内核符号表
grep "__do_softirq" System.map
# 输出：ffffffc010081234 T __do_softirq

# 使用 addr2line
aarch64-linux-gnu-addr2line -e vmlinux 0xffffffc010081234
# 输出：kernel/softirq.c:456
```

---

### Q7：如何在没有串口日志的情况下调试？

**场景：**
- 设备直接重启，没有日志输出
- 串口硬件损坏

**解决方案：**

**方法 1：使用内存转储（Ramdump）**
某些平台支持崩溃后保存内存到 eMMC：
```bash
# 从 eMMC 读取 ramdump 分区
dd if=/dev/mmcblk0p10 of=ramdump.bin bs=1M

# 解析寄存器状态
hexdump -C ramdump.bin | grep "pc :"
```

**方法 2：使用 JTAG 调试器**
```bash
# OpenOCD 连接
openocd -f interface/jlink.cfg -f target/rk3399.cfg

# GDB 连接
aarch64-linux-gnu-gdb u-boot
(gdb) target remote localhost:3333
(gdb) bt  # 查看调用栈
```

**方法 3：增加早期日志输出**
修改 `arch/arm/cpu/armv8/start.S`:
```asm
_start:
    /* 在最早期输出字符到 UART */
    ldr x0, =0xff1a0000  /* RK3399 UART2 基地址 */
    mov w1, #0x41        /* ASCII 'A' */
    str w1, [x0]         /* 写入 UART 数据寄存器 */

    /* 继续启动流程 */
    b reset
```

---

### Q8：能否集成到 IDE（VSCode/Vim）中？

**VSCode 集成：**

**步骤 1：创建 tasks.json**
```json
// .vscode/tasks.json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "U-Boot Address Query",
            "type": "shell",
            "command": "cd uboot && ./make.sh ${input:address}",
            "problemMatcher": [],
            "presentation": {
                "reveal": "always",
                "panel": "new"
            }
        }
    ],
    "inputs": [
        {
            "id": "address",
            "description": "Enter address (e.g., 0x0002e9f8 or 0xfff59abc-0x3ff57000)",
            "type": "promptString"
        }
    ]
}
```

**步骤 2：创建键盘快捷键**
```json
// .vscode/keybindings.json
[
    {
        "key": "ctrl+shift+a",
        "command": "workbench.action.tasks.runTask",
        "args": "U-Boot Address Query"
    }
]
```

**使用方法：**
1. 选中崩溃日志中的地址
2. 按 `Ctrl+Shift+A`
3. 输入地址（或粘贴选中内容）
4. 查看输出面板

---

**Vim 集成：**

**步骤 1：添加 Vim 函数**
```vim
" ~/.vimrc
function! UBootAddrQuery()
    let addr = input("Enter address: ")
    if addr != ""
        execute "!cd uboot && ./make.sh " . addr
    endif
endfunction

" 键盘快捷键
nnoremap <leader>ua :call UBootAddrQuery()<CR>
```

**步骤 2：使用方法**
1. 打开 Vim
2. 按 `,ua`（假设 leader 键是逗号）
3. 输入地址
4. 按 Enter 查看结果

---

## 总结

### 核心价值

地址查询功能是嵌入式 U-Boot 开发的**必备调试工具**，提供：

1. **快速定位** - 从崩溃地址直接跳转到源代码
2. **自动计算** - 处理 U-Boot 重定位地址偏移
3. **多阶段支持** - 支持 TPL、SPL、U-Boot proper 的调试
4. **简单易用** - 一条命令完成复杂的符号查找和地址转换

### 最佳实践

✅ **记录启动日志** - 保存完整的串口日志，包含重定位偏移量
✅ **启用调试符号** - 编译时加 `-g` 选项（CONFIG_DEBUG_INFO=y）
✅ **保留符号表** - 不要 strip ELF 文件，保留 .sym 文件
✅ **版本对应** - 确保查询的地址和符号表版本匹配
✅ **备份工具链** - 固定工具链版本，避免 addr2line 不兼容

### 进阶学习

- **GDB 调试** - 使用 GDB 配合 JTAG 进行单步调试
- **Trace32** - Lauterbach 调试器的高级功能
- **内核 Oops 分析** - 学习 Linux 内核崩溃日志解析
- **反汇编分析** - 深入学习 ARM 汇编和调用约定

---

## 附录

### 相关文件位置

| 文件 | 路径 | 说明 |
|-----|------|-----|
| 地址查询实现 | `uboot/make.sh:475-514` | 核心代码 |
| 工具链配置 | `uboot/make.sh:41-58` | addr2line 工具路径 |
| 符号表文件 | `u-boot.sym` | U-Boot proper 符号表 |
| SPL 符号表 | `spl/u-boot-spl.sym` | SPL 符号表 |
| TPL 符号表 | `tpl/u-boot-tpl.sym` | TPL 符号表 |
| ELF 文件 | `u-boot` | 带调试信息的可执行文件 |
| 内存映射 | `u-boot.map` | 链接器生成的内存布局 |

### 参考资料

- **U-Boot 官方文档**: https://u-boot.readthedocs.io/
- **ARM Architecture Reference Manual**: ARM 官方架构手册
- **RK3399 TRM**: Rockchip RK3399 Technical Reference Manual
- **GCC 调试选项**: https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html
- **addr2line 手册**: `man addr2line`

### 版本历史

| 版本 | 日期 | 说明 |
|-----|------|-----|
| v1.0 | 2025-12-16 | 初始版本，完整功能介绍 |

---

**文档作者**: Claude Code
**最后更新**: 2025-12-16
**适用版本**: U-Boot 2017.09 及以上
