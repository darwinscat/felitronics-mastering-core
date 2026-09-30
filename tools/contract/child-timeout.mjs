// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// ONE TIMEOUT MECHANISM for the contract harnesses' child processes (run.mjs, wav-contract.mjs): the harness names its
// own default, and FC_SESSION_CONTRACT_TIMEOUT_MS, when set, overrides it for a slower tier. Whole milliseconds, 1000
// or more; anything else is refused by name rather than read as a budget no one chose.
export function childTimeout(fallback, value = process.env.FC_SESSION_CONTRACT_TIMEOUT_MS) {
    if (value === undefined) return fallback;
    if (!/^[1-9][0-9]{3,8}$/.test(value)) throw new Error(`FC_SESSION_CONTRACT_TIMEOUT_MS must be whole milliseconds, 1000 or more: ${value}`);
    return Number(value);
}
