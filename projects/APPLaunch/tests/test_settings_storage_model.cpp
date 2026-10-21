/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/settings/settings_storage_model.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

void write_file(const fs::path &file, const std::string &text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << text;
}

/* A fake /sys and /dev (report 028, x-b: the FZ-M1's SATA SSD, an NVMe disk with a LUKS root, an eMMC, a hard disk,
 * a virtual disk): sys/devices/.../block/<disk>/<partition> as the kernel lays it out, sys/class/block/<name> links
 * to them, dev/<name> nodes (plain files) and dev/mapper/cryptroot -> ../dm-0. */
fs::path make_fake_sys()
{
    const fs::path root = fs::temp_directory_path() / ("settings-storage-test." + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path devices = root / "sys/devices/pci0000:00";
    const fs::path blocks = root / "sys/class/block";
    fs::create_directories(blocks);
    fs::create_directories(root / "dev/mapper");
    auto disk = [&](const std::string &name, const std::string &rotational, const std::string &model,
                    const std::string &mmc_type, std::initializer_list<std::string> partitions) {
        const fs::path dir = devices / "block" / name;
        if (!rotational.empty()) write_file(dir / "queue/rotational", rotational + "\n");
        if (!model.empty()) write_file(dir / "device/model", model + "\n");
        if (!mmc_type.empty()) write_file(dir / "device/type", mmc_type + "\n");
        fs::create_directories(dir);
        fs::create_directory_symlink(dir, blocks / name);
        write_file(root / "dev" / name, "");
        for (const std::string &part : partitions) {
            write_file(dir / part / "partition", "1\n");
            fs::create_directory_symlink(dir / part, blocks / part);
            write_file(root / "dev" / part, "");
        }
    };
    disk("sda", "0", "KINGSTON SA400S37240G   ", "", {"sda1", "sda2"});
    disk("sdb", "1", "WDC WD10EZEX", "", {"sdb1"});
    disk("nvme0n1", "0", "Samsung SSD 970 EVO", "", {"nvme0n1p1", "nvme0n1p2", "nvme0n1p3"});
    disk("mmcblk0", "0", "", "MMC", {"mmcblk0p1", "mmcblk0p2"});
    disk("mmcblk1", "0", "", "SD", {"mmcblk1p2"});
    disk("vda", "1", "", "", {"vda1"});
    /* dm-0: LUKS on nvme0n1p3 */
    const fs::path dm = devices / "virtual/block/dm-0";
    fs::create_directories(dm / "slaves");
    fs::create_directory_symlink(devices / "block/nvme0n1/nvme0n1p3", dm / "slaves/nvme0n1p3");
    fs::create_directory_symlink(dm, blocks / "dm-0");
    write_file(root / "dev/dm-0", "");
    fs::create_symlink("../dm-0", root / "dev/mapper/cryptroot");
    return root;
}

SettingsStorageModel::SpaceProbe probe_from(
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> &spaces)
{
    return [&spaces](const std::string &path,
                     std::uintmax_t &capacity,
                     std::uintmax_t &available) {
        const auto found = spaces.find(path);
        if (found == spaces.end()) return false;
        capacity = found->second.first;
        available = found->second.second;
        return true;
    };
}

SettingsStorageModel::DeviceTypeProbe type_probe_from(
    const std::map<std::string, SettingsMmcDeviceType> &types)
{
    return [&types](const std::string &device_name) {
        const auto found = types.find(device_name);
        return found == types.end() ? SettingsMmcDeviceType::Unknown : found->second;
    };
}

} // namespace

