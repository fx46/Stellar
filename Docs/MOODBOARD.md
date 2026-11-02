# The "gam" moodboard: what it asks of Stellar

Review of the full board export (`canvas_gam-261102_0022.pdf`, 2026-11-02: the text notes and
all 26 images, plus the "SECOND SKIN" video's thumbnail). The board marks itself as an
important guide to follow. The export is not in the repository: the images are other
artists' work and this repository is public. They are described here instead, by their
order in the export.

## Notes on the board

| Note | Where it lives |
|---|---|
| Giant landscapes with giant machines, realistic sci-fi | Milestone 0 (scale, look); every milestone's art |
| Infinite, fully deformable 3D world | Milestone 0: 1000 km editable planet, further bodies later |
| 3D mining logistics: automated drilling machines carve tunnels, construct shafts and connect deposits | Milestone 3 |
| Elaborate processing chains, physical transport layouts, programmable robots, digging through challenging geology | Milestones 1-5 |
| Fluid mechanics: lava, underground gases, groundwater and pressure become engineering challenges | 0c spike, milestone 6 |
| Terrain-based machines: excavate a reservoir, sculpt a channel, build a giant underground heat exchanger | Milestone 6 |
| Satisfactory without belts: machines have wanted inputs; bots are modular, general and dynamic | Milestones 1-4 |
| Small independent bots weaving between depots, orderly lanes of cargo carriers, modular trains; heavy haulers need wide foundations and gentle slopes, making road design part of the logistics puzzle; foundation roads with designed, dedicated lanes | Milestone 2 |
| Robots with interchangeable modules: drills, cargo containers, wheels, **legs**, batteries, **sensors**, **manipulators**, **fabrication tools**. "Snatoms." Connection points | Milestone 4 (sockets) - the module list is wider than the plan's |
| Blueprint-type drone programming? | Ported in Godot (execution graphs); to be carried into milestone 4 |
| **Mobile factories**: entire production facilities can crawl through terrain, consuming resources as they move | Not in the plan yet (see below) |
| Progression: **surface industry** (basic metals, generators, drones) -> **deep industry** (geothermal energy, rare minerals, high-pressure environments) -> **planetary engineering** (underground transport networks, cooling systems, enormous power infrastructure) -> **endgame** (turn the planet into a stellar engine, a spacecraft or a computational megastructure) | Partly in the plan (milestones 5-8); the tiers and the endgame options are not |
| Goal: build a stellar engine around our star or black hole | Milestone 8 |
| The four compositions: assembly workshop, logistics interchange, excavation frontier, planetary freight terminal | The plan's visual comparisons |
| Brutalist sci-fi megastructures | Milestones 5 and 8 |

"Snatoms" is the magnetic molecular-model kit: parts that snap together at fixed points into
anything. Read as a direction for robot assembly: a small vocabulary of parts and sockets
from which many machines can be built, rather than a catalogue of fixed vehicles.

## The images, grouped

1. **Underground installations and cutaways** (1 power-generation hub diagram with geothermal
   access and water purification; 2 a base cut into rock in section; 3 a teal-lit cavern
   factory floor; 20 a reactor hall with a lava-red core and a figure for scale): industry as
   inhabitable layers, with access, cabling and support visible. A section view is needed
   when this becomes playable.
2. **Stylised factory games** (5, 6, 7, 18): saturated colour, flow made legible, glowing
   paths. For readability in the planning view only; the register is realistic.
3. **Heavy surface machines** (4 robot arms over a cart line with a drone; 8 a rusted walking
   factory on six legs with a smokestack; 9 a drill crawler, smoke, and a lone walker in front;
   10 a giant-wheeled tanker in a dust storm with crystals in the rubble; 11 a tracked fortress
   in ochre haze; 22 a grey crawler-city loading a convoy): these are the "giant machines" and
   the "mobile factories". They are heavy, worn and smoking. Wheels, legs and tracks are visible
   engineering choices. Tool heads are huge, and people and small vehicles give the scale.
4. **Rock meets industry** (12 a bored tunnel with a lit ring in a vegetated cliff; 13 a
   geometric machine hanging in a dark cave; 16 a rover convoy with headlights in a raw
   tunnel): the excavation frontier. Tunnels are roads; lights are small against dark rock.
5. **Hopeful terraforming** (14 legged bots venting green gas by blue pools on a red plain at
   sunset; 15 small bots around a pond under a giant planet, a domed tower behind): the
   brighter register. Water shaped by terrain, with repeated autonomous activity.
6. **Sites from above** (17 a modular base on a white-and-red plain, small bots around it):
   the planning view's subject.
7. **Vertical scale** (19 rockets and towers in mist behind a person in the foreground): launch
   infrastructure seen from the ground.
8. **Brutalist megastructures** (21 a grey block with an orange accent and logo over a forest
   valley, aircraft for scale; 23 a yellow hull over a city; 24-26 weathered hull-like masses,
   repeated openings, rows of small lights): few large planes, one accent colour, enormous
   horizontal scale.
9. **"SECOND SKIN"** (27): autonomous gantry machines straddling crop rows on rails. Robots
   working in dedicated lanes and tracks: the same idea as the board's lane notes.

## Compared with the milestone 0 build (2026-11-02)

| Board | Build before this review | Done in this pass |
|---|---|---|
| Hazy depth, pale warm desert skies, golden-hour light (9-11, 14, 22) | Clear blue sky, little near haze, one fixed afternoon sun | Dustier, warmer air; the sun can be moved ([ / ]) and the tour takes golden-hour views |
| Textured ground with rubble and rocks (9, 10, 14) | Smooth ochre, with "cracked mud" cells that were a precision artifact | Scattered stones, rocks and boulders; the ground's colour noise fixed (it read positions from half-float UVs) |
| Banded cliffs (14) | Steep slopes plain grey | Cliffs, mesas and escarpments show strata |
| Giant, detailed, worn machines; people for scale (8-11, 22) | Box stand-ins | Kitbashed stand-ins: a tracked drill crawler, a walking mobile factory, a rover, a brutalist mass with openings and an accent |

Tried and dropped: rock micro-relief from screen-space derivatives of the material's noise.
It rendered as pixel blocks at every amplitude tried, so it needs an authored detail-normal
texture instead.

Still for later art passes: smoke and dust effects, authored rock materials and detail normals,
wear and decals, vegetation for the hopeful register, the cavern lighting palette (3, 20).

## What this adds to the plan

Recorded at the end of [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md): mobile factories, the wider
module list, the four-tier progression, and the endgame alternatives.
