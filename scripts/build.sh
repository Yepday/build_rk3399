#!/bin/bash
# Orange Pi RK3399 构建系统主脚本

set -e  # 遇到错误立即退出

# ========== 路径定义 ==========
ROOT=`pwd`                      # 项目根目录
UBOOT="${ROOT}/uboot"           # U-Boot 源码目录
BUILD="${ROOT}/output"          # 编译输出目录
LINUX="${ROOT}/kernel"          # Linux 内核源码目录
EXTER="${ROOT}/external"        # 外部工具目录
SCRIPTS="${ROOT}/scripts"       # 构建脚本目录
DEST="${BUILD}/rootfs"          # rootfs 目标目录
UBOOT_BIN="$BUILD/uboot"        # U-Boot 编译输出目录
PACK_OUT="${BUILD}/pack/"       # 固件打包输出目录

# ========== 配置变量初始化 ==========
OS=""                           # 操作系统类型
BT=""                           # 引导类型
CHIP=""                         # 芯片型号
ARCH=""                         # 目标架构 (arm/arm64)
DISTRO=""                       # Linux 发行版类型
ROOTFS=""                       # rootfs 文件系统类型
BOOT_PATH=""                    # boot 分区路径
UBOOT_PATH=""                   # uboot 分区路径
ROOTFS_PATH=""                  # rootfs 分区路径
BUILD_KERNEL=""                 # 是否构建内核标志
BUILD_MODULE=""                 # 是否构建内核模块标志

# ========== 全局设置 ==========
SOURCES="CN"                    # 软件源地区 (CN=中国)
METHOD="download"               # 下载方式
KERNEL_NAME="linux"             # 内核名称
UNTAR="bsdtar -xpf"            # 解压工具命令
CORES=$(nproc --ignore=1)      # 编译使用的 CPU 核心数 (总数-1)
PLATFORM="$(basename `pwd`)"   # 平台名称 (从当前目录名提取)

# ========== 权限检查 ==========
# 检查是否以 root 用户运行 (EUID=0 表示 root)
if [[ "${EUID}" == 0 ]]; then
        :  # 已经是 root，继续执行
else
	# 非 root 用户，提示需要 root 权限
	echo " "
        echo -e "\e[1;31m This script requires root privileges, trying to use sudo \e[0m"
	echo " "
        sudo "${ROOT}/build.sh"  # 使用 sudo 重新执行脚本
	exit $?                      # 退出并返回 sudo 执行的状态码
fi

# ========== 导入功能库 ==========
source "${SCRIPTS}"/lib/general.sh        # 通用功能库 (系统准备、依赖检查)
source "${SCRIPTS}"/lib/pack.sh           # 打包工具库 (H3/H6 固件打包)
source "${SCRIPTS}"/lib/compilation.sh    # 编译功能库 (内核、U-Boot 编译)
source "${SCRIPTS}"/lib/distributions.sh  # 发行版构建库 (rootfs 构建)
source "${SCRIPTS}"/lib/build_image.sh    # 镜像构建库 (最终镜像组装)
source "${SCRIPTS}"/lib/platform/rk3399.sh # RK3399 平台特定配置

# ========== 主机环境准备 ==========
# 检查是否已经准备好主机环境 (标记文件存在则跳过)
if [ ! -f $BUILD/.prepare_host ]; then
        prepare_host              # 执行主机环境准备 (安装依赖工具)
        touch $BUILD/.prepare_host # 创建标记文件，避免重复执行
fi

