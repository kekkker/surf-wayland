# X11 to Wayland Migration Guide

## Overview
This document helps users migrate existing surf automation scripts from X11 to Wayland.

## Key Changes

### Window Identification
- **X11**: Use `xprop -id $WINDOW_ID` to get window properties
- **Wayland**: Use instance IDs via `surf-dbus.sh list`

### URI Access
- **X11**: `xprop -id $WINDOW_ID _SURF_URI`
- **Wayland**: `surf-dbus.sh uri <instance_id>`

### Navigation
- **X11**: `xprop -id $WINDOW_ID -f _SURF_GO 8s -set _SURF_GO "http://example.com"`
- **Wayland**: `surf-dbus.sh go <instance_id> "http://example.com"`

### Text Search
- **X11**: `xprop -id $WINDOW_ID -f _SURF_FIND 8s -set _SURF_FIND "search text"`
- **Wayland**: `surf-dbus.sh find <instance_id> "search text"`

## Script Migration Examples

### Before (X11)
```bash
#!/bin/sh
WINDOW_ID=$(xdotool search --class "surf" | head -1)
xprop -id "$WINDOW_ID" -f _SURF_GO 8s -set _SURF_GO "$1"
```

### After (Wayland)
```bash
#!/bin/sh
INSTANCE_ID=$(surf-dbus.sh list | head -1)
surf-dbus.sh go "$INSTANCE_ID" "$1"
```

## Testing Your Migration

1. Ensure surf is running with Wayland backend
2. Test D-Bus connectivity: `surf-dbus.sh list`
3. Verify each migrated command works correctly

## Backward Compatibility

The updated `surf-open.sh` script automatically detects the display backend:
- On Wayland: Uses D-Bus communication
- On X11: Falls back to traditional xprop workflow

## Troubleshooting

### D-Bus Issues
- Ensure D-Bus session bus is running: `echo $DBUS_SESSION_BUS_ADDRESS`
- Check if surf is registered: `dbus-send --session --dest=org.freedesktop.DBus --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames | grep surf`

### Instance Not Found
- List available instances: `surf-dbus.sh list`
- Verify surf is running with Wayland backend

### Command Failures
- Check surf-dbus.sh help: `surf-dbus.sh help`
- Verify instance ID format (should be like "surf-browser-1234-5678")