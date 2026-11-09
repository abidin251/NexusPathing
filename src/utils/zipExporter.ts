import JSZip from 'jszip';
import { CPP_SOURCE_FILES } from '../data/cppSourceFiles';
import { SAMPLE_LUA_SCRIPT, SAMPLE_PACK_XML, getSampleTrlBuffer } from '../data/samplePackData';

export async function downloadCompiledDll(): Promise<void> {
  const a = document.createElement('a');
  a.href = '/downloads/NexusPathing.dll?v=3.1.1';
  a.download = 'NexusPathing.dll';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

export async function downloadNexusAddonProjectZip(): Promise<void> {
  const zip = new JSZip();

  // Fetch and bundle the pre-compiled Windows x64 PE32+ binary (NexusPathing.dll)
  try {
    const dllResp = await fetch('/downloads/NexusPathing.dll?v=3.1.1');
    if (dllResp.ok) {
      const dllBuffer = await dllResp.arrayBuffer();
      zip.file('Release/NexusPathing.dll', dllBuffer);
    }
  } catch (e) {
    console.warn('Could not attach pre-built NexusPathing.dll to zip:', e);
  }

  // Add all C++20 source files, headers, HLSL shaders, and CMakeLists.txt
  for (const file of CPP_SOURCE_FILES) {
    zip.file(file.path, file.code);
  }

  // Add sample Blish HUD / TacO pack inside markers/ sample folder
  zip.file('sample_pack/OverlayData.xml', SAMPLE_PACK_XML);
  zip.file('sample_pack/pack.lua', SAMPLE_LUA_SCRIPT);
  zip.file(
    'sample_pack/Data/Trails/amnytas_skyscale_route.trl',
    getSampleTrlBuffer('Data/Trails/amnytas_skyscale_route.trl')
  );
  zip.file(
    'sample_pack/Data/Trails/kryptis_rift_patrol.trl',
    getSampleTrlBuffer('Data/Trails/kryptis_rift_patrol.trl')
  );
  zip.file(
    'sample_pack/Data/Trails/octovine_burn_phase.trl',
    getSampleTrlBuffer('Data/Trails/octovine_burn_phase.trl')
  );

  // Add comprehensive README.md
  const readme = `# NexusPathing - Blish HUD Pathing Port for Raidcore Nexus

NexusPathing is a native C++20 / DirectX 11 addon for **Raidcore Nexus** in Guild Wars 2 that provides 100% feature parity with the **Blish HUD Pathing** module.

## Supported Blish HUD Pathing Features
1. **Archive & Format Parity**: Loads \`.taco\`, \`.zip\`, and unpacked directories from \`<GW2>/addons/NexusPathing/markers/\`.
2. **Binary \`.trl\` Trail Mesh Extrusion**: Parses little-endian \`int32 Version\`, \`int32 MapID\`, and \`float32 (X,Y,Z)\` streams including \`(0,0,0)\` disjoint segment breaks.
3. **Hierarchical XML Categories & \`bh-*\` Attributes**: Supports recursive \`<MarkerCategory>\` attribute inheritance and strips \`bh-\` prefixes to prioritize Blish HUD attributes over legacy TacO attributes.
4. **D3D11 World-Space & Minimap/World Map Projection**: Hardware-accelerated HLSL Shader Model 5.0 trail ribbons with UV flow (\`animSpeed\`), spherical player cutout, distance fading (\`fadeNear\` / \`fadeFar\`), and Compass/Fullscreen Map (\`M\`) projection.
5. **Behaviors 0-7 & GW2 v2 API Sync**: Supports daily reset (\`behavior="2"\`), map change (\`behavior="1"\`), instance (\`behavior="3"\`), timer (\`behavior="4"\` + \`resetLength\`), per-character (\`behavior="6"\`), and achievement bitmask (\`behavior="7"\` + \`achievementId\` / \`achievementBit\`) with \`invertBehavior\`.
6. **Interactive Attributes**: Supports \`copy\`, \`copy-message\`, \`bounce\` (\`bounce\` / \`rise\`), \`showHide\`, \`mount\`, \`profession\`, and \`festival\` filters.
7. **LuaJIT 2.1 + sol2 Scripting VM**: Executes \`pack.lua\` and marker script hooks (\`script-once\`, \`script-tick\`, \`script-trigger\`, \`script-focus\`, \`script-filter\`).

## Building the Addon DLL (\`NexusPathing.dll\`)
\`\`\`bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake -A x64
cmake --build . --config Release
\`\`\`
Copy \`Release/NexusPathing.dll\` into \`<Guild Wars 2>/addons/\`.
`;
  zip.file('README.md', readme);

  const blob = await zip.generateAsync({ type: 'blob' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'NexusPathing_Raidcore_Addon_Source.zip';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
