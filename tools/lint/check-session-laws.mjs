// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
//
// THE LAWS OF felitronics::session THAT A LEXER CAN HOLD (docs/SESSION.md has all of them and what holds each).
// A law that is only written down is a wish; these three are red builds.
//
//   GLOBALS  NO MUTABLE STATE OUTSIDE AN OBJECT. A variable with static storage duration that the code can change —
//            at namespace scope (named or anonymous namespace, `extern` included), a class's `static` data member, a
//            function-local `static`, anything `thread_local` — is refused. `constexpr` and `const` data are allowed:
//            a constant is not state. Why it matters here more than anywhere: a session is REPLAYED (the same
//            commands into a new instance must give the same session), and two sessions live side by side in one
//            module; a global is the one place their histories can meet. The C boundary is the one exception, and
//            tools/lint/session-laws.txt names each of its globals — two, the handle table and the poison flag.
//   OS       NO OPERATING SYSTEM, FILE, CONSOLE, LOCALE, THREAD OR CLOCK. The session is called synchronously by a
//            shell, gets its input as arguments and gives its answers as values; what a file says, what time it is
//            and which locale the user chose are the shell's business. A clock would make the sequence of events
//            depend on the machine; a locale would make a number print differently for two users; a thread would
//            make the order of work depend on the scheduler. Held on two levels: the HEADERS below are refused
//            outright, and the few CALLS that reach the same things through headers that cannot be refused
//            (<string>, <new>) are refused by name.
//   ZONE     THE DETERMINISTIC ZONE COVERS THE WHOLE MODULE. The det-math lint (felitronics-core's
//            tools/lint/check-det-math.mjs, run with --satellite) refuses a system libm call in every file that
//            tools/lint/det-math-zone.txt lists as `zone` — and ONLY in those. So a new file of modules/session is
//            outside the zone until someone remembers to list it, and a law that depends on remembering is not held.
//            This rule is the remembering: every file of a scanned directory must have its `zone` line.
//
// THE HEADER LIST, AND WHY EACH IS ON IT (the verdict prints the reason). Headers are refused, not audited, because a
// header is the moment a facility enters a file; what it then does with it is not a lexer's to judge.
//   files, the console   <fstream> <iostream> <istream> <ostream> <sstream> <spanstream> <strstream> <syncstream>
//                        <streambuf> <ios> <iosfwd> <iomanip> <print> <cstdio> <stdio.h> <filesystem>
//                        — the streams also format through the global locale, which is the next group's reason too.
//   the locale           <locale> <clocale> <locale.h> <codecvt> <cctype> <ctype.h> <cwctype> <wctype.h> <cwchar>
//                        <wchar.h> <regex> <format> <text_encoding>
//                        — isalpha and friends answer by locale; <regex>'s traits read it; std::format's `L` option
//                        does, and its only failure path is an exception. Numbers are formatted by the session's own
//                        declared rules. <charconv> is NOT on the list: to_chars/from_chars are exact and
//                        locale-free, which is what those rules stand on.
//   threads              <thread> <mutex> <shared_mutex> <condition_variable> <future> <latch> <barrier> <semaphore>
//                        <stop_token> <atomic> <stdatomic.h> <execution> <pthread.h> <threads.h> <rcu>
//                        <hazard_pointer>
//                        — <atomic> too, deliberately: an atomic exists to share a value with another thread, and the
//                        session has none. A long job runs in steps the shell drives; cancelling it is a call between
//                        two steps, not a flag another thread sets.
//   the clock            <chrono> <ctime> <time.h>   (and <sys/time.h>, with every <sys/...>, below)
//   randomness           <random> — std::random_device reads the operating system, and the standard distributions
//                        are implementation-defined (libstdc++, libc++ and MSVC answer differently from one seed).
//                        Anything random the session needs is its own declared generator.
//   the OS, the process  <cstdlib> <stdlib.h> <csignal> <signal.h> <csetjmp> <setjmp.h> <stacktrace> <debugging>
//                        <unistd.h> <fcntl.h> <dlfcn.h> <windows.h> <io.h> <direct.h>, and every <sys/...>,
//                        <mach/...>, <linux/...>, <emscripten...>
//                        — <cstdlib> is getenv, system, exit, a process-wide rand() and strtod, which reads the locale.
//                        The runtime (emscripten) belongs to the shell and the facade, never to the session.
//   process-wide state   <cerrno> <errno.h> <cfenv> <fenv.h> <memory_resource>
//                        — errno is a global; the floating-point environment (rounding mode, exception flags) is
//                        global state that changes what arithmetic returns; <memory_resource> carries the
//                        process-wide default resource, set_default_resource.
//   exceptions, RTTI     <exception> <stdexcept> <system_error> <typeinfo> <typeindex>
//                        — the library is compiled without both (modules/session/CMakeLists.txt); these headers are
//                        the only reason to reach for either.
// THE CALLS, by name, qualified with std:: or not, never a member of the same name:
//   to_string to_wstring stoi stol stoll stoul stoull stof stod stold   (<string>: the floating ones format and parse
//                        through the locale, and every sto* reports a bad parse by throwing)
//   set_new_handler set_terminate   (<new>, <exception>: process-wide handlers)
//   getenv setlocale rand srand atexit at_quick_exit   (should one arrive through a header that includes another)
//
// WHAT THIS LINT CANNOT DO, said plainly:
//   · It reads what the preprocessor has not expanded. A macro that expands to a global, an include or a call is
//     invisible to it, and preprocessor lines are blanked before the GLOBALS rule parses (both branches of an #if
//     are parsed; braces that only balance per branch are a [PARSE] error, which is red, not a pass).
//   · It checks the DIRECT includes of a scanned file. A permitted header that includes a forbidden one internally
//     (a standard library's <string> may pull in <iosfwd>) is not the session using it.
//   · It cannot see through a const object with a `mutable` member, a const pointer to mutable state it did not
//     declare, or state reached through a function of another module. The first two are the session's own code and
//     review's business; the third is that module's law.
//   · It decides "function declaration or variable?" lexically. `T name (x);` is read as a VARIABLE when the
//     parenthesis holds a single name, a literal or an expression, and as a function when it holds a parameter — a
//     type followed by a name, or a type keyword. An unnamed parameter of a user-defined type therefore reads as a
//     direct initialiser and is refused: name the parameter. Refusing a harmless line is the failure this errs toward.
//   · One declaration is judged by its first declarator: `const int* const a = p, *b = q;` passes as a whole. The
//     session declares one name per statement.
//
// Usage: node tools/lint/check-session-laws.mjs [--self-test]        (from the repository root)
//   --self-test  run the matcher's own cases — each rule's hits and its misses — and exit
// Exit status: 0 clean, 1 violations (each printed as `file:line: [RULE] why`), 2 not run from a repository root.