int main()
{
    assert(SettingsStorageModel::format_bytes(0) == "0 B");
    assert(SettingsStorageModel::format_bytes(1023) == "1023 B");
    assert(SettingsStorageModel::format_bytes(1024) == "1.0 KiB");
    assert(SettingsStorageModel::format_bytes(5ULL * 1024 * 1024 * 1024) == "5.0 GiB");

    const std::string mixed_mmc_mounts =
        "/dev/mmcblk0p1 /boot vfat rw 0 0\n"
        "/dev/mmcblk0p2 / ext4 rw 0 0\n"
        "/dev/mmcblk1p1 /media/SD\\040Card exfat rw 0 0\n"
        "/dev/nvme0n1p1 /home ext4 rw 0 0\n";
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> mixed_spaces = {
        {"/boot", {512, 128}},
        {"/", {4096, 1536}},
        {"/media/SD Card", {32768, 24576}},
        {"/home", {8192, 4096}},
    };
    const std::map<std::string, SettingsMmcDeviceType> mixed_types = {
        {"mmcblk0", SettingsMmcDeviceType::Mmc},
        {"mmcblk1", SettingsMmcDeviceType::Sd},
    };
    const SettingsStorageInfo mixed = SettingsStorageModel::read_from_mount_table(
        mixed_mmc_mounts, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(mixed.valid);
    assert(mixed.mount_path == "/media/SD Card");
    assert(mixed.total_bytes == 32768);
    assert(mixed.available_bytes == 24576);

    const SettingsStorageInfo emmc_only = SettingsStorageModel::read_from_mount_table(
        "/dev/mmcblk0p2 / ext4 rw 0 0\n",
        probe_from(mixed_spaces),
        type_probe_from(mixed_types));
    assert(!emmc_only.valid);

    const std::string partitioned_sd_mounts =
        "/dev/mmcblk1p1 /media/SD\\040Boot vfat rw 0 0\n"
        "/dev/mmcblk1p2 /media/SD\\040Data exfat rw 0 0\n";
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> separate_spaces = {
        {"/media/SD Boot", {512, 128}},
        {"/media/SD Data", {32768, 24576}},
    };
    const std::map<std::string, SettingsMmcDeviceType> sd_types = {
        {"mmcblk1", SettingsMmcDeviceType::Sd},
    };
    const SettingsStorageInfo separate = SettingsStorageModel::read_from_mount_table(
        partitioned_sd_mounts, probe_from(separate_spaces), type_probe_from(sd_types));
    assert(separate.valid);
    assert(separate.mount_path == "/media/SD Data");
    assert(separate.total_bytes == 32768);
    assert(separate.available_bytes == 24576);

    const std::string host_mounts =
        "/dev/nvme0n1p2 / ext4 rw 0 0\n"
        "tmpfs /tmp tmpfs rw 0 0\n";
    const SettingsStorageInfo host = SettingsStorageModel::read_from_mount_table(
        host_mounts, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(!host.valid);

    const std::string malformed_mmc =
        "/dev/notmmcblk0p1 /fake ext4 rw 0 0\n"
        "/dev/mmcblkXp1 /also-fake ext4 rw 0 0\n";
    const SettingsStorageInfo malformed = SettingsStorageModel::read_from_mount_table(
        malformed_mmc, probe_from(mixed_spaces), type_probe_from(mixed_types));
    assert(!malformed.valid);

    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> invalid_space = {
        {"/", {1024, 2048}},
    };
    const SettingsStorageInfo invalid = SettingsStorageModel::read_from_mount_table(
        "/dev/mmcblk1p2 / ext4 rw 0 0\n",
        probe_from(invalid_space),
        type_probe_from(sd_types));
    assert(!invalid.valid);

    const SettingsStorageInfo no_type_probe = SettingsStorageModel::read_from_mount_table(
        partitioned_sd_mounts, probe_from(separate_spaces), {});
    assert(!no_type_probe.valid);

    // ---- any root device (report 028, x-b): a PC's disk, never called an SD card ------------------------------
    assert(SettingsStorageModel::disk_of("sda2") == "sda");
    assert(SettingsStorageModel::disk_of("sda") == "sda");
    assert(SettingsStorageModel::disk_of("sdab12") == "sdab");
    assert(SettingsStorageModel::disk_of("nvme0n1p2") == "nvme0n1");
    assert(SettingsStorageModel::disk_of("nvme0n1") == "nvme0n1");
    assert(SettingsStorageModel::disk_of("mmcblk0p2") == "mmcblk0");
    assert(SettingsStorageModel::disk_of("vda1") == "vda");
    assert(SettingsStorageModel::disk_of("xvda1") == "xvda");
    assert(SettingsStorageModel::disk_of("dm-0") == "dm-0");
    assert(SettingsStorageModel::disk_of("loop0") == "loop0");

    const fs::path fake = make_fake_sys();
    const std::string sys = (fake / "sys").string();
    const std::string dev = (fake / "dev").string();
    const auto ssd = SettingsStorageModel::probe_root_device(dev + "/sda2", sys);
    assert(ssd && ssd->disk == "sda" && ssd->kind == SettingsStorageKind::Ssd);
    assert(ssd->model == "KINGSTON SA400S37240G"); /* trimmed */
    const auto hdd = SettingsStorageModel::probe_root_device(dev + "/sdb1", sys);
    assert(hdd && hdd->disk == "sdb" && hdd->kind == SettingsStorageKind::HardDisk);
    const auto nvme = SettingsStorageModel::probe_root_device(dev + "/nvme0n1p2", sys);
    assert(nvme && nvme->disk == "nvme0n1" && nvme->kind == SettingsStorageKind::Ssd);
    const auto luks = SettingsStorageModel::probe_root_device(dev + "/mapper/cryptroot", sys);
    assert(luks && luks->disk == "nvme0n1" && luks->kind == SettingsStorageKind::Ssd);
    const auto emmc = SettingsStorageModel::probe_root_device(dev + "/mmcblk0p2", sys);
    assert(emmc && emmc->disk == "mmcblk0" && emmc->kind == SettingsStorageKind::Emmc && emmc->model.empty());
    const auto sd = SettingsStorageModel::probe_root_device(dev + "/mmcblk1p2", sys);
    assert(sd && sd->kind == SettingsStorageKind::SdCard);
    const auto virt = SettingsStorageModel::probe_root_device(dev + "/vda1", sys);
    assert(virt && virt->disk == "vda" && virt->kind == SettingsStorageKind::Disk); /* rotational 1, but virtual */
    const auto whole = SettingsStorageModel::probe_root_device(dev + "/sda", sys);
    assert(whole && whole->disk == "sda");
    assert(!SettingsStorageModel::probe_root_device("overlay", sys));
    assert(!SettingsStorageModel::probe_root_device("tmpfs", sys));
    assert(!SettingsStorageModel::probe_root_device("server:/export/root", sys));
    assert(!SettingsStorageModel::probe_root_device(dev + "/sdz9", sys)); /* not in sysfs */

    // the page's line for each kind; the SD card found among the mounts keeps its line exactly
    SettingsStorageInfo line_info;
    assert(SettingsStorageModel::device_line(line_info) == "SD card");
    line_info.kind = SettingsStorageKind::Ssd;
    line_info.device = "sda";
    line_info.model = "KINGSTON SA400S37240G";
    assert(SettingsStorageModel::device_line(line_info) == "SSD sda (KINGSTON SA400S37240G)");
    line_info.kind = SettingsStorageKind::HardDisk;
    line_info.model.clear();
    assert(SettingsStorageModel::device_line(line_info) == "Hard disk sda");
    line_info.kind = SettingsStorageKind::Emmc;
    line_info.device = "mmcblk0";
    assert(SettingsStorageModel::device_line(line_info) == "eMMC mmcblk0");
    line_info.kind = SettingsStorageKind::Disk;
    line_info.device = "vda";
    assert(SettingsStorageModel::device_line(line_info) == "Disk vda");

    // the root filesystem from mount tables, through the fake sysfs (sources rewritten into the fake /dev)
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> root_space = {
        {"/", {117ULL << 30, 90ULL << 30}},
    };
    const SettingsStorageModel::RootDeviceProbe fake_probe = [&](const std::string &source) {
        const std::string mapped = source.compare(0, 5, "/dev/") == 0 ? dev + source.substr(4) : source;
        return SettingsStorageModel::probe_root_device(mapped, sys);
    };
    auto root_of = [&](const std::string &table) {
        return SettingsStorageModel::read_root_from_mount_table(table, probe_from(root_space), fake_probe);
    };
    const SettingsStorageInfo tablet = root_of(
        "sysfs /sys sysfs rw 0 0\n/dev/sda2 / ext4 rw,noatime 0 0\n/dev/sda1 /boot/efi vfat rw 0 0\n");
    assert(tablet.valid && tablet.kind == SettingsStorageKind::Ssd && tablet.device == "sda");
    assert(tablet.mount_path == "/" && tablet.total_bytes == (117ULL << 30) && tablet.available_bytes == (90ULL << 30));
    assert(SettingsStorageModel::device_line(tablet) == "SSD sda (KINGSTON SA400S37240G)");
    const SettingsStorageInfo nvme_root = root_of("/dev/nvme0n1p2 / ext4 rw 0 0\n");
    assert(nvme_root.valid && nvme_root.device == "nvme0n1" && nvme_root.kind == SettingsStorageKind::Ssd);
    const SettingsStorageInfo emmc_root = root_of("/dev/mmcblk0p2 / ext4 rw 0 0\n");
    assert(emmc_root.valid && SettingsStorageModel::device_line(emmc_root) == "eMMC mmcblk0");
    const SettingsStorageInfo luks_root = root_of("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    assert(luks_root.valid && luks_root.device == "nvme0n1");
    // an overlay or tmpfs root (live system, container): nothing to name; the last "/" entry is what is on top
    assert(!root_of("overlay / overlay rw,lowerdir=/a,upperdir=/b,workdir=/c 0 0\n").valid);
    assert(!root_of("tmpfs / tmpfs rw 0 0\n").valid);
    assert(!root_of("/dev/sda2 / ext4 rw 0 0\noverlay / overlay rw 0 0\n").valid);
    assert(root_of("overlay / overlay rw 0 0\n/dev/sda2 / ext4 rw 0 0\n").valid);
    assert(!root_of("/dev/sda2 /home ext4 rw 0 0\n").valid); /* no root entry */
    assert(!root_of("").valid);
    // the space unreadable or absurd: nothing
    const std::map<std::string, std::pair<std::uintmax_t, std::uintmax_t>> no_space = {};
    assert(!SettingsStorageModel::read_root_from_mount_table("/dev/sda2 / ext4 rw 0 0\n", probe_from(no_space),
                                                             fake_probe).valid);
    assert(!SettingsStorageModel::read_root_from_mount_table("/dev/sda2 / ext4 rw 0 0\n",
                                                             probe_from(invalid_space), fake_probe).valid);
    assert(!SettingsStorageModel::read_root_from_mount_table("/dev/sda2 / ext4 rw 0 0\n", probe_from(root_space),
                                                             {}).valid);
    // the SD path is untouched: an SD card on the deck is still found first by read_from_mount_table
    const SettingsStorageInfo deck = SettingsStorageModel::read_from_mount_table(
        "/dev/mmcblk1p2 / ext4 rw 0 0\n", probe_from(root_space), type_probe_from(sd_types));
    assert(deck.valid && SettingsStorageModel::device_line(deck) == "SD card");
    fs::remove_all(fake);

#if !defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) || !defined(__linux__)
    // Standalone and SDL builds have no target MMC contract and must not expose host `/`.
    assert(!SettingsStorageModel::read().valid);
#endif
}
