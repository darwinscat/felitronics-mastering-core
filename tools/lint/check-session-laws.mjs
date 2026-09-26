// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE LAWS OF felitronics::session THAT ONLY THE SOURCE CAN SHOW (docs/SESSION.md has all of them and what holds each).
//
// WHAT THIS IS NOT. "No mutable state outside an object" and "no operating system, file, console, locale, clock or
// process state" are held by the OBJECT-FILE GATE (modules/session/tests/object-gates.cmake): the compiled object says
// exactly which symbols live in writable memory and which are called, however the source spelled them — through a macro,
// an asm label, a pointer, a header that made printf visible. A lexer answers those questions badly, and this one no
// longer tries. What it holds is what leaves no symbol in any object:
//
//   INCLUDE      every #include is canonically spelled (no `./`, no `..`, no backslash, no capital) and on the allowlist
//                below: a standard header the session may use, or a felitronics header that resolves in this repository
//                or in felitronics-core, or a quoted header inside modules/session. Everything else — <chrono>,
//                <thread>, <cstdio>, <./chrono>, <Memory> — is refused, with no list of what is forbidden to go stale.
//   PRAGMA       no pragma but `#pragma once`, and no `_Pragma(`, `__pragma(`, [[gnu::optimize]], [[gnu::target]],
//                __attribute__((optimize / target)). A local pragma changes floating-point semantics for the code after
//                it (`#pragma clang fp contract(fast)` makes clang fuse under -ffp-contract=off — reproduced), and no
//                flag of the library reaches past it.
//   EXCEPTIONS   no throw, try, catch, typeid, dynamic_cast (nor MSVC's __try / __except / __finally / __leave), in any
//                branch of any #if: MSVC compiles a bare `throw` under /EHs-c-, and a branch other rows preprocess away
//                is still that row's code.
//   CONDITIONAL  no #if / #ifdef / #ifndef / #elif / #else anywhere but src/BuildGuards.h and src/BuildContract.cpp: a
//                branch is code one row compiles and another does not, and the session is one program on every row.
//   NOSYMBOL     no facility that compiles to instructions and leaves no symbol: atomics (std::atomic and friends,
//                __atomic_*, __sync_*, _Interlocked*), cycle counters and target intrinsics (__builtin_readcyclecounter,
//                __rdtsc, __builtin_ia32_* / _arm_ / _aarch64_), inline assembly (asm, __asm__) — a clock read by
//                `mrs cntvct_el0` is invisible to every object-file tool.
//   ORDER        no std::unordered_* containers, std::hash, or std::sort / partial_sort / partial_sort_copy /
//                nth_element: their order is implementation-defined (libstdc++, libc++ and MSVC answer differently
//                from one input), the same reason <random> is not on the include list. std::stable_sort, or a total
//                order, gives one answer everywhere.
//   BODY         no function body in modules/session/include — only `= default` and `= delete`. A body in the public
//                header is compiled under the CONSUMER's flags, and the library's flags say nothing about it.
//   GUARD        every translation unit's first #include is "BuildGuards.h", which refuses a unit compiled with
//                exceptions, RTTI or fast-math — so a per-source flag override cannot slip past the target's flags.
//   ZONE         every file of modules/session has a `zone` line in tools/lint/det-math-zone.txt, and every translation
//                unit an `entry` line too, so felitronics-core's det-math lint audits it and follows its #includes.
//   FILES        the scan is driven by the target, and fails closed: sources.txt is the target's list (cross-checked
//                against compile_commands.json with --build); every .cpp under the module is on it; every other file is
//                reached by the #include closure of those units or is a public header; a file of unknown type is
//                refused rather than skipped. Only modules/session/tests is outside the scan.
//
// WHAT IT CANNOT DO, said plainly: it reads the text the preprocessor has not expanded, so a macro could spell a token it
// looks for out of pieces (`#define T thr ## ow`), and it cannot see what a flag or a source property in a CMake file
// does to a unit (the compile-line gate over compile_commands.json and src/BuildGuards.h hold that).
//
// Usage: node tools/lint/check-session-laws.mjs [--build <dir>] [--self-test]     (from the repository root)
// Exit status: 0 clean · 1 violations (each `file:line: [RULE] why`) · 2 not run from a repository root.

