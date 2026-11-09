/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useEffect, useState } from 'react';
import {
  GW2Mount,
  GW2Profession,
  MarkerCategory,
  MumbleLinkState,
  PathingMarker,
  PathingTrail,
} from './types/pathing';
import {
  SAMPLE_LUA_SCRIPT,
  SAMPLE_PACK_XML,
  SAMPLE_TRL_SEGMENTS,
} from './data/samplePackData';
import { parseOverlayXml } from './utils/xmlParser';
import { Viewport3DSimulator } from './components/Viewport3DSimulator';
import { CppSourceExplorer } from './components/CppSourceExplorer';
import { PackBinaryInspector } from './components/PackBinaryInspector';
import { FeatureParityMatrix } from './components/FeatureParityMatrix';
import { downloadCompiledDll, downloadNexusAddonProjectZip } from './utils/zipExporter';
import {
  Check,
  Code2,
  Compass,
  Cpu,
  Download,
  FileArchive,
  Layers,
  RefreshCw,
  Sliders,
} from 'lucide-react';

const MOUNTS: GW2Mount[] = [
  'Skyscale',
  'Griffon',
  'Raptor',
  'Springer',
  'Skimmer',
  'Jackal',
  'RollerBeetle',
  'Warclaw',
  'SiegeTurtle',
  'None',
];

const PROFESSIONS: GW2Profession[] = [
  'Elementalist',
  'Mesmer',
  'Necromancer',
  'Engineer',
  'Ranger',
  'Thief',
  'Guardian',
  'Warrior',
  'Revenant',
];

function toggleCategoryRecursive(
  list: MarkerCategory[],
  targetFullPath: string
): MarkerCategory[] {
  return list.map((cat) => {
    if (cat.fullPath.toLowerCase() === targetFullPath.toLowerCase()) {
      const nextState = !cat.enabled;
      const setAllChildren = (children: MarkerCategory[], enabled: boolean): MarkerCategory[] =>
        children.map((c) => ({
          ...c,
          enabled,
          children: setAllChildren(c.children, enabled),
        }));
      return {
        ...cat,
        enabled: nextState,
        children: setAllChildren(cat.children, nextState),
      };
    }
    if (cat.children.length > 0) {
      return {
        ...cat,
        children: toggleCategoryRecursive(cat.children, targetFullPath),
      };
    }
    return cat;
  });
}

