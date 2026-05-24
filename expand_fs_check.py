#!/usr/bin/env python3
# Checks if raspberry pi needs filesystem expansion
# Created 23 May 2026 by Sam Gardner <samuel.gardner@ttu.edu>

import subprocess

def get_block_device_size(device):
    with open(f"/sys/class/block/{device.split('/')[-1]}/size") as f:
        size_in_sectors = int(f.read().strip())
    size_in_bytes = size_in_sectors * 512
    return  size_in_bytes

if __name__ == '__main__':
    disk_size = get_block_device_size("/dev/mmcblk0")
    part_size = get_block_device_size("/dev/mmcblk0p2")

    if part_size < disk_size / 2:
        print(f"Partition ({part_size >> 20} MiB) < half of disk ({disk_size >> 20} MiB) — expanding...")
        subprocess.run(["raspi-config", "--expand-rootfs"], check=True)
        subprocess.run(["reboot"], check=True)
    else:
        print(f"Partition ({part_size >> 20} MiB) >= half of disk ({disk_size >> 20} MiB) — nothing to do.")
        exit(0)
