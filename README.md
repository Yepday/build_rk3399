# OrangePi RK3399 项目架构与构建流程

> **精简版本** - 专注于学习项目架构和镜像打包流程

## 📦 项目说明

这是 OrangePi RK3399 开发板的精简版项目，**只保留了项目架构和构建脚本**，方便学习和理解嵌入式 Linux 系统的构建流程。

- **原始项目大小**: 2.9GB（包含完整源码）
- **精简后大小**: 709KB（只保留架构和构建脚本）
- **精简比例**: 99.97% 的体积减少

## 📁 项目结构

```
OrangePiRK3399/
├── scripts/                # 构建脚本（核心）
│   ├── build.sh           # 主构建脚本
│   ├── lib/
│   │   ├── build_image.sh # 镜像构建
│   │   ├── compilation.sh # 编译流程
│   │   ├── pack.sh        # 打包流程
│   │   └── platform/
│   │       └── rk3399.sh  # RK3399 平台配置
│
├── kernel/                # Linux 内核（只保留顶层配置）
│   ├── Makefile          # 内核构建入口
│   ├── Kconfig           # 内核配置
│   └── build.config.*    # 构建配置文件
│
├── uboot/                # U-Boot 引导加载器（只保留顶层配置）
│   ├── Makefile         # U-Boot 构建入口
│   ├── make.sh          # 构建脚本
│   ├── docs/            # 打包教程
│   │   ├── loader镜像打包教程.md
│   │   ├── trust镜像打包教程.md
│   │   └── uboot镜像打包教程.md
│   ├── 固件打包原理深度解析.md
│   └── 固件生成教学文档.md
│
├── external/            # 外部工具和脚本
│   ├── install_to_emmc  # 安装到 eMMC 脚本
│   ├── bluetooth/       # 蓝牙工具
│   └── README.md
│
└── toolchain/          # 交叉编译工具链（已排除）
```

## 🎯 保留的核心内容

### 1. 构建脚本 (`scripts/`)
- **build.sh**: 主构建脚本，协调整个编译流程
- **compilation.sh**: 编译 kernel 和 uboot
- **build_image.sh**: 生成最终镜像
- **pack.sh**: 打包固件
- **rk3399.sh**: RK3399 平台特定配置

### 2. 配置文件
- **Kernel**: Makefile, Kconfig, build.config
- **U-Boot**: Makefile, Kconfig, make.sh

### 3. 打包教程文档
- Loader 镜像打包教程
- Trust 镜像打包教程
- U-Boot 镜像打包教程
- 固件打包原理深度解析

## 🚀 构建流程学习路径

1. **了解项目架构**
   - 查看 `scripts/build.sh` 了解整体构建流程
   - 阅读 `scripts/lib/` 下的各个脚本模块

2. **学习内核构建**
   - 查看 `kernel/Makefile`
   - 理解 `kernel/build.config.*` 配置文件

3. **学习 U-Boot 构建**
   - 查看 `uboot/Makefile` 和 `uboot/make.sh`
   - 阅读 `uboot/docs/` 下的打包教程

4. **学习镜像打包**
   - 阅读 `uboot/固件打包原理深度解析.md`
   - 查看 `scripts/lib/pack.sh` 打包逻辑

## ⚙️ 完整源码获取

如果需要编译完整项目，需要获取以下源码：

```bash
# 1. Kernel 源码
git clone https://github.com/orangepi-xunlong/OrangePiRK3399_kernel.git kernel

# 2. U-Boot 源码
git clone https://github.com/orangepi-xunlong/OrangePiRK3399_uboot.git uboot

# 3. External 工具
git clone https://github.com/orangepi-xunlong/OrangePiRK3399_external.git external

# 4. 交叉编译工具链
# 需要下载 GCC Linaro 工具链
```

## 📚 学习重点

这个精简版本适合：
- ✅ 理解嵌入式 Linux 系统的构建架构
- ✅ 学习镜像打包和部署流程
- ✅ 研究 Makefile 和构建脚本编写
- ✅ 了解 RK3399 平台的启动流程

不适合：
- ❌ 直接编译运行（缺少源码）
- ❌ 驱动开发和调试（已排除驱动源码）
- ❌ 内核开发（已排除内核源码）

## 🔗 相关资源

- [OrangePi 官方网站](http://www.orangepi.org/)
- [OrangePi RK3399 GitHub](https://github.com/orangepi-xunlong)
- [Rockchip 官方文档](http://opensource.rock-chips.com/wiki_Main_Page)

## 📄 许可证

- Kernel: GPL-2.0
- U-Boot: GPL-2.0
- 构建脚本: 遵循原项目许可

---

**提示**: 这是一个学习用精简版本。如需实际编译，请获取完整源码。
