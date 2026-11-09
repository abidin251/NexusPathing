import React, { useEffect, useRef, useState } from 'react';
import {
  MarkerCategory,
  MumbleLinkState,
  PathingMarker,
  PathingTrail,
  Vector3,
} from '../types/pathing';
import { Compass, Copy, Eye, EyeOff, Keyboard, Layers, Navigation, Sparkles } from 'lucide-react';

interface Viewport3DSimulatorProps {
  mumble: MumbleLinkState;
  setMumble: React.Dispatch<React.SetStateAction<MumbleLinkState>>;
  categories: MarkerCategory[];
  onToggleCategory: (fullPath: string) => void;
  markers: PathingMarker[];
  trails: PathingTrail[];
  onTriggerMarker: (markerId: string) => void;
  onResetAllBehaviors: () => void;
  clipboardToast: string | null;
  luaLogs: string[];
}

function isCategoryEnabled(categories: MarkerCategory[], typePath: string): boolean {
  const parts = typePath.toLowerCase().split('.');
  let currentList = categories;
  let currentPath = '';

  for (const part of parts) {
    currentPath = currentPath ? `${currentPath}.${part}` : part;
    const found = currentList.find((c) => c.fullPath.toLowerCase() === currentPath);
    if (!found) return true; // If unlisted category, default visible
    if (!found.enabled) return false;
    currentList = found.children;
  }
  return true;
}

