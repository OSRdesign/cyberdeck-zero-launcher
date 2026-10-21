/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

/* What Settings > Storage shows: an SD card among the mounts (the Pi boards, as before), else the disk the root
 * filesystem lives on (a PC's SATA / NVMe disk, an eMMC, a virtual disk), never called an SD card. */
enum class SettingsStorageKind : std::uint8_t {
    SdCard,   /* an SD card (/sys/class/block/mmcblkN/device/type SD) */
    Emmc,     /* an eMMC (type MMC) */
    Ssd,      /* queue/rotational 0 (SATA SSD, NVMe) */
    HardDisk, /* queue/rotational 1 */
    Disk,     /* anything else: a virtual disk (vdX, xvdX), rotational unknown */
};

struct SettingsStorageInfo {
    bool valid = false;
    std::uintmax_t total_bytes = 0;
    std::uintmax_t available_bytes = 0;
    std::string mount_path;
    SettingsStorageKind kind = SettingsStorageKind::SdCard;
    std::string device; /* the root's disk (sda, nvme0n1, mmcblk0): read_root_from_mount_table only */
    std::string model;  /* its model from sysfs (device/model), may be empty */
};

/* The disk behind a root filesystem (probe_root_device) */
struct SettingsRootDevice {
    std::string disk;
    SettingsStorageKind kind = SettingsStorageKind::Disk;
    std::string model;
};

enum class SettingsMmcDeviceType : std::uint8_t {
    Unknown,
    Sd,
    Mmc,
};

class SettingsStorageModel {
public:
    using SpaceProbe = std::function<bool(const std::string &path,
                                          std::uintmax_t &capacity,
                                          std::uintmax_t &available)>;
    using DeviceTypeProbe = std::function<SettingsMmcDeviceType(
        const std::string &device_name)>;
    /* the disk behind a mount source of the root filesystem; nullopt: not a block device (overlay, tmpfs, ...) */
    using RootDeviceProbe = std::function<std::optional<SettingsRootDevice>(
        const std::string &source)>;

    /* An SD card among the mounts first (the Pi boards: unchanged), else the root filesystem's disk. Device builds
     * on Linux only (CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE); APPLAUNCH_STORAGE_MOUNTS replaces /proc/self/mounts
     * (a test hook: the render harness points it at nothing, so the page never shows the host's disk). */
    static SettingsStorageInfo read() noexcept;
    /* SD cards only: the largest one mounted */
    static SettingsStorageInfo read_from_mount_table(
        std::string_view mount_table,
        const SpaceProbe &space_probe,
        const DeviceTypeProbe &device_type_probe) noexcept;
    /* The filesystem mounted at / (the last "/" entry: what is on top) when it is on a block device: its disk, kind,
     * model and space. Invalid for an overlay, tmpfs or network root and when its space cannot be read. */
    static SettingsStorageInfo read_root_from_mount_table(
        std::string_view mount_table,
        const SpaceProbe &space_probe,
        const RootDeviceProbe &root_device_probe) noexcept;
    /* The disk behind a mount source from sysfs under sysfs_root ("/sys"): the source resolved (a /dev/mapper link:
     * LUKS, LVM, through the device mapper's slaves), its partition's disk, the kind (MMC type, rotational) and the
     * model. nullopt when the source is not a block device listed in sysfs_root/class/block. */
    static std::optional<SettingsRootDevice> probe_root_device(const std::string &source,
                                                               const std::string &sysfs_root);
    /* A partition's disk by its name (sda2 -> sda, nvme0n1p2 -> nvme0n1, mmcblk0p2 -> mmcblk0, vda1 -> vda); a
     * whole disk or another name is returned as it is */
    static std::string disk_of(std::string_view block_name);
    /* The page's first line: "SD card" (as before), "eMMC mmcblk0", "SSD sda (model)", "Hard disk sdb", "Disk vda" */
    static std::string device_line(const SettingsStorageInfo &info);
    static std::string format_bytes(std::uintmax_t bytes);
};
