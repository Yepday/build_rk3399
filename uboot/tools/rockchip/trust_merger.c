/*
 * Rockchip trust 镜像生成工具
 * 用于合并 BL30/BL31/BL32/BL33 组件生成 trust.img 固件
 *
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Peter, Software Engineering, <superpeter.cai@gmail.com>.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <sys/stat.h>
#include <u-boot/sha256.h>
#include "trust_merger.h"
#include "sha2.h"

/* #define DEBUG */  // 调试模式开关

// 全局调试标志，根据 DEBUG 宏决定是否启用调试输出
static bool gDebug =
#ifdef DEBUG
        true;
#else
        false;
#endif /* DEBUG */

// 错误日志宏：输出错误信息到标准错误流
#define LOGE(fmt, args...) fprintf(stderr, "E: [%s] " fmt, __func__, ##args)
// 调试日志宏：仅在 gDebug 为 true 时输出调试信息
#define LOGD(fmt, args...)                                                     \
  do {                                                                         \
    if (gDebug)                                                                \
      fprintf(stderr, "D: [%s] " fmt, __func__, ##args);                       \
  } while (0)

/* trust 镜像备份数量，默认有 2 个备份 */
static uint32_t g_trust_max_num = 2;
/* 每个 trust 镜像的最大尺寸，默认 2MB */
static uint32_t g_trust_max_size = 2 * 1024 * 1024;

/* SHA 和 RSA 加密算法配置 */
#define SHA_SEL_256 3    /* SHA-256 小端模式 */
#define SHA_SEL_256_RK 2 /* SHA-256 大端模式：仅 RK3368 需要 */
#define SHA_SEL_160 1    /* SHA-160 模式 */
#define SHA_SEL_NONE 0   /* 不使用 SHA 校验 */

#define RSA_SEL_2048_PSS 3 /* RSA-2048 PSS 模式：仅 RK3326/PX30/RK3308 使用 */
#define RSA_SEL_2048 2     /* RSA-2048 标准模式：大多数平台使用 */
#define RSA_SEL_1024 1     /* RSA-1024 模式 */
#define RSA_SEL_NONE 0     /* 不使用 RSA 签名 */

// 判断字符是否为数字的宏
#define is_digit(c) ((c) >= '0' && (c) <= '9')

// 全局变量定义
static char *gConfigPath;              // 配置文件路径
static OPT_T gOpts;                    // 存储从配置文件解析的选项
#define BL3X_FILESIZE_MAX (512 * 1024) // BL3x 文件最大尺寸 512KB
static uint8_t gBuf[BL3X_FILESIZE_MAX]; // 全局缓冲区用于文件读写
static bool gSubfix;                   // 子后缀标志
static char *gLegacyPath;              // 旧路径（用于路径替换）
static char *gNewPath;                 // 新路径（用于路径替换）
static char *gPrePath;                 // 前缀路径
static uint8_t gRSAmode = RSA_SEL_2048; // RSA 加密模式，默认 2048 位
static uint8_t gSHAmode = SHA_SEL_256;  // SHA 哈希模式，默认 SHA-256
static bool gIgnoreBL32;               // 是否忽略 BL32 组件

// BL30/BL31/BL32/BL33 的组件标识符（ASCII 码）
const uint8_t gBl3xID[BL_MAX_SEC][4] = { { 'B', 'L', '3', '0' },
	{ 'B', 'L', '3', '1' },
	{ 'B', 'L', '3', '2' },
	{ 'B', 'L', '3', '3' }
};

/**
 * 将十进制数值转换为 BCD 码（Binary-Coded Decimal）
 * @param value 输入的十进制值（不超过 0xFFFF）
 * @return BCD 编码后的值
 */
static inline uint32_t getBCD(uint16_t value)
{
	uint8_t tmp[2] = { 0 };
	int i;
	uint32_t ret;

	// 输入值检查，不能超过 16 位
	if (value > 0xFFFF) {
		return 0;
	}

	// 将十进制数值转换为 BCD 码
	// 例如：1234 -> 0x1234 (BCD)
	for (i = 0; i < 2; i++) {
		tmp[i] = (((value / 10) % 10) << 4) | (value % 10);
		value /= 100;
	}
	ret = ((uint16_t)(tmp[1] << 8)) | tmp[0];

	LOGD("ret:%x\n", ret);
	return ret & 0xFF;  // 返回低 8 位
}

/**
 * 修正文件路径格式
 * 1. 将反斜杠 '\' 转换为正斜杠 '/'
 * 2. 移除回车换行符
 * 3. 根据全局路径设置进行路径替换或添加前缀
 * @param path 需要修正的路径字符串（会被直接修改）
 */
static inline void fixPath(char *path)
{
	int i, len = strlen(path);
	char tmp[MAX_LINE_LEN];
	char *start, *end;

	// 路径字符规范化：反斜杠转正斜杠，移除换行符
	for (i = 0; i < len; i++) {
		if (path[i] == '\\')
			path[i] = '/';        // Windows 路径转 Unix 格式
		else if (path[i] == '\r' || path[i] == '\n')
			path[i] = '\0';       // 移除行尾字符
	}

	// 如果设置了路径替换（gLegacyPath 替换为 gNewPath）
	if (gLegacyPath && gNewPath) {
		start = strstr(path, gLegacyPath);
		if (start) {
			// 找到旧路径，进行替换
			end = start + strlen(gLegacyPath);
			/* 备份后半部分，以便 tmp 可以作为 strcat() 的源 */
			strcpy(tmp, end);
			/* 截断，以便 path 可以作为 strcat() 的目标 */
			*start = '\0';
			strcat(path, gNewPath);
			strcat(path, tmp);
		} else {
			// 未找到旧路径，直接在前面添加新路径
			strcpy(tmp, path);
			strcpy(path, gNewPath);
			strcat(path, tmp);
		}
	} else if ((ulong)path != (ulong)gOpts.outPath && /* 忽略输出路径 */
		   gPrePath && strncmp(path, gPrePath, strlen(gPrePath))) {
		// 如果设置了前缀路径且当前路径还没有该前缀，添加之
		strcpy(tmp, path);
		strcpy(path, gPrePath);
		strcat(path, tmp);
	}
}

/**
 * 解析配置文件中的版本信息段
 * @param file 已打开的配置文件指针
 * @return 解析成功返回 true，失败返回 false
 */
static bool parseVersion(FILE *file)
{
	int d = 0;

	// 跳过空白字符和注释
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	// 解析主版本号 (MAJOR)
	if (fscanf(file, OPT_MAJOR "=%d", &d) != 1)
		return false;
	gOpts.major = (uint16_t) d;

	if (SCANF_EAT(file) != 0) {
		return false;
	}
	// 解析次版本号 (MINOR)
	if (fscanf(file, OPT_MINOR "=%d", &d) != 1)
		return false;
	gOpts.minor = (uint16_t) d;
	LOGD("major:%d, minor:%d\n", gOpts.major, gOpts.minor);
	return true;
}

/**
 * 解析配置文件中的 BL3x 组件信息（BL30/BL31/BL32/BL33）
 * @param file 已打开的配置文件指针
 * @param bl3x_id 组件 ID (BL30_SEC/BL31_SEC/BL32_SEC/BL33_SEC)
 * @return 解析成功返回 true，失败返回 false
 */
static bool parseBL3x(FILE *file, int bl3x_id)
{
	int pos;
	int sec;
	char buf[MAX_LINE_LEN];
	bl_entry_t *pbl3x = NULL;

	// 检查组件 ID 是否有效
	if (bl3x_id >= BL_MAX_SEC) {
		return false;
	}

	pbl3x = &gOpts.bl3x[bl3x_id];

	/* 解析 SEC 字段：是否启用该组件 (0=禁用, 1=启用) */
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	if (fscanf(file, OPT_SEC "=%d", &sec) != 1) {
		return false;
	}
	// 根据 gSubfix 标志调整 BL32 的 sec 值
	if ((gSubfix) && (bl3x_id == BL32_SEC)) {
		if (sec == 0) {
			sec = 1;
			printf("BL3%d adjust sec from 0 to 1\n", bl3x_id);
		}
	} else if (gIgnoreBL32 && (bl3x_id == BL32_SEC)) {
		// 如果设置了忽略 BL32 标志，强制禁用
		if (sec == 1) {
			sec = 0;
			printf("BL3%d adjust sec from 1 to 0\n", bl3x_id);
		}
	}
	pbl3x->sec = sec;
	LOGD("bl3%d sec: %d\n", bl3x_id, pbl3x->sec);

	/* 解析 PATH 字段：组件二进制文件路径 */
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	memset(buf, 0, MAX_LINE_LEN);
	if (fscanf(file, OPT_PATH "=%s", buf) != 1) {
		// 如果该组件被启用，路径必须存在
		if (pbl3x->sec)
			return false;
	} else {
		if (strlen(buf) != 0) {
			fixPath(buf);  // 修正路径格式
			strcpy(pbl3x->path, buf);
			LOGD("bl3%d path:%s\n", bl3x_id, pbl3x->path);
		}
	}

	/* 解析 ADDR 字段：组件加载地址（运行时地址） */
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	memset(buf, 0, MAX_LINE_LEN);
	if (fscanf(file, OPT_ADDR "=%s", buf) != 1) {
		if (pbl3x->sec)
			return false;
	} else {
		if (strlen(buf) != 0) {
			pbl3x->addr = strtoul(buf, NULL, 16);  // 16 进制地址
			LOGD("bl3%d addr:0x%x\n", bl3x_id, pbl3x->addr);
		}
	}

	// 记录文件位置并跳过空白字符
	pos = ftell(file);
	if (pos < 0) {
		return false;
	}
	if (SCANF_EAT(file) != 0) {
		return false;
	}

	return true;
}

/**
 * 解析配置文件中的输出路径
 * @param file 已打开的配置文件指针
 * @return 解析成功返回 true，失败返回 false
 */
static bool parseOut(FILE *file)
{
	if (SCANF_EAT(file) != 0) {
		return false;
	}
	// 读取输出文件路径，直到遇到回车或换行
	if (fscanf(file, OPT_OUT_PATH "=%[^\r^\n]", gOpts.outPath) != 1)
		return false;
	/* fixPath(gOpts.outPath); */  // 输出路径不需要修正
	printf("out:%s\n", gOpts.outPath);

	return true;
}

/**
 * 打印配置选项到输出流
 * @param out 输出文件流（stdout 或文件）
 */
void printOpts(FILE *out)
{
	// 输出 BL30 配置
	fprintf(out, SEC_BL30 "\n" OPT_SEC "=%d\n", gOpts.bl3x[BL30_SEC].sec);
	if (gOpts.bl3x[BL30_SEC].sec) {
		fprintf(out, OPT_PATH "=%s\n", gOpts.bl3x[BL30_SEC].path);
		fprintf(out, OPT_ADDR "=0x%08x\n", gOpts.bl3x[BL30_SEC].addr);
	}

	// 输出 BL31 配置（ARM Trusted Firmware）
	fprintf(out, SEC_BL31 "\n" OPT_SEC "=%d\n", gOpts.bl3x[BL31_SEC].sec);
	if (gOpts.bl3x[BL31_SEC].sec) {
		fprintf(out, OPT_PATH "=%s\n", gOpts.bl3x[BL31_SEC].path);
		fprintf(out, OPT_ADDR "=0x%08x\n", gOpts.bl3x[BL31_SEC].addr);
	}

	// 输出 BL32 配置（OP-TEE）
	fprintf(out, SEC_BL32 "\n" OPT_SEC "=%d\n", gOpts.bl3x[BL32_SEC].sec);
	if (gOpts.bl3x[BL32_SEC].sec) {
		fprintf(out, OPT_PATH "=%s\n", gOpts.bl3x[BL32_SEC].path);
		fprintf(out, OPT_ADDR "=0x%08x\n", gOpts.bl3x[BL32_SEC].addr);
	}

	// 输出 BL33 配置（U-Boot）
	fprintf(out, SEC_BL33 "\n" OPT_SEC "=%d\n", gOpts.bl3x[BL33_SEC].sec);
	if (gOpts.bl3x[BL33_SEC].sec) {
		fprintf(out, OPT_PATH "=%s\n", gOpts.bl3x[BL33_SEC].path);
		fprintf(out, OPT_ADDR "=0x%08x\n", gOpts.bl3x[BL33_SEC].addr);
	}

	// 输出路径配置
	fprintf(out, SEC_OUT "\n" OPT_OUT_PATH "=%s\n", gOpts.outPath);
}

/**
 * 解析配置文件，读取所有 BL3x 组件的配置信息
 * @return 解析成功返回 true，失败返回 false
 */
static bool parseOpts(void)
{
	FILE *file = NULL;
	char *configPath = (gConfigPath == NULL) ? DEF_CONFIG_FILE : gConfigPath;
	bool bl30ok = false, bl31ok = false, bl32ok = false, bl33ok = false;
	bool outOk = false;
	bool versionOk = false;
	char buf[MAX_LINE_LEN];
	bool ret = false;

	// 打开配置文件
	file = fopen(configPath, "r");
	if (!file) {
		fprintf(stderr, "config(%s) not found!\n", configPath);
		// 如果使用默认配置文件且不存在，创建一个模板
		if (configPath == (char *)DEF_CONFIG_FILE) {
			file = fopen(DEF_CONFIG_FILE, "w");
			if (file) {
				fprintf(stderr, "create defconfig\n");
				printOpts(file);  // 写入默认配置
			}
		}
		goto end;
	}

	LOGD("start parse\n");

	// 跳过文件开头的空白字符
	if (SCANF_EAT(file) != 0) {
		goto end;
	}

	// 逐行读取配置文件，解析各个段
	while (fscanf(file, "%s", buf) == 1) {
		if (!strcmp(buf, SEC_VERSION)) {
			// 解析版本信息段
			versionOk = parseVersion(file);
			if (!versionOk) {
				LOGE("parseVersion failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_BL30)) {
			// 解析 BL30 段
			bl30ok = parseBL3x(file, BL30_SEC);
			if (!bl30ok) {
				LOGE("parseBL30 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_BL31)) {
			// 解析 BL31 段（ARM Trusted Firmware）
			bl31ok = parseBL3x(file, BL31_SEC);
			if (!bl31ok) {
				LOGE("parseBL31 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_BL32)) {
			// 解析 BL32 段（OP-TEE）
			bl32ok = parseBL3x(file, BL32_SEC);
			if (!bl32ok) {
				LOGE("parseBL32 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_BL33)) {
			// 解析 BL33 段（U-Boot）
			bl33ok = parseBL3x(file, BL33_SEC);
			if (!bl33ok) {
				LOGE("parseBL33 failed!\n");
				goto end;
			}
		} else if (!strcmp(buf, SEC_OUT)) {
			// 解析输出路径段
			outOk = parseOut(file);
			if (!outOk) {
				LOGE("parseOut failed!\n");
				goto end;
			}
		} else if (buf[0] == '#') {
			// 跳过注释行
			continue;
		} else {
			// 未知段名
			LOGE("unknown sec: %s!\n", buf);
			goto end;
		}
		// 跳过段之间的空白字符
		if (SCANF_EAT(file) != 0) {
			goto end;
		}
	}

	// 检查所有必需的段是否都已成功解析
	if (bl30ok && bl31ok && bl32ok && bl33ok && outOk)
		ret = true;
end:
	if (file)
		fclose(file);

	return ret;
}

/**
 * 初始化配置选项为默认值，然后调用 parseOpts 解析配置文件
 * @return 初始化成功返回 true，失败返回 false
 */
bool initOpts(void)
{

	// 清零配置结构体
	memset(&gOpts, 0, sizeof(gOpts));

	// 设置默认版本号
	gOpts.major = DEF_MAJOR;
	gOpts.minor = DEF_MINOR;

	// 初始化 BL30 默认配置
	memcpy(&gOpts.bl3x[BL30_SEC].id, gBl3xID[BL30_SEC], 4);
	strcpy(gOpts.bl3x[BL30_SEC].path, DEF_BL30_PATH);

	// 初始化 BL31 默认配置（ARM Trusted Firmware）
	memcpy(&gOpts.bl3x[BL31_SEC].id, gBl3xID[BL31_SEC], 4);
	strcpy(gOpts.bl3x[BL31_SEC].path, DEF_BL31_PATH);

	// 初始化 BL32 默认配置（OP-TEE）
	memcpy(&gOpts.bl3x[BL32_SEC].id, gBl3xID[BL32_SEC], 4);
	strcpy(gOpts.bl3x[BL32_SEC].path, DEF_BL32_PATH);

	// 初始化 BL33 默认配置（U-Boot）
	memcpy(&gOpts.bl3x[BL33_SEC].id, gBl3xID[BL33_SEC], 4);
	strcpy(gOpts.bl3x[BL33_SEC].path, DEF_BL33_PATH);

	// 设置默认输出路径
	strcpy(gOpts.outPath, DEF_OUT_PATH);

	// 解析配置文件（会覆盖上述默认值）
	return parseOpts();
}

/**
 * 获取文件大小
 * @param path 文件路径
 * @param size 输出参数，文件大小（字节）
 * @return 成功返回 true，失败返回 false
 */
static inline bool getFileSize(const char *path, uint32_t *size)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return false;
	*size = st.st_size;
	LOGD("path:%s, size:%d\n", path, *size);
	return true;
}

/**
 * 向文件填充指定字符
 * @param file 文件指针
 * @param ch 填充字符
 * @param fill_size 填充大小（字节）
 */
void fill_file(FILE *file, char ch, uint32_t fill_size)
{
	uint8_t fill_buffer[1024];
	uint32_t cur_write;

	memset(fill_buffer, ch, 1024);
	while (fill_size > 0) {
		// 每次最多写入 1024 字节
		cur_write = (fill_size >= 1024) ? 1024 : fill_size;
		fwrite(fill_buffer, 1, cur_write, file);
		fill_size -= cur_write;
	}
}

/**
 * 过滤 ELF 文件，提取 PT_LOAD 可加载段信息
 * @param index BL3x 索引 (BL30_SEC/BL31_SEC/BL32_SEC/BL33_SEC)
 * @param pMeta 元数据缓冲区，用于存储段信息
 * @param pMetaNum 输入/输出参数，当前元数据数量
 * @param bElf 输出参数，指示文件是否为 ELF 格式
 * @return 处理成功返回 true，失败返回 false
 */
bool filter_elf(uint32_t index, uint8_t *pMeta, uint32_t *pMetaNum,
                bool *bElf)
{
	bool ret = false;
	FILE *file = NULL;
	uint8_t *file_buffer = NULL;
	uint32_t file_size, read_size, i;
	Elf32_Ehdr *pElfHeader32;    // 32 位 ELF 文件头
	Elf32_Phdr *pElfProgram32;   // 32 位程序头
	Elf64_Ehdr *pElfHeader64;    // 64 位 ELF 文件头
	Elf64_Phdr *pElfProgram64;   // 64 位程序头
	bl_entry_t *pEntry = (bl_entry_t *)(pMeta + sizeof(bl_entry_t) * (*pMetaNum));
	LOGD("index=%d,file=%s\n", index, gOpts.bl3x[index].path);

	// 获取文件大小
	if (!getFileSize(gOpts.bl3x[index].path, &file_size))
		goto exit_fileter_elf;

	// 打开二进制文件
	file = fopen(gOpts.bl3x[index].path, "rb");
	if (!file) {
		LOGE("open file(%s) failed\n", gOpts.bl3x[index].path);
		goto exit_fileter_elf;
	}

	// 分配文件缓冲区并读取整个文件
	file_buffer = malloc(file_size);
	if (!file_buffer)
		goto exit_fileter_elf;
	read_size = fread(file_buffer, 1, file_size, file);
	if (read_size != file_size)
		goto exit_fileter_elf;

	// 检查 ELF 魔数 (0x7F 'E' 'L' 'F')
	if (*((uint32_t *)file_buffer) != ELF_MAGIC) {
		// 不是 ELF 文件，按普通二进制文件处理
		ret = true;
		*bElf = false;
		goto exit_fileter_elf;
	}

	*bElf = true;

	// 检查字节序：仅支持小端模式
	if (file_buffer[5] != 1) { /* only support little endian */
		goto exit_fileter_elf;
	}

	// 检查文件类型：仅支持可执行文件
	if (*((uint16_t *)(file_buffer + EI_NIDENT)) !=
	    2) { /* only support executable case */
		goto exit_fileter_elf;
	}

	// 根据 ELF 文件类别（32 位或 64 位）处理
	if (file_buffer[4] == 2) {
		// 64 位 ELF 文件
		pElfHeader64 = (Elf64_Ehdr *)file_buffer;
		// 遍历所有程序头，查找 PT_LOAD 类型段
		for (i = 0; i < pElfHeader64->e_phnum; i++) {
			pElfProgram64 = (Elf64_Phdr *)(file_buffer + pElfHeader64->e_phoff +
			                               i * pElfHeader64->e_phentsize);
			if (pElfProgram64->p_type == 1) { /* PT_LOAD 可加载段 */
				pEntry->id = gOpts.bl3x[index].id;
				strcpy(pEntry->path, gOpts.bl3x[index].path);
				pEntry->size = (uint32_t) pElfProgram64->p_filesz;      // 文件中的大小
				pEntry->offset = (uint32_t) pElfProgram64->p_offset;    // 文件内偏移
				pEntry->align_size = DO_ALIGN(pEntry->size, ENTRY_ALIGN); // 对齐后大小
				pEntry->addr = (uint32_t) pElfProgram64->p_vaddr;       // 虚拟地址
				if (pEntry->align_size > BL3X_FILESIZE_MAX) {
					LOGE("elf_file %s too large,segment=%d.\n", pEntry->path, i);
					goto exit_fileter_elf;
				}
				LOGD("bl3%d: filesize = %d, imagesize = %d, segment=%d\n", index,
				     pEntry->size, pEntry->align_size, i);
				pEntry++;
				(*pMetaNum)++;  // 增加元数据计数
			}
		}

	} else {
		// 32 位 ELF 文件
		pElfHeader32 = (Elf32_Ehdr *)file_buffer;
		// 遍历所有程序头，查找 PT_LOAD 类型段
		for (i = 0; i < pElfHeader32->e_phnum; i++) {
			pElfProgram32 = (Elf32_Phdr *)(file_buffer + pElfHeader32->e_phoff +
			                               i * pElfHeader32->e_phentsize);
			if (pElfProgram32->p_type == 1) { /* PT_LOAD 可加载段 */
				pEntry->id = gOpts.bl3x[index].id;
				strcpy(pEntry->path, gOpts.bl3x[index].path);
				pEntry->size = pElfProgram32->p_filesz;
				pEntry->offset = pElfProgram32->p_offset;
				pEntry->align_size = DO_ALIGN(pEntry->size, ENTRY_ALIGN);
				pEntry->addr = pElfProgram32->p_vaddr;
				if (pEntry->align_size > BL3X_FILESIZE_MAX) {
					LOGE("elf_file %s too large,segment=%d.\n", pEntry->path, i);
					goto exit_fileter_elf;
				}
				LOGD("bl3%d: filesize = %d, imagesize = %d, segment=%d\n", index,
				     pEntry->size, pEntry->align_size, i);
				pEntry++;
				(*pMetaNum)++;
			}
		}
	}
	ret = true;
exit_fileter_elf:
	if (file)
		fclose(file);
	if (file_buffer)
		free(file_buffer);
	return ret;
}

#define SHA256_CHECK_SZ ((uint32_t)(256 * 1024))  // SHA256 分块计算的块大小

/**
 * 计算 BL3x 组件的 SHA256 哈希值
 * @param pHash 输出缓冲区，存储计算得到的哈希值（32 字节）
 * @param pData 输入数据缓冲区
 * @param nDataSize 输入数据大小
 * @return 成功返回 true，失败返回 false
 */
static bool bl3xHash256(uint8_t *pHash, uint8_t *pData, uint32_t nDataSize)
{
	uint32_t nHashSize, nHasHashSize;

	if (!pHash || !pData || !nDataSize) {
		return false;
	}

	nHasHashSize = 0;

	// 根据 SHA 模式选择不同的哈希算法
	if (gSHAmode == SHA_SEL_256_RK) {
		// RK3368 使用大端模式的 SHA256
		sha256_ctx ctx;

		sha256_begin(&ctx);
		// 分块计算哈希，避免一次处理过大的数据
		while (nDataSize > 0) {
			nHashSize = (nDataSize >= SHA256_CHECK_SZ) ? SHA256_CHECK_SZ : nDataSize;
			sha256_hash(&ctx, pData + nHasHashSize, nHashSize);
			nHasHashSize += nHashSize;
			nDataSize -= nHashSize;
		}
		sha256_end(&ctx, pHash);
	} else {
		// 大多数平台使用小端模式的 SHA256
		sha256_context ctx;

		sha256_starts(&ctx);
		while (nDataSize > 0) {
			nHashSize = (nDataSize >= SHA256_CHECK_SZ) ? SHA256_CHECK_SZ : nDataSize;
			sha256_update(&ctx, pData + nHasHashSize, nHashSize);
			nHasHashSize += nHashSize;
			nDataSize -= nHashSize;
		}
		sha256_finish(&ctx, pHash);
	}
	return true;
}

/**
 * 合并 trust 镜像 - 核心函数
 * 将 BL30/BL31/BL32/BL33 组件合并成 trust.img 固件文件
 *
 * Trust 镜像结构：
 * - Trust Header (2048 字节)
 * - BL3x 组件数据（对齐到 ENTRY_ALIGN）
 * - 可选：多个备份副本（由 g_trust_max_num 控制）
 *
 * @return 成功返回 true，失败返回 false
 */
static bool mergetrust(void)
{
	FILE *outFile = NULL;
	uint32_t OutFileSize;
	uint32_t SrcFileNum, SignOffset, nComponentNum;
	TRUST_HEADER *pHead = NULL;            // Trust 头部结构
	COMPONENT_DATA *pComponentData = NULL; // 组件数据区（加载地址 + 哈希）
	TRUST_COMPONENT *pComponent = NULL;    // 组件信息区（ID + 存储地址 + 大小）
	bool ret = false, bElf;
	uint32_t i, n;
	uint8_t *outBuf = NULL, *pbuf = NULL, *pMetaBuf = NULL;
	bl_entry_t *pEntry = NULL;

	// 初始化配置选项
	if (!initOpts())
		return false;

	// 调试模式下打印配置信息
	if (gDebug) {
		printf("---------------\nUSING CONFIG:\n");
		printOpts(stdout);
		printf("---------------\n\n");
	}

	// 分配元数据缓冲区，最多支持 32 个段
	pMetaBuf = malloc(sizeof(bl_entry_t) * 32);
	if (!pMetaBuf) {
		LOGE("Merge trust image: malloc buffer error.\n");
		goto end;
	}

	// 第一阶段：解析所有 BL3x 文件，提取 ELF 段或整个二进制文件
	nComponentNum = SrcFileNum = 0;
	for (i = BL30_SEC; i < BL_MAX_SEC; i++) {
		if (gOpts.bl3x[i].sec) {  // 如果该组件被启用
			// 过滤 ELF 文件，提取 PT_LOAD 段信息
			if (!filter_elf(i, pMetaBuf, &nComponentNum, &bElf)) {
				LOGE("filter_elf %s file failed\n", gOpts.bl3x[i].path);
				goto end;
			}
			if (!bElf) {
				// 不是 ELF 文件，按普通二进制文件处理
				pEntry = (bl_entry_t *)(pMetaBuf + sizeof(bl_entry_t) * nComponentNum);
				pEntry->id = gOpts.bl3x[i].id;
				strcpy(pEntry->path, gOpts.bl3x[i].path);
				getFileSize(pEntry->path, &pEntry->size);
				pEntry->offset = 0;  // 从文件开头读取
				pEntry->align_size = DO_ALIGN(pEntry->size, ENTRY_ALIGN);
				pEntry->addr = gOpts.bl3x[i].addr;
				if (pEntry->align_size > BL3X_FILESIZE_MAX) {
					LOGE("file %s too large.\n", gOpts.bl3x[i].path);
					goto end;
				}
				LOGD("bl3%d: filesize = %d, imagesize = %d\n", i, pEntry->size,
				     pEntry->align_size);
				pEntry++;
				nComponentNum++;
			}

		}
	}
	LOGD("bl3x bin sec = %d\n", nComponentNum);

	// 第二阶段：构建 Trust Header
	/* 分配 2048 字节用于头部 */
	memset(gBuf, 0, TRUST_HEADER_SIZE);

	/* Trust 头部初始化 */
	pHead = (TRUST_HEADER *)gBuf;
	memcpy(&pHead->tag, TRUST_HEAD_TAG, 4);  // 魔数 "TRUS"
	// 版本号转换为 BCD 码
	pHead->version = (getBCD(gOpts.major) << 8) | getBCD(gOpts.minor);
	pHead->flags = 0;
	pHead->flags |= (gSHAmode << 0);  // 低 4 位：SHA 模式
	pHead->flags |= (gRSAmode << 4);  // 高 4 位：RSA 模式

	// 计算签名偏移和组件数量
	SignOffset = sizeof(TRUST_HEADER) + nComponentNum * sizeof(COMPONENT_DATA);
	LOGD("trust bin sign offset = %d\n", SignOffset);
	// size 字段高 16 位存储组件数量，低 16 位存储签名偏移（以 4 字节为单位）
	pHead->size = (nComponentNum << 16) | (SignOffset >> 2);

	// 组件信息区位于签名区之后
	pComponent = (TRUST_COMPONENT *)(gBuf + SignOffset + SIGNATURE_SIZE);
	// 组件数据区紧跟在头部之后
	pComponentData = (COMPONENT_DATA *)(gBuf + sizeof(TRUST_HEADER));

	// 第三阶段：填充组件信息
	OutFileSize = TRUST_HEADER_SIZE;
	pEntry = (bl_entry_t *)pMetaBuf;
	for (i = 0; i < nComponentNum; i++) {
		/* BL3x 加载和运行地址 */
		pComponentData->LoadAddr = pEntry->addr;

		// 填充组件信息
		pComponent->ComponentID = pEntry->id;                    // 组件 ID (BL30/31/32/33)
		pComponent->StorageAddr = (OutFileSize >> 9);           // 存储地址（以 512 字节为单位）
		pComponent->ImageSize = (pEntry->align_size >> 9);      // 镜像大小（以 512 字节为单位）

		LOGD("bl3%c: LoadAddr = 0x%08x, StorageAddr = %d, ImageSize = %d\n",
		     (char)((pEntry->id & 0xFF000000) >> 24), pComponentData->LoadAddr,
		     pComponent->StorageAddr, pComponent->ImageSize);

		OutFileSize += pEntry->align_size;
		pComponentData++;
		pComponent++;
		pEntry++;
	}

	/* 创建输出文件 */
	outFile = fopen(gOpts.outPath, "wb+");
	if (!outFile) {
		LOGE("open out file(%s) failed\n", gOpts.outPath);

		// 尝试使用默认路径
		outFile = fopen(DEF_OUT_PATH, "wb");
		if (!outFile) {
			LOGE("open default out file:%s failed!\n", DEF_OUT_PATH);
			goto end;
		}
	}

	// 第四阶段：写入数据到输出文件
#if 0
	// 旧的简单写入方式（已禁用）
	/* save trust head to out file */
	if (!fwrite(gBuf, TRUST_HEADER_SIZE, 1, outFile))
		goto end;

	/* save trust bl3x bin */
	for (i = BL30_SEC; i < BL_MAX_SEC; i++) {
		if (gOpts.bl3x[i].sec) {
			FILE *inFile = fopen(gOpts.bl3x[i].path, "rb");
			if (!inFile)
				goto end;

			memset(gBuf, 0, imagesize[i]);
			if (!fread(gBuf, filesize[i], 1, inFile))
				goto end;
			fclose(inFile);

			if (!fwrite(gBuf, imagesize[i], 1, outFile))
				goto end;
		}
	}
#else
	/* 检查镜像大小是否超限 */
	if (OutFileSize > g_trust_max_size) {
		LOGE("Merge trust image: trust bin size overfull.\n");
		goto end;
	}

	/* 分配完整的输出缓冲区（包含所有备份副本） */
	pbuf = outBuf = calloc(g_trust_max_size, g_trust_max_num);
	if (!outBuf) {
		LOGE("Merge trust image: calloc buffer error.\n");
		goto end;
	}
	memset(outBuf, 0, (g_trust_max_size * g_trust_max_num));

	/* 保存 trust 头部数据 */
	memcpy(pbuf, gBuf, TRUST_HEADER_SIZE);
	pbuf += TRUST_HEADER_SIZE;

	uint8_t *pHashData = NULL;
	pComponentData = (COMPONENT_DATA *)(outBuf + sizeof(TRUST_HEADER));

	/* 保存 trust bl3x 二进制数据并计算哈希 */
	pEntry = (bl_entry_t *)pMetaBuf;
	for (i = 0; i < nComponentNum; i++) {
		FILE *inFile = fopen(pEntry->path, "rb");
		if (!inFile)
			goto end;

		// 清零并读取组件数据
		memset(gBuf, 0, pEntry->align_size);
		fseek(inFile, pEntry->offset, SEEK_SET);  // 定位到 ELF 段偏移
		if (!fread(gBuf, pEntry->size, 1, inFile))
			goto end;
		fclose(inFile);

		/* 计算 BL3x 组件的 SHA256 哈希 */
		pHashData = (uint8_t *)&pComponentData->HashData[0];
		bl3xHash256(pHashData, gBuf, pEntry->align_size);
		memcpy(pbuf, gBuf, pEntry->align_size);

		pComponentData++;
		pbuf += pEntry->align_size;
		pEntry++;
	}

	/* 复制其他备份副本（g_trust_max_num - 1 个） */
	for (n = 1; n < g_trust_max_num; n++) {
		memcpy(outBuf + g_trust_max_size * n, outBuf, g_trust_max_size);
	}

	/* 将数据写入文件 */
	if (!fwrite(outBuf, g_trust_max_size * g_trust_max_num, 1, outFile)) {
		LOGE("Merge trust image: write file error.\n");
		goto end;
	}
#endif

	ret = true;

end:
	/*
	// 清理临时 ELF 文件（已禁用）
		for (i = BL30_SEC; i < BL_MAX_SEC; i++) {
			if (gOpts.bl3x[i].sec != false) {
				if (gOpts.bl3x[i].is_elf) {
					if (stat(gOpts.bl3x[i].path, &st) >= 0)
						remove(gOpts.bl3x[i].path);
				}
			}
		}
	*/
	// 释放资源
	if (pMetaBuf)
		free(pMetaBuf);
	if (outBuf)
		free(outBuf);
	if (outFile)
		fclose(outFile);
	return ret;
}

/**
 * 将数据保存到文件
 * @param FileName 输出文件名
 * @param pBuf 数据缓冲区
 * @param size 数据大小
 * @return 成功返回 0，失败返回 -1
 */
static int saveDatatoFile(char *FileName, void *pBuf, uint32_t size)
{
	FILE *OutFile = NULL;
	int ret = -1;

	OutFile = fopen(FileName, "wb");
	if (!OutFile) {
		printf("open OutPutFlie:%s failed!\n", FileName);
		goto end;
	}
	if (1 != fwrite(pBuf, size, 1, OutFile)) {
		printf("write output file failed!\n");
		goto end;
	}

	ret = 0;
end:
	if (OutFile)
		fclose(OutFile);

	return ret;
}

/**
 * 解包 trust 镜像文件
 * 从 trust.img 中提取各个 BL3x 组件并保存为单独的文件
 * @param path trust 镜像文件路径
 * @return 成功返回 true，失败返回 false
 */
static bool unpacktrust(char *path)
{
	FILE *FileSrc = NULL;
	uint32_t FileSize;
	uint8_t *pBuf = NULL;
	uint32_t SrcFileNum, SignOffset;
	TRUST_HEADER *pHead = NULL;
	COMPONENT_DATA *pComponentData = NULL;
	TRUST_COMPONENT *pComponent = NULL;
	char str[MAX_LINE_LEN];
	bool ret = false;
	uint32_t i;

	// 打开 trust 镜像文件
	FileSrc = fopen(path, "rb");
	if (FileSrc == NULL) {
		printf("open %s failed!\n", path);
		goto end;
	}

	// 获取文件大小
	if (getFileSize(path, &FileSize) == false) {
		printf("File Size failed!\n");
		goto end;
	}
	printf("File Size = %d\n", FileSize);

	// 读取整个文件到缓冲区
	pBuf = (uint8_t *)malloc(FileSize);
	if (1 != fread(pBuf, FileSize, 1, FileSrc)) {
		printf("read input file failed!\n");
		goto end;
	}

	// 解析 Trust Header
	pHead = (TRUST_HEADER *)pBuf;

	// 打印头部标签（魔数）
	memcpy(str, &pHead->tag, 4);
	str[4] = '\0';
	printf("Header Tag:%s\n", str);
	printf("Header version:%d\n", pHead->version);
	printf("Header flag:%d\n", pHead->flags);

	// 提取组件数量和签名偏移
	SrcFileNum = (pHead->size >> 16) & 0xffff;  // 高 16 位：组件数量
	SignOffset = (pHead->size & 0xffff) << 2;   // 低 16 位：签名偏移（以 4 字节为单位）
	printf("SrcFileNum:%d\n", SrcFileNum);
	printf("SignOffset:%d\n", SignOffset);

	// 定位组件信息区和组件数据区
	pComponent = (TRUST_COMPONENT *)(pBuf + SignOffset + SIGNATURE_SIZE);
	pComponentData = (COMPONENT_DATA *)(pBuf + sizeof(TRUST_HEADER));

	// 遍历所有组件，提取并保存
	for (i = 0; i < SrcFileNum; i++) {
		printf("Component %d:\n", i);

		// 打印组件 ID
		memcpy(str, &pComponent->ComponentID, 4);
		str[4] = '\0';
		printf("ComponentID:%s\n", str);
		printf("StorageAddr:0x%x\n", pComponent->StorageAddr);
		printf("ImageSize:0x%x\n", pComponent->ImageSize);
		printf("LoadAddr:0x%x\n", pComponentData->LoadAddr);

		// 保存组件数据到文件（以组件 ID 作为文件名）
		saveDatatoFile(str, pBuf + (pComponent->StorageAddr << 9),
		               pComponent->ImageSize << 9);

		pComponentData++;
		pComponent++;
	}

	ret = true;
end:
	if (FileSrc)
		fclose(FileSrc);
	if (pBuf)
		free(pBuf);

	return ret;
}

/**
 * 打印帮助信息
 */
static void printHelp(void)
{
	printf("Usage: trust_merger [options]... FILE\n");
	printf(
	        "Merge or unpack Rockchip's trust image (Default action is to merge.)\n");
	printf("Options:\n");
	printf("\t" OPT_MERGE "\t\t\tMerge trust with specified config.\n");
	printf("\t" OPT_UNPACK "\t\tUnpack specified trust to current dir.\n");
	printf("\t" OPT_VERBOSE "\t\tDisplay more runtime informations.\n");
	printf("\t" OPT_HELP "\t\t\tDisplay this information.\n");
	printf("\t" OPT_VERSION "\t\tDisplay version information.\n");
	printf("\t" OPT_SUBFIX "\t\tSpec subfix.\n");
	printf("\t" OPT_REPLACE "\t\tReplace some part of binary path.\n");
	printf("\t" OPT_PREPATH "\t\tAdd prefix path of binary path.\n");
	printf("\t" OPT_RSA "\t\t\tRSA mode.\"--rsa [mode]\", [mode] can be: "
	       "0(none), 1(1024), 2(2048), 3(2048 pss).\n");
	printf("\t" OPT_SHA
	       "\t\t\tSHA mode.\"--sha [mode]\", [mode] can be: 0(none), 1(160), "
	       "2(256 RK big endian), 3(256 little endian).\n");
	printf("\t" OPT_SIZE "\t\t\tTrustImage size.\"--size [per image KB size] "
	       "[copy count]\", per image must be 64KB aligned\n");
}

/**
 * 主函数 - 程序入口
 * 解析命令行参数，执行合并或解包操作
 *
 * 使用示例：
 *   合并：trust_merger RK3399TRUST.ini
 *   解包：trust_merger --unpack trust.img
 *   指定 RSA/SHA：trust_merger --rsa 2 --sha 3 RK3399TRUST.ini
 *   指定镜像大小：trust_merger --size 2048 2 RK3399TRUST.ini
 *
 * @param argc 参数数量
 * @param argv 参数数组
 * @return 成功返回 0，失败返回 -1
 */
int main(int argc, char **argv)
{
	bool merge = true;          // 默认执行合并操作
	char *optPath = NULL;       // 配置文件路径或 trust 镜像路径
	int i;

	gConfigPath = NULL;

	// 解析命令行参数
	for (i = 1; i < argc; i++) {
		if (!strcmp(OPT_VERBOSE, argv[i])) {
			// 启用详细日志输出
			gDebug = true;
		} else if (!strcmp(OPT_HELP, argv[i])) {
			// 显示帮助信息
			printHelp();
			return 0;
		} else if (!strcmp(OPT_VERSION, argv[i])) {
			// 显示版本信息
			printf("trust_merger (cwz@rock-chips.com)\t" VERSION "\n");
			return 0;
		} else if (!strcmp(OPT_MERGE, argv[i])) {
			// 合并模式
			merge = true;
		} else if (!strcmp(OPT_UNPACK, argv[i])) {
			// 解包模式
			merge = false;
		} else if (!strcmp(OPT_SUBFIX, argv[i])) {
			// 启用子后缀标志
			gSubfix = true;
			printf("trust_merger: Spec subfix!\n");
		} else if (!strcmp(OPT_REPLACE, argv[i])) {
			// 路径替换：将 gLegacyPath 替换为 gNewPath
			i++;
			gLegacyPath = argv[i];
			i++;
			gNewPath = argv[i];
		} else if (!strcmp(OPT_PREPATH, argv[i])) {
			// 路径前缀：为所有路径添加前缀
			i++;
			gPrePath = argv[i];
		} else if (!strcmp(OPT_RSA, argv[i])) {
			// RSA 模式设置
			i++;
			if (!is_digit(*(argv[i]))) {
				printHelp();
				return -1;
			}
			gRSAmode = *(argv[i]) - '0';
			LOGD("rsa mode:%d\n", gRSAmode);
		} else if (!strcmp(OPT_SHA, argv[i])) {
			// SHA 模式设置
			i++;
			if (!is_digit(*(argv[i]))) {
				printHelp();
				return -1;
			}
			gSHAmode = *(argv[i]) - '0';
			LOGD("sha mode:%d\n", gSHAmode);
		} else if (!strcmp(OPT_SIZE, argv[i])) {
			/* 单个 trust 镜像的大小 */
			g_trust_max_size = strtoul(argv[++i], NULL, 10);
			/*
			 * 通常情况下，由于 preloader 每隔 512KB 检测一次，
			 * 镜像大小必须按 512KB 对齐。但某些产品对 flash 空间
			 * 有严格要求，我们必须使其小于 512KB。
			 * 这里要求至少按 64KB 对齐。
			 */
			if (g_trust_max_size % 64) {
				printHelp();
				return -1;
			}
			g_trust_max_size *= 1024; /* 转换为字节 */

			/* 备份副本数量 */
			g_trust_max_num = strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(OPT_IGNORE_BL32, argv[i])) {
			// 忽略 BL32 组件
			gIgnoreBL32 = true;
		} else {
			// 配置文件路径或 trust 镜像路径
			if (optPath) {
				fprintf(stderr, "only need one path arg, but we have:\n%s\n%s.\n",
				        optPath, argv[i]);
				printHelp();
				return -1;
			}
			optPath = argv[i];
		}
	}

	// 解包模式必须指定输出路径
	if (!merge && !optPath) {
		fprintf(stderr, "need set out path to unpack!\n");
		printHelp();
		return -1;
	}

	// 执行合并或解包操作
	if (merge) {
		LOGD("do_merge\n");
		gConfigPath = optPath;  // 配置文件路径
		if (!mergetrust()) {
			fprintf(stderr, "merge failed!\n");
			return -1;
		}
		printf("merge success(%s)\n", gOpts.outPath);
	} else {
		LOGD("do_unpack\n");
		if (!unpacktrust(optPath)) {
			fprintf(stderr, "unpack failed!\n");
			return -1;
		}
		printf("unpack success\n");
	}

	return 0;
}
