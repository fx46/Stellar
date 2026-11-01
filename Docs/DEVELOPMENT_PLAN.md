> Copied from the Godot project (`fx46/StellarGodot`, `NewGodotProject/docs/DEVELOPMENT_PLAN.md`, commit a0d7cc1). It still describes the Godot code; the Unreal port follows its milestones.

# Stellar development plan

Date: 2026-10-31, revised 2026-11-01 against the "gam" Milanote moodboard. Planning
document; no gameplay changes are included.

Priority update: establish the vastness of a planet before expanding factory and road
systems. World scale is the first development milestone and an art-direction gate,
not background work to postpone until logistics is complete. Decisions about terrain
storage made in that milestone must also leave room for terrain-shaped fluids and
layered geology, because the moodboard makes both central rather than late additions.

## Direction agreed in the design conversation

Build a robotic industrial civilization that progresses from hand-built foundations
to interplanetary logistics and a stellar engine around a star or black hole.

- Switch between embodied exploration and an overhead planning view.
- Assemble robots from physical parts joined at connection points. Layout, load,
  propulsion and tools should create meaningful engineering choices.
- Build foundations manually initially. Grow a network with dedicated lanes,
  intersections, signals, ramps, depots, small bots, heavy haulers and modular trains.
- Combine dynamic source/request/depot dispatch with optional deeper programming.
- Make logistics and elaborate production chains the central challenge; exploration,
  excavation and research create reasons to extend the network.
- Keep established planetary factories operating when space logistics opens.
- Preserve both the beauty of alien landscapes and the beauty of immense industry.

## Moodboard pillars

The moodboard notes restate and extend the direction. Each pillar below is a design
requirement; where the plan previously disagreed, the milestone changes are noted.

| Moodboard note | What it requires of Stellar | Plan consequence |
| --- | --- | --- |
| Giant landscapes with giant machines, realistic sci-fi | Kilometre-scale terrain and machines that are large against people and robots. | Milestone 0 (unchanged priority). |
| Infinite, fully deformable 3D world | The player never runs out of world to dig or build on; every surface can be edited. | Decided: huge bounded planets, larger than Space Engineers' 120 km-diameter worlds; see product decision 7. |
| 3D mining logistics: automated drilling machines carve tunnels, construct shafts and connect deposits | Excavation is something the factory does, not only a player tool; tunnels and shafts become network routes. | Automated excavation moves forward to milestone 3 (was milestone 5). |
| Elaborate processing chains, physical transport layouts, programmable robots, digging through challenging geology | Rock varies in hardness, structure and content; geology shapes routes and tool choice. | Geology layers enter terrain authoring in milestone 0 and drive excavation in milestone 3. |
| Satisfactory without belts: machines have wanted inputs; bots are modular, general and dynamic | Machines state demand; general-purpose bots and vehicles satisfy it through roads and depots. | Matches milestones 1–4 (local buffers, dispatch, assembled bots). |
| Fluid mechanics: lava, underground gases, groundwater and pressure become engineering challenges | Fluids exist in the deformable world and react to excavation. | A terrain-coupled fluid spike runs in milestone 0c; fluids are built in milestone 6. |
| Terrain-based machines: excavate a reservoir, sculpt a channel, build a giant underground heat exchanger | Shaped terrain is part of a machine's function, not only its foundation. | Milestone 6; the fluid spike must show a sculpted channel carrying flow. |
| Brutalist sci-fi megastructures | Late-game industry reads as monumental concrete-and-steel architecture. | Structure art family in milestone 5; the stellar engine (milestone 8) uses it at full scale. |
| Goal: build a stellar engine around a star or black hole | A final, staged construction objective. | Milestone 8 (unchanged; setting still open). |

Reference games are design inputs from the conversation, not specifications to copy.

## Current project: evidence and gaps

The active project is `NewGodotProject`, Godot 4.7 .NET / C# / Forward+. The archived
Unreal project is outside this review. Current source and README take precedence over
historical entries in IMPLEMENTATION_PROGRESS.md and PROJECT_REVIEW.md.

