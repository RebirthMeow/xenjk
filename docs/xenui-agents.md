# xenui for agents: writing RML/RCSS that works

xenui is RmlUi 6.3 inside the MP UI module, beside the stock `.menu` system (design: `docs/wishlist/ui-system.md` in the
main checkout). RmlUi looks like HTML and CSS but is not a browser. Web CSS that RmlUi does not have fails silently,
so lint before every launch:

```
python -I tools/ui/rcss_lint.py <folder with .rml/.rcss>
```

The lint reads RmlUi's property and tween lists from `lib/rmlui`, so it follows the vendored version.

## Where things are

| What | Where |
|---|---|
| UI side (RmlUi interfaces, commands) | `codemp/ui/xenui.cpp`, `xenui.h`; host glue `ui_xenui_host.c`; stub `xenui_stub.c` |
| Renderer side | `codemp/rd-common/xenui_render.h` (the interface), `codemp/rd-vulkan/tr_xenui.cpp` |
| Libraries | `lib/rmlui` (Core and Debugger), `lib/freetype` (OpenType/TrueType/CFF only); `lib/VENDORED.md` |
| Documents | `ui/xen/*.rml`, `*.rcss` in a pack or loose in the home's `base` |
| Fonts | every `.ttf`/`.otf` in `fonts/xenui/` loads at start; RmlUi names a face by its family name |

## Units and layout

- Design on the 1920 x 1080 canvas in `dp`: the context's dp ratio is window height / 1080 (1.0 at 1080p, 2.0 at
  2160p). Text and shapes rasterize at the real size, so 4K is sharp; never bake text into images.
- Layout is block, inline, flex and absolute positioning. There is **no grid**, no `calc()`, no `var()`, no `clamp()`.
- Units: `px`, `dp`, `em`, `rem`, `%`, `vw`, `vh`, `deg`, `rad`, `s`, `ms`.
- `box-sizing` defaults to `content-box`, as in CSS: padding adds to width.

## Motion

- `transition: <properties> <duration> [<delay>] <tween>`, e.g. `transition: transform color 0.24s back-out;`.
- `animation: <duration> [<delay>] <tween> [infinite|<count>] [alternate] [paused] <name>` with `@keyframes`.
- Tweens are RmlUi names, not web easings: `<family>-in`, `-out`, `-in-out` for `back`, `bounce`, `circular`,
  `cubic`, `elastic`, `exponential`, `quadratic`, `quartic`, `quintic`, `sine`, plus `linear`. **No** `ease-in-out`
  or `cubic-bezier()`. Overshoot is `back-out`.
- Transforms: `translate`, `rotate`, `scale`, `skew`, `skewX`/`skewY`, `matrix`, `perspective`. A transformed element
  keeps its layout box; `transform-origin` defaults to the centre.

## Input and focus

- Keyboard and pad navigation: give focusable elements `tab-index: auto; focus: auto;` and a `nav` value
  (`nav: vertical`, `nav: horizontal`, `nav: auto`, or `nav-up: #id` and the other directions). The lint checks
  `#id` targets exist.
- Enter clicks the focused element. Esc closes the document (xenui.cpp).
- Style focus and hover together (`button:hover, button:focus`): a pad has no hover.
- Inline `onclick="..."` does nothing yet: there is no event listener instancer. Events come from C++ listeners or
  data-model events (planned for phase 1).

## Data models and screens over C code

A C screen keeps its logic and publishes a model; the document only draws it and sends input back
(`codemp/ui/ui_player_look.c`, end of file, is the example; `xenui.h` has the API):

- **Model:** `XenUI_ModelCreate( "xp", onEvent )`, then every frame `XenUI_SetInt/Float/Bool/String( model, path,
  value )` and `XenUI_SetCount( model, path, n )` for arrays. Paths are `name`, `name.field`, `list[3].field`. A set
  that does not change the value costs a lookup and dirties nothing. Set every field an element reads, even when
  the element is hidden: a `data-if` element's `data-style-*` still evaluates, and a missing value logs
  `Syntax error parsing inline property declaration 'left: dp;'`.