import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

// Everything below the matcher runs ONLY when this file is invoked as a program, so a harness can import the
// matcher without running the gate (the same arrangement as core's lints).
const RUN_AS_PROGRAM = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];

//==============================================================================
// THE LEXER. The words this lint matches occur constantly in prose — docs/SESSION.md's own comments say "static" and
// "<chrono>" while explaining why not — so comments are blanked before anything is matched, and for the GLOBALS and
// call rules string and character literals too. Newlines are kept, so line numbers survive.
function blank (s) { return s.replace(/[^\n]/g, ' '); }

// keepStrings: blank comments only (an #include "x.h" is a string literal and must survive for the OS rule).
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
        // A ' BETWEEN DIGITS IS A SEPARATOR (1'000), not the start of a character literal that swallows the line.
        if (src[i] === '\'' && /[0-9a-fA-F]/.test(src[i - 1] || '') && /[0-9a-fA-F]/.test(src[i + 1] || ''))
        { out += src[i]; i++; continue; }
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

// Preprocessor lines blanked, continuation lines with them. Run on text whose comments are already gone.
function blankPreprocessor (code)
{
    const lines = code.split('\n');
    for (let n = 0; n < lines.length; n++)
    {
        if (! /^\s*#/.test(lines[n])) continue;
        let k = n;
        for (;;)
        {
            const cont = /\\\s*$/.test(lines[k]);
            lines[k] = blank(lines[k]);
            if (! cont || k + 1 >= lines.length) break;
            k++;
        }
        n = k;
    }
    return lines.join('\n');
}

class ParseError extends Error { constructor (line, msg) { super(msg); this.line = line; } }

// Tokens: identifiers, numbers, and punctuation. `::`, `->` and `...` are one token each (a qualified name and a
// trailing return type are read by them); every other operator is one character, which is all this reader needs —
// it never evaluates an expression, and a `>>` that closes two template argument lists is two `>`.
export function tokenize (code)
{
    const toks = [];
    let line = 1;
    for (let i = 0; i < code.length;)
    {
        const c = code[i];
        if (c === '\n') { line++; i++; continue; }
        if (c === ' ' || c === '\t' || c === '\r' || c === '\f' || c === '\v') { i++; continue; }
        if (/[A-Za-z_$]/.test(c))
        {
            let j = i + 1; while (j < code.length && /[A-Za-z0-9_$]/.test(code[j])) j++;
            toks.push({ t: code.slice(i, j), id: true, line }); i = j; continue;
        }
        if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(code[i + 1] || '')))
        {
            let j = i + 1;
            while (j < code.length && (/[A-Za-z0-9_.']/.test(code[j]) || (/[+-]/.test(code[j]) && /[eEpP]/.test(code[j - 1])))) j++;
            toks.push({ t: code.slice(i, j), num: true, line }); i = j; continue;
        }
        const three = code.slice(i, i + 3), two = code.slice(i, i + 2);
        if (three === '...') { toks.push({ t: three, line }); i += 3; continue; }
        if (two === '::' || two === '->' || two === '==' || two === '!=') { toks.push({ t: two, line }); i += 2; continue; }
        toks.push({ t: c, line }); i++;
    }
    return toks;
}

const OPEN = { '(': ')', '[': ']', '{': '}' };
function matchClose (toks, i)
{
    const stack = [];
    for (let j = i; j < toks.length; j++)
    {
        const t = toks[j].t;
        if (OPEN[t]) stack.push(OPEN[t]);
        else if (t === ')' || t === ']' || t === '}')
        {
            if (stack.pop() !== t) throw new ParseError(toks[j].line, `unbalanced '${t}' (opened at line ${toks[i].line})`);
            if (stack.length === 0) return j;
        }
    }
    throw new ParseError(toks[i].line, `'${toks[i].t}' is never closed`);
}

// toks[i] is `<` opening a template argument list: the index just past its `>`.
function skipAngles (toks, i)
{
    let depth = 0;
    for (let j = i; j < toks.length; j++)
    {
        const t = toks[j].t;
        if (OPEN[t]) { j = matchClose(toks, j); continue; }
        if (t === '<') depth++;
        else if (t === '>') { if (--depth === 0) return j + 1; }
        else if (t === ';' || t === '{' || t === '}') break;
    }
    throw new ParseError(toks[i].line, `a template argument list that does not close`);
}

//==============================================================================
// THE GLOBALS RULE. A small reader of declarations: namespace scope, class scope and function bodies, told apart by
// what opens them. It finds every declaration with static storage duration and asks one question of it: can the code
// change it? It does not type-check; it reads shapes — and where a shape is ambiguous it refuses (see the header).

const CLASS_KEYS = new Set(['class', 'struct', 'union', 'enum']);
const QUALIFIERS = new Set(['const', 'volatile', 'noexcept', 'override', 'final', 'mutable', '&', 'throw', 'requires']);
const TYPE_WORDS = new Set(['void', 'bool', 'char', 'char8_t', 'char16_t', 'char32_t', 'wchar_t', 'short', 'int', 'long',
    'float', 'double', 'signed', 'unsigned', 'auto', 'const', 'volatile', 'decltype', 'typename', 'struct', 'class', 'enum']);
const SPECIFIERS = new Set(['static', 'extern', 'inline', 'constexpr', 'constinit', 'consteval', 'thread_local',
    'virtual', 'explicit', 'friend', 'mutable', 'register']);

export function scanGlobals (text)
{
    const code = blankPreprocessor(strip(text, false));
    const toks = tokenize(code);
    const found = [];           // { line, name, what }
    const report = (line, name, what) => found.push({ line, name, what });

    // Inside a function body or an initializer every `static` begins a function-local static, and every
    // `thread_local` is one whatever it begins. Nested braces are the same scope for this purpose.
    function scanLocal (from, to)
    {
        for (let j = from; j < to; j++)
        {
            const t = toks[j].t;
            if (t !== 'static' && t !== 'thread_local') continue;
            // the declaration runs to its `;` at this nesting level
            let k = j, braceDepth = 0;
            const head = [];
            for (; k < to; k++)
            {
                const u = toks[k].t;
                if (u === ';' && braceDepth === 0) break;
                if (OPEN[u]) { const c = matchClose(toks, k); head.push({ group: u, from: k, to: c, line: toks[k].line }); k = c; continue; }
                head.push(toks[k]);
            }
            const v = judge(head, true);
            if (v.function) { j = k; continue; }
            if (secondDeclarator(head) && ! allConstant(head)) report(toks[j].line, namesOf(head), SEVERAL);
            else if (v.mutable) report(toks[j].line, v.name, t === 'thread_local' ? 'a thread_local variable' : 'a function-local static');
            j = k;
        }
    }

    // A parenthesis group's tokens, top level only (nested groups as single items).
    function groupItems (g)
    {
        const items = [];
        for (let k = g.from + 1; k < g.to; k++)
        {
            if (OPEN[toks[k].t]) { const c = matchClose(toks, k); items.push({ group: toks[k].t, from: k, to: c }); k = c; continue; }
            items.push(toks[k]);
        }
        return items;
    }

    // Does this parenthesis group read as a PARAMETER LIST (a function declarator) rather than a direct initialiser?
    function looksLikeParameters (g)
    {
        const items = groupItems(g);
        if (items.length === 0) return true;
        if (items.length === 1 && items[0].t === 'void') return true;
        if (items.some(x => x.t === '...')) return true;
        // the first parameter, up to a top-level comma
        const first = [];
        for (const x of items) { if (x.t === ',') break; first.push(x); }
        if (first.length === 0) return false;
        if (first[0].id && TYPE_WORDS.has(first[0].t)) return true;
        // collapse qualified names and template argument lists into one NAME each; then a parameter is
        // NAME [ptr-ops] NAME, or NAME followed by ptr-ops alone (an unnamed pointer or reference parameter)
        const shape = [];
        for (let k = 0; k < first.length; k++)
        {
            const x = first[k];
            if (x.id && ! TYPE_WORDS.has(x.t))
            {
                while (first[k + 1] && first[k + 1].t === '::' && first[k + 2] && first[k + 2].id) k += 2;
                if (first[k + 1] && first[k + 1].t === '<')
                {
                    let depth = 0;
                    for (k = k + 1; k < first.length; k++)
                    {
                        if (first[k].t === '<') depth++;
                        else if (first[k].t === '>' && --depth === 0) break;
                    }
                }
                shape.push('N');
            }
            else if (x.t === '*' || x.t === '&') shape.push('P');
            else if (x.id) shape.push('N');                     // const/volatile after a type
            else shape.push('X');                               // a literal, an operator, a group: an expression
        }
        const s = shape.join('');
        return /^N+P*N/.test(s) && ! /X/.test(s) || /^NP+$/.test(s);
    }

    // Is this simple declaration (no body) a function declaration? The first top-level parenthesis group that is not
    // inside a template argument list and comes before any `=` decides.
    function isFunctionDeclaration (head)
    {
        let angle = 0;
        for (let k = 0; k < head.length; k++)
        {
            const x = head[k];
            if (x.t === '=') return false;
            if (x.t === 'operator') return true;                // operator==, operator(), operator new, operator T
            if (x.t === '<') { angle++; continue; }
            if (x.t === '>') { if (angle > 0) angle--; continue; }
            if (x.group === '{') return false;
            if (x.group === '(' && angle === 0)
            {
                const prev = head[k - 1];
                if (! prev) return false;
                const inner = groupItems(x);
                // `T (*fp)(int)` / `T (&r)[3]`: a declarator in parentheses — a variable
                if (inner.length && (inner[0].t === '*' || inner[0].t === '&' || inner[0].t === '::')) return false;
                if (prev.t === 'operator' || (prev.id && ! SPECIFIERS.has(prev.t)) || prev.t === '>' || prev.group === '(')
                    return prev.group === '(' ? true : looksLikeParameters(x);
                return false;
            }
        }
        return false;
    }

    // THE ONE QUESTION: can the code change what this declaration declares? `head` holds its tokens (groups folded)
    // up to its `;`. For a declaration that is a function, or declares nothing, the answer is "not state".
    function judge (head, local)
    {
        const words = head.filter(x => x.id).map(x => x.t);
        if (! local && isFunctionDeclaration(head)) return { mutable: false, function: true };
        // The declarator part: up to the first `=`, initializer group, direct-initializer, bit-field or comma.
        const decl = [];
        let angle = 0;
        for (let k = 0; k < head.length; k++)
        {
            const x = head[k];
            if (x.t === '<') angle++;
            if (x.t === '>' && angle > 0) angle--;
            if (angle === 0 && (x.t === '=' || x.group === '{' || x.t === ',' || x.t === ':')) break;
            if (angle === 0 && x.group === '(' && decl.length && (decl[decl.length - 1].id || decl[decl.length - 1].t === '>')
                && ! groupItems(x).some(y => y.t === '*' || y.t === '&')) break;
            decl.push(x);
        }
        if (local && isFunctionDeclarationLocal(decl)) return { mutable: false, function: true };
        // A parenthesised declarator — `(*callback)`, `(&row)` — is opened up: its name and its pointer are the
        // declaration's. Any other group (a function pointer's parameter list, an array bound) is not.
        const flat = [];
        for (const x of decl)
        {
            if (x.group === '(')
            {
                const inner = groupItems(x);
                if (inner.length && (inner[0].t === '*' || inner[0].t === '&')) { for (const y of inner) flat.push(y); }
                continue;
            }
            flat.push(x);
        }
        const names = flat.filter(x => x.id && ! SPECIFIERS.has(x.t) && x.t !== 'const' && x.t !== 'volatile');
        const name = names.length ? names[names.length - 1].t : '?';
        if (words.includes('thread_local')) return { mutable: true, name };
        if (words.includes('constexpr')) return { mutable: false, name };
        // pointer and reference operators of the declarator itself: top level, outside template arguments
        let lastPtr = -1, ptrKind = null, constBefore = false;
        angle = 0;
        for (let k = 0; k < flat.length; k++)
        {
            const x = flat[k];
            if (x.t === '<') { angle++; continue; }
            if (x.t === '>') { if (angle > 0) angle--; continue; }
            if (angle > 0) continue;
            if (x.t === 'const' && lastPtr < 0) constBefore = true;
            if (x.t === '*' || x.t === '&') { lastPtr = k; ptrKind = x.t; }
        }
        if (lastPtr < 0) return { mutable: ! constBefore, name };
        if (ptrKind === '*') return { mutable: ! (flat[lastPtr + 1] && flat[lastPtr + 1].t === 'const'), name };
        return { mutable: ! constBefore, name };                // a reference: to const, or an alias of state
    }

    // In a function body `static int f (int);` declares a function too — rare, and read the same way.
    function isFunctionDeclarationLocal (decl)
    {
        const last = decl[decl.length - 1];
        return !! (last && last.group === '(' && decl.length >= 2 && decl[decl.length - 2].id && looksLikeParameters(last));
    }

    // ONE NAME PER DECLARATION, for anything with static storage duration. `volatile int allowed = 0, hidden = 0;`
    // would otherwise be judged — and allowed — by its first name, and the second would pass under it. So a declaration
    // of several such names is refused whatever they are: a line a reviewer splits in two, never a hole.
    const SEVERAL = 'a declaration of several names with static storage duration (declare one per statement, so each is judged)';
    function secondDeclarator (head)
    {
        let angle = 0, init = false;
        for (const x of head)
        {
            if (! init && x.t === '<') angle++;
            else if (! init && x.t === '>' && angle > 0) angle--;
            else if (angle === 0 && x.t === '=') init = true;
            else if (angle === 0 && x.group === '{') init = true;
            else if (x.t === ',' && (init || angle === 0)) return true;
        }
        return false;
    }
    // The names a declaration declares: the last identifier of each declarator, before its initialiser.
    function namesOf (head)
    {
        const names = [];
        let angle = 0, init = false, last = null;
        for (const x of head)
        {
            if (! init && x.t === '<') angle++;
            else if (! init && x.t === '>' && angle > 0) angle--;
            else if (angle === 0 && (x.t === '=' || x.group === '{') && ! init) { init = true; if (last) names.push(last); last = null; }
            else if (x.t === ',' && (init || angle === 0)) { if (! init && last) names.push(last); init = false; last = null; }
            else if (! init && angle === 0 && x.id && ! SPECIFIERS.has(x.t) && ! TYPE_WORDS.has(x.t)) last = x.t;
            else if (! init && angle === 0 && x.group === '(')
            {
                const inner = groupItems(x).filter(y => y.id);
                if (inner.length) last = inner[inner.length - 1].t;
            }
        }
        if (! init && last) names.push(last);
        return names.join(', ');
    }
    // Every declarator of this head is a constant: `constexpr` binds each of them, and a `const` in front binds each
    // one that adds no pointer or reference of its own.
    function allConstant (head)
    {
        if (head.some(x => x.t === 'constexpr')) return true;
        let angle = 0, init = false, sawConst = false;
        for (const x of head)
        {
            if (! init && x.t === '<') { angle++; continue; }
            if (! init && x.t === '>') { if (angle > 0) angle--; continue; }
            if (angle > 0) continue;
            if (x.t === '=' || x.group === '{') { init = true; continue; }
            if (x.t === ',') { init = false; continue; }
            if (init) continue;
            if (x.t === 'const') sawConst = true;
            if (x.t === '*' || x.t === '&' || x.group === '(') return false;
        }
        return sawConst;
    }

    // Keywords that cannot occur inside a declaration: meeting one means the tokens before it were a macro invocation
    // with no semicolon (`FELITRONICS_FIR_EXACT_BEGIN` before a namespace), and the keyword starts the next statement.
    const STATEMENT_KEYWORDS = new Set(['namespace', 'using', 'typedef', 'static_assert', 'template']);

    // One declaration at namespace or class scope, from toks[i]. Returns the index after it.
    function declaration (i, end, scope)
    {
        const head = [];
        let assign = false, sawParen = false, ctorInit = false, arrow = false, classBody = false;
        const specifiers = new Set();
        let classKeyAt = -1, opaque = false, angle = 0, operatorName = false;
        for (let j = i; j < end; j++)
        {
            const tk = toks[j];
            if (tk.t === ';')
            {
                finish(head, specifiers, scope, classKeyAt, classBody, opaque, tk.line);
                return j + 1;
            }
            if (j > i && tk.id && STATEMENT_KEYWORDS.has(tk.t) && ! assign) return j;
            if (j > i && scope === 'class' && (tk.t === 'public' || tk.t === 'private' || tk.t === 'protected')
                && toks[j + 1] && toks[j + 1].t === ':') return j;
            // Template argument lists in the head, so a parenthesis inside one (`void_t<decltype (…)>`) is not taken
            // for a parameter list. `operator<`, `operator<<`: the tokens of an operator's name are not brackets.
            if (tk.t === 'operator')
            {
                head.push(tk);
                if (toks[j + 1] && toks[j + 1].t === '(' && toks[j + 2] && toks[j + 2].t === ')')   // operator()
                { head.push({ group: '(', from: j + 1, to: j + 2, line: tk.line }); j += 2; continue; }
                operatorName = true; continue;
            }
            if (operatorName && tk.t !== '(') { head.push(tk); continue; }
            operatorName = false;
            if (! assign && tk.t === '<') { angle++; head.push(tk); continue; }
            if (! assign && tk.t === '>') { if (angle > 0) angle--; head.push(tk); continue; }
            if (tk.t === '[' && toks[j + 1] && toks[j + 1].t === '[') { j = matchClose(toks, j); continue; }   // [[attr]]
            if ((tk.t === 'alignas' || tk.t === '__attribute__' || tk.t === '__declspec') && toks[j + 1] && toks[j + 1].t === '(')
            { j = matchClose(toks, j + 1); continue; }
            if (tk.t === '(' || tk.t === '[')
            {
                const c = matchClose(toks, j);
                const g = { group: tk.t, from: j, to: c, line: tk.line };
                if (assign) scanLocal(j + 1, c);                // a lambda in an initializer's arguments
                if (tk.t === '(' && ! assign && angle === 0) sawParen = true;
                head.push(g); j = c; continue;
            }
            if (tk.t === '{')
            {
                const c = matchClose(toks, j);
                const last = head[head.length - 1];
                if (assign) { scanLocal(j + 1, c); head.push({ group: '{', from: j, to: c, line: tk.line }); j = c; continue; }
                if (classKeyAt >= 0 && ! sawParen && ! classBody)
                {
                    if (head[classKeyAt].t !== 'enum') declSeq(j + 1, c, 'class');
                    classBody = true; head.push({ group: 'body', from: j, to: c, line: tk.line }); j = c; continue;
                }
                const lastIsName = last && (last.id && ! QUALIFIERS.has(last.t)) || (last && last.t === '>');
                if (sawParen && (arrow || ! lastIsName || (ctorInit && last.group)))
                {
                    if (ctorInit && lastIsName && ! arrow) { scanLocal(j + 1, c); head.push({ group: '{', from: j, to: c }); j = c; continue; }
                    scanLocal(j + 1, c);                        // a function body
                    return c + 1;
                }
                // a brace initialiser: `T x {…}`, or a mem-initialiser `a{…}` in a constructor's list
                scanLocal(j + 1, c); head.push({ group: '{', from: j, to: c, line: tk.line }); j = c; continue;
            }
            if (tk.t === '=' && ! (head.length && head[head.length - 1].t === 'operator')) assign = true;
            if (tk.t === '->' && sawParen && ! assign) arrow = true;
            if (tk.t === ':' && sawParen && ! assign) ctorInit = true;
            if (tk.id && SPECIFIERS.has(tk.t) && ! assign) specifiers.add(tk.t);
            if (tk.id && CLASS_KEYS.has(tk.t) && classKeyAt < 0 && ! sawParen && ! assign)
            {
                classKeyAt = head.length;
                // `class X;`, `struct X final;`, `enum class E : std::uint8_t;` — a declaration of a type, not a variable
                let k = j + 1;
                if (tk.t === 'enum' && toks[k] && (toks[k].t === 'class' || toks[k].t === 'struct')) k++;
                while (toks[k] && toks[k].t === '[' && toks[k + 1] && toks[k + 1].t === '[') k = matchClose(toks, k) + 1;
                // the tag: ONE (possibly qualified) name — a second name after it is a declarator (`struct X x;`)
                if (toks[k] && toks[k].id) { k++; while (toks[k] && toks[k].t === '::' && toks[k + 1] && toks[k + 1].id) k += 2; }
                if (toks[k] && toks[k].t === 'final') k++;
                if (toks[k] && toks[k].t === ';') opaque = true;
                // `enum class E : std::uint8_t;` — an enum base, and a `;` before any `{`
                if (toks[k] && tk.t === 'enum' && toks[k].t === ':')
                {
                    let m = k + 1;
                    while (m < end && toks[m].t !== ';' && toks[m].t !== '{') m++;
                    if (m < end && toks[m].t === ';') opaque = true;
                }
            }
            head.push(tk);
        }
        // Nothing but names up to the end of the scope: a macro invocation with no semicolon (a pragma wrapper closing
        // a file), not a declaration. Anything else that runs off the end is a file this reader cannot follow.
        if (head.every(x => x.id)) return end;
        throw new ParseError(toks[i].line, `a declaration that never ends (no ';' before the end of its scope)`);
    }

    function finish (head, specifiers, scope, classKeyAt, classBody, opaque, line)
    {
        if (head.length === 0 || opaque) return;
        const first = head[0];
        if (first.t === 'friend' || specifiers.has('friend')) return;
        let tail = head;
        if (classBody)
        {
            // declarators after a class body: `struct { … } name;` — judged with the specifiers in front of the key
            const b = head.findIndex(x => x.group === 'body');
            tail = head.slice(0, classKeyAt).concat(head.slice(b + 1));
            if (! head.slice(b + 1).some(x => x.id)) return;    // no declarator: a type definition
        }
        else if (classKeyAt >= 0)
        {
            // `struct X x;` — an elaborated type and a declarator; `struct X;` was handled as opaque
            tail = head.slice(0, classKeyAt).concat(head.slice(classKeyAt + 1));
        }
        if (scope === 'class' && ! specifiers.has('static') && ! specifiers.has('thread_local')) return;   // instance state
        // `NAME (args);` with nothing in front of the name declares nothing — a variable needs a type. It is a macro
        // invocation (a layout check, a registration) or, at class scope, a constructor.
        const firstGroup = tail.findIndex(x => x.group === '(');
        if (firstGroup === 1 && tail[0].id && ! TYPE_WORDS.has(tail[0].t)
            && ! groupItems(tail[1]).some((y, n) => n === 0 && (y.t === '*' || y.t === '&'))) return;
        const v = judge(tail, false);
        if (v.function) return;
        if (secondDeclarator(tail) && ! allConstant(tail))
        {
            report(line, namesOf(tail), SEVERAL);
            return;
        }
        if (! v.mutable) return;
        const what = scope === 'class' ? 'a static data member'
                   : specifiers.has('thread_local') ? 'a thread_local variable'
                   : specifiers.has('extern') ? 'an extern variable (a mutable global declared here)'
                   : 'a namespace-scope variable';
        report(line, v.name, what);
    }

    function skipStatement (i, end)
    {
        for (let j = i; j < end; j++)
        {
            if (toks[j].t === ';') return j + 1;
            if (OPEN[toks[j].t]) j = matchClose(toks, j);
        }
        throw new ParseError(toks[i].line, `a statement that never ends`);
    }

    function declSeq (i, end, scope)
    {
        while (i < end)
        {
            const t = toks[i].t;
            if (t === ';') { i++; continue; }
            if (t === '}' || t === ')' || t === ']') throw new ParseError(toks[i].line, `unbalanced '${t}'`);
            if (scope === 'class' && (t === 'public' || t === 'private' || t === 'protected') && toks[i + 1] && toks[i + 1].t === ':')
            { i += 2; continue; }
            if (t === 'namespace' || (t === 'inline' && toks[i + 1] && toks[i + 1].t === 'namespace'))
            {
                let j = i + 1;
                while (j < end && toks[j].t !== '{' && toks[j].t !== '=' && toks[j].t !== ';') j++;
                if (j < end && toks[j].t === '{') { const c = matchClose(toks, j); declSeq(j + 1, c, 'namespace'); i = c + 1; continue; }
                i = skipStatement(j, end); continue;
            }
            if (t === 'extern' && toks[i + 1] && toks[i + 1].t === '{')          // extern "C" { … } — the string is blanked
            { const c = matchClose(toks, i + 1); declSeq(i + 2, c, 'namespace'); i = c + 1; continue; }
            if (t === 'using' || t === 'typedef' || t === 'static_assert' || t === 'concept' || t === 'asm')
            { i = skipStatement(i, end); continue; }
            if (t === 'template')
            {
                let j = i + 1;
                if (toks[j] && toks[j].t === '<') j = skipAngles(toks, j);
                i = j; continue;                                  // the templated declaration follows
            }
            i = declaration(i, end, scope);
        }
    }

    declSeq(0, toks.length, 'namespace');
    return found;
}

//==============================================================================
// THE OS RULE — the header list and the call list, reasons attached (the header of this file argues each).
const HEADER_REASONS = [
    ['files and the console', ['fstream', 'iostream', 'istream', 'ostream', 'sstream', 'spanstream', 'strstream', 'syncstream',
        'streambuf', 'ios', 'iosfwd', 'iomanip', 'print', 'cstdio', 'stdio.h', 'filesystem']],
    ['the locale', ['locale', 'clocale', 'locale.h', 'codecvt', 'cctype', 'ctype.h', 'cwctype', 'wctype.h', 'cwchar',
        'wchar.h', 'regex', 'format', 'text_encoding']],
    ['threads', ['thread', 'mutex', 'shared_mutex', 'condition_variable', 'future', 'latch', 'barrier', 'semaphore',
        'stop_token', 'atomic', 'stdatomic.h', 'execution', 'pthread.h', 'threads.h', 'rcu', 'hazard_pointer']],
    ['the clock', ['chrono', 'ctime', 'time.h']],
    ['randomness (random_device reads the OS; the standard distributions differ between libraries)', ['random']],
    ['the operating system and the process', ['cstdlib', 'stdlib.h', 'csignal', 'signal.h', 'csetjmp', 'setjmp.h',
        'stacktrace', 'debugging', 'unistd.h', 'fcntl.h', 'dlfcn.h', 'windows.h', 'io.h', 'direct.h']],
    ['process-wide state (errno, the floating-point environment, the default memory resource)',
        ['cerrno', 'errno.h', 'cfenv', 'fenv.h', 'memory_resource']],
    ['exceptions and RTTI, which the library is compiled without', ['exception', 'stdexcept', 'system_error', 'typeinfo', 'typeindex']],
];
const HEADER_PREFIXES = [['sys/', 'the operating system'], ['mach/', 'the operating system'], ['linux/', 'the operating system'],
    ['emscripten', 'the wasm runtime, which belongs to the shell and the facade']];
const FORBIDDEN_HEADER = new Map();
for (const [why, list] of HEADER_REASONS) for (const h of list) FORBIDDEN_HEADER.set(h, why);

const CALL_REASONS = [
    ['formats or parses through the locale, or reports a bad parse by throwing — numbers go through the session\'s own declared rules',
        ['to_string', 'to_wstring', 'stoi', 'stol', 'stoll', 'stoul', 'stoull', 'stof', 'stod', 'stold']],
    ['installs a process-wide handler', ['set_new_handler', 'set_terminate', 'atexit', 'at_quick_exit']],
    ['reaches the environment, the locale or a process-wide generator', ['getenv', 'setlocale', 'rand', 'srand']],
];
const FORBIDDEN_CALL = new Map();
for (const [why, list] of CALL_REASONS) for (const f of list) FORBIDDEN_CALL.set(f, why);
const CALL_RE = new RegExp(`(?<![A-Za-z0-9_])(?:std::|(?<![.>:]))(${[...FORBIDDEN_CALL.keys()].join('|')})\\s*\\)?\\s*\\(`, 'g');

function lineAt (text, idx) { let n = 1; for (let i = 0; i < idx; i++) if (text[i] === '\n') n++; return n; }

export function scanOs (text)
{
    const found = [];
    const noComments = strip(text, true);
    noComments.split('\n').forEach((l, n) =>
    {
        const m = /^\s*#\s*include(?:_next)?\b\s*(.*)$/.exec(l);
        if (! m) return;
        const inc = /^([<"])([^>"]+)[>"]/.exec(m[1]);
        if (! inc) { found.push({ line: n + 1, what: `an #include this lint cannot read (${m[1].trim() || 'empty'}) — a computed include is one nobody audits` }); return; }
        const h = inc[2].trim();
        const why = FORBIDDEN_HEADER.get(h) ?? (HEADER_PREFIXES.find(([p]) => h.startsWith(p)) || [])[1];
        if (why) found.push({ line: n + 1, what: `#include ${inc[1]}${h}${inc[1] === '<' ? '>' : '"'} — ${why}` });
    });
    const code = strip(text, false);
    for (const m of code.matchAll(CALL_RE))
        found.push({ line: lineAt(code, m.index), what: `a call to ${m[1]}() — ${FORBIDDEN_CALL.get(m[1])}` });
    return found.sort((a, b) => a.line - b.line);
}

//==============================================================================
// THE LISTS — tools/lint/session-laws.txt.
const LISTS_FILE = 'tools/lint/session-laws.txt';
const ZONE_FILE = 'tools/lint/det-math-zone.txt';
const RULES = new Set(['globals', 'os', 'zone']);

export function parseLists (text)
{
    const lists = { scans: [], allows: [], errors: [] };
    text.split('\n').forEach((raw, n) =>
    {
        const line = raw.trim();
        if (! line || line.startsWith('#')) return;
        let m;
        if ((m = /^scan\s+(\S+)\s+(\S+)\s+(\S.*)$/.exec(line)))
        {
            const rules = m[2].split(',');
            const bad = rules.filter(r => ! RULES.has(r));
            if (bad.length) lists.errors.push({ line: n + 1, msg: `unknown rule(s) ${bad.join(', ')} — the rules are ${[...RULES].join(', ')}` });
            else lists.scans.push({ path: m[1], rules: new Set(rules), line: n + 1 });
        }
        else if ((m = /^allow-global\s+(\S+)\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S.*)$/.exec(line)))
            lists.allows.push({ file: m[1], name: m[2], line: n + 1, used: 0 });
        else
            lists.errors.push({ line: n + 1, msg: `unparseable line: ${line} — expected "scan <path> <rules> <why>" or "allow-global <file> <name> <why>", each with its reason` });
    });
    return lists;
}

const SOURCE = /\.(h|hh|hpp|hxx|inl|ipp|inc|c|cc|cpp|cxx|cppm|ixx|mpp)$/;
function walk (dir, acc)
{
    for (const e of readdirSync(dir).sort())
    {
        const p = dir + '/' + e;
        if (statSync(p).isDirectory()) { if (e !== 'tests') walk(p, acc); }
        else if (SOURCE.test(e)) acc.push(p);
    }
    return acc;
}

//==============================================================================
function selfTest ()
{
    // [source, the names the GLOBALS rule must report, in order]
    const globals = [
        // --- refused: state outside an object
        ['int counter = 0;',                                              ['counter']],
        ['int counter;',                                                  ['counter']],
        ['static int counter = 0;',                                       ['counter']],
        ['namespace { int hidden = 1; }',                                 ['hidden']],
        ['namespace a::b { double gain {1.0}; }',                         ['gain']],
        ['std::vector<int> table;',                                       ['table']],
        ['std::array<int, 3> table { 1, 2, 3 };',                         ['table']],
        ['int table[4];',                                                 ['table']],
        ['const char* names[] = { "a", "b" };',                           ['names']],   // mutable pointers to const chars
        ['static const char* name = "x";',                                ['name']],
        ['int g (0);',                                                    ['g']],       // direct-initialised, not a function
        ['Session s (cfg);',                                              ['s']],
        ['extern int shared;',                                            ['shared']],
        ['inline int shared = 0;',                                        ['shared']],
        ['constinit int early = 3;',                                      ['early']],   // constant-initialised, still mutable
        ['thread_local int perThread = 0;',                               ['perThread']],
        ['volatile unsigned char flag = 0;',                              ['flag']],
        ['int* const* p = nullptr;',                                      ['p']],       // the outer pointer is not const
        ['int& alias = counter;',                                         ['alias']],
        ['struct { int a; } anon;',                                       ['anon']],
        ['enum E { A, B } current;',                                      ['current']],
        ['struct X x;',                                                   ['x']],
        ['int Session::instances = 0;',                                   ['instances']],
        ['template <class T> T zero = T();',                              ['zero']],
        ['void (*callback) (int) = nullptr;',                             ['callback']],
        ['struct S { static int n; };',                                   ['n']],
        ['struct S { static inline int n = 0; };',                        ['n']],
        ['class C { public: inline static std::vector<int> cache; };',   ['cache']],
        ['struct S { struct In { static int deep; }; };',                 ['deep']],
        ['int f () { static int calls = 0; return ++calls; }',           ['calls']],
        ['void f () { if (x) { static double last; } }',                  ['last']],
        ['void f () { thread_local int t = 0; }',                         ['t']],
        ['struct S { int m () { static int k = 1; return k; } };',        ['k']],
        ['S::S () : a {1}, b (2) { static int made = 0; }',              ['made']],
        ['auto f () -> int { static int n = 0; return n; }',              ['n']],
        ['const auto l = [] { static int n = 0; return ++n; };',         ['n']],        // a lambda's local, in an initialiser
        ['int x = f ([] { static int n; return n; } ());',               ['n', 'x']],
        ['extern "C" { int cstate; }',                                    ['cstate']],
        ["int a = (1'000 > 2) ? 3 : 4;",                                  ['a']],       // a digit separator is not a quote
        // --- allowed: constants, functions, types, instance state
        ['constexpr int kLimit = 8;',                                     []],
        ['const int kLimit = 8;',                                         []],
        ['int const kLimit = 8;',                                         []],
        ['static constexpr double kPi = 3.14;',                           []],
        ['const char* const names[] = { "a" };',                          []],
        ['constexpr const char* name = "x";',                             []],
        ['const std::array<int, 3> k { 1, 2, 3 };',                       []],
        ['const std::vector<int*> ptrs {};',                              []],        // the * is inside the template
        ['const int& ref = kLimit;',                                      []],
        ['int f (int x);',                                                []],
        ['int f (void);',                                                 []],
        ['int f ();',                                                     []],
        ['static int helper (const Session& s, std::size_t n);',         []],
        ['std::unique_ptr<Session> make (Config cfg) noexcept;',          []],
        ['int f (Session*);',                                             []],        // an unnamed pointer parameter
        ['void g (int, ...);',                                            []],
        ['int f (int x) { return x; }',                                   []],
        ['std::uint64_t Session::createBytes () noexcept { return 1; }', []],
        ['Session::~Session () = default;',                               []],
        ['bool operator== (const A& a, const A& b);',                     []],
        ['struct S { int member = 0; std::vector<int> v; };',             []],
        ['struct S { static int f (); static constexpr int k = 1; static const int c = 2; };', []],
        ['class C final : public B<int> { public: C () noexcept = default; ~C (); };', []],
        ['struct S;',                                                     []],
        ['enum class E : std::uint8_t;',                                  []],
        ['enum class E : std::uint8_t { A = 0, B = 1 };',                 []],
        ['enum : unsigned { kA = 1 };',                                   []],
        ['using T = int; typedef long L; static_assert (sizeof (int) == 4, "x");', []],
        ['namespace n = std;',                                            []],
        ['template <class T> struct Box { T value; };',                   []],
        ['template <class T> constexpr T pi = T(3.14);',                  []],
        ['int f () { const int local = 1; static constexpr int k = 2; static const double c = 3.0; return local; }', []],
        ['int f () { return static_cast<int> (x); }',                      []],
        ['struct S { friend bool operator== (S, S) { return true; } };',   []],
        ['// static int commented = 0;\n/* int alsoCommented; */',         []],
        ['const char* s = "static int inAString = 0;";',                   ['s']],     // only the pointer, not the words
        ['#define STATE static int hidden = 0;\n#if X\nint f ();\n#endif', []],        // preprocessor: not parsed
        ['void f () noexcept (true) { int x = 0; (void) x; }',             []],
        ['[[nodiscard]] int f ();\nalignas (16) const float k[4] {};',     []],
        // --- one name per declaration with static storage, unless every name is a constant
        ['volatile int allowed = 0, hidden = 0;',                          ['allowed, hidden']],
        ['int f () { static int a = 0, b = 1; return a + b; }',            ['a, b']],
        ['std::map<int, int> table;',                                      ['table']],   // a comma inside <> is not a second name
        ['constexpr int kA = 1, kB = 2;',                                  []],
        ['const double kLo = 1.0, kHi = 2.0;',                             []],
        ['const int* a = nullptr, *b = nullptr;',                          ['a, b']],    // const binds the pointee, not the pointers
        // --- macro invocations are not declarations
        ['LAYOUT_CHECK (fc_config, pad);\nint after = 0;',                 ['after']],
        ['PRAGMA_BEGIN\nnamespace n { int inside = 0; }\nPRAGMA_END',       ['inside']],
        ['template <class C> struct Has<C, std::void_t<decltype (f (C ()))>> : std::true_type {};', []],
        ['bool operator< (const A& a, const A& b) { return a.v < b.v; }\nint after;', ['after']],
        ['struct S { bool operator() (int x) const { static int n; return x > n; } };', ['n']],
        ['struct S { bool operator! () const { return false; } };',         []],
    ];
    const os = [
        ['#include <chrono>',                      1],
        ['#  include  <thread>',                   1],
        ['#include<cstdio>',                       1],
        ['#include <sys/time.h>',                  1],
        ['#include <emscripten/heap.h>',           1],
        ['#include <atomic>\n#include <random>',   2],
        ['#include MACRO_HEADER',                  1],   // computed: refused, not skipped
        ['#include <cstdint>\n#include <memory>\n#include <charconv>\n#include "local.h"', 0],
        ['// #include <chrono>\n/* #include <thread> */', 0],
        ['auto s = std::to_string (x);',           1],
        ['auto d = stod (text);',                  1],
        ['auto s = obj.to_string ();',             0],   // a member of the same name is not the call
        ['auto s = mine::to_string (x);',          0],   // another namespace's
        ['auto s = my_to_string (x);',             0],
        ['const char* s = "std::to_string (x)";',  0],
        ['std::set_new_handler (h);',              1],
    ];
    const lists = [
        ['scan modules/session globals,os,zone the brain',   { scans: 1, errors: 0 }],
        ['allow-global tools/x.cpp g_slots the table',       { allows: 1, errors: 0 }],
        ['scan modules/session globals,clocks the brain',    { scans: 0, errors: 1 }],
        ['scan modules/session globals',                     { scans: 0, errors: 1 }],   // no reason
        ['allow-global tools/x.cpp g_slots',                 { allows: 0, errors: 1 }],
        ['alow-global tools/x.cpp g the table',              { errors: 1 }],
        ['# comment\n\n   ',                                 { errors: 0 }],
    ];
    let bad = 0;
    for (const [src, want] of globals)
    {
        let got;
        try { got = scanGlobals(src).map(f => f.name); } catch (e) { got = [`PARSE ERROR: ${e.message}`]; }
        if (got.join(',') !== want.join(','))
        { console.error(`  SELF-TEST FAIL (globals): wanted [${want}], got [${got}] for: ${JSON.stringify(src)}`); bad++; }
    }
    for (const [src, want] of os)
    {
        const got = scanOs(src).length;
        if (got !== want) { console.error(`  SELF-TEST FAIL (os): wanted ${want}, got ${got} for: ${JSON.stringify(src)}`); bad++; }
    }
    for (const [src, want] of lists)
    {
        const got = parseLists(src);
        for (const [k, n] of Object.entries(want))
            if (got[k].length !== n) { console.error(`  SELF-TEST FAIL (lists): wanted ${n} ${k}, got ${got[k].length} for: ${JSON.stringify(src)}`); bad++; }
    }
    // A file whose braces do not balance is a violation, never a pass.
    let threw = false;
    try { scanGlobals('void f () { int x;'); } catch (e) { threw = e instanceof ParseError; }
    if (! threw) { console.error('  SELF-TEST FAIL (parse): an unclosed brace did not raise a parse error'); bad++; }
    const total = globals.length + os.length + lists.length + 1;
    if (bad) { console.error(`session-laws lint self-test: ${bad} of ${total} cases wrong`); process.exit(1); }
    console.log(`session-laws lint self-test: ${total}/${total} cases correct`);
}

//==============================================================================
if (RUN_AS_PROGRAM)
{
    const args = process.argv.slice(2);
    if (args.includes('--self-test')) { selfTest(); process.exit(0); }
    if (args.length) { console.error('usage: node tools/lint/check-session-laws.mjs [--self-test]'); process.exit(2); }
    if (! existsSync(LISTS_FILE))
    { console.error(`check-session-laws: no ${LISTS_FILE} here — run from the root of felitronics-mastering-core`); process.exit(2); }

    const violations = [];
    const V = (f, line, rule, msg) => violations.push({ f, line, rule, msg });
    const lists = parseLists(readFileSync(LISTS_FILE, 'utf8'));
    for (const e of lists.errors) V(LISTS_FILE, e.line, 'LISTS', e.msg);

    // path -> set of rules
    const scope = new Map();
    for (const s of lists.scans)
    {
        if (! existsSync(s.path)) { V(LISTS_FILE, s.line, 'LISTS-ROT', `scan ${s.path}: no such file or directory — a scan of nothing reads as coverage`); continue; }
        const files = statSync(s.path).isDirectory() ? walk(s.path, []) : [s.path];
        if (files.length === 0) { V(LISTS_FILE, s.line, 'LISTS-ROT', `scan ${s.path}: no source file in it`); continue; }
        for (const f of files)
        {
            if (! scope.has(f)) scope.set(f, new Set());
            for (const r of s.rules) scope.get(f).add(r);
        }
    }
    for (const a of lists.allows)
        if (! (scope.get(a.file) || new Set()).has('globals'))
            V(LISTS_FILE, a.line, 'LISTS', `allow-global ${a.file} ${a.name}: that file is not scanned by the globals rule, so the allowance allows nothing`);

    const zoned = new Set();
    if (existsSync(ZONE_FILE))
        for (const l of readFileSync(ZONE_FILE, 'utf8').split('\n'))
        { const m = /^\s*zone\s+(\S+)\s+\S/.exec(l); if (m) zoned.add(m[1]); }

    const counts = { globals: 0, os: 0, zone: 0 };
    for (const [f, rules] of [...scope].sort((a, b) => a[0].localeCompare(b[0])))
    {
        const text = readFileSync(f, 'utf8');
        if (rules.has('globals'))
        {
            counts.globals++;
            let found = [];
            try { found = scanGlobals(text); }
            catch (e)
            {
                if (! (e instanceof ParseError)) throw e;
                V(f, e.line, 'PARSE', `${e.message}. A file this lint cannot follow is a file it cannot vouch for, so it is red.`);
            }
            for (const g of found)
            {
                const allow = lists.allows.find(a => a.file === f && a.name === g.name);
                if (allow) { allow.used++; continue; }
                V(f, g.line, 'GLOBALS', `${g.what}, \`${g.name}\`, that the code can change. felitronics::session keeps no state outside its objects (docs/SESSION.md): a session is replayed and sessions live side by side, and a global is where their histories meet. Make it a member of the object that owns it, or constexpr/const if it is a constant.`);
            }
        }
        if (rules.has('os'))
        {
            counts.os++;
            for (const o of scanOs(text))
                V(f, o.line, 'OS', `${o.what}. felitronics::session is called synchronously and reaches no operating system, file, console, locale, thread or clock (docs/SESSION.md); tools/lint/check-session-laws.mjs says why each of these is on the list.`);
        }
        if (rules.has('zone'))
        {
            counts.zone++;
            if (! zoned.has(f))
                V(f, 0, 'ZONE', `not in the deterministic zone: ${ZONE_FILE} has no \`zone ${f} <why>\` line, so the det-math lint would let a system libm call in this file through. Add the line.`);
        }
    }
    for (const a of lists.allows)
        if ((scope.get(a.file) || new Set()).has('globals') && a.used !== 1)
            V(LISTS_FILE, a.line, 'LISTS-ROT', a.used === 0
                ? `allow-global ${a.file} ${a.name}: no mutable global of that name is declared there. An allowance for something that is gone is how a list stops meaning anything — remove it, or fix the name.`
                : `allow-global ${a.file} ${a.name}: matches ${a.used} declarations. One allowance, one global.`);

    if (violations.length)
    {
        for (const v of violations) console.error(`${v.f}${v.line ? ':' + v.line : ''}: [${v.rule}] ${v.msg}`);
        console.error(`\n^^ ${violations.length} violation(s) of the session laws.`);
        process.exit(1);
    }
    const used = lists.allows.map(a => `${a.file.split('/').pop()}:${a.name}`).join(', ');
    console.log(`session laws: clean — globals in ${counts.globals} files (${lists.allows.length} allowed: ${used || 'none'}), `
              + `os in ${counts.os}, zone in ${counts.zone} (every one listed in ${ZONE_FILE}).`);
}
