#!/bin/bash

###############################################################################
# distributions.sh - Linux发行版根文件系统构建脚本
# 功能：负责创建、配置和打包Ubuntu/Debian rootfs
###############################################################################

# 安装XFCE桌面环境（轻量级桌面环境）
install_xfce_desktop()
{
	# 复制宿主机的DNS配置到目标rootfs，使chroot环境可以联网
 	cp /etc/resolv.conf "$DEST/etc/resolv.conf"
	# 创建临时脚本文件，用于在chroot环境中安装XFCE桌面
	cat > "$DEST/type-phase" <<EOF
#!/bin/bash
# 设置非交互式安装，避免安装过程中弹出确认对话框
export DEBIAN_FRONTEND=noninteractive
# 安装XFCE桌面核心组件、附加工具、VLC播放器和网络管理器
apt-get -y install xorg xfce4 xfce4-goodies vlc network-manager-gnome

# 清理不再需要的依赖包
apt-get -y autoremove
# 清理apt缓存以减小rootfs体积
apt-get clean

EOF
        # 给脚本添加执行权限
        chmod +x "$DEST/type-phase"
        # 在chroot环境中执行安装脚本
        do_chroot /type-phase
	# 同步文件系统，确保所有写入操作完成
	sync
	# 删除临时脚本
	rm -f "$DEST/type-phase"
	# 删除DNS配置文件（避免残留宿主机配置）
	rm -f "$DEST/etc/resolv.conf"
}

# 安装LXDE桌面环境（比XFCE更轻量）
install_lxde_desktop()
{
	# 设置默认用户名
	_user="orangepi"
	# 设置apt-get自动确认和静默模式参数
	_auto="-y -q"
	# 根据发行版确定操作系统类型（Ubuntu或Debian）
	if [ $DISTRO = "bionic" -o $DISTRO = "xenial" ]; then
		_DST=Ubuntu
	else
		_DST=Debian
	fi
	# 复制DNS配置以支持网络安装
	cp /etc/resolv.conf "$DEST/etc/resolv.conf"
	# 创建LXDE安装脚本
	cat > "$DEST/type-phase" <<EOF
#!/bin/bash

# 显示空行和当前日期
echo ""
date
# 打印安装提示信息（带颜色）
echo -e "\033[36m======================="
echo -e "Installing LXDE Desktop"
echo -e "=======================\033[37m"
# 恢复终端默认颜色
setterm -default
echo ""



# 更新软件包索引
echo "Package update..."
apt-get $_auto update
# 注释掉的升级命令（可避免不必要的升级）
#echo "Package upgrade..."
#apt-get $_auto upgrade
echo ""

# 显示正在安装的发行版和桌面环境
echo "$_DST - $_REL, Installing LXDE DESKTOP..."

# === 安装桌面环境核心组件 ===========================================
echo "  installing xserver & lxde desktop, please wait..."
# 安装X Window系统初始化工具和X服务器
apt-get $_auto install xinit xserver-xorg
# 安装LXDE桌面、LightDM显示管理器、认证工具（不安装推荐包以节省空间）
apt-get $_auto install lxde lightdm lightdm-gtk-greeter policykit-1 --no-install-recommends
# 安装网络工具（ifconfig等）
apt-get $_auto install net-tools
# 安装LXDE会话注销工具
apt-get $_auto install lxsession-logout
# 清理apt缓存
apt-get clean

# Ubuntu发行版需要安装特定的图标主题
if [ "${_DST}" = "Ubuntu" ] ; then
	apt-get $_auto install humanity-icon-theme --no-install-recommends
fi

# 安装音频系统（PulseAudio + ALSA）和音频控制工具
apt-get $_auto install pulseaudio pulseaudio-utils alsa-oss alsa-utils alsa-tools libasound2-data pavucontrol
# 安装媒体播放器
apt-get $_auto install smplayer
# 安装系统工具：软件包管理器、任务管理器、计算器、认证代理
apt-get $_auto install synaptic software-properties-gtk lxtask galculator policykit-1-gnome --no-install-recommends
apt-get clean

# === 安装网络组件和浏览器 ===========================================
# === 如果不需要浏览器可以注释掉，可节省约100MB空间 ===

echo "  installing network packages, please wait..."
# Ubuntu使用chromium-browser，Debian使用chromium
if [ "${_DST}" = "Ubuntu" ]; then
	apt-get $_auto install chromium-browser gvfs-fuse gvfs-backends --no-install-recommends
else
	apt-get $_auto install chromium gvfs-fuse gvfs-backends --no-install-recommends
fi
# 安装NetworkManager的GNOME前端
apt-get $_auto install network-manager-gnome
apt-get clean


# === 配置桌面环境 ===================================================
echo ""
echo "Configuring desktop..."

# 配置X Window允许任何用户启动（默认只允许console用户）
if [ -f /etc/X11/Xwrapper.config ]; then
    cat /etc/X11/Xwrapper.config | sed s/"allowed_users=console"/"allowed_users=anybody"/g > /tmp/_xwrap
    mv /tmp/_xwrap /etc/X11/Xwrapper.config
fi

# 配置LightDM登录界面的背景图片
if [ -f /etc/lightdm/lightdm-gtk-greeter.conf ]; then
    # 删除原有background配置
    cat /etc/lightdm/lightdm-gtk-greeter.conf | sed "/background=\/usr/d" > /tmp/_greet
    mv /tmp/_greet /etc/lightdm/lightdm-gtk-greeter.conf
    # 添加新的OrangePi背景图片
    cat /etc/lightdm/lightdm-gtk-greeter.conf | sed '/\[greeter\]/abackground=\/usr\/share\/lxde\/wallpapers\/orangepi.jpg' > /tmp/_greet
    mv /tmp/_greet /etc/lightdm/lightdm-gtk-greeter.conf
fi

#*********************
# ** 配置音频系统
#*********************
# 配置ALSA默认音频设备（card 1通常是HDMI音频输出）
cat > /etc/asound.conf << _EOF_
pcm.!default {
    type hw
    card 1    # 如果要将HDMI设为输出，将0改为1
    device 0
  }
  ctl.!default {
    type hw
    card 1   # 如果要将HDMI设为输出，将0改为1
  }
_EOF_

# 配置PulseAudio禁用定时器调度（解决某些音频问题）
if [ -f /etc/pulse/default.pa ]; then
    cat /etc/pulse/default.pa | sed s/"load-module module-udev-detect$"/"load-module module-udev-detect tsched=0"/g > /tmp/default.pa
    mv /tmp/default.pa /etc/pulse/default.pa
fi


# 将orangepi用户添加到必要的系统组
# adm:系统日志访问 dialout:串口访问 cdrom:光驱 dip:网络 video:视频设备
# plugdev:可移动设备 netdev:网络配置 fuse:FUSE文件系统
usermod -a -G adm,dialout,cdrom,dip,video,plugdev,netdev,fuse $_user

# 修复用户主目录权限
chown -R $_user:$_user /home/$_user

EOF
	# 给安装脚本添加执行权限
	chmod +x "$DEST/type-phase"
	# 在chroot环境中执行安装脚本
 	do_chroot /type-phase
	# 同步文件系统
	sync
	# 清理临时文件
	rm -f "$DEST/type-phase"
	rm -f "$DEST/etc/resolv.conf"

}

