// Offline tests for the optional BIOS administrator password support. Nothing
// here loads a driver, touches firmware, or knows any real password: a
// SIMULATED firmware stands in for the board, with the behaviour AMI's public
// SCE documentation describes (a protected variable refuses with
// EFI_SECURITY_VIOLATION until the administrator password is accepted, the
// firmware counts wrong attempts, and eventually refuses further tries).
#define NOMINMAX
#include <Windows.h>
#include "core.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace luminami;
namespace {
int checks = 0;
// Everything the engine says or writes during these tests ends up here, and at
// the end no password may appear in it.
std::string everything;
void check(bool success, const std::string& message) {
    ++checks;
    if (!success)
        throw Error("Test failed: " + message);
}
void note(const std::string& text) {
    everything += text;
    everything += '\n';
}
template <class Exception = std::exception, class F>
std::string caught(F&& operation, const std::string& message) {
    try {
        operation();
    } catch (const Exception& error) {
        note(error.what());
        return error.what();
    }
    check(false, message + " (no exception)");
    return {};
}
SecretBytes secret(const std::string& value) {
    return SecretBytes(Bytes(value.begin(), value.end()));
}
std::string slurp(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    return std::string(bytes.begin(), bytes.end());
}

const std::string Guid = "12345678-1234-5678-9012-123456789012";
const std::string GoodPassword = "Zq7!hunter-Tw9"; // distinctive, so a leak is easy to find
const std::string WrongPassword = "Wr0ng!guess-Kx4";

struct SimulatedFirmware {
    std::map<std::string, Bytes> nv{{"Setup", {0x01, 0x64, 0x00, 0x00}}, {"Other", {0x01, 0x02, 0x03, 0x04}}};
    std::set<std::string> protected_names{"Setup", "Other"};
    bool locked = true;
    std::string password = GoodPassword;
    size_t max_length = 20;
    int attempts_allowed = 3;
    int failures = 0;
    bool lockout = false;
    bool report_remaining = true;
    bool echo_password_in_reason = false;
    bool throw_with_password = false;
    int submissions = 0, writes = 0, reads = 0;

    Json read(const std::string& name, const std::string& guid) {
        ++reads;
        if (locked && protected_names.count(name))
            throw SetupPasswordRequired("AMI GetVariable failed for " + name + " (firmware status 154)",
                                        0x9a);
        return {{"name", name}, {"guid", guid}, {"attributes", 7}, {"data", hex(nv.at(name))}};
    }
    void write(const std::string& name, const std::string&, uint32_t attributes, const Bytes& data) {
        if (locked && protected_names.count(name))
            throw SetupPasswordRequired(
                "AMI SetVariable rejected for " + name +
                    " (EFI_SECURITY_VIOLATION, firmware status 154 / 0x9A); simulated",
                0x9a);
        check(attributes == 7, "simulated write keeps NV/BS/RT attributes");
        ++writes;
        nv.at(name) = data;
    }
    PasswordUnlockOutcome unlock(const SecretBytes& attempt) {
        ++submissions;
        const std::string text(attempt.bytes().begin(), attempt.bytes().end());
        if (throw_with_password)
            throw Error("simulated transport fault while sending " + text);
        PasswordUnlockOutcome outcome;
        outcome.submitted = true;
        if (lockout) {
            outcome.locked_out = true;
            outcome.remaining_attempts = 0;
            outcome.reason = "the firmware refuses further attempts until restart";
            return outcome;
        }
        const bool right = text == password && text.size() <= max_length;
        if (right) {
            locked = false;
            failures = 0;
            outcome.accepted = true;
            return outcome;
        }
        ++failures;
        outcome.reason = echo_password_in_reason ? "firmware rejected '" + text + "'" : "password rejected";
        if (report_remaining)
            outcome.remaining_attempts = std::max(0, attempts_allowed - failures);
        if (failures >= attempts_allowed) {
            lockout = true;
            outcome.locked_out = true;
        }
        return outcome;
    }
    VariableOperations operations(bool offer_unlock = true) {
        VariableOperations ops{[this](const std::string& n, const std::string& g) { return read(n, g); },
                               [this](const std::string& n, const std::string& g, uint32_t a,
                                      const Bytes& d) { write(n, g, a, d); }};
        if (offer_unlock)
            ops.unlock_setup_password = [this](const SecretBytes& s) { return unlock(s); };
        return ops;
    }
};

Json two_variable_plan() {
    Json plan = {
        {"format", "luminami-import-plan-v1"}, {"variables", Json::array()}, {"patches", Json::array()}};
    auto add = [&](const std::string& name, const Bytes& before, const Bytes& after, size_t offset) {
        plan["variables"].push_back({{"name", name},
                                     {"guid", Guid},
                                     {"attributes", 7},
                                     {"size", before.size()},
                                     {"before_data", hex(before)},
                                     {"after_data", hex(after)},
                                     {"before_sha256", sha256(before)},
                                     {"after_sha256", sha256(after)}});
        plan["patches"].push_back({{"variable", name},
                                   {"guid", Guid},
                                   {"offset", offset},
                                   {"width", 1},
                                   {"before", before[offset]},
                                   {"after", after[offset]}});
    };
    add("Setup", {0x01, 0x64, 0x00, 0x00}, {0x00, 0x64, 0x00, 0x00}, 0);
    add("Other", {0x01, 0x02, 0x03, 0x04}, {0x01, 0x02, 0x09, 0x04}, 2);
    return plan;
}
} // namespace