| Area | Implemented foundation | Gap relative to the direction |
| --- | --- | --- |
| World and excavation | Editable voxel bodies, sculpting and building with the brush, off-thread erosion edits, grading, ore, detached fragments, bodies that grow past their box and weld, moving asteroid ring, black-hole scenery | The home body is 40 u radius. No layered geology, no fluids, no automated excavation plans, and surface construction and underground infrastructure are not yet a playable factory network. |
| Player and camera | First/third person, planetary movement, jetpack, debug freecam | Debug freecam is not a production planning interface with selection, overlays and construction tools. |
| Construction | Paid placement of eight module types on a fixed ship deck; ghosts and protected access cells; free terrain building in god mode | Expandable surface foundations, ramps, bridges, roads, lanes and junctions. Terrain building is free and cannot serve as the paid construction economy unchanged. |
| Robots | 5 x 4 x 5 hull grid, facing, adjacency validation, mass/thrust speed, cargo and drill contributions; workshop, refits, templates and undo | Explicit compatible sockets, articulated assemblies, loaded handling, turning envelopes and road capability constraints. Present wheel speed is aggregate, not a wheel/traction simulation. No tracked or legged chassis. |
| Robot logic | Freeform execution graphs, loops, conditions, hardware gates, pause/step/trace; reusable programs | Generic destinations, dynamically assigned hauling contracts, fleet dispatch and traffic-aware orders. |
| Ground logistics | Receiving rack to storage intake cargo loop, obstacle-avoiding cardinal deck routing | Current destinations and driving are ship-specific. No expandable lane graph, traffic reservations, signals or general depot economy. |
| Production | Engine-independent recipes, refinery/crusher/assembler, autocrafting, stock targets, shared power | Machines consume/produce through shared ship storage. Spatial logistics cannot constrain production until machines have local buffers. Existing crafting-job buffers are not physical freight routes. |
| Research | Analyzer, collected alloy/data samples, five research nodes | Manufactured research supplies and sustained production demands; exploration samples should complement these. |
| Space | Authored ship orbits/transfers, surveys, fuel/range, departure manifest | Autonomous freight, persistent distributed settlements, offscreen production, planetary specialization and stellar-engine construction. |
| Persistence and verification | Versioned validated saves, backups, background serialization, native tests and opt-in engine harness | Every new factory/network/assembly system needs stable identities, migration and live restore coverage. |

Key source anchors:

- `scripts/Voxel/VoxelBody.cs`, `VoxelWorld.cs`, `BodyGrid.cs`: body grid, chunk slots,
  paged per-slot tables, growth and welds (see `docs/TERRAIN_ARCHITECTURE.md`).
- `scripts/Game/ShipSim.cs`: `Tick` currently accepts one shared `Inventory`.
- `scripts/Game/BuildController.cs`: placement intersects the ship deck plane.
- `scripts/Game/DeckNavigation.cs`: cardinal breadth-first floor routing.
- `scripts/Game/Drone.cs`: `DoWalkTo` uses the ship's fixed receiving/storage/bay stations.
- `scripts/Game/DroneHull.cs`: grid, adjacency, part capabilities and derived performance.
- `scripts/Game/FlowBlueprint.cs`, `DroneProgram.cs`: reusable graph/compiler/VM base.
- `scripts/Game/TechTree.cs`, `Recipes.cs`, `Crafting.cs`: progression and production base.
- `scripts/Game/Save*.cs`, `verification/native`: persistence and verification base.

## Moodboard comparison and art direction

The moodboard holds 26 images, a link to the "SECOND SKIN" video and the notes above.
The images share industrial weight, readable mechanisms, dramatic scale, atmospheric
depth and small lights against large dark forms, with a smaller hopeful register of
robots transforming bright landscapes. They fall into seven groups.

| Group (board images) | Observed qualities | Requirement for Stellar |
| --- | --- | --- |
| Heavy surface machines (1–3, 19) | Drill crawler, tracked yellow fortress, wheeled tanker, rusted walking factory; dust, smoke, a lone person for scale | Visible tool heads, structural frames, service panels, large wheels, tracks or legs; slow heavy motion; human/robot scale cues. Locomotion type is a visible engineering choice. |
| Rock meets industry (4, 5, 8) | Support ring cut into a vegetated cliff; an unexplained geometric machine in a dark cave; a rover convoy in a bored tunnel | Preserve the meeting of irregular rock and manufactured rings, gantries, pipes and work lights. Bored tunnels are roads; convoys use them. |
| Hopeful terraforming (6, 7) | Robots around a blue lake on a red plain, green plumes, a planet in the sky | Spacious terrain, warm daylight and repeated autonomous activity. Water shaped by terrain is part of the image. |
| Underground installations (11–14, 17, 18) | Reactor hall with a lit core; layered cutaway diagram; teal-lit cavern city; base diorama; robot arms over a cart line | A late-game machine is an inhabitable installation with access paths, cables and support. Layer transport, processing and power vertically; provide a section/underground view when this becomes playable. |
| Stylised factory games (9, 10, 15, 16) | Saturated colour, clear modular silhouettes, lit paths and colour-coded flows | Gameplay-readability references only; the game is realistic (decided 2026-11-01). Take strong silhouettes and legible flow and status from them, mainly for the planning view, not their palette or rendering style. |
| Brutalist megastructures (20–25) | Grey concrete masses, yellow industrial bands, hull-like forms with tiny windows and lights, enormous horizontal scale | Late-game structures read as monumental architecture: few large planes, repeated openings, restrained colour with one accent. This is the stellar engine's visual family. |
| Vertical scale (26) | Misty vertical rockets and towers with a figure in the foreground | Launch and orbital infrastructure seen from the ground; scale through atmosphere and a foreground person. |

