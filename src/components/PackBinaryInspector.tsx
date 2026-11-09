import React, { useState } from 'react';
import JSZip from 'jszip';
import { MarkerCategory, PathingMarker, PathingTrail } from '../types/pathing';
import { formatHexDump, parseTrlBinary } from '../utils/trlParser';
import { parseOverlayXml } from '../utils/xmlParser';
import { getSampleTrlBuffer } from '../data/samplePackData';
import { Binary, FileCode2, Play, Upload } from 'lucide-react';

interface PackBinaryInspectorProps {
  xmlText: string;
  onUpdateXml: (newXml: string) => void;
  luaText: string;
  onUpdateLua: (newLua: string) => void;
  onLoadCustomPack: (
    categories: MarkerCategory[],
    markers: PathingMarker[],
    trails: PathingTrail[],
    xmlRaw: string
  ) => void;
}

export const PackBinaryInspector: React.FC<PackBinaryInspectorProps> = ({
  xmlText,
  onUpdateXml,
  luaText,
  onUpdateLua,
  onLoadCustomPack,
}) => {
  const [selectedTrlPath, setSelectedTrlPath] = useState<string>(
    'Data/Trails/amnytas_skyscale_route.trl'
  );
  const [customTrlBuffer, setCustomTrlBuffer] = useState<ArrayBuffer | null>(null);
  const [uploadStatus, setUploadStatus] = useState<string | null>(null);
  const [activeSubTab, setActiveSubTab] = useState<'xml' | 'trl' | 'lua'>('xml');

  const activeBuffer = customTrlBuffer || getSampleTrlBuffer(selectedTrlPath);
  const parsedTrl = parseTrlBinary(activeBuffer);
  const hexRows = formatHexDump(parsedTrl.rawBytes, 140);

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    try {
      const ext = file.name.toLowerCase();
      if (ext.endsWith('.trl')) {
        const buf = await file.arrayBuffer();
        const trl = parseTrlBinary(buf);
        setCustomTrlBuffer(buf);
        setSelectedTrlPath(file.name);
        setActiveSubTab('trl');
        setUploadStatus(
          `Loaded binary .trl "${file.name}": MapID=${trl.mapId}, ${trl.totalPoints} vertices across ${trl.segments.length} segments.`
        );
      } else if (ext.endsWith('.xml')) {
        const text = await file.text();
        onUpdateXml(text);
        setActiveSubTab('xml');
        setUploadStatus(`Loaded XML Marker Pack "${file.name}" into live 3D simulation.`);
      } else if (ext.endsWith('.taco') || ext.endsWith('.zip')) {
        const buf = await file.arrayBuffer();
        const zip = await JSZip.loadAsync(buf);
        let foundXml = '';
        const trlMap: Record<string, ArrayBuffer> = {};

        for (const [relativePath, entry] of Object.entries(zip.files)) {
          if (entry.dir) continue;
          const lower = relativePath.toLowerCase();
          if (lower.endsWith('.xml') && !foundXml) {
            foundXml = await entry.async('string');
          } else if (lower.endsWith('.trl')) {
            trlMap[relativePath.toLowerCase()] = await entry.async('arraybuffer');
          }
        }

        if (foundXml) {
          const parsed = parseOverlayXml(foundXml);
          // Attach any matching .trl binary segments
          for (const t of parsed.trails) {
            const key = t.trailDataPath.toLowerCase();
            const matchBuf =
              trlMap[key] ||
              Object.entries(trlMap).find(([k]) => k.endsWith(key.split('/').pop() || ''))?.[1];
            if (matchBuf) {
              t.segments = parseTrlBinary(matchBuf).segments;
              setCustomTrlBuffer(matchBuf);
              setSelectedTrlPath(t.trailDataPath);
            }
          }
          onLoadCustomPack(parsed.categories, parsed.markers, parsed.trails, foundXml);
          setUploadStatus(
            `Extracted archive "${file.name}": ${parsed.markers.length} POIs, ${parsed.trails.length} trails.`
          );
        } else {
          setUploadStatus(`Archive "${file.name}" did not contain an XML marker definition.`);
        }
      }
    } catch (err) {
      setUploadStatus(`Error parsing file: ${(err as Error).message}`);
    }
  };

  return (
    <div className="space-y-6">
      {/* Top Action Bar for Uploading Real .taco / .trl / .xml */}
      <div className="bg-[#111622] border border-slate-800 rounded-lg p-4 flex flex-wrap items-center justify-between gap-4">
        <div>
          <h3 className="text-sm font-semibold text-slate-100">
            Pack Archive, XML Category & Binary .trl Workbench
          </h3>
          <p className="text-xs text-slate-400 mt-0.5">
            Edit the live Blish HUD XML (<code className="text-slate-200">bh-*</code> overrides),
            inspect IEEE-754 float32 <code className="text-slate-200">.trl</code> binary streams, or
            drop any <code className="text-slate-200">.taco</code> / <code className="text-slate-200">.trl</code> file.
          </p>
        </div>

        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1 p-1 bg-slate-900 border border-slate-800 rounded-md">
            <button
              onClick={() => setActiveSubTab('xml')}
              className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap ${
                activeSubTab === 'xml'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              OverlayData.xml
            </button>
            <button
              onClick={() => setActiveSubTab('trl')}
              className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap ${
                activeSubTab === 'trl'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              Binary .trl Hex Inspector
            </button>
            <button
              onClick={() => setActiveSubTab('lua')}
              className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap ${
                activeSubTab === 'lua'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              pack.lua Sandbox
            </button>
          </div>

          <label className="px-3.5 py-2 bg-slate-800 hover:bg-slate-700 text-slate-100 text-xs font-medium rounded-md cursor-pointer transition-colors flex items-center gap-2 whitespace-nowrap border border-slate-700">
            <Upload className="w-3.5 h-3.5 text-sky-400" />
            <span>Load .taco / .trl / .xml</span>
            <input
              type="file"
              accept=".taco,.zip,.trl,.xml"
              onChange={handleFileUpload}
              className="hidden"
            />
          </label>
        </div>
      </div>

      {uploadStatus && (
        <div className="px-4 py-2.5 bg-sky-950/50 border border-sky-500/40 rounded-md text-xs text-sky-200 font-mono">
          {uploadStatus}
        </div>
      )}

      {activeSubTab === 'xml' && (
        <div className="bg-[#111622] border border-slate-800 rounded-lg overflow-hidden">
          <div className="px-4 py-2.5 bg-slate-900 border-b border-slate-800 flex items-center justify-between">
            <div className="flex items-center gap-2 text-xs font-mono text-slate-300">
              <FileCode2 className="w-4 h-4 text-sky-400" />
              <span>OverlayData.xml (Live synchronized with 3D Viewport)</span>
            </div>
            <span className="text-xs text-slate-400">
              Supports <code className="text-amber-300">bh-*</code> attribute prefix stripping &
              recursive category inheritance
            </span>
          </div>
          <textarea
            value={xmlText}
            onChange={(e) => onUpdateXml(e.target.value)}
            spellCheck={false}
            className="w-full h-[460px] p-4 bg-[#090D16] text-slate-200 font-mono text-xs leading-relaxed focus:outline-none resize-y"
          />
        </div>
      )}

      {activeSubTab === 'trl' && (
        <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
          <div className="lg:col-span-4 bg-[#111622] border border-slate-800 rounded-lg p-4 space-y-4">
            <div className="flex items-center gap-2 pb-2 border-b border-slate-800">
              <Binary className="w-4 h-4 text-sky-400" />
              <h4 className="text-xs font-semibold text-slate-100">
                Select Sample or Custom .trl Binary
              </h4>
            </div>

            <div className="space-y-1.5">
              {[
                'Data/Trails/amnytas_skyscale_route.trl',
                'Data/Trails/kryptis_rift_patrol.trl',
                'Data/Trails/octovine_burn_phase.trl',
              ].map((path) => (
                <button
                  key={path}
                  onClick={() => {
                    setCustomTrlBuffer(null);
                    setSelectedTrlPath(path);
                  }}
                  className={`w-full text-left px-3 py-2 rounded text-xs font-mono transition-colors ${
                    selectedTrlPath === path && !customTrlBuffer
                      ? 'bg-sky-500/20 text-sky-300 border border-sky-500/30'
                      : 'text-slate-300 hover:bg-slate-800'
                  }`}
                >
                  {path}
                </button>
              ))}
            </div>

            <div className="pt-3 border-t border-slate-800 space-y-2 text-xs font-mono tabular-nums">
              <div className="flex justify-between text-slate-300">
                <span className="text-slate-400">Header Version (int32):</span>
                <span>{parsedTrl.version}</span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span className="text-slate-400">Header MapID (int32):</span>
                <span className="text-sky-400">{parsedTrl.mapId}</span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span className="text-slate-400">Disjoint Segments:</span>
                <span>{parsedTrl.segments.length}</span>
              </div>
              <div className="flex justify-between text-slate-300">
                <span className="text-slate-400">Total 3D Nodes:</span>
                <span>{parsedTrl.totalPoints}</span>
              </div>
            </div>
          </div>

          <div className="lg:col-span-8 bg-[#111622] border border-slate-800 rounded-lg overflow-hidden">
            <div className="px-4 py-2.5 bg-slate-900 border-b border-slate-800 flex items-center justify-between text-xs">
              <span className="font-mono text-slate-200">{selectedTrlPath} // Binary Frame Layout</span>
              <span className="text-slate-400 font-mono">Little-Endian IEEE-754 Float32</span>
            </div>
            <div className="overflow-x-auto">
              <table className="w-full text-left border-collapse text-xs font-mono tabular-nums">
                <thead>
                  <tr className="border-b border-slate-800 text-slate-400 bg-slate-900/50">
                    <th className="py-2.5 px-4">Offset</th>
                    <th className="py-2.5 px-4">Raw Little-Endian Bytes</th>
                    <th className="py-2.5 px-4">Decoded C++ Struct Interpretation</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-800/70">
                  {hexRows.map((r) => (
                    <tr key={r.offsetHex} className="hover:bg-slate-900/50">
                      <td className="py-2 px-4 text-slate-500">{r.offsetHex}</td>
                      <td className="py-2 px-4 text-sky-300">{r.hexBytes.join(' ')}</td>
                      <td className="py-2 px-4 text-slate-200">{r.interpretation}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {activeSubTab === 'lua' && (
        <div className="bg-[#111622] border border-slate-800 rounded-lg overflow-hidden">
          <div className="px-4 py-2.5 bg-slate-900 border-b border-slate-800 flex items-center justify-between">
            <div className="flex items-center gap-2 text-xs font-mono text-slate-300">
              <Play className="w-3.5 h-3.5 text-emerald-400" />
              <span>pack.lua // Embedded LuaJIT 2.1 + sol2 Sandbox</span>
            </div>
            <span className="text-xs text-slate-400">
              Globals: <code className="text-slate-200">Pack</code>,{' '}
              <code className="text-slate-200">World</code>,{' '}
              <code className="text-slate-200">Mumble</code>,{' '}
              <code className="text-slate-200">Debug</code>,{' '}
              <code className="text-slate-200">Vector3</code>
            </span>
          </div>
          <textarea
            value={luaText}
            onChange={(e) => onUpdateLua(e.target.value)}
            spellCheck={false}
            className="w-full h-[420px] p-4 bg-[#090D16] text-slate-200 font-mono text-xs leading-relaxed focus:outline-none resize-y"
          />
        </div>
      )}
    </div>
  );
};
