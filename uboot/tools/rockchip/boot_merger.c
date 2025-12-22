/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Rockchip Loader 镜像打包工具 - boot_merger
 *
 * 功能说明:
 *   - 将 DDR 初始化代码(FlashData)和 Miniloader(FlashBoot)合并成 loader.bin
 *   - 支持从 INI 配置文件读取组件路径
 *   - 添加 Rockchip 专用镜像头部(magic, chip type, version, CRC 等)
 *   - 支持 RC4 加密和镜像解包
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include "boot_merger.h"
#include <time.h>
#include <sys/stat.h>
#include <version.h>

/* #define USE_P_RC4 */  /* RC4 加密开关(已禁用,通过命令行参数控制) */

/* 调试模式标志: DEBUG 宏定义时启用详细日志输出 */
bool gDebug =
#ifdef DEBUG
        true;
#else
        false;
#endif /* DEBUG */

#define ENTRY_ALIGN (2048)  /* Entry 数据对齐单位: 2048 字节(2KB) */

/* ===== 全局变量定义 ===== */
options gOpts;                          /* 解析后的配置选项(芯片类型、版本、组件路径等) */
char gLegacyPath[MAX_LINE_LEN] = { 0 }; /* 旧路径字符串,用于路径替换 */
char gNewPath[MAX_LINE_LEN] = { 0 };    /* 新路径字符串,用于路径替换 */
static char *gPrePath;                  /* 路径前缀,用于拼接相对路径 */
char gSubfix[MAX_LINE_LEN] = OUT_SUBFIX;/* 输出文件名后缀 */
char gEat[MAX_LINE_LEN];                /* 用于读取并丢弃 INI 文件中的无用字符 */
char *gConfigPath;                      /* INI 配置文件路径 */
uint8_t *gBuf;                          /* 全局缓冲区,用于文件读写操作 */
bool enableRC4 = false;                 /* RC4 加密使能标志(默认禁用) */

static uint32_t g_merge_max_size = MAX_MERGE_SIZE;  /* 合并镜像最大尺寸(默认值) */

/*
 * CRC32 查表法计算表
 * 标准 CRC-32 算法查找表,用于快速计算文件校验和
 * 多项式: 0x04C11DB7 (以太网标准)
 */
uint32_t gTable_Crc32[256] = {
	0x00000000, 0x04c10db7, 0x09821b6e, 0x0d4316d9, 0x130436dc, 0x17c53b6b,
	0x1a862db2, 0x1e472005, 0x26086db8, 0x22c9600f, 0x2f8a76d6, 0x2b4b7b61,
	0x350c5b64, 0x31cd56d3, 0x3c8e400a, 0x384f4dbd, 0x4c10db70, 0x48d1d6c7,
	0x4592c01e, 0x4153cda9, 0x5f14edac, 0x5bd5e01b, 0x5696f6c2, 0x5257fb75,
	0x6a18b6c8, 0x6ed9bb7f, 0x639aada6, 0x675ba011, 0x791c8014, 0x7ddd8da3,
	0x709e9b7a, 0x745f96cd, 0x9821b6e0, 0x9ce0bb57, 0x91a3ad8e, 0x9562a039,
	0x8b25803c, 0x8fe48d8b, 0x82a79b52, 0x866696e5, 0xbe29db58, 0xbae8d6ef,
	0xb7abc036, 0xb36acd81, 0xad2ded84, 0xa9ece033, 0xa4aff6ea, 0xa06efb5d,
	0xd4316d90, 0xd0f06027, 0xddb376fe, 0xd9727b49, 0xc7355b4c, 0xc3f456fb,
	0xceb74022, 0xca764d95, 0xf2390028, 0xf6f80d9f, 0xfbbb1b46, 0xff7a16f1,
	0xe13d36f4, 0xe5fc3b43, 0xe8bf2d9a, 0xec7e202d, 0x34826077, 0x30436dc0,
	0x3d007b19, 0x39c176ae, 0x278656ab, 0x23475b1c, 0x2e044dc5, 0x2ac54072,
	0x128a0dcf, 0x164b0078, 0x1b0816a1, 0x1fc91b16, 0x018e3b13, 0x054f36a4,
	0x080c207d, 0x0ccd2dca, 0x7892bb07, 0x7c53b6b0, 0x7110a069, 0x75d1adde,
	0x6b968ddb, 0x6f57806c, 0x621496b5, 0x66d59b02, 0x5e9ad6bf, 0x5a5bdb08,
	0x5718cdd1, 0x53d9c066, 0x4d9ee063, 0x495fedd4, 0x441cfb0d, 0x40ddf6ba,
	0xaca3d697, 0xa862db20, 0xa521cdf9, 0xa1e0c04e, 0xbfa7e04b, 0xbb66edfc,
	0xb625fb25, 0xb2e4f692, 0x8aabbb2f, 0x8e6ab698, 0x8329a041, 0x87e8adf6,
	0x99af8df3, 0x9d6e8044, 0x902d969d, 0x94ec9b2a, 0xe0b30de7, 0xe4720050,
	0xe9311689, 0xedf01b3e, 0xf3b73b3b, 0xf776368c, 0xfa352055, 0xfef42de2,
	0xc6bb605f, 0xc27a6de8, 0xcf397b31, 0xcbf87686, 0xd5bf5683, 0xd17e5b34,
	0xdc3d4ded, 0xd8fc405a, 0x6904c0ee, 0x6dc5cd59, 0x6086db80, 0x6447d637,
	0x7a00f632, 0x7ec1fb85, 0x7382ed5c, 0x7743e0eb, 0x4f0cad56, 0x4bcda0e1,
	0x468eb638, 0x424fbb8f, 0x5c089b8a, 0x58c9963d, 0x558a80e4, 0x514b8d53,
	0x25141b9e, 0x21d51629, 0x2c9600f0, 0x28570d47, 0x36102d42, 0x32d120f5,
	0x3f92362c, 0x3b533b9b, 0x031c7626, 0x07dd7b91, 0x0a9e6d48, 0x0e5f60ff,
	0x101840fa, 0x14d94d4d, 0x199a5b94, 0x1d5b5623, 0xf125760e, 0xf5e47bb9,
	0xf8a76d60, 0xfc6660d7, 0xe22140d2, 0xe6e04d65, 0xeba35bbc, 0xef62560b,
	0xd72d1bb6, 0xd3ec1601, 0xdeaf00d8, 0xda6e0d6f, 0xc4292d6a, 0xc0e820dd,
	0xcdab3604, 0xc96a3bb3, 0xbd35ad7e, 0xb9f4a0c9, 0xb4b7b610, 0xb076bba7,
	0xae319ba2, 0xaaf09615, 0xa7b380cc, 0xa3728d7b, 0x9b3dc0c6, 0x9ffccd71,
	0x92bfdba8, 0x967ed61f, 0x8839f61a, 0x8cf8fbad, 0x81bbed74, 0x857ae0c3,
	0x5d86a099, 0x5947ad2e, 0x5404bbf7, 0x50c5b640, 0x4e829645, 0x4a439bf2,
	0x47008d2b, 0x43c1809c, 0x7b8ecd21, 0x7f4fc096, 0x720cd64f, 0x76cddbf8,
	0x688afbfd, 0x6c4bf64a, 0x6108e093, 0x65c9ed24, 0x11967be9, 0x1557765e,
	0x18146087, 0x1cd56d30, 0x02924d35, 0x06534082, 0x0b10565b, 0x0fd15bec,
	0x379e1651, 0x335f1be6, 0x3e1c0d3f, 0x3add0088, 0x249a208d, 0x205b2d3a,
	0x2d183be3, 0x29d93654, 0xc5a71679, 0xc1661bce, 0xcc250d17, 0xc8e400a0,
	0xd6a320a5, 0xd2622d12, 0xdf213bcb, 0xdbe0367c, 0xe3af7bc1, 0xe76e7676,
	0xea2d60af, 0xeeec6d18, 0xf0ab4d1d, 0xf46a40aa, 0xf9295673, 0xfde85bc4,
	0x89b7cd09, 0x8d76c0be, 0x8035d667, 0x84f4dbd0, 0x9ab3fbd5, 0x9e72f662,
	0x9331e0bb, 0x97f0ed0c, 0xafbfa0b1, 0xab7ead06, 0xa63dbbdf, 0xa2fcb668,
	0xbcbb966d, 0xb87a9bda, 0xb5398d03, 0xb1f880b4,
};

/**
 * CRC_32 - 计算数据的 CRC32 校验和
 * @pData: 待计算的数据缓冲区
 * @ulSize: 数据大小(字节)
 *
 * 使用查表法快速计算 CRC32,用于镜像完整性校验
 * 返回: 32 位 CRC 校验值
 */
uint32_t CRC_32(uint8_t *pData, uint32_t ulSize)
{
	uint32_t i;
	uint32_t nAccum = 0;  /* CRC 累加器,初始值为 0 */
	for (i = 0; i < ulSize; i++) {
		/* 查表法: 将当前字节与累加器高 8 位异或后查表,再与累加器左移 8 位异或 */
		nAccum = (nAccum << 8) ^ gTable_Crc32[(nAccum >> 24) ^ (*pData++)];
	}
	return nAccum;
}

