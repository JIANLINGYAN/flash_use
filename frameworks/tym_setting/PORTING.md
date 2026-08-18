# TYM Setting 组件库移植说明（PORTING）

## 1. 组件概述

- 名称：TYM Setting（Tymphany 参数持久化框架）
- 来源：Tymphany HK Ltd. 专有框架（提取自 Airoha SDK 项目）
- 存储语义类别：**ID 索引静态表 + RAM 全镜像 + 延时批量整页回写**
- 功能：编译期固定 `eSettingId` → 固定 Flash 地址的静态映射；`Setting_Set`
  只写 RAM，经延时窗口后整页擦除 + 全表回写；`Setting_Get` O(1) 下标访问
- 与 NVDM/Zephyr 组件差异：**无磨损均衡、无 GC、无校验和、掉电不安全、
  写放大极大**（改 1 项 = 擦整区 + 写全表）

## 2. 目录结构

```
frameworks/tym_setting/
├── vendor/                 # 框架本体（已去耦裁剪）
│   ├── inc/
│   │   ├── app_setting_idle_activity.h  # 公共 API（去掉 ui_shell/QP 事件）
│   │   ├── SettingSrv_priv.h            # tSettingDatabase + 属性位掩码
│   │   ├── StorageDrv.h                 # cStorageDrv 函数指针表（去掉 deviceTypes）
│   │   ├── StorageDrv_priv.h / NvmDrv.h / NvmDrv_priv.h
│   └── src/
│       ├── app_setting_idle_activity.c  # 核心（去 FreeRTOS/ui_shell/hal_log）
│       └── StorageDrv.c                 # 驱动工厂（单 NVM 直装）
├── config/                 # 数据契约（精简三表，须按产品重写）
│   ├── setting_id.h        # eSettingId 枚举（含 SETID_MAX 收尾）
│   └── SettingSrv.config   # 结构 + settingDB + OFFSET + romMap（含编译期断言）
├── compat/
│   ├── cplus.h             # C-OO 宏（原样）
│   ├── commonTypes.h       # 类型/位操作宏（原样）
│   └── tym_setting_log.h   # 日志抽象（新增，替代 hal_log）
├── tym_setting_sim_port.c/.h  # 移植层：NvmDrv 回调桥接 flash_sim
├── test/main_tym_setting.c   # 组件自检（基本写读改）
└── PORTING.md
```

## 3. 对外 API（vendor/inc/app_setting_idle_activity.h）

| 函数 | 说明 |
|------|------|
| `SettingSrv_Init()` | 装配驱动 + 从 Flash 载入 RAM 镜像（替代原 ui_shell 活动入口） |
| `SettingSrv_Tick(elapsed_ms)` | 裸机 tick 轮询（替代原 FreeRTOS 1ms 定时器） |
| `Setting_Set(id, pValue)` | 写 RAM + 置位 + 启动延时回写窗口 |
| `Setting_Get(id)` / `GetEx` | 读值（RAM 就绪返回 RAM，否则返回默认值） |
| `Setting_GetAddr(id)` | 返回 RAM 镜像地址（可写） |
| `Setting_GetSize(id)` | 返回槽位大小 |
| `Setting_IsReady/IsIdValid/IsIdNVM` | 状态查询 |
| `Setting_Reset(id)` | 清 RAM 标记（不擦 Flash） |
| `SettingSrv_BookkeepingEx()` | 立即触发整页回写 |

## 4. 去耦修改记录（vendor 相对原版）

| 原版依赖 | 修改 |
|----------|------|
| FreeRTOS `xTimerCreate`/1ms 定时器 | 删除，改为 `SettingSrv_Tick(elapsed_ms)` 裸机轮询 |
| `ui_shell_*` 事件框架 / `app_setting_idle_activity_proc` | 删除，改为直接 `SettingSrv_Init()` |
| `hal_log.h` / `log_hal_info` | 删除，改为 `compat/tym_setting_log.h` 的 LOG_E/W/I |
| `REQ_EVT`/`RESP_EVT`/`SUBCLASS(cSettingSrv,cServer)`（QP 形态） | 删除（未启用 `SETTING_HAS_ROM_DATA`，死代码） |
| `ProductDefine.h` / `deviceTypes.h` / `attachedDevices.h` | 删除，`tStorageDevice` 由移植层提供 |
| `Setting_GetEx` 返回 Flash 裸地址 | 改为仅回退默认值（模拟平台 Flash 不可内存映射） |
| `Bookkeeping` 只判 `ErasePage` 不判 `SetValue` | 补判 `SetValue`，写失败上报 |
| 补齐分支 `uint32 buf`（64 位主机 8 字节） | 改为 `uint32_t`（固定 4 字节），修复错位写 |
| 热路径 `ASSERT` 被注释 | 恢复 id 越界检查 |

## 5. 平台依赖（移植层 tym_setting_sim_port.c）

框架经 `cStorageDrv` 三个函数指针访问 Flash：

| 回调 | 桥接 |
|------|------|
| `SetValue(addr,pBuf,size)` | `flash_sim_write`（要求 4 字节对齐，小于 4 由 config 补齐） |
| `GetValue(addr,pBuf,size)` | `flash_sim_read` |
| `ErasePage(addr)` | 擦除整个 Setting 分区（`flash_sim_erase` 按块） |

分区配置由 `tym_setting_sim_setup(dev, base, capacity, erase_size)` 注入。

## 6. 数据契约（config/，移植须重写）

三表必须严格对齐（已有编译期断言保护）：
1. `eSettingId` 枚举（`SETID_MAX` 收尾）
2. `settingDB[SETID_MAX]`：ID → `{&成员, 大小, 属性}`，**顺序与枚举一致**
3. `settingRomMap[]`：ID → Flash 绝对地址（`SETT_PAGE_ROM_ADDR + OFFSET*4`）

属性位：`NVM`(落 Flash) / `EEPROM` / `SET`(RAM 已载入) / `VALID`(本产品有效) / `UPD`。
`OFFSET_*` 链单位为**字（4 字节）**，须与实际写入大小一致。

## 7. 已知限制

- **写放大极大**：改 1 项 = 擦整区 + 写全表；应用层压测轮数宜小。
- **掉电不安全**：擦除后、全量写完成前掉电 → 整区数据丢失（无 A/B 备份）。
- **固定槽位**：NVM 槽位数固定，应用层 items > NVM 槽位数时 key 碰撞（后写覆盖先写）。
- **无磨损均衡**：频繁 Set 会缩短 Flash 寿命。
- `Setting_GetEx` 已去掉 Flash 裸指针回退（模拟平台不可内存映射），
  移植到可内存映射平台可恢复原语义。

## 8. 验证方法

```bash
python3 scripts/run_tests.py tym_setting
python3 scripts/check_app_build.py tym_setting
# 应用层（items 须 <= 4，避免槽位碰撞）
APP_COMPONENT=tym_setting APP_TASK=durability APP_ITEMS=4 APP_VLEN=4 APP_ROUNDS=5 /tmp/main_app_tym
```