import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, dirname, normalize, relative, sep, posix } from 'node:path';
import { fileURLToPath } from 'node:url';

const RUN_AS_PROGRAM = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];

const MODULE = 'modules/session';
const TESTS = `${MODULE}/tests`;
const INCLUDE_ROOT = `${MODULE}/include`;
const SOURCES_FILE = `${MODULE}/sources.txt`;
const ZONE_FILE = 'tools/lint/det-math-zone.txt';
const NOT_CODE = new Set([`${MODULE}/CMakeLists.txt`, SOURCES_FILE, `${MODULE}/build-flags.txt`]);
const CODE_EXT = /\.(h|hh|hpp|hxx|inl|ipp|tpp|inc|cpp|cc|cxx)$/;
const TU_EXT = /\.(cpp|cc|cxx)$/;
const CONDITIONALS_ALLOWED = new Set([`${MODULE}/src/BuildGuards.h`, `${MODULE}/src/BuildContract.cpp`]);

// The standard headers the session may include. Not a list of what is forbidden — a list of what is allowed, so a header
// nobody thought about is refused by default. None of these reaches the operating system, a file, the console, the
// locale, a thread, the clock or process-wide state by including it; what they declare that does (std::to_string of a
// double, std::stod) is a call, and a call is the object-file gate's.
const STD_ALLOWED = new Set(['algorithm', 'array', 'bit', 'charconv', 'cfloat', 'climits', 'cmath', 'compare', 'concepts',
    'cstddef', 'cstdint', 'cstring', 'initializer_list', 'iterator', 'limits', 'memory', 'new', 'numeric', 'optional',
    'span', 'string', 'string_view', 'tuple', 'type_traits', 'utility', 'variant', 'vector']);

