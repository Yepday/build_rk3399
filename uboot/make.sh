#!/bin/bash
#
# Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
#
# SPDX-License-Identifier: GPL-2.0
#

# 设置错误立即退出模式：任何命令返回非零状态都会导致脚本退出
set -e
# 获取第一个参数作为板子名称（如 evb-rk3399、rk3399 等）
BOARD=$1
# 获取第一个参数作为子命令（如 loader、trust、uboot 等）
SUBCMD=$1
# 获取第一个参数作为函数地址（用于调试时查询符号和代码位置）
FUNCADDR=$1
# 获取第二个参数作为文件路径或输出目录（如 O=rockdev）
FILE=$2
# 获取CPU核心数，用于并行编译（-j参数）。通过解析/proc/cpuinfo中的processor条目计数
JOB=`sed -n "N;/processor/p" /proc/cpuinfo|wc -l`
# 获取所有支持的板子配置文件列表（匹配 configs/ 目录下特定命名规则的 defconfig 文件）
SUPPORT_LIST=`ls configs/*[r,p][x,v,k][0-9][0-9]*_defconfig`

# @target board: 目标板配置（定义在 arch/arm/mach-rockchip/<soc>/Kconfig 中）
# @label: 构建信息显示标签
# @loader: 搜索用于打包 loader 的 ini 配置文件名
# @trust: 搜索用于打包 trust 的 ini 配置文件名
#
# "NA" 表示使用从 .config 文件读取的默认名称
#
# 格式:           目标板配置                    标签         loader名     trust名
RKCHIP_INI_DESC=("CONFIG_TARGET_GVA_RK3229       NA          RK322XAT     NA"
                 "CONFIG_COPROCESSOR_RK1808  RKNPU-LION      RKNPULION    RKNPULION"
# 待添加更多芯片配置...
                )

########################################### 用户可修改区域 #############################################
# 用户的 rkbin 工具相对路径（包含 Rockchip 固件打包工具和二进制 blob）
RKBIN_TOOLS=../external/rkbin/tools

# 用户的 GCC 交叉编译工具链和相对路径
# ARM32 架构的地址转行号工具（用于调试）
ADDR2LINE_ARM32=arm-linux-gnueabihf-addr2line
# ARM64 架构的地址转行号工具（用于调试）
ADDR2LINE_ARM64=aarch64-linux-gnu-addr2line
# ARM32 架构的反汇编工具
OBJ_ARM32=arm-linux-gnueabihf-objdump
# ARM64 架构的反汇编工具
OBJ_ARM64=aarch64-linux-gnu-objdump
# ARM32 交叉编译器前缀
GCC_ARM32=arm-linux-gnueabihf-
# ARM64 交叉编译器前缀
GCC_ARM64=aarch64-linux-gnu-
# ARM32 工具链路径（Linaro GCC 6.3.1）
TOOLCHAIN_ARM32=../prebuilts/gcc/linux-x86/arm/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/bin
# ARM64 工具链路径（注释掉的是备用路径）
#TOOLCHAIN_ARM64=../prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin
# ARM64 工具链路径（当前使用的路径）
TOOLCHAIN_ARM64=../toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/

########################################### 用户请勿修改区域 #############################################
# 二进制路径修复参数：将 ini 文件中的 tools/rk_tools/ 替换为 ./（用于兼容不同的目录结构）
BIN_PATH_FIXUP="--replace tools/rk_tools/ ./"
# Rockchip 工具路径（loaderimage、boot_merger、trust_merger 等打包工具）
RKTOOLS=./tools

# 声明全局 INI 文件搜索索引名称变量，在 select_chip_info() 函数中更新
# 芯片型号（如 RK3399、RK3328 等）
RKCHIP=
# 芯片显示标签（用于构建信息提示）
RKCHIP_LABEL=
# 用于搜索 loader 配置文件的芯片名（如 RK3399MINIALL.ini）
RKCHIP_LOADER=
# 用于搜索 trust 配置文件的芯片名（如 RK3399TRUST.ini）
RKCHIP_TRUST=

# 声明 rkbin 仓库路径变量，在 prepare() 函数中更新为绝对路径
RKBIN=

# 声明全局工具链路径变量（用于 CROSS_COMPILE），在 select_toolchain() 函数中更新
# 交叉编译器路径（如 aarch64-linux-gnu-）
TOOLCHAIN_GCC=
# 反汇编工具路径
TOOLCHAIN_OBJDUMP=
# 地址转行号工具路径
TOOLCHAIN_ADDR2LINE=

# 声明全局默认输出目录和命令变量，在 prepare() 函数中更新
# 输出目录（默认为第二个参数，如 O=rockdev 中的 rockdev）
OUTDIR=$2
# 输出选项（如果指定了输出目录，则为 O=<dir>，否则为空）
OUTOPT=

# 声明全局平台配置变量，在 fixup_platform_configure() 函数中更新
# RSA 加密模式参数（如 --rsa 3 用于 RK3308/PX30）
PLATFORM_RSA=
# SHA 哈希模式参数（如 --sha 2 用于 RK3368）
PLATFORM_SHA=
# U-Boot 镜像大小参数（如 --size 1024 2）
PLATFORM_UBOOT_IMG_SIZE=
# Trust 镜像大小参数（如 --size 1024 2）
PLATFORM_TRUST_IMG_SIZE=

# 外部环境参数
# 打包时忽略 BL32（OP-TEE）的选项，值只能是 "--ignore-bl32"
PACK_IGNORE_BL32=$TRUST_PACK_IGNORE_BL32
#########################################################################################################
# 帮助信息函数：显示脚本的使用方法、参数说明和示例
help()
{
	echo
	echo "Usage:"
	echo "	./make.sh [board|subcmd] [O=<dir>]"
	echo
	echo "	 - board: board name of defconfig"         # 板子名称（对应 configs/ 目录下的 defconfig 文件）
	echo "	 - subcmd: loader|loader-all|trust|trust-all|uboot|elf|map|sym|<addr>|"  # 子命令选项
	echo "	 - O=<dir>: assigned output directory"     # 指定输出目录
	echo
	echo "Example:"
	echo
	echo "1. Build board:"                              # 编译板子配置的示例
	echo "	./make.sh evb-rk3399               --- build for evb-rk3399_defconfig"
	echo "	./make.sh evb-rk3399 O=rockdev     --- build for evb-rk3399_defconfig with output dir \"./rockdev\""
	echo "	./make.sh firefly-rk3288           --- build for firefly-rk3288_defconfig"
	echo "	./make.sh                          --- build with exist .config"  # 使用已存在的 .config 编译
	echo
	echo "	After build, Images of uboot, loader and trust are all generated."  # 编译后生成 uboot、loader、trust 镜像
	echo
	echo "2. Pack helper:"                              # 打包辅助命令示例
	echo "	./make.sh uboot                    --- pack uboot.img"              # 打包 uboot.img
	echo "	./make.sh trust                    --- pack trust.img"              # 打包 trust.img
	echo "	./make.sh trust-all                --- pack trust img (all supported)"  # 打包所有支持的 trust 镜像
	echo "	./make.sh loader                   --- pack loader bin"             # 打包 loader 二进制
	echo "	./make.sh loader-all	           --- pack loader bin (all supported)"  # 打包所有支持的 loader
	echo
	echo "3. Debug helper:"                             # 调试辅助命令示例
	echo "	./make.sh elf                      --- dump elf file with -D(default)"  # 反汇编 ELF 文件（默认 -D 选项）
	echo "	./make.sh elf-S                    --- dump elf file with -S"           # 反汇编 ELF 文件（-S 选项）
	echo "	./make.sh elf-d                    --- dump elf file with -d"           # 反汇编 ELF 文件（-d 选项）
	echo "	./make.sh <no reloc_addr>          --- dump function symbol and code position of address(no relocated)"      # 查询未重定位地址的函数符号和代码位置
	echo "	./make.sh <reloc_addr-reloc_off>   --- dump function symbol and code position of address(relocated)"        # 查询重定位地址的函数符号和代码位置
	echo "	./make.sh map                      --- cat u-boot.map"                  # 显示 u-boot.map 内存映射文件
	echo "	./make.sh sym                      --- cat u-boot.sym"                  # 显示 u-boot.sym 符号表文件
}

