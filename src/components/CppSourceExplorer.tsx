import React, { useState } from 'react';
import { CPP_SOURCE_FILES } from '../data/cppSourceFiles';
import { CppSourceFile } from '../types/pathing';
import { Check, Copy, Cpu, Download, FileCode, FolderTree } from 'lucide-react';
import { downloadCompiledDll, downloadNexusAddonProjectZip } from '../utils/zipExporter';

export const CppSourceExplorer: React.FC = () => {
  const [selectedFile, setSelectedFile] = useState<CppSourceFile>(CPP_SOURCE_FILES[1]); // default to dllmain.cpp
  const [copied, setCopied] = useState<boolean>(false);
  const [isExporting, setIsExporting] = useState<boolean>(false);

  const handleCopyCode = () => {
    navigator.clipboard.writeText(selectedFile.code);
    setCopied(true);
    setTimeout(() => setCopied(false), 1800);
  };

  const handleDownloadZip = async () => {
    setIsExporting(true);
    try {
      await downloadNexusAddonProjectZip();
    } finally {
      setIsExporting(false);
    }
  };

  return (
    <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
      {/* Left Sidebar: C++20 Project File Tree */}
      <div className="lg:col-span-4 bg-[#111622] border border-slate-800 rounded-lg p-4 flex flex-col justify-between">
        <div>
          <div className="flex items-center justify-between pb-3 mb-3 border-b border-slate-800">
            <div className="flex items-center gap-2">
              <FolderTree className="w-4 h-4 text-sky-400" />
              <h3 className="text-sm font-semibold text-slate-100">
                NexusPathing C++20 Solution
              </h3>
            </div>
            <span className="text-xs font-mono text-slate-400 tabular-nums">
              {CPP_SOURCE_FILES.length} files
            </span>
          </div>

          <div className="space-y-1">
            {CPP_SOURCE_FILES.map((file) => {
              const active = file.path === selectedFile.path;
              return (
                <button
                  key={file.path}
                  onClick={() => setSelectedFile(file)}
                  className={`w-full text-left px-3 py-2.5 rounded-md transition-colors flex items-center justify-between gap-2 ${
                    active
                      ? 'bg-sky-500/15 text-sky-300 border border-sky-500/30'
                      : 'text-slate-300 hover:bg-slate-800/60 hover:text-white border border-transparent'
                  }`}
                >
                  <div className="flex items-center gap-2.5 min-w-0">
                    <FileCode className="w-4 h-4 shrink-0 text-slate-400" />
                    <div className="truncate">
                      <div className="text-xs font-mono font-medium truncate">
                        {file.path}
                      </div>
                      <div className="text-[11px] text-slate-400 truncate mt-0.5">
                        Ports: {file.blishEquivalent}
                      </div>
                    </div>
                  </div>
                  <span className="text-[11px] font-mono text-slate-400 tabular-nums shrink-0">
                    {file.linesCount}L
                  </span>
                </button>
              );
            })}
          </div>
        </div>

        <div className="mt-6 pt-4 border-t border-slate-800 space-y-3">
          <div className="p-3 bg-emerald-950/30 border border-emerald-500/30 rounded-md space-y-1.5">
            <div className="flex items-center justify-between text-xs font-semibold text-emerald-300">
              <span className="flex items-center gap-1.5">
                <Cpu className="w-3.5 h-3.5" />
                <span>v3.1.1 VRAM Halved + Leak Fixes + Safe Unload</span>
              </span>
              <span className="font-mono text-[11px] text-emerald-400 tabular-nums">4.5 MB PE32+</span>
            </div>
            <div className="text-[11px] text-slate-300 font-mono leading-relaxed">
              Engine: <span className="text-sky-300">D3D11 + WinHTTP + Lua 5.4 + Single VRAM Copy</span><br />
              Safety: <span className="text-emerald-400">Thread Drain + Texture Cache Teardown</span>
            </div>
            <button
              onClick={() => downloadCompiledDll()}
              className="w-full mt-1.5 py-2 px-3 bg-emerald-500 hover:bg-emerald-400 text-slate-950 font-semibold text-xs rounded transition-colors flex items-center justify-center gap-2 whitespace-nowrap"
            >
              <Download className="w-3.5 h-3.5" />
              <span>Download NexusPathing.dll (v3.1.1 x64)</span>
            </button>
          </div>

          <button
            onClick={handleDownloadZip}
            disabled={isExporting}
            className="w-full py-2.5 px-4 bg-sky-500 hover:bg-sky-400 disabled:opacity-50 text-slate-950 font-semibold text-xs rounded-md transition-colors flex items-center justify-center gap-2 whitespace-nowrap"
          >
            <Download className="w-4 h-4" />
            <span>
              {isExporting
                ? 'Bundling DLL + C++20 Source...'
                : 'Download Full Bundle (.dll + Source .zip)'}
            </span>
          </button>
        </div>
      </div>

      {/* Right Code Viewer */}
      <div className="lg:col-span-8 bg-[#111622] border border-slate-800 rounded-lg overflow-hidden flex flex-col">
        <div className="px-4 py-3 bg-slate-900/90 border-b border-slate-800 flex flex-wrap items-center justify-between gap-3">
          <div>
            <div className="flex items-center gap-2 text-xs font-mono text-slate-200">
              <span className="font-semibold text-sky-400">{selectedFile.path}</span>
              <span className="text-slate-600">·</span>
              <span className="text-slate-400">Blish Equivalent: {selectedFile.blishEquivalent}</span>
            </div>
            <p className="text-xs text-slate-400 mt-1">{selectedFile.description}</p>
          </div>

          <button
            onClick={handleCopyCode}
            className="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-slate-200 rounded text-xs font-medium transition-colors flex items-center gap-1.5 whitespace-nowrap shrink-0"
          >
            {copied ? (
              <>
                <Check className="w-3.5 h-3.5 text-emerald-400" />
                <span>Copied</span>
              </>
            ) : (
              <>
                <Copy className="w-3.5 h-3.5" />
                <span>Copy File</span>
              </>
            )}
          </button>
        </div>

        <div className="p-4 overflow-x-auto max-h-[600px] overflow-y-auto bg-[#090D16]">
          <pre className="text-xs font-mono leading-relaxed text-slate-200 tabular-nums">
            {selectedFile.code.split('\n').map((line, i) => (
              <div key={i} className="table-row hover:bg-slate-900/60">
                <span className="table-cell pr-4 text-right text-slate-600 select-none w-10">
                  {i + 1}
                </span>
                <span className="table-cell whitespace-pre">{line}</span>
              </div>
            ))}
          </pre>
        </div>
      </div>
    </div>
  );
};
