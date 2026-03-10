# surf - simple webkit-based browser
![surf Logo](surf.png)

surf is a simple Web browser based on WebKit/GTK+ with a modal, keyboard-driven interface inspired by qutebrowser and vim.

## About This Fork

This is a fork of the suckless surf browser that reimplements many qutebrowser features while maintaining surf's minimalist philosophy. The goal is to provide a lightweight, keyboard-driven browser with vim-like modal editing and modern features like hint-based navigation, built-in tabs, and userscript support.

## Features

- **Modal Interface**: Vi-style Normal/Insert/Command/Search/Hint/Select modes (qutebrowser-inspired)
- **Built-in Tab Management**: Process-based tabs with visual tab bar, tab move/reorder, closed tab history
- **Hint-based Navigation**: Keyboard-driven link following (viewport-only, no label exhaustion)
- **History & Completion**: Smart URL completion with fuzzy matching
- **Userscript Support**: Compatible with Greasemonkey/Tampermonkey scripts
- **Password Integration**: Built-in pass(1) integration for form filling
- **Downloads**: Integrated download bar with GTK save dialog for choosing destination
- **Screenshots**: Full-page screenshots via `Ctrl+p`
- **Highlighted Search**: CSS Highlight API shows all matches as you type
- **Wayland Native**: Pure Wayland implementation (no X11 dependencies)

## Differences from Upstream surf

This fork differs significantly from vanilla surf:

| Feature | Upstream surf | This Fork |
|---------|---------------|-----------|
| Interface | Single-key bindings | Modal (Normal/Insert/Command/etc.) |
| Tabs | External (tabbed/dmenu) | Built-in tab bar |
| Navigation | Mouse-centric | Hint-based keyboard navigation |
| URL bar | dmenu prompt | Integrated command bar |
| Completion | None | History-based with fuzzy matching |
| Userscripts | Limited | Full Greasemonkey API support |
| Display | X11/Wayland | Wayland only |

If you want the original minimalist surf, use the upstream version. This fork is for users who want qutebrowser's UX in a lighter package.

## Requirements

To build surf you need:
- GTK+ 3 and WebKit2GTK 4.1 header files
- GDK Wayland backend (`gdk-wayland-3.0`)
- C compiler (gcc or clang)

Optional dependencies:
- `pass` - for password manager integration
- `bemenu` or `dmenu` - for password selection UI

## Installation

Edit `config.mk` to match your local setup (surf is installed into the `/usr/local` namespace by default).

Build and install (as root if necessary):

```bash
make clean install
```

## Usage

Basic usage:

```bash
surf [URI]
```

### Keyboard Interface

#### Normal Mode (default)

**Navigation & Tabs:**
| Key | Action |
|-----|--------|
| `o` | Open URL prompt |
| `e` | Edit current URL in prompt |
| `t` | New tab |
| `Shift+O` | New tab with URL prompt |
| `d` | Close current tab |
| `u` | Reopen last closed tab |
| `Shift+J` | Next tab |
| `Shift+K` | Previous tab |
| `Shift+P` | Pin/unpin current tab |
| `Ctrl+j` | Move tab right |
| `Ctrl+k` | Move tab left |

**Link Following:**
| Key | Action |
|-----|--------|
| `f` | Follow link in current tab (hint mode) |
| `Shift+F` | Follow link in new tab (hint mode) |
| `y` | Yank link URL (hint mode) |
| `Shift+Y` | Yank current page URL to clipboard |

**Scrolling:**
| Key | Action |
|-----|--------|
| `j` / `k` | Scroll down/up |
| `Space` | Page down |
| `b` | Page up |
| `g` | Scroll to top |
| `Shift+G` | Scroll to bottom |

**Page Control:**
| Key | Action |
|-----|--------|
| `h` | Back in history |
| `l` | Forward in history |
| `r` | Reload page |
| `Ctrl+Shift+r` | Reload (bypass cache) |
| `/` | Search in page |
| `Esc` | Cancel/stop loading |

**Zoom:**
| Key | Action |
|-----|--------|
| `-` | Zoom out |
| `+` | Zoom in |
| `=` | Reset zoom |

**Mode Switching:**
| Key | Action |
|-----|--------|
| `i` | Enter insert mode |
| `v` | Enter select mode (word at search match) |
| `Shift+V` | Enter select mode (line at search match) |

