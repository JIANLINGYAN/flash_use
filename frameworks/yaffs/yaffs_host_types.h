/**
 * yaffs_host_types.h - YAFFS 宿主类型补丁头
 *
 * yaffs Direct 假设 Linux 语义（loff_t/dev_t），在裸机/仿真宿主上
 * 需由平台提供。本文件以 -include 方式注入所有 yaffs vendor 源文件，
 * 避免修改 upstream 代码。
 */
#ifndef YAFFS_HOST_TYPES_H
#define YAFFS_HOST_TYPES_H

#include <stdint.h>

#ifndef loff_t
typedef long long loff_t;
#endif

#ifndef dev_t
typedef unsigned int dev_t;
#endif

#ifndef off_t
typedef long off_t;
#endif

#ifndef mode_t
typedef unsigned int mode_t;
#endif

#ifndef ino_t
typedef unsigned long long ino_t;
#endif

#ifndef nlink_t
typedef unsigned int nlink_t;
#endif

#ifndef uid_t
typedef unsigned int uid_t;
#endif

#ifndef gid_t
typedef unsigned int gid_t;
#endif

#ifndef time_t
typedef long time_t;
#endif

#endif /* YAFFS_HOST_TYPES_H */
