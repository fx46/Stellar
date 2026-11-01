> Copied from the Godot project (`fx46/StellarGodot`, `NewGodotProject/docs/SCALE_STUDY.md`, commit a0d7cc1) for reference; its screenshots live in that repository.

# Planet scale study (milestone 0)

An isolated scene for choosing the home planet's radius from the ground, as the
development plan's milestone 0 asks: one huge procedural planet, a site on open plain
facing a mountain range 15-42 km away, and stand-ins at known sizes. It does not touch
the game; the voxel bodies, saves and ship are unchanged. The radius chosen from it is
**1000 km** (development plan, product decision 7), now the scene's default.

```
dotnet build Stellar.csproj -c Debug -p:Optimize=true
Godot_v4.7.1-stable_mono_win64_console.exe --disable-vsync --path NewGodotProject res://scenes/ScaleStudy.tscn
```

User arguments: `--radius=<m>` (default 1000000), `--seed=<n>`, `--shots=<dir>` (take
the preset views, write PNGs, print timings and quit), `--views=0,2` (which presets),
`--background`. Controls: click to look, Esc to release, WASD to move, Space/C up/down,
Shift faster (speed follows altitude), 1-6 the preset views, F12 a screenshot. The plain
Debug build (the editor's) meshes about 3-4 times slower than the optimised one.

Stand-ins, from the camera outwards: a 1.8 m person at 7 m, a 1.4 m wheeled robot at
12 m, a 34 m drill crawler at 140 m, and a brutalist megastructure 1.6 km long and 340 m
tall at 2.6 km. Presets: eye height, 150 m, 2 km and 30 km up, orbit, and eye height
looking back.

## How it works

**Terrain** (`scripts/Scale/ScaleTerrainField.cs`): radius plus a height in metres per
direction, from layers authored at fixed metre sizes - 70 km basins flattened into plain,
450 m escarpments, ridged mountain ranges up to 3.9 km inside a 45 km range mask, hills,
and ground detail down to 19 m wavelength. A bigger planet has more of the same
landscape, not stretched landscape. Improved Perlin noise in doubles with 64-bit lattice
coordinates.

**Height bounds** (`ScaleTerrainField.HeightRange`): the height's range over every surface
point within a distance of a given one, by interval arithmetic through the same layers -
each noise octave within its value there plus or minus its slope bound times the distance,
each shaping step mapping intervals to intervals. Tight where the layers are broad compared
with the distance. The noise's slope bound (4.0) is measured, not derived: hill-climbing
its gradient from 20,000 random starts peaks at 3.31.

**Level of detail** (`PlanetOctree.cs`): an octree over a cube around the planet. Every
node is meshed with 32 cells by the voxel bodies' Transvoxel mesher, so a node twice as
far is twice as large and twice as coarse. A node splits while the camera is within twice
its size; any factor above sqrt(3) keeps face neighbours within one level (proof in the
class comment), which the transition cells need. Two tests drop nodes, with everything
inside them (`ScaleTerrain.Select`):

- **No surface**: the node's radial range against the height bounds over its footprint,
  whole, then as 8 and 64 sub-boxes (`HoldsSurface`, cached per node).
- **Below the horizon**: no ground lies below `R + MinHeight`, so that sphere hides
  everything past the distance at which a line grazing it from the camera reaches the
  node's highest possible point (`Visible`). On the ground at 1000 km this drops most of
  the planet: about 1,050 nodes are left where 3,400 were drawn without it.

**Sparse corner grids** (`ScaleTerrain.FillCorners`): within a node, 8-cell boxes and then
2-cell blocks the surface cannot reach are skipped, and only the corners of the remaining
blocks are evaluated - at the same float positions as the dense grid, so the mesh is the
same bit for bit. A node now costs about 6 ms on one thread, against 16 ms dense.

**Coordinates**: the planet frame is kept in doubles. Each node's mesh is node-local, and
its density adds the node origin in doubles, so a mesh is as precise on a 1000 km planet
(where a float resolves 62.5 mm) as on a small one. What is rendered is the planet frame
shifted to an anchor near the camera and turned so the anchor's up is +Y (the sky's up);
the anchor follows the camera every 2 km. This runs on the stock single-precision Godot
build: no engine rebuild is needed.

