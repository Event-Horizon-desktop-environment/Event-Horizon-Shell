# Hit testing: never fix hit positions again

## The rule

**Input code may never contain geometry. It may only read rects stored
during the last paint (or a layout both sides share).**

Every paint stores the exact rects it drew; pointer handlers resolve
through those rects. Resize, rescale, widget reorder, panel/dock modes —
the next paint re-stores everything, so hits recalculate themselves.
There is no parallel geometry left to drift. (Same model as Nautilus'
`gtk_widget_pick()` over widget allocations and Qt's `childAt()` over
item geometries.)

## Pieces

- `src/shared/system/input/hit_registry.hpp` — generic retained-region
  store (`eh::shell::HitRegistry`, double coords, header-only,
  unit-tested in `test/test_hit_registry.cpp`). Use it for new surfaces.
- Dock strip: paint emits `DockWidgetHit` rects; `dock_bar.cpp` retains
  them on `DockApp::dockRetainedHits` every dock paint; `dock_pick_at()`
  resolves through retained rects first (validated by slot count + key
  sequence) and falls back to computed layout only when stale.
- `EH_HIT_DEBUG=1` outlines live registry regions (generic surfaces);
  dock paint dumps `[layout] widgets …` snapshots under its
  settings-debug flag.

## Status by surface

Migrated (paint and input now share one source):
- Dock strip pick (retained-first).
- Control-center audio sliders: paint widget boxes now derive from the
  shared metrics (was 8px/side drift), 3 sites.
- Control-center mixer sliders: hit truncates app names exactly like
  paint (same helper, font, budget).
- Battery popup close: scaled metrics (was unscaled).
- Media player popup clicks: deleted mirrored constants, delegates to
  the shared `.tpp` layout/hover implementation.
- Desktop media player widget: shared `CardMetrics` for paint/click/hover.

Verified compliant (already shared/retained, no change needed):
- Taskbar strip (retained `g_widgetHits`), control-center cards/rows
  (`layout_for` + shared row helpers), power confirm (shared `layout()`),
  notifications (retained `cards_`), desktop icons (retained cells +
  shared metrics), app drawer + launchpad (shared rect helpers / paint
  model), spotlight (shared geometry fns), overview (shared
  `OverviewLayout`), desktop open-with + mount dialog (retained rects),
  media compact + world clock settings (shared constants/layout fn),
  m3 + ui widgets (retained geometry + `containsPoint`), VPN/battery/
  bluetooth popups (shared constexprs / shared layout + retained item
  rects), calendar/weather popups (no interior hits), OSD/lockscreen
  (no pointer input).

Known leftovers (same recipe applies):
- Popup slot-center anchors post-pick (cosmetic placement only).
- Dead code: launchpad `hit_test_dock_button` has no callers.
- Mixer expanded sliders follow the same truncate rule via shared code.
