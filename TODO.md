## Overall
This is a **real project**, not total garbage, but it has a bunch of classic “large C single-file fork” problems:
- too much logic jammed into `surf.c`
- global/shared state in places where per-window/per-tab state should exist
- lifetime / ownership bugs
- some API misuse / suspicious assumptions
- UI state coupled to active tab in fragile ways
- userscript support is pretty sketchy security-wise
- history/download/file chooser implementations are functional but rough and brittle
- config/docs drift exists
- a lot of “works on my machine” hardcoding
The biggest issue is not style. It’s **state ownership and correctness**.
---
# 1. Architecture is too fucking tangled
## `surf.c` is 4.7k lines and doing everything
It handles:
- browser lifecycle
- GTK window creation
- WebKit setup
- tab management
- modal input handling
- hints
- search/select
- userscript loading/injection
- FIFO IPC
- history persistence/UI
- downloads
- screenshots
- file picker integration
- TLS/cert handling
That’s too much in one file. It makes correctness harder because unrelated concerns share globals and mutate `Client` in ways that are easy to break.
### Why this is bad
- hard to reason about invariants
- hard to test anything
- easy to introduce regressions in event-driven code
- active-tab state and per-view state get mixed constantly
This file should have been split long ago:
- `tabs.c`
- `history.c`
- `userscripts.c`
- `downloads.c`
- `ui_bar.c`
- `hints.c`
Not just for beauty. For preventing bugs.
---
~~# 3. Global hint state is wrong~~
~~## `static HintState hintstate = {0};`~~
~~Hints are global for the entire process.~~
~~That is bad because:~~
~~- multiple windows would race on shared hint state~~
~~- even within one window, active view changes can invalidate global state awkwardly~~
~~- extension messages can arrive after tab/window changes~~
~~You partly defend against this with `hintstate.pageid != c->pageid`, but this is still a band-aid over wrong ownership.~~
~~### Concrete issue~~
~~If there are multiple `Client`s/windows, only one hint session can exist at a time. Starting hints in one can clobber the other.~~
~~Hints belong to a tab or at least a `Client`, not global static process state.~~
~~**FIXED**: HintState is now a member of the Tab struct, so each tab has its own hint state.~~
---
# 4. History popup state is global and broken across windows
These globals are bad:
- `static GtkWidget *history_list = NULL;`
- `static GtkWidget *history_scroll = NULL;`
- `static int history_selected = -1;`
That means the history UI is **shared globally across all clients/windows**.
## Why that’s bad
- opening command bar in one window steals/reparents the same widget from another window
- callbacks are connected with a single `Client *c` when first created
- later reattachment to another client does not recreate signal wiring safely
- selection state is shared globally too
This is one of the clearest correctness bugs in the code. History UI must be per-window.
`history_attach()` literally reparents the same `history_scroll` widget between windows. That’s not a feature. That’s state leakage.
---
# 5. Userscript system is security-loose and semantically sloppy
This part is ambitious, but rough as hell.
## a) `GM_*` storage is just `localStorage`
In `preprocess_userscript()`:
- `GM_getValue`, `GM_setValue`, etc use `localStorage`
That means storage is not really userscript storage:
- tied to page origin/world behavior
- not namespaced per script
- different scripts can stomp each other
- page JS may interact in weird ways depending on injection world
- not durable in the way userscript APIs normally imply
This is a fake implementation, not a real one.
## b) `GM_xmlhttpRequest` is not what it claims
It’s just `XMLHttpRequest` in page/isolated JS.
That does **not** provide real userscript-style cross-origin capability in the normal extension sense. README claims:
- “Cross-origin HTTP requests”
That’s misleading at best.
## c) grant handling is mostly fake
The metadata parser barely uses grants. It mostly:
- checks `unsafeWindow`
- injects big bundles of APIs regardless
So `@grant` support is performative. Scripts aren’t meaningfully sandboxed by declared capabilities.
## d) script matching is naive
`inject_userscripts_early()` parses metadata with primitive string matching. It’s easy to get false matches and malformed behavior.
No robust parser, no proper normalization, no validation.
## e) reload strategy is blunt
`reloaduserscripts()` nukes all scripts from the shared content manager and reloads every tab.
Works, but crude.
## f) shared content manager for all views
`shared_content_manager` is global and reused everywhere. That’s fine for some things, but combined with global userscript reloading and per-window expectations, it increases blast radius of mistakes.
## g) executing external userscripts via shell
`spawnuserscript()` does:
```c
execl("/bin/sh", "sh", "-c", script, NULL);
```
That means the configured script string is interpreted by a shell. In your config you pass a literal path, so it’s okay-ish, but this is still shell-eval based execution. If script source becomes dynamic later, this becomes nasty instantly.
---
# 6. FIFO IPC is very underdesigned
The FIFO command parser in `fifo_read()` is primitive:
- line-based only
- ad hoc parsing
- supports `message-error`, `message-info`, `jseval`, `open`
- no escaping/quoting model
- no authentication beyond filesystem perms
- no command framing beyond newline
## Specific badness
### `jseval` is arbitrary JS execution
Anything with FIFO write access gets direct JS injection into current page context.
That may be intended, but it’s high-trust and dangerous.
### parsing of options is sloppy
For `jseval` / `open`, options are skipped by manually walking through `-...` chunks. No robust parser.
### current-tab coupling
Commands act on the `Client` currently associated with the FIFO, but because that client multiplexes many tabs, semantics are inconsistent.
This is “good enough for a personal browser hack”, not clean IPC design.
---
# 7. A bunch of ownership/lifetime stuff is shaky
## a) `destroyclient()` list unlink is suspicious but works by luck
```c
for (p = clients; p && p->next != c; p = p->next)
    ;
if (p)
    p->next = c->next;
else
    clients = c->next;
```
If `c == clients`, loop starts with `p = clients`, and since `p->next != c`, eventually `p == NULL`, then `clients = c->next`.
So it works, but it’s ugly and fragile-looking. Standard “prev pointer” unlink would be clearer.
## b) tab closing doesn’t explicitly unref view objects
`tab_close()` removes widget from box:
```c
gtk_box_remove(GTK_BOX(c->vbox), GTK_WIDGET(dead));
```
Maybe GTK object lifecycle handles it through parenting/refcounts, but this code relies heavily on GTK ownership behavior without being explicit. That’s survivable, but in this codebase’s complexity it’s risky.
## c) `c->cert` handling is suspicious
`webkit_web_view_get_tls_info(c->view, &c->cert, &c->tlserr);`
You assign certificate pointers repeatedly but do not manage ownership consistently. Depends on API ownership rules. If API gives borrowed refs, you’re okay as long as you don’t unref. But then `failedcert` is explicitly ref’d. There’s asymmetry and potential confusion.
## d) async callbacks guarded by `gtk_widget_get_realized()` are hacky
In `screenshot_cb()` and `find_select_yank_cb()` you use:
```c
if (!gtk_widget_get_realized(GTK_WIDGET(obj)))
    return;
```
That is not a robust lifetime guarantee for async results. “realized” is not “still valid and semantically safe to finish callback on”. It’s a heuristic, not proper object lifetime management.
## e) `g_array_append_val` with partially initialized stack structs
Usually okay here, but the design is delicate because you rely on manual deep-freeing later.
---
# 8. Search/select UX implementation is half-baked
## Search-on-type is disabled despite UI/docs implying otherwise
README says highlighted search updates “as you type”.
But:
```c
static gboolean bar_update_search(gpointer data)
{
    /* Do nothing - search only happens on Enter */
    return FALSE;
}
```
So either docs lie or the code regressed.
## `find_current_match` bookkeeping is manual and can drift
You manually increment/decrement current match in `find()` based on search-next/previous calls rather than syncing from WebKit result state. That can desync.
## CSS Highlight API usage is brute-force
`find_highlight_update()` walks all text nodes and creates ranges manually. That’s expensive on large pages and can become nasty.
It’s clever, but definitely not cheap.
## Uses selection `.modify(...)`
That API is not something I’d call robust/portable long-term. Browser behavior can be quirky.
---
# 9. File chooser integration is pretty janky
The external file picker feature is cool, but the implementation is rough.
## a) hardcoded tmp file in `/tmp`
```c
gchar *tmpfile = g_strdup("/tmp/surf-filepick-XXXXXX");
```
You use `mkstemp`, so naming is okay, but it still hardcodes `/tmp` instead of proper runtime/temp APIs.
## b) shell-template replacement is sketchy
You replace `"{}"` in arguments, and also inside shell command strings using split/join. Functional, but sloppy and easy to break with more complex command templates.
## c) ignores `allow_multiple`
You store:
```c
fcd->allow_multiple = webkit_file_chooser_request_get_select_multiple(r);
```
but never actually enforce it when reading the temp file. If picker returns many paths and request is single-select, this code still hands them over.
## d) callback cleanup is subtle
The `argv` cleanup logic compares pointer identities against original config strings. Works-ish, but it’s fiddly and brittle.
## e) config is machine-specific in `config.h`
```c
"NNN_PREVIEWIMGPROG='/home/kek/.bin/nnn-img2sixel' ..."
```
That is pure local-machine leakage into repo config.
---
# 10. Downloads flow is weird and stateful in a bad way
The download UI works by:
1. intercepting destination decision
2. canceling the download
3. using command bar as a path prompt
4. restarting download with saved URI/path state
That’s pretty hacky.
## Problems
### a) single pending download state per client
`dl_pending_uri` and `dl_pending_path` are single fields on `Client`.
So concurrent destination prompts/download starts are not handled properly. One can stomp another.
### b) command bar overloaded for downloads
The same command/status entry is reused for URL entry, search, and download path input. This creates mode/state coupling and weird branches in `baractivate()` and `barkeypress()`.
### c) cancellation/restart semantics are awkward
Canceling and restarting a download may lose some headers/request context depending on WebKit behavior.
### d) static global counter
```c
static guint dl_counter = 0;
```
Fine for display, but global across all clients and lifetime-long. Not terrible, just another sign of “throw globals at it”.
---
# 11. `loaduri()` URL detection is simplistic and wrong in edge cases
```c
else if ((strchr(uri, '.') && !strchr(uri, ' ')) ||
         g_str_has_prefix(uri, "localhost")) {
    url = g_strdup_printf("https://%s", uri);
}
```
This is crude.
## Problems
- blindly assumes hostnames with dots should be HTTPS
- `localhost:8080` becomes `https://localhost:8080` even though README says `http://localhost:8080`
- non-http custom schemes without explicit prefix get mangled
- no proper URI parsing
Your README and implementation are already inconsistent here.
---
# 12. Config and docs drift / repo hygiene issues
## `config.def.h` vs `config.h`
You ship both, and `config.h` is clearly personalized:
- custom search engine
- dark mode enabled
- inspector enabled
- WebGL changed
- local path to image preview helper
- custom keybinds / behavior
This is fine for local use, but in a repo it muddies what the intended defaults really are.
## README claims don’t fully match code
A few examples:
- search highlighting “as you type” does not currently happen
- screenshot path in README says `/tmp/surf-screenshot-<timestamp>.png`, code writes to `~/Pictures` or `$HOME`
- command URL handling docs differ from actual `loaduri()` logic
- userscript API support is oversold
## `display.c` / `display.h` are almost fake abstraction
They pretend there is a display backend abstraction, but:
- only one backend exists
- code just zeroes struct and sets enum
- no actual Wayland-specific logic
This is dead-weight abstraction at the moment.
---
# 13. Build system is serviceable but crude
## Makefile compiles all sources in one shot
```make
$(OBJ): $(SRC)
	$(CC) $(SURFCFLAGS) -c $(SRC)
```
That’s not proper per-file compilation. Same for webext object.
### Why bad
- rebuild granularity sucks
- dependency behavior is weird
- parallel make friendliness is poor
- not idiomatic make
## No generated dependency tracking
Not fatal, but for C it’s a missing basic quality-of-life thing.
## Warnings are partially suppressed
`-Wno-sign-compare`, `-Wno-missing-field-initializers`, etc. Some okay, some likely hiding laziness.
---
# 14. Some code is just awkward or misleading
## a) `newwindow(..., int noembed)` ignores `noembed`
Dead parameter smell.
## b) deprecated / legacy leftovers
There are bits of upstream surf style still hanging around:
- old `SETPROP` / xprop / dmenu stuff in `config.def.h`
- embedding references
- weirdly mixed conceptual model between original surf and your modal/tabbed browser
Repo feels like a fork in mid-metamorphosis.
## c) weird unused or vestigial globals
Examples:
- `togglestats`
- `pagestats`
- `mainloop` being global
- `spair` socketpair created and channel set up, but basically unused
That `socketpair()` setup in `setup()` looks like leftover architecture debris. It creates a channel and then does nothing meaningful with it.
## d) style application leaks intent
`setstyle()` creates a stylesheet with `webkit_user_style_sheet_new(...)` and adds it directly, but doesn’t keep explicit ownership visible. Fine if API consumes refs as expected, but inconsistent style again.
---
# 15. Signal wiring / event handling is fragile
## Multiple signals tied to `Client *c` for all views
Every view created gets signals with the same `Client *c`. Then callbacks try to recover meaning by checking whether the emitting view is active.
That works until it doesn’t.
### Symptoms
- lots of `if (v != c->view)` branches
- active-tab assumptions spread everywhere
- easy to accidentally update active state from background tab event
## `showview()` only really designed for initial client window
Function name suggests “show a view”; it also constructs the whole window, status bar, tab bar, CSS, FIFO, etc. That’s not “show view”, that’s “initialize full browser window”.
Naming and responsibility are muddy.
---
# 16. Potential bugs / correctness concerns worth real attention
These are the ones I’d prioritize.
## High priority
### 1. Global history UI widgets across clients
This is a real design bug.
~~### 2. Global hint state~~
Also a real design bug.
### 3. Single pending download state
Can break with overlapping downloads/prompts.
### 4. Userscript API claims vs reality
Not just docs problem; it creates false expectations and security ambiguity.
### 5. Per-tab state shoved into `Client`
Root cause of a lot of fragility.
## Medium priority
### 6. `loaduri()` URL inference mismatch and bad heuristics
User-visible wrong behavior.
### 7. search “as-you-type” docs mismatch
Feature drift / broken expectation.
### 8. machine-specific `config.h` committed
Bad repo hygiene.
### 9. Makefile object build rules
Crude and annoying.
### 10. async lifetime handling via realized checks
Suspicious defensive hack, not solid correctness.
---
# 17. Security-ish concerns
This is a browser, so these matter.
## a) userscripts are heavily trusted
- shell execution
- FIFO command interface
- arbitrary JS eval
- fake GM APIs backed by page-accessible-ish storage patterns
That’s okay for a personal browser for a technical user, but not okay to present as robust userscript support.
## b) page-world injection for `unsafeWindow`
Understandable, but dangerous. Any mistake in grant detection or script preprocessing has big consequences.
## c) custom certificate allow logic
`setcert()` allows a custom certificate for host. Useful, but dangerous territory, and implementation is simplistic. No deeper validation UX.
---
# 18. What is actually good
Not all bad shit.
Some good things:
- the code clearly has a coherent UX vision
- modal interface integration is more complete than a toy hack
- hinting via web extension is a sane direction
- download bar UI is practical
- history dedup idea is decent
- pinned-tab keepalive is pragmatic
- cert error page is functional
- there are visible attempts to avoid active-tab corruption from background tab events
So this is not trash. It’s just **overgrown and under-structured**.
---
# Bottom line
## Main diagnosis
The codebase’s real problem is:
**state ownership is wrong.**
Everything else comes from that:
- global widgets
- global hint state
- active-tab mutation on a window-global struct
- callback complexity
- weird branches and special cases
## Short blunt ranking of worst shit
1. **`surf.c` is too big and too coupled**
2. **tab/window state model is wrong**
3. **history popup is global shared UI**
4. **hint state is global shared logic**
5. **userscript support is overclaimed and under-specified**
6. **download prompt state is single-slot and fragile**
7. **docs/config drift is real**
8. **build system is rough**
9. **hardcoded local config leaked into repo**
10. **lots of hacks instead of explicit per-tab/per-window abstractions**
If you want, next I can turn this into a **proper actionable audit**:
- file-by-file
- severity-ranked
- with exact line references
- plus a refactor plan in the right order so you can actually fix the worst parts first.