################################################################################
# prepare() - 构建流程初始化函数
#
# 职责：
#   1. 解析输出目录参数 (O=<dir>)
#   2. 处理板子配置或子命令
#   3. 初始化 rkbin 工具路径
#
################################################################################
# 一、输出目录解析逻辑
#
# 场景 1：用户指定输出目录
#   $ ./make.sh rk3399 O=rockdev
#   → 设置 OUTDIR=rockdev, OUTOPT=O=rockdev
#   → 编译输出（.config, u-boot.bin 等）将放在 ./rockdev/ 目录
#
# 场景 2：子命令模式（需要查找已有 .config）
#   $ ./make.sh trust    # 或 loader/uboot/elf 等子命令
#   → 搜索当前目录树中的 .config 文件
#   → 找到 1 个：提取所在目录作为 OUTDIR（如 ./rockdev/.config → OUTDIR=rockdev）
#   → 找到 0 个：报错退出（需要先编译生成 .config）
#   → 找到多个：报错退出（存在歧义，要求用户删除多余的）
#
# 场景 3：板子配置模式
#   $ ./make.sh rk3399
#   → 设置 OUTDIR=.（当前目录）
#   → 稍后执行 make rk3399_defconfig 生成 .config
#
################################################################################
# 二、板子配置与子命令处理
#
# • 帮助命令：./make.sh --help
#   → 显示帮助后退出
#
# • 子命令跳过配置：./make.sh trust
#   → 不执行 make defconfig，直接进入打包流程
#
# • 函数地址查询：./make.sh 0x00200000 或 0x00300000-0x8400000
#   → 检查是否为十六进制地址格式
#   → 如果是地址查询，跳过配置（后续在 sub_commands 中处理）
#
# • 板子配置执行：./make.sh rk3399
#   → 检查 configs/rk3399_defconfig 是否存在
#   → 存在：执行 make rk3399_defconfig O=<dir> 生成 .config
#   → 不存在：列出所有支持的板子并退出
#
################################################################################
# 三、rkbin 工具路径初始化
#
# 检查 RKBIN_TOOLS (默认 ../external/rkbin/tools) 目录：
#   → 存在：获取绝对路径并设置全局变量 RKBIN
#   → 不存在：提示下载方法并退出
#
# 预期目录结构：
#   项目根目录/
#   ├── build_rk3399/uboot/make.sh    # 当前脚本
#   └── external/rkbin/               # rkbin 仓库（必需）
#       ├── tools/                    # 打包工具（boot_merger, trust_merger, loaderimage）
#       ├── RKBOOT/                   # Loader 配置文件（RK3399MINIALL.ini）
#       └── RKTRUST/                  # Trust 配置文件（RK3399TRUST.ini）
#
################################################################################
# 关键目录说明
#
#   configs/                存放板子配置文件（*_defconfig）
#   .config                 编译时生成，决定芯片型号和功能选项
#   ../external/rkbin/      Rockchip 固件打包工具和二进制 blob（需单独下载）
#   OUTDIR (如 rockdev/)    编译输出目录（.config, u-boot.bin, uboot.img 等）
#
################################################################################
# 典型执行流程
#
# 完整编译：
#   $ sudo ./make.sh rk3399 O=rockdev
#   → 解析 O=rockdev → OUTDIR=rockdev
#   → 检查 configs/rk3399_defconfig → 执行 make rk3399_defconfig O=rockdev
#   → 检查 ../external/rkbin/tools → 设置 RKBIN 绝对路径
#
# 仅打包镜像：
#   $ ./make.sh trust
#   → 搜索 .config（假设找到 ./rockdev/.config）→ OUTDIR=rockdev
#   → 跳过 make defconfig → 直接调用 pack_trust_image()
#
################################################################################

prepare()
{
	# 声明局部变量：absolute_path用于存储绝对路径，cmd用于解析命令，dir用于存储目录，count用于计数
	local absolute_path cmd dir count

	# 解析输出目录参数 'O=<dir>'
	# 提取等号前的部分，判断是否为'O'参数
	cmd=${OUTDIR%=*}
	# 如果参数是'O'，说明用户指定了输出目录
	if [ "${cmd}" = 'O' ]; then
		# 提取等号后的目录路径
		OUTDIR=${OUTDIR#*=}
		# 设置输出选项变量，后续make命令会使用
		OUTOPT=O=${OUTDIR}
	else
		# 如果没有指定O参数，则根据BOARD的值来决定输出目录
		case $BOARD in
			# 对于子命令或空命令，需要从现有的.config文件中解析输出目录
			''|elf*|loader*|spl*|itb|debug*|trust|uboot|map|sym)
			# 查找当前目录及子目录下的.config文件数量
			count=`find -name .config | wc -l`
			# 获取.config文件的路径
			dir=`find -name .config`
			# 如果只找到一个.config文件，这是最理想的情况
			if [ $count -eq 1 ]; then
				# 去除路径中的文件名，只保留目录部分
				dir=${dir%/*}
				# 去除路径前的"./"前缀，得到相对路径
				OUTDIR=${dir#*/}
				# 如果输出目录不是当前目录，则设置OUTOPT参数
				if [ $OUTDIR != '.' ]; then
					OUTOPT=O=${OUTDIR}
				fi
			# 如果没有找到.config文件，报错退出
			elif [ $count -eq 0 ]; then
				echo
				echo "Build failed, Can't find .config"
				help
				exit 1
			# 如果找到多个.config文件，也报错退出（避免歧义）
			else
				echo
				echo "Build failed, find $count '.config': "
				echo "$dir"
				echo "Please leave only one of them"
				exit 1
			fi
			;;

			# 对于其他情况（编译新的board配置），使用当前目录作为输出目录
			*)
			OUTDIR=.
			;;
		esac
	fi

	# 解析帮助命令和执行defconfig配置
	case $BOARD in
		# 处理帮助命令
		--help|-help|help|--h|-h)
		help
		exit 0
		;;

		# 对于子命令，不需要执行defconfig，直接跳过
		''|elf*|loader*|spl*|itb|debug*|trust*|uboot|map|sym)
		;;

		# 对于其他情况，处理board配置或函数地址查询
		*)
		# 检查FUNCADDR是否是有效的函数地址（只包含十六进制字符、数字和-符号）
		# 如果sed替换后为空，说明是有效的地址格式，直接返回（用于地址查询功能）
		if [ -z $(echo ${FUNCADDR} | sed 's/[0-9,a-f,A-F,x,X,-]//g') ]; then
			return
		# 如果不是地址查询，则检查对应的defconfig文件是否存在
		elif [ ! -f configs/${BOARD}_defconfig ]; then
			# 如果找不到defconfig文件，打印错误信息和支持的板子列表
			echo
			echo "Can't find: configs/${BOARD}_defconfig"
			echo
			echo "******** Rockchip Support List *************"
			echo "${SUPPORT_LIST}"
			echo "********************************************"
			echo
			exit 1
		# 找到了defconfig文件，执行make配置
		else
			# 打印正在配置的信息，显示使用的CPU核心数
			echo "make for ${BOARD}_defconfig by -j${JOB}"
			# 执行make defconfig命令，生成.config文件
			make ${BOARD}_defconfig ${OUTOPT}
		fi
		;;
	esac

	# 初始化RKBIN工具路径
	# 检查rkbin工具目录是否存在
	if [ -d ${RKBIN_TOOLS} ]; then
		# 获取rkbin工具的绝对路径（进入父目录并获取pwd）
		absolute_path=$(cd `dirname ${RKBIN_TOOLS}`; pwd)
		# 设置全局RKBIN变量为绝对路径
		RKBIN=${absolute_path}
	# 如果找不到rkbin工具目录，打印错误信息并退出
	else
		echo
		echo "Can't find '../rkbin/' repository, please download it before pack image!"
		echo "How to obtain? 3 ways:"
		echo "	1. Login your Rockchip gerrit account: \"Projects\" -> \"List\" -> search \"rk/rkbin\" repository"
		echo "	2. Github repository: https://github.com/rockchip-linux/rkbin"
		echo "	3. Download full release SDK repository"
		exit 1
	fi
}