# 使用debootstrap从零构建rootfs（适用于Debian系统）
deboostrap_rootfs() {
	# 发行版名称（如stretch）
	dist="$1"
	# rootfs打包输出文件（.tar.gz）
	tgz="$(readlink -f "$2")"
	# 创建临时工作目录
	TEMP=$(mktemp -d)

	# 确保临时目录创建成功
	[ "$TEMP" ] || exit 1
	cd $TEMP && pwd

	# Debian归档密钥环包（用于验证软件包签名）
	# 这个更新不频繁，所以硬编码URL是可以的
	debian_archive_keyring_deb="${SOURCES}/pool/main/d/debian-archive-keyring/debian-archive-keyring_2019.1_all.deb"
	# 下载密钥环包
	wget -O keyring.deb "$debian_archive_keyring_deb"
	# 解压deb包（ar是deb包的归档格式）
	ar -x keyring.deb && rm -f control.tar.gz debian-binary && rm -f keyring.deb
	# 检测数据压缩格式（可能是.gz, .xz等）
	DATA=$(ls data.tar.*) && compress=${DATA#data.tar.}

	# 提取GPG密钥环文件
	KR=debian-archive-keyring.gpg
	bsdtar --include ./usr/share/keyrings/$KR --strip-components 4 -xvf "$DATA"
	rm -f "$DATA"

	# 安装debootstrap工具和QEMU用户态模拟器（用于交叉架构chroot）
	apt-get -y install debootstrap qemu-user-static

	# 使用qemu-debootstrap构建ARM64/ARMHF rootfs
	# --arch指定目标架构，--keyring指定验证密钥
	qemu-debootstrap --arch=${ARCH} --keyring=$TEMP/$KR $dist rootfs ${SOURCES}
	rm -f $KR

	# 清理qemu静态二进制（稍后会重新复制）
	# 根据架构删除对应的qemu模拟器
#	rm -f rootfs/usr/bin/qemu-arm-static
       if [ $ARCH = "arm64" ]; then
               rm -f rootfs/usr/bin/qemu-aarch64-static
       elif [ $ARCH = "armhf" ]; then
               rm -f rootfs/usr/bin/qemu-arm-static
       fi

	# 将rootfs打包成tar.gz
	bsdtar -C $TEMP/rootfs -a -cf $tgz .
	rm -fr $TEMP/rootfs

	cd -
}

# 在ARM rootfs中执行命令（通过chroot和QEMU模拟）
do_chroot() {
	# 复制QEMU静态二进制到rootfs，使x86_64主机可以执行ARM二进制
	# QEMU用户态模拟允许在非ARM平台上执行ARM程序
#	cp /usr/bin/qemu-arm-static "$DEST/usr/bin"
       if [ $ARCH = "arm64" ]; then
               # ARM64架构使用qemu-aarch64-static
               cp /usr/bin/qemu-aarch64-static "$DEST/usr/bin"
       elif [ $ARCH = "arm" ]; then
               # ARM32架构使用qemu-arm-static
               cp /usr/bin/qemu-arm-static "$DEST/usr/bin"
       fi

	# 获取要执行的命令参数
	cmd="$@"
	# 挂载proc文件系统（进程信息，很多程序依赖）
	chroot "$DEST" mount -t proc proc /proc || true
	# 挂载sys文件系统（设备和内核信息）
	chroot "$DEST" mount -t sysfs sys /sys || true
	# 在chroot环境中执行命令
	chroot "$DEST" $cmd
	# 卸载文件系统
	chroot "$DEST" umount /sys
	chroot "$DEST" umount /proc

	# 清理QEMU二进制文件
	rm -f "$DEST/usr/bin/qemu-arm-static"
}

# 复制平台相关的配置文件和工具到rootfs
do_conffile() {
        # 创建boot目录用于存放启动文件
        mkdir -p $DEST/opt/boot
	# 根据不同硬件平台复制对应的文件
	if [ "${PLATFORM}" = "OrangePiH3" ]; then
		# H3平台（Allwinner H3 SoC）
        	cp $EXTER/install_to_emmc_$OS $DEST/usr/local/sbin/install_to_emmc -f
        	cp $EXTER/uboot/*.bin $DEST/opt/boot/ -f
        	cp $EXTER/resize_rootfs.sh $DEST/usr/local/sbin/ -f
	elif [ "${PLATFORM}" = "OrangePiH3_mainline" ]; then
		# H3主线内核平台
		cp $BUILD/uboot/u-boot-sunxi-with-spl.bin-${BOARD} $DEST/opt/boot/u-boot-sunxi-with-spl.bin -f
        	cp $EXTER/mainline/install_to_emmc_$OS $DEST/usr/local/sbin/install_to_emmc -f
        	cp $EXTER/mainline/resize_rootfs.sh $DEST/usr/local/sbin/ -f
        	cp $EXTER/mainline/boot_emmc/* $DEST/opt/boot/ -f
	elif [ "${PLATFORM}" = "OrangePiRK3399" ]; then
		# RK3399平台（Rockchip RK3399 SoC）
		# 复制eMMC安装脚本
		cp $EXTER/install_to_emmc_$OS $DEST/usr/local/sbin/install_to_emmc -f
		# 创建存放固件镜像的目录
		mkdir -p $DEST/usr/local/lib/install_to_emmc
		# 复制U-Boot镜像（uboot.img, trust.img, idbloader.img等）
		cp $BUILD/uboot/*.img $DEST/usr/local/lib/install_to_emmc/ -f
		# 复制内核镜像（boot.img包含kernel+dtb+ramdisk）
		cp $BUILD/kernel/boot.img $DEST/usr/local/lib/install_to_emmc/ -f
		# 创建固件目录（用于WiFi/BT固件）
		[ -d $DEST/system/etc/firmware ] || mkdir -p $DEST/system/etc/firmware
		# 复制无线和蓝牙固件
		cp -rf $EXTER/firmware/* $DEST/system/etc/firmware
		# 复制ALSA音频配置状态
		cp -rf $EXTER/asound.state $DEST/var/lib/alsa/
		# 清空fstab文件（RK3399使用动态挂载）
		echo "" > $DEST/etc/fstab

	else
	        # 未知平台，报错退出
	        echo -e "\e[1;31m Pls select correct platform \e[0m"
	        exit 0
	fi

	# 以下配置对所有平台通用
        # 复制SSH配置（可能禁用root密码登录等安全配置）
        cp $EXTER/sshd_config $DEST/etc/ssh/ -f
        # 复制root用户的profile配置
        cp $EXTER/profile_for_root $DEST/root/.profile -f
        # 复制蓝牙启动脚本
        cp $EXTER/bluetooth/bt.sh $DEST/usr/local/sbin/ -f
        # 复制博通蓝牙固件加载工具
        cp $EXTER/bluetooth/brcm_patchram_plus/brcm_patchram_plus $DEST/usr/local/sbin/ -f
        # 给/usr/local/sbin下所有脚本添加执行权限
        chmod +x $DEST/usr/local/sbin/*
}

# 添加SSH密钥自动生成服务（首次启动时生成SSH host keys）
add_ssh_keygen_service() {
	# 创建systemd服务单元文件
	cat > "$DEST/etc/systemd/system/ssh-keygen.service" <<EOF
[Unit]
Description=Generate SSH keys if not there
# 确保在SSH服务启动前执行
Before=ssh.service
# 以下条件为"或"关系：只要有任何一个密钥不存在就执行服务
ConditionPathExists=|!/etc/ssh/ssh_host_key
ConditionPathExists=|!/etc/ssh/ssh_host_key.pub
ConditionPathExists=|!/etc/ssh/ssh_host_rsa_key
ConditionPathExists=|!/etc/ssh/ssh_host_rsa_key.pub
ConditionPathExists=|!/etc/ssh/ssh_host_dsa_key
ConditionPathExists=|!/etc/ssh/ssh_host_dsa_key.pub
ConditionPathExists=|!/etc/ssh/ssh_host_ecdsa_key
ConditionPathExists=|!/etc/ssh/ssh_host_ecdsa_key.pub
ConditionPathExists=|!/etc/ssh/ssh_host_ed25519_key
ConditionPathExists=|!/etc/ssh/ssh_host_ed25519_key.pub

[Service]
# -A 参数自动生成所有类型的host keys
ExecStart=/usr/bin/ssh-keygen -A
# 一次性服务
Type=oneshot
# 执行后保持激活状态
RemainAfterExit=yes

[Install]
# SSH服务依赖此服务
WantedBy=ssh.service
EOF
	# 启用服务（在chroot环境中）
	do_chroot systemctl enable ssh-keygen
}

# 安装OrangePi GPIO Python库（用于GPIO硬件控制）
add_opi_python_gpio_libs() {
        # 复制OPi.GPIO库源码到rootfs
        cp $EXTER/packages/OPi.GPIO $DEST/usr/local/sbin/ -rfa
        # 复制GPIO测试脚本
        cp $EXTER/packages/OPi.GPIO/test_gpio.py $DEST/usr/local/sbin/ -f

        # 创建安装脚本
        cat > "$DEST/install_opi_gpio" <<EOF
#!/bin/bash

# 更新软件包索引
apt-get update
# 安装Python3开发工具
apt-get install -y python3-pip python3-setuptools
# 进入库目录并安装
cd /usr/local/sbin/OPi.GPIO
python3 setup.py install
EOF
        # 给脚本添加执行权限
        chmod +x "$DEST/install_opi_gpio"
        # 在chroot环境中执行安装
        do_chroot /install_opi_gpio
	# 删除临时安装脚本
	rm $DEST/install_opi_gpio
}

# 添加蓝牙服务（启动时自动初始化蓝牙硬件）
add_bt_service() {
        # 创建systemd服务单元
        cat > "$DEST/lib/systemd/system/bt.service" <<EOF
[Unit]
Description=OrangePi BT Service

[Service]
# 执行蓝牙初始化脚本
ExecStart=/usr/local/sbin/bt.sh
# 服务退出后保持激活状态
RemainAfterExit=yes

[Install]
# 在多用户运行级别启动
WantedBy=multi-user.target
EOF
        # 启用蓝牙服务
        do_chroot systemctl enable bt.service
}


# 添加OrangePi配置工具库（opi-config系统配置工具）
add_opi_config_libs() {
	# 安装配置工具所需的依赖包
	# dialog:TUI对话框 expect:自动化交互 bc:计算器 cpufrequtils:CPU频率管理
	# figlet/toilet:ASCII艺术字 lsb-release:系统版本信息
	do_chroot apt-get install -y dialog expect bc cpufrequtils figlet toilet lsb-release
        # 复制opi-config库文件
        cp $EXTER/packages/opi_config_libs $DEST/usr/local/sbin/ -rfa
        # 复制opi-config主程序
        cp $EXTER/packages/opi_config_libs/opi-config $DEST/usr/local/sbin/ -rfa

	# 删除默认的MOTD（每日提示）脚本
	rm -rf $DEST/etc/update-motd.d/*
	# 复制自定义的MOTD和配置文件
        cp $EXTER/packages/opi_config_libs/overlay/* $DEST/ -rf
}

# 配置Debian软件源
add_debian_apt_sources() {
	local release="$1"  # 发行版版本（如stretch, buster）
	local aptsrcfile="$DEST/etc/apt/sources.list"
	# 创建主软件源配置
	cat > "$aptsrcfile" <<EOF
deb ${SOURCES} ${release} main contrib non-free
#deb-src ${SOURCES} ${release} main contrib non-free
EOF
	# sid（unstable）没有单独的安全和更新仓库
	# 对于稳定版，添加updates和security仓库
	[ "$release" = "sid" ] || cat >> "$aptsrcfile" <<EOF
deb ${SOURCES} ${release}-updates main contrib non-free
#deb-src ${SOURCES} ${release}-updates main contrib non-free

deb http://security.debian.org/ ${release}/updates main contrib non-free
#deb-src http://security.debian.org/ ${release}/updates main contrib non-free
EOF
}

# 配置Ubuntu软件源
add_ubuntu_apt_sources() {
	local release="$1"  # 发行版版本（如xenial, bionic）
	cat > "$DEST/etc/apt/sources.list" <<EOF
deb ${SOURCES} ${release} main restricted universe multiverse
deb-src ${SOURCES} ${release} main restricted universe multiverse

deb ${SOURCES} ${release}-updates main restricted universe multiverse
deb-src ${SOURCES} ${release}-updates main restricted universe multiverse

deb ${SOURCES} ${release}-security main restricted universe multiverse
deb-src $SOURCES ${release}-security main restricted universe multiverse

deb ${SOURCES} ${release}-backports main restricted universe multiverse
deb-src ${SOURCES} ${release}-backports main restricted universe multiverse
EOF
}

# 准备构建环境（检查目标目录、配置软件源、下载base rootfs）
prepare_env()
{
	# 检查目标目录是否存在
	if [ ! -d "$DEST" ]; then
		echo "Destination $DEST not found or not a directory."
		echo "Create $DEST"
		mkdir -p $DEST
	fi

	# 如果目标目录不为空（忽略lost+found），先清空
	if [ "$(ls -A -Ilost+found $DEST)" ]; then
		echo "Destination $DEST is not empty."
		echo "Clean up space."
		rm -rf $DEST
	fi

	# 定义清理函数（脚本退出时自动执行）
	cleanup() {
		# 卸载可能挂载的proc文件系统
		if [ -e "$DEST/proc/cmdline" ]; then
			umount "$DEST/proc"
		fi
		# 卸载可能挂载的sys文件系统
		if [ -d "$DEST/sys/kernel" ]; then
			umount "$DEST/sys"
		fi
		# 清理临时目录
		if [ -d "$TEMP" ]; then
			rm -rf "$TEMP"
		fi
	}
	# 注册EXIT陷阱，脚本退出时自动执行cleanup
	trap cleanup EXIT

	# 根据发行版选择软件源和base rootfs下载地址
	case $DISTRO in
		xenial)  # Ubuntu 16.04
			case $SOURCES in
				"CDN"|"OFCL")  # 官方源
			       	        SOURCES="http://ports.ubuntu.com"
					ROOTFS="http://cdimage.ubuntu.com/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-16.04-core-${ARCH}.tar.gz"
				        ;;
				"CN")  # 中国镜像源
				        #SOURCES="http://mirrors.aliyun.com/ubuntu-ports"
		                        #SOURCES="http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports"
				        SOURCES="http://mirrors.ustc.edu.cn/ubuntu-ports"
					ROOTFS="https://mirrors.tuna.tsinghua.edu.cn/ubuntu-cdimage/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-16.04-core-${ARCH}.tar.gz"
				        ;;
				*)
					SOURCES="http://ports.ubuntu.com"
					ROOTFS="http://cdimage.ubuntu.com/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-16.04-core-${ARCH}.tar.gz"
					;;
			esac
			;;
		bionic)  # Ubuntu 18.04
		        case $SOURCES in
		                "CDN"|"OFCL")
		                        SOURCES="http://ports.ubuntu.com"
					ROOTFS="http://cdimage.ubuntu.com/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-18.04-base-${ARCH}.tar.gz"
		                        ;;
		                "CN")
		                        #SOURCES="http://mirrors.aliyun.com/ubuntu-ports"
		                        SOURCES="http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports"
				        #SOURCES="http://mirrors.ustc.edu.cn/ubuntu-ports"
					ROOTFS="https://mirrors.tuna.tsinghua.edu.cn/ubuntu-cdimage/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-18.04-base-${ARCH}.tar.gz"
		                        ;;
		                *)
		                        SOURCES="http://ports.ubuntu.com"
					ROOTFS="http://cdimage.ubuntu.com/ubuntu-base/releases/${DISTRO}/release/ubuntu-base-18.04-base-${ARCH}.tar.gz"
		                        ;;
		        esac
		        ;;
		stretch)  # Debian 9
			ROOTFS="${DISTRO}-base-${ARCH}.tar.gz"
			METHOD="debootstrap"  # Debian使用debootstrap方法构建
			case $SOURCES in
		                "CDN")
		                        SOURCES="http://httpredir.debian.org/debian"
		                        ;;
		                "OFCL")
		                        SOURCES="http://ftp2.debian.org/debian"
		                        ;;
		                "CN")
		                        SOURCES="http://ftp2.cn.debian.org/debian"
		                        ;;
				*)
					SOURCES="http://httpredir.debian.org/debian"
		                        ;;
		        esac
			;;
		*)
			echo "Unknown distribution: $DISTRO"
			exit 1
			;;
	esac

	# 确定rootfs tarball的本地路径
	TARBALL="$EXTER/$(basename $ROOTFS)"
	# 如果tarball不存在，下载或构建
	if [ ! -e "$TARBALL" ]; then
		if [ "$METHOD" = "download" ]; then
			# Ubuntu使用下载预构建的base rootfs
			echo "Downloading $DISTRO rootfs tarball ..."
			wget -O "$TARBALL" "$ROOTFS"
		elif [ "$METHOD" = "debootstrap" ]; then
			# Debian使用debootstrap从零构建
			deboostrap_rootfs "$DISTRO" "$TARBALL"
		else
			echo "Unknown rootfs creation method"
			exit 1
		fi
	fi

	# 解压rootfs到目标目录（使用BSD tar）
	echo -n "Extracting ... "
	mkdir -p $DEST
	$UNTAR "$TARBALL" -C "$DEST"
	echo "OK"
}

# 准备服务器版rootfs（安装基础软件包、创建用户）
prepare_rootfs_server()
{

	# 删除旧的resolv.conf，复制宿主机的DNS配置
	rm "$DEST/etc/resolv.conf"
	cp /etc/resolv.conf "$DEST/etc/resolv.conf"
	# 根据发行版设置不同的配置
	if [ "$DISTRO" = "xenial" -o "$DISTRO" = "bionic" ]; then
		DEB=ubuntu
		DEBUSER=orangepi
		# Ubuntu额外安装的包
		EXTRADEBS="software-properties-common libjpeg8-dev usbmount zram-config ubuntu-minimal net-tools"
		ADDPPACMD=
		DISPTOOLCMD=
	elif [ "$DISTRO" = "sid" -o "$DISTRO" = "stretch" -o "$DISTRO" = "stable" ]; then
		DEB=debian
		DEBUSER=orangepi
		# Debian额外安装的包
		EXTRADEBS="sudo net-tools g++ libjpeg-dev"
		ADDPPACMD=
		DISPTOOLCMD=
	else
		echo "Unknown DISTRO=$DISTRO"
		exit 2
	fi
	# 添加对应发行版的软件源配置
	add_${DEB}_apt_sources $DISTRO
	# 删除proposed源（测试版软件源）
	rm -rf "$DEST/etc/apt/sources.list.d/proposed.list"
	# 创建第二阶段安装脚本（在chroot环境中执行）
	cat > "$DEST/second-phase" <<EOF
#!/bin/bash
# 非交互式安装
export DEBIAN_FRONTEND=noninteractive
# 生成英文UTF-8 locale
locale-gen en_US.UTF-8

# 更新软件包索引
apt-get -y update
# 安装基础系统工具
apt-get -y install dosfstools curl xz-utils iw rfkill ifupdown
# 安装网络和SSH工具
apt-get -y install wpasupplicant openssh-server alsa-utils
apt-get -y install rsync u-boot-tools vim
# 安装开发工具
apt-get -y install parted network-manager git autoconf gcc libtool
apt-get -y install libsysfs-dev pkg-config libdrm-dev xutils-dev hostapd
apt-get -y install dnsmasq apt-transport-https man subversion
apt-get -y install imagemagick libv4l-dev cmake bluez
# 安装额外的平台特定软件包
apt-get -y install $EXTRADEBS

# 修复可能的依赖问题
apt-get install -f

# 删除ureadahead（SSD优化不需要）
apt-get -y remove --purge ureadahead
$ADDPPACMD
apt-get -y update
$DISPTOOLCMD
# 创建普通用户orangepi（UID 1000）
adduser --gecos $DEBUSER --disabled-login $DEBUSER --uid 1000
# 重新添加root用户（UID 0）
adduser --gecos root --disabled-login root --uid 0
# 设置root密码为orangepi
echo root:orangepi | chpasswd
# 修复用户主目录权限
chown -R 1000:1000 /home/$DEBUSER
# 设置orangepi用户密码为orangepi
echo "$DEBUSER:$DEBUSER" | chpasswd
# 将orangepi用户添加到必要的组
usermod -a -G sudo $DEBUSER      # sudo权限
usermod -a -G adm $DEBUSER       # 日志访问
usermod -a -G video $DEBUSER     # 视频设备
usermod -a -G plugdev $DEBUSER   # 可插拔设备
# 清理
apt-get -y autoremove
apt-get clean
EOF
	# 给脚本添加执行权限
	chmod +x "$DEST/second-phase"
	# 在chroot环境中执行第二阶段安装
	do_chroot /second-phase
	# 清理临时文件
	rm -f "$DEST/second-phase"
        rm -f "$DEST/etc/resolv.conf"

	# 打包服务器版rootfs为tar.gz（可复用，加速后续构建）
	cd $BUILD
	tar czf ${DISTRO}_server_rootfs.tar.gz rootfs
	cd -
}

# 准备桌面版rootfs（在服务器版基础上安装桌面环境）
prepare_rootfs_desktop()
{
	# 安装LXDE桌面环境
	install_lxde_desktop
	# 打包桌面版rootfs
	cd $BUILD
	tar czf ${DISTRO}_desktop_rootfs.tar.gz rootfs
	cd -

}

# 服务器配置（网络、主机名、系统服务、内核模块安装）
server_setup()
{
	# zero_plus2_h3板子不需要eth0配置
	if [ $BOARD = "zero_plus2_h3" ];then
		:
	else
	# 配置以太网接口使用DHCP自动获取IP
	cat > "$DEST/etc/network/interfaces.d/eth0" <<EOF
auto eth0
iface eth0 inet dhcp
EOF
	fi
	# 设置主机名（如orangepi4）
	cat > "$DEST/etc/hostname" <<EOF
orangepi${BOARD}
EOF
	# 配置hosts文件
	cat > "$DEST/etc/hosts" <<EOF
127.0.0.1 localhost
127.0.1.1 orangepi${BOARD}

# 以下行为支持IPv6的主机所需
::1     localhost ip6-localhost ip6-loopback
fe00::0 ip6-localnet
ff00::0 ip6-mcastprefix
ff02::1 ip6-allnodes
ff02::2 ip6-allrouters
EOF
	# 配置DNS服务器（Google DNS）
	cat > "$DEST/etc/resolv.conf" <<EOF
nameserver 8.8.8.8
EOF

	# 复制平台相关配置文件
	do_conffile
	# 添加各种系统服务
	add_ssh_keygen_service          # SSH密钥生成服务
	add_opi_python_gpio_libs        # GPIO库
	add_opi_config_libs             # 配置工具
	add_bt_service                  # 蓝牙服务
	# 修改serial-getty服务配置（注释掉rc.local依赖）
	sed -i 's|After=rc.local.service|#\0|;' "$DEST/lib/systemd/system/serial-getty@.service"
	# 删除SSH host keys（由ssh-keygen服务首次启动时生成）
	rm -f "$DEST"/etc/ssh/ssh_host_*

	# 恢复必要的目录结构
	mkdir -p "$DEST/lib"
	mkdir -p "$DEST/usr"

	# 创建fstab文件（文件系统挂载表）
	cat  > "$DEST/etc/fstab" <<EOF
# <file system>	<dir>	<type>	<options>			<dump>	<pass>
/dev/mmcblk0p1	/boot	vfat	defaults			0		2
/dev/mmcblk0p2	/	ext4	defaults,noatime		0		1
EOF
	# 创建/lib/modules目录（用于内核模块）
	if [ ! -d $DEST/lib/modules ]; then
		mkdir "$DEST/lib/modules"
	else
		rm -rf $DEST/lib/modules
		mkdir "$DEST/lib/modules"
	fi

	# RK3399平台特殊配置
	if [ $PLATFORM = "OrangePiRK3399" ]; then
		# 清空fstab（RK3399使用不同的分区方案）
		echo "" > $DEST/etc/fstab
		# 添加ttyFIQ0到securetty（允许root从该串口登录）
		echo "ttyFIQ0" >> $DEST/etc/securetty
		# 缩短网络服务超时时间（从5分钟改为15秒）
		sed -i '/^TimeoutStartSec=/s/5min/15sec/' $DEST/lib/systemd/system/networking.service
		# 设置首次启动时自动扩展rootfs（函数定义在platform/rk3399.sh中）
		setup_resize-helper
	fi
	# 安装内核模块到rootfs
	make -C $LINUX ARCH=${ARCH} CROSS_COMPILE=$TOOLS modules_install INSTALL_MOD_PATH="$DEST"

	# 安装内核头文件（用于编译驱动）
	make -C $LINUX ARCH=${ARCH} CROSS_COMPILE=$TOOLS headers_install INSTALL_HDR_PATH="$DEST/usr/local"
	# 复制固件文件（WiFi/BT固件等）
	cp $EXTER/firmware $DEST/lib/ -rf

	# 可选：备份rootfs（当前已注释）
	#rm -rf $BUILD/${DISTRO}_${IMAGETYPE}_rootfs
	#cp -rfa $DEST $BUILD/${DISTRO}_${IMAGETYPE}_rootfs
}

# 桌面环境额外配置（RK3399特定）
desktop_setup()
{
	if [ $PLATFORM = "OrangePiRK3399" ]; then
		# 修改LXDE桌面壁纸
		sed -i '/^wallpaper=/s/\/etc\/alternatives\/desktop-background/\/usr\/share\/lxde\/wallpapers\/newxitong_17.jpg/' $DEST/etc/xdg/pcmanfm/LXDE/pcmanfm.conf
		# 设置LXDE面板透明
		sed -i '/^[ ]*transparent=/s/0/1/' $DEST/etc/xdg/lxpanel/LXDE/panels/panel
		# 禁用面板背景
		sed -i '/^[ ]*background=/s/1/0/' $DEST/etc/xdg/lxpanel/LXDE/panels/panel

		# 配置NetworkManager只管理以太网、WiFi和WWAN设备
		echo -e "\n[keyfile]\nunmanaged-devices=*,except:type:ethernet,except:type:wifi,except:type:wwan" >> ${DEST}/etc/NetworkManager/NetworkManager.conf
		# Debian Stretch需要禁用WiFi MAC地址随机化
		[ $DISTRO = "stretch" ] && echo -e "\n[device]\nwifi.scan-rand-mac-address=no" >> $DEST/etc/NetworkManager/NetworkManager.conf
		# Debian Stretch安装glmark2（GPU性能测试工具）
		[ $DISTRO = "stretch" ] && cp -rfa $EXTER/packages/others/glmark2/* $DEST
		# Ubuntu Xenial需要额外设置
		[ $DISTRO = "xenial" ] && setup_front
		# 复制额外的软件包和配置文件
		cp -rfa $EXTER/packages $DEST
		cp -rfa $EXTER/packages/overlay/* $DEST
		# 安装GStreamer多媒体框架（函数定义在platform/rk3399.sh中）
		install_gstreamer
		# 安装GPU库（Mali GPU驱动）
		install_gpu_lib
		# 清理临时packages目录
		rm -rf $DEST/packages
	fi

}

# 构建rootfs的主函数（根据TYPE选择构建服务器版或桌面版）
build_rootfs()
{
	# 准备基础环境（下载/解压base rootfs）
	prepare_env

	# TYPE=1表示桌面版，TYPE=0表示服务器版
	if [ $TYPE = "1" ]; then
		# 构建桌面版rootfs
		if [ -f $BUILD/${DISTRO}_desktop_rootfs.tar.gz ]; then
			# 如果已有打包好的桌面版，直接解压使用
			rm -rf $DEST
			tar zxf $BUILD/${DISTRO}_desktop_rootfs.tar.gz -C $BUILD
		else
			# 否则需要先构建桌面版
			if [ -f $BUILD/${DISTRO}_server_rootfs.tar.gz ]; then
				# 如果有服务器版，在其基础上安装桌面环境
				rm -rf $DEST
				tar zxf $BUILD/${DISTRO}_server_rootfs.tar.gz -C $BUILD
				prepare_rootfs_desktop
			else
				# 从零开始：先构建服务器版，再安装桌面环境
				prepare_rootfs_server
				prepare_rootfs_desktop

			fi
		fi
		# 执行服务器配置和桌面配置
		server_setup
		desktop_setup
	else
		# 构建服务器版rootfs
		if [ -f $BUILD/${DISTRO}_server_rootfs.tar.gz ]; then
			# 如果已有打包好的服务器版，直接解压使用
			rm -rf $DEST
			tar zxf $BUILD/${DISTRO}_server_rootfs.tar.gz -C $BUILD
		else
			# 否则从零开始构建服务器版
			prepare_rootfs_server
		fi
		# 执行服务器配置
		server_setup
	fi
}

