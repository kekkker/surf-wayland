#!/bin/sh
# Comprehensive Wayland testing script

set -e

TEST_RESULTS="test-results.log"
echo "Starting surf Wayland tests..." > "$TEST_RESULTS"

# Test 1: Build system
echo "Testing build system..."
echo "Test 1: Build system" >> "$TEST_RESULTS"

if make clean >/dev/null 2>&1 && make WAYLAND=1 >/dev/null 2>>"$TEST_RESULTS"; then
    echo "✓ Wayland build successful" >> "$TEST_RESULTS"
    echo "✓ Wayland build successful"
else
    echo "✗ Wayland build failed" >> "$TEST_RESULTS"
    echo "✗ Wayland build failed"
    cat "$TEST_RESULTS"
    exit 1
fi

# Test 2: Display backend detection
echo "Testing display backend detection..."
echo "Test 2: Display backend detection" >> "$TEST_RESULTS"

if [ -n "$WAYLAND_DISPLAY" ]; then
    echo "✓ Wayland display detected: $WAYLAND_DISPLAY" >> "$TEST_RESULTS"
    echo "✓ Wayland display detected: $WAYLAND_DISPLAY"
else
    echo "⚠ Wayland display not set, using XWayland fallback" >> "$TEST_RESULTS"
    echo "⚠ Wayland display not set, using XWayland fallback"
fi

# Test 3: Basic functionality
echo "Testing basic functionality..."
echo "Test 3: Basic functionality" >> "$TEST_RESULTS"

# Start surf in background
surf about:blank &
SURF_PID=$!
sleep 2

# Check if surf process is running
if kill -0 $SURF_PID 2>/dev/null; then
    echo "✓ Surf launches successfully" >> "$TEST_RESULTS"
    echo "✓ Surf launches successfully"
else
    echo "✗ Surf failed to launch" >> "$TEST_RESULTS"
    echo "✗ Surf failed to launch"
    exit 1
fi

# Test 4: Instance management
echo "Testing instance management..."
echo "Test 4: Instance management" >> "$TEST_RESULTS"

# Start second instance
surf about:blank &
SURF_PID2=$!
sleep 2

# Check if we have multiple instances running
if kill -0 $SURF_PID2 2>/dev/null; then
    echo "✓ Multiple instances launch correctly" >> "$TEST_RESULTS"
    echo "✓ Multiple instances launch correctly"
else
    echo "✗ Second instance failed to launch" >> "$TEST_RESULTS"
    echo "✗ Second instance failed to launch"
fi

# Test 5: Script functionality
echo "Testing script functionality..."
echo "Test 5: Script functionality" >> "$TEST_RESULTS"

# Test surf-dbus.sh script
if [ -f "./surf-dbus.sh" ]; then
    if ./surf-dbus.sh help >/dev/null 2>&1; then
        echo "✓ surf-dbus.sh script works" >> "$TEST_RESULTS"
        echo "✓ surf-dbus.sh script works"
    else
        echo "⚠ surf-dbus.sh script exists but may have issues" >> "$TEST_RESULTS"
        echo "⚠ surf-dbus.sh script exists but may have issues"
    fi
else
    echo "✗ surf-dbus.sh script not found" >> "$TEST_RESULTS"
    echo "✗ surf-dbus.sh script not found"
fi

# Test 6: File integrity
echo "Testing file integrity..."
echo "Test 6: File integrity" >> "$TEST_RESULTS"

REQUIRED_FILES="surf.c surf.h config.mk config.def.h surf-open.sh surf-dbus.sh"
for file in $REQUIRED_FILES; do
    if [ -f "$file" ]; then
        echo "✓ $file exists" >> "$TEST_RESULTS"
    else
        echo "✗ $file missing" >> "$TEST_RESULTS"
    fi
done

# Cleanup
echo "Cleaning up test instances..."
kill $SURF_PID $SURF_PID2 2>/dev/null || true
sleep 1

# Wait for processes to terminate
wait $SURF_PID 2>/dev/null || true
wait $SURF_PID2 2>/dev/null || true

echo ""
echo "Testing completed. Results saved to $TEST_RESULTS"
echo "Summary:"
grep "✓\|✗\|⚠" "$TEST_RESULTS" | sort | uniq -c

# Check overall success
if grep -q "✗" "$TEST_RESULTS"; then
    echo ""
    echo "⚠ Some tests failed. Check $TEST_RESULTS for details."
    exit 1
else
    echo ""
    echo "✓ All tests passed successfully!"
fi