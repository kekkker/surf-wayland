# Surf Wayland Migration Guide

## Overview

This guide helps existing surf users migrate from X11 to Wayland, covering changes in external tool integration, automation scripts, and workflows.

## What Changed

### Window Management
- **X11**: Used global window IDs accessible via `xprop` and `xdotool`
- **Wayland**: Uses application-instance IDs accessible via D-Bus

### External Communication
- **X11**: X11 properties (`_SURF_FIND`, `_SURF_GO`, `_SURF_URI`)
- **Wayland**: D-Bus methods and signals

### Embedding
- **X11**: XEmbed protocol for tabbed browsers
- **Wayland**: Process-based tabbing or compositor integration

## Migration Steps

### 1. Update Build Process

**Before (X11 only):**
```bash
make
```

**After (Wayland or X11):**
```bash
make WAYLAND=1    # For Wayland
make X11=1        # For X11 (fallback)
```

### 2. Update Automation Scripts

**Before (X11 window ID based):**
```bash
#!/bin/sh
# Get the surf window ID
WINDOW_ID=$(xdotool search --class "surf" | head -1)

# Navigate to a URL
xprop -id "$WINDOW_ID" -f _SURF_GO 8s -set _SURF_GO "https://example.com"

# Get current URI
URI=$(xprop -id "$WINDOW_ID" _SURF_URI | cut -d'"' -f2)
```

**After (D-Bus based):**
```bash
#!/bin/sh
# Get the surf instance ID
INSTANCE_ID=$(surf-dbus.sh list | head -1)

# Navigate to a URL
surf-dbus.sh go "$INSTANCE_ID" "https://example.com"

# Get current URI
URI=$(surf-dbus.sh uri "$INSTANCE_ID")
```

### 3. Update surf-open.sh Integration

The `surf-open.sh` script has been updated to work with both X11 and Wayland. If you have custom versions, update them to use the new D-Bus interface.

### 4. Update Tabbing Workflows

**X11 Embedding (deprecated):**
```bash
# Embed surf in tabbed window manager
surf -e $(xwininfo -tree | grep "tabbed" | awk '{print $1}')
```

**Wayland Process-based Tabbing:**
```bash
# Use tab manager
surf-tab-manager &

# Or use compositor tabbing (if available)
# This depends on your specific compositor
```

## External Tool Changes

### xprop Replacements

| X11 Command | Wayland Equivalent |
|-------------|-------------------|
| `xprop -id $WIN _SURF_URI` | `surf-dbus.sh uri $INSTANCE` |
| `xprop -id $WIN -f _SURF_GO 8s -set _SURF_GO "url"` | `surf-dbus.sh go $INSTANCE "url"` |
| `xprop -id $WIN -f _SURF_FIND 8s -set _SURF_FIND "text"` | `surf-dbus.sh find $INSTANCE "text"` |

### xdotool Replacements

| X11 Command | Wayland Equivalent |
|-------------|-------------------|
| `xdotool search --class "surf"` | `surf-dbus.sh list` |
| `xdotool windowactivate $WIN` | Window activation via compositor |
| `xdotool key --window $WIN ctrl+l` | `surf-dbus.sh focus $INSTANCE` (if implemented) |

## Troubleshooting

### Common Issues

1. **"No D-Bus service found"**
   - Ensure surf is running with Wayland backend
   - Check D-Bus session bus is running
   - Verify surf was compiled with Wayland support

2. **"Instance ID not found"**
   - Use `surf-dbus.sh list` to see available instances
   - Ensure target surf instance is still running

3. **Performance issues**
   - D-Bus communication may be slightly slower than X11 properties
   - Consider batching operations when possible

4. **Embedding doesn't work**
   - XEmbed is not supported on Wayland
   - Use process-based tabbing or compositor integration

### Debug Information

To debug Wayland issues:

```bash
# Check Wayland display
echo $WAYLAND_DISPLAY

# Check if surf is using Wayland
ps aux | grep surf

# Check D-Bus services
dbus-send --session --dest=org.freedesktop.DBus --print-reply \
          /org/freedesktop/DBus org.freedesktop.DBus.ListNames | \
    grep surf
```

## Getting Help

- Check the migration guide examples in `examples/`
- Review the D-Bus interface documentation
- Test with the provided `surf-dbus.sh` script
- Report issues on the surf issue tracker

## Backward Compatibility

If you need to maintain X11 compatibility:

1. Build with X11 support: `make X11=1`
2. Continue using existing X11-based scripts
3. Gradually migrate scripts to D-Bus interface
4. Test both backends during transition period

The goal is to provide a smooth transition while maintaining functionality for existing workflows.

## Testing Your Migration

1. **Basic functionality**: Launch surf, navigate websites, verify rendering
2. **External tools**: Test surf-open.sh, surf-dbus.sh, and custom scripts
3. **Multi-instance**: Launch multiple browser instances and control independently
4. **Tab management**: Test process-based tabbing functionality
5. **Error handling**: Test behavior with invalid commands, missing instances

## Performance Considerations

- **D-Bus overhead**: D-Bus communication has slightly higher latency than X11 properties
- **Memory usage**: Wayland backend may use slightly more memory due to additional abstractions
- **Startup time**: Additional D-Bus initialization may increase startup time minimally
- **Tab manager**: Process-based tabbing uses more memory than XEmbed but provides better isolation