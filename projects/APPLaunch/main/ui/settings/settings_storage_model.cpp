/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_storage_model.hpp"

#if __has_include("global_config.h")
#include "global_config.h"
#endif

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/sysmacros.h>
#endif

namespace {

struct MountEntry {
    std::string device_name;
    std::string path;
};

bool all_decimal(std::string_view value)
{
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (!std::isdigit(character)) return false;
    }
    return true;
}

std::optional<std::string> mmc_block_device_name(std::string_view source)
{
    const std::size_t slash = source.find_last_of('/');
    std::string_view name = slash == std::string_view::npos
        ? source
        : source.substr(slash + 1);
    constexpr std::string_view prefix = "mmcblk";
    if (name.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    name.remove_prefix(prefix.size());

    const std::size_t partition = name.find('p');
    const std::string_view device_index = name.substr(0, partition);
    if (!all_decimal(device_index)) return std::nullopt;
    if (partition != std::string_view::npos &&
        !all_decimal(name.substr(partition + 1)))
        return std::nullopt;
    return std::string(prefix) + std::string(device_index);
}

std::optional<std::string> resolve_mmc_block_device_name(const std::string &source)
{
    if (source.compare(0, 5, "/dev/") != 0) return std::nullopt;
    if (const auto direct = mmc_block_device_name(source)) return direct;

    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::canonical(source, error);
    if (error) return std::nullopt;
    return mmc_block_device_name(resolved.string());
}

std::string decode_mount_field(std::string_view encoded)
{
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '\\' && index + 3 < encoded.size() &&
            encoded[index + 1] >= '0' && encoded[index + 1] <= '7' &&
            encoded[index + 2] >= '0' && encoded[index + 2] <= '7' &&
            encoded[index + 3] >= '0' && encoded[index + 3] <= '7') {
            const unsigned int value =
                static_cast<unsigned int>(encoded[index + 1] - '0') * 64U +
                static_cast<unsigned int>(encoded[index + 2] - '0') * 8U +
                static_cast<unsigned int>(encoded[index + 3] - '0');
            decoded.push_back(static_cast<char>(value));
            index += 3;
        } else {
            decoded.push_back(encoded[index]);
        }
    }
    return decoded;
}

/* the source of the last entry mounted at / (what is on top), "" when there is none */
std::string root_source(std::string_view mount_table)
{
    std::string found;
    std::istringstream input{std::string(mount_table)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string source;
        std::string path;
        if (!(fields >> source >> path)) continue;
        if (decode_mount_field(path) == "/") found = decode_mount_field(source);
    }
    return found;
}

std::string first_line(const std::filesystem::path &file)
{
    std::ifstream input(file);
    std::string line;
    std::getline(input, line);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    std::size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
    return line.substr(start);
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

std::vector<MountEntry> mmc_mounts(std::string_view mount_table)
{
    std::vector<MountEntry> result;
    std::istringstream input{std::string(mount_table)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string source;
        std::string path;
        if (!(fields >> source >> path)) continue;
        source = decode_mount_field(source);
        path = decode_mount_field(path);
        const auto device_name = resolve_mmc_block_device_name(source);
        if (path.empty() || !device_name) continue;
        result.push_back({*device_name, std::move(path)});
    }
    return result;
}

#if defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) && defined(__linux__)
bool native_space_probe(const std::string &path,
                        std::uintmax_t &capacity,
                        std::uintmax_t &available)
{
    std::error_code error;
    const std::filesystem::space_info space = std::filesystem::space(path, error);
    if (error || space.capacity == static_cast<std::uintmax_t>(-1) ||
        space.available == static_cast<std::uintmax_t>(-1) ||
        space.capacity == 0 || space.available > space.capacity)
        return false;
    capacity = space.capacity;
    available = space.available;
    return true;
}

SettingsMmcDeviceType native_device_type_probe(const std::string &device_name)
{
    if (!mmc_block_device_name(device_name)) return SettingsMmcDeviceType::Unknown;
    std::ifstream type_file(
        std::filesystem::path("/sys/class/block") / device_name / "device/type");
    std::string type;
    if (!(type_file >> type)) return SettingsMmcDeviceType::Unknown;
    if (type == "SD") return SettingsMmcDeviceType::Sd;
    if (type == "MMC") return SettingsMmcDeviceType::Mmc;
    return SettingsMmcDeviceType::Unknown;
}

/* The root's disk: its mount source, else (a "/dev/root" without a node) the device number of / itself */
std::optional<SettingsRootDevice> native_root_device_probe(const std::string &source)
{
    if (auto found = SettingsStorageModel::probe_root_device(source, "/sys")) return found;
    struct stat root {};
    if (stat("/", &root) != 0) return std::nullopt;
    std::error_code error;
    const std::filesystem::path device = std::filesystem::canonical(
        "/sys/dev/block/" + std::to_string(major(root.st_dev)) + ":" + std::to_string(minor(root.st_dev)), error);
    if (error) return std::nullopt;
    return SettingsStorageModel::probe_root_device("/dev/" + device.filename().string(), "/sys");
}
#endif

} // namespace

