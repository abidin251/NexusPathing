import { FeatureParityItem, Vector3 } from '../types/pathing';
import { encodeTrlBinary } from '../utils/trlParser';

export const SAMPLE_PACK_XML = `<?xml version="1.0" encoding="utf-8"?>
<OverlayData>
  <!-- Blish HUD / TacO Category Tree with bh-* Prefix Attribute Overrides -->
  <MarkerCategory name="tehs" displayname="Teh's Trails &amp; Skyscale Routes" defaulttoggle="1" iconSize="1.15" fadeNear="85" fadeFar="145">
    <MarkerCategory name="soto" displayname="Secrets of the Obscure (Amnytas)" defaulttoggle="1">
      <MarkerCategory name="skyscale" displayname="Skyscale Ley-Line Mastery Course" bh-displayname="Skyscale Ley-Line Mastery (Nexus D3D11)" color="#38BDF8" defaulttoggle="1" />
      <MarkerCategory name="rifts" displayname="Kryptis Rift Hunting &amp; Essences" color="#F43F5E" defaulttoggle="1" />
      <MarkerCategory name="lanterns" displayname="Illuminated Astral Lanterns" color="#F59E0B" defaulttoggle="1" />
    </MarkerCategory>
    <MarkerCategory name="octovine" displayname="Auric Basin: Octovine Meta" defaulttoggle="1">
      <MarkerCategory name="south" displayname="Southwatch Pylon &amp; Armor Sequence" color="#10B981" defaulttoggle="1" />
    </MarkerCategory>
  </MarkerCategory>

  <POIs>
    <!-- 1. Skyscale Start Beacon with bh-bounce & copy waypoint -->
    <POI MapID="1510" xpos="-42.0" ypos="8.5" zpos="-38.0" type="tehs.soto.skyscale" GUID="kR9vL2mP4qW8xZ1aB3cE5g=="
         iconFile="Data/Icons/skyscale_mount.png" bh-bounce="bounce" bounce-height="2.4" bounce-duration="1.4"
         copy="[&amp;BDkOAAA=]" copy-message="Copied Amnytas Bastion Waypoint [&amp;BDkOAAA=] to clipboard!"
         info="Press [F] Interact to copy Waypoint &amp; trigger Skyscale Route timer" triggerRange="9.0" />

    <!-- 2. Updraft Ley-Line Boost Ring (Mount filtered: Skyscale or Griffon) -->
    <POI MapID="1510" xpos="-18.0" ypos="14.2" zpos="-14.0" type="tehs.soto.skyscale" GUID="mN4pQ7rS9tU2vW5xY8zA1b=="
         iconFile="Data/Icons/updraft_ring.png" mount="Skyscale,Griffon" bh-bounce="rise" bounce-height="3.0"
         info="Ley-Line Updraft: Requires Skyscale or Griffon mounted" triggerRange="8.0" autoTrigger="1" />

    <!-- 3. Astral Ward Chest #1 (Behavior 2: Daily Reset Filter + Achievement ID 6912 Bit 0) -->
    <POI MapID="1510" xpos="8.0" ypos="17.8" zpos="4.0" type="tehs.soto.lanterns" GUID="pL3kM6nJ9hG2fD5sA8qW1e=="
         iconFile="Data/Icons/astral_lantern.png" behavior="2" achievementId="6912" achievementBit="0"
         copy="[&amp;BEIOAAA=]" copy-message="Astral Lantern #1 Lit!"
         info="Astral Lantern #1 (Behavior=2 Daily Reset | Achievement #6912 Bit 0)" triggerRange="8.5" />

    <!-- 4. Kryptis Tier 3 Rift Portal (Lua Scripted Pulse & Trigger) -->
    <POI MapID="1510" xpos="32.0" ypos="12.0" zpos="24.0" type="tehs.soto.rifts" GUID="xZ8cV5bN2mL9kJ6hG3fD0s=="
         iconFile="Data/Icons/kryptis_rift.png" behavior="4" resetLength="30"
         script-tick="pulseRiftScale" script-trigger="onKryptisRiftSealed"
         info="Kryptis Rift Boss (Behavior=4 Timer 30s | Lua script-tick &amp; script-trigger)" triggerRange="10.0" />

    <!-- 5. Astral Ward Spire Summit (Behavior 7: Achievement Filter + InvertBehavior Toggle) -->
    <POI MapID="1510" xpos="48.0" ypos="21.5" zpos="-16.0" type="tehs.soto.lanterns" GUID="qW1eR4tY7uI0oP3aS6dF9g=="
         iconFile="Data/Icons/mastery_insight.png" behavior="7" achievementId="6912" achievementBit="1"
         bh-bounce="bounce" bounce-height="1.8"
         info="Bastion of the Celestial Mastery Insight (Achievement #6912 Bit 1)" triggerRange="8.5" />

    <!-- 6. Octovine South Pylon Marker -->
    <POI MapID="1510" xpos="-10.0" ypos="6.0" zpos="38.0" type="tehs.octovine.south" GUID="zX9cV2bN5mK8jH1gF4dS7a=="
         iconFile="Data/Icons/octovine_pylon.png" behavior="4" resetLength="15"
         showHide="tehs.soto.rifts"
         info="Octovine Explosive Keg Station (Toggles Rift Category via ShowHide)" triggerRange="9.0" />

    <!-- Trails referencing binary .trl files -->
    <Trail type="tehs.soto.skyscale" GUID="trL1_skyscale_main" trailData="Data/Trails/amnytas_skyscale_route.trl"
           texture="Data/Trails/chevron_flow.png" color="#38BDF8" animSpeed="1.4" trailScale="1.1"
           fadeNear="90" fadeFar="160" cull="None" />

    <Trail type="tehs.soto.rifts" GUID="trL2_kryptis_rifts" trailData="Data/Trails/kryptis_rift_patrol.trl"
           texture="Data/Trails/dashed_march.png" color="#F43F5E" animSpeed="1.8" trailScale="0.95"
           fadeNear="85" fadeFar="150" cull="None" />

    <Trail type="tehs.octovine.south" GUID="trL3_octovine_south" trailData="Data/Trails/octovine_burn_phase.trl"
           texture="Data/Trails/solid_glow.png" color="#10B981" animSpeed="1.0" trailScale="1.0"
           fadeNear="90" fadeFar="160" cull="None" />
  </POIs>
</OverlayData>`;