**Meshing** happens off the main thread: a job selects the octree cut for the camera,
meshes only the nodes that are new or whose coarser neighbours changed, and the main thread
swaps the whole cut in one frame (no holes while it updates).

Verified natively (`verification/native/ScaleTests.cs`, `dotnet run -c Release --
--scale-only`):

- at 60, 100 and 1000 km, over five camera heights, face neighbours are never two levels
  apart, no meshed node lies inside another, and every point of the terrain shell lies in
  exactly one meshed node;
- the surface test keeps every node a surface point lies in, and rejects only nodes whose
  mesh is empty;
- fine/coarse seams close (a mutation that drops the transition cells fails it);
- the noise's slope stays within its bound over 400,000 random pairs, and every sampled
  height lies within its bounds, at reaches from 1 m to 60 km;
- sparse meshes equal dense ones bit for bit (levels 0-12, random transition faces);
- ground inside nodes culled below the horizon is hidden behind the occluding sphere (a
  segment test, independent of the culling's formula);
- 1000 km out, the density is the same to 0.3 mm however a point is split into node
  origin and offset.

`dotnet run -c Release -- --scale-bench` prints the selection time and a sample of an
eye-height view's nodes meshed on one thread, sparse and dense.

## Results (2026-11-01)

![Radius comparison](screenshots/scale-study/radius-comparison.jpg)

Rows 60, 100, 250 and 500 km; columns eye height, 150 m, 2 km, 30 km, orbit.

- **From the ground, radius barely shows.** The eye-height and 150 m views are nearly
  identical at every radius: what is near is set by the terrain's relief, not curvature.
- **From the air it decides the planet's character.** At 30 km up, 60 km and 100 km
  planets read as small domes; 250 km reads as a landscape with a curved horizon; 500 km
  is nearly flat.

![1000 km views](screenshots/scale-study/r1000km-views.jpg)

The chosen 1000 km planet: eye height, 150 m, 2 km and 30 km up, orbit, eye height looking
back. From 30 km it is open country with a gentle curve.

**Cost does not depend on radius.** Optimised build, RTX 5080 / 16 threads, 1080p. Before
the height bounds and horizon culling (100 and 250 km):

| view | nodes, triangles | GPU frame | first mesh (14 threads) |
|---|---|---|---|
| eye height | 3,406-3,466, 2.9 M | 0.73 ms | 6.5-7.0 s |
| 2 km up | 1,584-1,605, 1.5-1.7 M | 0.73 ms | 1.5 s for ~640 changed nodes |
| 30 km up | 340-458, 0.5-0.8 M | 0.63-0.75 ms | 0.3 s |

With them, at 1000 km:

| view | nodes, triangles | GPU frame | meshing |
|---|---|---|---|
| eye height, from cold | 1,052, 1.9 M | 0.68 ms | 1.1 s |
| 150 m up | 921, 1.7 M | 0.94 ms | 0.1 s for 130 changed nodes |
| 2 km up | 647, 1.2 M | 0.73 ms | 0.3 s for 233 |
| 30 km up | 403, 0.7 M | 0.74 ms | 0.3 s for 222 |

Video memory 310-340 MB, managed heap 20-70 MB. GPU times are Godot's viewport
measurement.

## What it does not show yet

- **No edits.** Excavation, sparse edit storage on the big planet, eviction and save
  are the plan's next steps (milestone 0, steps 2 and 6).
- **Meshing could be faster still.** Normals are probably most of what is left (six field
  samples per vertex, scalar doubles; not yet measured apart). An analytic gradient and
  SIMD noise are the next levers.
- **The look is a placeholder.** Smooth ochre ground and dune-like mountains, a stock
  physical sky and exponential fog. Rock materials, aerial perspective and landmarks are
  step 4.
- **Distant normals** use the mesher's fixed 0.5 m gradient step, so far terrain is lit
  with sub-pixel detail.
