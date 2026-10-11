/* SPDX-License-Identifier: Apache-2.0 */

#include <storage.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

#define DISK_NAME   DT_PROP(DT_NODELABEL(sdmmc1), disk_name)
#define MOUNT_POINT "/" DISK_NAME ":"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_POINT,
};

const char *storage_mount_point(void)
{
	return MOUNT_POINT;
}

int storage_init(void)
{
	if (disk_access_init(DISK_NAME) != 0) {
		printk("[ERROR] disk_access_init failed\r");
		return -1;
	}
	if (fs_mount(&mp) != 0) {
		printk("[ERROR] fs_mount failed\r");
		return -1;
	}
	printk("[INFO] SD card mounted\r");
	return 0;
}
