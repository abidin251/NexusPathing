import { MarkerCategory, PathingMarker, PathingTrail } from '../types/pathing';

export interface ParsedOverlayXml {
  categories: MarkerCategory[];
  markers: PathingMarker[];
  trails: PathingTrail[];
  rawXml: string;
}

/**
 * Strips Blish HUD `bh-` prefix overrides and normalizes attribute names to lowercase.
 * In Blish HUD Pathing, any attribute starting with `bh-` overrides the standard TacO attribute
 * of the same name when loaded in Blish HUD / NexusPathing.
 */
function extractNormalizedAttributes(el: Element): Record<string, string> {
  const attrs: Record<string, string> = {};
  const bhOverrides: Record<string, string> = {};

  for (let i = 0; i < el.attributes.length; i++) {
    const attr = el.attributes[i];
    const rawName = attr.name.toLowerCase();
    if (rawName.startsWith('bh-')) {
      bhOverrides[rawName.slice(3)] = attr.value;
    } else {
      attrs[rawName] = attr.value;
    }
  }

  return { ...attrs, ...bhOverrides };
}

function parseCategoryTree(
  parentEl: Element,
  parentPath: string,
  inheritedAttrs: Record<string, string>
): MarkerCategory[] {
  const result: MarkerCategory[] = [];
  const children = Array.from(parentEl.children).filter(
    (c) => c.tagName.toLowerCase() === 'markercategory'
  );

  for (const el of children) {
    const ownAttrs = extractNormalizedAttributes(el);
    const mergedAttrs = { ...inheritedAttrs, ...ownAttrs };
    const name = ownAttrs['name'] || 'unnamed';
    const fullPath = parentPath ? `${parentPath}.${name}` : name;
    const displayName = ownAttrs['displayname'] || name;
    const isSeparator = ownAttrs['isseparator'] === '1' || ownAttrs['isseparator'] === 'true';
    const defaultToggle = ownAttrs['defaulttoggle'] !== '0' && ownAttrs['defaulttoggle'] !== 'false';

    const cat: MarkerCategory = {
      name,
      fullPath: fullPath.toLowerCase(),
      displayName,
      isSeparator,
      defaultToggle,
      enabled: defaultToggle,
      iconFile: mergedAttrs['iconfile'],
      color: mergedAttrs['color'] || '#FFFFFF',
      alpha: mergedAttrs['alpha'] ? parseFloat(mergedAttrs['alpha']) : 1.0,
      fadeNear: mergedAttrs['fadenear'] ? parseFloat(mergedAttrs['fadenear']) : 1200,
      fadeFar: mergedAttrs['fadefar'] ? parseFloat(mergedAttrs['fadefar']) : 1800,
      heightOffset: mergedAttrs['heightoffset'] ? parseFloat(mergedAttrs['heightoffset']) : 1.5,
      iconSize: mergedAttrs['iconsize'] ? parseFloat(mergedAttrs['iconsize']) : 1.0,
      children: parseCategoryTree(el, fullPath, mergedAttrs),
    };

    result.push(cat);
  }

  return result;
}

function flattenCategoryAttributes(
  categories: MarkerCategory[],
  map: Map<string, MarkerCategory> = new Map()
): Map<string, MarkerCategory> {
  for (const cat of categories) {
    map.set(cat.fullPath.toLowerCase(), cat);
    if (cat.children.length > 0) {
      flattenCategoryAttributes(cat.children, map);
    }
  }
  return map;
}