export const SAMPLE_TRL_SEGMENTS: Record<string, Vector3[][]> = {
  'Data/Trails/amnytas_skyscale_route.trl': [
    [
      { x: -42.0, y: 8.5, z: -38.0 },
      { x: -34.0, y: 10.2, z: -30.0 },
      { x: -26.0, y: 12.5, z: -22.0 },
      { x: -18.0, y: 14.2, z: -14.0 },
      { x: -6.0, y: 16.4, z: -4.0 },
      { x: 8.0, y: 17.8, z: 4.0 },
    ],
    // Segment 2 after a (0,0,0) break vector in the binary .trl
    [
      { x: 14.0, y: 18.5, z: 0.0 },
      { x: 26.0, y: 19.8, z: -6.0 },
      { x: 38.0, y: 20.6, z: -12.0 },
      { x: 48.0, y: 21.5, z: -16.0 },
    ],
  ],
  'Data/Trails/kryptis_rift_patrol.trl': [
    [
      { x: 8.0, y: 17.8, z: 4.0 },
      { x: 16.0, y: 15.5, z: 12.0 },
      { x: 24.0, y: 13.6, z: 18.0 },
      { x: 32.0, y: 12.0, z: 24.0 },
      { x: 20.0, y: 9.5, z: 32.0 },
      { x: 4.0, y: 7.2, z: 36.0 },
      { x: -10.0, y: 6.0, z: 38.0 },
    ],
  ],
  'Data/Trails/octovine_burn_phase.trl': [
    [
      { x: -10.0, y: 6.0, z: 38.0 },
      { x: -22.0, y: 6.8, z: 24.0 },
      { x: -32.0, y: 7.4, z: 4.0 },
      { x: -38.0, y: 8.0, z: -18.0 },
      { x: -42.0, y: 8.5, z: -38.0 },
    ],
  ],
};

export function getSampleTrlBuffer(path: string): ArrayBuffer {
  const segs = SAMPLE_TRL_SEGMENTS[path] || SAMPLE_TRL_SEGMENTS['Data/Trails/amnytas_skyscale_route.trl'];
  return encodeTrlBinary(1510, segs);
}

