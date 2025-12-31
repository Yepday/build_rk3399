/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Rockchip Loader镜像打包工具 - 用于生成带Rockchip专用头的uboot.img和trust.img
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include "compiler.h"
#include <version.h>
#include "sha.h"
#include <u-boot/sha256.h>
#include <u-boot/crc.h>
#include <linux/sizes.h>
#include <linux/kconfig.h>
#include <config.h>

/* Rockchip专用CRC32计算函数声明 */
extern uint32_t crc32_rk(uint32_t, const unsigned char *, uint32_t);

/* 命令行参数定义 */
#define OPT_PACK "--pack"           // 打包模式
#define OPT_UNPACK "--unpack"       // 解包模式
#define OPT_UBOOT "--uboot"         // U-Boot镜像类型
#define OPT_TRUSTOS "--trustos"     // TrustOS镜像类型
#define OPT_SIZE "--size"           // 指定镜像大小
#define OPT_VERSION "--version"     // 指定版本号
#define OPT_INFO "--info"           // 显示镜像信息模式
#define OPT_PREPATH             "--prepath"  // 输入文件路径前缀

/* 工作模式定义 */
#define MODE_PACK 0      // 打包模式：将原始bin文件添加Rockchip头生成.img
#define MODE_UNPACK 1    // 解包模式：从.img文件中提取原始bin
#define MODE_INFO 2      // 信息模式：显示.img文件的头部信息
#define CONFIG_SECUREBOOT_SHA256  // 使用SHA256哈希算法（而非SHA1）

/* 镜像类型定义 */
#define IMAGE_UBOOT 0    // U-Boot bootloader镜像
#define IMAGE_TRUST 1    // Trust OS (ARM Trusted Firmware) 镜像

/* Rockchip镜像头部常量 */
#define LOADER_MAGIC_SIZE 8   // 魔数字段长度（"LOADER  "或"TOS     "）
#define LOADER_HASH_SIZE 32   // SHA256哈希值长度（32字节）

/* U-Boot镜像配置参数 */
#define UBOOT_NAME "uboot"
#ifdef CONFIG_RK_NVME_BOOT_EN
#define UBOOT_NUM 2             // NVME启动时的备份副本数量（减少以节省空间）
#define UBOOT_MAX_SIZE 512 * 1024   // 单个副本最大512KB
#else
#define UBOOT_NUM 4             // 标准配置的备份副本数量（4个副本提高可靠性）
#define UBOOT_MAX_SIZE 1024 * 1024  // 单个副本最大1MB
#endif

/* U-Boot版本字符串：由编译时宏自动生成 */
#define UBOOT_VERSION_STRING                                                   \
  U_BOOT_VERSION " (" U_BOOT_DATE " - " U_BOOT_TIME ")" CONFIG_IDENT_STRING

#define RK_UBOOT_MAGIC "LOADER  "  // U-Boot镜像魔数（8字节，末尾有空格）
#define RK_UBOOT_RUNNING_ADDR CONFIG_SYS_TEXT_BASE  // U-Boot加载到内存的运行地址

/* Trust OS (ATF + OP-TEE) 镜像配置参数 */
#define TRUST_NAME "trustos"
#define TRUST_NUM 4              // Trust镜像备份副本数量
#define TRUST_MAX_SIZE 1024 * 1024   // 单个副本最大1MB
#define TRUST_VERSION_STRING "Trust os"

#define RK_TRUST_MAGIC "TOS     "   // Trust镜像魔数（8字节，末尾有空格）
/* Trust OS运行地址 = U-Boot地址 + 128MB + 4MB（避免地址冲突） */
#define RK_TRUST_RUNNING_ADDR (CONFIG_SYS_TEXT_BASE + SZ_128M + SZ_4M)

/**
 * Rockchip Second Stage Loader 镜像头结构（总大小2048字节）
 * 该头部会被添加到u-boot.bin或trust.bin之前，使BootROM能够识别并加载
 */