**Other:**
| Key | Action |
|-----|--------|
| `p` | Password manager (pass) |
| `Ctrl+o` | Toggle web inspector |
| `Ctrl+p` | Take full-page screenshot |
| `Ctrl+s` | Clear download bar |

#### Insert Mode

All keypresses go to web content (for typing in forms, etc.).

**Important:** Press `Esc` to return to Normal mode.

Insert mode is automatically activated when clicking on text inputs.

#### Command Mode (`o` / `e` / `Shift+O`)

Type URL or search term in the command bar:
- Press `Enter` to navigate
- Press `Tab` / `Shift+Tab` to cycle through history completions
- Press `Ctrl+n` / `Ctrl+p` to navigate completions
- Press `Esc` to cancel

**URL Detection:**
- Contains `.` or starts with `localhost` → Treated as URL
- Otherwise → Searches using configured search engine (default: DuckDuckGo)

**Examples:**
```
example.com          → https://example.com
localhost:8080       → http://localhost:8080
rust error handling  → https://duckduckgo.com/?q=rust+error+handling
```

#### Search Mode (`/`)

Type search term and all matches highlight as you type (via CSS Highlight API):
- `Ctrl+n` or bare `n` - Next match
- `Ctrl+p` or `Shift+N` - Previous match
- `v` - Enter Select mode at current match (word)
- `Shift+V` - Enter Select mode at current match (line)
- `Enter` - Accept and exit search
- `Esc` - Cancel search

#### Select Mode (`v` / `Shift+V` from Search)

Text selection anchored at the current search match position:
- `w` - Extend selection forward by word
- `b` - Extend selection backward by word
- `Shift+V` - Expand selection to full line
- `e` - Jump to next search match
- `y` - Yank (copy) selection to clipboard
- `Esc` - Exit select mode

#### Hint Mode (`f` / `Shift+F` / `y`)

Links, buttons, and form elements are labeled with keyboard hints (home row keys: `asdfghjkl`).

**Activation:**
- `f` - Follow link in current tab
- `Shift+F` - Follow link in new tab
- `y` - Yank (copy) link URL to clipboard

**Usage:**
1. Press `f` (or `Shift+F`, `y`)
2. Type the hint label (e.g., `as`)
3. Link/element activates automatically

**Features:**
- Works on links, buttons, inputs, and clickable elements
- Only labels elements currently visible in the viewport (prevents label exhaustion on long pages)
- Autocompletes as you type
- Press `Esc` to cancel

### Userscripts

Place Greasemonkey/Tampermonkey compatible scripts in `~/.surf/userscripts/`.

Scripts must have `.user.js` extension and standard metadata:

```javascript
// ==UserScript==
// @name         Example Script
// @match        https://example.com/*
// @run-at       document-end
// @grant        GM_getValue
// ==/UserScript==

// Your code here
```

**Supported metadata:**
- `@name` - Script name
- `@version` - Version string
- `@match` / `@include` - URL patterns where script runs
- `@exclude` - URL patterns to skip
- `@run-at` - `document-start` or `document-end` (injection timing)
- `@grant` - Permissions (`none`, `unsafeWindow`, API functions)

**Supported APIs:**
- `GM_getValue(key, default)` - Persistent storage (get)
- `GM_setValue(key, value)` - Persistent storage (set)
- `GM_deleteValue(key)` - Delete stored value
- `GM_listValues()` - List all keys
- `GM_xmlhttpRequest(details)` - Cross-origin HTTP requests
- `GM_addStyle(css)` - Inject CSS
- `GM_openInTab(url)` - Open new tab
- `GM_setClipboard(text)` - Copy to clipboard
- `GM_log(...)` - Console logging
- `GM_info` - Script metadata
- `unsafeWindow` - Access page's JavaScript context (requires `@grant unsafeWindow`)

**IPC with surf:**

Userscripts can communicate with surf via `$SURF_FIFO`:

```bash
# In userscript (bash):
echo "open -t https://example.com" > "$SURF_FIFO"  # Open in new tab
echo "jseval console.log('hello')" > "$SURF_FIFO"  # Execute JS
echo "message-info Script complete" > "$SURF_FIFO" # Show message
```

Available environment variables in userscripts:
- `$SURF_FIFO` - Path to command FIFO
- `$SURF_URL` - Current page URL
- `$SURF_TITLE` - Current page title
- `$SURF_MODE` - Current mode (normal/insert)

