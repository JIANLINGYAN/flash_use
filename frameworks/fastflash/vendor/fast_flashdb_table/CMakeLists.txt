cmake_minimum_required(VERSION 3.10)
project(fast_flash C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 设置编译选项
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra)
endif()

# 添加包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/port_win)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/app)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion/win-adapter)

# ========================================
# 核心源文件
# ========================================
set(CORE_SOURCES
    core/fast_flash_core.c
    core/fast_flash_log.c
)

# ========================================
# Windows平台源文件
# ========================================
set(PORT_SOURCES
    port_win/flash_adapter_win.c
)

# ========================================
# RS Motion Fast FlashDB Windows版本源文件
# ========================================
set(RS_MOTION_FAST_FLASHDB_SOURCES
    app/rs_motion_fast_flashdb_win.c
    app/rs_motion_fast_flashdb_win_adapter.c
    app/rs_motion_adapter_wrapper.c
)

# ========================================
# RS Motion源文件
# ========================================
set(RS_MOTION_SOURCES
    examples/rs_motion/rs_motion.c
    examples/rs_motion/rs_motion_adapter.c
    examples/rs_motion/rs_motion_manager.c
    examples/rs_motion/rs_motion_fast_flash.c
    examples/rs_motion/rs_motion_manager_new.c
    examples/rs_motion/rs_motion_data_types.h
)

# ========================================
# RS Motion Windows适配器源文件
# ========================================
set(RS_MOTION_WIN_ADAPTER_SOURCES
    examples/rs_motion/win-adapter/cmsis_os2.c
    examples/rs_motion/win-adapter/cwm_os_adapter.c
    examples/rs_motion/win-adapter/cwm_lib.c
    examples/rs_motion/win-adapter/hal_timer.c
    examples/rs_motion/win-adapter/hal_trace.c
)

# ========================================
# 测试源文件
# ========================================
set(CORE_TEST_SOURCES
    port_win/test_fast_flash.c
)

set(RS_MOTION_FAST_FLASHDB_TEST_SOURCES
    app/test_rs_motion_fast_flashdb_win.c
)

set(RS_MOTION_TEST_SOURCES
    examples/rs_motion/test_rs_motion_win.c
)

# ========================================
# 创建核心库
# ========================================
add_library(fast_flash_core_lib STATIC ${CORE_SOURCES})
target_include_directories(fast_flash_core_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

# ========================================
# 创建Windows端口库
# ========================================
add_library(port_win_lib STATIC ${PORT_SOURCES})
target_include_directories(port_win_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/port_win
)

# ========================================
# 创建核心测试可执行文件
# ========================================
add_executable(fast_flash_test 
    ${CORE_TEST_SOURCES}
)
target_include_directories(fast_flash_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/port_win
)

# ========================================
# 创建RS Motion库
# ========================================
add_library(rs_motion_lib STATIC 
    ${RS_MOTION_SOURCES}
    ${RS_MOTION_WIN_ADAPTER_SOURCES}
)
target_include_directories(rs_motion_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion/win-adapter
)


# ========================================
# 创建RS Motion测试可执行文件
# ========================================
add_executable(rs_motion_test 
    ${RS_MOTION_TEST_SOURCES}
)
target_include_directories(rs_motion_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/rs_motion/win-adapter
)

# ========================================
# 链接库
# ========================================
target_link_libraries(fast_flash_test 
    fast_flash_core_lib 
    port_win_lib
)

target_link_libraries(rs_motion_test 
    rs_motion_lib
)

# ========================================
# Windows特定配置
# ========================================
if(WIN32)
    target_link_libraries(fast_flash_test kernel32)
    target_link_libraries(rs_motion_test kernel32)
    
    target_compile_definitions(fast_flash_test PRIVATE _WIN32 _WIN64)
    target_compile_definitions(rs_motion_test PRIVATE _WIN32 _WIN64)
endif()

# ========================================
# 编译选项
# ========================================
target_compile_options(fast_flash_test PRIVATE -DDEBUG)
target_compile_options(rs_motion_test PRIVATE -DDEBUG)

# ========================================
# 自定义目标
# ========================================
add_custom_target(test_core
    COMMAND fast_flash_test
    DEPENDS fast_flash_test
    COMMENT "Running core tests"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(test_rs_motion_fast_flashdb
    COMMAND rs_motion_fast_flashdb_test
    DEPENDS rs_motion_fast_flashdb_test
    COMMENT "Running RS Motion Fast FlashDB tests"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(test_rs_motion
    COMMAND rs_motion_test
    DEPENDS rs_motion_test
    COMMENT "Running RS Motion tests"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(test_all
    COMMAND ${CMAKE_COMMAND} -E echo "Running all tests..."
    COMMAND fast_flash_test
    COMMAND rs_motion_fast_flashdb_test
    COMMAND rs_motion_test
    DEPENDS fast_flash_test rs_motion_fast_flashdb_test rs_motion_test
    COMMENT "Running all tests"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)