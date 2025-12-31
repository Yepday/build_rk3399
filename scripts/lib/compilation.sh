#!/bin/bash

# Functions:
# compile_uboot   - 编译 U-Boot 引导加载器
# compile_kernel  - 编译 Linux 内核
# compile_module  - 编译并安装内核模块

# ============================================================================
# 函数: compile_uboot
# 功能: 根据不同平台编译 U-Boot 引导加载器
# 输入: 依赖环境变量 - $PLATFORM, $UBOOT_BIN, $UBOOT, $CHIP, $BOARD 等
# 输出: 生成的 U-Boot 镜像文件到 $UBOOT_BIN 目录
# ============================================================================
compile_uboot()
{
	# 检查 U-Boot 二进制输出目录是否存在，不存在则创建
	if [ ! -d $UBOOT_BIN ]; then
		mkdir -p $UBOOT_BIN
	fi

	# 检查 U-Boot 源码目录是否存在
	if [ ! -d $UBOOT ]; then
		# 如果源码不存在，弹出对话框提示用户准备源码
		whiptail --title "OrangePi Build System" \
			--msgbox "u-boot doesn't exist, pls perpare u-boot source code." \
			10 50 0
		exit 0  # 退出脚本
	fi

	# 切换到 U-Boot 源码目录
	cd $UBOOT
	# 打印红色提示信息：开始构建 U-Boot
	echo -e "\e[1;31m Build U-boot \e[0m"

	# 根据不同的平台执行不同的编译流程
	case "${PLATFORM}" in
		# ===== Allwinner H3/H6 平台 (传统版本) =====
		"OrangePiH3" | "OrangePiH6" | "OrangePiH6_Linux4.9")
			# 检查是否已经生成过二进制文件，如果没有则加载配置
			if [ ! -f $UBOOT/u-boot-"${CHIP}".bin -o ! -f $UBOOT/boot0_sdcard_"${CHIP}".bin ]; then
			make "${CHIP}"_config  # 加载芯片特定配置 (如 sun50iw6p1_config)
			fi
			# 编译 U-Boot 主程序，使用多核并行编译
			make -j${CORES} CROSS_COMPILE="${UBOOT_COMPILE}"
			# 编译 SPL (Secondary Program Loader，第二阶段引导加载器)
			make spl -j${CORES} CROSS_COMPILE="${UBOOT_COMPILE}"
			# 返回之前的目录
			cd -
			# 调用打包函数 (在 pack.sh 中定义)
			pack
			;;

		# ===== Allwinner H3/H6 平台 (主线内核版本) =====
		"OrangePiH3_mainline" | "OrangePiH6_mainline")
			# 加载开发板特定的 defconfig 配置文件
			make orangepi_"${BOARD}"_defconfig
			# 编译，指定 ARM 架构和交叉编译工具链，使用 4 核并行
			make -j4 ARCH=arm CROSS_COMPILE="${UBOOT_COMPILE}"

			# 拷贝生成的 U-Boot 镜像 (包含 SPL) 到输出目录
			# -f 参数强制覆盖已存在文件
			cp "$UBOOT"/u-boot-sunxi-with-spl.bin "$UBOOT_BIN"/u-boot-sunxi-with-spl.bin-"${BOARD}" -f
			;;

		# ===== Rockchip RK3399 平台 =====
		"OrangePiRK3399")
			# 调用 U-Boot 仓库中的 make.sh 脚本编译 RK3399 平台
			# 该脚本会自动完成配置、编译、打包等所有步骤
			./make.sh rk3399

			# 拷贝生成的镜像文件到输出目录
			cp -rf uboot.img $UBOOT_BIN              # U-Boot 主镜像 (带 Rockchip 头)
			cp -rf trust.img $UBOOT_BIN              # Trust 镜像 (包含 ATF BL31 + OP-TEE BL32)
			cp -rf rk3399_loader_v1.22.119.bin $UBOOT_BIN  # Loader 镜像 (DDR 初始化 + Miniloader)
			cp -rf idbloader.img $UBOOT_BIN          # IDBLoader (用于 SD 卡启动)
			;;

		# ===== 未知平台 =====
		*)
			# 如果平台不匹配，打印错误信息并退出
	        	echo -e "\e[1;31m Pls select correct platform \e[0m"
	        	exit 0
			;;
	esac

	# 打印编译完成提示
	echo -e "\e[1;31m Complete U-boot compile.... \e[0m"

	# 注释掉的代码：原本用于弹出编译完成对话框
	#whiptail --title "OrangePi Build System" \
	#	--msgbox "Build uboot finish. The output path: $BUILD" 10 60 0
}

