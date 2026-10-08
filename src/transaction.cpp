#include "core.hpp"
#include <set>
#include <map>
#include <algorithm>

namespace luminami {
namespace {
struct PreparedVariable {
    std::string name, guid;
    Bytes before, after;
};
std::vector<PreparedVariable> decode_plan(const Json& plan) {
    if (plan.at("format") != "luminami-import-plan-v1" || !plan.at("variables").is_array() ||
        plan["variables"].size() > 1024)
        throw Error("Invalid import transaction format");
    std::vector<PreparedVariable> variables;
    std::set<std::string> identities;
    for (const auto& v : plan["variables"]) {
        PreparedVariable p{v.at("name").get<std::string>(), v.at("guid").get<std::string>(),
                           unhex(v.at("before_data").get<std::string>()),
                           unhex(v.at("after_data").get<std::string>())};
        auto guid = guid_string(guid_bytes(p.guid), 0);
        if (guid != p.guid || !identities.insert(guid + ":" + p.name).second)
            throw Error("Repeated or noncanonical variable identity");
        if (p.before.size() != p.after.size() || p.before.empty() || p.before == p.after ||
            v.at("size") != p.before.size() || v.at("before_sha256") != sha256(p.before) ||
            v.at("after_sha256") != sha256(p.after) || v.at("attributes") != 7)
            throw Error("Import transaction payload, checksum or attributes are invalid");
        // Check transport constraints now, before any journal or callback side effect.
        (void)protocol::set_variable(0x100000, p.name, p.guid, 7, p.after);
        variables.push_back(std::move(p));
    }
    if (!plan.at("patches").is_array() || plan["patches"].size() > 100000)
        throw Error("Invalid transaction patch list");
    std::map<std::string, size_t> by_identity;
    std::vector<std::set<size_t>> covered(variables.size());
    for (size_t i = 0; i < variables.size(); ++i)
        by_identity.emplace(variables[i].guid + ":" + variables[i].name, i);
    for (const auto& patch : plan["patches"]) {
        auto key = patch.at("guid").get<std::string>() + ":" + patch.at("variable").get<std::string>();
        auto it = by_identity.find(key);
        if (it == by_identity.end())
            throw Error("Patch has no prepared variable payload");
        const auto& p = variables[it->second];
        auto offset = patch.at("offset").get<size_t>(), width = patch.at("width").get<size_t>();
        auto encoding = patch.value("encoding", std::string{});
        if (!encoding.empty() && encoding != "utf16")
            throw Error("Unknown transaction field encoding");
        if (patch.value("encoding", std::string{}) == "utf16") {
            auto before = unhex(patch.at("before_data").get<std::string>()),
                 after = unhex(patch.at("after_data").get<std::string>());
            if (!width || width % 2 || width > 510 || before.size() != width || after.size() != width ||
                before == after || offset > p.before.size() || width > p.before.size() - offset ||
                decode_utf16(before) != patch.at("before").get<std::string>() ||
                decode_utf16(after) != patch.at("after").get<std::string>() ||
                after != encode_utf16(patch.at("after").get<std::string>(), width / 2) ||
                !std::equal(before.begin(), before.end(),
                            p.before.begin() + static_cast<ptrdiff_t>(offset)) ||
                !std::equal(after.begin(), after.end(), p.after.begin() + static_cast<ptrdiff_t>(offset)))
                throw Error("String patch disagrees with prepared variable bytes");
        } else if (!width || width > 8 || patch.at("before") == patch.at("after") ||
                   read_le(p.before, offset, width) != patch.at("before").get<uint64_t>() ||
                   read_le(p.after, offset, width) != patch.at("after").get<uint64_t>())
            throw Error("Patch values disagree with the prepared variable bytes");
        for (size_t i = 0; i < width; ++i)
            covered[it->second].insert(offset + i);
    }
    for (size_t i = 0; i < variables.size(); ++i)
        for (size_t at = 0; at < variables[i].before.size(); ++at)
            if (variables[i].before[at] != variables[i].after[at] && !covered[i].count(at))
                throw Error("Prepared payload changes bytes outside its reviewed question patches");
    return variables;
}
Bytes read_value(const VariableOperations& operations, const PreparedVariable& p) {
    auto v = operations.read(p.name, p.guid);
    if (v.at("name") != p.name || v.at("guid") != p.guid || v.at("attributes") != 7)
        throw Error("Live variable identity or attributes differ: " + p.name);
    auto bytes = unhex(v.at("data").get<std::string>());
    if (bytes.size() != p.before.size())
        throw Error("Live variable size differs: " + p.name);
    return bytes;
}
} // namespace
void check_amd_write_policy(const Json& catalog, const Json& plan, const VariableOperations& operations) {
    bool amd_write = false;
    for (const auto& v : plan.at("variables")) {
        const auto guid = v.at("guid").get<std::string>();
        // CBS, PBS and AOD stores, identified by vendor GUID rather than CPU or board branding.
        if (guid == "3a997502-647a-4c82-998e-52ef9486a247" ||
            guid == "a339d746-f678-49b3-9fc7-54ce0f9df226" || guid == "5ed15dc0-edef-4161-9151-6014c4cc630c")
            amd_write = true;
    }
    if (!amd_write)
        return;
    for (const auto& q : catalog.at("questions")) {
        if (q.value("name", "") != "AMD Variable Protection" || !q.contains("variable") ||
            q.value("width", 0) != 1 || q.value("opcode", 0) != 5 ||
            q["variable"].value("kind", "") != "buffer")
            continue;
        bool disabled = false, enabled = false;
        for (const auto& option : q.at("options")) {
            disabled |= option.value("name", "") == "Disabled" && option.at("value") == 0;
            enabled |= option.value("name", "") == "Enabled" && option.at("value") == 1;
        }
        if (!disabled || !enabled)
            continue;
        const auto& store = q.at("variable");
        const auto name = store.at("name").get<std::string>(), guid = store.at("guid").get<std::string>();
        const auto live = operations.read(name, guid);
        if (live.at("name") != name || live.at("guid") != guid || live.at("attributes") != 7)
            throw Error("AMD protection variable identity or attributes differ");
        const auto bytes = unhex(live.at("data").get<std::string>());
        if (bytes.size() != store.at("size").get<size_t>())
            throw Error("AMD protection variable size differs");
        const auto value = read_le(bytes, q.at("offset").get<size_t>(), 1);
        if (value == 1)
            throw FirmwareWriteBlocked(
                "AMD Variable Protection is Enabled in BIOS (AMD PBS). Firmware blocks CBS/PBS/AOD "
                "imports from Windows. Set AMD Variable Protection to Disabled in BIOS, save and "
                "reboot, then export fresh settings before importing. No BIOS settings were written.");
        if (value != 0)
            throw Error("Unknown AMD Variable Protection value; read BIOS again");
    }
}
void write_runtime_fallback(const Json& before, const Bytes& data, const VariableOperations& ami,
                            const VariableOperations& runtime) {
    const auto name = before.at("name").get<std::string>(), guid = before.at("guid").get<std::string>();
    const auto baseline = unhex(before.at("data").get<std::string>());
    if (name.empty() || before.at("attributes") != 7 || baseline.empty() || data.size() != baseline.size())
        throw Error("Runtime fallback requires an existing ordinary variable of unchanged size");
    const auto matches = [&](const Json& current, const Bytes& expected) {
        return current.at("name") == name && current.at("guid") == guid && current.at("attributes") == 7 &&
               unhex(current.at("data").get<std::string>()) == expected;
    };
    // A rejected AMI request can still have side effects. Leave those to the
    // existing transaction rollback, rather than trying another write route.
    if (!matches(ami.read(name, guid), baseline))
        throw Error("AMI variable changed after the rejected write: " + name);
    if (!matches(runtime.read(name, guid), baseline))
        throw Error("Windows and AMI variable baselines disagree: " + name);
    runtime.write(name, guid, 7, data);
    // Verify through AMI as well as Windows; a runtime cache is not NVRAM proof.
    if (!matches(ami.read(name, guid), data) || !matches(runtime.read(name, guid), data))
        throw Error("Windows UEFI write readback mismatch: " + name);
}

Json prepare_numlock_restore(const Json& staged, const Json& live) {
    if (staged.at("format") != "luminami-import-journal-v1" || staged.at("stage") != "committed" ||
        staged.at("ok") != true || staged.at("applied") != Json::array({0}) ||
        staged.at("attempted") != Json::array({0}))
        throw Error("Journal does not describe a verified staged NumLock test");
    auto plan = staged.at("plan");
    const auto variables = decode_plan(plan);
    if (variables.size() != 1 || plan.at("patches").size() != 1)
        throw Error("NumLock restore requires exactly one reviewed patch");
    const auto& p = variables[0];
    const auto patch = plan["patches"][0];
    if (p.name != "Setup" || p.guid != "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9" ||
        patch.at("name") != "Bootup NumLock State" || patch.at("offset") != 0 || patch.at("width") != 1 ||
        patch.at("before") != 1 || patch.at("after") != 0 || !patch.value("encoding", std::string{}).empty())
        throw Error("Journal is not the reviewed NumLock On -> Off test");
    const auto& test = plan.at("numlock_reboot_test");
    auto boot = test.at("boot_identifier").get<std::string>();
    if (guid_string(guid_bytes(boot), 0) != boot || test.at("baseline") != 1 ||
        test.at("expected_after_reboot") != 0)
        throw Error("Invalid NumLock reboot metadata");
    if (live.at("name") != p.name || live.at("guid") != p.guid || live.at("attributes") != 7)
        throw Error("Live restore variable identity or attributes differ");
    auto current = unhex(live.at("data").get<std::string>()), target = current;
    if (current.size() != p.before.size() || current[0] > 1)
        throw Error("Live NumLock field size or value differs");
    // A reboot may update other Setup fields. Restore only the reviewed byte
    // using a fresh full-store baseline, preserving every other live byte.
    target[0] = 1;
    plan["patches"] = Json::array();
    plan["variables"] = Json::array();
    if (target != current) {
        auto reverse = patch;
        reverse["before"] = 0;
        reverse["after"] = 1;
        plan["patches"].push_back(reverse);
        plan["variables"].push_back({{"name", p.name},
                                     {"guid", p.guid},
                                     {"attributes", 7},
                                     {"size", current.size()},
                                     {"before_data", hex(current)},
                                     {"after_data", hex(target)},
                                     {"before_sha256", sha256(current)},
                                     {"after_sha256", sha256(target)}});
    }
    (void)decode_plan(plan);
    return plan;
}
Json execute_import(const Json& plan, const std::filesystem::path& journal,
                    const VariableOperations& operations, bool restore_after) {
    if (std::filesystem::exists(journal))
        throw Error("Import journal already exists");
    if (!operations.read || !operations.write)
        throw Error("Import requires a complete variable transport");
    const auto variables = decode_plan(plan);
    // Every baseline is checked before the first write, rather than discovering
    // a stale later variable after earlier variables have already been changed.
    for (const auto& p : variables)
        if (read_value(operations, p) != p.before)
            throw Error("Live variable changed since capture: " + p.name);
    Json receipt = {{"format", "luminami-import-journal-v1"},
                    {"plan", plan},
                    {"stage", "prepared"},
                    {"writes_attempted", false},
                    {"applied", Json::array()},
                    {"attempted", Json::array()},
                    {"restoration", Json::array()},
                    {"reboot_validated", false}};
    write_json(journal, receipt);
    std::vector<size_t> attempted;
    std::string failure;
    try {
        for (size_t i = 0; i < variables.size(); ++i) {
            const auto& p = variables[i];
            if (read_value(operations, p) != p.before)
                throw Error("Variable changed immediately before write: " + p.name);
            receipt["stage"] = "write_pending";
            receipt["writes_attempted"] = true;
            receipt["attempted"].push_back(i);
            write_json(journal, receipt, true);
            // Include a failing request in rollback: an error return does not
            // prove that the firmware left the target entirely unchanged.
            attempted.push_back(i);
            operations.write(p.name, p.guid, 7, p.after);
            if (read_value(operations, p) != p.after)
                throw Error("Import readback mismatch: " + p.name);
            receipt["applied"].push_back(i);
            receipt["stage"] = "readback_verified";
            write_json(journal, receipt, true);
        }
    } catch (const std::exception& e) {
        failure = e.what();
        receipt["error"] = failure;
    }
    if (!failure.empty() || restore_after) {
        bool restored = true;
        for (auto it = attempted.rbegin(); it != attempted.rend(); ++it) {
            const auto& p = variables[*it];
            Json item = {{"index", *it}, {"name", p.name}, {"restored", false}};
            try {
                auto current = read_value(operations, p), target = current;
                for (size_t i = 0; i < p.before.size(); ++i)
                    if (p.before[i] != p.after[i]) {
                        // Preserve unrelated changes made after this transaction.
                        // Refuse to overwrite a conflicting new value in our field.
                        if (current[i] != p.before[i] && current[i] != p.after[i])
                            throw Error("Rollback field conflict: " + p.name);
                        target[i] = p.before[i];
                    }
                if (target != current)
                    operations.write(p.name, p.guid, 7, target);
                if (read_value(operations, p) != target)
                    throw Error("Rollback readback mismatch: " + p.name);
                item["restored"] = true;
                item["complete_baseline_matches"] = target == p.before;
            } catch (const std::exception& e) {
                restored = false;
                item["error"] = e.what();
            }
            receipt["restoration"].push_back(item);
        }
        receipt["rollback_verified"] = restored;
        receipt["stage"] = restored ? "restored" : "restore_failed";
        receipt["ok"] = failure.empty() && restored;
        // Attempt every restoration even when an earlier restore failed. The
        // durable original plan remains usable if this final journal write fails.
        write_json(journal, receipt, true);
        if (!restored)
            throw Error("Import restoration incomplete; inspect the recovery journal");
        if (!failure.empty())
            throw Error("Import failed; changed fields restored: " + failure);
    } else {
        receipt["stage"] = variables.empty() ? "unchanged" : "committed";
        receipt["ok"] = true;
        write_json(journal, receipt, true);
    }
    return receipt;
}
} // namespace luminami