/**
 * P_RC4 - RC4 流加密算法(Rockchip 专用变种)
 * @buf: 待加密/解密的数据缓冲区(原地加密)
 * @len: 数据长度(字节)
 *
 * 功能: 对 Loader 组件进行 RC4 加密/解密(对称算法)
 * 注意: 使用 Rockchip 固定密钥,非标准 RC4 实现
 */
void P_RC4(uint8_t *buf, uint32_t len)
{
	uint8_t S[256], K[256], temp;  /* S: 置换盒, K: 密钥调度数组 */
	uint32_t i, j, t, x;
	/* Rockchip 固定密钥(16 字节): 用于初始化密钥调度算法 */
	uint8_t key[16] = { 124, 78, 3, 4, 85, 5, 9, 7, 45, 44, 123, 56, 23, 13, 23,
	                    17
	                  };

	/* === 阶段 1: 初始化 S 盒和 K 数组 === */
	j = 0;
	for (i = 0; i < 256; i++) {
		S[i] = (uint8_t) i;         /* S 盒初始化为 0~255 */
		j &= 0x0f;                  /* j 限制在 0~15 范围内 */
		K[i] = key[j];              /* 循环填充密钥 */
		j++;
	}

	/* === 阶段 2: 密钥调度算法(KSA) - 打乱 S 盒 === */
	j = 0;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + K[i]) % 256;  /* 伪随机索引计算 */
		/* 交换 S[i] 和 S[j] */
		temp = S[i];
		S[i] = S[j];
		S[j] = temp;
	}

	/* === 阶段 3: 伪随机生成算法(PRGA) - 加密数据 === */
	i = j = 0;
	for (x = 0; x < len; x++) {
		i = (i + 1) % 256;              /* 递增 i */
		j = (j + S[i]) % 256;           /* 更新 j */
		/* 交换 S[i] 和 S[j] */
		temp = S[i];
		S[i] = S[j];
		S[j] = temp;
		/* 生成密钥流字节 t,与明文异或完成加密 */
		t = (S[i] + (S[j] % 256)) % 256;
		buf[x] = buf[x] ^ S[t];
	}
}

/**
 * fixPath - 修正文件路径格式
 * @path: 待修正的路径字符串(会被原地修改)
 *
 * 功能:
 *   1. 将 Windows 路径分隔符 '\' 转换为 Unix 格式 '/'
 *   2. 移除路径末尾的换行符 \r 和 \n
 *   3. 支持路径替换(gLegacyPath -> gNewPath)
 *   4. 支持添加路径前缀(gPrePath)
 */
static inline void fixPath(char *path)
{
	int i, len = strlen(path);
	char tmp[MAX_LINE_LEN];
	char *start, *end;

	/* === 步骤 1: 规范化路径格式 === */
	for (i = 0; i < len; i++) {
		if (path[i] == '\\')                    /* 转换 Windows 路径分隔符 */
			path[i] = '/';
		else if (path[i] == '\r' || path[i] == '\n')  /* 移除换行符 */
			path[i] = '\0';
	}

	/* === 步骤 2: 路径替换(如果配置了 gLegacyPath 和 gNewPath) === */
	if (strlen(gLegacyPath) && strlen(gNewPath)) {
		start = strstr(path, gLegacyPath);  /* 查找旧路径 */
		if (start) {
			/* 替换路径中的旧部分为新部分 */
			end = start + strlen(gLegacyPath);
			strcpy(tmp, end);           /* 备份剩余部分 */
			*start = '\0';              /* 截断原路径 */
			strcat(path, gNewPath);     /* 拼接新路径 */
			strcat(path, tmp);          /* 拼接剩余部分 */
		} else {
			/* 旧路径不存在,直接在开头添加新路径 */
			strcpy(tmp, path);
			strcpy(path, gNewPath);
			strcat(path, tmp);
		}
	}
	/* === 步骤 3: 添加路径前缀(如果配置了 gPrePath) === */
	else if ((ulong)path != (ulong)gOpts.outPath && /* 忽略输出路径 */
		    gPrePath && strncmp(path, gPrePath, strlen(gPrePath))) {
		strcpy(tmp, path);
		strcpy(path, gPrePath);  /* 添加前缀 */
		strcat(path, tmp);
	}
}

/**
 * parseChip - 解析 INI 文件中的 [CHIP_NAME] 段
 * @file: 打开的 INI 文件指针
 *
 * 读取芯片名称(如 RK3399, RK3328 等)并保存到 gOpts.chip
 * 返回: true=成功, false=失败
 */
static bool parseChip(FILE *file)
{
	if (SCANF_EAT(file) != 0) {  /* 跳过空白字符和注释 */
		return false;
	}
	/* 读取 NAME=RK3399 格式 */
	if (fscanf(file, OPT_NAME "=%s", gOpts.chip) != 1) {
		return false;
	}
	LOGD("chip:%s\n", gOpts.chip);
	return true;
}

/**
 * parseVersion - 解析 INI 文件中的 [VERSION] 段
 * @file: 打开的 INI 文件指针
 *
 * 读取版本号(MAJOR 和 MINOR)并保存到 gOpts
 * 返回: true=成功, false=失败
 */
static bool parseVersion(FILE *file)
{
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	/* 读取 MAJOR=2 */
	if (fscanf(file, OPT_MAJOR "=%d", &gOpts.major) != 1)
		return false;
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	/* 读取 MINOR=50 */
	if (fscanf(file, OPT_MINOR "=%d", &gOpts.minor) != 1)
		return false;
	LOGD("major:%d, minor:%d\n", gOpts.major, gOpts.minor);
	return true;
}

/**
 * parse471 - 解析 INI 文件中的 [CODE471_OPTION] 段
 * @file: 打开的 INI 文件指针
 *
 * CODE471 是 DDR 初始化代码(ddr.bin),用于在 DRAM 初始化前由 BootROM 加载到 SRAM 执行
 * 支持多个文件路径(Path1, Path2, ...),以及延迟参数(Sleep)
 * 返回: true=成功, false=失败
 */
static bool parse471(FILE *file)
{
	int i, index, pos;
	char buf[MAX_LINE_LEN];

	if (SCANF_EAT(file) != 0) {
		return false;
	}
	/* 读取 NUM=1 (471 文件数量) */
	if (fscanf(file, OPT_NUM "=%d", &gOpts.code471Num) != 1)
		return false;
	LOGD("num:%d\n", gOpts.code471Num);
	if (!gOpts.code471Num)  /* 数量为 0 是合法的,直接返回成功 */
		return true;
	if (gOpts.code471Num < 0)
		return false;

	/* 分配路径数组 */
	gOpts.code471Path = (line_t *)malloc(sizeof(line_t) * gOpts.code471Num);

	/* 读取所有路径: Path1=bin/rk33/rk3399_ddr_800MHz_v1.25.bin */
	for (i = 0; i < gOpts.code471Num; i++) {
		if (SCANF_EAT(file) != 0) {
			return false;
		}
		if (fscanf(file, OPT_PATH "%d=%[^\r^\n]", &index, buf) != 2)
			return false;
		index--;  /* INI 中索引从 1 开始,数组索引从 0 开始 */
		fixPath(buf);  /* 修正路径格式 */
		strcpy((char *)gOpts.code471Path[index], buf);
		LOGD("path%i:%s\n", index, gOpts.code471Path[index]);
	}

	/* 读取可选的 Sleep 参数(单位: ms) */
	pos = ftell(file);
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	if (fscanf(file, OPT_SLEEP "=%d", &gOpts.code471Sleep) != 1)
		fseek(file, pos, SEEK_SET);  /* Sleep 参数不存在,回退文件指针 */
	LOGD("sleep:%d\n", gOpts.code471Sleep);
	return true;
}

/**
 * parse472 - 解析 INI 文件中的 [CODE472_OPTION] 段
 * @file: 打开的 INI 文件指针
 *
 * CODE472 是 USB 插件代码(usbplug.bin),用于 Maskrom 模式下通过 USB 下载镜像
 * 支持多个文件路径(Path1, Path2, ...),以及延迟参数(Sleep)
 * 返回: true=成功, false=失败
 */
static bool parse472(FILE *file)
{
	int i, index, pos;
	char buf[MAX_LINE_LEN];

	if (SCANF_EAT(file) != 0) {
		return false;
	}
	/* 读取 NUM=1 (472 文件数量) */
	if (fscanf(file, OPT_NUM "=%d", &gOpts.code472Num) != 1)
		return false;
	LOGD("num:%d\n", gOpts.code472Num);
	if (!gOpts.code472Num)  /* 数量为 0 是合法的 */
		return true;
	if (gOpts.code472Num < 0)
		return false;

	/* 分配路径数组 */
	gOpts.code472Path = (line_t *)malloc(sizeof(line_t) * gOpts.code472Num);

	/* 读取所有路径: Path1=bin/rk33/rk3399_usbplug_v1.27.bin */
	for (i = 0; i < gOpts.code472Num; i++) {
		if (SCANF_EAT(file) != 0) {
			return false;
		}
		if (fscanf(file, OPT_PATH "%d=%[^\r^\n]", &index, buf) != 2)
			return false;
		fixPath(buf);
		index--;
		strcpy((char *)gOpts.code472Path[index], buf);
		LOGD("path%i:%s\n", index, gOpts.code472Path[index]);
	}

	/* 读取可选的 Sleep 参数 */
	pos = ftell(file);
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	if (fscanf(file, OPT_SLEEP "=%d", &gOpts.code472Sleep) != 1)
		fseek(file, pos, SEEK_SET);
	LOGD("sleep:%d\n", gOpts.code472Sleep);
	return true;
}

