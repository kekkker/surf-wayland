#!/bin/sh
#
# See the LICENSE file for copyright and license details.
#
# Updated surf-open.sh for Wayland/D-Bus support

# Function to check if surf instance exists via D-Bus
surf_instance_exists() {
    local instance_id="$1"
    dbus-send --session --dest=org.suckless.surf --print-reply \
              /org/suckless/surf org.suckless.surf.ListInstances 2>/dev/null | \
        grep -q "$instance_id"
}

# Function to get URI from surf instance via D-Bus
surf_get_uri() {
    local instance_id="$1"
    dbus-send --session --dest=org.suckless.surf --print-reply \
              /org/suckless/surf org.suckless.surf.GetURI \
              string:"$instance_id" 2>/dev/null | \
        awk '/string/ {print $2}' | tr -d '"'
}

# Function to navigate surf instance via D-Bus
surf_go() {
    local instance_id="$1"
    local uri="$2"
    dbus-send --session --dest=org.suckless.surf \
              /org/suckless/surf org.suckless.surf.Go \
              string:"$instance_id" string:"$uri"
}

# Fallback to X11 for backwards compatibility
surf_open_x11() {
    xidfile="$HOME/tmp/tabbed-surf.xid"

    if [ ! -r "$xidfile" ]; then
        tabbed -dn tabbed-surf -r 2 surf -e '' "$uri" >"$xidfile" \
            2>/dev/null &
    else
        xid=$(cat "$xidfile")
        xprop -id "$xid" >/dev/null 2>&1
        if [ $? -gt 0 ]; then
            tabbed -dn tabbed-surf -r 2 surf -e '' "$uri" >"$xidfile" \
                2>/dev/null &
        else
            surf -e "$xid" "$uri" >/dev/null 2>&1 &
        fi
    fi
}

# Main logic
uri="$1"
if [ -z "$uri" ]; then
    uri="about:blank"
fi

# Check if we're running under Wayland and have D-Bus support
if [ -n "$WAYLAND_DISPLAY" ] && command -v dbus-send >/dev/null 2>&1; then
    # Try to find existing surf instance via D-Bus
    instance_id=$(dbus-send --session --dest=org.suckless.surf --print-reply \
                           /org/suckless/surf org.suckless.surf.ListInstances 2>/dev/null | \
                  awk '/string/ {print $2}' | tr -d '"' | head -1)

    if [ -n "$instance_id" ] && surf_instance_exists "$instance_id"; then
        # Navigate existing instance
        surf_go "$instance_id" "$uri"
    else
        # Launch new instance
        surf "$uri" &
    fi
else
    # Fallback to X11 workflow
    surf_open_x11
fi

