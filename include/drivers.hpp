#pragma once
#include "core.hpp"
#include <array>

namespace luminami {
struct DriverSource {
    const char* name;
    const char* sha256;
    unsigned resource;
};

inline constexpr std::array<DriverSource, 2> DriverSources = {
    {{"amigendrv64.sys", "ffc72f0bde21ba20aa97bee99d9e96870e5aa40cce9884e44c612757f939494f", 101},
     {"amifldrv64.sys", "e7cbfb16261de1c7f009431d374d90e9eb049ba78246e38bc4c8b9e06f324b6f", 102}}};

std::filesystem::path driver_state_directory();
const DriverSource& verify_ami_driver(const Bytes& bytes);
Bytes provided_ami_driver(const DriverSource& source);
Json remember_ami_driver(const std::filesystem::path& driver,
                         const std::filesystem::path& config_directory = {});
Json ami_driver_status(const std::filesystem::path& config_directory = {});
std::filesystem::path resolve_ami_driver(const std::filesystem::path& supplied = {},
                                         const std::filesystem::path& config_directory = {});
Json install_ami_drivers(const std::filesystem::path& directory = {},
                         const std::filesystem::path& config_directory = {});
} // namespace luminami
