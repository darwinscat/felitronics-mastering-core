// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE LAWS OF felitronics::session THAT ONLY THE SOURCE CAN SHOW (docs/SESSION.md has all of them and what holds each).
//
// WHAT THIS IS NOT. "No mutable state outside an object" and "no operating system, file, console, locale, clock or
// process state" are held by the OBJECT-FILE GATE (modules/session/tests/object-gates.cmake): the compiled object says
// exactly which symbols live in writable memory and which are called, however the source spelled them — through a macro,
// an asm label, a pointer, a header that made printf visible. A lexer answers those questions badly, and this one does
// not try. What it holds is what leaves no symbol in any object, and the spellings that would take the object's answer
// out of the object gate's reach:
//
//   INCLUDE      every #include is canonically spelled (no `./`, no `..`, no backslash, no capital) and on an allowlist:
//                a standard header the session may use, a felitronics header named one by one in FELITRONICS_ALLOWED
//                (a core header can set the FPU's flush-to-zero with no symbol, or pull in <atomic>: each is a reviewed
//                one-line addition), or a quoted header inside modules/session. Everything else is refused, with no list
//                of what is forbidden to go stale.
//   DIRECTIVE    no preprocessor directive but #include and `#pragma once` — and, in the files ALLOWANCES names, exactly
//                the directives it lists. #line, #include_next, #import, #embed, #error elsewhere: refused.
//   MACRO        no #define, no #undef, no `##`: the session needs no macro, and a macro is how a token is spelled out
//                of pieces (`thr ## ow`) or a body qualified past every rule here. The facade's FC_EXPORT is its one,
//                stated allowance.
//   CONDITIONAL  no #if / #ifdef / #ifndef / #elif / #else anywhere but src/BuildGuards.h and src/BuildContract.cpp (and
//                the facade's one `#if defined(__EMSCRIPTEN__)`): a branch is code one row compiles and another does
//                not, and the session is one program on every row.
//   PRAGMA       no pragma but `#pragma once`, and no `_Pragma(` or `__pragma(`. A local pragma changes floating-point
//                semantics for the code after it (`#pragma clang fp contract(fast)` makes clang fuse under
//                -ffp-contract=off — reproduced), and `#pragma section` / `data_seg` place data where they like.
//   ATTRIBUTE    an attribute is one of nodiscard, maybe_unused, likely, unlikely, noreturn, fallthrough, spelled in
//                [[ ]] with no namespace — every attribute of a list, `__x__` spellings read as `x`. No __attribute__,
//                no __declspec, no [[gnu::...]], [[clang::...]], [[using ns: ...]]: optimize and target change code
//                generation per function, section and allocate move data out of the places the object gate reads.
//   DIGRAPH      no alternative tokens (`<:` `:>` `<%` `%>` `%:` `%:%:`): every rule here reads `[`, `{` and `#`.
//   EXCEPTIONS   no throw, try, catch, typeid, dynamic_cast (nor MSVC's __try / __except / __finally / __leave), in any
//                branch of any #if: MSVC compiles a bare `throw` under /EHs-c-.
//   NOSYMBOL     no facility that compiles to instructions and leaves no symbol: atomics (std::atomic and friends,
//                __atomic_*, __sync_*, _Interlocked*), cycle counters and target intrinsics (__builtin_readcyclecounter,
//                __rdtsc, __builtin_ia32_* / _arm_ / _aarch64_), inline assembly (asm, __asm__), and the x86 FP
//                control register touched by hand — core's ScopedFlushToZero, _mm_setcsr / _mm_getcsr and every
//                _MM_SET_* / _MM_GET_* accessor macro (rounding mode, exception mask and state, flush-to-zero,
//                denormals-are-zero): what an admitted header brings that could change the session's arithmetic
//                with no symbol, since each compiles to a bare ldmxcsr / stmxcsr.
//   ORDER        no std::unordered_* containers, no hash<> (qualified or not), no std::sort / partial_sort /
//                partial_sort_copy / nth_element, and no `using namespace`: their order is implementation-defined
//                (libstdc++, libc++ and MSVC answer differently from one input), and a using-directive would let a
//                name reach std unqualified. std::stable_sort, or a total order, gives one answer everywhere.
//   BODY         no function body in modules/session/include — only `= default` and `= delete`. A body in the public
//                header is compiled under the CONSUMER's flags, and the library's flags say nothing about it.
//   MUTABLE      no `mutable`, anywhere in the scan. A mutable member is state that changes inside a const — or
//                constexpr — object: `struct S { mutable int n = 0; }; inline constexpr S s {};` in a public header is
//                one shared, changing variable in every consumer, and `constexpr` said it was not. The session keeps its
//                state in its objects and has no use for the keyword (nor for a mutable lambda), so it is refused in
//                every file, not only the public header.
//   PUBLIC       in modules/session/include, no variable with static storage duration that is not constexpr — at
//                namespace scope or a static member, `inline` or not — and no namespace-scope function declaration. A
//                variable a public header defines is emitted by the CONSUMER's translation units, where the object gate
//                never looks. And at namespace scope `T name (x);` is a function or a variable depending on what x is,
//                which no lexer can tell — so a free function is declared as a static member or a friend of a class.
//   GUARD        every translation unit's first #include is "BuildGuards.h", which refuses a unit compiled with
//                exceptions, RTTI, fast-math or (MSVC) /fp:contract — so a per-source flag override cannot slip past.
//   ZONE         every scanned file has a `zone` line in tools/lint/det-math-zone.txt, and every translation unit an
//                `entry` line too, so felitronics-core's det-math lint audits it and follows its #includes.
//   FILES        the scan is driven by the target, and fails closed: sources.txt is the target's list (cross-checked
//                against compile_commands.json with --build, as is the facade's one unit); every .cpp under the module
//                is on it; every other file is reached by the #include closure of those units or is a public header; a
//                file of unknown type is refused rather than skipped. Only modules/session/tests is outside the scan.
//
// THE SCOPE: modules/session, and the C boundary compiled with its flags — tools/wasm/fc_session.cpp and the ABI header
// it includes — under the same rules, with the allowances ALLOWANCES states (its FC_EXPORT macro, its emscripten
// headers, its one platform branch).
//
// BEFORE ANY RULE the text goes through translation phase 2 (every backslash-newline joined, a line map kept, so a token
// split across lines is read whole and reported on the line it starts), and the lexer consumes identifiers and
// preprocessing numbers whole: `'` is a digit separator only inside a pp-number that starts with a digit, so `u8'0'` is
// a character literal and not a separator that makes the rest of the line a literal.
//
// THE THREAT MODEL, said plainly (docs/SESSION.md): this catches honest mistakes and reasonable spelling variants, not
// deliberate obfuscation by a hostile author. It reads text no preprocessor has expanded; the structural bans above —
// no macros, no directives beyond the listed ones, no alternative tokens, no vendor attributes — close whole classes of
// spelling cheaply, and nothing here is a C preprocessor. What a flag or a source property in a CMake file does to a unit
// is the compile-line gate's (compile_commands.json) and src/BuildGuards.h's.
//
// Usage: node tools/lint/check-session-laws.mjs [--build <dir>] [--self-test]     (from the repository root)
// Exit status: 0 clean · 1 violations (each `file:line: [RULE] why`) · 2 not run from a repository root.

import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, dirname, normalize, relative, sep, posix, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';

const RUN_AS_PROGRAM = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];

const MODULE = 'modules/session';
const TESTS = `${MODULE}/tests`;
const INCLUDE_ROOT = `${MODULE}/include`;
const SOURCES_FILE = `${MODULE}/sources.txt`;
const ZONE_FILE = 'tools/lint/det-math-zone.txt';
const GUARDS = `${MODULE}/src/BuildGuards.h`;
const CONTRACT = `${MODULE}/src/BuildContract.cpp`;
const FACADE_TU = 'tools/wasm/fc_session.cpp';
const FACADE_ABI = 'tools/fc_session_abi.h';
// Files of the module that are not code, by name: the build's lists, the config's two documents and the text's two — data
// the build embeds (felitronics_toml_embed) and checks before the library is built.
const NOT_CODE = new Set([`${MODULE}/CMakeLists.txt`, SOURCES_FILE, `${MODULE}/build-flags.txt`,
                          `${MODULE}/config/targets.toml`, `${MODULE}/config/engine.toml`,
                          `${MODULE}/text/catalog.toml`, `${MODULE}/text/format.toml`]);