typedef struct tag_second_loader_hdr {
	/* 基础信息区（32字节） */
	uint8_t magic[LOADER_MAGIC_SIZE]; /* 魔数：LOADER或TOS（用于识别镜像类型） */
	uint32_t version;          /* 版本号：用于Rollback保护（防回滚攻击） */
	uint32_t reserved0;        /* 保留字段 */
	uint32_t loader_load_addr; /* 物理加载地址：BootROM将镜像加载到此DRAM地址 */
	uint32_t loader_load_size; /* 镜像大小（字节）：实际二进制代码的长度 */

	/* 校验信息区（32字节） */
	uint32_t crc32;            /* CRC32校验值：用于快速数据完整性验证 */
	uint32_t hash_len;         /* 哈希长度：20(SHA1)或32(SHA256)，0表示无哈希 */
	uint8_t hash[LOADER_HASH_SIZE]; /* SHA哈希值：用于安全启动验证 */

	/* 填充区（960字节） */
	uint8_t reserved[1024 - 32 - 32];  /* 对齐到1024字节 */

	/* RSA签名区（264字节） */
	uint32_t signTag;          /* 签名标记：0x4E474953 ("SIGN"的小端表示) */
	uint32_t signlen;          /* RSA签名长度：128(RSA1024)或256(RSA2048) */
	uint8_t rsaHash[256];      /* RSA签名数据：使用私钥对hash的签名 */

	/* 尾部填充（760字节） */
	uint8_t reserved2[2048 - 1024 - 256 - 8];  /* 填充至2048字节对齐 */
} second_loader_hdr;

/**
 * 打印工具使用帮助信息
 * @param prog 程序名称
 */
void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--pack|--unpack] [--uboot|--trustos]\
		file_in "
	        "file_out [load_addr]  [--size] [size number]\
		[--version] "
	        "[version] | [--info] [file]\n",
	        prog);
}

/**
 * 将十六进制字符串转换为无符号整数
 * 支持格式：0x12345678, 0X12345678, x12345678, 12345678
 * @param str 输入的十六进制字符串
 * @return 转换后的整数值
 */
unsigned int str2hex(char *str)
{
	int i = 0;
	unsigned int value = 0;

	/* 跳过"0x"或"0X"前缀 */
	if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X'))
		str += 2;
	/* 跳过单独的"x"或"X"前缀 */
	if (*str == 'x' || *str == 'X')
		str += 1;

	/* 逐字符解析十六进制 */
	for (i = 0; *str != '\0'; i++, ++str) {
		if (*str >= '0' && *str <= '9')
			value = value * 16 + *str - '0';      /* 处理数字0-9 */
		else if (*str >= 'a' && *str <= 'f')
			value = value * 16 + *str - 'a' + 10; /* 处理小写a-f */
		else if (*str >= 'A' && *str <= 'F')
			value = value * 16 + *str - 'A' + 10; /* 处理大写A-F */
		else
			break;  /* 遇到非十六进制字符，停止解析 */
	}
	return value;
}

/**
 * 主函数：Rockchip镜像打包/解包/信息查询工具
 * 功能：
 *  1. 打包模式：将u-boot.bin或trust.bin添加Rockchip头生成.img
 *  2. 解包模式：从.img文件中提取原始bin文件
 *  3. 信息模式：显示.img文件的头部信息（版本、加载地址等）
 */