### Password Manager Integration

surf includes built-in `pass` integration via the `surf-pass` userscript.

**Setup:**

```bash
# Store passwords in pass (format: domain/username)
pass insert www.example.com/username
```

Password entry format:
```
<password>
user: <username>
```

**Usage:**

1. Press `p` in Normal mode on a login page
2. If multiple accounts exist, select one from bemenu
3. Username and password are auto-filled

**Example pass structure:**
```
~/.password-store/
  github.com/
    myusername
  reddit.com/
    account1
    account2
```

The script matches based on domain and supports multiple accounts per site.

### Downloads

When a download is triggered, a GTK save dialog opens so you can choose the destination file and rename it if needed. The dialog starts in your Downloads directory with the suggested filename pre-filled.

A download bar appears below the tab bar showing filename, progress percentage, speed, and elapsed time. Press `Ctrl+s` to clear it after downloads complete.

### Screenshots

Press `Ctrl+p` to capture a full-page screenshot. The file is saved to `/tmp/surf-screenshot-<timestamp>.png` and a desktop notification is shown with the path.

### Tab Management

Tabs are displayed in a tab bar at the top of the window.

**Visual Indicators:**
- Active tab: Highlighted background
- Pinned tabs: Prefixed with `[P]`
- Tab count shown in window title: `[2/5]`

**Interaction:**
- Left-click tab to switch
- Middle-click to close tab
- Keyboard: `Shift+J` / `Shift+K` to cycle
- `Ctrl+j` / `Ctrl+k` to move (reorder) tabs

**Closed Tab History:**
- Press `u` to reopen the last closed tab (up to 10 tabs tracked)

**Pinned Tabs:**
- Press `Shift+P` to pin/unpin current tab
- Pinned tabs are kept alive (prevent process suspension)
- Useful for background music players, chat apps, etc.

### History

History is automatically saved to `~/.surf/history` with format:
```
<timestamp> <url> <title>
```

**Features:**
- Deduplicates entries (latest timestamp wins)
- Updates titles on page load
- Ignores `about:*` and `file://` URLs

**Completion:**

When in Command mode (`o`), start typing to filter history:
- Matches against both URL and title
- Supports multiple space-separated search terms (AND logic)
- Shows up to 15 most recent matches
- Navigate with `Tab` / `Shift+Tab`, `Ctrl+n` / `Ctrl+p`, or arrow keys

### Configuration

Edit `config.h` before building to customize:

**Key bindings:**
```c
static Key keys[] = {
    { MODKEY, GDK_KEY_o, openbar, { .i = 0 } },
    // ...
};
```

**Search engine:**
```c
static const char *searchengine = "https://duckduckgo.com/?q=%s";
```

**Colors:**
```c
static const char *stat_bg_normal  = "#000000";
static const char *stat_fg_normal  = "#ffffff";
static const char *stat_font       = "monospace 11";
```

**WebKit settings:**
```c
static Parameter defconfig[ParameterLast] = {
    [JavaScript]      = { { .i = 1 }, },
    [DarkMode]        = { { .i = 1 }, },
    [Inspector]       = { { .i = 1 }, },
    // ...
};
```

**Site-specific settings:**
```c
static UriParameters uriparams[] = {
    { "(://|\\.)example\\.com(/|$)", {
        [JavaScript] = { { .i = 0 }, },  // Disable JS on example.com
    }, },
};
```

**Custom styles:**

Place CSS files in `~/.surf/styles/`:
- `default.css` - Applied to all sites
- Site-specific styles configured in `config.h`

## Known Issues

- **No session restore** - Tabs are lost on restart (use history)
- **No quickmarks** - Use browser history or bookmarks in password manager
- **No ad blocking** - Use userscripts or DNS-level blocking
- **Wayland-only** - Won't run on X11 (use Xwayland if needed)

## Contributing

This is a personal fork focused on my workflow. Bug reports and patches welcome, but I may not accept feature requests that deviate from the qutebrowser-inspired design.

**Development:**
```bash
git clone <your-fork>
cd surf
make clean && make
./surf
```

## License

MIT/X Consortium License (same as upstream surf). See LICENSE file.

## Links

- Forked surf: https://github.com/DGC75/surf-wayland
- Upstream surf: https://surf.suckless.org
- qutebrowser: https://qutebrowser.org
- suckless: https://suckless.org