The current code builds much machinery from primitive meshes and simple materials.
That is a useful functional prototype, but the reference look needs a deliberate kit
of silhouettes, joints, structure, surfaces, animation, sound and lighting. Repository
screenshots show an older open-deck layout and are not a reliable current visual
baseline; capture a fresh rendered comparison in milestone 0.

Create these comparison compositions over development. The first four are the scenes
described on the moodboard; the last two cover moodboard pillars they do not show.

1. The assembly workshop: a robot chassis surrounded by wheels, joints, batteries, cargo
   modules and tools, with visible attachment points.
2. The logistics interchange: small bots moving between depots beside dedicated freight
   lanes, with a heavy modular convoy climbing a broad ramp.
3. The excavation frontier: finished roads extending into raw caverns, carrying
   construction materials toward a huge drilling machine.
4. The planetary freight terminal: surface traffic feeding launch infrastructure, with
   orbital construction visible overhead.
5. The terrain machine: a sculpted reservoir or channel feeding an underground heat
   exchanger, with fluid visibly following the excavated shape.
6. The megastructure: a brutalist industrial mass at landscape scale, seen from a
   walking astronaut with a robot and a large machine for comparison.

Use restrained material colours, selective safety markings and status lights. Reserve
bright overlays for planning. Test silhouettes from both walking and planning views.
Start the first art sample during the core prototype, rather than polishing the entire
world before logistics works or postponing all visual identity until the end.

## Recommended product decisions to prototype

These are proposals, not additional user decisions.

1. Keep the orbital ship as a workshop, expedition hub and later freight participant.
   Add a surface industrial site; do not force the whole game into the fixed cabin.
2. Separate physical foundations from lane rules. One platform can host machines,
   several lanes, pedestrian access or depot bays. Roads use explicit directed lane
   graphs, not the voxel density field as their navigation database.
3. For the first playable test, ordinary autonomous wheeled transport follows built
   roads. Test tracked and legged off-road chassis later, as the moodboard's crawlers
   and walking factory suggest. Their terrain access should have range, speed, traction
   or cost tradeoffs so roads retain value. Off-road policy is open.
4. Use predictable vehicle motion and graph reservations first. Derive engineering
   limits from mass, cargo, wheel layout and propulsion; defer a full joint/rigid-body
   vehicle simulation until a test demonstrates why it improves play.
5. Make dispatch an allocator of transport jobs and programs an executor of behavior.
   Both must use the same cargo reservations, routes and traffic permissions.
6. Carry forward the existing black-hole setting provisionally. Star versus black hole,
   and the final engine's purpose, remain open art/progression decisions. Existing
   black-hole rendering does not implement stellar-engine gameplay.
7. Decided 2026-11-01: "infinite world" means huge bounded planets, plus further bodies
   to expand to. Planets are larger than Space Engineers' largest (120 km diameter,
   60 km radius) so megastructures and big landscapes have room. Decided after the
   milestone 0 scale study: the home planet has a **1000 km radius**, about 16 times
   Space Engineers' largest. Terrain is procedural and edits are sparse, so a larger radius should
   cost little at runtime once residency is bounded; the costs are precision, streaming
   distance and authoring terrain forms that do not repeat across the surface.
8. Decided 2026-11-01: the visual register is realistic sci-fi: realistic materials,
   lighting and atmosphere throughout. Stylised references inform readability only.
9. Make excavation a factory activity. Drilling machines execute surveyed cut plans;
   a completed tunnel, shaft or ramp registers as a lane-graph edge, and spoil is freight.
   Player digging remains for bootstrap and fine work.
10. Treat geology as data the terrain already carries: material layers with hardness,
    ore content and (later) fluid content, generated from the same terrain definition.
    Hardness and structure set drilling rate, tool wear and which machines can cut.
