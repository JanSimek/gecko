# Site Plans

Every FOC map is planned on paper before anything is placed: a to-scale drawing in the game's own projection, built from a real place and from how the original games and the total conversions lay out the same kind of place. The map is then built from the plan's data, and checked against it.

## How a plan is made

1. **The real site.** `tools/real_site.py` fetches the real place's layout from OpenStreetMap (runways, streets, building footprints) and draws it to scale, north up. Where there is nothing usable on the map (a Cold War relay site), the plan follows documented examples instead.
2. **The game references.** Every comparable map in Fallout 1, Fallout 2, Sonora, Nevada, Resurrection, Yesterday and Story of the Jackal is rendered and measured by `tools/plan_refs.py`, using `tools/map_survey.py`'s numbers: play area, building count and size, the gaps between buildings, the road share, and clutter near buildings and on open ground.
3. **The plan.** `tools/site_plan.py` reads `mod/maps_src/plans/<name>.plan.json` and draws `docs/plans/<name>.png`:
   - every building's true footprint, from its stamp, with its doors in red and new doorways in orange
   - roads, zones, features, exits, the start, and the walking routes the player is meant to take
   - when the map starts from an existing level, that level drawn underneath, with its real rooms found, numbered and named
   - checks for overlaps, gaps under the minimum, buildings off the play area or away from a road, the start-to-exit distance, and any Olympus asset
4. **The stories.** Every plan names its kind of place, its materials and its vignettes, the small scenes that tell what happened there, by the rules in [MAP_STORYTELLING_GUIDE.md](MAP_STORYTELLING_GUIDE.md). They are drawn as numbered markers.
5. **The build.** The map is composed from the plan's own coordinates, then rendered and compared against the drawing.

Olympus 2207 assets are the owner's alone. `site_plan.py` flags them, and `compose_map.py` refuses to write a map that contains any.

---

*The rest of the original page is a table of this mod's own site plans and is not part of the kit.*
