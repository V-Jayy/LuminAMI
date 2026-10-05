#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "drivers.hpp"

namespace luminami {
namespace {
std::wstring environment(const wchar_t* name) {
    auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size)
        return {};
    std::wstring value(size, L'\0');
    auto written = GetEnvironmentVariableW(name, value.data(), size);
    if (!written || written >= size)
        throw Error("Cannot read driver configuration directory");
    value.resize(written);
    return value;
}

std::filesystem::path state_directory(const std::filesystem::path& supplied) {
    return supplied.empty() ? driver_state_directory() : std::filesystem::absolute(supplied);
}
} // namespace

std::filesystem::path driver_state_directory() {
    auto override = environment(L"LUMINAMI_CONFIG_DIR");
    if (!override.empty()) {
        std::filesystem::path path(override);
        if (!path.is_absolute())
            throw Error("LUMINAMI_CONFIG_DIR must be an absolute path");
        return path;
    }
    auto local = environment(L"LOCALAPPDATA");
    if (local.empty())
        throw Error("LOCALAPPDATA is unavailable; set LUMINAMI_CONFIG_DIR to an absolute directory");
    return std::filesystem::path(local) / L"LuminAMI";
}

const DriverSource& verify_ami_driver(const Bytes& bytes) {
    auto hash = sha256(bytes);
    for (const auto& source : DriverSources) {
        if (hash == source.sha256)
            return source;
    }
    throw Error("Driver SHA256 does not match either supported AMI driver; see docs/DRIVERS.md");
}

Bytes provided_ami_driver(const DriverSource& source) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(source.resource), RT_RCDATA);
    auto size = resource ? SizeofResource(module, resource) : 0;
    auto loaded = resource ? LoadResource(module, resource) : nullptr;
    auto data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    if (!data || !size || size > 2 * 1024 * 1024)
        throw Error("Bundled AMI driver resource is unavailable");
    Bytes bytes(data, data + size);
    if (sha256(bytes) != source.sha256)
        throw Error(std::string("Bundled driver SHA256 mismatch: ") + source.name);
    return bytes;
}

Json remember_ami_driver(const std::filesystem::path& driver, const std::filesystem::path& config_directory) {
    const auto& source = verify_ami_driver(read_file(driver));
    auto absolute = std::filesystem::absolute(driver).lexically_normal();
    auto state = state_directory(config_directory);
    std::filesystem::create_directories(state);
    auto config = state / L"driver.json";
    write_json(config, {{"format", "luminami-driver-config-v1"}, {"driver", utf8(absolute.wstring())}}, true);
    return {{"ok", true},
            {"configured", true},
            {"driver", utf8(absolute.wstring())},
            {"kind", source.name},
            {"sha256", source.sha256},
            {"config", utf8(config.wstring())},
            {"writes_firmware", false},
            {"loads_driver", false}};
}

Json ami_driver_status(const std::filesystem::path& config_directory) {
    auto config = state_directory(config_directory) / L"driver.json";
    Json result = {{"ok", true},
                   {"configured", false},
                   {"config", utf8(config.wstring())},
                   {"writes_firmware", false},
                   {"loads_driver", false}};
    if (!std::filesystem::exists(config))
        return result;
    try {
        auto settings = Json::parse(read_file(config));
        if (settings.value("format", "") != "luminami-driver-config-v1")
            throw Error("Unsupported driver configuration");
        std::filesystem::path driver(wide(settings.at("driver").get<std::string>()));
        if (!driver.is_absolute())
            throw Error("Saved driver path must be absolute");
        const auto& source = verify_ami_driver(read_file(driver));
        result["configured"] = true;
        result["driver"] = utf8(driver.wstring());
        result["kind"] = source.name;
        result["sha256"] = source.sha256;
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    return result;
}

std::filesystem::path resolve_ami_driver(const std::filesystem::path& supplied,
                                         const std::filesystem::path& config_directory) {
    if (!supplied.empty()) {
        auto saved = remember_ami_driver(supplied, config_directory);
        return std::filesystem::path(wide(saved.at("driver").get<std::string>()));
    }
    auto status = ami_driver_status(config_directory);
    if (!status.at("configured").get<bool>()) {
        auto detail = status.value("error", std::string{});
        throw Error("No verified AMI driver is configured. Run LuminAMI install-drivers or use-driver "
                    "--driver PATH." +
                    (detail.empty() ? "" : " " + detail));
    }
    return std::filesystem::path(wide(status.at("driver").get<std::string>()));
}

Json install_ami_drivers(const std::filesystem::path& directory,
                         const std::filesystem::path& config_directory) {
    auto destination = directory.empty() ? state_directory(config_directory) / L"drivers"
                                         : std::filesystem::absolute(directory);
    std::array<Bytes, DriverSources.size()> pending;
    // Verify all resources and existing files before publishing or changing the saved choice.
    for (size_t i = 0; i < DriverSources.size(); ++i) {
        const auto& source = DriverSources[i];
        auto file = destination / source.name;
        bool exists = std::filesystem::exists(file);
        auto bytes = exists ? read_file(file) : provided_ami_driver(source);
        if (sha256(bytes) != source.sha256)
            throw Error(std::string("SHA256 mismatch for ") + source.name + "; nothing was activated");
        if (!exists)
            pending[i] = std::move(bytes);
    }
    std::filesystem::create_directories(destination);
    Json files = Json::array();
    for (size_t i = 0; i < DriverSources.size(); ++i) {
        const auto& source = DriverSources[i];
        auto file = destination / source.name;
        if (!pending[i].empty())
            write_file(file, pending[i]);
        if (sha256(read_file(file)) != source.sha256)
            throw Error("Extracted driver changed before setup completed");
        files.push_back({{"driver", utf8(file.wstring())},
                         {"sha256", source.sha256},
                         {"extracted", !pending[i].empty()}});
    }
    auto result = remember_ami_driver(destination / DriverSources[0].name, config_directory);
    result["drivers"] = std::move(files);
    return result;
}
} // namespace luminami
