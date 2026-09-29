// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fcore_session — the native CLI over felitronics::session: a session driven from a command script, with no screen.
// It links the library AS C++, the way a desktop application does, and the ABI's translation unit beside it
// (tools/wasm/fc_session.cpp, a separate TU on purpose, as fcore_master keeps fc_master.cpp) so that the version of
// the C surface it reports is the facade's own answer, not a macro copied into this file.
//
//   fcore_session version         the releases and the ABI, one `name value` line each:
//                                   felitronics-mastering-core <MAJOR.MINOR.PATCH>   (Session::version)
//                                   felitronics-core <MAJOR.MINOR.PATCH>             (Session::coreVersion)
//                                   fc_session_abi <N>                               (fc_session_abi_version)
//   fcore_session run <script>    runs the shared native/Wasm contract grammar and writes tab-delimited codec records.
//   fcore_session parse <script>  validates the grammar and prints its instruction encoding.
//   fcore_session table           who may do what, when: the command × state table and the session's own transitions
//                                 (Commands.h, Table), as Markdown — the text docs/SESSION.md carries between its
//                                 markers, which ctest holds byte for byte to this output.
//   fcore_session config targets  the config the library was built with, one document at a time, spelled by
//   fcore_session config engine   felitronics-toml's canonical writer (config::Config::text): the numbers of
//                                 modules/session/config/*.toml without their comments, to open and read.
//   fcore_session config version        the config's versions (config::Config::versions), 16 hexadecimal digits:
//   fcore_session config sound-version  `version` of all its data — which config this is — and `sound-version` of
//                                       what can change a master, the one a recipe will record.
//
// THE SCRIPT. tools/contract/grammar.json defines the one instruction grammar for this CLI and the Wasm runner.
// One instruction per line; `#` starts a comment. Parsing completes before any session is created.
//
// EXIT STATUS: 0 done; 2 refused — usage, file, script, or session refusal.
// stdout carries the result and nothing else; every diagnostic goes to stderr.

#include "fc_session_abi.h"
#include "session-script.h"

#include <felitronics/session/Commands.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Session.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

using felitronics::session::Session;
namespace config = felitronics::session::config;
using config::Config;

