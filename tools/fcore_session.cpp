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
//   fcore_session run <script>    reads the script (`-` is stdin) into a fresh session and prints `done <commands>`.
//
// THE SCRIPT. One command per line; `#` starts a comment that runs to the end of the line; a line that is empty or
// blank after that is not a command. There are no commands yet, so the only script this accepts is one with none in
// it — empty, or comments and blank lines — and it answers `done 0`. A line that is a command is refused: exit 2, the
// line named on stderr, nothing on stdout. A refusal is the whole run's, never a prefix of it: the script is read and
// checked to the end before the session is created, so a refused script touched no session at all.
//
// EXIT STATUS: 0 done; 2 refused — a usage error, a file that cannot be read, a script this build does not accept, or a
// session that refused to be created (felitronics::session::Status, named on stderr).
// stdout carries the result and nothing else; every diagnostic goes to stderr.

#include "fc_session_abi.h"

#include <felitronics/session/Session.h>

#include <cstdio>
#include <cstring>
#include <string>

using felitronics::session::Session;

namespace
{
int usage (const char* argv0)
{
    std::fprintf (stderr, "usage: %s version\n       %s run <script|->\n", argv0, argv0);
    return 2;
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

// The number of commands in `script`, or -1 after naming the first line that is one this build does not know — which,
// with no commands defined yet, is any command at all.
long countCommands (const std::string& script)
{
    long line = 0, commands = 0;
    for (std::size_t at = 0; at < script.size();)
    {
        const std::size_t eol = script.find ('\n', at);
        const std::size_t end = eol == std::string::npos ? script.size() : eol;
        ++line;
        std::string text = script.substr (at, end - at);
        if (const std::size_t hash = text.find ('#'); hash != std::string::npos) text.resize (hash);
        const std::size_t first = text.find_first_not_of (" \t\r\v\f");
        if (first != std::string::npos)
        {
            const std::size_t last = text.find_last_not_of (" \t\r\v\f");
            std::fprintf (stderr, "fcore_session: line %ld: unknown command '%s' — this build knows no commands\n",
                          line, text.substr (first, last - first + 1).c_str());
            return -1;
        }
        at = end + 1;
    }
    return commands;
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
    if (argc == 3 && std::strcmp (argv[1], "run") == 0)
    {
        std::string script;
        if (! readAll (argv[2], script)) return 2;
        const long commands = countCommands (script);
        if (commands < 0) return 2;
        auto created = Session::create();
        if (created.status != felitronics::session::Status::Ok)
        {
            std::fprintf (stderr, "fcore_session: the session refused to be created (%s)\n",
                          created.status == felitronics::session::Status::FloatingPointEnvironment
                              ? "this thread's floating-point environment is not IEEE-754's default"
                              : "a shared helper was kept in a copy compiled with FP contraction");
            return 2;
        }
        // The script held no command, so nothing runs against the session: it is created, and destroyed.
        created.session.reset();
        std::printf ("done %ld\n", commands);
        return 0;
    }
    return usage (argc > 0 ? argv[0] : "fcore_session");
}