int main() {
    // Capture everything printed during the run so the leak scan covers it.
    std::ostringstream captured_out, captured_err;
    auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
    auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
    int result = 0;
    try {
        const auto workspace = std::filesystem::temp_directory_path() /
                               (L"luminami-password-tests-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::remove_all(workspace);
        std::filesystem::create_directories(workspace);

        // --- Nothing supplied: the engine behaves as it always did. -----------------------------
        {
            SimulatedFirmware firmware;
            PasswordAttempt none; // no password
            check(!none.supplied() && password_report(none).is_null(),
                  "No password: nothing supplied, nothing reported");
            auto ops = with_password_unlock(firmware.operations(), none);
            auto refusal = caught<SetupPasswordRequired>([&] { ops.read("Setup", Guid); },
                                                         "A protected read is refused without a password");
            check(refusal == "AMI GetVariable failed for Setup (firmware status 154)",
                  "The refusal text is exactly the firmware text, unchanged");
            check(firmware.submissions == 0, "No password is ever submitted when none was supplied");
            PasswordAttempt empty(SecretBytes(Bytes{}));
            check(!empty.supplied(), "An empty password counts as none");
            caught<SetupPasswordRequired>(
                [&] { with_password_unlock(firmware.operations(), empty).read("Setup", Guid); },
                "An empty password does not unlock");
            check(firmware.submissions == 0, "An empty password is never submitted");
            check(password_submission_supported(),
                  "This build contains the reconstructed AMI password channel");
        }

        // --- Correct password: one submission, then the read and the write both work. -----------
        {
            SimulatedFirmware firmware;
            PasswordAttempt attempt(secret(GoodPassword));
            auto ops = with_password_unlock(firmware.operations(), attempt);
            auto value = ops.read("Setup", Guid);
            check(value.at("data") == "01640000", "A correct password lets the protected read through");
            check(firmware.submissions == 1 && attempt.spent(), "The password was submitted exactly once");
            ops.write("Other", Guid, 7, {0x01, 0x02, 0x09, 0x04});
            ops.read("Other", Guid);
            check(firmware.submissions == 1, "Later protected calls do not submit it again");
            check(firmware.writes == 1,
                  "The only NVRAM write is the caller's own write: none to verify the password");
            auto report = password_report(attempt);
            check(report["supplied"] == true && report["submitted"] == true && report["accepted"] == true &&
                      report["locked_out"] == false && report["needed"] == true,
                  "The report says the password was submitted and accepted");
            note(report.dump());
        }

        // --- A password that is supplied but never needed is never submitted. -------------------
        {
            SimulatedFirmware firmware;
            firmware.locked = false;
            PasswordAttempt attempt(secret(GoodPassword));
            auto ops = with_password_unlock(firmware.operations(), attempt);
            ops.read("Setup", Guid);
            check(firmware.submissions == 0 && !attempt.spent(), "An unneeded password is not submitted");
            check(password_report(attempt)["needed"] == false, "The report says it was not needed");
        }

        // --- Wrong password: reported once, with remaining attempts, never retried. -------------
        {
            SimulatedFirmware firmware;
            PasswordAttempt attempt(secret(WrongPassword));
            auto ops = with_password_unlock(firmware.operations(), attempt);
            SetupPasswordFailure* seen = nullptr;
            SetupPasswordFailure copy("", {});
            try {
                ops.read("Setup", Guid);
            } catch (const SetupPasswordFailure& error) {
                copy = error;
                seen = &copy;
                note(error.what());
            }
            check(seen != nullptr, "A wrong password is reported as a password failure");
            check(!seen->outcome.accepted && seen->outcome.submitted && !seen->outcome.unsupported,
                  "The failure says it was submitted and rejected");
            check(seen->outcome.remaining_attempts && *seen->outcome.remaining_attempts == 2,
                  "The firmware's remaining-attempts count is passed on");
            check(std::string(seen->what()).find("2 attempt(s) remaining") != std::string::npos &&
                      std::string(seen->what()).find("does not retry") != std::string::npos,
                  "The message says how many attempts remain and that nothing retries");
            check(firmware.submissions == 1, "One attempt was made");
            // The same operation again, and other operations, must not submit again.
            caught<SetupPasswordRequired>([&] { ops.read("Setup", Guid); },
                                          "A second protected call is refused again");
            caught<SetupPasswordRequired>([&] { ops.write("Setup", Guid, 7, {0, 0x64, 0, 0}); },
                                          "A protected write is refused again");
            check(firmware.submissions == 1, "A wrong password is never retried automatically");
            check(firmware.failures == 1 && firmware.writes == 0,
                  "No further attempt or write reached the firmware");
        }

        // --- Lockout is reported, and a fresh run does not push through it. ---------------------
        {
            SimulatedFirmware firmware;
            firmware.attempts_allowed = 1;
            PasswordAttempt first(secret(WrongPassword));
            SetupPasswordFailure locked("", {});
            try {
                with_password_unlock(firmware.operations(), first).read("Setup", Guid);
            } catch (const SetupPasswordFailure& error) {
                locked = error;
                note(error.what());
            }
            check(locked.outcome.locked_out && locked.outcome.remaining_attempts &&
                      *locked.outcome.remaining_attempts == 0,
                  "Lockout is reported with zero attempts remaining");
            check(std::string(locked.what()).find("restart the computer") != std::string::npos,
                  "The lockout message tells the person what to do");
            PasswordAttempt second(secret(GoodPassword)); // even the right one: the firmware is locked out
            SetupPasswordFailure again("", {});
            try {
                with_password_unlock(firmware.operations(), second).read("Setup", Guid);
            } catch (const SetupPasswordFailure& error) {
                again = error;
                note(error.what());
            }
            check(again.outcome.locked_out && firmware.submissions == 2,
                  "A locked-out firmware is reported, not hammered");
        }

        // --- Firmware that gives no remaining-attempts figure. ----------------------------------
        {
            SimulatedFirmware firmware;
            firmware.report_remaining = false;
            PasswordAttempt attempt(secret(WrongPassword));
            auto text = caught<SetupPasswordFailure>(
                [&] { with_password_unlock(firmware.operations(), attempt).read("Setup", Guid); },
                "Wrong password without a count still fails");
            check(text.find("remaining") == std::string::npos, "No invented attempt count");
            check(password_report(attempt)["remaining_attempts"].is_null(),
                  "The report leaves the count null");
        }

        // --- Too long: refused before anything is sent. -----------------------------------------
        {
            SimulatedFirmware firmware;
            const std::string long_password(MaxPasswordBytes + 1, 'A');
            caught<SetupPasswordFailure>([&] { PasswordAttempt attempt(secret(long_password)); },
                                         "A password beyond the sanity bound is refused");
            check(firmware.submissions == 0, "An over-long password is never sent");
            // Within the bound but longer than this board accepts: the firmware decides, once.
            PasswordAttempt attempt(secret(std::string(30, 'A')));
            caught<SetupPasswordFailure>(
                [&] { with_password_unlock(firmware.operations(), attempt).read("Setup", Guid); },
                "A password this board's firmware rejects is reported");
            check(firmware.submissions == 1, "That was a single attempt");
            PasswordAttempt longest(secret(std::string(MaxPasswordBytes, 'A')));
            check(longest.supplied(), "A password at the bound is accepted for submission");
        }

        // --- Non-ASCII and control characters: refused before anything is sent. ------------------
        {
            SimulatedFirmware firmware;
            for (const std::string odd :
                 {std::string("p\xC3\xA4ssword-1"), std::string("tab\there1"), std::string("new\nline1"),
                  std::string("\xF0\x9F\x98\x80\xF0\x9F\x98\x80")}) {
                auto text = caught<SetupPasswordFailure>([&] { PasswordAttempt attempt(secret(odd)); },
                                                         "An unusable password is refused");
                check(text.find("Nothing was sent") != std::string::npos,
                      "The refusal says nothing was sent");
            }
            check(firmware.submissions == 0, "No unusable password reached the firmware");
        }

        // --- A transport that cannot submit: said plainly, nothing sent. ------------------------
        {
            SimulatedFirmware firmware;
            PasswordAttempt attempt(secret(GoodPassword));
            auto ops = with_password_unlock(firmware.operations(false), attempt);
            SetupPasswordFailure unsupported("", {});
            try {
                ops.read("Setup", Guid);
            } catch (const SetupPasswordFailure& error) {
                unsupported = error;
                note(error.what());
            }
            check(unsupported.outcome.unsupported && !unsupported.outcome.submitted &&
                      !unsupported.outcome.accepted,
                  "A transport with no submit step reports unsupported, not 'wrong password'");
            check(std::string(unsupported.what()).rfind("AMI GetVariable failed for Setup", 0) == 0,
                  "The firmware's own refusal text still leads the message");
            check(firmware.submissions == 0, "Nothing was sent");
        }

        // --- Hostile transports: a password in their text is removed before it can be shown. -----
        {
            SimulatedFirmware echo;
            echo.echo_password_in_reason = true;
            PasswordAttempt attempt(secret(WrongPassword));
            auto text = caught<SetupPasswordFailure>(
                [&] { with_password_unlock(echo.operations(), attempt).read("Setup", Guid); },
                "An echoing transport still fails cleanly");
            check(text.find(WrongPassword) == std::string::npos,
                  "A password echoed by the transport is removed from the message");
            check(password_report(attempt).dump().find(WrongPassword) == std::string::npos,
                  "...and from the report");
            SimulatedFirmware faulty;
            faulty.throw_with_password = true;
            PasswordAttempt second(secret(WrongPassword));
            text = caught<SetupPasswordFailure>(
                [&] { with_password_unlock(faulty.operations(), second).read("Setup", Guid); },
                "A transport that throws is reported");
            check(text.find(WrongPassword) == std::string::npos,
                  "A password inside a transport exception is removed");
            note(password_report(second).dump());
        }

        // --- A full import through the real transaction engine. ---------------------------------
        {
            SimulatedFirmware firmware;
            PasswordAttempt attempt(secret(GoodPassword));
            auto journal = workspace / L"protected-import.json";
            auto receipt = execute_import(two_variable_plan(), journal,
                                          with_password_unlock(firmware.operations(), attempt));
            check(receipt["stage"] == "committed" && firmware.nv["Setup"][0] == 0 &&
                      firmware.nv["Other"][2] == 9,
                  "A protected two-variable import commits with the password");
            check(firmware.submissions == 1, "The whole import used one submission");
            auto journal_text = slurp(journal);
            check(journal_text.find(GoodPassword) == std::string::npos &&
                      journal_text.find("password") == std::string::npos,
                  "The journal holds no password and no password field");
            note(journal_text);

            SimulatedFirmware wrong_firmware;
            PasswordAttempt wrong(secret(WrongPassword));
            auto wrong_journal = workspace / L"wrong-password-import.json";
            SetupPasswordFailure failure("", {});
            try {
                execute_import(two_variable_plan(), wrong_journal,
                               with_password_unlock(wrong_firmware.operations(), wrong));
            } catch (const SetupPasswordFailure& error) {
                failure = error;
                note(error.what());
            }
            check(!failure.outcome.accepted && wrong_firmware.submissions == 1 && wrong_firmware.writes == 0,
                  "A wrong password in an import is reported once and writes nothing");
            check(wrong_firmware.nv["Setup"] == Bytes({1, 0x64, 0, 0}) &&
                      wrong_firmware.nv["Other"] == Bytes({1, 2, 3, 4}),
                  "The firmware's settings are untouched");
            check(!std::filesystem::exists(wrong_journal),
                  "A refused password stops the import before any journal or write exists");

            // Without a password the import fails as before, with the same kind of error and no submission.
            SimulatedFirmware none_firmware;
            PasswordAttempt none;
            auto none_journal = workspace / L"no-password-import.json";
            caught<SetupPasswordRequired>(
                [&] {
                    execute_import(two_variable_plan(), none_journal,
                                   with_password_unlock(none_firmware.operations(), none));
                },
                "An import with no password is refused as before");
            check(none_firmware.submissions == 0, "No password, no submission");
        }

        // --- A refusal that returns after a successful unlock is not answered with a second try. -
        {
            SimulatedFirmware firmware;
            PasswordAttempt attempt(secret(GoodPassword));
            auto ops = with_password_unlock(firmware.operations(), attempt);
            ops.read("Setup", Guid);
            firmware.locked = true; // the firmware re-locks (for example after a reboot between steps)
            caught<SetupPasswordRequired>([&] { ops.read("Setup", Guid); }, "A later refusal propagates");
            check(firmware.submissions == 1, "The password is not submitted a second time");
        }

        // --- The secret container. ---------------------------------------------------------------
        {
            std::string text = GoodPassword;
            auto made = secret_from_utf8(text);
            check(text.empty() && made.size() == GoodPassword.size(),
                  "secret_from_utf8 empties the source string");
            SecretBytes moved(std::move(made));
            check(made.empty() && moved.size() == GoodPassword.size(),
                  "Moving a secret leaves the source empty");
            moved.wipe();
            check(moved.empty(), "wipe empties a secret");
            uint8_t block[16];
            std::fill(std::begin(block), std::end(block), uint8_t{0xAB});
            secure_zero(block, sizeof(block));
            check(std::all_of(std::begin(block), std::end(block), [](uint8_t b) { return b == 0; }),
                  "secure_zero zeroes memory");
            PasswordAttempt consumed(secret(GoodPassword));
            check(consumed.supplied(), "The attempt took ownership of the secret");
        }

        // --- Actual control packets, against a simulated SMI exchange. ---------------------------
        {
            auto packet = protocol::setup_password(0x1000, secret("9Ab"));
            check(read_le(packet, 0, 4) == 0x300 && read_le(packet, 4, 4) == 0x1030,
                  "Authentication uses the established AMI SetVariable service");
            check(guid_string(packet, 0x30) == "5855ce1b-fb8e-47e4-bc1a-39ecaa0c96cf",
                  "The vendor identity matches the researched authentication control");
            check(decode_utf16(Bytes(packet.begin() + 0x50, packet.begin() + 0x6a)) == "$SETUPPASSWD",
                  "The exact control variable name is terminated UTF-16");
            check(read_le(packet, 0x44, 4) == 7 && read_le(packet, 0x48, 4) == 8 &&
                      read_le(packet, 0x4c, 4) == 0x106a &&
                      hex(Bytes(packet.begin() + 0x6a, packet.end())) == "3900410062000000",
                  "Password length includes its UTF-16 terminator, with no encoding or pointer guesses");
            auto relock = protocol::setup_password_relock(0x1000);
            check(read_le(relock, 0x48, 4) == 1 && relock.back() == 0,
                  "Relock sends one zero byte, rather than deleting a settings variable");
            check(read_le(protocol::setup_password_probe(0x1000), 0x48, 4) == 0,
                  "The support probe reads no credential data");
            caught([&] { protocol::setup_password(0xfffffff0, secret("9Ab")); },
                   "Wrapping physical pointers fail");
            caught([&] { protocol::set_variable(0x1000, "Setup", Guid, 7, {}); },
                   "Ordinary deletion remains forbidden");
        }
        for (const auto status : {0x9au, 0x8fu, 0x86u, 0x82u}) {
            int calls = 0;
            SetupPasswordChannel channel(0x1000, [&](Bytes&) {
                ++calls;
                return status;
            });
            auto outcome = channel.submit(secret(GoodPassword));
            check(outcome.submitted && !outcome.accepted && calls == 1,
                  "A firmware rejection costs one request and no follow-up attempts");
            check(outcome.locked_out == (status == 0x8f),
                  "Only the firmware's retry-limit status means lockout");
            check(!outcome.remaining_attempts, "The protocol supplies no count, so none is invented");
            caught([&] { channel.submit(secret(GoodPassword)); }, "The channel forbids a second submission");
            channel.relock();
            check(calls == 1, "A known rejected request is never submitted or relocked again");
        }
        for (const bool fail_operation : {false, true}) {
            std::vector<uint32_t> functions, lengths;
            SetupPasswordChannel channel(0x1000, [&](Bytes& packet) {
                functions.push_back(static_cast<uint32_t>(read_le(packet, 0, 4)));
                lengths.push_back(static_cast<uint32_t>(read_le(packet, 0x48, 4)));
                return functions.size() == 1 ? 0u : functions.size() == 2 ? 0x8eu : 0x9au;
            });
            auto operation = [&]() -> Json {
                check(channel.submit(secret(GoodPassword)).accepted,
                      "A consumed authentication request is accepted");
                if (fail_operation)
                    throw Error("simulated import failure after unlock");
                return {{"ok", true}};
            };
            if (fail_operation)
                caught([&] { channel.run(operation); }, "A failing operation propagates after cleanup");
            else
                check(channel.run(operation)["ok"], "A successful operation returns after cleanup");
            check(functions == std::vector<uint32_t>{0x300, 0x100, 0x300} && lengths.back() == 1,
                  "Success and failure both re-lock exactly once before ending the session");
            channel.relock();
            check(functions.size() == 3, "Destructor cleanup cannot repeat an explicit re-lock");
        }
        for (const auto exists : {0u, 0x85u}) {
            int calls = 0;
            SetupPasswordChannel channel(0x1000, [&](Bytes& packet) {
                ++calls;
                if (calls == 2)
                    return exists;
                if (calls == 3)
                    check(read_le(packet, 0x48, 4) == 0,
                          "Unsupported persistence is deleted only on the control channel");
                return 0u;
            });
            auto outcome = channel.submit(secret(GoodPassword));
            check(outcome.unsupported && !outcome.accepted && calls == 3,
                  "Firmware that stores the request is cleaned and never reported as unlocked");
            channel.relock();
            check(calls == 3, "The removed unsupported control variable is not recreated");
        }
        {
            int calls = 0;
            SetupPasswordChannel channel(0x1000, [&](Bytes& packet) {
                ++calls;
                if (calls == 1)
                    throw Error("simulated driver failure after submission");
                check(read_le(packet, 0x48, 4) == 1,
                      "Ambiguous transport failure re-locks without resending the secret");
                return 0x9au;
            });
            caught(
                [&] {
                    channel.run([&]() -> Json {
                        channel.submit(secret(GoodPassword));
                        return {};
                    });
                },
                "An ambiguous submission error stops the operation");
            check(calls == 2, "One submission and one cleanup, with no retry");
        }
        {
            int calls = 0;
            SetupPasswordChannel channel(0x1000, [&](Bytes&) {
                ++calls;
                return calls == 1 ? 0u : calls == 2 ? 0x8eu : 0x87u;
            });
            auto error = caught(
                [&] {
                    channel.run([&]() -> Json {
                        channel.submit(secret(GoodPassword));
                        return {{"ok", true}};
                    });
                },
                "A failed re-lock cannot report success");
            check(error.find("re-lock failed") != std::string::npos && calls == 3,
                  "Cleanup failure is explicit and is not retried");
        }
        for (const auto& short_password : {std::string("~"), std::string("^!"), std::string("~^!")}) {
            SimulatedFirmware firmware;
            firmware.echo_password_in_reason = true;
            PasswordAttempt attempt(secret(short_password));
            auto message = caught<SetupPasswordFailure>(
                [&] { with_password_unlock(firmware.operations(), attempt).read("Setup", Guid); },
                "Short passwords are rejected without leaking");
            check(message.find(short_password) == std::string::npos &&
                      password_report(attempt).dump().find(short_password) == std::string::npos,
                  "Even one-character passwords are removed from transport output");
        }

        // --- The password appears nowhere that anything was printed or written. -----------------
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        note(captured_out.str());
        note(captured_err.str());
        for (const auto& entry : std::filesystem::recursive_directory_iterator(workspace))
            if (entry.is_regular_file())
                note(slurp(entry.path()));
        for (const auto& needle : {GoodPassword, WrongPassword})
            check(everything.find(needle) == std::string::npos,
                  "A password appears in no message, report, journal, file or console output");
        check(!everything.empty(), "The leak scan had something to scan");
        std::filesystem::remove_all(workspace);
        std::cout << "PASS: " << checks << " password checks; firmware was not accessed.\n";
    } catch (const std::exception& error) {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cerr << error.what() << "\n";
        result = 1;
    }
    return result;
}
