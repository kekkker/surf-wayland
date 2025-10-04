# External Tools Integration

## Overview

Surf's external tool interface has been updated for Wayland compatibility. This document describes the new D-Bus based interface and provides examples for tool developers.

## D-Bus Interface

### Service Information

- **Service Name**: `org.suckless.surf.instance<PID>`
- **Object Path**: `/org/suckless/surf`
- **Interface**: `org.suckless.surf`

### Methods

#### GetURI
Get the current URI of a surf instance.

```bash
dbus-send --session --dest=org.suckless.surf.instance1234 \
          /org/suckless/surf org.suckless.surf.GetURI \
          string:"surf-instance-1234-5678"
```

#### Go
Navigate to a specific URI.

```bash
dbus-send --session --dest=org.suckless.surf.instance1234 \
          /org/suckless/surf org.suckless.surf.Go \
          string:"surf-instance-1234-5678" \
          string:"https://example.com"
```

#### Find
Search for text on the current page.

```bash
dbus-send --session --dest=org.suckless.surf.instance1234 \
          /org/suckless/surf org.suckless.surf.Find \
          string:"surf-instance-1234-5678" \
          string:"search text"
```

#### ListInstances
Get all running surf instances.

```bash
dbus-send --session --dest=org.suckless.surf \
          /org/suckless/surf org.suckless.surf.ListInstances
```

### Signals

#### URIChanged
Emitted when the current URI changes.

```bash
# Monitor URI changes
dbus-monitor --session "interface='org.suckless.surf',member='URIChanged'"
```

## Helper Scripts

### surf-dbus.sh

A comprehensive script for interacting with surf via D-Bus.

```bash
# List all instances
surf-dbus.sh list

# Get current URI
surf-dbus.sh uri <instance_id>

# Navigate to URL
surf-dbus.sh go <instance_id> <url>

# Find text
surf-dbus.sh find <instance_id> <text>
```

### surf-open.sh (Updated)

Updated to work with both X11 and Wayland backends.

```bash
# Open URL in existing instance or create new one
surf-open.sh "https://example.com"
```

## Integration Examples

### Python Integration

```python
import dbus
import sys

def surf_go(instance_id, url):
    bus = dbus.SessionBus()
    proxy = bus.get_object('org.suckless.surf', '/org/suckless/surf')
    interface = dbus.Interface(proxy, 'org.suckless.surf')
    interface.Go(instance_id, url)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python surf_go.py <instance_id> <url>")
        sys.exit(1)

    surf_go(sys.argv[1], sys.argv[2])
```

### Shell Integration

```bash
#!/bin/bash
# surf-bookmark.sh - Simple bookmark manager

BOOKMARK_FILE="$HOME/.surf-bookmarks"

case "$1" in
    add)
        if [ $# -ne 3 ]; then
            echo "Usage: $0 add <instance_id> <bookmark_name>"
            exit 1
        fi

        instance_id="$2"
        name="$3"
        uri=$(surf-dbus.sh uri "$instance_id")

        echo "$name $uri" >> "$BOOKMARK_FILE"
        echo "Bookmark '$name' added: $uri"
        ;;

    list)
        if [ -f "$BOOKMARK_FILE" ]; then
            cat "$BOOKMARK_FILE"
        else
            echo "No bookmarks found"
        fi
        ;;

    open)
        if [ $# -ne 3 ]; then
            echo "Usage: $0 open <instance_id> <bookmark_name>"
            exit 1
        fi

        instance_id="$2"
        name="$3"

        uri=$(grep "^$name " "$BOOKMARK_FILE" | cut -d' ' -f2-)
        if [ -n "$uri" ]; then
            surf-dbus.sh go "$instance_id" "$uri"
            echo "Opened bookmark '$name': $uri"
        else
            echo "Bookmark '$name' not found"
        fi
        ;;

    *)
        echo "Usage: $0 {add|list|open} [args...]"
        exit 1
        ;;
esac
```

## Migration from X11

### Key Changes

1. **Window IDs → Instance IDs**: Use surf-dbus.sh list to get instances
2. **xprop → D-Bus**: Replace xprop calls with surf-dbus.sh commands
3. **Property Access → Method Calls**: Use D-Bus methods instead of property manipulation

### Example Migration

**X11 Version:**
```bash
WINDOW_ID=$(xdotool search --class "surf" | head -1)
URI=$(xprop -id "$WINDOW_ID" _SURF_URI | cut -d'"' -f2)
echo "Current URI: $URI"
```

**Wayland Version:**
```bash
INSTANCE_ID=$(surf-dbus.sh list | head -1)
URI=$(surf-dbus.sh uri "$INSTANCE_ID")
echo "Current URI: $URI"
```

## Troubleshooting

### Common Issues

1. **"Service not found" errors**
   - Ensure surf is running
   - Check correct instance ID
   - Verify D-Bus session bus is available

2. **Permission errors**
   - Check D-Bus session bus permissions
   - Ensure running in same user session

3. **Method call failures**
   - Verify method signatures
   - Check instance ID format
   - Test with surf-dbus.sh first

### Debug Tools

```bash
# Monitor all D-Bus activity
dbus-monitor --session

# Check surf service
dbus-send --session --dest=org.freedesktop.DBus \
          --print-reply /org/freedesktop/DBus \
          org.freedesktop.DBus.ListNames | grep surf

# Test basic connectivity
surf-dbus.sh list
```

## Best Practices

1. **Always check instance existence** before sending commands
2. **Handle D-Bus errors gracefully** in your scripts
3. **Use surf-dbus.sh** as reference implementation
4. **Test with both backends** if maintaining compatibility
5. **Cache instance IDs** when using same instance multiple times

## Future Enhancements

Potential future improvements to the external tool interface:

- Window focus control
- Tab management (when process-based tabbing is implemented)
- Screenshot capture
- Download management
- Cookie and storage management