# ========== 平台和开发板选择 ==========
MENUSTR="Welcome to Orange Pi Build System. Pls choose Platform."
#################################################################
# 根据平台名称 (从目录名识别) 选择对应的配置
case "${PLATFORM}" in
	# ===== H3 平台 (32位 ARM) =====
	"OrangePiH3" | "OrangePiH3_mainline")

		# 显示开发板选择菜单
		OPTION=$(whiptail --title "Orange Pi Build System" \
			--menu "${MENUSTR}" 20 80 10 --cancel-button Exit --ok-button Select \
			"0"  "OrangePi PC Plus" \
			"1"  "OrangePi PC" \
			"2"  "OrangePi Plus2E" \
			"3"  "OrangePi Lite" \
			"4"  "OrangePi One" \
			"5"  "OrangePi 2" \
			"6"  "OrangePi ZeroPlus2 H3" \
			"7"  "OrangePi Plus" \
			"8"  "OrangePi Zero" \
			"9"  "OrangePi R1" \
			3>&1 1>&2 2>&3)

		# 根据用户选择设置开发板型号
		case "${OPTION}" in
			"0") BOARD="pc-plus" ;;
			"1") BOARD="pc"	;;
			"2") BOARD="plus2e" ;;
			"3") BOARD="lite" ;;
			"4") BOARD="one" ;;
			"5") BOARD="2" ;;
			"6") BOARD="zero_plus2_h3" ;;
			"7") BOARD="plus" ;;
			"8") BOARD="zero" ;;
			"9") BOARD="r1" ;;
			"*")
			echo -e "\e[1;31m Pls select correct board \e[0m"
			exit 0 ;;
		esac

		# 根据 H3 平台类型设置工具链
		if [ "${PLATFORM}" = "OrangePiH3" ]; then
			# 旧版 H3 (Linux 3.4.113) 使用 Linaro 1.13.1 工具链
			TOOLS=$ROOT/toolchain/gcc-linaro-1.13.1-2012.02-x86_64_arm-linux-gnueabi/bin/arm-linux-gnueabi-
			UBOOT_COMPILE="${TOOLS}"
			KERNEL_NAME="linux3.4.113"
		elif [ "${PLATFORM}" = "OrangePiH3_mainline" ]; then
			# 主线 H3 (Linux 5.3.5) 使用 Linaro 7.2.1 工具链
			TOOLS=$ROOT/toolchain/gcc-linaro-7.2.1-2017.11-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
			UBOOT_COMPILE="${TOOLS}"
			KERNEL_NAME="linux5.3.5"
		fi

		# H3 平台架构和芯片配置
		ARCH="arm"              # 32位 ARM 架构
		CHIP="sun8iw7p1";       # Allwinner H3 芯片代号
		CHIP_BOARD="dolphin-p1" # 芯片开发板代号
		;;
	# ===== H6 平台 (64位 ARM) =====
	"OrangePiH6" | "OrangePiH6_Linux4.9" | "OrangePiH6_mainline")

		# 显示 H6 开发板选择菜单
		OPTION=$(whiptail --title "Orange Pi Build System" \
		        --menu "$MENUSTR" 15 60 5 --cancel-button Exit --ok-button Select \
		        "0"  "OrangePi 3" \
		        "1"  "OrangePi Lite2" \
		        "2"  "OrangePi OnePlus" \
		        "3"  "OrangePi Zero2" \
		        3>&1 1>&2 2>&3)

		# 根据用户选择设置 H6 开发板型号
		case "${OPTION}" in
			"0") BOARD="3" ;;
			"1") BOARD="lite2" ;;
			"2") BOARD="oneplus" ;;
			"3") BOARD="zero2" ;;
			"*")
			echo -e "\e[1;31m Pls select correct board \e[0m"
			exit 0 ;;
		esac

		# H6 平台基本配置
		ARCH="arm64"                 # 64位 ARM 架构
		CHIP="sun50iw6p1"            # Allwinner H6 芯片代号
		CHIP_BOARD="petrel-p1"       # 芯片开发板代号
		CHIP_FILE="${EXTER}"/chips/"${CHIP}"  # 芯片配置文件路径
		# H6 工具链配置 (内核和 U-Boot 使用不同的工具链)
		TOOLS=$ROOT/toolchain/gcc-linaro-4.9-2015.01-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
		UBOOT_COMPILE=$ROOT/toolchain/gcc-linaro-4.9-2015.01-x86_64_aarch64-linux-gnu/gcc-linaro/bin/arm-linux-gnueabi-

		# 根据 H6 平台类型设置内核版本
		if [ "${PLATFORM}" = "OrangePiH6" ]; then
			# 旧版 H6 (Linux 3.10)
			KERNEL_NAME="linux3.10"
		elif [ "${PLATFORM}" = "OrangePiH6_Linux4.9" ]; then
			# 中间版本 (Linux 4.9.118)
			KERNEL_NAME="linux4.9.118"
		elif [ "${PLATFORM}" = "OrangePiH6_mainline" ]; then
			# 主线版本 (Linux 5.3.5)，使用更新的工具链
			KERNEL_NAME="linux5.3.5"
			TOOLS=$ROOT/toolchain/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
			UBOOT_COMPILE="${TOOLS}"
		fi
		;;
	# ===== RK3399 平台 (64位 ARM - Rockchip) =====
	"OrangePiRK3399")
		# 显示 RK3399 开发板选择菜单
		OPTION=$(whiptail --title "Orange Pi Build System" \
		        --menu "$MENUSTR" 15 60 5 --cancel-button Exit --ok-button Select \
		        "0"  "OrangePi 4" \
		        "1"  "OrangePi rk3399" \
		        3>&1 1>&2 2>&3)

		# 根据用户选择设置 RK3399 开发板型号
		case "${OPTION}" in
			"0") BOARD="4" ;;         # OrangePi 4
			"1") BOARD="rk3399" ;;    # OrangePi RK3399
			*)
			echo -e "\e[1;31m Pls select correct board \e[0m"
			exit 0 ;;
		esac

		# RK3399 平台配置
		ARCH="arm64"              # 64位 ARM 架构
		CHIP="RK3399"             # Rockchip RK3399 芯片
		# RK3399 使用 Linaro GCC 6.3.1 工具链
		TOOLS=$ROOT/toolchain/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
		KERNEL_NAME="linux4.4.179" # Linux 4.4.179 长期支持版本
		;;
	# ===== 未识别的平台 =====
	*)
		echo -e "\e[1;31m Pls select correct platform \e[0m"
		exit 0
		;;
