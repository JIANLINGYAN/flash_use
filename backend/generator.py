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
    return (
        "# %s 移植说明\n\n"
        "本包由 Flash 存储仿真平台「代码生成引擎」导出，包含可直接移植到\n"
        "嵌入式目标的库文件。\n\n"
        "## 文件清单\n\n"
        "- `%s.c` / `%s.h`：存储框架实现（核心库，平台无关）\n"
        "- `test_main.c`：基于模拟基座的自检示例（仅 PC 仿真用，可删除）\n"
        "- `manifest.json`：包元信息（导入平台时使用）\n\n"
        "## 移植步骤\n\n"
        "1. 将 `%s.c` / `%s.h` 加入你的工程（无需 RTOS 依赖）。\n"
        "2. 实现底层 Flash 驱动，并暴露与 `flash_sim.h` **相同签名**的接口：\n"
        "   `flash_sim_init / flash_sim_read / flash_sim_write / flash_sim_erase / flash_sim_deinit`。\n"
        "3. 把 `%s.h` 中的 `#include \"flash_sim.h\"` 指向你的驱动头，或保留\n"
        "   `flash_sim` 实现直接对接硬件。\n"
        "4. 调用 `kv_init / kv_write / kv_read / kv_delete` 即可使用。\n\n"
        "## 配置参数（本次导出）\n\n"
        "- capacity: %s 字节\n"
        "- page_size: %s 字节\n"
        "- erase_size: %s 字节\n\n"
        "## 掉电安全说明\n\n"
        "写入采用两步提交（先写数据、再写状态字 COMMITTED），中途掉电的记录\n"
        "在 `kv_init` 加载时会被丢弃，保证数据一致性。\n"
    ) % (recipe["title"], lib, lib, lib, lib, lib,
         params.get("capacity", 8192), params.get("page_size", 256),
         params.get("erase_size", 4096))


def generate_zip(fw_id, params):
    """生成库文件 zip 包，返回 (zip_bytes, manifest_dict)。

    失败时抛出 RuntimeError，message 描述原因。
    """
    recipe = RECIPES.get(fw_id)
    if not recipe:
        raise RuntimeError("不支持生成的框架: %s" % fw_id)

    src_dir = os.path.join(ROOT, recipe["src_dir"])
    if not os.path.isdir(src_dir):
        raise RuntimeError("框架源目录缺失: %s" % src_dir)

    lib = recipe["lib_name"]
    ts = int(time.time())
    manifest = {
        "id": "%s_export_%d" % (fw_id, ts),
        "name": "%s（导出库）" % recipe["title"],
        "desc": recipe["desc"],
        "base": fw_id,
        "requires": "flash_sim",           # 标记依赖模拟基座接口（导入校验依据）
        "params": params,
        "sources": recipe["copy_files"] + ["test_main.c"],
        "entry": "test_main.c",            # 编译运行入口
        "lib": lib,
    }

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        # 复制真实库源码
        for fn in recipe["copy_files"]:
            full = os.path.join(src_dir, fn)
            if not os.path.exists(full):
                raise RuntimeError("源文件缺失: %s" % full)
            with open(full, "rb") as f:
                zf.writestr(fn, f.read())
        # 生成配套文件
        zf.writestr("test_main.c", _render_test_main(fw_id, recipe, params))
        zf.writestr("PORTING.md", _render_porting_md(fw_id, recipe, params))
        zf.writestr("manifest.json",
                    json.dumps(manifest, ensure_ascii=False, indent=2))
    return buf.getvalue(), manifest
