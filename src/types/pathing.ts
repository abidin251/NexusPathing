export interface Vector3 {
  x: number;
  y: number; // height (up) in GW2 Mumble coordinates
  z: number; // north/south in GW2 Mumble coordinates
}

export interface MarkerCategory {
  name: string;
  fullPath: string;
  displayName: string;
  isSeparator?: boolean;
  defaultToggle: boolean;
  enabled: boolean;
  iconFile?: string;
  color?: string;
  alpha?: number;
  fadeNear?: number;
  fadeFar?: number;
  heightOffset?: number;
  iconSize?: number;
  children: MarkerCategory[];
}

export interface PathingMarker {
  id: string;
  guid: string;
  type: string; // e.g., "tw.skyscale.unlock.egg.fire"
  mapId: number;
  position: Vector3;
  iconFile: string;
  iconSize: number;
  heightOffset: number;
  alpha: number;
  color: string;
  fadeNear: number;
  fadeFar: number;
  cull: 'None' | 'Clockwise' | 'CounterClockwise';
  canFade: boolean;
  occlude: boolean;
  rotate?: Vector3;
  // Blish HUD Specific Attributes (bh-* stripped or standard)
  bounce?: {
    behavior: 'rise' | 'bounce';
    height: number;
    duration: number;
    delay?: number;
  };
  behavior?: number; // 0: Always, 1: Reappear on map change, 2: Reappear on daily reset, 3: Once per instance, 4: Reappear after timer, 6: Once per character, 7: Achievement
  invertBehavior?: boolean;
  resetLength?: number; // in seconds for behavior=4
  achievementId?: number;
  achievementBit?: number;
  mountFilter?: string[]; // e.g. ['Skyscale', 'Raptor', 'Griffon']
  professionFilter?: string[];
  festivalFilter?: string;
  copyValue?: string;
  copyMessage?: string;
  infoText?: string;
  triggerRange?: number;
  autoTrigger?: boolean;
  showHideCategory?: string;
  // Scripting attributes
  scriptOnce?: string;
  scriptTick?: string;
  scriptTrigger?: string;
  scriptFocus?: string;
  scriptFilter?: string;
  // Runtime state
  triggered?: boolean;
  triggeredAt?: number;
  focused?: boolean;
  scriptVisible?: boolean;
}

export interface PathingTrail {
  id: string;
  guid: string;
  type: string;
  mapId: number;
  trailDataPath: string;
  texturePath: string;
  color: string;
  alpha: number;
  animSpeed: number;
  trailScale: number;
  fadeNear: number;
  fadeFar: number;
  cull: 'None' | 'Clockwise' | 'CounterClockwise';
  canFade: boolean;
  mountFilter?: string[];
  achievementId?: number;
  // Segments separated by (0,0,0) break vectors in .trl binary
  segments: Vector3[][];
}

export type GW2Mount =
  | 'None'
  | 'Raptor'
  | 'Springer'
  | 'Skimmer'
  | 'Jackal'
  | 'Griffon'
  | 'RollerBeetle'
  | 'Warclaw'
  | 'Skyscale'
  | 'SiegeTurtle';

export type GW2Profession =
  | 'Guardian'
  | 'Warrior'
  | 'Engineer'
  | 'Ranger'
  | 'Thief'
  | 'Elementalist'
  | 'Mesmer'
  | 'Necromancer'
  | 'Revenant';

export interface MumbleLinkState {
  uiVersion: number;
  uiTick: number;
  avatarPosition: Vector3;
  avatarFront: Vector3;
  cameraPosition: Vector3;
  cameraFront: Vector3;
  fov: number;
  characterName: string;
  profession: GW2Profession;
  race: 'Asura' | 'Charr' | 'Human' | 'Norn' | 'Sylvari';
  mapId: number;
  mapName: string;
  worldId: number;
  mount: GW2Mount;
  inCombat: boolean;
  isCommander: boolean;
  isMapOpen: boolean;
  compassWidth: number;
  compassHeight: number;
  compassRotation: number;
  activeFestival: 'None' | 'LunarNewYear' | 'SuperAdventureBox' | 'DragonBash' | 'FourWinds' | 'Halloween' | 'Wintersday';
  completedAchievements: Record<number, number[]>; // achievementId -> unlocked bits
}

export interface CppSourceFile {
  path: string;
  filename: string;
  category: 'Core & Entry' | 'D3D11 Renderer & Shaders' | 'Parsers (.taco / .trl / XML)' | 'Blish Attributes & Behaviors' | 'Lua Scripting VM' | 'ImGui Nexus UI & Build';
  language: 'cpp' | 'hlsl' | 'cmake' | 'markdown';
  description: string;
  blishEquivalent: string;
  linesCount: number;
  code: string;
}

export interface FeatureParityItem {
  id: string;
  featureName: string;
  category: 'Pack Formats' | 'Rendering & Shaders' | 'Behaviors & Filters' | 'Blish Exclusive Attributes' | 'Lua Scripting Engine' | 'Nexus Ecosystem Integration';
  blishSourceFile: string;
  nexusCppFile: string;
  status: 'Ported (100%)' | 'Enhanced in D3D11';
  description: string;
  technicalMechanism: string;
}