/**
 * parseLoader - 解析 INI 文件中的 [LOADER_OPTION] 段
 * @file: 打开的 INI 文件指针
 *
 * LOADER 包含实际的 loader 组件(FlashData 和 FlashBoot):
 *   - FlashData: DDR 初始化代码,写入 Flash 的 Data 分区
 *   - FlashBoot: Miniloader,写入 Flash 的 Boot 分区
 * 返回: true=成功, false=失败
 */
static bool parseLoader(FILE *file)
{
	int i, j, index, pos;
	char buf[MAX_LINE_LEN];
	char buf2[MAX_LINE_LEN];

	if (SCANF_EAT(file) != 0) {
		return false;
	}
	pos = ftell(file);
	/* 尝试读取 NUM=2 或 LoaderNum=2 (兼容旧格式) */
	if (fscanf(file, OPT_NUM "=%d", &gOpts.loaderNum) != 1) {
		fseek(file, pos, SEEK_SET);
		if (fscanf(file, OPT_LOADER_NUM "=%d", &gOpts.loaderNum) != 1) {
			return false;
		}
	}
	LOGD("num:%d\n", gOpts.loaderNum);
	if (!gOpts.loaderNum)  /* Loader 数量必须 > 0 */
		return false;
	if (gOpts.loaderNum < 0)
		return false;

	/* 分配 loader 名称-路径映射数组 */
	gOpts.loader = (name_entry *)malloc(sizeof(name_entry) * gOpts.loaderNum);

	/* === 阶段 1: 读取 Loader 名称 === */
	/* 示例: LOADER1=FlashData, LOADER2=FlashBoot */
	for (i = 0; i < gOpts.loaderNum; i++) {
		if (SCANF_EAT(file) != 0) {
			return false;
		}
		if (fscanf(file, OPT_LOADER_NAME "%d=%s", &index, buf) != 2)
			return false;
		index--;  /* 转换为数组索引 */
		strcpy(gOpts.loader[index].name, buf);
		LOGD("name%d:%s\n", index, gOpts.loader[index].name);
	}

	/* === 阶段 2: 读取每个 Loader 的文件路径 === */
	/* 示例: FlashData=bin/rk33/rk3399_ddr_800MHz_v1.25.bin */
	for (i = 0; i < gOpts.loaderNum; i++) {
		if (SCANF_EAT(file) != 0) {
			return false;
		}
		/* 读取 name=path 格式 */
		if (fscanf(file, "%[^=]=%[^\r^\n]", buf, buf2) != 2)
			return false;
		/* 查找匹配的 name,将 path 保存到对应位置 */
		for (j = 0; j < gOpts.loaderNum; j++) {
			if (!strcmp(gOpts.loader[j].name, buf)) {
				fixPath(buf2);
				strcpy(gOpts.loader[j].path, buf2);
				LOGD("%s=%s\n", gOpts.loader[j].name, gOpts.loader[j].path);
				break;
			}
		}
		if (j >= gOpts.loaderNum) {  /* 未找到匹配的 name */
			return false;
		}
	}
	return true;
}

/**
 * parseOut - 解析 INI 文件中的 [OUTPUT] 段
 * @file: 打开的 INI 文件指针
 *
 * 读取输出文件名,如: PATH=rk3399_loader_v1.25.126.bin
 * 返回: true=成功, false=失败
 */
static bool parseOut(FILE *file)
{
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	if (fscanf(file, OPT_OUT_PATH "=%[^\r^\n]", gOpts.outPath) != 1)
		return false;
	/* fixPath(gOpts.outPath); */  /* 输出路径不需要修正 */
	printf("out:%s\n", gOpts.outPath);
	return true;
}

/**
 * printOpts - 将配置选项打印到文件
 * @out: 输出文件指针(可以是 stdout 或配置文件)
 *
 * 功能: 将已解析的配置选项按 INI 格式输出
 * 用途:
 *   1. 调试时打印配置到终端
 *   2. 生成默认配置文件
 */
void printOpts(FILE *out)
{
	uint32_t i;
	/* 打印 [CHIP_NAME] 段 */
	fprintf(out, SEC_CHIP "\n" OPT_NAME "=%s\n", gOpts.chip);

	/* 打印 [VERSION] 段 */
	fprintf(out, SEC_VERSION "\n" OPT_MAJOR "=%d\n" OPT_MINOR "=%d\n",
	        gOpts.major, gOpts.minor);

	/* 打印 [CODE471_OPTION] 段 (DDR 初始化代码) */
	fprintf(out, SEC_471 "\n" OPT_NUM "=%d\n", gOpts.code471Num);
	for (i = 0; i < gOpts.code471Num; i++) {
		fprintf(out, OPT_PATH "%d=%s\n", i + 1, gOpts.code471Path[i]);
	}
	if (gOpts.code471Sleep > 0)  /* 可选的 Sleep 参数 */
		fprintf(out, OPT_SLEEP "=%d\n", gOpts.code471Sleep);

	/* 打印 [CODE472_OPTION] 段 (USB 插件代码) */
	fprintf(out, SEC_472 "\n" OPT_NUM "=%d\n", gOpts.code472Num);
	for (i = 0; i < gOpts.code472Num; i++) {
		fprintf(out, OPT_PATH "%d=%s\n", i + 1, gOpts.code472Path[i]);
	}
	if (gOpts.code472Sleep > 0)
		fprintf(out, OPT_SLEEP "=%d\n", gOpts.code472Sleep);

	/* 打印 [LOADER_OPTION] 段 (FlashData + FlashBoot) */
	fprintf(out, SEC_LOADER "\n" OPT_NUM "=%d\n", gOpts.loaderNum);
	for (i = 0; i < gOpts.loaderNum; i++) {
		fprintf(out, OPT_LOADER_NAME "%d=%s\n", i + 1, gOpts.loader[i].name);
	}
	for (i = 0; i < gOpts.loaderNum; i++) {
		fprintf(out, "%s=%s\n", gOpts.loader[i].name, gOpts.loader[i].path);
	}

	/* 打印 [OUTPUT] 段 */
	fprintf(out, SEC_OUT "\n" OPT_OUT_PATH "=%s\n", gOpts.outPath);
}

/**
 * parseOpts_from_file - 从 INI 配置文件解析所有配置项
 *
 * 功能: 读取 INI 文件并依次解析各个段:
 *   [CHIP_NAME]      - 芯片型号
 *   [VERSION]        - 版本号
 *   [CODE471_OPTION] - DDR 初始化代码
 *   [CODE472_OPTION] - USB 插件代码
 *   [LOADER_OPTION]  - Loader 组件(FlashData/FlashBoot)
 *   [OUTPUT]         - 输出文件名
 *
 * 返回: true=解析成功, false=失败(缺失段或格式错误)
 *
 * 特殊处理:
 *   - 如果配置文件不存在且使用默认路径,会自动创建默认配置文件
 *   - CODE471 和 CODE472 段允许为空(Num=0)
 */