##
# 选择工具链函数：根据 .config 中的架构配置选择 ARM32 或 ARM64 工具链
##
select_toolchain()
{
	# 声明局部变量用于存储工具链绝对路径
	local absolute_path

	# 检查是否为 ARM64 架构配置（查找 CONFIG_ARM64=y）
	if grep  -q '^CONFIG_ARM64=y' ${OUTDIR}/.config ; then
		# 检查 ARM64 工具链目录是否存在
		if [ -d ${TOOLCHAIN_ARM64} ]; then
			# 获取工具链的绝对路径
			absolute_path=$(cd `dirname ${TOOLCHAIN_ARM64}`; pwd)
			# 设置 ARM64 交叉编译器路径
			TOOLCHAIN_GCC=${absolute_path}/bin/${GCC_ARM64}
			# 设置 ARM64 反汇编工具路径
			TOOLCHAIN_OBJDUMP=${absolute_path}/bin/${OBJ_ARM64}
			# 设置 ARM64 地址转行号工具路径
			TOOLCHAIN_ADDR2LINE=${absolute_path}/bin/${ADDR2LINE_ARM64}
		else
			# 找不到 ARM64 工具链，报错退出
			echo "Can't find toolchain: ${TOOLCHAIN_ARM64}"
			exit 1
		fi
	# ARM32 架构配置
	else
		# 检查 ARM32 工具链目录是否存在
		if [ -d ${TOOLCHAIN_ARM32} ]; then
			# 获取工具链的绝对路径
			absolute_path=$(cd `dirname ${TOOLCHAIN_ARM32}`; pwd)
			# 设置 ARM32 交叉编译器路径
			TOOLCHAIN_GCC=${absolute_path}/bin/${GCC_ARM32}
			# 设置 ARM32 反汇编工具路径
			TOOLCHAIN_OBJDUMP=${absolute_path}/bin/${OBJ_ARM32}
			# 设置 ARM32 地址转行号工具路径
			TOOLCHAIN_ADDR2LINE=${absolute_path}/bin/${ADDR2LINE_ARM32}
		else
			# 找不到 ARM32 工具链，报错退出
			echo "Can't find toolchain: ${TOOLCHAIN_ARM32}"
			exit 1
		fi
	fi

	# 调试输出：显示使用的工具链路径（已注释）
	# echo "toolchain: ${TOOLCHAIN_GCC}"
}

##
# 子命令处理函数：处理各种子命令（elf、debug、map、sym、trust、loader、uboot 等）
##
sub_commands()
{
	# 解析子命令和选项（如 elf-S 分解为 cmd=elf, opt=S）
	local cmd=${SUBCMD%-*} opt=${SUBCMD#*-}
	# 设置默认的 elf、map、sym 文件路径
	local elf=${OUTDIR}/u-boot map=${OUTDIR}/u-boot.map sym=${OUTDIR}/u-boot.sym

	# 如果指定了 tpl 或 spl 文件，则查找对应的文件路径
	if [ "$FILE" == "tpl" -o "$FILE" == "spl" ]; then
		elf=`find -name u-boot-${FILE}`
		map=`find -name u-boot-${FILE}.map`
		sym=`find -name u-boot-${FILE}.sym`
	fi

	# 根据命令类型执行不同操作
	case $cmd in
		# ELF 文件反汇编命令
		elf)
		# 检查 ELF 文件是否存在
		if [ -o ! -f ${elf} ]; then
			echo "Can't find elf file: ${elf}"
			exit 1
		else
			# 如果没有指定选项，默认使用 '-D'（反汇编并显示汇编代码）
			if [ "${cmd}" = 'elf' -a "${opt}" = 'elf' ]; then
				opt=D
			fi
			# 使用 objdump 反汇编 ELF 文件并通过 less 分页显示
			${TOOLCHAIN_OBJDUMP} -${opt} ${elf} | less
			exit 0
		fi
		;;

		# 调试命令：启用各种调试选项
		debug)
		debug_command
		exit 0
		;;

		# 显示内存映射文件
		map)
		cat ${map} | less
		exit 0
		;;

		# 显示符号表文件
		sym)
		cat ${sym} | less
		exit 0
		;;

		# 打包 trust 镜像（包含 ATF 和 OP-TEE）
		trust)
		pack_trust_image ${opt}
		exit 0
		;;

		# 打包 loader 镜像（包含 DDR 初始化和 miniloader）
		loader)
		pack_loader_image ${opt}
		exit 0
		;;

		# 打包 SPL loader 镜像（TPL + SPL）
		spl)
		pack_spl_loader_image ${opt}
		exit 0
		;;

		# 打包 ITB 镜像（Image Tree Blob，包含内核、DTB 等）
		itb)
		pack_uboot_itb_image
		exit 0
		;;

		# 打包 U-Boot 镜像
		uboot)
		pack_uboot_image ${opt}
		exit 0
		;;

		# 默认情况：地址查询功能（查找函数符号和代码位置）
		*)
		# 搜索函数地址对应的符号和代码位置
		# 解析重定位偏移量（格式：地址-偏移量）
		RELOC_OFF=${FUNCADDR#*-}
		FUNCADDR=${FUNCADDR%-*}
		# 检查是否为有效的十六进制地址（只包含 0-9, a-f, A-F, x, X, -）
		if [ -z $(echo ${FUNCADDR} | sed 's/[0-9,a-f,A-F,x,X,-]//g') ] && [ ${FUNCADDR} ]; then
			# 处理带有 '0x' 或 '0X' 前缀的地址
			if [ `echo ${FUNCADDR} | sed -n "/0[x,X]/p" | wc -l` -ne 0 ]; then
				# 将十六进制字符串转换为十进制数
				FUNCADDR=`echo $FUNCADDR | awk '{ print strtonum($0) }'`
				# 将十进制数转换为小写十六进制字符串
				FUNCADDR=`echo "obase=16;${FUNCADDR}"|bc |tr '[A-Z]' '[a-z]'`
			fi
			# 处理重定位偏移量的十六进制前缀
			if [ `echo ${RELOC_OFF} | sed -n "/0[x,X]/p" | wc -l` -ne 0 ] && [ ${RELOC_OFF} ]; then
				RELOC_OFF=`echo $RELOC_OFF | awk '{ print strtonum($0) }'`
				RELOC_OFF=`echo "obase=16;${RELOC_OFF}"|bc |tr '[A-Z]' '[a-z]'`
			fi

			# 如果指定了重定位偏移量，则计算原始地址（重定位地址 - 偏移量）
			if [ "${FUNCADDR}" != "${RELOC_OFF}" ]; then
				# 十六进制 -> 十进制 -> 减法 -> 十六进制
				FUNCADDR=`echo $((16#${FUNCADDR}))`     # 十六进制转十进制
				RELOC_OFF=`echo $((16#${RELOC_OFF}))`   # 十六进制转十进制
				FUNCADDR=$((FUNCADDR-RELOC_OFF))        # 计算原始地址
				FUNCADDR=$(echo "obase=16;${FUNCADDR}"|bc |tr '[A-Z]' '[a-z]')  # 转换回十六进制
			fi

			echo
			# 在符号表中搜索匹配的地址
			sed -n "/${FUNCADDR}/p" ${sym}
			# 使用 addr2line 工具将地址转换为源代码文件名和行号
			${TOOLCHAIN_ADDR2LINE} -e ${elf} ${FUNCADDR}
			exit 0
		fi
		;;
	esac
}