const CONFIG_TU = `${MODULE}/src/Config.cpp`;
const TEXT_TU = `${MODULE}/src/Text.cpp`;
const CODE_EXT = /\.(h|hh|hpp|hxx|inl|ipp|tpp|inc|cpp|cc|cxx)$/;
const TU_EXT = /\.(cpp|cc|cxx)$/;
// Where each translation unit's object lives in a CMake build tree (the `output`, or the -o, of its compile command).
const TARGET_DIRS = [['felitronics_session.dir', 'felitronics_session'], ['felitronics_session_facade.dir', 'felitronics_session_facade']];

// The standard headers the session may include. Not a list of what is forbidden — a list of what is allowed, so a header
// nobody thought about is refused by default. None of these reaches the operating system, a file, the console, the
// locale, a thread, the clock or process-wide state by including it; what they declare that does (std::to_string of a
// double, std::stod) is a call, and a call is the object-file gate's.
const STD_ALLOWED = new Set(['algorithm', 'array', 'bit', 'charconv', 'cfloat', 'climits', 'cmath', 'compare', 'concepts',
    'cstddef', 'cstdint', 'cstring', 'initializer_list', 'iterator', 'limits', 'memory', 'new', 'numeric', 'optional',
    'span', 'string', 'string_view', 'tuple', 'type_traits', 'utility', 'variant', 'vector']);

// The felitronics headers the session may include — each by name. Not `felitronics/...` wholesale: core's FlushToZero.h
// sets the FPU's flush-to-zero with no symbol any object gate could read, and several core headers pull <atomic> in. A
// header added here is reviewed, in one line, for what it does by being included.
const FELITRONICS_ALLOWED = new Set([
    'felitronics/session/Measurements.h',       // owned measurement values and exact arrays
    'felitronics/session/Session.h',             // this module's own public headers, scanned here as every file of it is
    'felitronics/session/Config.h',
    'felitronics/session/Commands.h',
    'felitronics/session/Events.h',             // value payloads only, scanned as every public header
    'felitronics/session/Wire.h',
    'felitronics/session/Snapshot.h',           // owned snapshots and codec declarations, scanned here

    'felitronics/session/Project.h',
    'felitronics/session/Text.h',
    // felitronics-toml (resolved in the checkout the build uses): its parser and canonical writer, over <algorithm>,
    // <cfloat>, <cmath>, <cstddef>, <cstdint>, <limits>, <optional>, <string>, <string_view>, <utility>, <variant> and
    // <vector> — no file, locale, libc number conversion, exception or RTTI, by its own contract; the schema's Reader
    // over it (+ <concepts>, <type_traits>); and the views of an embedded document over it.
    'felitronics/toml/Toml.h',
    'felitronics/toml/Schema.h',
    'felitronics/toml/Embedded.h',
    'felitronics/core/DetMath.h',
    // The analyzers the config feeds, of this repository: the schema calls only their static storageFor(), a size
    // computation over its arguments. They bring core's DSP with them — FlushToZero.h through the EQ, whose
    // ScopedFlushToZero sets the FPU's flush-to-zero with no symbol, and <xmmintrin.h> behind it has the _MM_SET_* macros
    // (the rounding mode, the exception mask) that compile to a bare ldmxcsr; rule NOSYMBOL refuses all of them, and
    // _mm_setcsr / _mm_getcsr, as tokens in every session source, so what is admitted here cannot touch the register.
    'felitronics/analysis/ProgrammeReport.h', // deterministic analyzer storage declarations
    'felitronics/analysis/ClipDetector.h', // deterministic analyzer storage declarations
    'felitronics/analysis/SourceForensics.h', // deterministic analyzer storage declarations
    'felitronics/analysis/StereoColumns.h', // deterministic analyzer storage declarations
    'felitronics/analysis/WaveformPeaks.h', // deterministic analyzer storage declarations
    'felitronics/analysis/HumDetector.h', // deterministic analyzer storage declarations
    'felitronics/tempo/TempoDetector.h', // deterministic analyzer storage declarations
    'felitronics/storage/VectorBytes.h', // deterministic analyzer storage declarations
    'felitronics/analysis/LowEnd.h',
    'felitronics/analysis/BandCrest.h',
    'felitronics/analysis/StereoBandBursts.h',
]);

// THE STATED ALLOWANCES — the files outside the plain rules, and exactly what each may carry beyond them. A directive is
// matched as `<name> <rest>`, whitespace collapsed.
const GUARD_DIRECTIVES = [/^(?:if|ifdef|ifndef|elif|elifdef|elifndef|else|endif|error)(?: |$)/];
const ALLOWANCES = new Map([
    // The build guards and the build contract: preprocessor LOGIC, and nothing that defines a macro.
    [GUARDS, { directives: GUARD_DIRECTIVES }],
    [CONTRACT, { directives: GUARD_DIRECTIVES }],
    // The config compiled in: the two headers the build GENERATES from modules/session/config/*.toml
    // (felitronics_toml_embed; tools/wasm/build.sh runs the same tool) — constexpr data of felitronics-toml's embedded
    // types, admitted by name and not scanned: they live in the build tree, and what they compile to is read-only data
    // the object-file gate reads.
    [CONFIG_TU, { directives: [], generated: new Set(['embedded/engine.h', 'embedded/targets.h', 'embedded/versions.h']) }],
    // The text compiled in, the same way: the two headers the build generates from modules/session/text/*.toml.
    [TEXT_TU, { directives: [], generated: new Set(['embedded/catalog.h', 'embedded/format.h']) }],
    // The C boundary: the export macro build.sh's export scanner reads, the emscripten headers and one platform branch
    // (natively there is no linear memory to bound an out-pointer by), and its two quoted includes, by name.
    [FACADE_TU, {
        directives: [/^if defined ?\( ?__EMSCRIPTEN__ ?\)$/, /^else$/, /^endif$/, /^define FC_EXPORT extern "C"(?: EMSCRIPTEN_KEEPALIVE)?$/],
        std: new Set(['emscripten/emscripten.h', 'emscripten/heap.h']),
        quoted: new Map([['fc_session_abi.h', FACADE_ABI], ['BuildGuards.h', GUARDS]]) }],
    // The ABI header, which C compiles too: an include guard, the ABI's integer constants, and the C++ linkage block.
    [FACADE_ABI, {
        directives: [/^ifndef FC_SESSION_ABI_H$/, /^define FC_SESSION_ABI_H$/, /^define FC_SESSION_[A-Z0-9_]+ [0-9]+u$/,
                     /^ifdef __cplusplus$/, /^endif$/],
        std: new Set(['stdint.h']) }],
]);

// The attributes session code may carry, all of them standard and none of them able to change code generation or where
// data lives.
const ATTRIBUTES_ALLOWED = new Set(['nodiscard', 'maybe_unused', 'likely', 'unlikely', 'noreturn', 'fallthrough']);
const PLACEMENT = /\b(section|allocate|code_seg|data_seg|bss_seg|const_seg|init_seg|alloc_text)\b/;

//==============================================================================
// TRANSLATION PHASE 2 — every backslash-newline joined (with trailing blanks between them, which gcc and clang accept and
// C++23 makes standard), and a map from each character of the joined text to the line it came from.
export function phase2 (src)
{
    let out = '';
    const map = [];
    let line = 1;
    for (let i = 0; i < src.length;)
    {
        if (src[i] === '\\')
        {
            const m = /^\\[ \t]*\r?\n/.exec(src.slice(i, i + 256));
            if (m) { line++; i += m[0].length; continue; }
        }
        out += src[i]; map.push(line);
        if (src[i] === '\n') line++;
        i++;
    }
    return { text: out, lineOf: (idx) => (idx < map.length ? map[idx] : line) };
}