static bool parseOpts_from_file(void)
{
	bool ret = false;
	/* 各段解析状态标志 */
	bool chipOk = false;
	bool versionOk = false;
	bool code471Ok = true;      /* 默认为 true(允许不存在) */
	bool code472Ok = true;      /* 默认为 true(允许不存在) */
	bool loaderOk = false;
	bool outOk = false;
	char buf[MAX_LINE_LEN];

	/* 使用默认配置文件路径或命令行指定路径 */
	char *configPath = (gConfigPath == NULL) ? DEF_CONFIG_FILE : gConfigPath;
	FILE *file;
	file = fopen(configPath, "r");
	if (!file) {
		fprintf(stderr, "config(%s) not found!\n", configPath);
		/* 如果是默认配置文件不存在,尝试创建一个默认配置 */
		if (configPath == (char *)DEF_CONFIG_FILE) {
			file = fopen(DEF_CONFIG_FILE, "w");
			if (file) {
				fprintf(stderr, "create defconfig\n");
				printOpts(file);  /* 写入默认配置 */
			}
		}
		goto end;
	}

	LOGD("start parse\n");

	/* 跳过文件开头的空白字符 */
	if (SCANF_EAT(file) != 0) {
		goto end;
	}

	/* === 主解析循环: 逐段读取 INI 文件 === */
	while (fscanf(file, "%s", buf) == 1) {
		if (!strcmp(buf, SEC_CHIP)) {  /* [CHIP_NAME] */
			chipOk = parseChip(file);
			if (!chipOk) {
				LOGE("parseChip failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_VERSION)) {  /* [VERSION] */
			versionOk = parseVersion(file);
			if (!versionOk) {
				LOGE("parseVersion failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_471)) {  /* [CODE471_OPTION] */
			code471Ok = parse471(file);
			if (!code471Ok) {
				LOGE("parse471 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_472)) {  /* [CODE472_OPTION] */
			code472Ok = parse472(file);
			if (!code472Ok) {
				LOGE("parse472 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_LOADER)) {  /* [LOADER_OPTION] */
			loaderOk = parseLoader(file);
			if (!loaderOk) {
				LOGE("parseLoader failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_OUT)) {  /* [OUTPUT] */
			outOk = parseOut(file);
			if (!outOk) {
				LOGE("parseOut failed!\n");
				goto end;
			}
		} else if (buf[0] == '#') {  /* 忽略注释行 */
			continue;
		} else {
			LOGE("unknown sec: %s!\n", buf);  /* 未知段名 */
			goto end;
		}
		/* 跳过段之间的空白 */
		if (SCANF_EAT(file) != 0) {
			goto end;
		}
	}

	/* 检查必需段是否全部解析成功 */
	if (chipOk && versionOk && code471Ok && code472Ok && loaderOk && outOk)
		ret = true;
end:
	if (file)
		fclose(file);
	return ret;
}

/**
 * parseOpts_from_cmdline - 从命令行参数解析配置
 * @argc: 参数数量
 * @argv: 参数数组
 *
 * 功能: 支持不使用 INI 文件,直接通过命令行参数指定所有选项
 *
 * 必需参数(tag 位掩码验证):
 *   --471   <path>  - CODE471(DDR 初始化) 文件路径       (tag bit 0)
 *   --472   <path>  - CODE472(USB 插件) 文件路径         (tag bit 1)
 *   --loader0 <path>  - FlashData(Loader0) 文件路径     (tag bit 2)
 *   --loader1 <path>  - FlashBoot(Loader1) 文件路径     (tag bit 3)
 *
 * 可选参数:
 *   --out   <path>  - 输出文件名(tag bit 4, 可自动生成)
 *   --chip  <name>  - 芯片型号(tag bit 5, 可自动生成)
 *
 * 版本号自动解析:
 *   从文件名提取版本号: rk3399_ddr_vX.Y.bin -> major/minor
 *
 * 返回: true=必需参数齐全(tag & 0x0f == 0x0f), false=缺失参数
 */
static bool parseOpts_from_cmdline(int argc, char **argv)
{
	int i;
	int tag = 0;  /* 位掩码: 记录哪些必需参数已设置 */
	int v0, v1, v2, v3;  /* 版本号: v0.v1(Loader0), v2.v3(Loader1) */

	/* 从第 3 个参数开始(跳过程序名和主操作参数) */
	for (i = 2; i < argc; i++) {
		if (!strcmp(OPT_471, argv[i])) {  /* --471 */
			i++;
			snprintf(gOpts.code471Path[0], sizeof(gOpts.code471Path[0]), "%s",
			         argv[i]);
			tag |= 1;  /* 设置 bit 0 */
		} else if (!strcmp(OPT_472, argv[i])) {  /* --472 */
			i++;
			snprintf(gOpts.code472Path[0], sizeof(gOpts.code472Path[0]), "%s",
			         argv[i]);
			tag |= 2;  /* 设置 bit 1 */
		} else if (!strcmp(OPT_DATA, argv[i])) {  /* --loader0 (FlashData) */
			i++;
			snprintf(gOpts.loader[0].path, sizeof(gOpts.loader[0].path), "%s",
			         argv[i]);
			tag |= 4;  /* 设置 bit 2 */
		} else if (!strcmp(OPT_BOOT, argv[i])) {  /* --loader1 (FlashBoot) */
			i++;
			snprintf(gOpts.loader[1].path, sizeof(gOpts.loader[1].path), "%s",
			         argv[i]);
			tag |= 8;  /* 设置 bit 3 */
		} else if (!strcmp(OPT_OUT, argv[i])) {  /* --out */
			i++;
			snprintf(gOpts.outPath, sizeof(gOpts.outPath), "%s", argv[i]);
			tag |= 0x10;  /* 设置 bit 4 */
		} else if (!strcmp(OPT_CHIP, argv[i])) {  /* --chip */
			i++;
			snprintf(gOpts.chip, sizeof(gOpts.chip), "%s", argv[i]);
			tag |= 0x20;  /* 设置 bit 5 */
		} else if (!strcmp(OPT_VERSION, argv[i])) {
			/* --version: 预留参数,暂无处理逻辑 */
		}
	}

	/* === 自动生成版本号和输出文件名 === */
	/* 从文件名解析版本号: rk3399_ddr_800MHz_v1.25.bin -> v0=1, v1=25 */
	sscanf(gOpts.loader[0].path, "%*[^v]v%d.%d.bin", &v0, &v1);
	/* 从文件名解析版本号: rk3399_miniloader_v1.26.bin -> v2=1, v3=26 */
	sscanf(gOpts.loader[1].path, "%*[^v]v%d.%d.bin", &v2, &v3);
	gOpts.major = v2;  /* 主版本号使用 miniloader 的版本 */
	gOpts.minor = v3;  /* 次版本号使用 miniloader 的版本 */

	/* 自动生成输出文件名: RK3399_loader_v1.26.125.bin
	 * 格式: <chip>_loader_v<miniloader_major>.<miniloader_minor>.<ddr_major><ddr_minor>.bin
	 */
	snprintf(gOpts.outPath, sizeof(gOpts.outPath),
	         "%s_loader_v%d.%02d.%d%02d.bin", gOpts.chip, v0, v1, v2, v3);

	/* 检查必需的 4 个参数是否全部提供(bit 0~3) */
	return ((tag & 0x0f) == 0x0f) ? true : false;
}

/**
 * initOpts - 初始化配置选项结构
 * @argc: 命令行参数数量
 * @argv: 命令行参数数组
 *
 * 功能: 根据参数数量选择解析方式并设置默认值
 *
 * 解析策略:
 *   1. argc > 10: 使用命令行参数解析模式(parseOpts_from_cmdline)
 *   2. argc <= 10: 使用 INI 文件解析模式(parseOpts_from_file)
 *
 * 默认配置值:
 *   - 芯片型号: RK3368
 *   - 版本号: 2.50
 *   - CODE471: rk3368_ddr_600MHz_v1.00.bin (1 个文件)
 *   - CODE472: rk3368_usbplug_v2.50.bin (1 个文件)
 *   - Loader: FlashData + FlashBoot (2 个组件)
 *   - 输出文件: rk3368_loader_v2.50.bin
 *
 * 返回: true=解析成功, false=解析失败
 */
bool initOpts(int argc, char **argv)
{
	bool ret;

	/* === 设置默认配置值 === */
	gOpts.major = DEF_MAJOR;                    /* 默认主版本号: 2 */
	gOpts.minor = DEF_MINOR;                    /* 默认次版本号: 50 */
	strcpy(gOpts.chip, DEF_CHIP);               /* 默认芯片: RK3368 */

	/* CODE471 配置(DDR 初始化代码) */
	gOpts.code471Sleep = DEF_CODE471_SLEEP;     /* 默认延迟: 0ms */
	gOpts.code471Num = DEF_CODE471_NUM;         /* 默认文件数: 1 */
	gOpts.code471Path = (line_t *)malloc(sizeof(line_t) * gOpts.code471Num);
	strcpy((char *)gOpts.code471Path[0], DEF_CODE471_PATH);  /* 默认路径 */

	/* CODE472 配置(USB 插件代码) */
	gOpts.code472Sleep = DEF_CODE472_SLEEP;     /* 默认延迟: 0ms */
	gOpts.code472Num = DEF_CODE472_NUM;         /* 默认文件数: 1 */
	gOpts.code472Path = (line_t *)malloc(sizeof(line_t) * gOpts.code472Num);
	strcpy((char *)gOpts.code472Path[0], DEF_CODE472_PATH);  /* 默认路径 */

	/* Loader 配置(FlashData + FlashBoot) */
	gOpts.loaderNum = DEF_LOADER_NUM;           /* 默认 Loader 数: 2 */
	gOpts.loader = (name_entry *)malloc(sizeof(name_entry) * gOpts.loaderNum);
	strcpy(gOpts.loader[0].name, DEF_LOADER0);  /* Loader0 名称: FlashData */
	strcpy(gOpts.loader[0].path, DEF_LOADER0_PATH);  /* Loader0 路径 */
	strcpy(gOpts.loader[1].name, DEF_LOADER1);  /* Loader1 名称: FlashBoot */
	strcpy(gOpts.loader[1].path, DEF_LOADER1_PATH);  /* Loader1 路径 */

	/* 输出文件配置 */
	strcpy(gOpts.outPath, DEF_OUT_PATH);        /* 默认输出文件名 */

	/* === 根据参数数量选择解析方式 === */
	if (argc > 10)
		ret = parseOpts_from_cmdline(argc, argv);  /* 命令行模式 */
	else
		ret = parseOpts_from_file();               /* INI 文件模式 */

	return ret;
}

/************merge code****************/

/**
 * getBCD - 将十进制数转换为 BCD 码(Binary-Coded Decimal)
 * @value: 十进制数值(范围: 0~9999)
 *
 * BCD 码编码规则:
 *   每个十进制位用 4 位二进制表示(0~9)
 *   示例: 1234(DEC) -> 0x1234(BCD)
 *
 * 应用场景: Rockchip 镜像头部的版本号字段使用 BCD 编码
 *
 * 返回: BCD 码低 8 位(只保留低两位十进制数)
 */
static inline uint32_t getBCD(unsigned short value)
{
	uint8_t tmp[2] = { 0 };  /* tmp[0]: 低两位, tmp[1]: 高两位 */
	int i;
	uint32_t ret;

	if (value > 0xFFFF) {  /* 超出范围检查 */
		return 0;
	}

	/* 转换每两位十进制数为一个 BCD 字节 */
	for (i = 0; i < 2; i++) {
		/* tmp[i] = (十位 << 4) | 个位 */
		tmp[i] = (((value / 10) % 10) << 4) | (value % 10);
		value /= 100;  /* 移到下一对十进制位 */
	}

	/* 组合成 16 位 BCD 码 */
	ret = ((uint16_t)(tmp[1] << 8)) | tmp[0];

	LOGD("ret:%x\n", ret);
	return ret & 0xFF;  /* 只返回低 8 位(低两位十进制数) */
}

/**
 * str2wide - 将 ASCII 字符串转换为宽字符数组
 * @str: 源 ASCII 字符串
 * @wide: 目标宽字符数组(uint16_t)
 * @len: 要转换的字符数
 *
 * 功能: 将单字节字符扩展为双字节宽字符(仅保留低 8 位)
 * 应用场景: Entry 名称字段使用 Unicode 编码存储
 */
static inline void str2wide(const char *str, uint16_t *wide, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		wide[i] = (uint16_t) str[i];  /* ASCII 转 Unicode(低位) */
	}
	wide[len] = 0;  /* 添加宽字符终止符 */
}

/**
 * getName - 从完整路径提取文件名并转换为宽字符
 * @path: 完整文件路径(如: bin/rk33/rk3399_ddr_800MHz_v1.25.bin)
 * @dst: 输出的宽字符数组(存储文件名,不含扩展名)
 *
 * 提取规则:
 *   1. 从最后一个 '/' 后开始提取(没有 '/' 则从头开始)
 *   2. 到最后一个 '.' 前结束(没有 '.' 则到字符串末尾)
 *   3. 限制长度不超过 MAX_NAME_LEN
 *
 * 示例:
 *   输入: "bin/rk33/rk3399_ddr_800MHz_v1.25.bin"
 *   输出: "rk3399_ddr_800MHz_v1" (宽字符格式)
 */
static inline void getName(char *path, uint16_t *dst)
{
	char *end;
	char *start;
	int len;

	if (!path || !dst)
		return;

	/* 查找文件名起始位置(最后一个 '/' 之后) */
	start = strrchr(path, '/');
	if (!start)
		start = path;        /* 没有 '/',从路径开头开始 */
	else
		start++;             /* 跳过 '/' 字符 */

	/* 查找扩展名起始位置(最后一个 '.') */
	end = strrchr(path, '.');
	if (!end)
		end = path + strlen(path);  /* 没有扩展名,到字符串末尾 */

	/* 计算文件名长度(不含扩展名) */
	len = end - start;
	if (len >= MAX_NAME_LEN)  /* 长度限制 */
		len = MAX_NAME_LEN - 1;

	/* 转换为宽字符 */
	str2wide(start, dst, len);

	/* 调试输出: 打印提取的文件名 */
	if (gDebug) {
		char name[MAX_NAME_LEN];
		memset(name, 0, sizeof(name));
		memcpy(name, start, len);
		LOGD("path:%s, name:%s\n", path, name);
	}
}

/**
 * getFileSize - 获取文件大小
 * @path: 文件路径
 * @size: 输出参数,用于接收文件大小(字节)
 *
 * 返回: true=成功获取, false=文件不存在或无法访问
 */
static inline bool getFileSize(const char *path, uint32_t *size)
{
	struct stat st;
	if (stat(path, &st) < 0)  /* 调用 stat 系统调用获取文件属性 */
		return false;
	*size = st.st_size;       /* 从 stat 结构提取文件大小 */
	LOGD("path:%s, size:%d\n", path, *size);
	return true;
}

/**
 * getTime - 获取当前系统时间并转换为 Rockchip 时间格式
 *
 * 功能: 将 Unix 时间戳转换为 rk_time 结构
 * 应用场景: 镜像头部的 releaseTime 字段记录打包时间
 *
 * 返回: rk_time 结构(包含年/月/日/时/分/秒)
 */
static inline rk_time getTime(void)
{
	rk_time rkTime;

	struct tm *tm;
	time_t tt = time(NULL);   /* 获取当前 Unix 时间戳 */
	tm = localtime(&tt);      /* 转换为本地时间 */

	/* 填充 Rockchip 时间结构 */
	rkTime.year = tm->tm_year + 1900;  /* tm_year 是从 1900 年起的年数 */
	rkTime.month = tm->tm_mon + 1;     /* tm_mon 范围 0~11,需要 +1 */
	rkTime.day = tm->tm_mday;
	rkTime.hour = tm->tm_hour;
	rkTime.minute = tm->tm_min;
	rkTime.second = tm->tm_sec;

	LOGD("%d-%d-%d %02d:%02d:%02d\n", rkTime.year, rkTime.month, rkTime.day,
	     rkTime.hour, rkTime.minute, rkTime.second);
	return rkTime;
}

/**
 * writeFile - 读取文件内容并写入到输出镜像(支持 RC4 加密和对齐)
 * @outFile: 输出文件指针
 * @path: 待写入的源文件路径
 * @fix: 是否使用固定分块大小(true=512字节分块加密, false=整体加密)
 *
 * 功能:
 *   1. 读取源文件到全局缓冲区 gBuf
 *   2. 根据 fix 参数选择加密方式:
 *      - fix=true: 按 SMALL_PACKET(512字节) 分块 RC4 加密(用于 Loader)
 *      - fix=false: 整体 RC4 加密(用于 CODE471/CODE472)
 *   3. 数据对齐到 ENTRY_ALIGN(2048字节) 边界
 *   4. 写入输出文件
 *
 * 对齐规则:
 *   - 先补齐到 SMALL_PACKET(512字节) 倍数
 *   - 再补齐到 ENTRY_ALIGN(2048字节) 倍数
 *
 * 返回: true=成功, false=失败(文件读取或写入错误)
 */
static bool writeFile(FILE *outFile, const char *path, bool fix)
{
	bool ret = false;
	uint32_t size = 0, fixSize = 0;
	uint8_t *buf;

	FILE *inFile = fopen(path, "rb");
	if (!inFile)
		goto end;

	/* 获取源文件大小 */
	if (!getFileSize(path, &size))
		goto end;

	/* === 根据 fix 模式计算对齐后的大小并初始化缓冲区 === */
	if (fix) {
		/* 固定模式: 先补齐到 512 字节倍数,再补齐到 2048 字节倍数 */
		fixSize = ((size - 1) / SMALL_PACKET + 1) * SMALL_PACKET;  /* 向上取整到 512 倍数 */
		uint32_t tmp = fixSize % ENTRY_ALIGN;                      /* 计算剩余字节 */
		tmp = tmp ? (ENTRY_ALIGN - tmp) : 0;                       /* 需要补齐的字节数 */
		fixSize += tmp;
		memset(gBuf, 0, fixSize);  /* 清零缓冲区(补齐部分填充 0) */
	} else {
		/* 普通模式: 直接补齐到 2048 字节倍数 */
		memset(gBuf, 0, size + ENTRY_ALIGN);
	}

	/* 读取文件内容到缓冲区 */
	if (!fread(gBuf, size, 1, inFile))
		goto end;

	/* === RC4 加密(如果启用) === */
	if (fix) {
		/* 固定模式: 分块加密(每 512 字节一块) */
		buf = gBuf;
		size = fixSize;
		while (1) {
			/* 加密当前块(最后一块可能小于 512 字节) */
			P_RC4(buf, fixSize < SMALL_PACKET ? fixSize : SMALL_PACKET);
			buf += SMALL_PACKET;
			if (fixSize <= SMALL_PACKET)
				break;
			fixSize -= SMALL_PACKET;
		}
	} else {
		/* 普通模式: 整体加密(补齐到 2048 字节倍数) */
		uint32_t tmp = size % ENTRY_ALIGN;
		tmp = tmp ? (ENTRY_ALIGN - tmp) : 0;
		size += tmp;
		P_RC4(gBuf, size);  /* 加密整个数据块 */
	}

	/* 写入加密后的数据到输出文件 */
	if (!fwrite(gBuf, size, 1, outFile))
		goto end;

	ret = true;
end:
	if (inFile)
		fclose(inFile);
	if (!ret)
		LOGE("write entry(%s) failed\n", path);
	return ret;
}

/**
 * saveEntry - 保存 Entry 元数据到输出文件
 * @outFile: 输出文件指针
 * @path: Entry 对应的源文件路径
 * @type: Entry 类型(ENTRY_471, ENTRY_472, ENTRY_LOADER)
 * @delay: 延迟时间(ms)
 * @offset: 数据偏移量(输入/输出参数,会更新为下一个 Entry 的偏移)
 * @fixName: 自定义 Entry 名称(NULL 则从路径提取)
 * @fix: 是否使用固定分块大小(影响 dataSize 计算)
 *
 * 功能:
 *   1. 填充 rk_boot_entry 结构体:
 *      - name: 文件名(宽字符)
 *      - type: Entry 类型
 *      - dataOffset: 数据在镜像中的偏移
 *      - dataSize: 数据大小(对齐后)
 *      - dataDelay: 延迟时间
 *   2. 写入 Entry 结构到输出文件
 *   3. 更新 offset 为下一个 Entry 的数据偏移
 *
 * 返回: true=成功, false=失败
 */
static bool saveEntry(FILE *outFile, char *path, rk_entry_type type,
                      uint16_t delay, uint32_t *offset, char *fixName,
                      bool fix)
{
	LOGD("write:%s\n", path);
	uint32_t size;
	rk_boot_entry entry;
	memset(&entry, 0, sizeof(rk_boot_entry));

	LOGD("write:%s\n", path);

	/* 提取文件名并转换为宽字符(使用自定义名称或从路径提取) */
	getName(fixName ? fixName : path, entry.name);

	/* 填充 Entry 元数据 */
	entry.size = sizeof(rk_boot_entry);  /* Entry 结构本身的大小 */
	entry.type = type;                   /* Entry 类型 */
	entry.dataOffset = *offset;          /* 数据在镜像中的偏移 */

	/* 获取源文件大小 */
	if (!getFileSize(path, &size)) {
		LOGE("save entry(%s) failed:\n\tcannot get file size.\n", path);
		return false;
	}

	/* === 计算对齐后的数据大小 === */
	if (fix)
		/* 固定模式: 先补齐到 512 字节倍数 */
		size = ((size - 1) / SMALL_PACKET + 1) * SMALL_PACKET;

	/* 再补齐到 ENTRY_ALIGN(2048) 字节倍数 */
	uint32_t tmp = size % ENTRY_ALIGN;
	size += tmp ? (ENTRY_ALIGN - tmp) : 0;

	LOGD("align size:%d\n", size);
	entry.dataSize = size;    /* 对齐后的数据大小 */
	entry.dataDelay = delay;  /* 延迟时间 */

	/* 更新 offset 为下一个 Entry 的数据偏移 */
	*offset += size;

	/* 写入 Entry 结构到输出文件 */
	fwrite(&entry, sizeof(rk_boot_entry), 1, outFile);
	return true;
}

/**
 * convertChipType - 将芯片名称字符串转换为 32 位芯片类型 ID
 * @chip: 芯片名称字符串(如 "3399")
 *
 * 功能: 将芯片名称的前 4 个字符转换为 32 位整数
 * 编码方式: Big-Endian 字节序(chip[0] 在高位)
 *
 * 示例:
 *   "3399" -> 0x33333939
 *   "3328" -> 0x33333238
 *
 * 返回: 32 位芯片类型 ID
 */
static inline uint32_t convertChipType(const char *chip)
{
	char buffer[5];
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer), "%s", chip);  /* 复制前 4 个字符 */
	/* 组合成 32 位整数: buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3] */
	return buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
}

/**
 * getChipType - 根据芯片名称获取对应的芯片类型 ID
 * @chip: 芯片名称字符串(如 "RK3399", "RK3328" 等)
 *
 * 功能: 将芯片名称映射到 Rockchip 定义的芯片类型枚举值
 *
 * 支持的芯片列表(部分):
 *   - RK28, RK281X, RKPANDA
 *   - RK27, RKNANO, RKSMART, RKCROWN, RKCAYMAN
 *   - RK29, RK292X
 *   - RK30, RK30B, RK31, RK32
 *   - 其他新芯片(如 RK3399): 通过 convertChipType 动态转换
 *
 * 转换规则:
 *   1. 先匹配预定义的芯片名称
 *   2. 如果不匹配,取芯片名称从第 3 个字符开始(跳过 "RK")转换
 *      示例: "RK3399" -> convertChipType("3399") -> 0x33333939
 *
 * 返回: 芯片类型 ID, 如果不支持则返回 RKNONE_DEVICE
 */
static inline uint32_t getChipType(const char *chip)
{
	LOGD("chip:%s\n", chip);
	int chipType = RKNONE_DEVICE;

	if (!chip) {
		goto end;
	}

	/* === 匹配预定义的芯片类型 === */
	if (!strcmp(chip, CHIP_RK28)) {
		chipType = RK28_DEVICE;
	} else if (!strcmp(chip, CHIP_RK28)) {  /* 重复判断(可能是代码错误) */
		chipType = RK28_DEVICE;
	} else if (!strcmp(chip, CHIP_RK281X)) {
		chipType = RK281X_DEVICE;
	} else if (!strcmp(chip, CHIP_RKPANDA)) {
		chipType = RKPANDA_DEVICE;
	} else if (!strcmp(chip, CHIP_RK27)) {
		chipType = RK27_DEVICE;
	} else if (!strcmp(chip, CHIP_RKNANO)) {
		chipType = RKNANO_DEVICE;
	} else if (!strcmp(chip, CHIP_RKSMART)) {
		chipType = RKSMART_DEVICE;
	} else if (!strcmp(chip, CHIP_RKCROWN)) {
		chipType = RKCROWN_DEVICE;
	} else if (!strcmp(chip, CHIP_RKCAYMAN)) {
		chipType = RKCAYMAN_DEVICE;
	} else if (!strcmp(chip, CHIP_RK29)) {
		chipType = RK29_DEVICE;
	} else if (!strcmp(chip, CHIP_RK292X)) {
		chipType = RK292X_DEVICE;
	} else if (!strcmp(chip, CHIP_RK30)) {
		chipType = RK30_DEVICE;
	} else if (!strcmp(chip, CHIP_RK30B)) {
		chipType = RK30B_DEVICE;
	} else if (!strcmp(chip, CHIP_RK31)) {
		chipType = RK31_DEVICE;
	} else if (!strcmp(chip, CHIP_RK32)) {
		chipType = RK32_DEVICE;
	} else {
		/* 未匹配到预定义类型,动态转换(跳过 "RK" 前缀) */
		chipType = convertChipType(chip + 2);
	}

end:
	LOGD("type:0x%x\n", chipType);
	if (chipType == RKNONE_DEVICE) {
		LOGE("chip type not support!\n");
	}
	return chipType;
}

/**
 * getBoothdr - 生成 Rockchip Boot 镜像头部
 * @hdr: 输出的 rk_boot_header 结构指针
 *
 * 功能: 填充 Boot 镜像头部的所有字段
 *
 * 头部结构说明:
 *   - tag: 魔数(固定值 "BOOT",用于镜像识别)
 *   - size: 头部结构大小
 *   - version: 版本号(BCD 编码)
 *   - mergerVersion: 打包工具版本
 *   - releaseTime: 打包时间戳
 *   - chipType: 芯片类型 ID
 *   - code471Num/Offset/Size: CODE471 Entry 数组信息
 *   - code472Num/Offset/Size: CODE472 Entry 数组信息
 *   - loaderNum/Offset/Size: Loader Entry 数组信息
 *   - rc4Flag: RC4 加密标志(0=启用, 1=禁用)
 */
static inline void getBoothdr(rk_boot_header *hdr)
{
	memset(hdr, 0, sizeof(rk_boot_header));

	/* === 基本信息 === */
	hdr->tag = TAG;  /* 魔数: "BOOT" */
	hdr->size = sizeof(rk_boot_header);  /* 头部大小 */

	/* 版本号: (major << 8 | minor), BCD 编码
	 * 示例: v2.50 -> 0x0250
	 */
	hdr->version = (getBCD(gOpts.major) << 8) | getBCD(gOpts.minor);

	hdr->mergerVersion = MERGER_VERSION;  /* 打包工具版本 */
	hdr->releaseTime = getTime();         /* 当前时间 */
	hdr->chipType = getChipType(gOpts.chip);  /* 芯片类型 ID */

	/* === CODE471 Entry 数组信息(DDR 初始化代码) === */
	hdr->code471Num = gOpts.code471Num;      /* Entry 数量 */
	hdr->code471Offset = sizeof(rk_boot_header);  /* 数组起始偏移(紧跟头部) */
	hdr->code471Size = sizeof(rk_boot_entry);     /* 单个 Entry 大小 */

	/* === CODE472 Entry 数组信息(USB 插件代码) === */
	hdr->code472Num = gOpts.code472Num;
	/* 数组起始偏移 = 头部 + CODE471 数组 */
	hdr->code472Offset = hdr->code471Offset + gOpts.code471Num * hdr->code471Size;
	hdr->code472Size = sizeof(rk_boot_entry);

	/* === Loader Entry 数组信息(FlashData + FlashBoot) === */
	hdr->loaderNum = gOpts.loaderNum;
	/* 数组起始偏移 = 头部 + CODE471 数组 + CODE472 数组 */
	hdr->loaderOffset = hdr->code472Offset + gOpts.code472Num * hdr->code472Size;
	hdr->loaderSize = sizeof(rk_boot_entry);

	/* === RC4 加密标志 === */
	if (!enableRC4)
		hdr->rc4Flag = 1;  /* 1=禁用 RC4 加密, 0=启用 */
}

/**
 * getCrc - 计算文件的 CRC32 校验和
 * @path: 文件路径
 *
 * 功能: 读取整个文件并计算 CRC32 校验值
 * 应用场景: 在镜像末尾添加 CRC32,用于完整性校验
 *
 * 返回: CRC32 校验值, 失败返回 0
 */
static inline uint32_t getCrc(const char *path)
{
	uint32_t size = 0;
	uint32_t crc = 0;
	FILE *file = fopen(path, "rb");

	getFileSize(path, &size);  /* 获取文件大小 */
	if (!file)
		goto end;

	/* 读取整个文件到全局缓冲区 */
	if (!fread(gBuf, size, 1, file))
		goto end;

	/* 计算 CRC32 */
	crc = CRC_32(gBuf, size);
	LOGD("crc:0x%08x\n", crc);

end:
	if (file)
		fclose(file);
	return crc;
}

/**
 * mergeBoot - 合并 Boot 镜像的核心函数
 * @argc: 命令行参数数量
 * @argv: 命令行参数数组
 *
 * 功能: 将多个组件合并成单个 loader.bin 文件
 *
 * 镜像布局:
 *   +---------------------------+
 *   | rk_boot_header            |  头部(包含魔数、版本、时间等)
 *   +---------------------------+
 *   | CODE471 Entry 数组        |  DDR 初始化代码的元数据
 *   +---------------------------+
 *   | CODE472 Entry 数组        |  USB 插件代码的元数据
 *   +---------------------------+
 *   | Loader Entry 数组         |  FlashData/FlashBoot 的元数据
 *   +---------------------------+
 *   | CODE471 数据              |  DDR 初始化代码(加密后)
 *   +---------------------------+
 *   | CODE472 数据              |  USB 插件代码(加密后)
 *   +---------------------------+
 *   | Loader 数据               |  FlashData/FlashBoot(分块加密)
 *   +---------------------------+
 *   | CRC32 校验值(4 字节)      |  整个镜像的 CRC32
 *   +---------------------------+
 *
 * 执行流程:
 *   1. 初始化配置选项(从 INI 文件或命令行)
 *   2. 处理输出文件名后缀
 *   3. 生成并写入镜像头部
 *   4. 依次写入所有 Entry 元数据
 *   5. 依次写入所有组件数据(加密)
 *   6. 计算并写入 CRC32 校验值
 *
 * 返回: true=成功生成镜像, false=失败
 */
static bool mergeBoot(int argc, char **argv)
{
	uint32_t dataOffset;  /* 数据区起始偏移(元数据之后) */
	bool ret = false;
	int i;
	FILE *outFile;
	uint32_t crc;
	rk_boot_header hdr;

	/* === 步骤 1: 初始化配置选项 === */
	if (!initOpts(argc, argv))
		return false;

	/* === 步骤 2: 处理输出文件名后缀 === */
	{
		char *subfix = strstr(gOpts.outPath, OUT_SUBFIX);  /* 查找默认后缀 */
		char version[MAX_LINE_LEN];
		snprintf(version, sizeof(version), "%s", gSubfix);  /* 复制自定义后缀 */

		/* 如果输出路径包含默认后缀,先移除 */
		if (subfix && !strcmp(subfix, OUT_SUBFIX)) {
			subfix[0] = '\0';
		}
		/* 添加自定义后缀(如版本号) */
		strcat(gOpts.outPath, version);
		printf("fix opt:%s\n", gOpts.outPath);
	}

	/* 调试模式: 打印完整配置 */
	if (gDebug) {
		printf("---------------\nUSING CONFIG:\n");
		printOpts(stdout);
		printf("---------------\n\n");
	}

	/* === 步骤 3: 创建输出文件 === */
	outFile = fopen(gOpts.outPath, "wb+");
	if (!outFile) {
		LOGE("open out file(%s) failed\n", gOpts.outPath);
		goto end;
	}

	/* === 步骤 4: 生成并写入镜像头部 === */
	getBoothdr(&hdr);
	LOGD("write hdr\n");
	fwrite(&hdr, 1, sizeof(rk_boot_header), outFile);

	/* === 步骤 5: 计算数据区起始偏移 === */
	/* 数据区偏移 = 头部 + 所有 Entry 元数据 */
	dataOffset = sizeof(rk_boot_header) +
	             (gOpts.code471Num + gOpts.code472Num + gOpts.loaderNum) *
	             sizeof(rk_boot_entry);

	/* === 步骤 6: 写入所有 Entry 元数据 === */
	LOGD("write code 471 entry\n");
	for (i = 0; i < gOpts.code471Num; i++) {
		/* 保存 CODE471 Entry(DDR 初始化),普通加密模式(fix=false) */
		if (!saveEntry(outFile, (char *)gOpts.code471Path[i], ENTRY_471,
		               gOpts.code471Sleep, &dataOffset, NULL, false))
			goto end;
	}

	LOGD("write code 472 entry\n");
	for (i = 0; i < gOpts.code472Num; i++) {
		/* 保存 CODE472 Entry(USB 插件),普通加密模式(fix=false) */
		if (!saveEntry(outFile, (char *)gOpts.code472Path[i], ENTRY_472,
		               gOpts.code472Sleep, &dataOffset, NULL, false))
			goto end;
	}

	LOGD("write loader entry\n");
	for (i = 0; i < gOpts.loaderNum; i++) {
		/* 保存 Loader Entry(FlashData/FlashBoot),分块加密模式(fix=true) */
		if (!saveEntry(outFile, gOpts.loader[i].path, ENTRY_LOADER, 0, &dataOffset,
		               gOpts.loader[i].name, true))
			goto end;
	}

	/* === 步骤 7: 写入所有组件数据(加密后) === */
	LOGD("write code 471\n");
	for (i = 0; i < gOpts.code471Num; i++) {
		/* 写入 CODE471 数据,普通加密模式 */
		if (!writeFile(outFile, (char *)gOpts.code471Path[i], false))
			goto end;
	}

	LOGD("write code 472\n");
	for (i = 0; i < gOpts.code472Num; i++) {
		/* 写入 CODE472 数据,普通加密模式 */
		if (!writeFile(outFile, (char *)gOpts.code472Path[i], false))
			goto end;
	}

	LOGD("write loader\n");
	for (i = 0; i < gOpts.loaderNum; i++) {
		/* 写入 Loader 数据,分块加密模式 */
		if (!writeFile(outFile, gOpts.loader[i].path, true))
			goto end;
	}

	fflush(outFile);  /* 刷新文件缓冲区 */

	/* === 步骤 8: 计算并写入 CRC32 校验值 === */
	LOGD("write crc\n");
	crc = getCrc(gOpts.outPath);  /* 计算整个镜像的 CRC32 */
	if (!fwrite(&crc, sizeof(crc), 1, outFile))
		goto end;

	ret = true;
end:
	if (outFile)
		fclose(outFile);
	return ret;
}

/************merge code end************/
/************unpack code***************/

/**
 * wide2str - 将宽字符数组转换为 ASCII 字符串
 * @wide: 源宽字符数组(uint16_t)
 * @str: 目标 ASCII 字符串缓冲区
 * @len: 要转换的字符数
 *
 * 功能: 将双字节宽字符转换为单字节 ASCII 字符(只保留低 8 位)
 * 应用场景: 解包时将 Entry 名称从 Unicode 转换为 ASCII
 */
static inline void wide2str(const uint16_t *wide, char *str, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		str[i] = (char)(wide[i] & 0xFF);  /* 只取低 8 位 */
	}
	str[len] = 0;  /* 添加字符串终止符 */
}

/**
 * unpackEntry - 解包单个 Entry 到独立文件
 * @entry: Entry 元数据指针
 * @name: 输出文件名
 * @inFile: 输入的 loader.bin 文件指针
 *
 * 功能:
 *   1. 根据 entry->dataOffset 定位到数据位置
 *   2. 读取 entry->dataSize 字节的数据
 *   3. 根据 Entry 类型选择解密方式:
 *      - ENTRY_LOADER: 分块解密(每 512 字节一块)
 *      - 其他类型: 整体解密
 *   4. 将解密后的数据写入到 name 指定的文件
 *
 * 返回: true=成功, false=失败
 */
static bool unpackEntry(rk_boot_entry *entry, const char *name, FILE *inFile)
{
	bool ret = false;
	int size, i;
	FILE *outFile = fopen(name, "wb+");

	if (!outFile)
		goto end;

	printf("unpack entry(%s)\n", name);

	/* 定位到数据位置 */
	fseek(inFile, entry->dataOffset, SEEK_SET);
	size = entry->dataSize;

	/* 读取加密数据到缓冲区 */
	if (!fread(gBuf, size, 1, inFile))
		goto end;

	/* === RC4 解密(与加密算法相同,对称加密) === */
	if (entry->type == ENTRY_LOADER) {
		/* Loader 类型: 分块解密(每 512 字节一块) */
		for (i = 0; i < size / SMALL_PACKET; i++)
			P_RC4(gBuf + i * SMALL_PACKET, SMALL_PACKET);

		/* 处理最后不足 512 字节的部分 */
		if (size % SMALL_PACKET) {
			P_RC4(gBuf + i * SMALL_PACKET, size - SMALL_PACKET * 512);
		}
	} else {
		/* 其他类型: 整体解密 */
		P_RC4(gBuf, size);
	}

	/* 写入解密后的数据 */
	if (!fwrite(gBuf, size, 1, outFile))
		goto end;

	ret = true;
end:
	if (outFile)
		fclose(outFile);
	return ret;
}

/**
 * unpackBoot - 解包整个 loader.bin 文件
 * @path: loader.bin 文件路径
 *
 * 功能:
 *   1. 读取镜像头部(rk_boot_header)
 *   2. 计算 Entry 总数 = code471Num + code472Num + loaderNum
 *   3. 读取所有 Entry 元数据数组
 *   4. 依次解包每个 Entry 到独立文件(文件名从 Entry.name 提取)
 *
 * 输出文件:
 *   - rk3399_ddr_800MHz_v1 (CODE471)
 *   - rk3399_usbplug_v2 (CODE472)
 *   - FlashData (Loader0)
 *   - FlashBoot (Loader1)
 *
 * 返回: true=成功, false=失败
 */
static bool unpackBoot(char *path)
{
	bool ret = false;
	FILE *inFile = fopen(path, "rb");
	int entryNum, i;
	char name[MAX_NAME_LEN];
	rk_boot_entry *entrys;

	if (!inFile) {
		fprintf(stderr, "loader(%s) not found\n", path);
		goto end;
	}

	/* === 步骤 1: 读取镜像头部 === */
	rk_boot_header hdr;
	if (!fread(&hdr, sizeof(rk_boot_header), 1, inFile)) {
		fprintf(stderr, "read header failed\n");
		goto end;
	}

	/* === 步骤 2: 计算并读取所有 Entry 元数据 === */
	entryNum = hdr.code471Num + hdr.code472Num + hdr.loaderNum;
	entrys = (rk_boot_entry *)malloc(sizeof(rk_boot_entry) * entryNum);
	if (!fread(entrys, sizeof(rk_boot_entry) * entryNum, 1, inFile)) {
		fprintf(stderr, "read data failed\n");
		goto end;
	}

	/* === 步骤 3: 依次解包每个 Entry === */
	LOGD("entry num:%d\n", entryNum);
	for (i = 0; i < entryNum; i++) {
		/* 将宽字符名称转换为 ASCII */
		wide2str(entrys[i].name, name, MAX_NAME_LEN);

		LOGD("entry:t=%d, name=%s, off=%d, size=%d\n", entrys[i].type, name,
		     entrys[i].dataOffset, entrys[i].dataSize);

		/* 解包 Entry 到文件 */
		if (!unpackEntry(entrys + i, name, inFile)) {
			fprintf(stderr, "unpack entry(%s) failed\n", name);
			goto end;
		}
	}

	ret = true;
end:
	if (inFile)
		fclose(inFile);
	return ret;
}

/************unpack code end***********/

/**
 * printHelp - 打印帮助信息
 *
 * 功能: 显示工具的使用方法、可用选项和示例
 *
 * 使用模式:
 *   模式 1: 基于 INI 配置文件
 *     boot_merger [--pack] <config.ini>
 *     boot_merger --unpack <loader.bin>
 *
 *   模式 2: 基于命令行参数(必须提供 5 个必需参数)
 *     boot_merger --pack -c <chip> -1 <471.bin> -2 <472.bin> -d <data.bin> -b <boot.bin>
 */
static void printHelp(void)
{
	printf("Usage1: boot_merger [options]... FILE\n");
	printf("Merge or unpack Rockchip's loader (Default action is to merge.)\n");

	printf("Options:\n");
	printf("\t" OPT_MERGE "\t\t\tMerge loader with specified config.\n");
	printf("\t" OPT_UNPACK "\t\tUnpack specified loader to current dir.\n");
	printf("\t" OPT_VERBOSE "\t\tDisplay more runtime informations.\n");
	printf("\t" OPT_HELP "\t\t\tDisplay this information.\n");
	printf("\t" OPT_VERSION "\t\tDisplay version information.\n");
	printf("\t" OPT_SUBFIX "\t\tSpec subfix.\n");
	printf("\t" OPT_REPLACE "\t\tReplace some part of binary path.\n");
	printf("\t" OPT_PREPATH "\t\tAdd prefix path of binary path.\n");
	printf("\t" OPT_SIZE
	       "\t\tImage size.\"--size [image KB size]\", must be 512KB aligned\n");

	printf("Usage2: boot_merger [options] [parameter]\n");
	printf("All below five option are must in this mode!\n");
	printf("\t" OPT_CHIP "\t\tChip type, used for check with usbplug.\n");
	printf("\t" OPT_471 "\t\t471 for download, ddr.bin.\n");
	printf("\t" OPT_472 "\t\t472 for download, usbplug.bin.\n");
	printf("\t" OPT_DATA "\t\tloader0 for flash, ddr.bin.\n");
	printf("\t" OPT_BOOT "\t\tloader1 for flash, miniloader.bin.\n");

	/* 打印示例命令 */
	printf("\n./tools/boot_merger --pack --verbose -c RK322A -1 "
	       "rkbin/rk322x_ddr_300MHz_v1.04.bin -2 "
	       "rkbin/rk32/rk322x_usbplug_v2.32.bin -d "
	       "rkbin/rk32/rk322x_ddr_300MHz_v1.04.bin -b "
	       "rkbin/rk32/rk322x_miniloader_v2.32.bin\n");
}

/**
 * main - 主函数入口
 * @argc: 命令行参数数量
 * @argv: 命令行参数数组
 *
 * 功能: 解析命令行参数,调用合并或解包函数
 *
 * 支持的操作:
 *   1. 合并模式(默认): 将多个组件合并成 loader.bin
 *      boot_merger [--pack] <config.ini>
 *      boot_merger --pack -c RK3399 -1 ddr.bin -2 usbplug.bin -d data.bin -b boot.bin
 *
 *   2. 解包模式: 将 loader.bin 拆分成各个组件
 *      boot_merger --unpack <loader.bin>
 *
 * 命令行选项:
 *   --verbose     启用调试模式,显示详细日志
 *   --help        显示帮助信息
 *   --version     显示版本信息
 *   --pack        明确指定合并模式
 *   --unpack      解包模式
 *   --rc4         启用 RC4 加密
 *   --subfix      指定输出文件后缀
 *   --replace     路径替换(旧路径 新路径)
 *   --prepath     添加路径前缀
 *   --size        指定镜像大小(KB,必须 512KB 对齐)
 *
 * 返回: 0=成功, -1=失败
 */
int main(int argc, char **argv)
{

	int i;
	bool merge = true;      /* 默认为合并模式 */
	char *optPath = NULL;   /* 配置文件路径或 loader.bin 路径 */

	/* === 解析命令行选项 === */
	for (i = 1; i < argc; i++) {
		if (!strcmp(OPT_VERBOSE, argv[i])) {  /* --verbose */
			gDebug = true;
			printf("enable debug\n");
		} else if (!strcmp(OPT_HELP, argv[i])) {  /* --help */
			printHelp();
			return 0;
		} else if (!strcmp(OPT_VERSION, argv[i])) {  /* --version */
			printf("boot_merger (cjf@rock-chips.com)\t" VERSION "\n");
			return 0;
		} else if (!strcmp(OPT_MERGE, argv[i])) {  /* --pack */
			merge = true;
		} else if (!strcmp(OPT_UNPACK, argv[i])) {  /* --unpack */
			merge = false;
		} else if (!strcmp(OPT_RC4, argv[i])) {  /* --rc4 */
			printf("enable RC4 for IDB data(both ddr and preloader)\n");
			enableRC4 = true;
		} else if (!strcmp(OPT_SUBFIX, argv[i])) {  /* --subfix <后缀> */
			i++;
			snprintf(gSubfix, sizeof(gSubfix), "%s", argv[i]);
		} else if (!strcmp(OPT_REPLACE, argv[i])) {  /* --replace <旧路径> <新路径> */
			i++;
			snprintf(gLegacyPath, sizeof(gLegacyPath), "%s", argv[i]);
			i++;
			snprintf(gNewPath, sizeof(gNewPath), "%s", argv[i]);
		} else if (!strcmp(OPT_PREPATH, argv[i])) {  /* --prepath <前缀> */
			i++;
			gPrePath = argv[i];
		} else if (!strcmp(OPT_SIZE, argv[i])) {  /* --size <KB大小> */
			g_merge_max_size = strtoul(argv[++i], NULL, 10);
			/* 检查是否 512KB 对齐 */
			if (g_merge_max_size % 512) {
				printHelp();
				return -1;
			}
			g_merge_max_size *= 1024;  /* 转换为字节 */
		} else {
			/* 非选项参数,作为配置文件或 loader.bin 路径 */
			optPath = argv[i];
			break;
		}
	}

	/* 解包模式必须指定输出路径 */
	if (!merge && !optPath) {
		fprintf(stderr, "need set out path to unpack!\n");
		printHelp();
		return -1;
	}

	/* === 分配全局缓冲区(用于文件读写) === */
	gBuf = calloc(g_merge_max_size, 1);
	if (!gBuf) {
		LOGE("Merge image: calloc buffer error.\n");
		return -1;
	}

	/* === 执行合并或解包操作 === */
	if (merge) {
		LOGD("do_merge\n");
		gConfigPath = optPath;  /* 设置配置文件路径 */
		if (!mergeBoot(argc, argv)) {
			fprintf(stderr, "merge failed!\n");
			return -1;
		}
		printf("merge success(%s)\n", gOpts.outPath);
	} else {
		LOGD("do_unpack\n");
		if (!unpackBoot(optPath)) {
			fprintf(stderr, "unpack failed!\n");
			return -1;
		}
		printf("unpack success\n");
	}

	return 0;
}