# 选择芯片信息函数：从 .config 文件中解析芯片型号，用于：
#	1. RKCHIP: 修正平台配置
#	2. RKCHIP_LOADER: 搜索用于打包 loader 的 ini 文件
#	3. RKCHIP_TRUST: 搜索用于打包 trust 的 ini 文件
#	4. RKCHIP_LABEL: 显示构建信息
#
# 芯片信息来自 .config 文件和 'RKCHIP_INI_DESC' 数组
select_chip_info()
{
	# 声明局部变量
	local target_board item value

	# 首先从 .config 文件读取 RKCHIP
	# 正则表达式匹配以下芯片型号：
	#  - PX30, PX3SE
	#  - RK????, RK????X（如 RK3399、RK3399X）
	#  - RV????（如 RV1108）
	local chip_reg='^CONFIG_ROCKCHIP_[R,P][X,V,K][0-9ESX]{1,5}'
	# 统计匹配到的芯片配置数量
	count=`egrep -c ${chip_reg} ${OUTDIR}/.config`
	# 提取匹配到的芯片配置字符串（如 CONFIG_ROCKCHIP_RK3399）
	RKCHIP=`egrep -o ${chip_reg} ${OUTDIR}/.config`

	# 如果只匹配到一个芯片配置
	if [ $count -eq 1 ]; then
		# 去除前缀，只保留芯片型号（如 RK3399）
		RKCHIP=${RKCHIP##*_}
		# RK3368 的特殊处理：使用 RK3368H 标识
		grep '^CONFIG_ROCKCHIP_RK3368=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RK3368H
		# RV1108 的特殊处理：使用 RV110X 标识
		grep '^CONFIG_ROCKCHIP_RV1108=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RV110X
	# 如果匹配到多个芯片配置（某些芯片变体会匹配多个配置项）
	elif [ $count -gt 1 ]; then
		# 通过精确匹配确定芯片变体
		grep '^CONFIG_ROCKCHIP_PX3SE=y' ${OUTDIR}/.config > /dev/null \
			&& RKCHIP=PX3SE
		grep '^CONFIG_ROCKCHIP_RK3126=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RK3126
		grep '^CONFIG_ROCKCHIP_RK3326=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RK3326
		grep '^CONFIG_ROCKCHIP_RK3128X=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RK3128X
		grep '^CONFIG_ROCKCHIP_PX5=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=PX5
		grep '^CONFIG_ROCKCHIP_RK3399PRO=y' ${OUTDIR}/.config >/dev/null \
			&& RKCHIP=RK3399PRO
	# 如果没有匹配到任何芯片配置，报错退出
	else
		echo "Can't get Rockchip SoC definition in .config"
		exit 1
	fi

	# 默认使用 RKCHIP 作为所有芯片相关变量的初始值
	RKCHIP_LABEL=${RKCHIP}
	RKCHIP_LOADER=${RKCHIP}
	RKCHIP_TRUST=${RKCHIP}

	# 从 RKCHIP_INI_DESC 数组读取特殊配置覆盖默认值
	for item in "${RKCHIP_INI_DESC[@]}"
	do
		# 提取目标板配置名（第一列）
		target_board=`echo $item | awk '{ print $1 }'`
		# 检查此配置是否在 .config 中启用
		if grep  -q "^${target_board}=y" ${OUTDIR}/.config ; then
			# 提取并设置 LABEL 值（第二列）
			value=`echo $item | awk '{ print $2 }'`
			if [ "$value" != "NA" ]; then
				RKCHIP_LABEL=${value};
			fi
			# 提取并设置 LOADER 值（第三列）
			value=`echo $item | awk '{ print $3 }'`
			if [ "$value" != "NA" ]; then
				RKCHIP_LOADER=${value};
			fi
			# 提取并设置 TRUST 值（第四列）
			value=`echo $item | awk '{ print $4 }'`
			if [ "$value" != "NA" ]; then
				RKCHIP_TRUST=${value};
			fi
		fi
	done
}

# 修正平台特殊配置函数：针对不同芯片平台设置特殊的打包参数
#	1. 修正打包模式（RSA/SHA）
#	2. 修正镜像大小
#	3. 修正 ARM64 CPU 以 AArch32 模式启动的配置
fixup_platform_configure()
{
	# 声明局部变量
	local count plat

# <*> 为不同平台修正 RSA/SHA 打包模式
	# RK3308/PX30/RK3326/RK1808 使用 RSA-PKCS1 V2.1 算法，打包魔数为 "3"
	if [ $RKCHIP = "PX30" -o $RKCHIP = "RK3326" -o $RKCHIP = "RK3308" -o $RKCHIP = "RK1808" ]; then
		PLATFORM_RSA="--rsa 3"
	# RK3368 使用 Rockchip 大端序 SHA256，打包魔数为 "2"
	elif [ $RKCHIP = "RK3368" ]; then
		PLATFORM_SHA="--sha 2"
	# 其他平台使用默认配置
	fi

# <*> 为不同平台修正镜像大小打包参数
	if [ $RKCHIP = "RK3308" ]; then
		# 如果 ARM64 以 AArch32 模式启动，使用较小的镜像尺寸
		if grep -q '^CONFIG_ARM64_BOOT_AARCH32=y' ${OUTDIR}/.config ; then
			PLATFORM_UBOOT_IMG_SIZE="--size 512 2"   # 512KB，2个扇区
			PLATFORM_TRUST_IMG_SIZE="--size 512 2"
		# 否则使用默认的 ARM64 镜像尺寸
		else
			PLATFORM_UBOOT_IMG_SIZE="--size 1024 2"  # 1MB，2个扇区
			PLATFORM_TRUST_IMG_SIZE="--size 1024 2"
		fi
	elif [ $RKCHIP = "RK1808" ]; then
		# RK1808 固定使用 1MB 镜像尺寸
		PLATFORM_UBOOT_IMG_SIZE="--size 1024 2"
		PLATFORM_TRUST_IMG_SIZE="--size 1024 2"
	fi

# <*> 为 ARM64 CPU 以 AArch32 模式启动的平台修正标签
	if grep -q '^CONFIG_ARM64_BOOT_AARCH32=y' ${OUTDIR}/.config ; then
		# RK3308 在 AArch32 模式下，修改标签和 trust 名称
		if [ $RKCHIP = "RK3308" ]; then
			RKCHIP_LABEL=${RKCHIP_LABEL}"AARCH32"
			RKCHIP_TRUST=${RKCHIP_TRUST}"AARCH32"
		# RK3326 在 AArch32 模式下，修改标签和 loader 名称
		elif [ $RKCHIP = "RK3326" ]; then
			RKCHIP_LABEL=${RKCHIP_LABEL}"AARCH32"
			RKCHIP_LOADER=${RKCHIP_LOADER}"AARCH32"
		fi
	fi
}

##
# 调试命令函数：提供多种调试选项来启用或修改调试功能
# 警告：这些命令会修改 .config 和源代码文件，且无法自动恢复！
##
debug_command()
{
		# 如果没有指定调试选项，显示所有可用的调试选项帮助
		if [ "${cmd}" = 'debug' -a "${opt}" = 'debug' ]; then
			echo
			echo "The commands will modify .config and files, and can't auto restore changes!"  # 警告：会修改文件且无法自动恢复
			echo "debug-N, the N:"  # 调试选项列表
			echo "    1. lib/initcall.c debug() -> printf()"                    # 将 initcall.c 的 debug 改为 printf
			echo "    2. common/board_r.c and common/board_f.c debug() -> printf()"  # 将 board 文件的 debug 改为 printf
			echo "    3. global #define DEBUG"                                  # 全局定义 DEBUG 宏
			echo "    4. enable CONFIG_ROCKCHIP_DEBUGGER"                       # 启用 Rockchip 调试器
			echo "    5. enable CONFIG_ROCKCHIP_CRC"                            # 启用 CRC 校验
			echo "    6. enable CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP"              # 启用启动阶段时间戳打印
			echo "    7. enable CONFIG_ROCKCHIP_CRASH_DUMP"                     # 启用崩溃转储
			echo "    8. set CONFIG_BOOTDELAY=5"                                # 设置启动延迟为5秒
			echo "    9. armv7 start.S: print entry warning"                    # ARMv7 启动时打印警告信息
			echo "   10. armv8 start.S: print entry warning"                    # ARMv8 启动时打印警告信息
			echo "   11. firmware bootflow debug() -> printf()"                 # 固件启动流程 debug 改为 printf
			echo "   12. bootstage timing report"                               # 启动阶段时序报告
			echo
			echo "Enabled: "  # 显示已启用的调试选项
			grep '^CONFIG_ROCKCHIP_DEBUGGER=y' ${OUTDIR}/.config > /dev/null \
			&& echo "    CONFIG_ROCKCHIP_DEBUGGER"
			grep '^CONFIG_ROCKCHIP_CRC=y' ${OUTDIR}/.config > /dev/null \
			&& echo "    CONFIG_ROCKCHIP_CRC"
			grep '^CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP=y' ${OUTDIR}/.config > /dev/null \
			&& echo "    CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP"
			grep '^CONFIG_ROCKCHIP_CRASH_DUMP=y' ${OUTDIR}/.config > /dev/null \
			&& echo "    CONFIG_ROCKCHIP_CRASH_DUMP"

		# 调试选项 1：将 lib/initcall.c 中的 debug() 替换为 printf()
		elif [ "${opt}" = '1' ]; then
			sed -i 's/\<debug\>/printf/g' lib/initcall.c
			sed -i 's/ifdef DEBUG/if 1/g' lib/initcall.c
			echo "DEBUG [1]: lib/initcall.c debug() -> printf()"
		# 调试选项 2：将 common/board_r.c 和 common/board_f.c 中的 debug() 替换为 printf()
		elif [ "${opt}" = '2' ]; then
			sed -i 's/\<debug\>/printf/g' ./common/board_f.c
			sed -i 's/\<debug\>/printf/g' ./common/board_r.c
			echo "DEBUG [2]: common/board_r.c and common/board_f.c debug() -> printf()"
		# 调试选项 3：在全局头文件中定义 DEBUG 宏
		elif [ "${opt}" = '3' ]; then
			sed -i '$i \#define DEBUG\' include/configs/rockchip-common.h
			echo "DEBUG [3]: global #define DEBUG"
		# 调试选项 4：启用 CONFIG_ROCKCHIP_DEBUGGER 配置
		elif [ "${opt}" = '4' ]; then
			sed -i 's/\# CONFIG_ROCKCHIP_DEBUGGER is not set/CONFIG_ROCKCHIP_DEBUGGER=y/g' ${OUTDIR}/.config
			echo "DEBUG [4]: CONFIG_ROCKCHIP_DEBUGGER is enabled"
		# 调试选项 5：启用 CONFIG_ROCKCHIP_CRC 配置（CRC 校验）
		elif [ "${opt}" = '5' ]; then
			sed -i 's/\# CONFIG_ROCKCHIP_CRC is not set/CONFIG_ROCKCHIP_CRC=y/g' ${OUTDIR}/.config
			echo "DEBUG [5]: CONFIG_ROCKCHIP_CRC is enabled"
		# 调试选项 6：启用 CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP 配置（启动阶段时间戳）
		elif [ "${opt}" = '6' ]; then
			sed -i 's/\# CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP is not set/CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP=y/g' ${OUTDIR}/.config
			echo "DEBUG [6]: CONFIG_BOOTSTAGE_PRINTF_TIMESTAMP is enabled"
		# 调试选项 7：启用 CONFIG_ROCKCHIP_CRASH_DUMP 配置（崩溃转储）
		elif [ "${opt}" = '7' ]; then
			sed -i 's/\# CONFIG_ROCKCHIP_CRASH_DUMP is not set/CONFIG_ROCKCHIP_CRASH_DUMP=y/g' ${OUTDIR}/.config
			echo "DEBUG [7]: CONFIG_ROCKCHIP_CRASH_DUMP is enabled"
		# 调试选项 8：设置启动延迟为 5 秒（方便中断启动过程）
		elif [ "${opt}" = '8' ]; then
			sed -i 's/^CONFIG_BOOTDELAY=0/CONFIG_BOOTDELAY=5/g' ${OUTDIR}/.config
			echo "DEBUG [8]: CONFIG_BOOTDELAY is 5s"
		# 调试选项 9：在 ARMv7 启动代码中插入串口输出 'UUUU...'（用于确认代码执行）
		elif [ "${opt}" = '9' ]; then
			sed -i '/save_boot_params_ret:/a\ldr r0, =CONFIG_DEBUG_UART_BASE\nmov r1, #100\nloop:\nmov r2, #0x55\nstr r2, [r0]\nsub r1, r1, #1\ncmp r1, #0\nbne loop\ndsb' \
			./arch/arm/cpu/armv7/start.S
			echo "DEBUG [9]: armv7 start.S entry warning 'UUUU...'"
		# 调试选项 10：在 ARMv8 启动代码中插入串口输出 'UUUU...'（用于确认代码执行）
		elif [ "${opt}" = '10' ]; then
			sed -i '/save_boot_params_ret:/a\ldr x0, =CONFIG_DEBUG_UART_BASE\nmov x1, #100\nloop:\nmov x2, #0x55\nstr x2, [x0]\nsub x1, x1, #1\ncmp x1, #0\nb.ne loop\ndsb sy' \
			./arch/arm/cpu/armv8/start.S
			echo "DEBUG [10]: armv8 start.S entry warning 'UUUU...'"
		# 调试选项 11：将固件启动流程相关文件中的 debug() 替换为 printf()
		elif [ "${opt}" = '11' ]; then
			sed -i 's/\<debug\>/printf/g' common/fdt_support.c
			sed -i 's/\<debug\>/printf/g' common/image-fdt.c
			sed -i 's/\<debug\>/printf/g' common/image.c
			sed -i 's/\<debug\>/printf/g' arch/arm/lib/bootm.c
			sed -i 's/\<debug\>/printf/g' common/bootm.c
			sed -i 's/\<debug\>/printf/g' common/image.c
			sed -i 's/\<debug\>/printf/g' common/image-android.c
			sed -i 's/\<debug\>/printf/g' common/android_bootloader.c
			echo "DEBUG [11]: firmware bootflow debug() -> printf()"
		# 调试选项 12：启用启动阶段时序报告功能
		elif [ "${opt}" = '12' ]; then
			sed -i '$a\CONFIG_BOOTSTAGE=y\' ${OUTDIR}/.config
			sed -i '$a\CONFIG_BOOTSTAGE_REPORT=y\' ${OUTDIR}/.config
			sed -i '$a\CONFIG_CMD_BOOTSTAGE=y\' ${OUTDIR}/.config
			echo "DEBUG [12]: bootstage timing report"
		fi
		echo
}

##
# 打包 U-Boot 镜像函数：将 u-boot.bin 添加 Rockchip 头部打包成 uboot.img
##
pack_uboot_image()
{
	# 声明局部变量：加载地址、最大KB数、实际KB数、头部大小（2KB）
	local UBOOT_LOAD_ADDR UBOOT_MAX_KB UBOOT_KB HEAD_KB=2

	# 检查 u-boot.bin 文件大小（字节数）
	UBOOT_KB=`ls -l u-boot.bin | awk '{print $5}'`
	# 如果没有指定平台特定的镜像大小限制
	if [ "$PLATFORM_UBOOT_IMG_SIZE" = "" ]; then
		# 使用默认最大值：1022KB（1046528 字节 = 1022KB * 1024）
		UBOOT_MAX_KB=1046528
	else
		# 从平台配置中提取最大 KB 数（第二个参数）
		UBOOT_MAX_KB=`echo $PLATFORM_UBOOT_IMG_SIZE | awk '{print strtonum($2)}'`
		# 减去头部大小（2KB），转换为字节数
		UBOOT_MAX_KB=$(((UBOOT_MAX_KB-HEAD_KB)*1024))
	fi

	# 检查 u-boot.bin 是否超过大小限制
	if [ $UBOOT_KB -gt $UBOOT_MAX_KB ]; then
		echo
		echo "ERROR: pack uboot failed! u-boot.bin actual: $UBOOT_KB bytes, max limit: $UBOOT_MAX_KB bytes"
		exit 1
	fi

	# 打包镜像
	# 从 autoconf.mk 或 .config 中读取 U-Boot 加载地址（如 0x00200000）
	UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" ${OUTDIR}/include/autoconf.mk|tr -d '\r'`
	if [ ! $UBOOT_LOAD_ADDR ]; then
		# 如果 autoconf.mk 中没有，则从 .config 中读取
		UBOOT_LOAD_ADDR=`sed -n "/CONFIG_SYS_TEXT_BASE=/s/CONFIG_SYS_TEXT_BASE=//p" ${OUTDIR}/.config|tr -d '\r'`
	fi

	# 使用 loaderimage 工具打包：添加 Rockchip 头部（magic、chip ID、load addr、size、CRC）
	${RKTOOLS}/loaderimage --pack --uboot ${OUTDIR}/u-boot.bin uboot.img ${UBOOT_LOAD_ADDR} ${PLATFORM_UBOOT_IMG_SIZE}

	# 删除中间生成的 u-boot.img 和 u-boot-dtb.img，避免用户混淆（最终镜像是 uboot.img）
	if [ -f ${OUTDIR}/u-boot.img ]; then
		rm ${OUTDIR}/u-boot.img
	fi

	if [ -f ${OUTDIR}/u-boot-dtb.img ]; then
		rm ${OUTDIR}/u-boot-dtb.img
	fi

	echo "pack uboot okay! Input: ${OUTDIR}/u-boot.bin"
}

##
# 打包 U-Boot ITB 镜像函数：将 U-Boot 与 ATF(BL31) 或 TEE 打包成 u-boot.itb
# ITB (Image Tree Blob) 是 FIT (Flattened Image Tree) 格式的二进制文件
##
pack_uboot_itb_image()
{
	# 声明局部变量：ini 配置文件路径
	local ini

	# ARM64 平台使用 trust_merger 工具打包
	if grep -Eq ''^CONFIG_ARM64=y'|'^CONFIG_ARM64_BOOT_AARCH32=y'' ${OUTDIR}/.config ; then
		# 设置 TRUST 配置文件路径（包含 BL31 等信息）
		ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}${PLATFORM_AARCH32}TRUST.ini
		# 检查配置文件是否存在
		if [ ! -f ${ini} ]; then
			echo "pack trust failed! Can't find: ${ini}"
			return
		fi

		# 从 ini 文件中提取 BL31 (ARM Trusted Firmware) 的路径
		bl31=`sed -n '/_bl31_/s/PATH=//p' ${ini} |tr -d '\r'`

		# 将 BL31 ELF 文件复制到当前目录（U-Boot 构建需要）
		cp ${RKBIN}/${bl31} bl31.elf
		# 调用 make 生成 u-boot.itb（包含 U-Boot + BL31）
		make CROSS_COMPILE=${TOOLCHAIN_GCC} u-boot.itb
		echo "pack u-boot.itb okay! Input: ${ini}"
	# ARM32 平台使用 TOS (Trusted OS) 配置
	else
		# 设置 TOS 配置文件路径
		ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TOS.ini
		if [ ! -f ${ini} ]; then
			echo "pack trust failed! Can't find: ${ini}"
			return
		fi

		# 从 ini 文件中提取 TOS 和 TOSTA 路径
		TOS=`sed -n "/TOS=/s/TOS=//p" ${ini} |tr -d '\r'`         # Trusted OS 主文件
		TOS_TA=`sed -n "/TOSTA=/s/TOSTA=//p" ${ini} |tr -d '\r'`  # Trusted Application

		# 优先使用 TOSTA，如果没有则使用 TOS
		if [ $TOS_TA ]; then
			cp ${RKBIN}/${TOS_TA} tee.bin
		elif [ $TOS ]; then
			cp ${RKBIN}/${TOS} tee.bin
		else
			echo "Can't find any tee bin"
			exit 1
		fi

		# 调用 make 生成 u-boot.itb（包含 U-Boot + TEE）
		make CROSS_COMPILE=${TOOLCHAIN_GCC} u-boot.itb
		echo "pack u-boot.itb okay! Input: ${ini}"
	fi
}