//==============================================================================
// THE LEXER. Comments blanked (and, for the token rules, string and character literals too), newlines kept.
function blank (s) { return s.replace(/[^\n]/g, ' '); }
export function strip (src, keepStrings)
{
    let out = '';
    for (let i = 0; i < src.length;)
    {
        const two = src.slice(i, i + 2);
        if (two === '/*') { const e = src.indexOf('*/', i + 2); const end = e < 0 ? src.length : e + 2; out += blank(src.slice(i, end)); i = end; continue; }
        if (two === '//') { const e = src.indexOf('\n', i); const end = e < 0 ? src.length : e; out += blank(src.slice(i, end)); i = end; continue; }
        const raw = /^(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(/.exec(src.slice(i, i + 24));
        if (raw && ! /[A-Za-z0-9_]/.test(src[i - 1] || ''))
        {
            const close = ')' + raw[1] + '"'; const e = src.indexOf(close, i + raw[0].length);
            const end = e < 0 ? src.length : e + close.length;
            out += keepStrings ? src.slice(i, end) : blank(src.slice(i, end)); i = end; continue;
        }
        if (src[i] === '\'' && /[0-9a-fA-F]/.test(src[i - 1] || '') && /[0-9a-fA-F]/.test(src[i + 1] || ''))
        { out += src[i]; i++; continue; }                     // a digit separator, not a character literal
        if (src[i] === '"' || src[i] === '\'')
        {
            const q = src[i]; let j = i + 1;
            while (j < src.length && src[j] !== q && src[j] !== '\n') { if (src[j] === '\\') j++; j++; }
            const end = Math.min(j + 1, src.length);
            out += keepStrings ? src.slice(i, end) : blank(src.slice(i, end)); i = end; continue;
        }
        out += src[i]; i++;
    }
    return out;
}

function lineAt (text, idx) { let n = 1; for (let i = 0; i < idx; i++) if (text[i] === '\n') n++; return n; }

// Preprocessor directives: { line, name, rest } — continuation lines joined, comments already gone.
export function directives (code)
{
    const out = [];
    const lines = code.split('\n');
    for (let n = 0; n < lines.length; n++)
    {
        const m = /^\s*#\s*([A-Za-z_]*)(.*)$/.exec(lines[n]);
        if (! m) continue;
        let rest = m[2]; const at = n;
        while (/\\\s*$/.test(rest) && n + 1 < lines.length) { rest = rest.replace(/\\\s*$/, ' ') + lines[++n]; }
        out.push({ line: at + 1, name: m[1], rest: rest.trim() });
    }
    return out;
}

//==============================================================================
// THE TOKEN RULES — over code with comments and literals blanked; preprocessor lines included, so a token inside an
// #if branch is found whatever the branch.
const TOKEN_RULES = [
    { rule: 'EXCEPTIONS', re: /(?<![A-Za-z0-9_$])(throw|try|catch|typeid|dynamic_cast|__try|__except|__finally|__leave)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`${t}\` — felitronics::session is compiled without exceptions and RTTI, and a branch another row preprocesses away is still this row's code (MSVC compiles a bare throw under /EHs-c-)` },
    { rule: 'NOSYMBOL', re: /(?<![A-Za-z0-9_$])(atomic|atomic_ref|atomic_flag|atomic_thread_fence|atomic_signal_fence|__atomic_[A-Za-z0-9_]+|__sync_[A-Za-z0-9_]+|_Interlocked[A-Za-z0-9_]*|__c11_atomic_[A-Za-z0-9_]+)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`${t}\` — an atomic exists to share a value with another thread, and the session has none; it compiles to instructions and leaves no symbol for the object-file gate` },
    { rule: 'NOSYMBOL', re: /(?<![A-Za-z0-9_$])(__builtin_readcyclecounter|__builtin_readsteadycounter|__rdtsc|__rdtscp|_rdtsc|__builtin_ia32_[A-Za-z0-9_]+|__builtin_arm_[A-Za-z0-9_]+|__builtin_aarch64_[A-Za-z0-9_]+|__builtin_wasm_[A-Za-z0-9_]+|__builtin_frame_address|__builtin_return_address)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`${t}\` — a clock, an address or a target instruction read without a symbol: invisible to the object-file gate, and a different answer per run or per row` },
    { rule: 'NOSYMBOL', re: /(?<![A-Za-z0-9_$])(asm|__asm|__asm__)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`${t}\` — inline assembly (or an asm label): code or names no flag and no object-file rule can judge` },
    { rule: 'ORDER', re: /(?<![A-Za-z0-9_$])(unordered_map|unordered_set|unordered_multimap|unordered_multiset)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`${t}\` — its iteration order is implementation-defined (and std::hash with it): one input, three answers across libstdc++, libc++ and MSVC` },
    { rule: 'ORDER', re: /(?<![A-Za-z0-9_$])std\s*::\s*(hash)(?![A-Za-z0-9_$])/g,
      why: (t) => `\`std::${t}\` — its values are implementation-defined, and so is every order built on them` },
    { rule: 'ORDER', re: /(?<![A-Za-z0-9_$.>]|->)(?:(?:::\s*)?std\s*::\s*(?:ranges\s*::\s*)?)?(sort|partial_sort|partial_sort_copy|nth_element)\s*[(<]/g,
      why: (t) => `\`${t}\` — not stable: elements that compare equal come out in an implementation-defined order. Use std::stable_sort, or a comparator that is a total order` },
    { rule: 'PRAGMA', re: /(?<![A-Za-z0-9_$])(_Pragma|__pragma)\s*\(/g,
      why: (t) => `\`${t}(\` — a pragma in an expression: it changes code generation (floating-point contraction among it) for what follows, past every flag of the library` },
    { rule: 'PRAGMA', re: /\[\[\s*((?:gnu|clang)\s*::\s*(?:optimize|target|optnone))\b/g,
      why: (t) => `[[${t.replace(/\s+/g, '')}]] — a per-function change of optimisation or target, past every flag of the library` },
    { rule: 'PRAGMA', re: /__attribute__\s*\(\s*\(\s*(optimize|target|optnone)\b/g,
      why: (t) => `__attribute__((${t})) — a per-function change of optimisation or target, past every flag of the library` },
];

// A public header may declare, not define: a body would be compiled with the consumer's flags. Braces that open a
// namespace, a class, an enum or an initialiser are fine; a brace after a parameter list, after a constructor's
// initialiser list or after a lambda introducer opens a body.
const BODY_RE = [
    /\)\s*(?:const\s*|volatile\s*|noexcept\s*(?:\([^()]*\)\s*)?|override\s*|final\s*|&&?\s*|->\s*[^;{}()]+?\s*|requires\s+[^;{}]+?)*\{/g,
    /[=(,]\s*\[[^\]]*\]\s*(?:mutable\s*|constexpr\s*|noexcept\s*)*\{/g,
    /\}\s*\{/g,
];

function tokenViolations (code)
{
    const found = [];
    for (const r of TOKEN_RULES)
    {
        r.re.lastIndex = 0;
        for (const m of code.matchAll(r.re)) found.push({ line: lineAt(code, m.index), rule: r.rule, msg: r.why(m[1]) });
    }
    return found;
}

// The #include path as written, canonical or not: backslashes, `.`, `..`, empty segments and absolute paths are not.
// (Case is checked where it can be decided: a standard header is spelled in lower case, as STD_ALLOWED is; a project
// header must match the file's own name letter for letter — see existsExactCase.)
export function canonicalInclude (path)
{
    if (/\\/.test(path) || path.startsWith('/') || /^[A-Za-z]:/.test(path)) return false;
    return path.split('/').every(seg => seg !== '' && seg !== '.' && seg !== '..');
}

// Does `p` exist with exactly this spelling? On a case-insensitive file system (macOS, Windows) `existsSync` says yes to
// `session.h` for `Session.h`, and a build that works there fails on Linux — or picks a different file.
function existsExactCase (p)
{
    if (! existsSync(p)) return false;
    const parts = normalize(p).split(sep);
    const absolute = parts[0] === '';
    let dir = absolute ? sep : '.';
    for (let i = absolute ? 1 : 0; i < parts.length; i++)
    {
        const entries = readdirSync(dir);
        if (! entries.includes(parts[i])) return false;
        dir = join(dir, parts[i]);
    }
    return true;
}

//==============================================================================
function walk (dir, acc)
{
    for (const e of readdirSync(dir).sort())
    {
        const p = dir + '/' + e;
        if (p === TESTS) continue;
        if (statSync(p).isDirectory()) walk(p, acc); else acc.push(p);
    }
    return acc;
}

function rel (p) { return relative(process.cwd(), p).split(sep).join('/'); }

// The felitronics-core checkout this repository builds against: the one a --build tree resolved, or the sibling.
function coreRoot (buildDir)
{
    if (buildDir && existsSync(join(buildDir, 'CMakeCache.txt')))
    {
        const m = /^FELITRONICS_MASTERING_CORE_SOURCE_DIR:INTERNAL=(.*)$/m.exec(readFileSync(join(buildDir, 'CMakeCache.txt'), 'utf8'));
        if (m && existsSync(m[1])) return m[1];
    }
    const sibling = join(process.cwd(), '..', 'felitronics-core');
    return existsSync(join(sibling, 'modules')) ? sibling : null;
}

// Resolve a felitronics include in this repository or in core: the first include root that has it.
function resolveFelitronics (inc, core)
{
    const roots = [];
    for (const base of [process.cwd(), core].filter(Boolean))
    {
        const modules = join(base, 'modules');
        if (! existsSync(modules)) continue;
        for (const m of readdirSync(modules)) roots.push(join(modules, m, 'include'));
    }
    for (const r of roots) { const p = join(r, inc); if (existsExactCase(p)) return p; }
    return null;
}

//==============================================================================
export function scanFile (path, text, opts)
{
    const found = [];
    const V = (line, rule, msg) => found.push({ line, rule, msg });
    const noComments = strip(text, true);
    const dirs = directives(noComments);
    // The token rules read every line of code, #if branches included — but not an #include's header name, which the
    // INCLUDE rule judges (`<atomic>` is a header there, not a use).
    const code = strip(text, false).split('\n').map(l => /^\s*#\s*(include|include_next|import)\b/.test(l) ? blank(l) : l).join('\n');
    const includes = [];
    for (const d of dirs)
    {
        if (d.name === 'include' || d.name === 'include_next' || d.name === 'import')
        {
            const m = /^([<"])([^>"]*)[>"]/.exec(d.rest);
            if (! m) { V(d.line, 'INCLUDE', `#${d.name} ${d.rest} — a computed include is one nobody audits`); continue; }
            includes.push({ line: d.line, kind: m[1], path: m[2] });
            continue;
        }
        if (d.name === 'pragma')
        {
            if (d.rest.replace(/\s+/g, ' ') !== 'once')
                V(d.line, 'PRAGMA', `#pragma ${d.rest} — the only pragma a session source may carry is \`#pragma once\`: a local pragma changes floating-point semantics or code generation past every flag of the library (\`#pragma clang fp contract(fast)\` fuses under -ffp-contract=off)`);
            continue;
        }
        if (/^(if|ifdef|ifndef|elif|elifdef|elifndef|else)$/.test(d.name) && ! CONDITIONALS_ALLOWED.has(path))
            V(d.line, 'CONDITIONAL', `#${d.name} ${d.rest} — conditional compilation outside src/BuildGuards.h and src/BuildContract.cpp: a branch is code one row compiles and another does not, and the session is one program on every row`);
    }
    for (const t of tokenViolations(code)) found.push(t);
    if (path.startsWith(INCLUDE_ROOT + '/'))
    {
        for (const re of BODY_RE)
        {
            re.lastIndex = 0;
            for (const m of code.matchAll(re))
                V(lineAt(code, m.index + m[0].length - 1), 'BODY', `a function body in a public header — it is compiled under the consumer's flags, not the library's. Declare it here and define it in src/ (\`= default\` and \`= delete\` are not bodies)`);
        }
    }
    // includes: canonical, allowed, resolvable — and the closure's next files
    const next = [];
    for (const inc of includes)
    {
        if (! canonicalInclude(inc.path))
        { V(inc.line, 'INCLUDE', `#include ${inc.kind}${inc.path}${inc.kind === '<' ? '>' : '"'} — not canonically spelled (no \`./\`, no \`..\`, no backslash, no capital letter): a header must be named the one way the build and this lint both read it`); continue; }
        if (inc.kind === '"')
        {
            const p = normalize(join(dirname(path), inc.path)).split(sep).join('/');
            if (! p.startsWith(MODULE + '/') || ! existsExactCase(p))
            { V(inc.line, 'INCLUDE', `#include "${inc.path}" — a quoted include must name a file inside ${MODULE}, spelled as the file is; this one resolves to ${p}${existsExactCase(p) ? '' : ', which does not exist with that spelling'}`); continue; }
            next.push(p);
            continue;
        }
        if (inc.path.startsWith('felitronics/'))
        {
            const p = resolveFelitronics(inc.path, opts.core);
            if (! p) { V(inc.line, 'INCLUDE', `#include <${inc.path}> resolves in neither this repository nor felitronics-core${opts.core ? '' : ' (no core checkout found: pass --build <dir>)'} — a header this lint cannot find is one it cannot audit`); continue; }
            const r = rel(p);
            if (r.startsWith(MODULE + '/')) next.push(r);
            continue;
        }
        if (! STD_ALLOWED.has(inc.path))
            V(inc.line, 'INCLUDE', `#include <${inc.path}> — not on the session's include allowlist (tools/lint/check-session-laws.mjs, STD_ALLOWED). The allowlist is the standard headers that reach no operating system, file, console, locale, thread, clock or process-wide state by being included; a header nobody reviewed is refused by default`);
    }
    return { found, next, firstInclude: includes[0] || null };
}

//==============================================================================
function selfTest ()
{
    const cases = [
        // [path, source, the rules that must fire, in order of line then rule]
        [`${MODULE}/src/A.cpp`, '#include "BuildGuards.h"\n#include <cstdint>\n#include <memory>\n', []],
        [`${MODULE}/src/A.cpp`, '#include <chrono>', ['INCLUDE']],
        [`${MODULE}/src/A.cpp`, '#include <./chrono>', ['INCLUDE']],
        [`${MODULE}/src/A.cpp`, '#include <./memory>', ['INCLUDE']],
        [`${MODULE}/src/A.cpp`, '#include <Memory>', ['INCLUDE']],
        [`${MODULE}/src/A.cpp`, '#include <cstdio>\n#include <atomic>', ['INCLUDE', 'INCLUDE']],
        [`${MODULE}/src/A.cpp`, '#include MACRO', ['INCLUDE']],
        [`${MODULE}/src/A.cpp`, '// #include <chrono>\n/* #include <thread> */', []],
        [`${MODULE}/src/A.cpp`, '#pragma once', []],
        [`${MODULE}/src/A.cpp`, '#if defined(__clang__)\n#pragma clang fp contract(fast)\n#endif', ['CONDITIONAL', 'PRAGMA']],
        [`${MODULE}/src/A.cpp`, '#pragma STDC FP_CONTRACT ON', ['PRAGMA']],
        [`${MODULE}/src/A.cpp`, 'double f () { _Pragma("clang fp contract(fast)") return 1.0; }', ['PRAGMA']],
        [`${MODULE}/src/A.cpp`, '__pragma(fp_contract(on)) int x;', ['PRAGMA']],
        [`${MODULE}/src/A.cpp`, '[[gnu::optimize("fast-math")]] double f ();', ['PRAGMA']],
        [`${MODULE}/src/A.cpp`, '__attribute__((target("fma"))) double f ();', ['PRAGMA']],
        [`${MODULE}/src/A.cpp`, 'void fail () {\n#if defined(_MSC_VER) && !defined(__clang__)\n    throw 1;\n#endif\n}', ['CONDITIONAL', 'EXCEPTIONS']],
        [`${MODULE}/src/A.cpp`, '#ifdef A\nint a;\n#else\nint b;\n#endif', ['CONDITIONAL', 'CONDITIONAL']],
        [`${MODULE}/src/A.cpp`, 'int f () noexcept { return 0; }', []],
        [`${MODULE}/src/A.cpp`, 'int f (B& b) { return typeid (b) == typeid (B); }', ['EXCEPTIONS', 'EXCEPTIONS']],
        [`${MODULE}/src/A.cpp`, 'const char* s = "throw try catch";', []],
        [`${MODULE}/src/A.cpp`, 'std::atomic<int> n;', ['NOSYMBOL']],
        [`${MODULE}/src/A.cpp`, 'auto t = __builtin_readcyclecounter ();', ['NOSYMBOL']],
        [`${MODULE}/src/A.cpp`, 'int counter asm ("counter") = 0;', ['NOSYMBOL']],
        [`${MODULE}/src/A.cpp`, 'std::unordered_map<int, int> m;', ['ORDER']],
        [`${MODULE}/src/A.cpp`, 'std::sort (v.begin(), v.end());', ['ORDER']],
        [`${MODULE}/src/A.cpp`, 'std :: ranges :: sort (v);', ['ORDER']],
        [`${MODULE}/src/A.cpp`, 'std::stable_sort (v.begin(), v.end()); list.sort (); p->sort ();', []],
        [`${MODULE}/src/A.cpp`, 'std::size_t h = std::hash<int>{} (3);', ['ORDER']],
        [`${MODULE}/src/BuildContract.cpp`, '#if defined(X)\n#endif', []],
        [`${MODULE}/src/BuildGuards.h`, '#if defined(X)\n#error "x"\n#endif', []],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'class S { public: S () noexcept = default; ~S (); int f () const noexcept; };', []],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'struct V { unsigned a = 0; int b {1}; };', []],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'inline int twice (int x) { return 2 * x; }', ['BODY']],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'class S { int f () const noexcept { return 1; } };', ['BODY']],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'struct S { S () : a {1} {} int a; };', ['BODY']],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'inline const auto k = [] { return 1; };', ['BODY']],
        [`${INCLUDE_ROOT}/felitronics/session/S.h`, 'auto f () -> int { return 1; }', ['BODY']],
    ];
    let bad = 0;
    for (const [path, src, want] of cases)
    {
        const got = scanFile(path, src, { core: null }).found.sort((a, b) => a.line - b.line).map(f => f.rule);
        if (got.join(',') !== want.join(','))
        { console.error(`  SELF-TEST FAIL: wanted [${want}], got [${got}] for ${path}: ${JSON.stringify(src)}`); bad++; }
    }
    const canonical = [['cstdint', true], ['./cstdint', false], ['a/../b.h', false], ['a\\b.h', false], ['a//b.h', false], ['/usr/include/x.h', false], ['Session.h', true]];
    for (const [p, want] of canonical)
        if (canonicalInclude(p) !== want) { console.error(`  SELF-TEST FAIL (canonical): ${p} should be ${want}`); bad++; }
    const total = cases.length + canonical.length;
    if (bad) { console.error(`session-laws lint self-test: ${bad} of ${total} cases wrong`); process.exit(1); }
    console.log(`session-laws lint self-test: ${total}/${total} cases correct`);
}

//==============================================================================
if (RUN_AS_PROGRAM)
{
    const args = process.argv.slice(2);
    if (args.includes('--self-test')) { selfTest(); process.exit(0); }
    let buildDir = null;
    for (let i = 0; i < args.length; i++)
    {
        if (args[i] === '--build' && args[i + 1]) { buildDir = args[++i]; continue; }
        console.error('usage: node tools/lint/check-session-laws.mjs [--build <dir>] [--self-test]'); process.exit(2);
    }
    if (! existsSync(SOURCES_FILE) || ! existsSync(ZONE_FILE))
    { console.error(`check-session-laws: run from the root of felitronics-mastering-core (no ${SOURCES_FILE} here)`); process.exit(2); }

    const violations = [];
    const V = (f, line, rule, msg) => violations.push({ f, line, rule, msg });
    const core = coreRoot(buildDir);

    // THE TARGET'S SOURCES — sources.txt, cross-checked against the compile commands a build wrote.
    const sources = readFileSync(SOURCES_FILE, 'utf8').split('\n').map(l => l.trim()).filter(l => l && ! l.startsWith('#'))
        .map(l => posix.join(MODULE, l));
    for (const s of sources)
        if (! existsSync(s) || ! TU_EXT.test(s) || ! s.startsWith(`${MODULE}/src/`))
            V(SOURCES_FILE, 0, 'FILES', `${s}: a translation unit of the library must be an existing .cpp/.cc/.cxx under ${MODULE}/src`);
    if (buildDir)
    {
        const cc = join(buildDir, 'compile_commands.json');
        if (! existsSync(cc)) V(cc, 0, 'FILES', `--build ${buildDir}: no compile_commands.json there to cross-check the target's sources with`);
        else
        {
            const compiled = new Set(JSON.parse(readFileSync(cc, 'utf8'))
                .filter(e => e.output && /(^|[\\/])felitronics_session\.dir[\\/]/.test(e.output))
                .map(e => rel(normalize(e.file.startsWith('/') || /^[A-Za-z]:/.test(e.file) ? e.file : join(e.directory, e.file)))));
            if (compiled.size === 0) V(cc, 0, 'FILES', `no entry of ${cc} builds felitronics_session — the cross-check would be of nothing`);
            for (const s of sources) if (compiled.size && ! compiled.has(s)) V(SOURCES_FILE, 0, 'FILES', `${s} is on sources.txt but the build does not compile it into felitronics_session`);
            for (const c of compiled) if (! sources.includes(c)) V(c, 0, 'FILES', `felitronics_session compiles ${c}, which sources.txt does not list — the lint would not scan it`);
        }
    }

    // THE FILES OF THE MODULE — every one accounted for.
    const all = walk(MODULE, []);
    for (const f of all)
    {
        if (NOT_CODE.has(f)) continue;
        if (! CODE_EXT.test(f)) { V(f, 0, 'FILES', `a file of an unknown type in the module — refused rather than skipped: it is either code this lint cannot classify, or it does not belong here`); continue; }
        if (TU_EXT.test(f) && ! sources.includes(f)) V(f, 0, 'FILES', `a translation unit that sources.txt does not list: the library does not compile it, and a file nothing compiles is not the session`);
    }

    // SCAN: the target's units and every file their #include closure reaches inside the module, whatever its name —
    // then every public header, which consumers include.
    const scanned = new Map();
    const queue = [...sources.filter(existsSync)];
    for (const f of all) if (f.startsWith(INCLUDE_ROOT + '/') && CODE_EXT.test(f)) queue.push(f);
    while (queue.length)
    {
        const f = queue.shift();
        if (scanned.has(f)) continue;
        const res = scanFile(f, readFileSync(f, 'utf8'), { core });
        scanned.set(f, res);
        for (const v of res.found) V(f, v.line, v.rule, v.msg);
        for (const n of res.next) if (! scanned.has(n)) queue.push(n);
    }
    for (const f of all)
        if (! NOT_CODE.has(f) && CODE_EXT.test(f) && ! scanned.has(f))
            V(f, 0, 'FILES', `nothing in the library compiles or includes this file, and it is not a public header — it is unscanned code, so it is refused`);

    // GUARD
    for (const s of sources)
    {
        const r = scanned.get(s);
        if (! r) continue;
        if (! r.firstInclude || r.firstInclude.path !== 'BuildGuards.h' || r.firstInclude.kind !== '"')
            V(s, r.firstInclude ? r.firstInclude.line : 0, 'GUARD', `the first #include of every translation unit must be "BuildGuards.h" — it refuses a unit compiled with exceptions, RTTI or fast-math, which is how a per-source flag override is caught`);
    }

    // ZONE
    const zone = new Set(), entry = new Set();
    for (const l of readFileSync(ZONE_FILE, 'utf8').split('\n'))
    {
        const m = /^\s*(zone|entry)\s+(\S+)\s+\S/.exec(l);
        if (m) (m[1] === 'zone' ? zone : entry).add(m[2]);
    }
    for (const f of scanned.keys())
    {
        if (! zone.has(f)) V(f, 0, 'ZONE', `not in the deterministic zone: ${ZONE_FILE} has no \`zone ${f} <why>\` line, so the det-math lint would let a system libm call in it through`);
        if (sources.includes(f) && ! entry.has(f)) V(f, 0, 'ZONE', `a translation unit of the library that is not a det-math entry point: ${ZONE_FILE} has no \`entry ${f} <why>\` line, so the det-math lint does not follow its #includes`);
    }

    if (violations.length)
    {
        for (const v of violations) console.error(`${v.f}${v.line ? ':' + v.line : ''}: [${v.rule}] ${v.msg}`);
        console.error(`\n^^ ${violations.length} violation(s) of the session laws.`);
        process.exit(1);
    }
    console.log(`session laws (source): clean — ${scanned.size} file(s) of ${MODULE} scanned from ${sources.length} translation unit(s)`
              + `${buildDir ? ' (cross-checked against ' + join(buildDir, 'compile_commands.json') + ')' : ''}, each in the det-math zone.`);
}