//==============================================================================
// THE LEXER. Comments blanked (and, for the token rules, string and character literals too), newlines and length kept.
// Identifiers and preprocessing numbers are consumed WHOLE: that is what decides that `u8'0'` is an encoding prefix and
// a character literal, and that the `'` of `1'000` is a digit separator.
function blank (s) { return s.replace(/[^\n]/g, ' '); }
const ID_START = /[A-Za-z_$\u0080-￿]/;
const ID_CHAR = /[A-Za-z0-9_$\u0080-￿]/;
export function strip (src, keepStrings)
{
    let out = '';
    const literal = (i, q) =>        // the index past a quoted literal whose opening quote is at i
    {
        let j = i + 1;
        while (j < src.length && src[j] !== q && src[j] !== '\n') { if (src[j] === '\\') j++; j++; }
        return Math.min(j + 1, src.length);
    };
    const raw = (i) =>               // a raw string starting at i (its prefix included), or -1
    {
        const m = /^(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(/.exec(src.slice(i, i + 24));
        if (! m) return -1;
        const close = ')' + m[1] + '"'; const e = src.indexOf(close, i + m[0].length);
        return e < 0 ? src.length : e + close.length;
    };
    const emit = (i, end) => { out += keepStrings ? src.slice(i, end) : blank(src.slice(i, end)); return end; };
    for (let i = 0; i < src.length;)
    {
        const two = src.slice(i, i + 2);
        if (two === '/*') { const e = src.indexOf('*/', i + 2); const end = e < 0 ? src.length : e + 2; out += blank(src.slice(i, end)); i = end; continue; }
        if (two === '//') { const e = src.indexOf('\n', i); const end = e < 0 ? src.length : e; out += blank(src.slice(i, end)); i = end; continue; }
        const c = src[i];
        if (ID_START.test(c))
        {
            let j = i + 1;
            while (j < src.length && ID_CHAR.test(src[j])) j++;
            const word = src.slice(i, j);
            if (/^(?:u8|u|U|L)?R$/.test(word) && src[j] === '"') { const end = raw(i); if (end > 0) { i = emit(i, end); continue; } }
            if (/^(?:u8|u|U|L)$/.test(word) && (src[j] === '"' || src[j] === '\'')) { i = emit(i, literal(j, src[j])); continue; }
            out += word; i = j; continue;
        }
        if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1] || '')))
        {
            let j = i + 1;
            while (j < src.length)
            {
                if ((src[j] === '+' || src[j] === '-') && /[eEpP]/.test(src[j - 1])) { j++; continue; }
                if (src[j] === '\'' && ID_CHAR.test(src[j + 1] || '')) { j += 2; continue; }
                if (ID_CHAR.test(src[j]) || src[j] === '.') { j++; continue; }
                break;
            }
            out += src.slice(i, j); i = j; continue;
        }
        if (c === '"' || c === '\'') { i = emit(i, literal(i, c)); continue; }
        out += c; i++;
    }
    return out;
}