##
# 打包 SPL Loader 镜像函数：将 TPL 和 SPL 打包成 loader 二进制文件
# SPL (Secondary Program Loader)，TPL (Tertiary Program Loader)
##
pack_spl_loader_image()
{
	# 声明局部变量：header 头部信息、label 标签、mode 模式
	local header label="SPL" mode=$1
	# 设置 MINIALL ini 配置文件路径
	local ini=${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini
	# 临时 ini 文件路径（会修改此文件）
	local temp_ini=${RKBIN}/.temp/${RKCHIP_LOADER}MINIALL.ini

	# 如果用户指定了自定义 ini 文件，则使用用户指定的
	if [ "$FILE" != "" ]; then
		ini=$FILE;
	fi

	# 检查 ini 文件是否存在
	if [ ! -f ${ini} ]; then
		echo "pack TPL+SPL loader failed! Can't find: ${ini}"
		return
	fi

	# 创建临时目录（如果已存在则先删除）
	if [ -d ${RKBIN}/.temp ]; then
		rm ${RKBIN}/.temp -rf
	else
		mkdir ${RKBIN}/.temp
	fi
	# 复制 SPL 和 TPL 二进制文件到临时目录
	cp ${OUTDIR}/spl/u-boot-spl.bin ${RKBIN}/.temp/
	cp ${OUTDIR}/tpl/u-boot-tpl.bin ${RKBIN}/.temp/
	# 复制 ini 配置文件到临时目录
	cp ${ini} ${RKBIN}/.temp/${RKCHIP_LOADER}MINIALL.ini -f

	# 切换到 rkbin 目录进行打包操作
	cd ${RKBIN}
	# 如果 mode 为 'spl'，则打包 TPL+SPL
	if [ "$mode" = 'spl' ]; then
		# 更新标签
		label="TPL+SPL"
		# 从原始 ini 文件提取头部名称（前4字节）
		header=`sed -n '/NAME=/s/NAME=//p' ${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini`
		# 跳过 TPL 的前4字节（原始头部），创建新的 tpl.bin
		dd if=${RKBIN}/.temp/u-boot-tpl.bin of=${RKBIN}/.temp/tpl.bin bs=1 skip=4
		# 在新的 tpl.bin 前添加正确的头部标识（取 header 前4字符）
		sed -i "1s/^/${header:0:4}/" ${RKBIN}/.temp/tpl.bin
		# 修改临时 ini 文件，将 FlashData 指向新的 tpl.bin
		sed -i "s/FlashData=.*$/FlashData=.\/.temp\/tpl.bin/"     ${temp_ini}
	fi

	# 修改临时 ini 文件，将 FlashBoot 指向 u-boot-spl.bin
	sed -i "s/FlashBoot=.*$/FlashBoot=.\/.temp\/u-boot-spl.bin/"  ${temp_ini}

	# 调用 boot_merger 工具进行打包（生成 *_loader_*.bin 文件）
	${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} ${temp_ini}
	# 清理临时目录
	rm ${RKBIN}/.temp -rf
	# 切换回原目录
	cd -
	# 删除旧的 loader 文件
	ls *_loader_*.bin >/dev/null 2>&1 && rm *_loader_*.bin
	# 移动新生成的 loader 文件到当前目录
	mv ${RKBIN}/*_loader_*.bin ./
	echo "pack loader(${label}) okay! Input: ${ini}"
	# 列出生成的 loader 文件
	ls ./*_loader_*.bin
}

##
# 打包 Loader 镜像函数：将 DDR 初始化代码和 Miniloader 打包成 loader.bin 和 idbloader.img
# Loader 是 BootROM 加载的第一段代码，负责初始化 DDR 内存和加载 U-Boot
##
pack_loader_image()
{
	# 声明局部变量：mode 模式、files 文件列表、ini 配置文件路径
	local mode=$1 files ini=${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini

	# 如果用户指定了自定义 ini 文件
	if [ "$FILE" != "" ]; then
		ini=$FILE;
	fi

	# 检查 ini 文件是否存在
	if [ ! -f $ini ]; then
		echo "pack loader failed! Can't find: $ini"
		return
	fi

	# 删除旧的 loader 文件
	ls *_loader_*.bin >/dev/null 2>&1 && rm *_loader_*.bin
	# 切换到 rkbin 目录
	cd ${RKBIN}

	# 如果 mode 为 'all'，则打包所有支持的 loader 变体
	if [ "${mode}" = 'all' ]; then
		# 查找所有匹配的 MINIALL ini 文件
		files=`ls ${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL*.ini`
		# 遍历每个 ini 文件进行打包
		for ini in $files
		do
			if [ -f "$ini" ]; then
				# 使用 boot_merger 工具打包
				${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} $ini
				echo "pack loader okay! Input: $ini"
			fi
		done
	# 否则只打包指定的单个 loader
	else
		${RKTOOLS}/boot_merger ${BIN_PATH_FIXUP} $ini
		echo "pack loader okay! Input: $ini"
	fi

	# 切换回原目录并移动生成的 loader 文件
	cd - && mv ${RKBIN}/*_loader_*.bin ./

	# 生成 idbloader.img（用于 SD 卡启动）
	# 从 ini 文件中提取 FlashData（DDR 初始化代码）的路径
	local temp=`grep FlashData= ${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini | cut -f 2 -d "="`
	local flashData=${temp/tools\/rk_tools\//}  # 去除路径前缀
	# 从 ini 文件中提取 FlashBoot（Miniloader）的路径
	temp=`grep FlashBoot= ${RKBIN}/RKBOOT/${RKCHIP_LOADER}MINIALL.ini | cut -f 2 -d "="`
	local flashBoot=${temp/tools\/rk_tools\//}
	# 将芯片名称转换为小写
	typeset -l localChip
	localChip=$RKCHIP
	# 使用 mkimage 生成 SD 卡启动镜像（-T rksd 表示 Rockchip SD 格式）
	${RKTOOLS}/mkimage -n ${localChip} -T rksd -d ${RKBIN}/${flashData} idbloader.img
	# 将 Miniloader 追加到 idbloader.img 后面
	cat ${RKBIN}/${flashBoot} >> idbloader.img

	# 注释掉的旧命令（功能已在上面实现）
	#    cd - && mv ${RKBIN}/*_loader_*.bin ./ && mv ${RKBIN}/idbloader.img ./
}

##
# 打包 32 位 Trust 镜像函数（内部函数）：用于 ARM32 平台
# Trust 镜像包含 Trusted OS (OP-TEE)，负责安全世界的代码执行
##
__pack_32bit_trust_image()
{
	# 声明局部变量
	local ini=$1 TOS TOS_TA DARM_BASE TEE_LOAD_ADDR TEE_OUTPUT TEE_OFFSET

	# 检查 ini 文件是否存在
	if [ ! -f ${ini} ]; then
		echo "pack trust failed! Can't find: ${ini}"
		return
	fi

	# 从 ini 文件解析原始路径
	TOS=`sed -n "/TOS=/s/TOS=//p" ${ini} |tr -d '\r'`         # Trusted OS 路径
	TOS_TA=`sed -n "/TOSTA=/s/TOSTA=//p" ${ini} |tr -d '\r'`  # Trusted Application 路径

	# 解析输出文件名和加载地址
	TEE_OUTPUT=`sed -n "/OUTPUT=/s/OUTPUT=//p" ${ini} |tr -d '\r'`
	# 如果没有指定输出文件名，使用默认值 "./trust.img"
	if [ "$TEE_OUTPUT" = "" ]; then
		TEE_OUTPUT="./trust.img"
	fi
	# 从 ini 文件读取加载地址偏移量（相对于 DRAM 基址）
	TEE_OFFSET=`sed -n "/ADDR=/s/ADDR=//p" ${ini} |tr -d '\r'`
	# 如果没有指定偏移量，使用默认值 0x8400000 (132MB)
	if [ "$TEE_OFFSET" = "" ]; then
		TEE_OFFSET=0x8400000
	fi

	# OP-TEE 的加载地址 = DRAM 基址 + 偏移量（通常是 132MB 偏移）
	# 从 autoconf.mk 读取 DRAM 基址（如 0x60000000）
	DARM_BASE=`sed -n "/CONFIG_SYS_SDRAM_BASE=/s/CONFIG_SYS_SDRAM_BASE=//p" ${OUTDIR}/include/autoconf.mk|tr -d '\r'`
	# 计算 TEE 加载地址（十进制）
	TEE_LOAD_ADDR=$((DARM_BASE+TEE_OFFSET))

	# 将十进制转换为十六进制（loaderimage 工具需要十六进制地址）
	TEE_LOAD_ADDR=$(echo "obase=16;${TEE_LOAD_ADDR}"|bc)

	# 替换路径前缀 "./tools/rk_tools/" 为 "./"（兼容旧的 ini 文件格式）
	TOS=$(echo ${TOS} | sed "s/tools\/rk_tools\//\.\//g")
	TOS_TA=$(echo ${TOS_TA} | sed "s/tools\/rk_tools\//\.\//g")

	# 使用 loaderimage 工具打包 Trust 镜像
	# 优先使用 TOSTA，如果没有则使用 TOS
	if [ $TOS_TA ]; then
		${RKTOOLS}/loaderimage --pack --trustos ${RKBIN}/${TOS_TA} ${TEE_OUTPUT} ${TEE_LOAD_ADDR} ${PLATFORM_TRUST_IMG_SIZE}
	elif [ $TOS ]; then
		${RKTOOLS}/loaderimage --pack --trustos ${RKBIN}/${TOS}    ${TEE_OUTPUT} ${TEE_LOAD_ADDR} ${PLATFORM_TRUST_IMG_SIZE}
	else
		echo "Can't find any tee bin"
		exit 1
	fi

	echo "pack trust okay! Input: ${ini}"
	echo
}

##
# 打包 64 位 Trust 镜像函数（内部函数）：用于 ARM64 平台
# ARM64 平台的 Trust 镜像包含 ATF (BL31) 和 OP-TEE (BL32)
##
__pack_64bit_trust_image()
{
	# 声明局部变量：ini 配置文件路径
	local ini=$1

	# 检查 ini 文件是否存在
	if [ ! -f ${ini} ]; then
		echo "pack trust failed! Can't find: ${ini}"
		return
	fi

	# 切换到 rkbin 目录进行打包
	cd ${RKBIN}
	# 使用 trust_merger 工具打包 Trust 镜像
	# 参数说明：
	#   PLATFORM_SHA: SHA 哈希模式（如 --sha 2）
	#   PLATFORM_RSA: RSA 加密模式（如 --rsa 3）
	#   PLATFORM_TRUST_IMG_SIZE: 镜像大小限制（如 --size 1024 2）
	#   BIN_PATH_FIXUP: 路径替换参数（--replace tools/rk_tools/ ./）
	#   PACK_IGNORE_BL32: 是否忽略 BL32（如 --ignore-bl32）
	#   ini: TRUST 配置文件路径（包含 BL31、BL32 等路径信息）
	${RKTOOLS}/trust_merger ${PLATFORM_SHA} ${PLATFORM_RSA} ${PLATFORM_TRUST_IMG_SIZE} ${BIN_PATH_FIXUP} \
				${PACK_IGNORE_BL32} ${ini}

	# 切换回原目录并移动生成的 trust*.img 文件
	cd - && mv ${RKBIN}/trust*.img ./
	echo "pack trust okay! Input: ${ini}"
	echo
}

pack_trust_image()
{
	# 定义局部变量：mode为打包模式参数，files用于存储ini文件列表，ini为配置文件路径
	local mode=$1 files ini

	# 检查并删除之前生成的trust镜像文件，避免旧文件干扰
	ls trust*.img >/dev/null && rm trust*.img
	# ARM64 uses trust_merger
	# 检查是否为ARM64架构（包括ARM64和ARM64启动AARCH32模式）
	if grep -Eq ''^CONFIG_ARM64=y'|'^CONFIG_ARM64_BOOT_AARCH32=y'' ${OUTDIR}/.config ; then
		# 设置ARM64默认的trust配置文件路径
		ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TRUST.ini
		# 如果用户指定了自定义配置文件，则使用用户指定的文件
		if [ "$FILE" != "" ]; then
			ini=$FILE;
		fi

		# 判断是否打包所有支持的trust镜像
		if [ "${mode}" = 'all' ]; then
			# 获取所有匹配的TRUST配置文件列表
			files=`ls ${RKBIN}/RKTRUST/${RKCHIP_TRUST}TRUST*.ini`
			# 遍历每个配置文件
			for ini in $files
			do
				# 调用64位trust镜像打包函数
				__pack_64bit_trust_image ${ini}
			done
		else
			# 仅打包单个默认的64位trust镜像
			__pack_64bit_trust_image ${ini}
		fi
	# ARM uses loaderimage
	# ARM32架构使用loaderimage工具打包
	else
		# 设置ARM32默认的trust配置文件路径（TOS表示Trusted OS）
		ini=${RKBIN}/RKTRUST/${RKCHIP_TRUST}TOS.ini
		# 如果用户指定了自定义配置文件，则使用用户指定的文件
		if [ "$FILE" != "" ]; then
			ini=$FILE;
		fi
		# 判断是否打包所有支持的trust镜像
		if [ "${mode}" = 'all' ]; then
			# 获取所有匹配的TOS配置文件列表
			files=`ls ${RKBIN}/RKTRUST/${RKCHIP_TRUST}TOS*.ini`
			# 遍历每个配置文件
			for ini in $files
			do
				# 调用32位trust镜像打包函数
				__pack_32bit_trust_image ${ini}
			done
		else
			# 仅打包单个默认的32位trust镜像
			__pack_32bit_trust_image ${ini}
		fi
	fi
}

##
# 完成提示函数：显示构建完成信息
##
finish()
{
	echo
	# 如果没有指定 BOARD 参数，说明是使用已存在的 .config 编译
	if [ "$BOARD" = '' ]; then
		echo "Platform ${RKCHIP_LABEL} is build OK, with exist .config"
	# 否则说明是用新的 defconfig 编译
	else
		echo "Platform ${RKCHIP_LABEL} is build OK, with new .config(make ${BOARD}_defconfig)"
	fi
}

##
# 主执行流程：按顺序调用各个函数完成 U-Boot 的编译和打包
##
# 1. 准备工作：解析参数、检查工具、初始化路径
prepare
# 2. 选择交叉编译工具链（ARM32 或 ARM64）
select_toolchain
# 3. 选择芯片信息（确定 RKCHIP、RKCHIP_LOADER、RKCHIP_TRUST 等）
select_chip_info
# 4. 修正平台特定配置（RSA/SHA 模式、镜像大小等）
fixup_platform_configure
# 5. 处理子命令（如果有的话）：elf、loader、trust、uboot 等
sub_commands
# 6. 执行 make 编译 U-Boot（生成 u-boot.bin）
make CROSS_COMPILE=${TOOLCHAIN_GCC}  all --jobs=${JOB} ${OUTOPT}
# 7. 打包 U-Boot 镜像（u-boot.bin -> uboot.img）
pack_uboot_image
# 8. 打包 Loader 镜像（DDR init + Miniloader -> loader.bin + idbloader.img）
pack_loader_image
# 9. 打包 Trust 镜像（ATF + OP-TEE -> trust.img）
pack_trust_image
# 10. 显示完成信息
finish
