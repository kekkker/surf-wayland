surf - simple webkit-based browser
==================================
surf is a simple Web browser based on WebKit/GTK+.

## Wayland Support

Surf now supports native Wayland rendering in addition to X11. This provides better integration with Wayland compositors and improved security.

### Building for Wayland

To build surf with Wayland support:

```bash
make WAYLAND=1
```

To build with X11 support:

```bash
make X11=1
```

### Running on Wayland

If you're running in a Wayland environment, surf will automatically detect and use the Wayland backend if compiled with Wayland support.

### Key Differences from X11

- **Window Identification**: Uses instance IDs instead of X11 window IDs
- **External Tools**: Communicates via D-Bus instead of X11 properties
- **Embedding**: XEmbed is not supported on Wayland (see tabbing alternatives)
- **Automation**: Use `surf-dbus.sh` instead of `xprop`-based scripts

### New Features

- D-Bus interface for external tool integration
- Process-based tabbing alternative
- Improved security through Wayland isolation
- Better integration with modern desktop environments

Requirements
------------
In order to build surf you need GTK+ and Webkit/GTK+ header files.
For Wayland support, you also need wayland-client, wayland-cursor, and dbus-1 development packages.

In order to use the functionality of the url-bar, also install dmenu[0].

Installation
------------
Edit config.mk to match your local setup (surf is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install surf (if
necessary as root):

    make clean install

Running surf
------------
run
	surf [URI]

See the manpage for further options.

Running surf in tabbed
----------------------
For running surf in tabbed[1] there is a script included in the distribution,
which is run like this:

	surf-open.sh [URI]

Further invocations of the script will run surf with the specified URI in this
instance of tabbed.

For Wayland, you can use the process-based tabbing alternative:

	surf-tab [URI]

### External Tool Integration

For Wayland environments, use the D-Bus interface:

```bash
# List all surf instances
surf-dbus.sh list

# Navigate to URL
surf-dbus.sh go <instance_id> <url>

# Get current URI
surf-dbus.sh uri <instance_id>

# Find text on page
surf-dbus.sh find <instance_id> <text>
```

See MIGRATION.md for detailed migration instructions from X11 to Wayland.

[0] http://tools.suckless.org/dmenu
[1] http://tools.suckless.org/tabbed