int main(int argc, char *argv[])
{
	/* 工作参数 */
	int mode = -1, image = -1;         /* 工作模式和镜像类型 */
	int max_size, max_num;             /* 单个副本最大尺寸、副本数量 */
	int size, i;                       /* 文件大小、循环计数器 */
	uint32_t loader_addr, in_loader_addr = -1; /* 加载地址 */
	char *magic, *version, *name;      /* 魔数、版本字符串、名称 */
	FILE *fi, *fo;                     /* 输入输出文件句柄 */
	second_loader_hdr hdr;             /* Rockchip镜像头结构 */
	char *buf = 0;                     /* 数据缓冲区 */
	uint32_t in_size = 0, in_num = 0;  /* 用户指定的大小和副本数 */
	char *file_in = NULL, *file_out = NULL; /* 输入输出文件路径 */
	char			*prepath = NULL;      /* 输入文件路径前缀 */
	char			file_name[1024];      /* 完整文件名缓冲区 */
	uint32_t curr_version = 0;         /* 用户指定的版本号 */

	/* 参数数量检查 */
	if (argc < 3) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	/* 命令行参数解析 */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], OPT_PACK)) {
			/* 打包模式：bin → img */
			mode = MODE_PACK;
		} else if (!strcmp(argv[i], OPT_UNPACK)) {
			/* 解包模式：img → bin */
			mode = MODE_UNPACK;
		} else if (!strcmp(argv[i], OPT_UBOOT)) {
			/* U-Boot镜像类型 */
			image = IMAGE_UBOOT;
			file_in = argv[++i];   /* 输入：u-boot.bin */
			file_out = argv[++i];  /* 输出：uboot.img */
			/* 检测是否提供了自定义加载地址（下一个参数不是"--"开头） */
			if ((argv[i + 1]) && (strncmp(argv[i + 1], "--", 2)))
				in_loader_addr = str2hex(argv[++i]);
		} else if (!strcmp(argv[i], OPT_TRUSTOS)) {
			/* Trust OS镜像类型 */
			image = IMAGE_TRUST;
			file_in = argv[++i];   /* 输入：trust.bin */
			file_out = argv[++i];  /* 输出：trust.img */
			/* 检测是否提供了自定义加载地址 */
			if ((argv[i + 1]) && (strncmp(argv[i + 1], "--", 2)))
				in_loader_addr = str2hex(argv[++i]);
		} else if (!strcmp(argv[i], OPT_SIZE)) {
			/* 自定义镜像大小和副本数量 */
			in_size = strtoul(argv[++i], NULL, 10); /* 单位：KB */
			/*
			 * 通常要求512KB对齐（preloader每512KB扫描一次）
			 * 但某些产品有严格的flash空间限制，允许小于512KB
			 * 最小对齐单位：64KB
			 */
			if (in_size % 64) {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			in_size *= 1024;  /* 转换为字节 */

			in_num = strtoul(argv[++i], NULL, 10); /* 副本数量 */
		} else if (!strcmp(argv[i], OPT_VERSION)) {
			/* 指定版本号（用于Rollback保护） */
			curr_version = strtoul(argv[++i], NULL, 10);
			printf("curr_version = 0x%x\n", curr_version);
		} else if (!strcmp(argv[i], OPT_INFO)) {
			/* 信息查询模式：显示.img头部信息 */
			mode = MODE_INFO;
			file_in = argv[++i];
		} else if (!strcmp(argv[i], OPT_PREPATH)) {
			/* 输入文件路径前缀 */
			prepath = argv[++i];
		} else {
			/* 未识别的参数 */
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	/* 根据镜像类型配置参数 */
	if (image == IMAGE_UBOOT) {
		/* U-Boot镜像配置 */
		name = UBOOT_NAME;           /* 名称：uboot */
		magic = RK_UBOOT_MAGIC;      /* 魔数：LOADER   */
		version = UBOOT_VERSION_STRING; /* 版本字符串 */
		/* 使用用户指定值或默认值 */
		max_size = in_size ? in_size : UBOOT_MAX_SIZE;
		max_num = in_num ? in_num : UBOOT_NUM;
		/* 加载地址：用户指定值或默认CONFIG_SYS_TEXT_BASE */
		loader_addr =
		        (in_loader_addr == -1) ? RK_UBOOT_RUNNING_ADDR : in_loader_addr;
	} else if (image == IMAGE_TRUST) {
		/* Trust OS镜像配置 */
		name = TRUST_NAME;           /* 名称：trustos */
		magic = RK_TRUST_MAGIC;      /* 魔数：TOS      */
		version = TRUST_VERSION_STRING; /* 版本字符串 */
		/* 使用用户指定值或默认值 */
		max_size = in_size ? in_size : TRUST_MAX_SIZE;
		max_num = in_num ? in_num : TRUST_NUM;
		/* 加载地址：默认在U-Boot地址 + 128MB + 4MB */
		loader_addr =
		        (in_loader_addr == -1) ? RK_TRUST_RUNNING_ADDR : in_loader_addr;
	} else if (mode == MODE_INFO) {
		/* 信息模式不需要配置这些参数 */
	} else {
		exit(EXIT_FAILURE);
	}

	/* ==================== 打包模式 ==================== */
	if (mode == MODE_PACK) {
		/* 分配缓冲区：max_size * max_num（为所有副本分配空间） */
		buf = calloc(max_size, max_num);
		if (!buf) {
			perror(file_out);
			exit(EXIT_FAILURE);
		}
		printf("\n load addr is 0x%x!\n", loader_addr);

		/* 如果提供了路径前缀，则将其添加到文件名前 */
		if (prepath && strncmp(prepath, file_in, strlen(prepath))) {
			strcpy(file_name, prepath);
			strcat(file_name, file_in);
			file_in = file_name;
		}

		/* 检查输入输出文件名是否有效 */
		if (!file_in || !file_out) {
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}

		/* 打开输入文件（原始bin文件） */
		fi = fopen(file_in, "rb");
		if (!fi) {
			perror(file_in);
			exit(EXIT_FAILURE);
		}

		/* 创建输出文件（.img文件） */
		fo = fopen(file_out, "wb");
		if (!fo) {
			perror(file_out);
			exit(EXIT_FAILURE);
		}

		/* 获取输入文件大小 */
		printf("pack input %s \n", file_in);
		fseek(fi, 0, SEEK_END);  /* 移动到文件末尾 */
		size = ftell(fi);        /* 获取文件大小 */
		fseek(fi, 0, SEEK_SET);  /* 回到文件开头 */
		printf("pack file size: %d(%d KB)\n", size, size / 1024);

		/* 检查文件大小是否超过限制（需要留出头部空间） */
		if (size > max_size - sizeof(second_loader_hdr)) {
			perror(file_out);
			exit(EXIT_FAILURE);
		}
		/* 初始化Rockchip头部结构 */
		memset(&hdr, 0, sizeof(second_loader_hdr));
		memcpy((char *)hdr.magic, magic, LOADER_MAGIC_SIZE); /* 设置魔数 */
		hdr.version = curr_version;        /* 设置版本号 */
		hdr.loader_load_addr = loader_addr; /* 设置加载地址 */

		/* 读取原始bin文件到缓冲区（跳过头部大小，为头部留出空间） */
		if (!fread(buf + sizeof(second_loader_hdr), size, 1, fi))
			exit(EXIT_FAILURE);

		/* 将大小对齐到4字节（Rockchip硬件加密引擎要求4字节对齐） */
		size = (((size + 3) >> 2) << 2);
		hdr.loader_load_size = size;

		/* 计算CRC32校验值（对实际数据进行校验，不包括头部） */
		hdr.crc32 = crc32_rk(
		                    0, (const unsigned char *)buf + sizeof(second_loader_hdr), size);
		printf("crc = 0x%08x\n", hdr.crc32);

		/* ==================== 计算哈希值（用于安全启动验证） ==================== */
#ifndef CONFIG_SECUREBOOT_SHA256
		/* 使用SHA1算法（20字节哈希） */
		SHA_CTX ctx;
		uint8_t *sha;
		hdr.hash_len = (SHA_DIGEST_SIZE > LOADER_HASH_SIZE) ? LOADER_HASH_SIZE
		               : SHA_DIGEST_SIZE;
		SHA_init(&ctx);
		SHA_update(&ctx, buf + sizeof(second_loader_hdr), size); /* 对数据计算哈希 */
		if (hdr.version > 0)
			SHA_update(&ctx, (void *)&hdr.version, 8); /* 包含版本号（防回滚） */

		SHA_update(&ctx, &hdr.loader_load_addr, sizeof(hdr.loader_load_addr));
		SHA_update(&ctx, &hdr.loader_load_size, sizeof(hdr.loader_load_size));
		SHA_update(&ctx, &hdr.hash_len, sizeof(hdr.hash_len));
		sha = (uint8_t *)SHA_final(&ctx);
		memcpy(hdr.hash, sha, hdr.hash_len);
#else
		/* 使用SHA256算法（32字节哈希，更安全） */
		sha256_context ctx;
		uint8_t hash[LOADER_HASH_SIZE];

		memset(hash, 0, LOADER_HASH_SIZE);

		hdr.hash_len = 32; /* SHA256输出32字节 */
		sha256_starts(&ctx);
		/* 依次对以下数据计算SHA256哈希： */
		sha256_update(&ctx, (void *)buf + sizeof(second_loader_hdr), size); /* 1. 镜像数据 */
		if (hdr.version > 0)
			sha256_update(&ctx, (void *)&hdr.version, 8); /* 2. 版本号（防回滚攻击） */

		sha256_update(&ctx, (void *)&hdr.loader_load_addr,
		              sizeof(hdr.loader_load_addr)); /* 3. 加载地址 */
		sha256_update(&ctx, (void *)&hdr.loader_load_size,
		              sizeof(hdr.loader_load_size)); /* 4. 数据大小 */
		sha256_update(&ctx, (void *)&hdr.hash_len, sizeof(hdr.hash_len)); /* 5. 哈希长度 */
		sha256_finish(&ctx, hash);
		memcpy(hdr.hash, hash, hdr.hash_len);
#endif /* CONFIG_SECUREBOOT_SHA256 */

		/* 显示版本信息 */
		printf("%s version: %s\n", name, version);

		/* 将头部复制到缓冲区开头 */
		memcpy(buf, &hdr, sizeof(second_loader_hdr));

		/* 将完整镜像（头部+数据）写入多个副本（提高可靠性） */
		for (i = 0; i < max_num; i++)
			fwrite(buf, max_size, 1, fo);

		printf("pack %s success! \n", file_out);
		fclose(fi);
		fclose(fo);
	/* ==================== 解包模式 ==================== */
	} else if (mode == MODE_UNPACK) {
		/* 分配缓冲区 */
		buf = calloc(max_size, max_num);
		if (!buf) {
			perror(file_out);
			exit(EXIT_FAILURE);
		}
		/* 检查文件名 */
		if (!file_in || !file_out) {
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}

		/* 打开输入文件（.img文件） */
		fi = fopen(file_in, "rb");
		if (!fi) {
			perror(file_in);
			exit(EXIT_FAILURE);
		}

		/* 创建输出文件（原始bin文件） */
		fo = fopen(file_out, "wb");
		if (!fo) {
			perror(file_out);
			exit(EXIT_FAILURE);
		}

		printf("unpack input %s \n", file_in);

		/* 读取Rockchip头部 */
		memset(&hdr, 0, sizeof(second_loader_hdr));
		if (!fread(&hdr, sizeof(second_loader_hdr), 1, fi))
			exit(EXIT_FAILURE);

		/* 读取实际数据部分（根据头部中的大小字段） */
		if (!fread(buf, hdr.loader_load_size, 1, fi))
			exit(EXIT_FAILURE);

		/* 将原始数据写入输出文件（不包括Rockchip头部） */
		fwrite(buf, hdr.loader_load_size, 1, fo);
		printf("unpack %s success! \n", file_out);
		fclose(fi);
		fclose(fo);
	/* ==================== 信息查询模式 ==================== */
	} else if (mode == MODE_INFO) {
		second_loader_hdr *hdr;

		/* 分配头部结构内存 */
		hdr = malloc(sizeof(struct tag_second_loader_hdr));
		if (hdr == NULL) {
			printf("Memory error!\n");
			exit(EXIT_FAILURE);
		}

		/* 打开输入文件 */
		fi = fopen(file_in, "rb");
		if (!fi) {
			perror(file_in);
			exit(EXIT_FAILURE);
		}

		/* 读取头部信息 */
		if (!fread(hdr, sizeof(struct tag_second_loader_hdr), 1, fi))
			exit(EXIT_FAILURE);

		/* 验证魔数并显示信息 */
		if (!(memcmp(RK_UBOOT_MAGIC, hdr->magic, 5)) ||      /* 检查"LOADER" */
		    !(memcmp(RK_TRUST_MAGIC, hdr->magic, 3))) {      /* 或"TOS" */
			printf("The image info:\n");
			printf("Rollback index is %d\n", hdr->version);         /* 版本号（防回滚） */
			printf("Load Addr is 0x%x\n", hdr->loader_load_addr);   /* 加载地址 */
		} else {
			printf("Please input the correct file.\n");
		}

		fclose(fi);
		free(hdr);
	}
	free(buf);

	return 0;
}
