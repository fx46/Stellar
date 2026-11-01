# Milestone 0c: terrain-coupled fluid spike

The development plan asks for a time-boxed prototype that chooses how fluids live in the
deformable world before their gameplay is built (milestone 6): compare at least two
representations against the edited voxel terrain, show a basin that fills, a channel that
carries flow to it and a sealed cavity that drains when breached, make settled flow cost
nothing, and define persistence, eviction and the exchange with pipes and tanks. One liquid
only; lava, gases and pressure stay design notes.

## Decision

**A cellular volume of fluid on the terrain's own cells** (representation A), in bounded
regions around active sites, persisted sparsely. The 2.5D heightfield (representation B) is
kept only as a possible far-field/aggregate model.

The deciding reason is the moodboard's own examples: groundwater breached by excavation,
flooded cuts, channels into caverns and a heat exchanger built into rock. All of them need
water under overhangs and inside cavities, which a heightfield cannot hold. A also came out
cheaper, because it simulates only what moves.

## The two prototypes

Both are engine-free C++ in `Source/Stellar/Terrain/FluidVolume.*`, tested in
`Source/Stellar/Tests/Milestone0Tests.cpp` (`Stellar.Fluid.*`).

**A - cellular volume (`FFluidVolume`).** A region is a box of 0.5 m cells in a local level
frame (curvature across a few tens of metres is about a millimetre). Rock cells come from
the edited terrain density at cell centres, so water and terrain agree by construction. Each
cell holds a fill in [0, 1]. A step:

1. settles each stacked pair of open cells once, by its lower cell (the lower fills from the
   upper; an overfull cell, such as a spring's, overflows up);
2. levels water that rests on something sideways with its four neighbours, the fuller cell
   giving a tenth of the difference.

Every transfer is exact, so volume is conserved to float rounding. Only cells that changed,
and their neighbours, are visited, so a settled region has no active cells and costs nothing.
A terrain edit that reaches a region rebuilds rock only inside the edit's box and wakes that
box. Water displaced by new rock moves to the first open cell above.

The first version used W. Gallant's compressible rules (cells up to 1.02 under pressure,
upward flow when compressed). It never settled: the dump of active cells showed a
checkerboard oscillating by 0.02 per step, sustained by updating each vertical pair from both
of its cells. The incompressible variant settles exactly. Its limit is no pressure: water does
not climb the far side of a U-bend, and a sealed aquifer cannot spout above its breach. That
fidelity belongs to milestone 6, where a per-connected-body hydrostatic level (one water level
per connected pool, computed when it settles) is the natural next step.

**B - heightfield (`FFluidHeightfield`).** Virtual pipes (O'Brien and Hodgins; Mei et al.):
one water depth per column over the first rock below the region's top, with fluxes between
columns. It is smooth and cheap per column, but it sees only the topmost ground.

## Measurements

Reference machine: NVIDIA GeForce RTX 5080, 16 logical cores; Development build. Unit tests
run on one thread with no rendering.

| | A: cellular | B: heightfield |
|---|---|---|
| test region | 24 x 24 x 12 m, 55,296 cells | 24 x 24 m, 2,304 columns |
| memory | 0.5 MB | 0.11 MB |
| fill a dug basin with 20 m3, until at rest | 1,039 steps (35 s simulated), **9 ms CPU in total** | 2,309 steps (77 s simulated), 79 ms CPU in total |
| cost once settled | **0 ms**: no active cells | 0.034 ms per step: every column, every step, until something stops it |
| volume conservation | exact to 1e-4 m3 | exact to 1e-4 m3 |
| sealed cavity under the plain | holds still; drains to its breach's floor when dug into | **invisible**: the column over it reads the plain's surface |
| save size | 3.4 KB for 37 m3 (sparse cells) | dense columns |

In the game's test site (`Docs/MILESTONE_0.md`), the 114 m3 sealed pocket behind the ramp wall
behaves as specified. Breaching it from the ramp floor (`FTestSitePlan::Breach`) drains
42.8 m3, everything above the breach's floor. 31.7 m3 of that reaches the pit and tunnel 30 m
down the ramp; the rest lies in the breach and the ramp's low spots. The region still holds
114.0 m3 at the end. The spring's 160 m3 runs down the channel cut in the mound's side and
across the plain, and collects in the dug basin. Both regions then go idle.

## Storage, persistence and eviction

- **Format.** Terrain edits are bricks with a channel mask per brick (density offset;
  material, the fill that building leaves). A future per-lattice-point channel, such as
  porosity or saturation for groundwater, adds a mask bit without migrating saved edits. Fluid
  regions are saved in their own tagged save section (`FLUD`): the region's frame, then
  (cell, fill) pairs for wet cells only. Rock is not saved; it is rebuilt from the saved terrain
  on load. Saves are versioned and sectioned; unknown sections are skipped.
- **Eviction.** A region outside the active area is not stepped, so it costs nothing, and keeps
  its state. A region left mid-flow resumes where it stopped when the player returns. The
  plan's offscreen simulation (milestone 7) should first settle a region before freezing it.
  Settling is cheap: the 20 m3 basin needed 9 ms. Regions are tens of metres and created around
  sites; there is no planet-wide water grid.
- **Pipes and tanks.** These exchange with terrain fluid at defined intake and outlet cells: an
  intake removes up to its rate from the cells it covers each step (exact, like any transfer),
  and an outlet adds water the way the spring does (`FFluidSpring`). Both wake only their own
  cells, so a pipe network costs nothing while its ends are dry or full.

## What it does not do yet

No pressure, viscosity or temperature; no second liquid; no rendering beyond a translucent
surface meshed from the fills (the same Transvoxel mesher, on a 0.5 m grid, remeshed only
where water changed); no regions created automatically when the player digs near water. The
milestone 6 work starts from here.