namespace
{
int usage (const char* argv0)
{
    std::fprintf (stderr, "usage: %s version\n       %s run <script|->\n       %s table\n"
                          "       %s config targets|engine|version|sound-version\n",
                  argv0, argv0, argv0, argv0);
    return 2;
}

// THE NAMES OF THE TABLE — the shell's words for the session's codes: the commands as the contract spells them, the
// columns, the transitions and the rejections as Commands.h names them.
const char* nameOf (felitronics::session::Command c)
{
    using felitronics::session::Command;
    switch (c)
    {
        case Command::Load:        return "load";
        case Command::SetTarget:   return "setTarget";
        case Command::EditTarget:  return "editTarget";
        case Command::EditDevice:  return "editDevice";
        case Command::RevertEdits: return "revertEdits";
        case Command::SetManual:   return "setManual";
        case Command::Master:      return "master";
        case Command::Cancel:      return "cancel";
        case Command::Forget:      return "forget";
        case Command::ContinueMeasurement: return "continueMeasurement";
        case Command::ImportProject: return "importProject";
    }
    return "?";
}

const char* nameOf (felitronics::session::Event e)
{
    using felitronics::session::Event;
    switch (e)
    {
        case Event::Measured1: return "the first measurement ends";
        case Event::Measured2: return "the second measurement ends";
        case Event::Mastered:  return "the master is done";
    }
    return "?";
}

const char* nameOf (felitronics::session::Rejection r)
{
    using felitronics::session::Rejection;
    switch (r)
    {
        case Rejection::None:                     return "yes";
        case Rejection::FloatingPointEnvironment: return "FloatingPointEnvironment";
        case Rejection::NoSource:                 return "NoSource";
        case Rejection::NotPlaced:                return "NotPlaced";
        case Rejection::NotMeasured:              return "NotMeasured";
        case Rejection::Busy:                     return "Busy";
        case Rejection::NoJob:                    return "NoJob";
        case Rejection::NoMaster:                 return "NoMaster";
        case Rejection::UnknownTarget:            return "UnknownTarget";
        case Rejection::NotOffered:               return "NotOffered";
        case Rejection::UnknownJob:               return "UnknownJob";
        case Rejection::UnknownMaster:            return "UnknownMaster";
        case Rejection::NotFinite:                return "NotFinite";
        case Rejection::NotOneOf:                 return "NotOneOf";
        case Rejection::OutOfDomain:              return "OutOfDomain";
        case Rejection::BadChannels:              return "BadChannels";
        case Rejection::BadRate:                  return "BadRate";
        case Rejection::NoAudio:                  return "NoAudio";
        case Rejection::TooLong:                  return "TooLong";
        case Rejection::NoJobId:                  return "NoJobId";
        case Rejection::InvalidUtf8:              return "InvalidUtf8";
        case Rejection::ProjectTooLarge: return "ProjectTooLarge";
        case Rejection::ProjectSyntax: return "ProjectSyntax";
        case Rejection::ProjectMissing: return "ProjectMissing";
        case Rejection::ProjectType: return "ProjectType";
        case Rejection::ProjectUnknownKey: return "ProjectUnknownKey";
        case Rejection::UnknownDefaults: return "UnknownDefaults";
        case Rejection::ProjectCore: return "ProjectCore";
        case Rejection::NewerDefaults: return "NewerDefaults";
        case Rejection::RateAboveLimit: return "RateAboveLimit";
        case Rejection::Contract:                 return "Contract";
        case Rejection::Memory: return "Memory";
    }
    return "?";
}

// The two tables as Markdown: a row per command (the cell is `yes` where it is taken, the rejection's name where not),
// then a row per transition (`yes` where it happens, `no` where it does not). ASCII only: a Windows console would
// read anything else in its own code page.
void printHeader (const char* first)
{
    std::printf ("| %s | Empty | Loaded | Measured1 | Measured2 | Mastering1 | Mastering2 | Stopped | StoppedMeasured | MasteringStopped | Measured1Unplaced | Measured2Unplaced | Mastering1Unplaced | Mastering2Unplaced | StoppedMeasuredUnplaced | MasteringStoppedUnplaced |\n"
                 "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n", first);
}

void printTable()
{
    using felitronics::session::Table;
    printHeader ("command");
    for (const auto& row : Table::commands)
    {
        std::printf ("| %s |", nameOf (row.command));
        for (const auto cell : row.cell) std::printf (" %s |", nameOf (cell));
        std::printf ("\n");
    }
    std::printf ("\n");
    printHeader ("the session's own transition");
    for (const auto& row : Table::events)
    {
        std::printf ("| %s |", nameOf (row.event));
        for (const bool cell : row.cell) std::printf (" %s |", cell ? "yes" : "no");
        std::printf ("\n");
    }
}

// The whole script, or false with the reason on stderr.
bool readAll (const char* path, std::string& out)
{
    const bool stdinStream = std::strcmp (path, "-") == 0;
    std::FILE* f = stdinStream ? stdin : std::fopen (path, "rb");
    if (f == nullptr) { std::fprintf (stderr, "fcore_session: cannot open %s\n", path); return false; }
    char buf[4096];
    for (std::size_t n; (n = std::fread (buf, 1, sizeof buf, f)) > 0;) out.append (buf, n);
    const bool failed = std::ferror (f) != 0;
    if (! stdinStream) std::fclose (f);
    if (failed) { std::fprintf (stderr, "fcore_session: cannot read %s\n", path); return false; }
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "version") == 0)
    {
        const auto v = Session::version();
        const auto c = Session::coreVersion();
        std::printf ("felitronics-mastering-core %u.%u.%u\n", (unsigned) v.major, (unsigned) v.minor, (unsigned) v.patch);
        std::printf ("felitronics-core %u.%u.%u\n", (unsigned) c.major, (unsigned) c.minor, (unsigned) c.patch);
        std::printf ("fc_session_abi %u\n", (unsigned) fc_session_abi_version());
        return 0;
    }
    if (argc == 3 && (std::strcmp (argv[1], "run") == 0 || std::strcmp (argv[1], "parse") == 0))
    {
        std::string script;
        if (! readAll (argv[2], script)) return 2;
        return sessionScript (script, argv[2], std::strcmp (argv[1], "parse") == 0);
    }
    if (argc == 2 && std::strcmp (argv[1], "table") == 0)
    {
        printTable();
        return 0;
    }
    if (argc == 3 && std::strcmp (argv[1], "config") == 0)
    {
        if (std::strcmp (argv[2], "version") == 0 || std::strcmp (argv[2], "sound-version") == 0)
        {
            const config::Versions v = Config::versions();
            std::printf ("%016" PRIx64 "\n", std::strcmp (argv[2], "version") == 0 ? v.all : v.sound);
            return 0;
        }
        const bool targets = std::strcmp (argv[2], "targets") == 0;
        if (targets || std::strcmp (argv[2], "engine") == 0)
        {
            const std::string text = Config::text (targets ? config::Document::Targets : config::Document::Engine);
            std::fwrite (text.data(), 1, text.size(), stdout);
            return 0;
        }
    }
    return usage (argc > 0 ? argv[0] : "fcore_session");
}