SettingsStorageInfo SettingsStorageModel::read() noexcept
{
#if defined(CONFIG_APPLAUNCH_TARGET_SD_MMC_STORAGE) && defined(__linux__)
    try {
        const char *hook = std::getenv("APPLAUNCH_STORAGE_MOUNTS");
        std::ifstream mounts(hook ? hook : "/proc/self/mounts");
        if (!mounts) return {};
        std::ostringstream contents;
        contents << mounts.rdbuf();
        if (!mounts.good() && !mounts.eof()) return {};
        SettingsStorageInfo sd = read_from_mount_table(
            contents.str(), native_space_probe, native_device_type_probe);
        if (sd.valid) return sd;
        return read_root_from_mount_table(contents.str(), native_space_probe, native_root_device_probe);
    } catch (...) {
        return {};
    }
#else
    return {};
#endif
}

SettingsStorageInfo SettingsStorageModel::read_from_mount_table(
    std::string_view mount_table,
    const SpaceProbe &space_probe,
    const DeviceTypeProbe &device_type_probe) noexcept
{
    if (!space_probe || !device_type_probe) return {};

    try {
        SettingsStorageInfo best;
        for (const MountEntry &mount : mmc_mounts(mount_table)) {
            if (device_type_probe(mount.device_name) != SettingsMmcDeviceType::Sd)
                continue;
            std::uintmax_t capacity = 0;
            std::uintmax_t available = 0;
            if (!space_probe(mount.path, capacity, available) || capacity == 0 ||
                available > capacity)
                continue;

            SettingsStorageInfo candidate;
            candidate.valid = true;
            candidate.total_bytes = capacity;
            candidate.available_bytes = available;
            candidate.mount_path = mount.path;
            if (!best.valid || candidate.total_bytes > best.total_bytes ||
                (candidate.total_bytes == best.total_bytes &&
                 candidate.available_bytes > best.available_bytes))
                best = std::move(candidate);
        }
        return best;
    } catch (...) {
        return {};
    }
}

SettingsStorageInfo SettingsStorageModel::read_root_from_mount_table(
    std::string_view mount_table,
    const SpaceProbe &space_probe,
    const RootDeviceProbe &root_device_probe) noexcept
{
    if (!space_probe || !root_device_probe) return {};
    try {
        const std::string source = root_source(mount_table);
        if (source.empty()) return {};
        const std::optional<SettingsRootDevice> device = root_device_probe(source);
        if (!device || device->disk.empty()) return {};
        std::uintmax_t capacity = 0;
        std::uintmax_t available = 0;
        if (!space_probe("/", capacity, available) || capacity == 0 || available > capacity) return {};
        SettingsStorageInfo info;
        info.valid = true;
        info.total_bytes = capacity;
        info.available_bytes = available;
        info.mount_path = "/";
        info.kind = device->kind;
        info.device = device->disk;
        info.model = device->model;
        return info;
    } catch (...) {
        return {};
    }
}