export default function App() {
  const [activeSection, setActiveSection] = useState<
    'simulator' | 'cpp-source' | 'inspector' | 'parity'
  >('simulator');

  const [xmlText, setXmlText] = useState<string>(SAMPLE_PACK_XML);
  const [luaText, setLuaText] = useState<string>(SAMPLE_LUA_SCRIPT);
  const [categories, setCategories] = useState<MarkerCategory[]>([]);
  const [markers, setMarkers] = useState<PathingMarker[]>([]);
  const [trails, setTrails] = useState<PathingTrail[]>([]);
  const [clipboardToast, setClipboardToast] = useState<string | null>(null);
  const [luaLogs, setLuaLogs] = useState<string[]>([
    'NexusPathing::Lua VM ready (LuaJIT 2.1 + sol2 bindings)',
  ]);
  const [isDownloading, setIsDownloading] = useState<boolean>(false);

  // Live MumbleLink Shared Memory & GW2 API State
  const [mumble, setMumble] = useState<MumbleLinkState>({
    uiVersion: 2,
    uiTick: 10420,
    avatarPosition: { x: -30.0, y: 11.5, z: -26.0 },
    avatarFront: { x: 0.7, y: 0.0, z: 0.7 },
    cameraPosition: { x: -48.0, y: 24.0, z: -44.0 },
    cameraFront: { x: 0.65, y: -0.35, z: 0.65 },
    fov: 55,
    characterName: 'Taimi Sparkwright',
    profession: 'Engineer',
    race: 'Asura',
    mapId: 1510,
    mapName: 'Amnytas (Secrets of the Obscure)',
    worldId: 2002,
    mount: 'Skyscale',
    inCombat: false,
    isCommander: true,
    isMapOpen: false,
    compassWidth: 176,
    compassHeight: 176,
    compassRotation: 0,
    activeFestival: 'None',
    completedAchievements: {
      6912: [], // Bit 0 = Lantern #1, Bit 1 = Spire Mastery Insight
    },
  });

  // Parse initial XML and attach binary .trl segments
  useEffect(() => {
    try {
      const parsed = parseOverlayXml(xmlText);
      const trailsWithSegments = parsed.trails.map((t) => ({
        ...t,
        segments:
          SAMPLE_TRL_SEGMENTS[t.trailDataPath] ||
          SAMPLE_TRL_SEGMENTS['Data/Trails/amnytas_skyscale_route.trl'],
      }));
      setCategories(parsed.categories);
      setMarkers(parsed.markers);
      setTrails(trailsWithSegments);
    } catch (e) {
      console.error('Failed to parse XML:', e);
    }
  }, [xmlText]);

  const handleToggleCategory = (fullPath: string) => {
    setCategories((prev) => toggleCategoryRecursive(prev, fullPath));
  };

  const handleTriggerMarker = (markerId: string) => {
    const nowSec = performance.now() / 1000;
    setMarkers((prev) =>
      prev.map((m) => {
        if (m.id !== markerId) return m;

        // 1. Handle Copy attribute -> Browser clipboard + Toast
        if (m.copyValue) {
          navigator.clipboard?.writeText(m.copyValue).catch(() => {});
          const msg = m.copyMessage || `Copied ${m.copyValue} to clipboard!`;
          setClipboardToast(msg);
          setTimeout(() => setClipboardToast(null), 3200);
        }

        // 2. Handle ShowHide category toggle attribute
        if (m.showHideCategory) {
          handleToggleCategory(m.showHideCategory);
          setLuaLogs((logs) => [
            ...logs.slice(-8),
            `ShowHide toggled category '${m.showHideCategory}'`,
          ]);
        }

        // 3. Handle Lua script-trigger hook
        if (m.scriptTrigger) {
          setLuaLogs((logs) => [
            ...logs.slice(-8),
            `${m.scriptTrigger}(Marker[${m.guid.slice(0, 8)}]) executed by ${mumble.characterName}`,
          ]);
        }

        return {
          ...m,
          triggered: true,
          triggeredAt: nowSec,
        };
      })
    );
  };

  const handleResetAllBehaviors = () => {
    setMarkers((prev) =>
      prev.map((m) => ({
        ...m,
        triggered: false,
        triggeredAt: undefined,
      }))
    );
    setMumble((prev) => ({
      ...prev,
      completedAchievements: { 6912: [] },
    }));
    setLuaLogs((logs) => [...logs.slice(-8), 'Reset all triggered GUIDs & Achievement filters']);
  };

  const toggleAchievementBit = (achId: number, bit: number) => {
    setMumble((prev) => {
      const current = prev.completedAchievements[achId] || [];
      const exists = current.includes(bit);
      const updated = exists ? current.filter((b) => b !== bit) : [...current, bit];
      return {
        ...prev,
        completedAchievements: {
          ...prev.completedAchievements,
          [achId]: updated,
        },
      };
    });
  };

  const handleExportZip = async () => {
    setIsDownloading(true);
    try {
      await downloadNexusAddonProjectZip();
    } finally {
      setIsDownloading(false);
    }
  };

  return (
    <div className="min-h-screen bg-[#0B0F19] text-slate-100 flex flex-col">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="flex items-center justify-between px-6 py-3.5 bg-[#0B0F19]/95 backdrop-blur-md border-b border-slate-800/90 sticky top-0 z-30">
        {/* Zone 1: Single Text Element Wordmark */}
        <a
          href="#top"
          onClick={(e) => {
            e.preventDefault();
            setActiveSection('simulator');
          }}
          className="text-base font-bold tracking-tight text-slate-100 whitespace-nowrap"
        >
          NexusPathing
        </a>

        {/* Zone 2: 4 Clean Text Navigation Links */}
        <nav className="hidden md:flex items-center gap-7 text-xs font-medium text-slate-300">
          <button
            onClick={() => setActiveSection('simulator')}
            className={`py-1 transition-colors whitespace-nowrap ${
              activeSection === 'simulator'
                ? 'text-sky-400 underline underline-offset-8 decoration-2 decoration-sky-400'
                : 'hover:text-white'
            }`}
          >
            3D D3D11 Simulator
          </button>
          <button
            onClick={() => setActiveSection('cpp-source')}
            className={`py-1 transition-colors whitespace-nowrap ${
              activeSection === 'cpp-source'
                ? 'text-sky-400 underline underline-offset-8 decoration-2 decoration-sky-400'
                : 'hover:text-white'
            }`}
          >
            C++20 Addon Source
          </button>
          <button
            onClick={() => setActiveSection('inspector')}
            className={`py-1 transition-colors whitespace-nowrap ${
              activeSection === 'inspector'
                ? 'text-sky-400 underline underline-offset-8 decoration-2 decoration-sky-400'
                : 'hover:text-white'
            }`}
          >
            Pack & .trl Inspector
          </button>
          <button
            onClick={() => setActiveSection('parity')}
            className={`py-1 transition-colors whitespace-nowrap ${
              activeSection === 'parity'
                ? 'text-sky-400 underline underline-offset-8 decoration-2 decoration-sky-400'
                : 'hover:text-white'
            }`}
          >
            Feature Parity Matrix
          </button>
        </nav>

        {/* Zone 3: Primary Actions (1-2 actions per Top Bar Contract) */}
        <div className="flex items-center gap-2.5">
          <button
            onClick={() => downloadCompiledDll()}
            className="px-3.5 py-2 text-xs font-semibold text-slate-950 bg-emerald-400 hover:bg-emerald-300 rounded-md transition-colors whitespace-nowrap flex items-center gap-1.5"
          >
            <Download className="w-3.5 h-3.5" />
            <span>Download NexusPathing.dll (v3.1.1)</span>
          </button>
          <button
            onClick={handleExportZip}
            disabled={isDownloading}
            className="px-3.5 py-2 text-xs font-medium text-slate-200 bg-slate-800 hover:bg-slate-700 disabled:opacity-50 rounded-md transition-colors whitespace-nowrap flex items-center gap-1.5"
          >
            <FileArchive className="w-3.5 h-3.5 text-sky-400" />
            <span>{isDownloading ? 'Packaging...' : 'Source + DLL (.zip)'}</span>
          </button>
        </div>
      </header>

      {/* Main Content Container */}
      <main className="flex-1 max-w-[1440px] w-full mx-auto px-6 py-6 space-y-6">
        {/* Compact Workbench Header */}
        <div className="flex flex-col lg:flex-row lg:items-end justify-between gap-4 pb-5 border-b border-slate-800/80">
          <div>
            <div className="flex items-center gap-2 text-xs text-slate-400 font-mono">
              <span>Raidcore Nexus API v6</span>
              <span aria-hidden="true">·</span>
              <span>DirectX 11 Hook</span>
              <span aria-hidden="true">·</span>
              <span>Blish HUD Pathing 100% Parity</span>
              <span aria-hidden="true">·</span>
              <span>LuaJIT 2.1 + sol2</span>
            </div>
            <h1 className="text-2xl sm:text-3xl font-bold tracking-tight text-slate-100 mt-1.5">
              Blish HUD Pathing to Raidcore Nexus Addon Port
            </h1>
          </div>

          {/* Mobile / Quick Section Switcher */}
          <div className="flex flex-wrap items-center gap-1.5 p-1 bg-[#111622] border border-slate-800 rounded-lg">
            <button
              onClick={() => setActiveSection('simulator')}
              className={`px-3 py-1.5 text-xs font-medium rounded-md transition-colors flex items-center gap-1.5 whitespace-nowrap ${
                activeSection === 'simulator'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              <Compass className="w-3.5 h-3.5" />
              <span>3D D3D11 Simulator</span>
            </button>
            <button
              onClick={() => setActiveSection('cpp-source')}
              className={`px-3 py-1.5 text-xs font-medium rounded-md transition-colors flex items-center gap-1.5 whitespace-nowrap ${
                activeSection === 'cpp-source'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              <Code2 className="w-3.5 h-3.5" />
              <span>C++20 Source (6 Files)</span>
            </button>
            <button
              onClick={() => setActiveSection('inspector')}
              className={`px-3 py-1.5 text-xs font-medium rounded-md transition-colors flex items-center gap-1.5 whitespace-nowrap ${
                activeSection === 'inspector'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              <FileArchive className="w-3.5 h-3.5" />
              <span>Pack & .trl Inspector</span>
            </button>
            <button
              onClick={() => setActiveSection('parity')}
              className={`px-3 py-1.5 text-xs font-medium rounded-md transition-colors flex items-center gap-1.5 whitespace-nowrap ${
                activeSection === 'parity'
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              <Layers className="w-3.5 h-3.5" />
              <span>12/12 Feature Parity</span>
            </button>
          </div>
        </div>

        {/* SECTION 1: 3D D3D11 Simulator & MumbleLink State Inspector */}
        {activeSection === 'simulator' && (
          <div className="grid grid-cols-1 xl:grid-cols-12 gap-6">
            {/* Left 8 Columns: 3D D3D11 Viewport */}
            <div className="xl:col-span-8 space-y-4">
              <Viewport3DSimulator
                mumble={mumble}
                setMumble={setMumble}
                categories={categories}
                onToggleCategory={handleToggleCategory}
                markers={markers}
                trails={trails}
                onTriggerMarker={handleTriggerMarker}
                onResetAllBehaviors={handleResetAllBehaviors}
                clipboardToast={clipboardToast}
                luaLogs={luaLogs}
              />

              {/* Interactive Marker Quick-Teleport & Trigger Strip */}
              <div className="bg-[#111622] border border-slate-800 rounded-lg p-4">
                <div className="flex items-center justify-between mb-3">
                  <span className="text-xs font-semibold text-slate-200">
                    Active Map POIs (Click to Teleport Player Avatar Within Trigger Range)
                  </span>
                  <span className="text-xs font-mono text-slate-400 tabular-nums">
                    MapID: {mumble.mapId} · {markers.length} POIs Loaded
                  </span>
                </div>

                <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2.5">
                  {markers.map((m) => (
                    <div
                      key={m.id}
                      className="p-2.5 bg-slate-900/90 border border-slate-800/90 rounded-md flex items-center justify-between gap-2 hover:border-slate-700 transition-colors"
                    >
                      <div className="min-w-0">
                        <div className="flex items-center gap-1.5">
                          <span
                            className="w-2 h-2 rounded-full shrink-0"
                            style={{ backgroundColor: m.color }}
                          />
                          <span className="text-xs font-medium text-slate-200 truncate">
                            {m.infoText ? m.infoText.split('(')[0].trim() : m.type}
                          </span>
                        </div>
                        <div className="text-[11px] font-mono text-slate-400 tabular-nums mt-0.5 truncate">
                          ({m.position.x}, {m.position.y}, {m.position.z}) · Beh:{m.behavior ?? 0}
                        </div>
                      </div>

                      <div className="flex items-center gap-1 shrink-0">
                        <button
                          onClick={() =>
                            setMumble((prev) => ({
                              ...prev,
                              avatarPosition: {
                                x: m.position.x - 2.0,
                                y: m.position.y,
                                z: m.position.z - 2.0,
                              },
                            }))
                          }
                          className="px-2 py-1 text-[11px] font-medium bg-slate-800 hover:bg-slate-700 text-sky-300 rounded transition-colors whitespace-nowrap"
                        >
                          Teleport
                        </button>
                        <button
                          onClick={() => handleTriggerMarker(m.id)}
                          className="px-2 py-1 text-[11px] font-medium bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 rounded transition-colors whitespace-nowrap"
                        >
                          [F]
                        </button>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </div>

            {/* Right 4 Columns: MumbleLink Shared Memory & Behavior Filter Control Panel */}
            <div className="xl:col-span-4 space-y-5">
              <div className="bg-[#111622] border border-slate-800 rounded-lg p-4 space-y-4">
                <div className="flex items-center justify-between pb-3 border-b border-slate-800">
                  <div className="flex items-center gap-2">
                    <Sliders className="w-4 h-4 text-sky-400" />
                    <h2 className="text-sm font-semibold text-slate-100">
                      MumbleLink & GW2 API Simulator
                    </h2>
                  </div>
                  <span className="text-xs font-mono text-emerald-400 tabular-nums">
                    DL_MUMBLE_LINK
                  </span>
                </div>

                {/* Mount Selector (Tests mount="Skyscale,Griffon" attribute filter) */}
                <div className="space-y-1.5">
                  <label className="block text-xs font-medium text-slate-300">
                    Active Mount (<code className="text-sky-400">Mumble.Context.MountIndex</code>)
                  </label>
                  <select
                    value={mumble.mount}
                    onChange={(e) =>
                      setMumble((prev) => ({ ...prev, mount: e.target.value as GW2Mount }))
                    }
                    className="w-full px-3 py-2 bg-slate-900 border border-slate-800 rounded-md text-xs text-slate-100 focus:outline-none focus:border-sky-500"
                  >
                    {MOUNTS.map((mt) => (
                      <option key={mt} value={mt}>
                        {mt} {mt === 'Skyscale' || mt === 'Griffon' ? '(Unlocks Updraft Ring POI)' : ''}
                      </option>
                    ))}
                  </select>
                </div>

                {/* Profession Selector */}
                <div className="grid grid-cols-2 gap-3">
                  <div className="space-y-1.5">
                    <label className="block text-xs font-medium text-slate-300">
                      Profession (<code className="text-sky-400">Identity</code>)
                    </label>
                    <select
                      value={mumble.profession}
                      onChange={(e) =>
                        setMumble((prev) => ({
                          ...prev,
                          profession: e.target.value as GW2Profession,
                        }))
                      }
                      className="w-full px-2.5 py-2 bg-slate-900 border border-slate-800 rounded-md text-xs text-slate-100 focus:outline-none focus:border-sky-500"
                    >
                      {PROFESSIONS.map((prof) => (
                        <option key={prof} value={prof}>
                          {prof}
                        </option>
                      ))}
                    </select>
                  </div>

                  <div className="space-y-1.5">
                    <label className="block text-xs font-medium text-slate-300">
                      FOV (<code className="text-sky-400">{mumble.fov}°</code>)
                    </label>
                    <input
                      type="range"
                      min={40}
                      max={85}
                      value={mumble.fov}
                      onChange={(e) =>
                        setMumble((prev) => ({ ...prev, fov: Number(e.target.value) }))
                      }
                      className="w-full accent-sky-400 mt-2"
                    />
                  </div>
                </div>

                {/* GW2 v2/account/achievements Live Filter Simulation */}
                <div className="pt-3 border-t border-slate-800 space-y-2.5">
                  <div className="flex items-center justify-between">
                    <span className="text-xs font-semibold text-slate-200">
                      GW2 API Achievement Sync (<code className="text-amber-300">/v2/account/achievements</code>)
                    </span>
                  </div>
                  <p className="text-xs text-slate-400 leading-relaxed">
                    Blish HUD hides markers when their linked <code className="text-slate-200">achievementId</code> &{' '}
                    <code className="text-slate-200">achievementBit</code> are completed on your GW2 account:
                  </p>

                  <div className="space-y-2">
                    <label className="flex items-center justify-between p-2.5 bg-slate-900 border border-slate-800 rounded-md cursor-pointer hover:border-slate-700">
                      <div className="text-xs">
                        <div className="font-medium text-slate-200">
                          Achievement #6912 · Bit 0 (Astral Lantern #1)
                        </div>
                        <div className="text-[11px] text-slate-400 font-mono">
                          Hides Astral Lantern #1 POI when unlocked
                        </div>
                      </div>
                      <input
                        type="checkbox"
                        checked={(mumble.completedAchievements[6912] || []).includes(0)}
                        onChange={() => toggleAchievementBit(6912, 0)}
                        className="rounded border-slate-600 bg-slate-800 text-sky-500"
                      />
                    </label>

                    <label className="flex items-center justify-between p-2.5 bg-slate-900 border border-slate-800 rounded-md cursor-pointer hover:border-slate-700">
                      <div className="text-xs">
                        <div className="font-medium text-slate-200">
                          Achievement #6912 · Bit 1 (Spire Mastery Insight)
                        </div>
                        <div className="text-[11px] text-slate-400 font-mono">
                          Hides Bastion Mastery Insight POI when unlocked
                        </div>
                      </div>
                      <input
                        type="checkbox"
                        checked={(mumble.completedAchievements[6912] || []).includes(1)}
                        onChange={() => toggleAchievementBit(6912, 1)}
                        className="rounded border-slate-600 bg-slate-800 text-sky-500"
                      />
                    </label>
                  </div>
                </div>

                {/* Behavior States & InvertBehavior Tester */}
                <div className="pt-3 border-t border-slate-800 space-y-2.5">
                  <div className="flex items-center justify-between">
                    <span className="text-xs font-semibold text-slate-200">
                      Blish HUD Behavior Engine (0–7)
                    </span>
                    <button
                      onClick={handleResetAllBehaviors}
                      className="flex items-center gap-1 text-xs text-sky-400 hover:text-sky-300 font-medium"
                    >
                      <RefreshCw className="w-3 h-3" />
                      <span>Reset All</span>
                    </button>
                  </div>

                  <div className="space-y-1.5 text-xs">
                    {markers.map((m) => (
                      <div
                        key={m.id}
                        className="flex items-center justify-between py-1.5 px-2.5 bg-slate-900/70 rounded border border-slate-800/80 font-mono tabular-nums"
                      >
                        <span className="truncate max-w-[175px] text-slate-300">
                          {m.guid.slice(0, 12)} (Beh {m.behavior ?? 0})
                        </span>
                        <span
                          className={
                            m.triggered ? 'text-amber-400 font-semibold' : 'text-emerald-400'
                          }
                        >
                          {m.triggered ? 'Filtered (Triggered)' : 'Active in World'}
                        </span>
                      </div>
                    ))}
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* SECTION 2: Complete C++20 Raidcore Nexus Addon Source Explorer */}
        {activeSection === 'cpp-source' && <CppSourceExplorer />}

        {/* SECTION 3: Pack Archive, XML Category & Binary .trl Hex Inspector */}
        {activeSection === 'inspector' && (
          <PackBinaryInspector
            xmlText={xmlText}
            onUpdateXml={setXmlText}
            luaText={luaText}
            onUpdateLua={setLuaText}
            onLoadCustomPack={(newCats, newMarkers, newTrails, rawXml) => {
              setCategories(newCats);
              setMarkers(newMarkers);
              setTrails(newTrails);
              setXmlText(rawXml);
            }}
          />
        )}

        {/* SECTION 4: Blish HUD Pathing -> Raidcore Nexus 100% Feature Parity Matrix */}
        {activeSection === 'parity' && <FeatureParityMatrix />}
      </main>

      {/* Clean Quiet Footer */}
      <footer className="mt-12 border-t border-slate-800/80 py-4 px-6 text-xs text-slate-500 flex flex-wrap items-center justify-between gap-4 max-w-[1440px] w-full mx-auto">
        <div>
          NexusPathing // Open-Source C++20 & DirectX 11 Port of Blish HUD Pathing for Raidcore Nexus
        </div>
        <div className="flex items-center gap-4">
          <button
            onClick={() => setActiveSection('cpp-source')}
            className="hover:text-slate-300 transition-colors"
          >
            View Nexus.h & dllmain.cpp
          </button>
          <span>·</span>
          <button
            onClick={handleExportZip}
            className="hover:text-slate-300 transition-colors"
          >
            Download CMake Solution (.zip)
          </button>
        </div>
      </footer>
    </div>
  );
}