export function parseOverlayXml(xmlText: string): ParsedOverlayXml {
  const parser = new DOMParser();
  const doc = parser.parseFromString(xmlText, 'text/xml');

  const overlayData = doc.querySelector('OverlayData') || doc.documentElement;
  const categories = parseCategoryTree(overlayData, '', {});
  const categoryMap = flattenCategoryAttributes(categories);

  const markers: PathingMarker[] = [];
  const trails: PathingTrail[] = [];

  const poisContainer = overlayData.querySelector('POIs') || overlayData;
  const poiElements = Array.from(poisContainer.children);

  let markerIndex = 0;
  let trailIndex = 0;

  for (const el of poiElements) {
    const tag = el.tagName.toLowerCase();
    if (tag !== 'poi' && tag !== 'trail') continue;

    const attrs = extractNormalizedAttributes(el);
    const type = (attrs['type'] || 'default').toLowerCase();
    const cat = categoryMap.get(type);

    const mapId = attrs['mapid'] ? parseInt(attrs['mapid'], 10) : 1510;
    const guid = attrs['guid'] || `guid-${tag}-${markerIndex + trailIndex}`;

    if (tag === 'poi') {
      const xpos = attrs['xpos'] ? parseFloat(attrs['xpos']) : 0;
      const ypos = attrs['ypos'] ? parseFloat(attrs['ypos']) : 0;
      const zpos = attrs['zpos'] ? parseFloat(attrs['zpos']) : 0;

      const mountStr = attrs['mount'];
      const mountFilter = mountStr
        ? mountStr.split(',').map((s) => s.trim())
        : undefined;

      const bounceBehavior = attrs['bounce'] as 'rise' | 'bounce' | undefined;

      markers.push({
        id: `poi-${markerIndex++}`,
        guid,
        type,
        mapId,
        position: { x: xpos, y: ypos, z: zpos },
        iconFile: attrs['iconfile'] || cat?.iconFile || 'Data/Icons/skyscale_target.png',
        iconSize: attrs['iconsize'] ? parseFloat(attrs['iconsize']) : cat?.iconSize ?? 1.0,
        heightOffset: attrs['heightoffset'] ? parseFloat(attrs['heightoffset']) : cat?.heightOffset ?? 1.5,
        alpha: attrs['alpha'] ? parseFloat(attrs['alpha']) : cat?.alpha ?? 1.0,
        color: attrs['color'] || cat?.color || '#38BDF8',
        fadeNear: attrs['fadenear'] ? parseFloat(attrs['fadenear']) : cat?.fadeNear ?? 80,
        fadeFar: attrs['fadefar'] ? parseFloat(attrs['fadefar']) : cat?.fadeFar ?? 140,
        cull: (attrs['cull'] as 'None' | 'Clockwise' | 'CounterClockwise') || 'None',
        canFade: attrs['canfade'] !== '0' && attrs['canfade'] !== 'false',
        occlude: attrs['occlude'] === '1' || attrs['occlude'] === 'true',
        bounce: bounceBehavior
          ? {
              behavior: bounceBehavior,
              height: attrs['bounce-height'] ? parseFloat(attrs['bounce-height']) : 2.0,
              duration: attrs['bounce-duration'] ? parseFloat(attrs['bounce-duration']) : 1.5,
              delay: attrs['bounce-delay'] ? parseFloat(attrs['bounce-delay']) : 0,
            }
          : undefined,
        behavior: attrs['behavior'] ? parseInt(attrs['behavior'], 10) : 0,
        invertBehavior: attrs['invertbehavior'] === '1' || attrs['invertbehavior'] === 'true',
        resetLength: attrs['resetlength'] ? parseInt(attrs['resetlength'], 10) : undefined,
        achievementId: attrs['achievementid'] ? parseInt(attrs['achievementid'], 10) : undefined,
        achievementBit: attrs['achievementbit'] ? parseInt(attrs['achievementbit'], 10) : undefined,
        mountFilter,
        copyValue: attrs['copy'],
        copyMessage: attrs['copy-message'],
        infoText: attrs['info'],
        triggerRange: attrs['triggerrange'] ? parseFloat(attrs['triggerrange']) : 6.0,
        autoTrigger: attrs['autotrigger'] === '1' || attrs['autotrigger'] === 'true',
        showHideCategory: attrs['showhide'],
        scriptOnce: attrs['script-once'],
        scriptTick: attrs['script-tick'],
        scriptTrigger: attrs['script-trigger'],
        scriptFocus: attrs['script-focus'],
        scriptFilter: attrs['script-filter'],
        scriptVisible: true,
      });
    } else if (tag === 'trail') {
      const mountStr = attrs['mount'];
      const mountFilter = mountStr
        ? mountStr.split(',').map((s) => s.trim())
        : undefined;

      trails.push({
        id: `trail-${trailIndex++}`,
        guid,
        type,
        mapId,
        trailDataPath: attrs['traildata'] || 'Data/Trails/amnytas_skyscale_route.trl',
        texturePath: attrs['texture'] || 'Data/Trails/dashed_chevrons.png',
        color: attrs['color'] || cat?.color || '#38BDF8',
        alpha: attrs['alpha'] ? parseFloat(attrs['alpha']) : cat?.alpha ?? 0.9,
        animSpeed: attrs['animspeed'] ? parseFloat(attrs['animspeed']) : 1.2,
        trailScale: attrs['trailscale'] ? parseFloat(attrs['trailscale']) : 1.0,
        fadeNear: attrs['fadenear'] ? parseFloat(attrs['fadenear']) : 90,
        fadeFar: attrs['fadefar'] ? parseFloat(attrs['fadefar']) : 160,
        cull: (attrs['cull'] as 'None' | 'Clockwise' | 'CounterClockwise') || 'None',
        canFade: attrs['canfade'] !== '0' && attrs['canfade'] !== 'false',
        mountFilter,
        achievementId: attrs['achievementid'] ? parseInt(attrs['achievementid'], 10) : undefined,
        segments: [],
      });
    }
  }

  return {
    categories,
    markers,
    trails,
    rawXml: xmlText,
  };
}