export const Viewport3DSimulator: React.FC<Viewport3DSimulatorProps> = ({
  mumble,
  setMumble,
  categories,
  onToggleCategory,
  markers,
  trails,
  onTriggerMarker,
  onResetAllBehaviors,
  clipboardToast,
  luaLogs,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [cameraYaw, setCameraYaw] = useState<number>(0.65);
  const [cameraPitch, setCameraPitch] = useState<number>(0.38);
  const [cameraDistance, setCameraDistance] = useState<number>(36);
  const [isDragging, setIsDragging] = useState<boolean>(false);
  const dragStartRef = useRef<{ x: number; y: number; yaw: number; pitch: number }>({
    x: 0,
    y: 0,
    yaw: 0,
    pitch: 0,
  });
  const keysPressedRef = useRef<Set<string>>(new Set());
  const [showImGuiWindow, setShowImGuiWindow] = useState<boolean>(true);
  const [focusedMarker, setFocusedMarker] = useState<PathingMarker | null>(null);
  const [playerCutoutEnabled, setPlayerCutoutEnabled] = useState<boolean>(true);
  const [showMinimap, setShowMinimap] = useState<boolean>(true);
  const [trailOpacity, setTrailOpacity] = useState<number>(1.0);
  const [categorySearch, setCategorySearch] = useState<string>('');
  const [simTab, setSimTab] = useState<'visuals' | 'categories'>('visuals');

  // Keyboard WASD movement + [F] interact
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (
        document.activeElement?.tagName === 'INPUT' ||
        document.activeElement?.tagName === 'TEXTAREA' ||
        document.activeElement?.tagName === 'SELECT'
      ) {
        return;
      }
      const k = e.key.toLowerCase();
      keysPressedRef.current.add(k);

      if (k === 'f' && focusedMarker) {
        e.preventDefault();
        onTriggerMarker(focusedMarker.id);
      } else if (k === 'm') {
        e.preventDefault();
        setMumble((prev) => ({ ...prev, isMapOpen: !prev.isMapOpen }));
      }
    };

    const handleKeyUp = (e: KeyboardEvent) => {
      keysPressedRef.current.delete(e.key.toLowerCase());
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
    };
  }, [focusedMarker, onTriggerMarker, setMumble]);

  // Helper to evaluate marker visibility with all Blish HUD Pathing rules
  const isMarkerVisible = (m: PathingMarker, nowSec: number): boolean => {
    if (!isCategoryEnabled(categories, m.type)) return false;

    // Mount filter check
    if (m.mountFilter && m.mountFilter.length > 0) {
      if (!m.mountFilter.some((mt) => mt.toLowerCase() === mumble.mount.toLowerCase())) {
        return false;
      }
    }

    // Achievement filter check (Behavior 7 or achievementId)
    if (m.achievementId !== undefined && m.achievementBit !== undefined) {
      const unlockedBits = mumble.completedAchievements[m.achievementId] || [];
      if (unlockedBits.includes(m.achievementBit)) {
        return m.invertBehavior ? true : false;
      }
    }

    // Behavior state check
    let filteredByBehavior = false;
    if (m.triggered) {
      if (m.behavior === 4 && m.resetLength && m.triggeredAt) {
        const elapsed = nowSec - m.triggeredAt;
        filteredByBehavior = elapsed < m.resetLength;
      } else if (m.behavior && m.behavior > 0) {
        filteredByBehavior = true;
      }
    }

    if (m.invertBehavior) {
      filteredByBehavior = !filteredByBehavior;
    }

    return !filteredByBehavior;
  };

  // Main 60 FPS D3D11 simulation render loop
  useEffect(() => {
    let animationFrameId: number;
    let lastTime = performance.now();

    const renderLoop = (nowMs: number) => {
      const dt = Math.min((nowMs - lastTime) / 1000, 0.1);
      lastTime = nowMs;
      const nowSec = nowMs / 1000;

      // Update player position from WASD
      const keys = keysPressedRef.current;
      const moveSpeed = mumble.mount === 'Skyscale' ? 28.0 : mumble.mount === 'None' ? 14.0 : 24.0;
      let moveForward = 0;
      let moveRight = 0;
      let moveUp = 0;

      if (keys.has('w') || keys.has('arrowup')) moveForward += 1;
      if (keys.has('s') || keys.has('arrowdown')) moveForward -= 1;
      if (keys.has('d') || keys.has('arrowright')) moveRight += 1;
      if (keys.has('a') || keys.has('arrowleft')) moveRight -= 1;
      if (keys.has(' ')) moveUp += 1;
      if (keys.has('shift')) moveUp -= 1;

      if (moveForward !== 0 || moveRight !== 0 || moveUp !== 0) {
        const forwardX = Math.sin(cameraYaw);
        const forwardZ = Math.cos(cameraYaw);
        const rightX = Math.cos(cameraYaw);
        const rightZ = -Math.sin(cameraYaw);

        setMumble((prev) => {
          const nx = Math.max(
            -65,
            Math.min(65, prev.avatarPosition.x + (forwardX * moveForward + rightX * moveRight) * moveSpeed * dt)
          );
          const nz = Math.max(
            -65,
            Math.min(65, prev.avatarPosition.z + (forwardZ * moveForward + rightZ * moveRight) * moveSpeed * dt)
          );
          // Smooth terrain height approximation + manual vertical flight
          const terrainBaseY = 8.0 + Math.sin(nx * 0.04) * 5.5 + Math.cos(nz * 0.04) * 4.5;
          const ny = Math.max(2.0, Math.min(35.0, prev.avatarPosition.y + moveUp * moveSpeed * 0.6 * dt));
          const finalY = moveUp !== 0 ? ny : prev.avatarPosition.y * 0.92 + terrainBaseY * 0.08;

          return {
            ...prev,
            uiTick: prev.uiTick + 1,
            avatarPosition: { x: nx, y: finalY, z: nz },
            avatarFront: { x: forwardX, y: 0, z: forwardZ },
          };
        });
      }

      const canvas = canvasRef.current;
      if (!canvas) {
        animationFrameId = requestAnimationFrame(renderLoop);
        return;
      }
      const ctx = canvas.getContext('2d');
      if (!ctx) {
        animationFrameId = requestAnimationFrame(renderLoop);
        return;
      }

      const width = canvas.width;
      const height = canvas.height;

      // Clear background with deep obsidian spatial gradient
      const bgGrad = ctx.createLinearGradient(0, 0, 0, height);
      bgGrad.addColorStop(0, '#070B12');
      bgGrad.addColorStop(0.55, '#0B111E');
      bgGrad.addColorStop(1, '#0F172A');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      // Compute camera position orbiting avatarPosition
      const target = mumble.avatarPosition;
      const camX = target.x - Math.sin(cameraYaw) * Math.cos(cameraPitch) * cameraDistance;
      const camY = target.y + Math.sin(cameraPitch) * cameraDistance;
      const camZ = target.z - Math.cos(cameraYaw) * Math.cos(cameraPitch) * cameraDistance;

      // Camera basis vectors
      const forward = {
        x: Math.sin(cameraYaw) * Math.cos(cameraPitch),
        y: -Math.sin(cameraPitch),
        z: Math.cos(cameraYaw) * Math.cos(cameraPitch),
      };
      const right = {
        x: Math.cos(cameraYaw),
        y: 0,
        z: -Math.sin(cameraYaw),
      };
      const up = {
        x: Math.sin(cameraYaw) * Math.sin(cameraPitch),
        y: Math.cos(cameraPitch),
        z: Math.cos(cameraYaw) * Math.sin(cameraPitch),
      };

      const fovScale = (height * 0.5) / Math.tan((mumble.fov * Math.PI) / 360);

      const project3D = (pt: Vector3): { x: number; y: number; depth: number; visible: boolean } => {
        const dx = pt.x - camX;
        const dy = pt.y - camY;
        const dz = pt.z - camZ;

        const cz = dx * forward.x + dy * forward.y + dz * forward.z;
        if (cz <= 0.5) {
          return { x: 0, y: 0, depth: cz, visible: false };
        }
        const cx = dx * right.x + dy * right.y + dz * right.z;
        const cy = dx * up.x + dy * up.y + dz * up.z;

        return {
          x: width * 0.5 + (cx / cz) * fovScale,
          y: height * 0.5 - (cy / cz) * fovScale,
          depth: cz,
          visible: true,
        };
      };

      // 1. Render subtle 3D terrain elevation grid (Amnytas Bastion coordinates)
      ctx.lineWidth = 1;
      ctx.strokeStyle = 'rgba(148, 163, 184, 0.08)';
      const gridStep = 10;
      for (let gx = -60; gx <= 60; gx += gridStep) {
        ctx.beginPath();
        let started = false;
        for (let gz = -60; gz <= 60; gz += 5) {
          const gy = 4.0 + Math.sin(gx * 0.03) * 2.0 + Math.cos(gz * 0.03) * 2.0;
          const p = project3D({ x: gx, y: gy, z: gz });
          if (p.visible) {
            if (!started) {
              ctx.moveTo(p.x, p.y);
              started = true;
            } else {
              ctx.lineTo(p.x, p.y);
            }
          } else {
            started = false;
          }
        }
        ctx.stroke();
      }
      for (let gz = -60; gz <= 60; gz += gridStep) {
        ctx.beginPath();
        let started = false;
        for (let gx = -60; gx <= 60; gx += 5) {
          const gy = 4.0 + Math.sin(gx * 0.03) * 2.0 + Math.cos(gz * 0.03) * 2.0;
          const p = project3D({ x: gx, y: gy, z: gz });
          if (p.visible) {
            if (!started) {
              ctx.moveTo(p.x, p.y);
              started = true;
            } else {
              ctx.lineTo(p.x, p.y);
            }
          } else {
            started = false;
          }
        }
        ctx.stroke();
      }

      // 2. Render 3D Animated Trail Ribbons (.trl binary segments)
      for (const trail of trails) {
        if (!isCategoryEnabled(categories, trail.type)) continue;
        if (trail.mountFilter && trail.mountFilter.length > 0) {
          if (!trail.mountFilter.some((mt) => mt.toLowerCase() === mumble.mount.toLowerCase())) continue;
        }

        for (const segment of trail.segments) {
          if (segment.length < 2) continue;

          // Draw outer glow ribbon
          ctx.save();
          ctx.lineCap = 'round';
          ctx.lineJoin = 'round';

          for (let i = 0; i < segment.length - 1; i++) {
            const p1 = segment[i];
            const p2 = segment[i + 1];
            const proj1 = project3D(p1);
            const proj2 = project3D(p2);
            if (!proj1.visible || !proj2.visible) continue;

            // Compute distance fade & spherical player cutout
            const mid = {
              x: (p1.x + p2.x) * 0.5,
              y: (p1.y + p2.y) * 0.5,
              z: (p1.z + p2.z) * 0.5,
            };
            const distToPlayer = Math.hypot(
              mid.x - mumble.avatarPosition.x,
              mid.y - mumble.avatarPosition.y,
              mid.z - mumble.avatarPosition.z
            );
            const distToCam = proj1.depth;

            let fadeFactor = 1.0;
            if (trail.canFade && distToCam > trail.fadeNear) {
              fadeFactor = Math.max(
                0,
                1 - (distToCam - trail.fadeNear) / Math.max(1, trail.fadeFar - trail.fadeNear)
              );
            }
            if (playerCutoutEnabled && distToPlayer < 6.5) {
              fadeFactor *= Math.max(0.12, (distToPlayer - 1.5) / 5.0);
            }

            if (fadeFactor <= 0.02) continue;

            const ribbonWidth = Math.max(2.5, (220 / proj1.depth) * trail.trailScale);

            // Base ribbon stroke
            ctx.strokeStyle = trail.color;
            ctx.globalAlpha = Math.max(0, Math.min(1, trail.alpha * fadeFactor * 0.35 * trailOpacity));
            ctx.lineWidth = ribbonWidth;
            ctx.beginPath();
            ctx.moveTo(proj1.x, proj1.y);
            ctx.lineTo(proj2.x, proj2.y);
            ctx.stroke();

            // Animated UV flow chevrons along the segment
            ctx.globalAlpha = Math.max(0, Math.min(1, trail.alpha * fadeFactor * 0.95 * trailOpacity));
            ctx.lineWidth = Math.max(1.5, ribbonWidth * 0.45);
            ctx.setLineDash([12, 10]);
            ctx.lineDashOffset = -nowSec * trail.animSpeed * 28;
            ctx.beginPath();
            ctx.moveTo(proj1.x, proj1.y);
            ctx.lineTo(proj2.x, proj2.y);
            ctx.stroke();
            ctx.setLineDash([]);
          }
          ctx.restore();
        }
      }

      // 3. Render Player Avatar position indicator + Trigger Range ring
      const avatarProj = project3D(mumble.avatarPosition);
      if (avatarProj.visible) {
        ctx.save();
        // Draw ground trigger circle
        ctx.beginPath();
        for (let a = 0; a <= Math.PI * 2 + 0.1; a += 0.25) {
          const rx = mumble.avatarPosition.x + Math.cos(a) * 8.5;
          const rz = mumble.avatarPosition.z + Math.sin(a) * 8.5;
          const rp = project3D({ x: rx, y: mumble.avatarPosition.y - 0.5, z: rz });
          if (rp.visible) {
            if (a === 0) ctx.moveTo(rp.x, rp.y);
            else ctx.lineTo(rp.x, rp.y);
          }
        }
        ctx.strokeStyle = 'rgba(56, 189, 248, 0.28)';
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Draw Player Avatar Core Beacon
        ctx.fillStyle = '#38BDF8';
        ctx.beginPath();
        ctx.arc(avatarProj.x, avatarProj.y, 7, 0, Math.PI * 2);
        ctx.fill();
        ctx.lineWidth = 2;
        ctx.strokeStyle = '#F8FAFC';
        ctx.stroke();

        ctx.font = '600 11px "Plus Jakarta Sans", sans-serif';
        ctx.fillStyle = '#E2E8F0';
        ctx.textAlign = 'center';
        ctx.fillText(`${mumble.characterName} (${mumble.mount})`, avatarProj.x, avatarProj.y - 13);
        ctx.restore();
      }

      // 4. Render 3D Billboard POI Markers (sorted back-to-front)
      let closestTriggerable: PathingMarker | null = null;
      let closestTriggerDist = Infinity;

      const projectedMarkers = markers
        .map((m) => {
          const distToPlayer = Math.hypot(
            m.position.x - mumble.avatarPosition.x,
            m.position.y - mumble.avatarPosition.y,
            m.position.z - mumble.avatarPosition.z
          );

          // Compute bh-bounce vertical offset
          let bounceY = 0;
          if (m.bounce && distToPlayer < (m.triggerRange || 8) * 2.2) {
            if (m.bounce.behavior === 'bounce') {
              bounceY =
                Math.abs(Math.sin((nowSec * Math.PI) / Math.max(0.3, m.bounce.duration))) *
                m.bounce.height;
            } else {
              bounceY = m.bounce.height * 0.75;
            }
          }

          // Compute Lua script-tick dynamic scale pulse on Kryptis Rift
          let luaScale = 1.0;
          if (m.scriptTick === 'pulseRiftScale' && distToPlayer < 28.0) {
            luaScale = 1.0 + Math.sin(nowSec * 4.5) * 0.25;
          }

          const worldPos = {
            x: m.position.x,
            y: m.position.y + m.heightOffset + bounceY,
            z: m.position.z,
          };

          const proj = project3D(worldPos);
          const visibleInWorld = isMarkerVisible(m, nowSec);

          if (visibleInWorld && distToPlayer <= (m.triggerRange || 8.5)) {
            if (distToPlayer < closestTriggerDist) {
              closestTriggerDist = distToPlayer;
              closestTriggerable = m;
            }
          }

          return {
            marker: m,
            proj,
            distToPlayer,
            luaScale,
            visibleInWorld,
          };
        })
        .filter((item) => item.proj.visible && item.visibleInWorld)
        .sort((a, b) => b.proj.depth - a.proj.depth);

      setFocusedMarker(closestTriggerable);

      for (const item of projectedMarkers) {
        const { marker, proj, distToPlayer, luaScale } = item;
        let alpha = marker.alpha;
        if (marker.canFade && proj.depth > marker.fadeNear) {
          alpha *= Math.max(
            0,
            1 - (proj.depth - marker.fadeNear) / Math.max(1, marker.fadeFar - marker.fadeNear)
          );
        }
        if (playerCutoutEnabled && distToPlayer < 3.5) {
          alpha *= Math.max(0.25, distToPlayer / 3.5);
        }
        if (alpha <= 0.02) continue;

        const radius = Math.max(9, Math.min(28, (380 / proj.depth) * marker.iconSize * luaScale));
        const isFocused = Boolean(closestTriggerable && (closestTriggerable as PathingMarker).id === marker.id);

        ctx.save();
        ctx.globalAlpha = alpha;

        // Outer halo if within triggerRange
        if (isFocused) {
          ctx.beginPath();
          ctx.arc(proj.x, proj.y, radius + 6, 0, Math.PI * 2);
          ctx.strokeStyle = '#F59E0B';
          ctx.lineWidth = 2;
          ctx.stroke();
        }

        // Diamond / Hexagonal GW2 tactical marker billboard
        ctx.beginPath();
        ctx.moveTo(proj.x, proj.y - radius);
        ctx.lineTo(proj.x + radius * 0.88, proj.y);
        ctx.lineTo(proj.x, proj.y + radius);
        ctx.lineTo(proj.x - radius * 0.88, proj.y);
        ctx.closePath();

        ctx.fillStyle = marker.color || '#38BDF8';
        ctx.fill();
        ctx.lineWidth = 1.75;
        ctx.strokeStyle = '#0B0F19';
        ctx.stroke();

        // Inner tactical core dot
        ctx.beginPath();
        ctx.arc(proj.x, proj.y, radius * 0.3, 0, Math.PI * 2);
        ctx.fillStyle = '#FFFFFF';
        ctx.fill();

        // Distance label in tabular monospace
        ctx.font = '500 10px "JetBrains Mono", monospace';
        ctx.fillStyle = '#F8FAFC';
        ctx.textAlign = 'center';
        ctx.fillText(`${distToPlayer.toFixed(1)}m`, proj.x, proj.y + radius + 12);

        ctx.restore();
      }

      // 5. Render 2D Minimap Compass Overlay (Bottom Right) or Fullscreen Map (M)
      if (showMinimap || mumble.isMapOpen) {
        const mapW = mumble.isMapOpen ? Math.min(width - 60, 480) : 176;
        const mapH = mumble.isMapOpen ? Math.min(height - 60, 380) : 176;
        const mapX = mumble.isMapOpen ? (width - mapW) * 0.5 : width - mapW - 16;
        const mapY = mumble.isMapOpen ? (height - mapH) * 0.5 : height - mapH - 16;

        ctx.save();
        ctx.fillStyle = 'rgba(11, 15, 25, 0.88)';
        ctx.strokeStyle = 'rgba(148, 163, 184, 0.25)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(mapX, mapY, mapW, mapH, 6);
        ctx.fill();
        ctx.stroke();

        ctx.beginPath();
        ctx.rect(mapX, mapY, mapW, mapH);
        ctx.clip();

        const mapScale = mumble.isMapOpen ? 3.2 : 1.45;
        const toMapXY = (wx: number, wz: number) => ({
          x: mapX + mapW * 0.5 + (wx - mumble.avatarPosition.x) * mapScale,
          y: mapY + mapH * 0.5 - (wz - mumble.avatarPosition.z) * mapScale,
        });

        // Draw trails on compass
        for (const trail of trails) {
          if (!isCategoryEnabled(categories, trail.type)) continue;
          ctx.strokeStyle = trail.color;
          ctx.lineWidth = 2;
          for (const seg of trail.segments) {
            ctx.beginPath();
            seg.forEach((pt, idx) => {
              const mp = toMapXY(pt.x, pt.z);
              if (idx === 0) ctx.moveTo(mp.x, mp.y);
              else ctx.lineTo(mp.x, mp.y);
            });
            ctx.stroke();
          }
        }

        // Draw markers on compass
        for (const m of markers) {
          if (!isMarkerVisible(m, nowSec)) continue;
          const mp = toMapXY(m.position.x, m.position.z);
          ctx.fillStyle = m.color || '#38BDF8';
          ctx.beginPath();
          ctx.arc(mp.x, mp.y, 4, 0, Math.PI * 2);
          ctx.fill();
        }

        // Draw player arrow in center of compass
        const center = { x: mapX + mapW * 0.5, y: mapY + mapH * 0.5 };
        ctx.fillStyle = '#38BDF8';
        ctx.beginPath();
        ctx.arc(center.x, center.y, 4.5, 0, Math.PI * 2);
        ctx.fill();

        ctx.font = '600 10px "JetBrains Mono", monospace';
        ctx.fillStyle = '#94A3B8';
        ctx.textAlign = 'left';
        ctx.fillText(
          mumble.isMapOpen
            ? `WORLD MAP [M] // MAP ID ${mumble.mapId}`
            : `COMPASS // ${mumble.mapId}`,
          mapX + 8,
          mapY + 15
        );
        ctx.restore();
      }

      animationFrameId = requestAnimationFrame(renderLoop);
    };

    animationFrameId = requestAnimationFrame(renderLoop);
    return () => cancelAnimationFrame(animationFrameId);
  }, [
    mumble,
    categories,
    markers,
    trails,
    cameraYaw,
    cameraPitch,
    cameraDistance,
    playerCutoutEnabled,
    showMinimap,
    trailOpacity,
    setMumble,
  ]);

  const handleMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    setIsDragging(true);
    dragStartRef.current = {
      x: e.clientX,
      y: e.clientY,
      yaw: cameraYaw,
      pitch: cameraPitch,
    };
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!isDragging) return;
    const dx = e.clientX - dragStartRef.current.x;
    const dy = e.clientY - dragStartRef.current.y;
    setCameraYaw(dragStartRef.current.yaw + dx * 0.007);
    setCameraPitch(Math.max(0.08, Math.min(1.35, dragStartRef.current.pitch + dy * 0.007)));
  };

  const handleMouseUp = () => {
    setIsDragging(false);
  };

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    setCameraDistance((prev) => Math.max(14, Math.min(85, prev + e.deltaY * 0.04)));
  };

  return (
    <div className="relative w-full h-[580px] bg-[#070B12] border border-slate-800 rounded-lg overflow-hidden select-none">
      {/* 3D D3D11 Simulation Viewport Canvas */}
      <canvas
        ref={canvasRef}
        width={1080}
        height={580}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
        onWheel={handleWheel}
        className="w-full h-full cursor-grab active:cursor-grabbing block"
      />

      {/* Top-Left Raidcore Nexus Quick-Access Bar Simulation */}
      <div className="absolute top-3 left-3 z-10 flex items-center gap-2 bg-slate-950/85 backdrop-blur-md border border-slate-800 px-3 py-1.5 rounded-md">
        <span className="text-xs font-semibold tracking-tight text-amber-400">
          RAIDCORE NEXUS
        </span>
        <span className="text-slate-600">·</span>
        <button
          onClick={() => setShowImGuiWindow((v) => !v)}
          className={`flex items-center gap-1.5 px-2.5 py-1 text-xs font-medium rounded transition-colors whitespace-nowrap ${
            showImGuiWindow
              ? 'bg-sky-500/20 text-sky-300 border border-sky-500/40'
              : 'text-slate-300 hover:text-white hover:bg-slate-800'
          }`}
        >
          <Compass className="w-3.5 h-3.5" />
          <span>NexusPathing (ImGui)</span>
        </button>
        <button
          onClick={() => setPlayerCutoutEnabled((v) => !v)}
          className={`px-2.5 py-1 text-xs font-medium rounded transition-colors whitespace-nowrap ${
            playerCutoutEnabled
              ? 'bg-slate-800 text-slate-200'
              : 'text-slate-400 hover:text-slate-200'
          }`}
          title="Toggle HLSL spherical alpha cutout around player character"
        >
          Cutout Shader: {playerCutoutEnabled ? 'On' : 'Off'}
        </button>
        <button
          onClick={() => setShowMinimap((v) => !v)}
          className="px-2.5 py-1 text-xs font-medium text-slate-300 hover:text-white bg-slate-900 hover:bg-slate-800 rounded transition-colors whitespace-nowrap"
        >
          Compass: {showMinimap ? 'Visible' : 'Hidden'}
        </button>
      </div>

      {/* Top-Right MumbleLink Live Coordinates Readout */}
      <div className="absolute top-3 right-3 z-10 bg-slate-950/85 backdrop-blur-md border border-slate-800 px-3 py-1.5 rounded-md font-mono text-xs text-slate-300 flex items-center gap-3 tabular-nums">
        <span>
          POS ({mumble.avatarPosition.x.toFixed(1)}, {mumble.avatarPosition.y.toFixed(1)},{' '}
          {mumble.avatarPosition.z.toFixed(1)})
        </span>
        <span className="text-slate-600">·</span>
        <span>MAP {mumble.mapId}</span>
        <span className="text-slate-600">·</span>
        <span className="text-sky-400">{mumble.mount}</span>
      </div>

      {/* Simulated Dear ImGui In-Game NexusPathing Window */}
      {showImGuiWindow && (
        <div className="absolute top-14 left-3 z-10 w-84 bg-[#111622]/95 backdrop-blur-md border border-slate-700/80 rounded-md shadow-xl text-xs text-slate-200 overflow-hidden">
          <div className="flex items-center justify-between px-3 py-2 bg-slate-900 border-b border-slate-800">
            <div className="flex items-center gap-2 font-semibold text-slate-100">
              <Layers className="w-3.5 h-3.5 text-sky-400" />
              <span>NexusPathing v3.1.0 // In-Game Addon UI</span>
            </div>
            <button
              onClick={() => setShowImGuiWindow(false)}
              className="text-slate-400 hover:text-white px-1 text-xs"
            >
              ✕
            </button>
          </div>

          {/* Quick Access Tab Bar */}
          <div className="flex border-b border-slate-800 bg-[#0E131E]">
            <button
              onClick={() => setSimTab('visuals')}
              className={`flex-1 py-1.5 px-3 text-[11px] font-medium transition-colors ${
                simTab === 'visuals'
                  ? 'bg-[#111622] text-sky-400 border-b-2 border-sky-400'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Visuals & Trails
            </button>
            <button
              onClick={() => setSimTab('categories')}
              className={`flex-1 py-1.5 px-3 text-[11px] font-medium transition-colors ${
                simTab === 'categories'
                  ? 'bg-[#111622] text-sky-400 border-b-2 border-sky-400'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              Categories & Search
            </button>
          </div>

          {simTab === 'visuals' ? (
            <div className="p-3 space-y-3 max-h-72 overflow-y-auto">
              <div className="space-y-1.5">
                <div className="flex items-center justify-between text-[11px]">
                  <span className="font-semibold text-sky-300">Trail Transparency / Opacity</span>
                  <span className="font-mono text-slate-400">
                    Opacity: {Math.round(trailOpacity * 100)}% (Trans: {Math.round((1 - trailOpacity) * 100)}%)
                  </span>
                </div>
                <input
                  type="range"
                  min="0"
                  max="1"
                  step="0.05"
                  value={trailOpacity}
                  onChange={(e) => setTrailOpacity(parseFloat(e.target.value))}
                  className="w-full accent-sky-500 h-1.5 bg-slate-800 rounded-lg cursor-pointer"
                />
                <div className="flex items-center gap-1 pt-1">
                  {[1.0, 0.8, 0.6, 0.4, 0.2].map((val) => (
                    <button
                      key={val}
                      onClick={() => setTrailOpacity(val)}
                      className={`flex-1 py-0.5 text-[10px] rounded transition-colors ${
                        Math.abs(trailOpacity - val) < 0.05
                          ? 'bg-sky-500 text-slate-950 font-bold'
                          : 'bg-slate-800 text-slate-300 hover:bg-slate-700'
                      }`}
                    >
                      {Math.round(val * 100)}%
                    </button>
                  ))}
                </div>
              </div>

              <div className="pt-2 border-t border-slate-800/80 space-y-2">
                <div className="flex items-center justify-between">
                  <span className="text-[11px] text-slate-300">Player Cutout Shader:</span>
                  <button
                    onClick={() => setPlayerCutoutEnabled((v) => !v)}
                    className={`px-2 py-0.5 rounded text-[11px] font-medium transition-colors ${
                      playerCutoutEnabled
                        ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    {playerCutoutEnabled ? 'Enabled (3.5m)' : 'Disabled'}
                  </button>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-[11px] text-slate-300">Minimap Compass:</span>
                  <button
                    onClick={() => setShowMinimap((v) => !v)}
                    className={`px-2 py-0.5 rounded text-[11px] font-medium transition-colors ${
                      showMinimap
                        ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    {showMinimap ? 'Visible' : 'Hidden'}
                  </button>
                </div>
              </div>
            </div>
          ) : (
            <div className="p-3 max-h-72 overflow-y-auto space-y-2">
              <div className="flex items-center gap-1.5 pb-2 border-b border-slate-800">
                <input
                  type="text"
                  placeholder="Filter categories..."
                  value={categorySearch}
                  onChange={(e) => setCategorySearch(e.target.value)}
                  className="flex-1 bg-slate-900 border border-slate-700 px-2 py-1 text-[11px] rounded text-slate-200 placeholder:text-slate-500 focus:outline-none focus:border-sky-500"
                />
                {categorySearch && (
                  <button
                    onClick={() => setCategorySearch('')}
                    className="px-1.5 py-1 text-[10px] bg-slate-800 hover:bg-slate-700 text-slate-300 rounded"
                  >
                    ✕
                  </button>
                )}
              </div>

              {categories
                .filter(
                  (c) =>
                    !categorySearch ||
                    c.displayName.toLowerCase().includes(categorySearch.toLowerCase()) ||
                    c.fullPath.toLowerCase().includes(categorySearch.toLowerCase())
                )
                .map((rootCat) => (
                  <div key={rootCat.fullPath} className="space-y-1.5">
                    <label className="flex items-center gap-2 cursor-pointer font-semibold text-amber-300 hover:text-amber-200">
                      <input
                        type="checkbox"
                        checked={rootCat.enabled}
                        onChange={() => onToggleCategory(rootCat.fullPath)}
                        className="rounded border-slate-600 bg-slate-800 text-sky-500 focus:ring-0"
                      />
                      <span className="truncate">{rootCat.displayName}</span>
                    </label>

                    <div className="pl-4 space-y-1.5 border-l border-slate-800">
                      {rootCat.children.map((subCat) => (
                        <div key={subCat.fullPath} className="space-y-1">
                          <label className="flex items-center gap-2 cursor-pointer text-slate-200 hover:text-white font-medium">
                            <input
                              type="checkbox"
                              checked={subCat.enabled}
                              onChange={() => onToggleCategory(subCat.fullPath)}
                              className="rounded border-slate-600 bg-slate-800 text-sky-500 focus:ring-0"
                            />
                            <span className="truncate">{subCat.displayName}</span>
                          </label>

                          {subCat.children.length > 0 && (
                            <div className="pl-4 space-y-1 border-l border-slate-800/80">
                              {subCat.children.map((leaf) => (
                                <label
                                  key={leaf.fullPath}
                                  className="flex items-center justify-between gap-2 cursor-pointer text-slate-300 hover:text-white py-0.5"
                                >
                                  <div className="flex items-center gap-2 truncate">
                                    <input
                                      type="checkbox"
                                      checked={leaf.enabled}
                                      onChange={() => onToggleCategory(leaf.fullPath)}
                                      className="rounded border-slate-600 bg-slate-800 text-sky-500 focus:ring-0"
                                    />
                                    <span className="truncate">{leaf.displayName}</span>
                                  </div>
                                  {leaf.color && (
                                    <span
                                      className="w-2.5 h-2.5 rounded-full shrink-0"
                                      style={{ backgroundColor: leaf.color }}
                                    />
                                  )}
                                </label>
                              ))}
                            </div>
                          )}
                        </div>
                      ))}
                    </div>
                  </div>
                ))}
            </div>
          )}

          <div className="px-3 py-2 bg-slate-900/90 border-t border-slate-800 flex items-center justify-between">
            <span className="text-[11px] text-slate-400 font-mono tabular-nums">
              {markers.length} POIs · {trails.length} Trails
            </span>
            <button
              onClick={onResetAllBehaviors}
              className="px-2 py-1 text-[11px] font-medium bg-slate-800 hover:bg-slate-700 text-slate-200 rounded transition-colors whitespace-nowrap"
            >
              Reset Triggers
            </button>
          </div>
        </div>
      )}

      {/* Blish HUD In-Range Interactive Prompt ([F] Interact / Copy / Trigger) */}
      {focusedMarker && (
        <div className="absolute bottom-14 left-1/2 -translate-x-1/2 z-10 bg-slate-950/90 backdrop-blur-md border border-amber-500/50 px-4 py-2.5 rounded-lg shadow-2xl flex items-center gap-3 max-w-md">
          <div className="flex flex-col">
            <div className="text-xs font-semibold text-amber-300 flex items-center gap-1.5">
              <span>{focusedMarker.infoText || focusedMarker.type}</span>
            </div>
            <div className="text-[11px] text-slate-400 font-mono tabular-nums mt-0.5">
              GUID: {focusedMarker.guid.slice(0, 14)} · Behavior: {focusedMarker.behavior ?? 0}
              {focusedMarker.copyValue ? ` · Copy: ${focusedMarker.copyValue}` : ''}
            </div>
          </div>
          <button
            onClick={() => onTriggerMarker(focusedMarker.id)}
            className="px-3 py-1.5 bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-xs rounded transition-colors whitespace-nowrap shrink-0"
          >
            Press [F] Interact
          </button>
        </div>
      )}

      {/* Clipboard Toast Notification */}
      {clipboardToast && (
        <div className="absolute top-14 right-3 z-20 bg-emerald-950/90 border border-emerald-500/50 text-emerald-200 px-3 py-2 rounded-md text-xs flex items-center gap-2 shadow-lg">
          <Copy className="w-3.5 h-3.5 text-emerald-400 shrink-0" />
          <span>{clipboardToast}</span>
        </div>
      )}

      {/* Bottom Controls Legend & Lua Output Strip */}
      <div className="absolute bottom-3 left-3 z-10 flex items-center gap-3 bg-slate-950/85 backdrop-blur-md border border-slate-800 px-3 py-1.5 rounded-md text-xs text-slate-300">
        <span className="flex items-center gap-1.5 font-medium text-slate-200">
          <Keyboard className="w-3.5 h-3.5 text-sky-400" />
          <span>WASD: Move Avatar</span>
        </span>
        <span className="text-slate-600">·</span>
        <span>Drag: Orbit Camera</span>
        <span className="text-slate-600">·</span>
        <span>[F]: Trigger Marker</span>
        <span className="text-slate-600">·</span>
        <span>[M]: Toggle Map</span>
        {luaLogs.length > 0 && (
          <>
            <span className="text-slate-600">·</span>
            <span className="font-mono text-[11px] text-emerald-400 truncate max-w-xs">
              Lua: {luaLogs[luaLogs.length - 1]}
            </span>
          </>
        )}
      </div>
    </div>
  );
};
