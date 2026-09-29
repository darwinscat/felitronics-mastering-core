// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Check encoded data against the generated declaration grammar; no second copy of the types.
export function types(source) {
    source = source.replace(/\/\*[\s\S]*?\*\//g, '');
    const aliases = new Map([...source.matchAll(/export type (\w+) = ([^;]+);/g)].map(m => [m[1], m[2].trim()]));
    const records = new Map([...source.matchAll(/export interface (\w+) \{([^}]+)\}/g)].map(m =>
        [m[1], [...m[2].matchAll(/readonly (\w+)(\?)?: ([^;]+);/g)].map(f => [f[1], !!f[2], f[3].trim()])]));
    function accepts(value, type) {
        type = type.trim();
        if (aliases.has(type)) return accepts(value, aliases.get(type));
        const derived = /^Omit<(\w+), "(\w+)"> & \{ readonly (\w+): (.+) \}$/.exec(type);
        if (derived && records.has(derived[1])) {
            if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
            const fields = [...records.get(derived[1]).filter(([name]) => name !== derived[2]),
                            [derived[3], false, derived[4]]];
            return Object.keys(value).every(key => fields.some(([name]) => name === key))
                && fields.every(([name, optional, t]) => Object.hasOwn(value, name) ? accepts(value[name], t) : optional);
        }
        if (type.includes(' | ')) return type.split(' | ').some(t => accepts(value, t));
        if (type.startsWith('ReadonlyArray<')) return Array.isArray(value) && value.every(v => accepts(v, type.slice(14, -1)));
        if (records.has(type)) {
            if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
            const fields = records.get(type);
            return Object.keys(value).every(key => fields.some(([name]) => name === key))
                && fields.every(([name, optional, t]) => Object.hasOwn(value, name) ? accepts(value[name], t) : optional);
        }
        if (type === 'null') return value === null;
        if (['number', 'string', 'boolean'].includes(type)) return typeof value === type;
        if (/^\d+$/.test(type) || /^".*"$/.test(type)) return value === JSON.parse(type);
        throw Error(`Unsupported declaration ${type}`);
    }
    return accepts;
}
