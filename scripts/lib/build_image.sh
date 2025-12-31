#!/bin/bash

# RK3399 平台镜像构建函数（使用 GPT 分区表）
build_rk_image()
{
	# 镜像版本号
	VER="v1.4"
	# 构建镜像文件名，包含板型、系统、发行版、镜像类型、内核版本等信息
	IMAGENAME="OrangePi_${BOARD}_${OS}_${DISTRO}_${IMAGETYPE}_${KERNEL_NAME}_${VER}"
	# 最终镜像文件的完整路径
	IMAGE="$BUILD/images/$IMAGENAME.img"

	# 如果镜像输出目录不存在，则创建
	if [ ! -d $BUILD/images ]; then
		mkdir -p $BUILD/images
	fi
        # GPT 分区布局定义（扇区为单位，1 扇区 = 512 字节）
	local UBOOT_START=24576     # U-Boot 分区起始扇区 (12MB 位置)
	local UBOOT_END=32767       # U-Boot 分区结束扇区 (4MB 大小)
	local TRUST_START=32768     # Trust 分区起始扇区 (16MB 位置)
	local TRUST_END=40959       # Trust 分区结束扇区 (4MB 大小)
	local BOOT_START=49152      # Boot 分区起始扇区 (24MB 位置)
	local BOOT_END=114687       # Boot 分区结束扇区 (32MB 大小)
	local ROOTFS_START=376832   # Rootfs 分区起始扇区 (184MB 位置)
	local LOADER1_START=64      # idbloader 写入位置 (32KB 位置，BootROM 加载点)
	# 计算 rootfs 实际大小（当前目录大小 + 400MB 缓冲），单位 KB
	local IMG_ROOTFS_SIZE=$(expr `du -s $DEST | awk 'END {print $1}'` + 400 \* 1024)
	# 计算 GPT 镜像最小尺寸：rootfs 大小 + 分区起始位置偏移，单位字节
	local GPTIMG_MIN_SIZE=$(expr $IMG_ROOTFS_SIZE \* 1024 + \( $(((${ROOTFS_START}))) \) \* 512)
	# 转换为 MB 并向上取整 + 2MB 对齐
	local GPT_IMAGE_SIZE=$(expr $GPTIMG_MIN_SIZE \/ 1024 \/ 1024 + 2)

	# 创建临时 rootfs 镜像文件（用零填充）
	dd if=/dev/zero of=${IMAGE}2 bs=1M count=$(expr $IMG_ROOTFS_SIZE  \/ 1024 )
	# 格式化为 ext4 文件系统
	# -O ^metadata_csum: 禁用元数据校验和（兼容性考虑）
	# -b 4096: 块大小 4KB
	# -E stride=2,stripe-width=1024: RAID 优化参数
	# -L rootfs: 卷标名称
	mkfs.ext4 -O ^metadata_csum -F -b 4096 -E stride=2,stripe-width=1024 -L rootfs ${IMAGE}2

	# 创建临时挂载点
	if [ ! -d /tmp/tmp ]; then
		mkdir -p /tmp/tmp
	fi

	# 挂载临时 rootfs 镜像
	mount -t ext4 ${IMAGE}2 /tmp/tmp
	# 将编译好的根文件系统内容复制到镜像中
	cp -rfa $DEST/* /tmp/tmp

	# 卸载临时镜像
	umount /tmp/tmp

	# 清理临时目录
	if [ -d $BUILD/orangepi ]; then
		rm -rf $BUILD/orangepi
	fi

	if [ -d /tmp/tmp ]; then
		rm -rf /tmp/tmp
	fi

	echo "Generate SD boot image : ${SDBOOTIMG} !"
	# 创建稀疏镜像文件（通过 seek 快速定位到指定大小，不实际写入数据）
	dd if=/dev/zero of=${IMAGE} bs=1M count=0 seek=$GPT_IMAGE_SIZE
	# 创建 GPT 分区表（GUID Partition Table，支持 >2TB 磁盘和更多分区）
	parted -s $IMAGE mklabel gpt
	# 创建 uboot 分区（4MB）：存储 U-Boot bootloader
	parted -s $IMAGE unit s mkpart uboot ${UBOOT_START} ${UBOOT_END}
	# 创建 trust 分区（4MB）：存储 ARM Trusted Firmware (ATF) 和 OP-TEE
	parted -s $IMAGE unit s mkpart trust ${TRUST_START} ${TRUST_END}
	# 创建 boot 分区（32MB）：存储 Linux 内核、设备树和 initramfs
	parted -s $IMAGE unit s mkpart boot ${BOOT_START} ${BOOT_END}
	# 创建 rootfs 分区（占用剩余空间，-34s 为 GPT 备份表预留空间）
	parted -s $IMAGE -- unit s mkpart rootfs ${ROOTFS_START} -34s
	# 关闭调试输出
	set +x
	# 设置 rootfs 分区的固定 UUID（用于 /etc/fstab 挂载识别）
	ROOT_UUID="614e0000-0000-4b53-8000-1d28000054a9"
# 使用 gdisk 交互式命令设置分区 UUID
gdisk $IMAGE <<EOF
x                       # 进入专家模式
c                       # 修改分区 GUID
4                       # 选择第 4 个分区（rootfs）
${ROOT_UUID}            # 设置 UUID
w                       # 写入更改
y                       # 确认写入
EOF
	# ===== 写入各个分区镜像到最终镜像文件 =====
	# 写入 idbloader.img（DDR 初始化 + Miniloader）到扇区 64（32KB 偏移）
	# Rockchip BootROM 从此位置加载第一阶段 bootloader
	dd if=$BUILD/uboot/idbloader.img of=$IMAGE seek=$LOADER1_START conv=notrunc
	# 写入 uboot.img（U-Boot 主程序）到 uboot 分区
	dd if=$BUILD/uboot/uboot.img of=$IMAGE seek=$UBOOT_START conv=notrunc,fsync
	# 写入 trust.img（ATF BL31 + OP-TEE BL32）到 trust 分区
	dd if=$BUILD/uboot/trust.img of=$IMAGE seek=$TRUST_START conv=notrunc,fsync
	# 写入 boot.img（内核 + dtb + initrd）到 boot 分区
	dd if=$BUILD/kernel/boot.img of=$IMAGE seek=$BOOT_START conv=notrunc,fsync
	# 写入 rootfs.img（根文件系统）到 rootfs 分区
	dd if=${IMAGE}2 of=$IMAGE seek=$ROOTFS_START conv=notrunc,fsync
	# 删除临时 rootfs 镜像文件
	rm -f ${IMAGE}2
	# 切换到镜像输出目录
	cd ${BUILD}/images/
	# 删除旧的压缩包
	rm -f ${IMAGENAME}.tar.gz
	# 生成镜像 MD5 校验和文件（用于验证下载完整性）
	md5sum ${IMAGENAME}.img > ${IMAGENAME}.img.md5sum
	# 打包镜像和校验和文件为 tar.gz 压缩包
	tar czvf  ${IMAGENAME}.tar.gz $IMAGENAME.img*
	# 删除临时校验和文件（已打包进 tar.gz）
	rm -f *.md5sum

	# 同步磁盘缓存到存储设备
	sync


}

# 通用镜像构建函数（用于 H3/H6 等平台，使用 MBR 分区表）
build_image()
{
	# 如果是 RK3399 平台，调用专用的 GPT 镜像构建函数
	if [ ${PLATFORM} = "OrangePiRK3399" ]; then
		build_rk_image
		return
	fi
	# 其他平台镜像版本号
	VER="v1.0"
	# 构建镜像文件名
	IMAGENAME="OrangePi_${BOARD}_${OS}_${DISTRO}_${IMAGETYPE}_${KERNEL_NAME}_${VER}"
	# 镜像文件完整路径
	IMAGE="${BUILD}/images/$IMAGENAME.img"

	# 创建镜像输出目录
	if [ ! -d ${BUILD}/images ]; then
		mkdir -p ${BUILD}/images
	fi

	# ===== MBR 分区布局配置（H3/H6 平台）=====
	boot0_position=8      # KiB - boot0 写入位置（SPL）
	uboot_position=16400  # KiB - U-Boot 写入位置
	part_position=20480   # KiB - 第一个分区起始位置（20MB）
	boot_size=50          # MiB - boot 分区大小（VFAT）

	# 创建镜像头部区域（用于存放 boot0 和 u-boot）
	dd if=/dev/zero bs=1M count=$((part_position/1024)) of="$IMAGE"

	# 创建 boot 分区镜像（FAT32 文件系统）
	dd if=/dev/zero bs=1M count=${boot_size} of=${IMAGE}1
	# 格式化为 VFAT，卷标为 BOOT
	mkfs.vfat -n BOOT ${IMAGE}1

	# ===== 根据不同平台写入 bootloader 和内核文件 =====
	case "${PLATFORM}" in
		# H3 和 H6 (Linux 4.9) 平台处理
		"OrangePiH3" | "OrangePiH6_Linux4.9")
			# 复制板型特定的内核镜像
			cp -rfa "${BUILD}/kernel/uImage_${BOARD}" "${BUILD}/kernel/uImage"

			# 定义 bootloader 文件路径
			boot0="${BUILD}/uboot/boot0_sdcard_${CHIP}.bin"      # SPL (Secondary Program Loader)
			uboot="${BUILD}/uboot/u-boot-${CHIP}.bin"            # U-Boot 主程序
			# 写入 boot0 到 8KB 偏移位置
			dd if="${boot0}" conv=notrunc bs=1k seek=${boot0_position} of="${IMAGE}"
			# 写入 u-boot 到 16400KB 偏移位置
			dd if="${uboot}" conv=notrunc bs=1k seek=${uboot_position} of="${IMAGE}"


			if [ "${PLATFORM}" = "OrangePiH3" ]; then
				# H3 平台：使用 script.bin（Allwinner FEX 格式设备树）
				cp -rfa ${EXTER}/script/script.bin_$BOARD $BUILD/script.bin
				# 使用 mtools 将文件复制到 FAT32 分区镜像
				mcopy -m -i ${IMAGE}1 ${BUILD}/kernel/uImage ::
				mcopy -sm -i ${IMAGE}1 ${BUILD}/script.bin_${BOARD} :: || true
			elif [ "${PLATFORM}" = "OrangePiH6_Linux4.9" ]; then
				# H6 平台：使用标准设备树（.dtb）
				mkdir -p "${BUILD}/orangepi"
				cp -f ${BUILD}/kernel/uImage "${BUILD}/orangepi/"
				cp -f ${BUILD}/uboot/${PLATFORM}.dtb "${BUILD}/orangepi/"

			        # 复制内核、设备树、initrd、启动脚本到 boot 分区
			        mcopy -sm -i ${IMAGE}1 ${BUILD}/orangepi ::
			        mcopy -m -i ${IMAGE}1 ${EXTER}/chips/$CHIP/initrd.img :: || true
			        mcopy -m -i ${IMAGE}1 ${EXTER}/chips/$CHIP/uEnv.txt :: || true

				# 清理临时目录
				rm -rf $BUILD/orangepi
			fi
			;;
		# H3/H6 主线内核平台处理
		"OrangePiH3_mainline" | "OrangePiH6_mainline")
			# 复制主线内核所需的启动文件
			cp -rfa ${EXTER}/chips/${CHIP}/mainline/boot_files/uInitrd ${BUILD}/uInitrd
			cp -rfa ${EXTER}/chips/${CHIP}/mainline/boot_files/orangepiEnv.txt ${BUILD}/orangepiEnv.txt
			# 生成 U-Boot 启动脚本（从 boot.cmd 编译为 boot.scr）
			mkimage -C none -A arm -T script -d ${EXTER}/chips/${CHIP}/mainline/boot_files/boot.cmd ${EXTER}/chips/${CHIP}/mainline/boot_files/boot.scr
			cp -rfa ${EXTER}/chips/${CHIP}/mainline/boot_files/boot.* ${BUILD}/

			# 主线内核使用带 SPL 的单一 U-Boot 镜像
			uboot="${BUILD}/uboot/u-boot-sunxi-with-spl.bin-${BOARD}"
			dd if="$uboot" conv=notrunc bs=1k seek=$boot0_position of="$IMAGE"

			if [ ${PLATFORM} = "OrangePiH6_mainline" ];then
				# H6 主线：使用 ARM64 Image 格式内核
				cp -rfa ${BUILD}/kernel/Image_${BOARD} ${BUILD}/kernel/Image
				mcopy -m -i ${IMAGE}1 ${BUILD}/kernel/Image ::
			elif [ ${PLATFORM}= "OrangePiH3_mainline" ];then
				# H3 主线：使用 ARM32 zImage 格式内核
				cp -rfa ${BUILD}/kernel/zImage_${BOARD} ${BUILD}/kernel/zImage
				mcopy -m -i ${IMAGE}1 ${BUILD}/kernel/zImage ::
			fi

			# 复制 initrd、环境变量、启动脚本、符号表、设备树到 boot 分区
			mcopy -m -i ${IMAGE}1 ${BUILD}/uInitrd :: || true
			mcopy -m -i ${IMAGE}1 ${BUILD}/orangepiEnv.txt :: || true
			mcopy -m -i ${IMAGE}1 ${BUILD}/boot.* :: || true
			mcopy -m -i ${IMAGE}1 ${BUILD}/kernel/System.map-${BOARD} :: || true
			mcopy -sm -i ${IMAGE}1 ${BUILD}/dtb :: || true
			;;
		"*")
			# 未知平台报错退出
			echo -e "\e[1;31m Pls select correct platform \e[0m"
			exit 0
			;;
	esac

	# ===== 计算最终镜像大小并创建 rootfs 分区 =====
	# 镜像总大小 = (rootfs 实际大小 + 分区起始偏移) / 1024 + 400MB 缓冲 + boot 分区大小
	disk_size=$[(`du -s $DEST | awk 'END {print $1}'`+part_position)/1024+400+boot_size]

	# 镜像最小要求 60MB
	if [ "$disk_size" -lt 60 ]; then
		echo "Disk size must be at least 60 MiB"
		exit 2
	fi

	echo "Creating image $IMAGE of size $disk_size MiB ..."

	# 将 boot 分区镜像追加到主镜像文件（从 20MB 位置开始）
	dd if=${IMAGE}1 conv=notrunc oflag=append bs=1M seek=$((part_position/1024)) of="$IMAGE"
	# 删除临时 boot 分区镜像文件
	rm -f ${IMAGE}1

	# 创建 rootfs 分区镜像（ext4 文件系统）
	dd if=/dev/zero bs=1M count=$((disk_size-boot_size-part_position/1024)) of=${IMAGE}2
	# 格式化 rootfs 为 ext4
	mkfs.ext4 -O ^metadata_csum -F -b 4096 -E stride=2,stripe-width=1024 -L rootfs ${IMAGE}2

	# 创建临时挂载点
	if [ ! -d /media/tmp ]; then
		mkdir -p /media/tmp
	fi

	# 挂载 rootfs 镜像并复制文件系统内容
	mount -t ext4 ${IMAGE}2 /media/tmp
	# 复制根文件系统到镜像
	cp -rfa $DEST/* /media/tmp

	# 卸载镜像
	umount /media/tmp

	# 将 rootfs 分区追加到主镜像（从 boot 分区结束位置开始）
	dd if=${IMAGE}2 conv=notrunc oflag=append bs=1M seek=$((part_position/1024+boot_size)) of="$IMAGE"
	# 删除临时 rootfs 镜像文件
	rm -f ${IMAGE}2

	# 清理临时目录
	if [ -d ${BUILD}/orangepi ]; then
		rm -rf ${BUILD}/orangepi
	fi

	if [ -d /media/tmp ]; then
		rm -rf /media/tmp
	fi

	# ===== 创建 MBR 分区表 =====
	# 使用 fdisk 交互式命令创建两个分区
	cat <<EOF | fdisk "$IMAGE"
o                                                        # 创建新的 DOS 分区表
n                                                        # 新建分区
p                                                        # 主分区
1                                                        # 分区号 1
$((part_position*2))                                     # 起始扇区（20MB 位置）
+${boot_size}M                                           # 大小 50MB
t                                                        # 修改分区类型
c                                                        # 类型 c = W95 FAT32 (LBA)
n                                                        # 新建分区
p                                                        # 主分区
2                                                        # 分区号 2
$((part_position*2 + boot_size*1024*2))                  # 起始扇区（boot 分区结束位置）
                                                         # 结束扇区（默认到磁盘末尾）
t                                                        # 修改分区类型
2                                                        # 选择分区 2
83                                                       # 类型 83 = Linux
w                                                        # 写入分区表并退出
EOF

	# 切换到镜像输出目录
	cd ${BUILD}/images/
	# 删除旧的压缩包
	rm -f ${IMAGENAME}.tar.gz
	# 生成 MD5 校验和
	md5sum ${IMAGE} > ${IMAGE}.md5sum
	# 打包镜像和校验和文件
	tar czvf  ${IMAGENAME}.tar.gz $IMAGENAME.img*
	# 删除临时校验和文件
	rm -f *.md5sum

	# 同步磁盘缓存
	sync
}