std::string SettingsStorageModel::disk_of(std::string_view name)
{
    if (const auto mmc = mmc_block_device_name(name)) return *mmc;          /* mmcblk0p2 -> mmcblk0 */
    if (starts_with(name, "nvme")) {                                          /* nvme0n1p2 -> nvme0n1 */
        const std::size_t namespace_at = name.find('n', 4);
        const std::size_t partition = name.find('p', 4);
        if (namespace_at != std::string_view::npos && partition != std::string_view::npos &&
            partition > namespace_at && all_decimal(name.substr(partition + 1)))
            return std::string(name.substr(0, partition));
        return std::string(name);
    }
    for (const std::string_view prefix : {"sd", "vd", "hd", "xvd"}) {        /* sda2 -> sda */
        if (!starts_with(name, prefix) || name.size() <= prefix.size()) continue;
        std::size_t end = name.size();
        while (end > prefix.size() && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
        if (end > prefix.size() && end < name.size()) return std::string(name.substr(0, end));
        return std::string(name);
    }
    return std::string(name);
}

std::optional<SettingsRootDevice> SettingsStorageModel::probe_root_device(const std::string &source,
                                                                          const std::string &sysfs_root)
{
    namespace fs = std::filesystem;
    /* overlay, tmpfs, rootfs, a network share (server:/path): no block device */
    if (source.empty() || source.front() != '/') return std::nullopt;
    std::error_code error;
    const fs::path resolved = fs::canonical(source, error);
    std::string name = (error ? fs::path(source) : resolved).filename().string();
    const fs::path blocks = fs::path(sysfs_root) / "class" / "block";
    if (name.empty() || !fs::exists(blocks / name, error)) return std::nullopt;
    /* the device mapper (LUKS, LVM): the device under it, a few levels at most */
    for (int depth = 0; depth < 4 && starts_with(name, "dm-"); ++depth) {
        fs::directory_iterator slaves(blocks / name / "slaves", error);
        if (error || slaves == fs::directory_iterator()) return std::nullopt;
        name = slaves->path().filename().string();
    }
    /* a partition: its disk (the parent of its sysfs directory, else by its name) */
    std::string disk = name;
    if (fs::exists(blocks / name / "partition", error)) {
        const fs::path parent = fs::canonical(blocks / name, error).parent_path();
        disk = !error && fs::exists(blocks / parent.filename(), error) ? parent.filename().string() : disk_of(name);
    }
    if (!fs::exists(blocks / disk, error)) return std::nullopt;

    SettingsRootDevice device;
    device.disk = disk;
    device.model = first_line(blocks / disk / "device" / "model");
    if (starts_with(disk, "mmcblk")) {
        const std::string type = first_line(blocks / disk / "device" / "type");
        device.kind = type == "SD" ? SettingsStorageKind::SdCard
                    : type == "MMC" ? SettingsStorageKind::Emmc
                                    : SettingsStorageKind::Disk;
    } else if (starts_with(disk, "vd") || starts_with(disk, "xvd")) {
        device.kind = SettingsStorageKind::Disk; /* a virtual disk: its rotational flag means nothing */
    } else {
        const std::string rotational = first_line(blocks / disk / "queue" / "rotational");
        device.kind = rotational == "0" ? SettingsStorageKind::Ssd
                    : rotational == "1" ? SettingsStorageKind::HardDisk
                                        : SettingsStorageKind::Disk;
    }
    return device;
}

std::string SettingsStorageModel::device_line(const SettingsStorageInfo &info)
{
    std::string label;
    switch (info.kind) {
    case SettingsStorageKind::SdCard: label = "SD card"; break;
    case SettingsStorageKind::Emmc: label = "eMMC"; break;
    case SettingsStorageKind::Ssd: label = "SSD"; break;
    case SettingsStorageKind::HardDisk: label = "Hard disk"; break;
    case SettingsStorageKind::Disk: label = "Disk"; break;
    }
    if (info.device.empty()) return label; /* an SD card found among the mounts: the line it always had */
    label += " " + info.device;
    if (!info.model.empty()) label += " (" + info.model + ")";
    return label;
}

std::string SettingsStorageModel::format_bytes(std::uintmax_t bytes)
{
    static constexpr std::array<const char *, 5> units = {
        "B", "KiB", "MiB", "GiB", "TiB",
    };

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream output;
    if (unit == 0)
        output << bytes;
    else
        output << std::fixed << std::setprecision(1) << value;
    output << ' ' << units[unit];
    return output.str();
}