esac

# ========== 构建选项菜单 ==========
MENUSTR="Pls select build option"
OPTION=$(whiptail --title "OrangePi Build System" \
	--menu "$MENUSTR" 20 60 10 --cancel-button Finish --ok-button Select \
	"0"   "Build Release Image" \     # 完整构建：uboot + kernel + rootfs + 镜像
	"1"   "Build Rootfs" \            # 仅构建根文件系统
	"2"   "Build Uboot" \             # 仅构建 U-Boot 引导加载器
	"3"   "Build Linux" \             # 构建 Linux 内核 + 模块
	"4"   "Build Module only" \       # 仅构建内核模块
	"5"   "Update Kernel Image" \     # 更新已有镜像中的内核
	"6"   "Update Module" \           # 更新已有 rootfs 中的内核模块
	"7"   "Update Uboot" \            # 更新已有镜像中的 U-Boot
	3>&1 1>&2 2>&3)

# ========== 构建选项执行 ==========
case "${OPTION}" in
	# ===== 选项 0: 完整构建发布镜像 =====
	"0")
		select_distro     # 选择 Linux 发行版 (Ubuntu/Debian 等)
		compile_uboot     # 编译 U-Boot 引导加载器
		compile_kernel    # 编译 Linux 内核
		build_rootfs      # 构建根文件系统
		build_image       # 打包最终可烧录镜像

		# 显示构建成功提示
		whiptail --title "OrangePi Build System" --msgbox "Succeed to build Image" \
			10 40 0 --ok-button Continue
		;;
	# ===== 选项 1: 仅构建根文件系统 =====
	"1")
		select_distro     # 选择 Linux 发行版
		compile_uboot     # 编译 U-Boot (生成引导文件)
		compile_kernel    # 编译内核 (生成 zImage/boot.img)
		build_rootfs      # 构建 rootfs (debootstrap + 配置)
		whiptail --title "OrangePi Build System" --msgbox "Succeed to build rootfs" \
			10 40 0 --ok-button Continue
		;;
	# ===== 选项 2: 仅编译 U-Boot =====
	"2")
		compile_uboot     # 编译 U-Boot，生成 uboot.img/trust.img/loader.bin
		;;
	# ===== 选项 3: 编译 Linux 内核和模块 =====
	"3")
		compile_kernel    # 编译内核镜像
		compile_module    # 编译内核模块 (.ko 文件)
		;;
	# ===== 选项 4: 仅编译内核模块 =====
	"4")
		compile_module    # 编译内核模块 (不编译内核本身)
		;;
	# ===== 选项 5: 更新镜像中的内核 =====
	"5")
		# RK3399 检查 uboot 分区，其他平台检查 boot 分区
		[ "${PLATFORM}" = "OrangePiRK3399" ] && uboot_check || boot_check
		kernel_update     # 将新编译的内核写入镜像
		;;
	# ===== 选项 6: 更新 rootfs 中的内核模块 =====
	"6")
		rootfs_check      # 检查 rootfs 分区是否存在
		modules_update    # 将新编译的模块复制到 rootfs
		;;
	# ===== 选项 7: 更新镜像中的 U-Boot =====
	"7")
		uboot_check       # 检查 uboot 分区是否存在
		uboot_update      # 将新编译的 U-Boot 写入镜像
		;;
	# ===== 无效选项 =====
	*)
		whiptail --title "OrangePi Build System" \
			--msgbox "Pls select correct option" 10 50 0
		;;
esac
