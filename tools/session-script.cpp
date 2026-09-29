// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#if defined(__EMSCRIPTEN__)
#include "session-script.h"
int sessionScript (const std::string&, const char*, bool) { return 2; }
#else
#include "session-script.h"
#include "fc_session_abi.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Wire.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
using namespace felitronics::session;
struct Rule { const char* op; const char* pattern; };
#include "session-script-grammar.h"
struct Instruction { std::string op; std::vector<std::string> args; std::size_t line; };
std::vector<Instruction> parse (const std::string& script)
{
    std::vector<Instruction> out;
    std::istringstream input (script);
    std::string text;
    std::size_t line = 0;
    while (std::getline (input, text))
    {
        ++line;
        text.resize (text.find ('#') == std::string::npos ? text.size() : text.find ('#'));
        const auto first = text.find_first_not_of (" \t\r\v\f");
        if (first == std::string::npos) continue;
        text = text.substr (first, text.find_last_not_of (" \t\r\v\f") - first + 1);
        bool found = false;
        for (const auto& rule : rules)
        {
            std::smatch match;
            if (! std::regex_match (text, match, std::regex (rule.pattern))) continue;
            Instruction i { rule.op, {}, line };
            for (std::size_t n = 1; n < match.size(); ++n) i.args.push_back (match[n].str());
            out.push_back (std::move (i)); found = true; break;
        }
        if (! found) throw std::runtime_error ("line " + std::to_string (line) + ": unknown command '" + text + "'");
    }
    return out;
}
void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
std::string hex (std::string_view bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string rowHex (const std::vector<double>& rows)
{
    std::string bytes;
    for (double value : rows)
    {
        const auto bits = std::bit_cast<std::uint64_t> (value);
        for (unsigned i = 0; i < 8; ++i) bytes += char ((bits >> (i * 8)) & 255);
    }
    return hex (bytes);
}
class Runner;
Runner* abandonedRunner = nullptr;
void afterPoison();
class Runner
{
public:
    std::vector<Instruction> instructions;
    std::filesystem::path fixtures;
    std::map<std::string, std::unique_ptr<Session>> sessions;
    std::map<std::string, std::string> projects;
    std::string current;
    std::size_t next = 0;
    bool poisoned = false;
    void record (const char* kind, const std::string& bytes, const std::string& rows = "")
    {
        std::printf ("%s\t%s\t%s\t%s\n", current.c_str(), kind, bytes.c_str(), rows.c_str());
    }
    Session& session()
    {
        require (! poisoned, "poisoned instance: use new instance");
        require (sessions.contains (current), "no current session");
        return *sessions.at (current);
    }
    std::pair<std::string, std::string> copy (bool snapshot)
    {
        auto& s = session();
        const auto need = snapshot ? Wire::snapshotBytes (s) : Wire::eventsBytes (s.events());
        require (need.status == CodecStatus::Ok, "transfer size refused");
        std::string json (need.jsonBytes, '\0'); std::vector<double> rows (need.rowBytes / 8);
        const auto status = snapshot ? Wire::snapshot (s, json, rows) : Wire::events (s.events(), json, rows);
        require (status == CodecStatus::Ok, "transfer copy refused");
        return { json, rowHex (rows) };
    }
    void transfer (bool snapshot)
    {
        const auto [json, rows] = copy (snapshot);
        record (snapshot ? "snapshot" : "events", json, rows);
    }
    template <class Query> void demand (Query query)
    {
        const auto events = copy (false), snapshot = copy (true);
        const auto need = query();
        require (need.bytes < 9007199254740992ull && need.largestBlockBytes <= need.bytes, "invalid demand");
        require (session().liveBytes() > 0, "live session is priced");
        require (copy (false) == events && copy (true) == snapshot, "demand changed events or snapshot");
    }
    void finishPoison()
    {
        fc_session_sizes sizes { sizeof (fc_session_sizes), 123, 456 };
        require (fc_session_snapshot_size (0, &sizes) == FC_SESSION_ERR_POISONED
                 && sizes.size == sizeof (sizes) && sizes.jsonBytes == 123 && sizes.rowBytes == 456, "abandoned call did not poison or wrote output");
        require (fc_session_create (nullptr, 0, 0, nullptr) == FC_SESSION_ERR_POISONED, "poison must precede arguments");
        poisoned = true;
        record ("poison", "{\"trap\":" + std::to_string (FC_SESSION_ERR_TRAP)
                + ",\"status\":" + std::to_string (FC_SESSION_ERR_POISONED) + ",\"untouched\":true}");
    }
    int run()
    {
        try
        {
            while (next < instructions.size())
            {
                const auto& i = instructions[next++];
                const auto& a = i.args;
                if (i.op == "new") { sessions.clear(); current.clear(); poisoned = false; continue; }
                require (! poisoned, "poisoned instance: use new instance");
                if (i.op == "create")
                {
                    current = a[0]; require (! sessions.contains (current), "duplicate session");
                    const Capabilities caps { double (std::stoull (a[1])), 96000, FC_SESSION_DEVICES_ALL, double (std::stoull (a[1])) };
                    require (Session::createBytes (caps) > 0, "pre-create demand");
                    auto made = Session::create (caps, config::Config::versions().all);
                    require (made.status == Status::Ok || made.status == Status::Memory, "create failed");
                    record ("create", std::to_string (made.status == Status::Ok ? FC_SESSION_OK : FC_SESSION_ERR_MEMORY));
                    if (made.session) sessions.emplace (current, std::move (made.session));
                    continue;
                }
                if (i.op == "use") { current = a[0]; (void) session(); continue; }
                if (i.op == "poison") { (void) session(); abandonedRunner = this; sessionScriptPoison (&afterPoison); }
                if (i.op == "snapshot") { transfer (true); continue; }
                if (i.op == "export")
                {
                    const auto need = session().exportProjectBytes();
                    require (need.rejection == Rejection::None, "project size refused");
                    std::string text (std::size_t (need.bytes), '\0');
                    require (session().exportProject (text) == Rejection::None, "project copy refused");
                    projects[a[0]] = text; record ("project", hex (text)); continue;
                }
                if (i.op == "capacity")
                {
                    require (session().setCapacity ({ double (std::stoull (a[0])), double (std::stoull (a[1])) }) == Status::Ok,
                             "capacity refused");
                    record ("capacity", "0"); continue;
                }
                if (i.op == "step")
                {
                    const auto stepped = session().step (std::uint32_t (std::stoul (a[0])));
                    require (! stepped.refused, "step refused"); record ("step", std::to_string (unsigned (stepped.state)));
                    transfer (false); continue;
                }
                char answer[kAnswerBytes]; std::uint32_t written = 0; CodecStatus status = CodecStatus::Invalid;
                if (i.op == "command" || i.op == "cancel")
                {
                    const auto json = i.op == "command" ? a[0] : "{\"kind\":\"cancel\",\"commandId\":\"" + a[0]
                        + "\",\"jobId\":" + a[1] + "}";
                    demand ([&] { return Wire::commandStorage (session(), json); });
                    status = Wire::command (session(), json, answer, written);
                }
                else if (i.op == "import" || i.op == "import-file")
                {
                    std::string project;
                    if (i.op == "import-file")
                    {
                        std::ifstream input (fixtures / (a[1] + ".toml"), std::ios::binary);
                        require (input.good(), "unknown project fixture");
                        project.assign (std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char>());
                        require (! input.bad(), "project fixture read failed");
                    }
                    else { require (projects.contains (a[1]), "unknown project"); project = projects.at (a[1]); }
                    demand ([&] { return Wire::importStorage (session(), project); });
                    status = Wire::importProject (session(), std::stoull (a[0]), project, answer, written);
                }
                else if (i.op == "load")
                {
                    std::ifstream input (fixtures / (a[1] + ".pcm"));
                    std::uint32_t rate = 0, channels = 0, frames = 0;
                    input >> rate >> channels >> frames;
                    require (input.good() && channels >= 1 && channels <= 2 && frames > 0 && frames <= 1000000, "bad PCM header");
                    std::vector<float> samples (std::size_t (channels) * frames);
                    for (auto& sample : samples)
                    {
                        int value = 0; require (bool (input >> value) && value >= -32768 && value <= 32767, "bad PCM sample");
                        sample = float (value) / 32768.0f;
                    }
                    std::string extra; require (! (input >> extra), "extra PCM sample");
                    const float* planes[] = { samples.data(), samples.data() + frames };
                    const auto meta = "{\"name\":\"" + a[1] + "\",\"fileRate\":" + std::to_string (rate) + ",\"bitDepth\":16,\"rateKnown\":true}";
                    demand ([&] { return Wire::loadStorage (session(), channels, frames, rate, meta); });
                    status = Wire::load (session(), std::stoull (a[0]), { planes, channels, frames, rate }, meta, answer, written);
                }
                require (status == CodecStatus::Ok, "codec call refused");
                record ("answer", std::string (answer, written)); transfer (false);
            }
            require (! poisoned, "scenario ended in a poisoned instance");
            for (const auto& [name, owner] : sessions) { (void) owner; current = name; transfer (true); }
            return 0;
        }
        catch (const std::exception& e)
        {
            std::fprintf (stderr, "fcore_session: line %zu: %s\n", next ? instructions[next - 1].line : 0, e.what()); return 2;
        }
    }
};
void afterPoison()
{
    int status = 2;
    try { abandonedRunner->finishPoison(); status = abandonedRunner->run(); }
    catch (const std::exception& e) { std::fprintf (stderr, "fcore_session: poison: %s\n", e.what()); }
    std::fflush (nullptr); std::_Exit (status);
}
}
int sessionScript (const std::string& script, const char* path, bool parseOnly)
{
    try
    {
        Runner runner;
        runner.instructions = parse (script);
#if defined(_WIN32)
        _setmode (_fileno (stdout), _O_BINARY);
#endif
        if (parseOnly)
        {
            for (const auto& i : runner.instructions)
            {
                std::printf ("%zu\t%s", i.line, i.op.c_str());
                for (const auto& a : i.args) std::printf ("\t%s", a.c_str());
                std::putchar ('\n');
            }
            return 0;
        }
        if (runner.instructions.empty()) { std::puts ("done 0"); return 0; }
        runner.fixtures = std::filesystem::path (path).parent_path().parent_path() / "fixtures";
        return runner.run();
    }
    catch (const std::exception& e) { std::fprintf (stderr, "fcore_session: %s\n", e.what()); return 2; }
}

#endif