11. Fluids follow the edited terrain. Pipes and tanks remain useful, but reservoirs,
    channels and flooded cavities must emerge from excavated shapes. The milestone 0c
    spike chooses the representation and fidelity.

## Milestones and completion gates

Sequence by dependencies and playable outcomes. These are scope gates, not calendar
estimates; estimate work after the first surface logistics prototype is measured.

### 0. Establish planetary scale and the feeling of vastness

This is the immediate priority. The current home radius is 40 u
(`VoxelWorld.PlanetRadius`), only 80 u across before terrain variation. That supports
the current small-body prototype, but not the broad plains, distant mountain walls,
huge excavations and long industrial routes suggested by the reference images. The
target (product decision 7) is a 1000 km radius: 25,000 times the current body.

Increasing the radius alone is insufficient. Recent work reduced the cost of large
grids: per-slot grid tables are paged and allocated only where written, bodies can grow
past their box, and chunk nodes are created only where ground can be. But `VoxelBody`
still holds a dense `GridChunks`³ array of chunk slots, `VoxelWorld.SetupBody` still
visits every slot, and resident chunks are not evicted. Existing LOD reduces mesh
detail, but does not by itself make planet-sized residency affordable. Preserve the
sparse edit layer, paging and tested mesher while changing spatial residency.

Work in this order:

