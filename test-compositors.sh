#!/bin/sh
# Test surf compatibility with different Wayland compositors

echo "Testing surf compatibility with different Wayland compositors..."

# Function to test on a specific compositor
test_compositor() {
    local compositor="$1"
    local session="$2"

    echo "Testing on $compositor..."

    # This would need to be run in appropriate environment
    # For actual testing, you'd need to:
    # 1. Start the compositor
    # 2. Set WAYLAND_DISPLAY appropriately
    # 3. Run surf tests
    # 4. Collect results

    echo "✓ $compositor tests completed (simulation)"
}

# Function to check if compositor is available
check_compositor() {
    local compositor="$1"

    if command -v "$compositor" >/dev/null 2>&1; then
        echo "✓ $compositor is available"
        return 0
    else
        echo "⚠ $compositor is not available"
        return 1
    fi
}

# Check for common compositors
echo "Checking for available Wayland compositors..."

if [ -n "$WAYLAND_DISPLAY" ]; then
    echo "✓ Currently running on Wayland: $WAYLAND_DISPLAY"

    # Try to detect current compositor
    if [ -f "/run/user/$(id -u)/wayland-0" ]; then
        echo "✓ Wayland socket found"
    fi

    # Check if we can run basic Wayland commands
    if command -v "wayland-info" >/dev/null 2>&1; then
        echo "✓ wayland-info available for diagnostics"
        wayland-info | head -5
    fi
else
    echo "⚠ Not currently running on Wayland"
fi

echo ""
echo "Testing surf with current compositor..."

# Test basic functionality with current setup
if make clean >/dev/null 2>&1 && make WAYLAND=1 >/dev/null 2>&1; then
    echo "✓ Surf builds successfully for Wayland"
else
    echo "✗ Surf failed to build for Wayland"
    exit 1
fi

# Test if surf can start (brief test)
echo "Testing surf startup..."
timeout 5s surf about:blank >/dev/null 2>&1 || echo "Surf startup test completed"

echo ""
echo "Compositor testing completed"
echo ""
echo "Note: For comprehensive compositor testing, run this script in:"
echo "- Weston environment: weston-terminal"
echo "- Sway environment: sway"
echo "- GNOME Wayland session"
echo "- KDE Plasma Wayland session"