# ============================================================================
# 函数: compile_kernel
# 功能: 根据不同平台编译 Linux 内核
# 输入: 依赖环境变量 - $PLATFORM, $BUILD, $LINUX, $ARCH, $TOOLS, $BOARD 等
# 输出: 生成的内核镜像和设备树文件到 $BUILD/kernel 目录
# ============================================================================
compile_kernel()
{
	# 检查构建输出根目录是否存在，不存在则创建
	if [ ! -d $BUILD ]; then
		mkdir -p $BUILD
	fi

	# 检查内核输出子目录是否存在，不存在则创建
	if [ ! -d $BUILD/kernel ]; then
		mkdir -p $BUILD/kernel
	fi

	# 打印开始编译提示
	echo -e "\e[1;31m Start compiling the kernel ...\e[0m"

	# 根据不同平台执行不同的内核编译流程
	case "${PLATFORM}" in
		# ===== Allwinner H3 平台 (ARMv7 32位) =====
		"OrangePiH3")
			# 编译 uImage 格式内核 (带 U-Boot header 的内核镜像)
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} uImage
			# 编译内核模块 (驱动程序等)
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} modules
			# 拷贝生成的 uImage 到输出目录
			cp $LINUX/arch/"${ARCH}"/boot/uImage $BUILD/kernel/uImage_$BOARD
			;;

		# ===== Allwinner H6 平台 (ARMv8 64位) =====
		"OrangePiH6" | "OrangePiH6_Linux4.9")
			# 编译原始 Image 格式内核 (ARM64 标准格式)
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} Image
			# 编译设备树二进制文件 (Device Tree Blob)
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} dtbs
			# 编译内核模块
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} modules
			# 使用 mkimage 工具将 Image 转换为 uImage 格式
			# -A: 架构 (arm), -O: 操作系统 (linux), -T: 类型 (kernel)
			# -C: 压缩方式 (none), -a: 加载地址, -e: 入口地址
			mkimage -A arm -n "${PLATFORM}" -O linux -T kernel -C none -a 0x40080000 -e 0x40080000 \
		                -d $LINUX/arch/"${ARCH}"/boot/Image "${BUILD}"/kernel/uImage_"${BOARD}"
			;;

		# ===== Allwinner H3/H6 平台 (主线内核版本) =====
		"OrangePiH3_mainline" | "OrangePiH6_mainline")
			# 编译内核 (默认目标，包括内核镜像和设备树)
			make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES}

			# 准备设备树输出目录
			if [ ! -d $BUILD/dtb ]; then
				mkdir -p $BUILD/dtb  # 不存在则创建
			else
				rm -rf $BUILD/dtb/*  # 存在则清空旧文件
			fi

			# 拷贝设备树文件
			echo -e "\e[1;31m Start Copy dtbs \e[0m"

			# 根据平台拷贝对应的设备树文件
			if [ ${PLATFORM} = "OrangePiH3_mainline" ];then
				# H3 平台：拷贝 sun8i-h3 系列设备树 (32位)
       				cp $LINUX/arch/"${ARCH}"/boot/dts/sun8i-h3-orangepi*.dtb $BUILD/dtb/
				# 同时拷贝 H2+ 系列设备树 (H2+ 是 H3 的简化版本)
       				cp $LINUX/arch/"${ARCH}"/boot/dts/sun8i-h2-plus-orangepi-*.dtb $BUILD/dtb/
			elif [ ${PLATFORM} = "OrangePiH6_mainline" ];then
				# H6 平台：拷贝 sun50i-h6 设备树 (64位，位于 allwinner 子目录)
       				cp $LINUX/arch/"${ARCH}"/boot/dts/allwinner/sun50i-h6-orangepi-${BOARD}.dtb $BUILD/dtb/
			fi

			# 拷贝内核镜像文件
			if [ ${PLATFORM} = "OrangePiH6_mainline" ];then
				# H6 使用未压缩的 Image (ARM64 标准)
				cp $LINUX/arch/"${ARCH}"/boot/Image $BUILD/kernel/Image_$BOARD
			elif [ ${PLATFORM} = "OrangePiH3_mainline" ];then
				# H3 使用压缩的 zImage (ARM32 标准)
				cp $LINUX/arch/"${ARCH}"/boot/zImage $BUILD/kernel/zImage_$BOARD
			fi

			# 拷贝内核符号表 (用于调试和内核模块加载)
			cp $LINUX/System.map $BUILD/kernel/System.map-$BOARD
			;;

		# ===== Rockchip RK3399 平台 =====
		"OrangePiRK3399")
			# 加载开发板特定的内核配置文件 (如 orangepi_linux_defconfig)
			make -C $LINUX ARCH=${ARCH} CROSS_COMPILE=$TOOLS ${BOARD}_linux_defconfig
			echo -e "\e[1;31m Using ${BOARD}_linux_defconfig\e[0m"
			# 编译 Rockchip 特定的 .img 格式 (包含内核+设备树+资源)
			# 目标格式: rk3399-orangepi-<board>.img
			make -C $LINUX ARCH=${ARCH} CROSS_COMPILE=$TOOLS -j${CORES} rk3399-orangepi-${BOARD}.img
			# 编译内核模块
			make -C $LINUX ARCH=${ARCH} CROSS_COMPILE=$TOOLS -j${CORES} modules
			# 拷贝生成的 boot.img 到输出目录
			# boot.img 是 Rockchip 格式的启动镜像，包含内核和设备树
			cp $LINUX/boot.img $BUILD/kernel
			;;

		# ===== 未知平台 =====
		*)
			# 平台不匹配时报错并退出
	        	echo -e "\e[1;31m Pls select correct platform \e[0m"
			exit 0
	esac

	# 打印编译完成提示
	echo -e "\e[1;31m Complete kernel compilation ...\e[0m"
}

# ============================================================================
# 函数: compile_module
# 功能: 编译并安装内核模块 (驱动程序)
# 输入: 依赖环境变量 - $BUILD, $LINUX, $ARCH, $TOOLS 等
# 输出: 内核模块文件到 $BUILD/lib/modules 目录
# ============================================================================
compile_module(){

	# 准备模块输出目录
	if [ ! -d $BUILD/lib ]; then
	        mkdir -p $BUILD/lib  # 不存在则创建
	else
	        rm -rf $BUILD/lib/*  # 存在则清空旧模块文件
	fi

	# 开始编译和安装内核模块
	echo -e "\e[1;31m Start installing kernel modules ... \e[0m"

	# 编译所有内核模块 (.ko 文件)
	# 模块包括：设备驱动、文件系统、网络协议等可动态加载的内核组件
	make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} modules

	# 安装编译好的模块到指定目录
	# INSTALL_MOD_PATH: 指定安装根路径，模块将安装到 $BUILD/lib/modules/<kernel-version>/
	make -C $LINUX ARCH="${ARCH}" CROSS_COMPILE=$TOOLS -j${CORES} modules_install INSTALL_MOD_PATH=$BUILD

	# 打印安装完成提示
	echo -e "\e[1;31m Complete kernel module installation ... \e[0m"

	# 注释掉的代码：原本用于弹出安装完成对话框
	#whiptail --title "OrangePi Build System" --msgbox \
	#	"Build Kernel OK. The path of output file: ${BUILD}" 10 80 0
}