export const SAMPLE_LUA_SCRIPT = `-- pack.lua // Blish HUD Pathing Lua VM Sandbox ported to Raidcore Nexus (sol2 + LuaJIT 2.1)
-- Exposes IPack, World, Mumble, Debug, and Vector3 globals at 60 FPS

Debug:Print("Initializing NexusPathing Lua VM for Secrets of the Obscure pack...")

function pulseRiftScale(marker, deltaSeconds)
    local dist = (Mumble.PlayerCharacter.Position - marker.Position):Length()
    if dist < 28.0 then
        local pulse = 1.0 + math.sin(World.Time * 4.5) * 0.28
        marker.Size = pulse
        marker.Alpha = 0.95
    else
        marker.Size = 1.0
        marker.Alpha = 0.70
    end
end

function onKryptisRiftSealed(marker)
    Debug:Info("Kryptis Rift sealed at " .. tostring(marker.Position) .. " by " .. Mumble.PlayerCharacter.Name)
    local lanternCat = Pack:Category("tehs.soto.lanterns")
    if lanternCat ~= nil then
        Debug:Print("Highlighting Astral Lantern category after Rift completion")
    end
end
`;

export const FEATURE_PARITY_MATRIX: FeatureParityItem[] = [
  {
    id: 'fp-1',
    featureName: 'Archive Loader (.taco / .zip / Directory)',
    category: 'Pack Formats',
    blishSourceFile: 'Content/ZipArchiveReader.cs',
    nexusCppFile: 'src/Pack/TacoArchiveReader.cpp',
    status: 'Ported (100%)',
    description: 'Zero-copy streaming decompression of .taco and .zip marker packs plus loose folder hot-reloading.',
    technicalMechanism: 'Uses libzip / miniz C++20 memory-mapped streams with case-insensitive path normalization.',
  },
  {
    id: 'fp-2',
    featureName: 'Binary Trail Parser (.trl) & Segment Break (0,0,0)',
    category: 'Pack Formats',
    blishSourceFile: 'Content/Readers/TrlReader.cs',
    nexusCppFile: 'src/Pack/TrlBinaryParser.cpp',
    status: 'Ported (100%)',
    description: 'Reads 4-byte Version, 4-byte MapID, and IEEE-754 float32 (X,Y,Z) node arrays with (0,0,0) sentinel breaks.',
    technicalMechanism: 'Direct reinterpret_cast<const TrlHeader*> and SIMD-aligned XMFLOAT3 ribbon extrusion.',
  },
  {
    id: 'fp-3',
    featureName: 'Hierarchical XML Category Tree & Inheritance',
    category: 'Pack Formats',
    blishSourceFile: 'Entities/PathableCategory.cs',
    nexusCppFile: 'src/Pack/XmlCategoryParser.cpp',
    status: 'Ported (100%)',
    description: 'Recursive MarkerCategory parsing with attribute cascade down to child POIs and Trails.',
    technicalMechanism: 'pugixml DOM traversal with O(1) FNV-1a hash lookup table for type="..." resolution.',
  },
  {
    id: 'fp-4',
    featureName: 'Blish HUD Prefix Overrides (bh-*)',
    category: 'Blish Exclusive Attributes',
    blishSourceFile: 'Format/XmlAttributeStripper.cs',
    nexusCppFile: 'src/Pack/XmlCategoryParser.cpp',
    status: 'Ported (100%)',
    description: 'Strips "bh-" prefix from XML attributes and prioritizes them over legacy TacO attributes.',
    technicalMechanism: 'Two-pass attribute ingestion prioritizing std::string_view::starts_with("bh-") keys.',
  },
  {
    id: 'fp-5',
    featureName: '3D World-Space Billboard Markers & trailScale Extrusion',
    category: 'Rendering & Shaders',
    blishSourceFile: 'Entities/Effects/MarkerEffect.fx',
    nexusCppFile: 'src/Renderer/D3D11PathingRenderer.cpp',
    status: 'Enhanced in D3D11',
    description: 'Hardware-instanced D3D11 billboards and camera-facing triangle-strip trail ribbons with UV flow.',
    technicalMechanism: 'Hooks IDXGISwapChain::Present via Raidcore Nexus RT_Render callback with HLSL SM 5.0 shaders.',
  },
  {
    id: 'fp-6',
    featureName: 'Player Sphere Cutout & Distance Fade (fadeNear / fadeFar)',
    category: 'Rendering & Shaders',
    blishSourceFile: 'Entities/Effects/TrailEffect.fx',
    nexusCppFile: 'src/Shaders/PathingShaders.hlsl',
    status: 'Ported (100%)',
    description: 'Smooth alpha attenuation near player character to prevent camera occlusion and far-clip popping.',
    technicalMechanism: 'Per-pixel HLSL smoothstep(FadeNear, FadeFar, distToCamera) * smoothstep(1.5, 4.5, distToAvatar).',
  },
  {
    id: 'fp-7',
    featureName: 'Minimap & Fullscreen World Map (M) Projection',
    category: 'Rendering & Shaders',
    blishSourceFile: 'UI/Map/MapPathingLayer.cs',
    nexusCppFile: 'src/Renderer/CompassMapProjector.cpp',
    status: 'Ported (100%)',
    description: 'Projects 3D continent/map coordinates onto the GW2 Compass minimap and fullscreen M map.',
    technicalMechanism: 'Reads MumbleLink Context compassWidth, compassHeight, compassRotation, and UI state bits.',
  },
  {
    id: 'fp-8',
    featureName: 'Behaviors 0–7 (Daily Reset, Instance, Timer, Character, Achievement)',
    category: 'Behaviors & Filters',
    blishSourceFile: 'Behaviors/StandardMarkerBehavior.cs',
    nexusCppFile: 'src/Behaviors/BehaviorEngine.cpp',
    status: 'Ported (100%)',
    description: 'Full state persistence for triggered GUIDs across daily UTC resets, map instances, and timers.',
    technicalMechanism: 'SQLite3 / JSON persistent state store in <GW2>/addons/NexusPathing/state.json.',
  },
  {
    id: 'fp-9',
    featureName: 'Live GW2 v2 API Achievement Synchronization',
    category: 'Behaviors & Filters',
    blishSourceFile: 'Behaviors/Filtered/AchievementFilter.cs',
    nexusCppFile: 'src/Behaviors/Gw2ApiSync.cpp',
    status: 'Ported (100%)',
    description: 'Automatically hides markers once their linked achievementId and achievementBit are unlocked.',
    technicalMechanism: 'Async WinHTTP / cpr worker polling /v2/account/achievements or shared Nexus API key.',
  },
  {
    id: 'fp-10',
    featureName: 'Context Filters (Mount, Profession, Race, MapType, Festival)',
    category: 'Behaviors & Filters',
    blishSourceFile: 'Behaviors/Filtered/MountFilter.cs',
    nexusCppFile: 'src/Behaviors/BehaviorEngine.cpp',
    status: 'Ported (100%)',
    description: 'Real-time filtering based on active mount (Skyscale, RollerBeetle, etc.), profession, and festival.',
    technicalMechanism: 'Zero-latency bitwise check against Nexus MumbleIdentity_t and MumbleLink_t.',
  },
  {
    id: 'fp-11',
    featureName: 'Interactive Attributes (Copy, Bounce, ShowHide, InvertBehavior)',
    category: 'Blish Exclusive Attributes',
    blishSourceFile: 'Behaviors/Activator/CopyBehavior.cs',
    nexusCppFile: 'src/Behaviors/InteractiveAttributes.cpp',
    status: 'Ported (100%)',
    description: 'Copies chat codes to Win32 clipboard on [F] interact, animates bounce/rise curves, toggles categories.',
    technicalMechanism: 'Hooks Nexus RegisterKeybind("KB_PATHING_INTERACT") and SetClipboardData(CF_UNICODETEXT).',
  },
  {
    id: 'fp-12',
    featureName: 'Lua Scripting Engine (script-once/tick/trigger/focus/filter)',
    category: 'Lua Scripting Engine',
    blishSourceFile: 'Scripting/ScriptingEngine.cs',
    nexusCppFile: 'src/Scripting/LuaPathingVM.cpp',
    status: 'Enhanced in D3D11',
    description: 'Full Blish HUD Lua API compatibility (Pack, World, Mumble, Debug, Vector3) with <0.15ms frame budget.',
    technicalMechanism: 'Embedded LuaJIT 2.1 with sol2 C++20 bindings and sandboxed instruction-count watchdog.',
  },
];