- **Markup:** `<body data-model="xp">`, `{{ list[0].field }}`, `data-for="row, i : rows"`, `data-if`,
  `data-class-<name>="expr"`, `data-style-<property>="row.x + 'dp'"`, `data-attr-class="'tg ' + tag.style"` (only on
  elements with no other class bindings). Expressions take `+ - * /`, comparisons, `&&` (`&amp;&amp;` in the
  file), `!`, `? :` and string literals in single quotes.
- **Events:** `XenUI_ModelEvent( model, "hit" )`, then `data-event-mousedown="hit('row', i, 0)"`; the handler gets
  the name, the arguments as strings and the mouse button (0 left, 1 right). The innermost handler acts (propagation
  stops). Act on `mousedown`, not `click`: menus act at the press, and scripted clicks send only the press.
  `mouseover` also fires when an element moves under a still pointer: gate hover on real cursor motion.
- **Overlay:** the screen calls `XenUI_Overlay( "ui/xen/<doc>.rml" )` every frame it is open; the document hides on
  the first frame without the call. The C screen keeps the keys; give it the mouse with `XenUI_OverlayMouse`.
- **Underlay:** `XenUI_Underlay( "ui/xen/<doc>.rml" )` draws a document at the call, so under whatever the screen draws
  after it (the player screen's paint goes under its 3D stage: `player_paint.rml` from layer 0). It lives in a context
  of its own with its own models (`XenUI_UnderlayModelCreate`, no events) and needs a version 2 renderer (it returns 0
  otherwise: draw the fallback). The renderer draws two document passes a frame.
- **Class names:** a data class must not reuse a class name the sheet styles for another element (`seg`, `btn` gave
  head labels and fields the option-box and commit-button styles).

## CPU: what costs

RmlUi lays out the whole document again whenever anything in it needs layout, so a layout frame costs about 1-2 ms
for the player screen against about 0.05 ms for a frame without one (2026-11-06, `ui_xenStats` and `ui_xenProfile`).
Keep layout to the frames where text or sizes really change:

- **Set each value once a frame.** A value set to 0 and then to its real value changes twice, dirties its name every
  frame and lays the document out every frame (the Force meter did: 1.2 ms a frame). `ui_xenStats` lists names
  dirtied on every frame of a still window.
- **Move and animate with `transform`, not `left`/`top`/`width`.** Transform, opacity and colour changes need no
  layout; a `transition` on `left`, `top` or `width` lays the document out on every frame it runs. Position a moving
  element with `data-style-transform="'translate(' + x + 'dp, ' + y + 'dp)'"` and grow a bar with `scaleX`.
- **Avoid flex inside flex.** RmlUi formats a flex item more than once; nesting multiplies it. Rows of inline items
  read as well with `display: inline-block` and `vertical-align`; whitespace-only text between elements is dropped,
  so it adds no spaces. `font-size: 0dp` on a container keeps its own line strut out of the height.

## One-shot motion from C events

RmlUi starts an element's animations when its `animation` value changes, and never on its own. To play a keyframe
animation when something happens in C (a section change, a commit, a refused key), flip between two copies of the same
keyframes: the C screen publishes a code that alternates 1 and 2 (0 before the first event, so nothing plays at load),
the document binds `data-class-ca="fx.cp == 1" data-class-cb="fx.cp == 2"`, and `.ca #bleed` / `.cb #bleed` name
`bleed-a` / `bleed-b`. For one item in a list, publish the code on the item and select it with an attribute
(`data-attr-fx="row.fx"`, `.row[fx="7"] .pips`); zero the codes when the list is rebuilt for another section. A
stagger needs a delay per item: publish the whole `animation` string (`data-style-animation="row.anim"`, set once and
unchanged until the next event). `ui_player_look.c` ("Motion") and `ui/xen/player.rcss` (the end) are the example.

- Keep the base style the end state, so a finished animation (RmlUi removes its value) leaves the element right; an
  element that only shows during an animation (a ghost, the veil, the confirm bloom) has `opacity: 0` as its base.
- An animation on an element that also takes a bound `transform` or a skew must carry it in every keyframe, or go on
  a wrapper (`.rin` in a row, `.fin` in a field).

## Effects (renderer interface version 2)

`box-shadow`, `filter` (blur, drop-shadow, opacity, brightness, contrast, invert, grayscale, sepia, hue-rotate,
saturate), `mask-image`, clip masks and the gradient decorators all draw (`ui/xen/fx.rml` in the research data shows
each). A version 1 renderer draws none of them. A custom `shader()` decorator never draws. Traps:

- **Clipping needs layout overflow.** RmlUi clips an `overflow: hidden` element only when its content overflows its
  layout box; content moved by `transform` does not count. Add `clip: always` to clip anyway (wipes, cuts).
- **A clip that moves:** to wipe something in or out, slide an `overflow: hidden; clip: always` wrapper by
  `translateX(100%)` and its child back by `translateX(-100%)`; the child stays put and the wrapper cuts it.
- **`drop-shadow` takes the colour first:** `filter: drop-shadow(#000000e0 0dp 4dp 6dp)`; the last length is sigma
  (half a CSS blur radius).
- **Text shadows:** use `font-effect: glow(0dp W 0dp Y colour)`, baked into the glyphs at no per-frame GPU cost. For
  a CSS `text-shadow: 0 Y B` use W = 1.25 B (RmlUi's sigma is 0.4 W, CSS's is B / 2).
- **Filters cost GPU passes per element** (copy, blur both ways, composite); keep them to few elements. Clip masks
  are cheap.

## The loop: lint, run, read

1. Lint: `python -I tools/ui/rcss_lint.py <docs>`.
2. Run the UI lab: the main menu with no map, the document opened with `ui_xenOpen`
   (`.codex-research/wishlist-20261101/drafts/xenui/game/spike.py` is the pattern: hidden client, private home,
   game lock).
3. Read the result: a JSON dump and a screenshot of each state.

Console commands (the UI module):

| Command | Does |
|---|---|
| `ui_xenOpen <doc.rml>` / `ui_xenClose` | open (takes the keys, draws over the stock menus) / close |
| `ui_xenReload` | clears the style sheet and template caches and reopens the document (edit files loose, reload) |
| `ui_xenDump [file]` | the live element tree as JSON: tag, id, class, border box in window pixels, visible, state (hover, focus, active, checked, disabled), text, local styles; plus focus, window size, dp ratio and CPU cost |
| `ui_xenShot <name>` | `screenshots/<name>.json` (the dump) and `screenshots/<name>.jpg` (the next frame) |
| `ui_xenSend <selector> click\|focus\|blur\|hover\|scrolltop <dp>` | act on the first element matching the selector |
| `ui_xenKey up\|down\|left\|right\|enter\|tab\|space` | a navigation key |
| `ui_xenDebug [0\|1]` | RmlUi's debugger overlay |
| `ui_xenStats [reset]` | CPU since the last reset: publish (a C screen's model sets), update and render (mean, median, p90, max), sets and changes per frame, and the model names dirtied with how many frames dirtied each |
| `ui_xenProfile [reset]` | RmlUi's own zones (layout, render, text) by self time per frame; needs a `-DXenUIProfile=ON` build (never a play build) |

`ui_xenui 0` (latched: set it on the command line) turns xenui off; a renderer without the interface also leaves the
stock menus only. A shot taken during a transition shows that frame; wait out the transition (`wait N` is frames) for
the settled state.

## Traps

- **Percent transforms in transitions** were resolved in place in the style sheet's shared value, so every element
  took the first one's width (all closed wipes at `translateX(213px)`). Fixed in the vendored RmlUi
  (`ElementAnimation.cpp`, marked `xenjk`); keep the fix when updating RmlUi.
- **Pictures:** the engine's PNG loader takes powers of two only (JPEG takes any size); a picture that fails to load
  draws as a white box.

- Commands appended by the UI (`EXEC_APPEND`) starve behind the test harness's `vstr loop`: xenui runs its screenshot
  with `EXEC_NOW`.
- The stock cursor is forwarded only when it moves, so `ui_xenSend ... hover` holds until the mouse moves.
- RmlUi's family name for a variable font is its typographic family (`Big Shoulders`); each named instance loads as
  a weight.
- `cl_renderer` can be longer than `MAX_QPATH`: the module name buffer is `MAX_OSPATH`.
