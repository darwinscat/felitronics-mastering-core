// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

import {createHash} from 'node:crypto';
import {brotliDecompressSync} from 'node:zlib';

// The archive contains exact codec JSON and binary row bytes for every saved
// response. Decompress once in a page test, then replay each scenario in order.
export function decodeWavRecording(recording) {
    if (recording.format !== 2 || recording.contractArchive?.codec !== 'br+base64')
        throw new Error('unsupported WAV recording');
    const bytes = brotliDecompressSync(Buffer.from(recording.contractArchive.base64, 'base64'));
    const hash = createHash('sha256').update(bytes).digest('hex');
    if (bytes.length !== recording.contractArchive.bytes || hash !== recording.contractArchive.sha256)
        throw new Error('WAV recording archive differs from its declared bytes');
    return JSON.parse(bytes.toString('utf8'));
}