// Preprocessor directives: { line, name, rest } — over phase-2 text (continuations already joined), comments gone. `%:`
// introduces a directive as `#` does.
export function directives (code, lineOf)
{
    const out = [];
    let off = 0;
    for (const l of code.split('\n'))
    {
        const m = /^\s*(?:#|%:)\s*([A-Za-z_][A-Za-z0-9_]*)?(.*)$/.exec(l);
        if (m) out.push({ line: lineOf(off), name: m[1] || '', rest: m[2].trim() });
        off += l.length + 1;
    }
    return out;
}

//==============================================================================
// THE TOKEN RULES — over code with comments and literals blanked; preprocessor lines included, so a token inside an
// #if branch is found whatever the branch.
const NOT_ID_BEFORE = '(?<![A-Za-z0-9_$\\u0080-\\uffff])';
const NOT_ID_AFTER = '(?![A-Za-z0-9_$\\u0080-\\uffff])';
const word = (alternatives) => new RegExp(`${NOT_ID_BEFORE}(${alternatives})${NOT_ID_AFTER}`, 'g');
const TOKEN_RULES = [
    { rule: 'EXCEPTIONS', re: word('throw|try|catch|typeid|dynamic_cast|__try|__except|__finally|__leave'),
      why: (t) => `\`${t}\` — felitronics::session is compiled without exceptions and RTTI, and a branch another row preprocesses away is still this row's code (MSVC compiles a bare throw under /EHs-c-)` },
    { rule: 'MUTABLE', re: word('mutable'),
      why: () => '`mutable` — state that changes inside a const or constexpr object: in a public header\'s constexpr variable it is shared, changing state in every consumer, which the constexpr rule would pass. The session has no use for it' },
    { rule: 'NOSYMBOL', re: word('atomic|atomic_ref|atomic_flag|atomic_thread_fence|atomic_signal_fence|__atomic_[A-Za-z0-9_]+|__sync_[A-Za-z0-9_]+|_Interlocked[A-Za-z0-9_]*|__c11_atomic_[A-Za-z0-9_]+'),
      why: (t) => `\`${t}\` — an atomic exists to share a value with another thread, and the session has none; it compiles to instructions and leaves no symbol for the object-file gate` },
    { rule: 'NOSYMBOL', re: word('__builtin_readcyclecounter|__builtin_readsteadycounter|__rdtsc|__rdtscp|_rdtsc|__builtin_ia32_[A-Za-z0-9_]+|__builtin_arm_[A-Za-z0-9_]+|__builtin_aarch64_[A-Za-z0-9_]+|__builtin_wasm_[A-Za-z0-9_]+|__builtin_frame_address|__builtin_return_address'),
      why: (t) => `\`${t}\` — a clock, an address or a target instruction read without a symbol: invisible to the object-file gate, and a different answer per run or per row` },
    { rule: 'NOSYMBOL', re: word('asm|__asm|__asm__'),
      why: (t) => `\`${t}\` — inline assembly (or an asm label): code or names no flag and no object-file rule can judge` },
    { rule: 'NOSYMBOL', re: word('ScopedFlushToZero|_mm_setcsr|_mm_getcsr|_MM_(?:SET|GET)_[A-Z0-9_]+'),
      why: (t) => `\`${t}\` — the FPU's control register touched by hand (x86 MXCSR, arm64 FPCR): its rounding mode, exception mask, flush-to-zero or denormals-are-zero set — every later operation of the thread rounds or traps differently — or read, and a bare ldmxcsr / msr leaves no symbol the object-file gate could read` },
    { rule: 'ORDER', re: word('unordered_map|unordered_set|unordered_multimap|unordered_multiset'),
      why: (t) => `\`${t}\` — its iteration order is implementation-defined (and std::hash with it): one input, three answers across libstdc++, libc++ and MSVC` },
    { rule: 'ORDER', re: new RegExp(`${NOT_ID_BEFORE}(std\\s*::\\s*hash${NOT_ID_AFTER}|hash\\s*<)`, 'g'),
      why: (t) => `\`${t.replace(/\s+/g, '')}\` — std::hash's values are implementation-defined, and so is every order built on them (qualified or not: a using-declaration or a namespace alias reaches the same template)` },
    { rule: 'ORDER', re: new RegExp(`${NOT_ID_BEFORE}(using\\s+namespace)${NOT_ID_AFTER}`, 'g'),
      why: () => '`using namespace` — a using-directive lets every unqualified name reach the whole namespace, std among them; the session names what it uses' },
    { rule: 'ORDER', re: /(?<![A-Za-z0-9_$.>]|->)(?:(?:::\s*)?std\s*::\s*(?:ranges\s*::\s*)?)?(sort|partial_sort|partial_sort_copy|nth_element)\s*[(<]/g,
      why: (t) => `\`${t}\` — not stable: elements that compare equal come out in an implementation-defined order. Use std::stable_sort, or a comparator that is a total order` },
    { rule: 'PRAGMA', re: new RegExp(`${NOT_ID_BEFORE}(_Pragma|__pragma)\\s*\\(`, 'g'),
      why: (t) => `\`${t}(\` — a pragma in an expression: it changes code generation (floating-point contraction among it) for what follows, past every flag of the library` },
    { rule: 'MACRO', re: /(##|%:%:)/g,
      why: (t) => `\`${t}\` — token pasting: a token spelled out of pieces, which no rule here can read. The session has no macros` },
    { rule: 'DIGRAPH', re: /(<%|%>|%:|:>|<:(?!:(?![:>])))/g,
      why: (t) => `\`${t}\` — an alternative token (digraph): every rule here reads \`[\`, \`{\` and \`#\` in their primary spelling, and the session uses nothing else` },
    { rule: 'ATTRIBUTE', re: word('__attribute__|__attribute|__declspec'),
      why: (t) => `\`${t}\` — a vendor attribute: optimize and target change code generation per function, section and allocate move data out of the places the object-file gate reads. Session code carries only [[nodiscard]], [[maybe_unused]], [[likely]], [[unlikely]], [[noreturn]], [[fallthrough]]` },
];

function tokenViolations (code, lineOf)
{
    const found = [];
    for (const r of TOKEN_RULES)
    {
        r.re.lastIndex = 0;
        for (const m of code.matchAll(r.re)) found.push({ line: lineOf(m.index), rule: r.rule, msg: r.why(m[1]) });
    }
    return found;
}

// Every [[ ... ]] — which C++ reserves for attributes, `[ [` with blanks between included — and every attribute in it.
function attributeViolations (code, lineOf)
{
    const found = [];
    const V = (at, msg) => found.push({ line: lineOf(at), rule: 'ATTRIBUTE', msg });
    const open = /\[\s*\[/g;
    for (let m; (m = open.exec(code));)
    {
        // the list runs to the `] ]` at depth 0
        let depth = 0, j = m.index + m[0].length, end = -1;
        for (; j < code.length; j++)
        {
            const c = code[j];
            if (c === '(' || c === '[' || c === '{') depth++;
            else if (c === ')' || c === '}') depth--;
            else if (c === ']')
            {
                if (depth > 0) { depth--; continue; }
                const k = /^\]\s*\]/.exec(code.slice(j, j + 64));
                if (k) { end = j; break; }
                break;
            }
        }
        if (end < 0) { V(m.index, '`[[` without its `]]` — an attribute list this lint cannot read is refused'); continue; }
        const list = code.slice(m.index + m[0].length, end);
        open.lastIndex = end;
        if (/^\s*using\b/.test(list)) { V(m.index, `[[using ...:]] — a vendor namespace for the whole list; session code carries only standard attributes`); continue; }
        // split at the top-level commas
        const items = [];
        let d = 0, from = 0;
        for (let k = 0; k < list.length; k++)
        {
            if ('([{'.includes(list[k])) d++;
            else if (')]}'.includes(list[k])) d--;
            else if (list[k] === ',' && d === 0) { items.push(list.slice(from, k)); from = k + 1; }
        }
        items.push(list.slice(from));
        for (const raw of items)
        {
            const item = raw.trim();
            if (item === '') continue;
            const a = /^(?:([A-Za-z_][A-Za-z0-9_]*)\s*::\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*(\([\s\S]*\))?\s*(\.\.\.)?$/.exec(item);
            if (! a) { V(m.index, `[[${item.replace(/\s+/g, ' ')}]] — an attribute this lint cannot read is refused`); continue; }
            const norm = (s) => (s && /^__.+__$/.test(s) ? s.slice(2, -2) : s);
            const ns = norm(a[1]), name = norm(a[2]);
            const spelled = `${a[1] ? a[1] + '::' : ''}${a[2]}`;
            if (PLACEMENT.test(name))
                V(m.index, `[[${spelled}]] — places data or code in a section of its own naming, out of the places the object-file gate reads as writable or read-only`);
            else if (ns)
                V(m.index, `[[${spelled}]] — a vendor attribute (${ns}::): optimize and target change code generation per function, and nothing vendor-specific is needed by the session. Session code carries only ${[...ATTRIBUTES_ALLOWED].join(', ')}`);
            else if (! ATTRIBUTES_ALLOWED.has(name))
                V(m.index, `[[${spelled}]] — not on the session's attribute allowlist (${[...ATTRIBUTES_ALLOWED].join(', ')})`);
        }
    }
    return found;
}

// A public header may declare, not define: a body would be compiled with the consumer's flags. Braces that open a
// namespace, a class, an enum or an initialiser are fine; a brace after a parameter list, after a constructor's
// initialiser list or after a lambda introducer opens a body.
const BODY_RE = [
    /\)\s*(?:constexpr\s*|consteval\s*|const\s*|volatile\s*|mutable\s*|static\s*|noexcept\s*(?:\([^()]*\)\s*)?|override\s*|final\s*|&&?\s*|\[\[[^\]]*\]\]\s*|->\s*[^;{}()]+?\s*|requires\s+[^;{}]+?)*\{/g,
    /[=(,]\s*\[[^\]]*\]\s*(?:mutable\s*|constexpr\s*|consteval\s*|static\s*|noexcept\s*)*\{/g,
    /\}\s*\{/g,
];

//==============================================================================
// PUBLIC — the declarations of a public header, at namespace and class scope, read from tokens. Not a C++ parser: it
// needs only to tell a type, an alias, a function and a variable apart, and where it could not (a namespace-scope
// `T name (x);`) the rule refuses the shape.
const TOKEN_RE = /[A-Za-z_$\u0080-￿][A-Za-z0-9_$\u0080-￿]*|\.?[0-9](?:[eEpP][+-]|'[A-Za-z0-9_$]|[A-Za-z0-9_$.\u0080-￿])*|::|->\*?|\.\.\.|<=>|<<=|>>=|[-+*/%&|^!=<>]=|&&|\|\||<<|>>|\+\+|--|\S/g;
function tokenize (code)
{
    const out = [];
    TOKEN_RE.lastIndex = 0;
    for (let m; (m = TOKEN_RE.exec(code));) out.push({ v: m[0], at: m.index });
    return out;
}
function matching (t, i, open, close)   // the index of the token closing the one at i
{
    let depth = 0;
    for (let j = i; j < t.length; j++)
    {
        if (t[j].v === open) depth++;
        else if (t[j].v === close && --depth === 0) return j;
    }
    return t.length - 1;
}
// Leading attributes, alignas, template headers and `export` — none of which says what the declaration is.
function skipPrefixes (t)
{
    let i = 0;
    for (;;)
    {
        const v = t[i] && t[i].v;
        if (v === '[' && t[i + 1] && t[i + 1].v === '[') { i = matching(t, i, '[', ']') + 1; continue; }
        if ((v === 'alignas' || v === '__attribute__' || v === '__declspec') && t[i + 1] && t[i + 1].v === '(') { i = matching(t, i + 1, '(', ')') + 1; continue; }
        if (v === 'template' && t[i + 1] && t[i + 1].v === '<')
        {
            let depth = 0, j = i + 1;
            for (; j < t.length; j++)
            {
                if (t[j].v === '<') depth++;
                else if (t[j].v === '>') depth--;
                else if (t[j].v === '>>') depth -= 2;
                if (depth <= 0) break;
            }
            i = j + 1; continue;
        }
        if (v === 'export') { i++; continue; }
        return t.slice(i);
    }
}
const NOT_A_DECLARATOR = new Set(['int', 'char', 'void', 'bool', 'short', 'long', 'unsigned', 'signed', 'float', 'double',
    'auto', 'wchar_t', 'char8_t', 'char16_t', 'char32_t', 'const', 'volatile', 'decltype', 'sizeof', 'alignof', 'alignas',
    'noexcept', 'typeof', '__typeof__', '__attribute__', '__declspec', 'requires', 'static_assert', 'return', 'static',
    'inline', 'extern', 'thread_local', 'constexpr', 'consteval', 'constinit', 'mutable', 'virtual', 'explicit', 'friend',
    'typename', 'template', 'class', 'struct', 'union', 'enum', 'new', 'delete']);
const PAREN_NOT_DECLARATOR = new Set(['decltype', 'alignas', 'sizeof', 'noexcept', 'alignof', 'typeof', '__typeof__',
    'requires', '__attribute__', '__declspec', 'explicit']);
const isIdentifier = (v) => ID_START.test(v[0]);

// { kind: 'type' | 'function' | 'variable', declSpec: [tokens before the declarator's `=`, `{` or `(`] }
function classify (stmt)
{
    const t = skipPrefixes(stmt);
    if (t.length === 0) return { kind: 'type', declSpec: [] };
    const first = t[0].v;
    if (['using', 'typedef', 'static_assert', 'friend', 'namespace'].includes(first)) return { kind: 'type', declSpec: [] };
    if (['class', 'struct', 'union', 'enum'].includes(first))
    {
        const body = t.findIndex(x => x.v === '{}');
        if (body >= 0) return body === t.length - 1 ? { kind: 'type', declSpec: [] } : { kind: 'variable', declSpec: t.slice(0, body) };
        if (first === 'enum')
        {
            let k = 1;
            if (t[k] && (t[k].v === 'class' || t[k].v === 'struct')) k++;
            if (t.length === k + 1 || (t[k + 1] && t[k + 1].v === ':')) return { kind: 'type', declSpec: [] };
            return { kind: 'variable', declSpec: t };
        }
        // `class A;`, `struct a::B;` — the key and a qualified name, nothing else
        const name = t.slice(1);
        if (name.length && name.every((x, k) => (k % 2 === 0 ? isIdentifier(x.v) : x.v === '::')) && name.length % 2 === 1)
            return { kind: 'type', declSpec: [] };
        return { kind: 'variable', declSpec: t };
    }
    let paren = -1, eq = -1, brace = -1;
    for (let k = 0, depth = 0; k < t.length; k++)
    {
        const v = t[k].v;
        if (v === '(')
        {
            if (depth === 0 && paren < 0 && ! (k > 0 && PAREN_NOT_DECLARATOR.has(t[k - 1].v))) paren = k;
            depth++;
        }
        else if (v === ')' || v === ']') depth--;
        else if (v === '[') depth++;
        else if (depth === 0 && v === '=' && eq < 0) eq = k;
        else if (depth === 0 && v === '{}' && brace < 0) brace = k;
    }
    const cut = Math.min(...[paren, eq, brace].map(x => (x < 0 ? t.length : x)));
    const declSpec = t.slice(0, cut);
    if (paren >= 0)
    {
        if (t.slice(0, paren).some(x => x.v === 'operator')) return { kind: 'function', declSpec };
        const before = (x) => x >= 0 && x < paren;
        if (! before(eq) && ! before(brace))
        {
            const prev = t[paren - 1];
            if (prev && (prev.v === '>' || (isIdentifier(prev.v) && ! NOT_A_DECLARATOR.has(prev.v)))) return { kind: 'function', declSpec };
        }
    }
    return { kind: 'variable', declSpec };
}

export function publicViolations (code, lineOf)
{
    const found = [];
    const t = tokenize(code.split('\n').map(l => (/^\s*(?:#|%:)/.test(l) ? blank(l) : l)).join('\n'));   // declarations, not directives
    const scopes = [{ kind: 'ns' }];
    let stmt = [];
    const judge = (s, scope) =>
    {
        if (s.every(x => x.v === '{}')) return;
        const c = classify(s);
        const at = (skipPrefixes(s)[0] || s[0]).at;
        const has = (w) => c.declSpec.some(x => x.v === w);
        if (scope === 'ns' && c.kind === 'function')
            found.push({ line: lineOf(at), rule: 'PUBLIC', msg: 'a function declared at namespace scope in a public header — declare it as a static member or a friend of a class: at namespace scope `T name (x);` is a function or a variable depending on what x is, and no lexer can tell which' });
        else if (c.kind === 'variable' && (scope === 'ns' || ['static', 'thread_local', 'extern', 'inline'].some(has)) && ! has('constexpr'))
            found.push({ line: lineOf(at), rule: 'PUBLIC', msg: 'a variable with static storage duration in a public header that is not constexpr — a consumer\'s translation unit emits it, under the consumer\'s flags, where the object-file gate never looks. Make it constexpr, or move it into src/' });
    };
    for (let i = 0; i < t.length; i++)
    {
        const v = t[i].v;
        const scope = scopes[scopes.length - 1].kind;
        if (v === '{')
        {
            const head = skipPrefixes(stmt).map(x => x.v);
            if (head[0] === 'namespace' || (head[0] === 'inline' && head[1] === 'namespace') || (head.length === 1 && head[0] === 'extern'))
            { scopes.push({ kind: 'ns' }); stmt = []; continue; }
            if (['class', 'struct', 'union'].includes(head[0]) && ! head.includes('(') && ! head.includes('='))
            { scopes.push({ kind: 'class', head: stmt }); stmt = []; continue; }
            const close = matching(t, i, '{', '}');          // an enum's body, an initialiser, a body: one placeholder
            stmt.push({ v: '{}', at: t[i].at });
            i = close;
            const open = stmt.reduce((n, x) => n + (x.v === '(' ? 1 : x.v === ')' ? -1 : 0), 0);
            if (open === 0 && classify(stmt).kind === 'function') { judge(stmt, scope); stmt = []; }   // a body ends its declaration
            continue;
        }
        if (v === '}')
        {
            const closed = scopes.length > 1 ? scopes.pop() : { kind: 'ns' };
            judge(stmt, closed.kind);
            stmt = closed.kind === 'class' ? [...closed.head, { v: '{}', at: t[i].at }] : [];
            continue;
        }
        if (v === ';') { judge(stmt, scope); stmt = []; continue; }
        if (scope === 'class' && stmt.length === 0 && ['public', 'private', 'protected'].includes(v) && t[i + 1] && t[i + 1].v === ':') { i++; continue; }
        stmt.push(t[i]);
    }
    judge(stmt, scopes[scopes.length - 1].kind);
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

// The felitronics-toml checkout this repository builds against: the one a --build tree resolved, or the sibling.
function tomlRoot (buildDir)
{
    if (buildDir && existsSync(join(buildDir, 'CMakeCache.txt')))
    {
        const m = /^FELITRONICS_MASTERING_TOML_SOURCE_DIR:INTERNAL=(.*)$/m.exec(readFileSync(join(buildDir, 'CMakeCache.txt'), 'utf8'));
        if (m && existsSync(m[1])) return m[1];
    }
    const sibling = join(process.cwd(), '..', 'felitronics-toml');
    return existsSync(join(sibling, 'include')) ? sibling : null;
}

// Resolve a felitronics include in this repository, in core or in felitronics-toml: the first include root that has it.
function resolveFelitronics (inc, core, toml)
{
    const roots = [];
    for (const base of [process.cwd(), core].filter(Boolean))
    {
        const modules = join(base, 'modules');
        if (! existsSync(modules)) continue;
        for (const m of readdirSync(modules)) roots.push(join(modules, m, 'include'));
    }
    if (toml) roots.push(join(toml, 'include'));
    for (const r of roots) { const p = join(r, inc); if (existsExactCase(p)) return p; }
    return null;
}

//==============================================================================
export function scanFile (path, text, opts)
{
    const found = [];
    const V = (line, rule, msg) => found.push({ line, rule, msg });
    const allowance = ALLOWANCES.get(path) || { directives: [] };
    const { text: joined, lineOf } = phase2(text);
    const noComments = strip(joined, true);
    const dirs = directives(noComments, lineOf);
    // The token rules read every line of code, #if branches included — but not an #include's header name, which the
    // INCLUDE rule judges (`<atomic>` is a header there, not a use).
    const code = strip(joined, false).split('\n').map(l => /^\s*(?:#|%:)\s*(include|include_next|import|embed)\b/.test(l) ? blank(l) : l).join('\n');
    const includes = [];
    for (const d of dirs)
    {
        const whole = `${d.name} ${d.rest}`.replace(/\s+/g, ' ').trim();
        if (d.name === 'include')
        {
            const m = /^([<"])([^>"]*)[>"]/.exec(d.rest);
            if (! m) { V(d.line, 'INCLUDE', `#include ${d.rest} — a computed include is one nobody audits`); continue; }
            includes.push({ line: d.line, kind: m[1], path: m[2] });
            continue;
        }
        if (d.name === 'pragma')
        {
            const rest = d.rest.replace(/\s+/g, ' ');
            if (rest !== 'once')
                V(d.line, 'PRAGMA', PLACEMENT.test(rest)
                    ? `#pragma ${d.rest} — places code or data in a section of its own naming, out of the places the object-file gate reads`
                    : `#pragma ${d.rest} — the only pragma a session source may carry is \`#pragma once\`: a local pragma changes floating-point semantics or code generation past every flag of the library (\`#pragma clang fp contract(fast)\` fuses under -ffp-contract=off)`);
            continue;
        }
        if (d.name === '' && d.rest === '') continue;                                     // the null directive
        if (allowance.directives.some(re => re.test(whole))) continue;
        if (d.name === 'define' || d.name === 'undef')
            V(d.line, 'MACRO', `#${whole} — the session defines no macros: a macro spells a token out of pieces, or qualifies a body, past every rule here`);
        else if (/^(if|ifdef|ifndef|elif|elifdef|elifndef|else)$/.test(d.name))
            V(d.line, 'CONDITIONAL', `#${whole} — conditional compilation outside src/BuildGuards.h and src/BuildContract.cpp: a branch is code one row compiles and another does not, and the session is one program on every row`);
        else if (d.name !== 'endif')
            V(d.line, 'DIRECTIVE', `#${whole} — a directive the session does not use (it has #include, #pragma once, and the stated allowances of tools/lint/check-session-laws.mjs): #line renames what every diagnostic reports, #include_next / #import / #embed reach files no rule here follows`);
    }
    for (const t of tokenViolations(code, lineOf)) found.push(t);
    for (const t of attributeViolations(code, lineOf)) found.push(t);
    if (path.startsWith(INCLUDE_ROOT + '/'))
    {
        for (const re of BODY_RE)
        {
            re.lastIndex = 0;
            for (const m of code.matchAll(re))
                V(lineOf(m.index + m[0].length - 1), 'BODY', `a function body in a public header — it is compiled under the consumer's flags, not the library's. Declare it here and define it in src/ (\`= default\` and \`= delete\` are not bodies)`);
        }
        for (const t of publicViolations(code, lineOf)) found.push(t);
    }
    // includes: canonical, allowed, resolvable — and the closure's next files
    const next = [];
    for (const inc of includes)
    {
        const spelled = `${inc.kind}${inc.path}${inc.kind === '<' ? '>' : '"'}`;
        if (! canonicalInclude(inc.path))
        { V(inc.line, 'INCLUDE', `#include ${spelled} — not canonically spelled (no \`./\`, no \`..\`, no backslash, no capital letter): a header must be named the one way the build and this lint both read it`); continue; }
        if (inc.kind === '"')
        {
            if (allowance.generated && allowance.generated.has(inc.path)) continue;    // the build's own output, by name
            if (allowance.quoted)
            {
                const p = allowance.quoted.get(inc.path);
                if (! p) V(inc.line, 'INCLUDE', `#include ${spelled} — not one of the quoted headers this file's allowance names (${[...allowance.quoted.keys()].join(', ')})`);
                else next.push(p);
                continue;
            }
            const p = normalize(join(dirname(path), inc.path)).split(sep).join('/');
            if (! p.startsWith(MODULE + '/') || ! existsExactCase(p))
            { V(inc.line, 'INCLUDE', `#include "${inc.path}" — a quoted include must name a file inside ${MODULE}, spelled as the file is; this one resolves to ${p}${existsExactCase(p) ? '' : ', which does not exist with that spelling'}`); continue; }
            next.push(p);
            continue;
        }
        if (inc.path.startsWith('felitronics/'))
        {
            if (! FELITRONICS_ALLOWED.has(inc.path))
            { V(inc.line, 'INCLUDE', `#include <${inc.path}> — not on the session's felitronics allowlist (tools/lint/check-session-laws.mjs, FELITRONICS_ALLOWED). Each felitronics header is admitted by name after a review of what it does by being included: core's FlushToZero.h sets the FPU's flush-to-zero with no symbol, and several core headers pull in <atomic>`); continue; }
            const p = resolveFelitronics(inc.path, opts.core, opts.toml);
            if (! p) { V(inc.line, 'INCLUDE', `#include <${inc.path}> resolves in none of this repository, felitronics-core and felitronics-toml${opts.core && opts.toml ? '' : ' (a checkout was not found: pass --build <dir>)'} — a header this lint cannot find is one it cannot audit`); continue; }
            const r = rel(p);
            if (r.startsWith(MODULE + '/')) next.push(r);
            continue;
        }
        if (! STD_ALLOWED.has(inc.path) && ! (allowance.std && allowance.std.has(inc.path)))
            V(inc.line, 'INCLUDE', `#include <${inc.path}> — not on the session's include allowlist (tools/lint/check-session-laws.mjs, STD_ALLOWED). The allowlist is the standard headers that reach no operating system, file, console, locale, thread, clock or process-wide state by being included; a header nobody reviewed is refused by default`);
    }
    return { found, next, firstInclude: includes[0] || null };
}

//==============================================================================
// THE COMPILE COMMANDS — which object an entry builds: its `output`, or else the -o / /Fo of its command. CMake 3.22's
// Ninja and Makefile generators write no `output` field.
export function splitCommand (cmd)
{
    const out = [];
    let cur = null, q = null;
    for (let i = 0; i < cmd.length; i++)
    {
        const c = cmd[i];
        if (q) { if (c === q) q = null; else if (c === '\\' && q === '"' && i + 1 < cmd.length) cur += cmd[++i]; else cur += c; continue; }
        if (c === '"' || c === '\'') { q = c; cur = cur || ''; continue; }
        if (c === '\\' && /["'\\\s]/.test(cmd[i + 1] || '')) { cur = (cur || '') + cmd[++i]; continue; }   // a Windows path keeps its separators
        if (/\s/.test(c)) { if (cur !== null) out.push(cur); cur = null; continue; }
        cur = (cur || '') + c;
    }
    if (cur !== null) out.push(cur);
    return out;
}
export function objectOf (e)
{
    if (typeof e.output === 'string' && e.output) return e.output;
    const args = Array.isArray(e.arguments) ? e.arguments : splitCommand(e.command || '');
    for (let i = 0; i < args.length; i++)
    {
        if (args[i] === '-o' && i + 1 < args.length) return args[i + 1];
        if (/^-o./.test(args[i])) return args[i].slice(2);
        if (/^[/-]Fo./.test(args[i])) return args[i].slice(3);
    }
    return null;
}

//==============================================================================
function selfTest ()
{
    const S = `${MODULE}/src/A.cpp`, H = `${INCLUDE_ROOT}/felitronics/session/S.h`;
    const cases = [
        // [path, source, the rules that must fire, in order of line then rule]
        [S, '#include "BuildGuards.h"\n#include <cstdint>\n#include <memory>\n', []],
        [S, '#include <chrono>', ['INCLUDE']],
        [S, '#include <./chrono>', ['INCLUDE']],
        [S, '#include <./memory>', ['INCLUDE']],
        [S, '#include <Memory>', ['INCLUDE']],
        [S, '#include <cstdio>\n#include <atomic>', ['INCLUDE', 'INCLUDE']],
        [S, '#include MACRO', ['INCLUDE']],
        [S, '// #include <chrono>\n/* #include <thread> */', []],
        [S, '#include <felitronics/core/FlushToZero.h>', ['INCLUDE']],
        [S, '#include <felitronics/session/Session.h>', []],                   // allowed by name, and resolved in this repository
        [S, '#include <felitronics/session/Config.h>\n#include <felitronics/analysis/LowEnd.h>', []],
        [S, '#include <felitronics/analysis/Loudness.h>', ['INCLUDE']],        // an analyzer the allowlist does not name
        [S, '#include "embedded/engine.h"', ['INCLUDE']],                       // generated headers: the config's unit only
        [CONFIG_TU, '#include "BuildGuards.h"\n#include "embedded/engine.h"\n#include "embedded/targets.h"', []],
        [CONFIG_TU, '#include "embedded/other.h"', ['INCLUDE']],
        [CONFIG_TU, '#include "embedded/catalog.h"', ['INCLUDE']],             // each unit its own generated headers
        [TEXT_TU, '#include "BuildGuards.h"\n#include "embedded/catalog.h"\n#include "embedded/format.h"', []],
        [TEXT_TU, '#include "embedded/engine.h"', ['INCLUDE']],
        [S, '#pragma once', []],
        [S, '#if defined(__clang__)\n#pragma clang fp contract(fast)\n#endif', ['CONDITIONAL', 'PRAGMA']],
        [S, '#pragma STDC FP_CONTRACT ON', ['PRAGMA']],
        [S, '#pragma data_seg(".session")', ['PRAGMA']],
        [S, 'double f () { _Pragma("clang fp contract(fast)") return 1.0; }', ['PRAGMA']],
        [S, '__pragma(fp_contract(on)) int x;', ['PRAGMA']],
        [S, 'void fail () {\n#if defined(_MSC_VER) && !defined(__clang__)\n    throw 1;\n#endif\n}', ['CONDITIONAL', 'EXCEPTIONS']],
        [S, '#ifdef A\nint a;\n#else\nint b;\n#endif', ['CONDITIONAL', 'CONDITIONAL']],
        // MACRO and DIRECTIVE
        [S, '#define T thr ## ow\nvoid f () { T 1; }', ['MACRO', 'MACRO']],
        [S, '#  define COUNTER static int n = 0;', ['MACRO']],
        [S, '#undef X', ['MACRO']],
        [S, '#line 1 "other.cpp"', ['DIRECTIVE']],
        [S, '#include_next <cstdint>', ['DIRECTIVE']],
        [S, '#error "x"', ['DIRECTIVE']],
        [S, '#\nint x;', []],
        // translation phase 2: a token split by backslash-newline is read whole, reported where it starts
        [S, 'int a;\nvoid f () { thr\\\now 1; }', ['EXCEPTIONS']],
        [S, '#def\\\nine X 1', ['MACRO']],
        [S, '// a comment \\\nthrow 1;', []],                                  // the comment continues onto the next line
        [S, 'int x; /* */ std::atom\\  \nic<int> n;', ['NOSYMBOL']],
        // pp-numbers and literals
        [S, 'char8_t c = u8\'0\'; void f () { throw 1; } int y = \'1\';', ['EXCEPTIONS']],
        [S, 'int n = 1\'000\'000; void f () { throw 1; }', ['EXCEPTIONS']],
        [S, 'auto h = 0x1\'e+1\'2; void f () { throw 1; }', ['EXCEPTIONS']],
        [S, 'const char* s = "throw try catch"; const char* r = R"x(throw)x"; auto c = U\'t\';', []],
        // DIGRAPH
        [S, '%:define X 1', ['MACRO', 'DIGRAPH']],
        [S, '<:<:gnu::section(".x"):>:> int y;', ['DIGRAPH', 'DIGRAPH', 'DIGRAPH', 'DIGRAPH']],
        [S, 'std::vector<::std::uint8_t> v; a<b>c;', []],
        // ATTRIBUTE
        [S, '[[nodiscard]] int f (); [[maybe_unused]] int g; [[__nodiscard__("why")]] int h ();', []],
        [S, 'void f (int x) { if (x) [[likely]] { return; } switch (x) { case 1: [[fallthrough]]; default: break; } }', []],
        [S, '[[gnu::optimize("fast-math")]] double f ();', ['ATTRIBUTE']],
        [S, '[[nodiscard, gnu::optimize("O0")]] double f ();', ['ATTRIBUTE']],
        [S, '[[gnu::section(".session_state")]] int n = 0;', ['ATTRIBUTE']],
        [S, '[[__gnu__::__section__(".s")]] int n = 0;', ['ATTRIBUTE']],
        [S, '[ [ gnu::section(".s") ] ] int n = 0;', ['ATTRIBUTE']],
        [S, '[[using gnu: section(".s")]] int n = 0;', ['ATTRIBUTE']],
        [S, '[[clang::optnone]] double f ();', ['ATTRIBUTE']],
        [S, '[[deprecated]] int f ();', ['ATTRIBUTE']],
        [S, '__attribute__((target("fma"))) double f ();', ['ATTRIBUTE']],
        [S, '__attribute__ ((section ("x"))) int n;', ['ATTRIBUTE']],
        [S, '__declspec(allocate("x")) int n;', ['ATTRIBUTE']],
        [S, 'int f () noexcept { return 0; }', []],
        [S, 'int f (B& b) { return typeid (b) == typeid (B); }', ['EXCEPTIONS', 'EXCEPTIONS']],
        [S, 'std::atomic<int> n;', ['NOSYMBOL']],
        [S, 'auto t = __builtin_readcyclecounter ();', ['NOSYMBOL']],
        [S, 'int counter asm ("counter") = 0;', ['NOSYMBOL']],
        [S, 'void f () { const felitronics::core::ScopedFlushToZero ftz; }', ['NOSYMBOL']],
        [S, 'void f () { _mm_setcsr (_mm_getcsr() | 0x8040u); }', ['NOSYMBOL', 'NOSYMBOL']],
        [S, 'void f () { _MM_SET_ROUNDING_MODE (_MM_ROUND_DOWN); }', ['NOSYMBOL']],
        [S, 'void f () { _MM_SET_EXCEPTION_MASK (_MM_MASK_MASK & ~_MM_MASK_INVALID); }', ['NOSYMBOL']],
        [S, 'void f () {\n_MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);\n_MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);\n_MM_SET_EXCEPTION_STATE (0);\n}', ['NOSYMBOL', 'NOSYMBOL', 'NOSYMBOL']],
        [S, 'unsigned f () { return _MM_GET_ROUNDING_MODE (); }', ['NOSYMBOL']],
        [S, 'constexpr unsigned kDown = 0x2000u; int _MM_SETTLE = 0; int x_MM_SET_ROUNDING_MODE = 0;', []],   // not an accessor
        // ORDER
        [S, 'std::unordered_map<int, int> m;', ['ORDER']],
        [S, 'std::sort (v.begin(), v.end());', ['ORDER']],
        [S, 'std :: ranges :: sort (v);', ['ORDER']],
        [S, 'std::stable_sort (v.begin(), v.end()); list.sort (); p->sort ();', []],
        [S, 'std::size_t h = std::hash<int>{} (3);', ['ORDER']],
        [S, 'using std::hash;\nstd::size_t h = hash<int>{} (3);', ['ORDER', 'ORDER']],
        [S, 'namespace s = std;\nauto h = s::hash <int>{} (3);', ['ORDER']],
        [S, 'using namespace std;', ['ORDER']],
        [S, 'int hash = 3; int rehash (int);', []],
        [CONTRACT, '#if defined(X)\n#endif', []],
        [GUARDS, '#if defined(X)\n#error "x"\n#endif', []],
        [GUARDS, '#define X 1', ['MACRO']],
        // the facade's allowance, and nothing past it
        [FACADE_TU, '#include "BuildGuards.h"\n#include "fc_session_abi.h"\n#if defined(__EMSCRIPTEN__)\n#include <emscripten/heap.h>\n#define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE\n#else\n#define FC_EXPORT extern "C"\n#endif', []],
        [FACADE_TU, '#define FC_PLANTED 1', ['MACRO']],
        [FACADE_TU, '#if defined(__APPLE__)\n#endif', ['CONDITIONAL']],
        [FACADE_TU, '#include "other.h"\n#include <cstdio>', ['INCLUDE', 'INCLUDE']],
        [FACADE_ABI, '#ifndef FC_SESSION_ABI_H\n#define FC_SESSION_ABI_H\n#define FC_SESSION_MAX_HANDLES 8u\n#include <stdint.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n#endif', []],
        [FACADE_ABI, '#define FC_SESSION_BAD(x) x', ['MACRO']],
        // BODY and PUBLIC
        [H, '#pragma once\n#include <cstdint>\nnamespace felitronics::session {\nclass S { public: S () noexcept = default; ~S (); int f () const noexcept; S& operator= (const S&) = delete; [[nodiscard]] static int g () noexcept; };\n}', []],
        [H, 'struct V { unsigned a = 0; int b {1}; };\nenum class E : unsigned char { A = 0, B = 1 };\nclass F;\nstruct Created { E status = E::A; std::unique_ptr<F> f; };', []],
        [H, 'using U = int; typedef int T; static_assert (sizeof (int) == 4); enum class G : int; inline constexpr int kMax = 8; struct K { static constexpr int kN = 3; };', []],
        [H, 'inline int twice (int x) { return 2 * x; }', ['BODY', 'PUBLIC']],
        [H, 'class S { int f () const noexcept { return 1; } };', ['BODY']],
        [H, 'struct S { S () : a {1} {} int a; };', ['BODY']],
        [H, 'inline const auto k = [] { return 1; };', ['BODY', 'PUBLIC']],
        [H, 'auto f () -> int { return 1; }', ['BODY', 'PUBLIC']],
        [H, 'auto l = [] () constexpr { return 1; };', ['BODY', 'PUBLIC']],
        [H, 'inline int x = 0;', ['PUBLIC']],
        [H, 'namespace n {\nint counter (int{0});\n}', ['PUBLIC']],
        [H, 'extern int y;', ['PUBLIC']],
        [H, 'const int kLimit = 3;', ['PUBLIC']],
        [H, 'constinit int z = 0;', ['PUBLIC']],
        [H, 'struct S { inline static int count = 0; static const int k = 1; static thread_local int t; };', ['PUBLIC', 'PUBLIC', 'PUBLIC']],
        [H, 'struct S { static int (*hook) (int); };', ['PUBLIC']],
        [H, 'struct W { int n; } g_w;', ['PUBLIC']],
        [H, 'struct W g_w2;', ['PUBLIC']],
        [H, 'int twice (int x);', ['PUBLIC']],
        // MUTABLE
        [H, 'struct S { mutable int n = 0; };\ninline constexpr S s {};', ['MUTABLE']],
        [S, 'auto f = [n = 0] () mutable { return ++n; };', ['MUTABLE']],
        [S, 'const char* c = "mutable"; // mutable in a comment', []],
    ];
    let bad = 0;
    for (const [path, src, want] of cases)
    {
        const got = scanFile(path, src, { core: null }).found.sort((a, b) => a.line - b.line).map(f => f.rule);
        if (got.join(',') !== want.join(','))
        { console.error(`  SELF-TEST FAIL: wanted [${want}], got [${got}] for ${path}: ${JSON.stringify(src)}`); bad++; }
    }
    // lines: a token split across a continuation is reported on the line it starts
    const split = scanFile(S, 'int a;\nvoid f () {\n    thr\\\now 1;\n}\nint b;\nvoid g () { throw 2; }', { core: null }).found.map(f => f.line);
    if (split.join(',') !== '3,7') { console.error(`  SELF-TEST FAIL (lines): wanted [3,7], got [${split}]`); bad++; }
    const canonical = [['cstdint', true], ['./cstdint', false], ['a/../b.h', false], ['a\\b.h', false], ['a//b.h', false], ['/usr/include/x.h', false], ['Session.h', true]];
    for (const [p, want] of canonical)
        if (canonicalInclude(p) !== want) { console.error(`  SELF-TEST FAIL (canonical): ${p} should be ${want}`); bad++; }
    const objects = [
        [{ output: 'a/felitronics_session.dir/src/S.cpp.o', command: 'c++ -o x.o -c S.cpp' }, 'a/felitronics_session.dir/src/S.cpp.o'],
        [{ command: 'c++ -DX=1 -o modules/session/CMakeFiles/felitronics_session.dir/src/S.cpp.o -c /r/S.cpp' }, 'modules/session/CMakeFiles/felitronics_session.dir/src/S.cpp.o'],
        [{ arguments: ['c++', '-c', '/r/S.cpp', '-oout/S.o'] }, 'out/S.o'],
        [{ command: 'cl.exe /nologo /Fofelitronics_session.dir\\Release\\S.obj /c S.cpp' }, 'felitronics_session.dir\\Release\\S.obj'],
        [{ command: 'c++ -c S.cpp' }, null],
    ];
    for (const [e, want] of objects)
        if (objectOf(e) !== want) { console.error(`  SELF-TEST FAIL (objectOf): ${JSON.stringify(e)} gave ${objectOf(e)}, not ${want}`); bad++; }
    const total = cases.length + 1 + canonical.length + objects.length;
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
    const toml = tomlRoot(buildDir);

    // THE TARGET'S SOURCES — sources.txt, cross-checked against the compile commands a build wrote; and the facade's one.
    const sources = readFileSync(SOURCES_FILE, 'utf8').split('\n').map(l => l.trim()).filter(l => l && ! l.startsWith('#'))
        .map(l => posix.join(MODULE, l));
    for (const s of sources)
        if (! existsSync(s) || ! TU_EXT.test(s) || ! s.startsWith(`${MODULE}/src/`))
            V(SOURCES_FILE, 0, 'FILES', `${s}: a translation unit of the library must be an existing .cpp/.cc/.cxx under ${MODULE}/src`);
    if (! existsSync(FACADE_TU)) V(FACADE_TU, 0, 'FILES', 'the C boundary\'s translation unit is not there — the scan would miss the one file that keeps globals');
    if (buildDir)
    {
        const cc = join(buildDir, 'compile_commands.json');
        if (! existsSync(cc)) V(cc, 0, 'FILES', `--build ${buildDir}: no compile_commands.json there to cross-check the target's sources with`);
        else
        {
            const expected = { felitronics_session: sources, felitronics_session_facade: [FACADE_TU] };
            const compiled = { felitronics_session: new Set(), felitronics_session_facade: new Set() };
            for (const e of JSON.parse(readFileSync(cc, 'utf8')))
            {
                const file = rel(normalize(isAbsolute(e.file) || /^[A-Za-z]:/.test(e.file) ? e.file : join(e.directory, e.file)));
                const obj = objectOf(e);
                if (obj === null)
                {
                    if (sources.includes(file) || file === FACADE_TU)
                        V(cc, 0, 'FILES', `the entry that compiles ${file} names no object (no \`output\`, no -o, no /Fo) — which target compiles it cannot be told`);
                    continue;
                }
                for (const [dir, target] of TARGET_DIRS)
                    if (new RegExp(`(^|[\\\\/])${dir.replace(/\./g, '\\.')}[\\\\/]`).test(obj)) compiled[target].add(file);
            }
            for (const [target, list] of Object.entries(expected))
            {
                const got = compiled[target];
                if (got.size === 0) { V(cc, 0, 'FILES', `no entry of ${cc} builds ${target} — the cross-check would be of nothing`); continue; }
                for (const s of list) if (! got.has(s)) V(target === 'felitronics_session' ? SOURCES_FILE : FACADE_TU, 0, 'FILES', `${s} is ${target === 'felitronics_session' ? 'on sources.txt' : 'the facade\'s unit'} but the build does not compile it into ${target}`);
                for (const c of got) if (! list.includes(c)) V(c, 0, 'FILES', `${target} compiles ${c}, which ${target === 'felitronics_session' ? 'sources.txt does not list' : 'is not the facade\'s one unit'} — the lint would not scan it`);
            }
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

    // SCAN: the target's units, the facade's, and every file their #include closure reaches inside the scope, whatever its
    // name — then every public header, which consumers include.
    const units = [...sources.filter(existsSync), ...(existsSync(FACADE_TU) ? [FACADE_TU] : [])];
    const scanned = new Map();
    const queue = [...units];
    for (const f of all) if (f.startsWith(INCLUDE_ROOT + '/') && CODE_EXT.test(f)) queue.push(f);
    while (queue.length)
    {
        const f = queue.shift();
        if (scanned.has(f)) continue;
        const res = scanFile(f, readFileSync(f, 'utf8'), { core, toml });
        scanned.set(f, res);
        for (const v of res.found) V(f, v.line, v.rule, v.msg);
        for (const n of res.next) if (! scanned.has(n)) queue.push(n);
    }
    for (const f of all)
        if (! NOT_CODE.has(f) && CODE_EXT.test(f) && ! scanned.has(f))
            V(f, 0, 'FILES', `nothing in the library compiles or includes this file, and it is not a public header — it is unscanned code, so it is refused`);

    // GUARD
    for (const s of units)
    {
        const r = scanned.get(s);
        if (! r) continue;
        if (! r.firstInclude || r.firstInclude.path !== 'BuildGuards.h' || r.firstInclude.kind !== '"')
            V(s, r.firstInclude ? r.firstInclude.line : 0, 'GUARD', `the first #include of every translation unit must be "BuildGuards.h" — it refuses a unit compiled with exceptions, RTTI, fast-math or /fp:contract, which is how a per-source flag override is caught`);
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
        if (units.includes(f) && ! entry.has(f)) V(f, 0, 'ZONE', `a translation unit that is not a det-math entry point: ${ZONE_FILE} has no \`entry ${f} <why>\` line, so the det-math lint does not follow its #includes`);
    }

    if (violations.length)
    {
        for (const v of violations) console.error(`${v.f}${v.line ? ':' + v.line : ''}: [${v.rule}] ${v.msg}`);
        console.error(`\n^^ ${violations.length} violation(s) of the session laws.`);
        process.exit(1);
    }
    console.log(`session laws (source): clean — ${scanned.size} file(s) scanned from ${units.length} translation unit(s) (${MODULE} and its C boundary)`
              + `${buildDir ? ', cross-checked against ' + join(buildDir, 'compile_commands.json') : ''}, each in the det-math zone.`);
}