1. **Define scale through ground-level compositions.** Treat 1 u as approximately
   1 metre for initial scale studies, subject to checking the existing astronaut and
   machine dimensions. Compare candidates across a 60–1000 km radius range with the
   same camera, person, robot, large-machine and megastructure stand-ins. Choose radius
   from the desired horizon, curvature and route space. Radius alone does not make a
   landscape feel vast: the horizon over flat ground stays short, so relief does the
   work. Approximate line-of-sight distances (d ≈ √(2Rh)):

   | Radius | Horizon from 1.8 m eye height | 2 km peak visible from (to its base horizon) |
   | --- | --- | --- |
   | 60 km (Space Engineers' largest) | 0.47 km | 15 km |
   | 100 km | 0.60 km | 20 km |
   | 250 km | 0.95 km | 32 km |
   | 500 km | 1.34 km | 45 km |
   | **1000 km (chosen)** | 1.90 km | 63 km |
   | 6,371 km (Earth) | 4.8 km | 160 km |

   Mountain walls, escarpments and megastructures several kilometres tall therefore
   carry the "big landscape" look, and view distance must reach tens of kilometres.
   Build the isolated test planet at the candidate radius from the start; a smaller
   stand-in planet would not exercise precision, streaming or terrain-form scale.
2. **Build bounded terrain residency.** Replace the dense slot array and whole-volume
   setup for large bodies with spatially addressed active regions and a hierarchy for
   distant surfaces. Generate procedural terrain on demand, store edits sparsely, evict
   inactive render/collision resources, and retain edits on disk. Evaluate a sparse
   hierarchy or a near-voxel/far-surface representation with a small prototype before
   selecting the implementation. Any far surface must derive from the same terrain
   definition and hand off without seams or loss of excavations. Leave room in the
   per-sample or per-chunk data for material and fluid fields (see 0c).
3. **Make terrain forms and geology work at several scales.** Author broad plains and
   basins, distant ridges/escarpments, traversable valleys, local outcrops and close
   surface detail independently. Add a first material-layer model (soil, hard rock,
   ore-bearing strata) from the same definition, visible in cuts. Do not merely stretch
   the current small-body noise or scale every ore vein with the planet. Reserve broad
   industrial sites and long corridors.
4. **Establish atmospheric distance and scale cues.** Tune horizon/sky integration,
   aerial perspective, directional light, material detail and sparse landmarks. Use
   the warm frontier and cavern references as separate compositions. Keep useful
   distant terrain visible; the target is readable depth across a landscape.
5. **Validate coordinates and world systems at the new scale.** At 100 km from the body
   centre a 32-bit float resolves about 8 mm, at 500 km about 31 mm (the finest voxel
   size in use, 0.03125 u) and at 1000 km 62.5 mm; body-local float coordinates alone
   are not enough. Choose early between a double-precision Godot build (`precision=double`,
   which needs a matching .NET build) and region-local coordinates with origin
   rebasing, with a small prototype of each if needed. Keep terrain and entities
   body-local, render camera-relative, and audit shader precision, collision, sampling,
   camera clipping and far-plane/depth range, gravity and atmosphere. Planet radius, body mass, ring placement and ship distances currently
   interact; use an isolated large-world test scenario first, then deliberately
   integrate its placement and travel rules. Increasing radius must not accidentally
   break the ring simulation, body growth and welds, or local movement.
6. **Prove traversal and editable persistence.** Walk, fly and use a development
   fast-travel tool across several regions, excavate each, leave render range, return,
   and reload. Streaming priorities must follow the astronaut and inspected camera
   region without rebuilding the whole planet or forcing distant terrain into full detail.

Progress (2026-11-01): the scale study (`docs/SCALE_STUDY.md`, `scenes/ScaleStudy.tscn`)
covers a first pass of steps 1, 2 and 5 for read-only terrain. A level-of-detail octree
meshes a 60-1000 km planet with the existing Transvoxel mesher; a double-precision planet
frame with a floating, re-oriented render origin runs on stock Godot. Cost does not depend
on radius. From the ground radius barely shows; from 30 km up, 60-100 km planets read as
small domes, 250 km as a landscape, 1000 km as open country. With sparse corner grids from
rigorous height bounds and horizon culling, the 1000 km planet's eye-height view is about
1,050 nodes and 1.9 M triangles, meshed from cold in 1.1 s (optimised build, 14 threads).
Not yet done: edits, eviction and save on the big planet.

Visual gate: from astronaut height, a person-sized robot is small in the foreground,
a large machine occupies a local site, and a distant ridge tens of kilometres away
still dominates the background. Ascending reveals new terrain rather than the entire
home world almost immediately. A megastructure stand-in reads as monumental from both
nearby and the far side of a valley. A cut through the terrain shows distinct material
layers. Materials and lighting read as realistic, not stylised. Review matching
walking and elevated screenshots before proceeding.

Technical gate: travel across tens of kilometres, including the far side of the planet
by fast travel, and excavation at each stop retain collision,
terrain continuity and saved edits. Record startup time, resident chunks, memory,
streaming latency and p95/p99 frame times on named hardware. Define the active-region
budget before the trial; resident resources must stay within it as explored area grows.
No claim of planetary-scale performance is established until these trials pass.

### 0b. Establish the industrial test site on the larger world

- Capture current in-engine ship, workshop, graph, surface and lighting views.
- Create a repeatable surface test scenario with two deposits in different rock, enough
  room for a road loop, an incline and a small cavern within the validated larger
  landscape. Use the same robot/machine scale established in milestone 0.
- Add a player-facing planning camera with selection, inspect, placement and a clean
  return to the astronaut. Preserve normal simulation while switching views.
- Record frame times, memory and terrain-edit latency in this same scenario.

Gate: inspect and place a test structure from both views, switch back reliably, and
save/reload the scenario. Fresh screenshots become the visual baseline.

### 0c. Spike terrain-coupled fluids

Fluids are a moodboard pillar and depend on the terrain representation chosen in
milestone 0, so their representation is decided now and their gameplay built later.
This is a time-boxed prototype in the isolated large-world scenario, not a feature.

- Compare at least two representations against the edited voxel terrain, for example
  a cellular volume of fluid per terrain cell and a surface/heightfield layer with
  cavity volumes. Choose by stability under live excavation, memory per resident region,
  cost at rest and whether it can be simulated in aggregate when unloaded.
- Show: digging a basin that fills, sculpting a channel that carries flow to it, and
  breaching a sealed cavity that drains. Flow must stop costing CPU once settled.
- Define how fluid state persists, how it behaves at region eviction, and how pipes and
  tanks exchange with terrain fluid.
- Record the cost on the milestone 0 hardware. Lava, gases and pressure are design
  notes only at this stage; the spike uses one liquid.

Gate: a written choice of representation with measurements, and a terrain storage
format from milestone 0 that can carry it without a later migration of saved edits.

### 1. Make a surface factory depend on physical deliveries

- Extract host-independent machine simulation from ship-specific layout concerns.
  Retain the ship through an adapter; reuse recipes and production rules.
- Introduce stable factory/machine/storage/port IDs and body-local placement frames.
- Give machines local input/output buffers, declared ports and explicit transfers.
  Keep aggregate stock visibility for planning without allowing remote consumption.
- Place paid foundation panels, machines, source storage and receiving depots.
  Initially use simple snapped straight roads and one ramp type.
- Adapt a wheeled hauler to general destinations and explicit load/unload operations.
  Keep the present shipboard program importable through destination adapters.
- Adapt autocrafting to request production and hauling rather than instantly moving
  items between remote machines. Keep manual bootstrap/emergency recovery possible.

Gate: manually mine ore, build a route, assemble a hauler, deliver to a refinery and
move its output onward. A disconnected route visibly starves the machine; reconnecting
it restores production. Counts remain conserved across transfers and save/load.

### 2. Prove that designing roads is fun

- Add road drawing, snapping, curve/ramp previews and clear cost/validity feedback.
- Implement directed lanes, lane class restrictions, depot entrances, waiting bays
  and junction connectors. Validate width, clearance, grade and turn radius.
- Add occupancy/reservations, braking, junction conflict zones, entry signals and
  reserve-through behavior so a vehicle does not stop across a junction without exit space.
- Distinguish planned route, actual occupancy and temporary reservations. Road edits
  invalidate routes safely; removal beneath occupied vehicles needs defined recovery.
- Add congestion/wait-reason overlays and alerts for cycles or blocked destinations.
  Provide manual recovery and rerouting; do not promise universal deadlock prevention.
- Offer small couriers and heavy haulers whose usable lanes and ramps differ.

Gate: a crossing and depot bottleneck produce understandable queues. A player can
improve throughput by changing lanes, signals or waiting space. Too-steep/narrow routes
are rejected with explanations before dispatch, not discovered through stuck bots.

### 3. Let machines dig the network

Moved forward from the former underground milestone: the moodboard's mining logistics
has drilling machines carving tunnels, building shafts and connecting deposits.

- Turn voxel tools into surveyed excavation plans: a tunnel, incline or shaft drawn
  with the road tools, previewed against geology, with clear cost and duration.
- Build one drilling machine that executes a plan from an accessible network edge.
  Hardness sets advance rate and tool wear; some layers need a better cutting head.
- Produce spoil as freight that must be hauled away or used as fill; a full drill
  waits. Ore met along the cut becomes available at the face.
- Register a completed cut as lane-graph edges with its real grade, width and clearance,
  so traffic and validation from milestone 2 apply unchanged.
- Define what happens to roads, foundations and vehicles near an active cut. Test
  invalidation and safe stops before any structural collapse model.

Gate: a deposit behind a ridge or under hard rock is reached by a planned tunnel that
a drilling machine cuts, haulers remove the spoil, and the finished tunnel carries
ore traffic. Harder rock measurably changes the plan's time and tool cost.

### 4. Make the fleet dynamic and the machines personal

- Introduce source/request/depot policies: item filters, target stock, priority,
  available vehicle classes and dispatch thresholds.
- Reserve cargo, destination capacity and jobs to prevent duplicate assignments;
  handle depletion, cancellation, changed demand, failed routes and loading a save.
- Add graph instructions for claiming a job, routing, docking, charging and reporting
  completion. Show assignment, route, program state and wait reason together.
- Evolve hull adjacency into explicit part sockets with compatibility, orientation
  and occupied-space validation. Keep existing hulls usable through a migration adapter.
- Prototype drive/cargo/battery/tool modules, including a cutting head usable by the
  milestone 3 drill. Compute loaded mass, power demand, grade limit, footprint and turn
  envelope. Preview these before fabrication.
- Introduce couplers and modular road trains after single-vehicle reservations work.
  Reserve for the full convoy length; free a block only after the tail clears it.
  Validate articulated turns and swept clearance rather than treating a convoy as a point.

Gate: several bots serve changing requests without duplicated cargo claims; a custom
program can use the same network safely. A courier, hauler and coupled convoy create
different capacity, energy and infrastructure tradeoffs with reusable parts.

### 5. Close the first complete progression loop

- Extend existing iron/copper/silicon recipes into a compact chain that produces
  foundation materials, robot parts and one manufactured research supply.
- Require repeated delivery of manufactured supplies to an analyzer. Use exploration
  samples for discoveries or new branches alongside the sustained factory demand.
- Let research unlock concrete infrastructure capabilities: stronger drives, wider
  freight roads, better processing, construction bots or harder-rock cutting heads.
- Add blueprinted road/machine construction: player places plans; bots deliver parts
  and build from accessible network edges. Construction uses the same freight rules.
- Teach the surface loop through missions and preserve a hand-built recovery path.
- Finish one coherent art kit: road/support family, depot, courier, hauler, drill,
  refinery, work lights, sockets, markings, motion and sound. Establish the brutalist
  structure language (large planes, repeated openings, restrained colour with one
  accent) on depots and foundations so it can scale to megastructures later.

Gate: in a fresh non-quick-mode session, the player builds a network that repeatedly
produces research supplies and unlocks a useful expansion. Transport capacity matters
as much as recipe capacity. A blueprint visibly extends the network using its output.

Milestone 0 (with 0b and 0c) is the first standalone deliverable. Milestones 1–5 then
form the first full industrial playable slice. Review whether road design, excavation,
robot assembly and production are enjoyable together before expanding the resource catalog.

### 6. Build depth into the planet

- Add shafts, bridges, lifts and road/machine placement in caverns, with an underground
  planning/section view.
- Introduce terrain fluids with the representation chosen in 0c, starting with
  groundwater: excavation can breach aquifers and flood cuts, pumps and channels manage it.
- Add terrain-based machines whose function depends on shaped terrain: a reservoir
  sized by what was dug, a channel that routes flow, and a giant underground heat
  exchanger built into rock. Validate the shape and report its capacity before it runs.
- Introduce heat and cooling as the first environmental engineering challenge. Add
  lava, gases and pressure individually, only when each creates useful routing,
  processing or excavation decisions.
- Keep pipes and tanks for transport between sites; they exchange with terrain fluid
  at defined intakes and outlets.
- Define support policy for built structures when excavation removes their terrain
  or creates a detached body. Test safe suspension/invalidation before structural collapse.
- Develop the giant drill, reactor hall and underground cutaway compositions as functional builds.

Gate: an underground outpost has a meaningful resource advantage and requires a
working transport, fluid and heat solution. A terrain-shaped reservoir or exchanger does
real work whose capacity follows its excavated shape. Excavation near foundations has
explicit outcomes and cannot silently detach the factory from its saved location.

### 7. Connect persistent planetary industries

- Generalize the ship's current POI travel into freight terminals, routes, cargo
  reservations, fuel, transfer times and repeatable schedules.
- Introduce two differentiated worlds with complementary production advantages and
  geology. Avoid requiring a complete duplicate starter factory on every destination.
- Separate fixed-tick economic simulation from loaded visuals and local physics.
  Distant factories continue production, storage, fluid systems and freight scheduling
  with conserved resources. Define a bounded aggregate traffic model for unloaded sites
  and test it against an explicitly documented accuracy target; do not silently teleport freight.
- Use stable world-local frames, streaming and precision-safe coordinates. Persist
  unloaded factory state, freight in transit, route revisions and cargo ownership.
- Make return visits show the consequences of remote shortages and deliveries.

Gate: leave world A, operate on B, and return to find A's industry has advanced
consistently. A repeating freight route supplies research or construction across both.
Reloading during transit never duplicates or destroys its cargo.

### 8. Construct the stellar engine

- Decide the engine's purpose and setting before specifying its components: propulsion,
  energy capture, extraction or another explicit goal; star and black-hole versions
  need different fiction, environment and milestones.
- Build staged requirements for orbital depots, construction fleets, power collection,
  structural segments and specialized processing that depend on planetary industries.
- Make delivered materials produce visible, persistent progress in the structure.
  Provide intermediate capabilities rather than one enormous final item sink.
- Build the structure in the brutalist megastructure language established in
  milestone 5, at a scale that dominates the sky from the planetary surfaces.
- Reuse assembly connections, freight contracts, blueprints and power systems at the
  larger scale instead of introducing an unrelated endgame interaction model.

Gate: the interplanetary economy constructs and activates a functional engine stage,
with a clear final objective and visible changes at each major milestone.

## Architecture boundaries to introduce incrementally

Suggested responsibilities, not an instruction to build a framework up front:

- Factory simulation: local buffers, machine progress, recipes and power membership.
- Foundation/build data: paid structure, transform, owning body, support and occupancy.
- Road network: directed lanes, geometry, junction connections and route revisions.
- Traffic controller: occupancy, reservations, signals and convoy clearance.
- Logistics dispatcher: demand, offers, cargo ownership and transport assignments.
- Excavation planner: cut plans, geology sampling, progress, spoil and resulting lane edges.
- Terrain fluid simulation: fluid state over the terrain, settling, intakes and outlets.
- Assembly model: socket graph, parts, capabilities and vehicle envelope.
- World simulation: loaded and distant factories plus interplanetary journeys.

Keep authoritative state engine-independent where practical, following the existing
ShipSim/recipe/VM pattern. Godot nodes present state and handle local interaction.
World position alone is not identity: structures and roads need owning-body IDs and
local transforms because the current worlds move, grow, weld and can split.

Do not embed production, dispatch, excavation and traffic rules into the growing
Game.cs or Drone.cs controllers. Introduce each boundary when its milestone needs it,
with adapters for the current ship rather than a wholesale rewrite.

## First implementation backlog

1. Capture the current ground-level world and establish matching reference-inspired
   landscape compositions with human, robot and machine scale stand-ins.
2. Choose the coordinate approach for the 1000 km planet (double-precision build or
   region-local rebasing), since it constrains everything after it.
3. Prototype sparse region residency and distant terrain on an isolated larger planet,
   replacing the dense chunk-slot array and whole-volume setup; benchmark allocation,
   startup, streaming and view-distance behavior before choosing a production architecture.
4. Author broad terrain forms, material layers, atmosphere and landmarks; validate
   traversal across tens of kilometres, close excavation, region eviction/revisit, save/load and
   coordinate precision.
5. Run the terrain-coupled fluid spike (0c) against the chosen residency prototype;
   fix the terrain storage format so it can carry material and fluid data.
6. Review planetary vastness visually and against measured resource budgets. Complete
   this gate before expanding road, dispatch or production features.
7. Author the industrial test site on that world; keep the existing shipboard tutorial
   as a regression scenario. Add the planning camera alongside surface tools.
8. Define and migrate factory/machine/port identities and local buffers; verify one
   refinery cannot consume an unconnected storage inventory.
9. Add paid surface foundations, placement preview and a single straight road/ramp kit.
10. Generalize destinations and move one wheeled hauler between two surface depots.
11. Wire local input/output transfers into real machine production.
12. Save/reload the complete route, inventories and working hauler; test a blocked road.
13. Play this loop before implementing curved multi-lane junctions, drilling machines,
    dynamic dispatch or a large research tree.

## Verification and performance gates

- Retain the full native suite and game build as the baseline; add targeted tests for
  inventory conservation, reservations/cancellation, route validity, loaded vehicle
  limits, socket validation, convoy tail clearance, cut-plan-to-lane registration,
  fluid volume conservation and save migrations.
- Use the existing engine harness with scratch saves for placement, camera switching,
  terrain/body transforms, real driving, intersections, drilling and live restoration.
- For each milestone, capture walking and planning views of the same functional site.
  Judge road readability, scale, activity and material/lighting coherence together.
- Benchmark representative scenarios at 10, 50 and 200 vehicles, including congestion,
  simultaneous mining, active drilling, settling fluid and save/load. These are proposed
  test sizes, not demonstrated capacity. Establish target hardware and CPU/GPU/frame-time
  budgets in milestone 0.
- Measure routing work, allocations, reservation cost and p95/p99 frame times. Avoid
  per-frame fleet-wide path searches. Use revision-based invalidation and bounded
  scheduling; optimize further when measurements identify the limiting cost.
- Validate normal progression with Quick testing disabled; its current faster factory,
  reduced costs and mining bonuses can conceal transport and research balance problems.

## Open decisions

Decided 2026-11-01:

- "Infinite world": huge bounded planets, larger than Space Engineers' 120 km-diameter
  worlds, plus further bodies (product decision 7).
- Visual register: realistic sci-fi (product decision 8).
- Home planet radius: 1000 km (product decision 7).

Needed during milestone 0:

- Double-precision engine build versus region-local coordinates with rebasing. The scale
  study's rebasing works on the stock build at 1000 km; editing and physics are untested.

Not blocking the first slice:

- Star versus black hole, and the engine's final function.
- Whether ordinary bots can leave roads, and whether tracked and legged chassis make
  off-road travel useful.
- Exact socket vocabulary, internal chassis grid and eventual articulation fidelity.
- Road pricing/support rules and how much engineering precision construction requires.
- Fluid fidelity beyond the 0c spike: which of lava, gases and pressure earn a place.
- Visibility versus simulation accuracy for distant traffic, including deadlock policy.
- Target hardware and intended fleet size.
- What the "SECOND SKIN" video on the moodboard is a reference for; it was not reviewed.

## Review validation

Source review covered the active project, current README, historical review/progress
notes, verification instructions and existing repository screenshots. A fresh Debug
build passed with zero warnings and zero errors. The full native Release suite
completed with exit code 0 and ALL PASS. No fresh rendered playtest or GPU benchmark was performed in
this planning pass; current visual conclusions are limited to source and older captures.

The 2026-11-01 revision compared the plan against the "gam" Milanote board: its text
notes and all 26 images, viewed at board-preview resolution. The linked video was not
watched. The terrain residency description was updated against the source and
`docs/TERRAIN_ARCHITECTURE.md` as of commit 4242695; no build or tests were run for
this documentation-only revision.
