#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backend/generator.py - 代码生成引擎（模块三：模板引擎，DEMO 版）

职责：根据前端选定的框架与配置参数，生成一份"可移植库文件包"：
  - <lib>.c / <lib>.h      真实可用的存储框架源码（从 frameworks/ 复制）
  - test_main.c            标准自检入口（对接模拟基座 flash_sim.h）
  - PORTING.md             移植说明（如何把 flash_sim_* 替换为真实 MCU 驱动）
  - manifest.json          包元信息（供导入校验识别"是否符合模拟基座要求"）

输出为 zip 字节流，由 server 直接回传给前端下载。

设计原则：仅依赖标准库 zipfile，无需联网/第三方模板引擎。
"""

import io
import json
import os
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# 各框架的生成配方：从真实源码复制 + 生成配套文件
# base 指向 frameworks/ 下的真实实现目录
RECIPES = {
    "kv": {
        "lib_name": "kv_store",
        "title": "KV/NVS 存储框架",
        "src_dir": "frameworks/kv",
        "copy_files": ["kv_store.c", "kv_store.h"],
        "desc": "键值存储，两步提交掉电安全、CRC 校验、压实垃圾回收。",
    },
    "simulator": {
        "lib_name": "flash_sim",
        "title": "Flash 模拟基座",
        "src_dir": "simulator",
        "copy_files": ["flash_sim.c", "flash_sim.h"],
        "desc": "Flash 物理特性模拟库，提供统一 read/write/erase 接口。",
    },
    "easyflash": {
        "lib_name": "easyflash",
        "title": "EasyFlash (ENV/KV 开源组件)",
        "desc": "成熟开源 KV 框架，内置磨损均衡、掉电保护与垃圾回收。",
        "src_dir": "frameworks/easyflash",
        # 需要打进导出包的源文件（相对 ROOT）
        "copy_files": [
            "frameworks/easyflash/ef_port.c",
            "frameworks/easyflash/ef_port.h",
            "frameworks/easyflash/ef_cfg.h",
            "frameworks/easyflash/vendor/src/ef_env.c",
            "frameworks/easyflash/vendor/src/ef_utils.c",
            "frameworks/easyflash/vendor/src/easyflash.c",
            "frameworks/easyflash/vendor/inc/easyflash.h",
            "frameworks/easyflash/vendor/inc/ef_def.h",
        ],
        # 自检入口（保留框架自带 main，重命名为 test_main.c）
        "test_entry": "frameworks/easyflash/test/main_easyflash.c",
        "extra_includes": ["frameworks/easyflash/vendor/inc"],
        "requires": "flash_sim",
    },
    "flashdb": {
        "lib_name": "flashdb",
        "title": "FlashDB (KVDB 开源组件)",
        "desc": "EasyFlash 作者的下一代 KV 框架，具备磨损均衡、掉电保护与 GC。",
        "src_dir": "frameworks/flashdb",
        "copy_files": [
            "frameworks/flashdb/fal_flash_sim_port.c",
            "frameworks/flashdb/fal_flash_sim_port.h",
            "frameworks/flashdb/fdb_cfg.h",
            "frameworks/flashdb/fal_cfg.h",
            "frameworks/flashdb/vendor/src/fdb.c",
            "frameworks/flashdb/vendor/src/fdb_utils.c",
            "frameworks/flashdb/vendor/src/fdb_kvdb.c",
            "frameworks/flashdb/vendor/src/fdb_tsdb.c",
            "frameworks/flashdb/vendor/src/fdb_file.c",
            "frameworks/flashdb/vendor/inc/flashdb.h",
            "frameworks/flashdb/vendor/inc/fdb_def.h",
            "frameworks/flashdb/vendor/inc/fdb_low_lvl.h",
            "frameworks/flashdb/vendor/fal/src/fal.c",
            "frameworks/flashdb/vendor/fal/src/fal_flash.c",
            "frameworks/flashdb/vendor/fal/src/fal_partition.c",
            "frameworks/flashdb/vendor/fal/inc/fal.h",
            "frameworks/flashdb/vendor/fal/inc/fal_def.h",
        ],
        "test_entry": "frameworks/flashdb/test/main_flashdb.c",
        "extra_includes": [
            "frameworks/flashdb/vendor/inc",
            "frameworks/flashdb/vendor/fal/inc",
        ],
        "requires": "flash_sim",
    },
    "baremetal": {
        "lib_name": "bm_config",
        "title": "裸机结构体配置 (A/B 双备份 + CRC)",
        "desc": "最简单的裸机架构：结构体整块映射，A/B 双备份 + CRC32 + 单调序号掉电恢复。",
        "src_dir": "frameworks/baremetal",
        "copy_files": [
            "frameworks/baremetal/bm_config.c",
            "frameworks/baremetal/bm_config.h",
        ],
        "test_entry": "frameworks/baremetal/test/main_baremetal.c",
        "extra_includes": [],
        "requires": "flash_sim",
    },
    "fs": {
        "lib_name": "fs_store",
        "title": "自研文件系统 (fs_store)",
        "desc": "简易块式文件系统：文件分配表 + 数据块，多文件读写/频繁修改/追加/删除/查询。",
        "src_dir": "frameworks/fs",
        "copy_files": [
            "frameworks/fs/fs_store.c",
            "frameworks/fs/fs_store.h",
        ],
        "test_entry": "frameworks/fs/test/main_fs.c",
        "extra_includes": [],
        "requires": "flash_sim",
    },
    "littlefs": {
        "lib_name": "littlefs",
        "title": "LittleFS (开源文件系统)",
        "desc": "littlefs-project/littlefs v2.x：嵌入式掉电安全文件系统，含磨损均衡。",
        "src_dir": "frameworks/littlefs",
        "copy_files": [
            "frameworks/littlefs/littlefs_sim_port.c",
            "frameworks/littlefs/littlefs_sim_port.h",
            "frameworks/littlefs/vendor/lfs.c",
            "frameworks/littlefs/vendor/lfs.h",
            "frameworks/littlefs/vendor/lfs_util.c",
            "frameworks/littlefs/vendor/lfs_util.h",
        ],
        "test_entry": "frameworks/littlefs/test/main_littlefs.c",
        "extra_includes": [],
        "requires": "flash_sim",
    },
    "fatfs": {
        "lib_name": "fatfs",
        "title": "FatFs (开源文件系统)",
        "desc": "ChaN/FatFs R0.16：通用 FAT 文件系统模块，经移植层对接模拟基座。",
        "src_dir": "frameworks/fatfs",
        "copy_files": [
            "frameworks/fatfs/fatfs_sim_port.c",
            "frameworks/fatfs/fatfs_sim_port.h",
            "frameworks/fatfs/vendor/ff.c",
            "frameworks/fatfs/vendor/ff.h",
            "frameworks/fatfs/vendor/ffconf.h",
            "frameworks/fatfs/vendor/ffsystem.c",
            "frameworks/fatfs/vendor/ffunicode.c",
            "frameworks/fatfs/vendor/diskio.h",
        ],
        "test_entry": "frameworks/fatfs/test/main_fatfs.c",
        "extra_includes": [],
        "requires": "flash_sim",
    },
    "spiffs": {
        "lib_name": "spiffs",
        "title": "SPIFFS (开源文件系统)",
        "desc": "pellepl/SPIFFS：面向 SPI NOR Flash 的小型掉电安全文件系统。",
        "src_dir": "frameworks/spiffs",
        "copy_files": [
            "frameworks/spiffs/spiffs_sim_port.c",
            "frameworks/spiffs/spiffs_sim_port.h",
            "frameworks/spiffs/spiffs_config.h",
            "frameworks/spiffs/vendor/spiffs_nucleus.c",
            "frameworks/spiffs/vendor/spiffs_nucleus.h",
            "frameworks/spiffs/vendor/spiffs_hydrogen.c",
            "frameworks/spiffs/vendor/spiffs_gc.c",
            "frameworks/spiffs/vendor/spiffs_check.c",
            "frameworks/spiffs/vendor/spiffs_cache.c",
            "frameworks/spiffs/vendor/spiffs.h",
        ],
        "test_entry": "frameworks/spiffs/test/main_spiffs.c",
        "extra_includes": [],
        "requires": "flash_sim",
    },
    "yaffs": {
        "lib_name": "yaffs",
        "title": "YAFFS (开源文件系统)",
        "desc": "YAFFS2 Direct：面向 NAND Flash 的日志型文件系统（GPL v2）。",
        "src_dir": "frameworks/yaffs",
        "copy_files": [
            "frameworks/yaffs/yaffs_sim_port.c",
            "frameworks/yaffs/yaffs_sim_port.h",
            "frameworks/yaffs/yaffs_host_types.h",
            "frameworks/yaffs/vendor/yaffsfs.c",
            "frameworks/yaffs/vendor/yaffsfs.h",
            "frameworks/yaffs/vendor/yaffs_guts.c",
            "frameworks/yaffs/vendor/yaffs_guts.h",
            "frameworks/yaffs/vendor/yaffs_allocator.c",
            "frameworks/yaffs/vendor/yaffs_tagscompat.c",
            "frameworks/yaffs/vendor/yaffs_tagsmarshall.c",
            "frameworks/yaffs/vendor/yaffs_nand.c",
            "frameworks/yaffs/vendor/yaffs_checkptrw.c",
            "frameworks/yaffs/vendor/yaffs_packedtags1.c",
            "frameworks/yaffs/vendor/yaffs_packedtags2.c",
            "frameworks/yaffs/vendor/yaffs_bitmap.c",
            "frameworks/yaffs/vendor/yaffs_verify.c",
            "frameworks/yaffs/vendor/yaffs_nameval.c",
            "frameworks/yaffs/vendor/yaffs_attribs.c",
            "frameworks/yaffs/vendor/yaffs_yaffs1.c",
            "frameworks/yaffs/vendor/yaffs_yaffs2.c",
            "frameworks/yaffs/vendor/yaffs_ecc.c",
            "frameworks/yaffs/vendor/yaffs_hweight.c",
            "frameworks/yaffs/vendor/yaffs_summary.c",
            "frameworks/yaffs/vendor/yaffs_endian.c",
            "frameworks/yaffs/vendor/yaffs_error.c",
            "frameworks/yaffs/vendor/yaffs_allocator.h",
            "frameworks/yaffs/vendor/yaffs_attribs.h",
            "frameworks/yaffs/vendor/yaffs_bitmap.h",
            "frameworks/yaffs/vendor/yaffs_checkptrw.h",
            "frameworks/yaffs/vendor/yaffs_ecc.h",
            "frameworks/yaffs/vendor/yaffs_endian.h",
            "frameworks/yaffs/vendor/yaffs_flashif.h",
            "frameworks/yaffs/vendor/yaffs_flashif2.h",
            "frameworks/yaffs/vendor/yaffs_getblockinfo.h",
            "frameworks/yaffs/vendor/yaffs_hweight.h",
            "frameworks/yaffs/vendor/yaffs_list.h",
            "frameworks/yaffs/vendor/yaffs_nameval.h",
            "frameworks/yaffs/vendor/yaffs_nand.h",
            "frameworks/yaffs/vendor/yaffs_nandemul2k.h",
            "frameworks/yaffs/vendor/yaffs_osglue.h",
            "frameworks/yaffs/vendor/yaffs_packedtags1.h",
            "frameworks/yaffs/vendor/yaffs_packedtags2.h",
            "frameworks/yaffs/vendor/yaffs_summary.h",
            "frameworks/yaffs/vendor/yaffs_tagscompat.h",
            "frameworks/yaffs/vendor/yaffs_tagsmarshall.h",
            "frameworks/yaffs/vendor/yaffs_trace.h",
            "frameworks/yaffs/vendor/yaffs_verify.h",
            "frameworks/yaffs/vendor/yaffs_yaffs1.h",
            "frameworks/yaffs/vendor/yaffs_yaffs2.h",
            "frameworks/yaffs/vendor/yaffscfg.h",
            "frameworks/yaffs/vendor/ydirectenv.h",
            "frameworks/yaffs/vendor/yportenv.h",
        ],
        "test_entry": "frameworks/yaffs/test/main_yaffs.c",
        "extra_includes": [],
        "cflags": [
            "-DCONFIG_YAFFS_DIRECT",
            "-DCONFIG_YAFFS_DEFINES_TYPES",
            "-DCONFIG_YAFFS_PROVIDE_DEFS",
            "-DCONFIG_YAFFSFS_PROVIDE_VALUES",
            "-include",
            "frameworks/yaffs/yaffs_host_types.h",
        ],
        "requires": "flash_sim",
    },
    # ---- Zephyr 存储组件：共享 zephyr_compat 兼容层，导出时整体打包 ----
    "fcb": {
        "lib_name": "zephyr_fcb",
        "title": "Zephyr FCB (闪存环形缓冲)",
        "desc": "Zephyr Flash Circular Buffer：append-only 环形日志 + 回卷覆盖 + CRC + 掉电恢复。",
        "src_dir": "frameworks/fcb",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/fcb/vendor/fcb.c",
            "frameworks/fcb/vendor/fcb_append.c",
            "frameworks/fcb/vendor/fcb_elem_info.c",
            "frameworks/fcb/vendor/fcb_getnext.c",
            "frameworks/fcb/vendor/fcb_rotate.c",
            "frameworks/fcb/vendor/fcb_walk.c",
            "frameworks/fcb/vendor/fcb_priv.h",
            "frameworks/fcb/vendor/include/zephyr/fs/fcb.h",
        ],
        "test_entry": "frameworks/fcb/test/main_fcb.c",
        "extra_includes": [
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/fcb/vendor/include",
        ],
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
    },
    "nvs": {
        "lib_name": "zephyr_nvs",
        "title": "Zephyr NVS (键值存储)",
        "desc": "Zephyr Non-Volatile Storage：ID+数据键值存储，ATE 日志 + 掉电安全 + GC。",
        "src_dir": "frameworks/nvs",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/nvs/vendor/nvs.c",
            "frameworks/nvs/vendor/nvs_priv.h",
            "frameworks/nvs/vendor/include/zephyr/kvss/nvs.h",
        ],
        "test_entry": "frameworks/nvs/test/main_nvs.c",
        "extra_includes": [
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/nvs/vendor/include",
        ],
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
    },
    "zms": {
        "lib_name": "zephyr_zms",
        "title": "Zephyr ZMS (固定槽位键值存储)",
        "desc": "Zephyr Memory Storage：固定大小槽位键值存储，磨损均衡 + 掉电安全。",
        "src_dir": "frameworks/zms",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/zms/vendor/zms.c",
            "frameworks/zms/vendor/zms_priv.h",
            "frameworks/zms/vendor/include/zephyr/kvss/zms.h",
        ],
        "test_entry": "frameworks/zms/test/main_zms.c",
        "extra_includes": [
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/zms/vendor/include",
        ],
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
    },
    # ---- TYM Setting（Tymphany 厂商组件）：ID 静态表 + RAM 镜像 + 整页回写 ----
    "tym_setting": {
        "lib_name": "tym_setting",
        "title": "TYM Setting (ID静态表/RAM镜像)",
        "desc": "Tymphany Setting：编译期固定 ID→地址静态映射，RAM 全镜像 + 延时批量整页回写。",
        "src_dir": "frameworks/tym_setting",
        "copy_files": [
            "frameworks/tym_setting/tym_setting_sim_port.c",
            "frameworks/tym_setting/tym_setting_sim_port.h",
            "frameworks/tym_setting/vendor/src/app_setting_idle_activity.c",
            "frameworks/tym_setting/vendor/src/StorageDrv.c",
            "frameworks/tym_setting/vendor/inc/app_setting_idle_activity.h",
            "frameworks/tym_setting/vendor/inc/SettingSrv_priv.h",
            "frameworks/tym_setting/vendor/inc/StorageDrv.h",
            "frameworks/tym_setting/vendor/inc/StorageDrv_priv.h",
            "frameworks/tym_setting/vendor/inc/NvmDrv.h",
            "frameworks/tym_setting/vendor/inc/NvmDrv_priv.h",
            "frameworks/tym_setting/config/setting_id.h",
            "frameworks/tym_setting/config/SettingSrv.config",
            "frameworks/tym_setting/compat/cplus.h",
            "frameworks/tym_setting/compat/commonTypes.h",
            "frameworks/tym_setting/compat/tym_setting_log.h",
        ],
        "test_entry": "frameworks/tym_setting/test/main_tym_setting.c",
        "extra_includes": [
            "frameworks/tym_setting",
            "frameworks/tym_setting/vendor/inc",
            "frameworks/tym_setting/config",
            "frameworks/tym_setting/compat",
        ],
        "cflags": [],
        "requires": "flash_sim",
    },
}


def _render_test_main(fw_id, recipe, params):
    """生成参数化的标准自检入口 test_main.c（用占位符 replace，避免 % 冲突）。"""
    capacity = int(params.get("capacity", 8192))
    page_size = int(params.get("page_size", 256))
    erase_size = int(params.get("erase_size", 4096))
    lib = recipe["lib_name"]
    title = recipe["title"]
    if fw_id == "kv":
        tpl = (
            "/* 自动生成的自检入口：对接模拟基座验证 __TITLE__ 库 */\n"
            "#include \"flash_sim.h\"\n"
            "#include \"__LIB__.h\"\n"
            "#include <stdio.h>\n"
            "#include <string.h>\n\n"
            "#define KV_CAPACITY __CAPACITY__\n"
            "#define KV_ERASE_SIZE __ERASE_SIZE__\n"
            "#define BIN_PATH \"imported_kv_demo.bin\"\n\n"
            "static int g_fail = 0;\n"
            "static void expect(const char *name, int cond) {\n"
            "    printf(\"  [%s] %s\\n\", cond ? \"OK  \" : \"FAIL\", name);\n"
            "    if (!cond) g_fail++;\n"
            "}\n\n"
            "int main(void) {\n"
            "    printf(\"=== 导入库运行验证: __TITLE__ (cap=__CAPACITY__) ===\\n\");\n"
            "    flash_config_t cfg = {\n"
            "        .type = FLASH_TYPE_NOR, .total_size = 64*1024,\n"
            "        .erase_size = KV_ERASE_SIZE, .write_size = 1, .read_size = 1,\n"
            "        .erase_cycles = 100000, .bin_path = BIN_PATH };\n"
            "    flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "    if (!dev) { printf(\"flash init failed\\n\"); return 1; }\n"
            "    flash_sim_erase(dev, 0, KV_CAPACITY);\n"
            "    kv_init(dev, 0, KV_CAPACITY);\n"
            "    const char *v = \"hello-imported\";\n"
            "    expect(\"kv_write\", kv_write(dev, 1, v, (uint16_t)strlen(v)) == FLASH_OK);\n"
            "    char rb[32] = {0}; uint16_t rl = sizeof(rb);\n"
            "    expect(\"kv_read\", kv_read(dev, 1, rb, &rl) == FLASH_OK);\n"
            "    expect(\"kv_data_match\", rl == strlen(v) && memcmp(rb, v, rl) == 0);\n"
            "    int32_t n = 0xCAFEBABE; uint16_t rn = sizeof(n);\n"
            "    expect(\"kv_write_int\", kv_write(dev, 2, &n, sizeof(n)) == FLASH_OK);\n"
            "    int32_t rn2 = 0; uint16_t rnl = sizeof(rn2);\n"
            "    expect(\"kv_read_int\", kv_read(dev, 2, &rn2, &rnl) == FLASH_OK);\n"
            "    expect(\"kv_int_match\", rn2 == n);\n"
            "    expect(\"kv_delete\", kv_delete(dev, 1) == FLASH_OK);\n"
            "    uint16_t dl = 4;\n"
            "    expect(\"kv_read_deleted\", kv_read(dev, 1, NULL, &dl) == FLASH_ERR_ARGS);\n"
            "    flash_sim_deinit(dev);\n"
            "    printf(\"\\n=== 导入库验证结果: %s ===\\n\", g_fail == 0 ? \"全部通过\" : \"存在失败\");\n"
            "    return g_fail == 0 ? 0 : 1;\n"
            "}\n"
        )
        return (tpl
                .replace("__TITLE__", title)
                .replace("__LIB__", lib)
                .replace("__CAPACITY__", str(capacity))
                .replace("__ERASE_SIZE__", str(erase_size)))
    # simulator 自检入口
    tpl = (
        "/* 自动生成的自检入口：验证 flash_sim 基座库 */\n"
        "#include \"flash_sim.h\"\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n\n"
        "#define BIN_PATH \"imported_sim_demo.bin\"\n\n"
        "static int g_fail = 0;\n"
        "static void expect(const char *name, int cond) {\n"
        "    printf(\"  [%s] %s\\n\", cond ? \"OK  \" : \"FAIL\", name);\n"
        "    if (!cond) g_fail++;\n"
        "}\n\n"
        "int main(void) {\n"
        "    printf(\"=== 导入库运行验证: __TITLE__ ===\\n\");\n"
        "    flash_config_t cfg = {\n"
        "        .type = FLASH_TYPE_NOR, .total_size = 64*1024,\n"
        "        .erase_size = __ERASE_SIZE__, .write_size = 1, .read_size = 1,\n"
        "        .erase_cycles = 100000, .bin_path = BIN_PATH };\n"
        "    flash_dev_t *dev = flash_sim_init(&cfg);\n"
        "    if (!dev) { printf(\"init failed\\n\"); return 1; }\n"
        "    uint8_t w[16]; for (int i=0;i<16;i++) w[i]=(uint8_t)(0xA0+i);\n"
        "    uint8_t r[16]={0};\n"
        "    expect(\"erase\", flash_sim_erase(dev,0,cfg.erase_size)==FLASH_OK);\n"
        "    expect(\"write\", flash_sim_write(dev,0,w,16)==FLASH_OK);\n"
        "    expect(\"read\", flash_sim_read(dev,0,r,16)==FLASH_OK);\n"
        "    expect(\"match\", memcmp(w,r,16)==0);\n"
        "    flash_sim_deinit(dev);\n"
        "    printf(\"\\n=== 导入库验证结果: %s ===\\n\", g_fail==0?\"全部通过\":\"存在失败\");\n"
        "    return g_fail==0?0:1;\n"
        "}\n"
    )
    return tpl.replace("__TITLE__", title).replace("__ERASE_SIZE__", str(erase_size))


def _render_porting_md(fw_id, recipe, params):
    """生成移植说明文档。"""
    lib = recipe["lib_name"]
    files = "\n".join("- `%s`" % os.path.basename(fn)
                      for fn in recipe["copy_files"])
    if fw_id == "kv":
        body = (
            "## 移植步骤\n\n"
            "1. 将 `%s.c` / `%s.h` 加入你的工程（无需 RTOS 依赖）。\n"
            "2. 实现底层 Flash 驱动，并暴露与 `flash_sim.h` **相同签名**的接口：\n"
            "   `flash_sim_init / flash_sim_read / flash_sim_write / flash_sim_erase / flash_sim_deinit`。\n"
            "3. 把 `%s.h` 中的 `#include \"flash_sim.h\"` 指向你的驱动头，或保留\n"
            "   `flash_sim` 实现直接对接硬件。\n"
            "4. 调用 `kv_init / kv_write / kv_read / kv_delete` 即可使用。\n\n"
            "## 掉电安全说明\n\n"
            "写入采用两步提交（先写数据、再写状态字 COMMITTED），中途掉电的记录\n"
            "在 `kv_init` 加载时会被丢弃，保证数据一致性。\n"
        ) % (lib, lib, lib)
        cfg_text = (
            "## 配置参数（本次导出）\n\n"
            "- capacity: %s 字节\n"
            "- page_size: %s 字节\n"
            "- erase_size: %s 字节\n\n"
        ) % (params.get("capacity", 8192), params.get("page_size", 256),
             params.get("erase_size", 4096))
    else:
        api_hint = {
            "easyflash": "调用 `easyflash_init()` 后使用 `ef_set_env / ef_get_env` 访问 KV。",
            "flashdb": "调用 `fdb_kvdb_init()` 后使用 `fdb_kv_set / fdb_kv_get` 访问 KV。",
            "baremetal": "调用 `bm_config_init()` 后使用 `bm_config_save / bm_config_load` 保存结构体。",
            "fs": "调用 `fs_init / fs_create / fs_write / fs_read / fs_delete` 进行多文件读写与管理。",
            "littlefs": "调用 `lfs_mount` 挂载后使用 `lfs_file_open/write/read/close` 访问文件。",
            "fatfs": "调用 `f_mount` 挂载后使用 `f_open / f_write / f_read / f_close` 访问文件。",
        }.get(fw_id, "参考框架自带头文件 API。")
        body = (
            "## 移植步骤\n\n"
            "1. 将本包内所有 `.c` / `.h` 加入你的工程。\n"
            "2. 实现底层 Flash 驱动，并暴露与 `flash_sim.h` **相同签名**的接口：\n"
            "   `flash_sim_init / flash_sim_read / flash_sim_write / flash_sim_erase / flash_sim_deinit`。\n"
            "3. 框架内部已 `#include \"flash_sim.h\"`，将 `flash_sim` 实现替换为你的\n"
            "   真实驱动即可（或保留 `flash_sim` 直接对接硬件）。\n"
            "4. %s\n\n"
            "## 掉电安全说明\n\n"
            "本框架在模拟基座上已通过掉电残留/回退测试（见 `test_main.c` 输出）。\n"
            "正式移植后建议同样在目标硬件上做一次掉电验证。\n"
        ) % api_hint
        cfg_text = (
            "## 配置参数（本次导出）\n\n"
            "- erase_size: %s 字节\n\n"
        ) % params.get("erase_size", 4096)
    return (
        "# %s 移植说明\n\n"
        "本包由 Flash 存储仿真平台「代码生成引擎」导出，包含可直接移植到\n"
        "嵌入式目标的库文件。\n\n"
        "## 文件清单\n\n"
        "%s\n"
        "- `test_main.c`：基于模拟基座的自检示例（仅 PC 仿真用，可删除）\n"
        "- `manifest.json`：包元信息（导入平台时使用）\n\n"
        "%s"
        "%s"
    ) % (recipe["title"], files, body, cfg_text)


def generate_zip(fw_id, params):
    """生成库文件 zip 包，返回 (zip_bytes, manifest_dict)。

    失败时抛出 RuntimeError，message 描述原因。

    说明：
      - kv/simulator 两个基础框架使用"复制源码 + 自动生成 test_main.c"方式。
      - easyflash/flashdb/baremetal 三个框架源码文件较多且自带 main 测试，
        采用"整目录相对 ROOT 复制 + 保留框架自带 main 作为 test_main.c"方式，
        以保证导出的包与模拟基座验证时完全一致（可独立编译运行）。
    """
    recipe = RECIPES.get(fw_id)
    if not recipe:
        raise RuntimeError("不支持生成的框架: %s" % fw_id)

    lib = recipe["lib_name"]
    ts = int(time.time())
    lib_sources = []  # 仅 .c，运行时需与 flash_sim.c 一起编译
    manifest = {
        "id": "%s_export_%d" % (fw_id, ts),
        "name": "%s（导出库）" % recipe["title"],
        "desc": recipe["desc"],
        "base": fw_id,
        "requires": recipe.get("requires", "flash_sim"),
        "cflags": recipe.get("cflags", []),
        "params": params,
        "entry": "test_main.c",
        "lib": lib,
        "lib_sources": lib_sources,
    }

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        # 复制真实库源码
        copied = []
        for fn in recipe["copy_files"]:
            full = os.path.join(ROOT, fn)
            if not os.path.exists(full):
                # 基础框架（kv/simulator）文件相对 src_dir
                full = os.path.join(ROOT, recipe["src_dir"], fn)
            if not os.path.exists(full):
                raise RuntimeError("源文件缺失: %s" % full)
            # 去掉 frameworks/... 前缀，扁平放进包内（避免深层目录）
            arcname = os.path.basename(fn)
            zf.writestr(arcname, open(full, "rb").read())
            copied.append(arcname)
            if fn.endswith(".c"):
                lib_sources.append(arcname)

        # 测试入口：基础框架自动生成，新框架保留自带 main
        test_entry = recipe.get("test_entry")
        if test_entry:
            full = os.path.join(ROOT, test_entry)
            if not os.path.exists(full):
                raise RuntimeError("测试入口缺失: %s" % full)
            zf.writestr("test_main.c", open(full, "rb").read())
        else:
            zf.writestr("test_main.c",
                        _render_test_main(fw_id, recipe, params))
        copied.append("test_main.c")

        manifest["sources"] = copied
        zf.writestr("PORTING.md", _render_porting_md(fw_id, recipe, params))
        zf.writestr("manifest.json",
                    json.dumps(manifest, ensure_ascii=False, indent=2))
    return buf.getvalue(), manifest
