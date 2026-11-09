import { PathingTrail, Vector3 } from '../types/pathing';

/**
 * Parses a genuine Guild Wars 2 TacO / Blish HUD binary .trl file.
 * Binary format specification:
 * - Bytes 0..3: int32 Version (Always 0)
 * - Bytes 4..7: int32 MapID (e.g., 50 for Lion's Arch, 1510 for Amnytas)
 * - Bytes 8..N: Sequence of 12-byte IEEE 754 float32 triplets (X, Y, Z)
 *   Note: A vector of (0.0, 0.0, 0.0) denotes a segment break (disconnecting the triangle strip).
 */
export interface ParsedTrlData {
  version: number;
  mapId: number;
  totalPoints: number;
  segments: Vector3[][];
  rawBytes: Uint8Array;
}

export function parseTrlBinary(buffer: ArrayBuffer): ParsedTrlData {
  const view = new DataView(buffer);
  if (buffer.byteLength < 8) {
    throw new Error(`Invalid .trl file: expected at least 8 header bytes, got ${buffer.byteLength}`);
  }

  const version = view.getInt32(0, true); // little-endian
  const mapId = view.getInt32(4, true);   // little-endian

  const segments: Vector3[][] = [];
  let currentSegment: Vector3[] = [];
  let totalPoints = 0;

  let offset = 8;
  while (offset + 12 <= buffer.byteLength) {
    const x = view.getFloat32(offset, true);
    const y = view.getFloat32(offset + 4, true);
    const z = view.getFloat32(offset + 8, true);
    offset += 12;

    // Check for TacO (0, 0, 0) break sentinel
    if (Math.abs(x) < 1e-6 && Math.abs(y) < 1e-6 && Math.abs(z) < 1e-6) {
      if (currentSegment.length > 0) {
        segments.push(currentSegment);
        currentSegment = [];
      }
    } else {
      currentSegment.push({ x, y, z });
      totalPoints++;
    }
  }

  if (currentSegment.length > 0) {
    segments.push(currentSegment);
  }

  return {
    version,
    mapId,
    totalPoints,
    segments,
    rawBytes: new Uint8Array(buffer.slice(0, Math.min(buffer.byteLength, 256))),
  };
}

/**
 * Encodes a list of trail segments into a genuine binary TacO/Blish .trl ArrayBuffer
 * so users can inspect the exact byte layout or export a valid .trl file.
 */
export function encodeTrlBinary(mapId: number, segments: Vector3[][]): ArrayBuffer {
  let pointCount = 0;
  for (let i = 0; i < segments.length; i++) {
    pointCount += segments[i].length;
    if (i < segments.length - 1) {
      pointCount += 1; // for (0,0,0) break vector
    }
  }

  const byteLength = 8 + pointCount * 12;
  const buffer = new ArrayBuffer(byteLength);
  const view = new DataView(buffer);

  // Header
  view.setInt32(0, 0, true); // Version = 0
  view.setInt32(4, mapId, true); // MapID

  let offset = 8;
  for (let s = 0; s < segments.length; s++) {
    const seg = segments[s];
    for (const pt of seg) {
      view.setFloat32(offset, pt.x, true);
      view.setFloat32(offset + 4, pt.y, true);
      view.setFloat32(offset + 8, pt.z, true);
      offset += 12;
    }
    // Insert (0,0,0) sentinel between segments
    if (s < segments.length - 1) {
      view.setFloat32(offset, 0.0, true);
      view.setFloat32(offset + 4, 0.0, true);
      view.setFloat32(offset + 8, 0.0, true);
      offset += 12;
    }
  }

  return buffer;
}

/**
 * Formats a Uint8Array into a clean hex + ASCII dump for the binary inspector.
 */
export function formatHexDump(bytes: Uint8Array, maxBytes = 128): Array<{
  offsetHex: string;
  hexBytes: string[];
  interpretation: string;
}> {
  const rows: Array<{ offsetHex: string; hexBytes: string[]; interpretation: string }> = [];
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const limit = Math.min(bytes.byteLength, maxBytes);

  // First 8 bytes are header
  if (limit >= 8) {
    const ver = view.getInt32(0, true);
    const mapId = view.getInt32(4, true);
    const headerHex: string[] = [];
    for (let i = 0; i < 8; i++) {
      headerHex.push(bytes[i].toString(16).padStart(2, '0').toUpperCase());
    }
    rows.push({
      offsetHex: '0x0000',
      hexBytes: headerHex,
      interpretation: `Header: Version=${ver} (int32), MapID=${mapId} (int32)`,
    });
  }

  // Subsequent 12-byte chunks are Vector3 (x, y, z)
  let offset = 8;
  let nodeIdx = 0;
  while (offset + 12 <= limit) {
    const x = view.getFloat32(offset, true);
    const y = view.getFloat32(offset + 4, true);
    const z = view.getFloat32(offset + 8, true);
    const chunkHex: string[] = [];
    for (let i = 0; i < 12; i++) {
      chunkHex.push(bytes[offset + i].toString(16).padStart(2, '0').toUpperCase());
    }

    const isBreak = Math.abs(x) < 1e-6 && Math.abs(y) < 1e-6 && Math.abs(z) < 1e-6;
    rows.push({
      offsetHex: `0x${offset.toString(16).padStart(4, '0').toUpperCase()}`,
      hexBytes: chunkHex,
      interpretation: isBreak
        ? `Segment Break Sentinel (0.00, 0.00, 0.00)`
        : `Node #${nodeIdx++}: X=${x.toFixed(2)}, Y=${y.toFixed(2)}, Z=${z.toFixed(2)}`,
    });
    offset += 12;
  }

  return rows;
}
