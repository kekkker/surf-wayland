#!/bin/sh
# Performance and memory testing

echo "Starting performance tests..."

# Function to measure memory usage
measure_memory() {
    local pid="$1"
    if kill -0 "$pid" 2>/dev/null; then
        ps -p "$pid" -o rss= 2>/dev/null | awk '{print $1}' || echo "0"
    else
        echo "0"
    fi
}

# Function to count running surf instances
count_surf_instances() {
    pgrep -f "^surf" | wc -l
}

# Test memory usage with single instance
echo "Testing memory usage with single instance..."
surf about:blank &
SURF_PID=$!
sleep 3

if kill -0 $SURF_PID 2>/dev/null; then
    MEMORY_SINGLE=$(measure_memory $SURF_PID)
    if [ "$MEMORY_SINGLE" != "0" ]; then
        echo "✓ Single instance memory: ${MEMORY_SINGLE}KB"
    else
        echo "⚠ Could not measure memory usage"
    fi
else
    echo "✗ Surf instance failed to start"
    exit 1
fi

# Test memory usage with multiple instances
echo "Testing memory usage with multiple instances..."
SURF_PIDS=""
for i in $(seq 1 5); do
    surf about:blank &
    SURF_PIDS="$SURF_PIDS $!"
    sleep 1
done

# Wait for all instances to stabilize
sleep 5

MEMORY_TOTAL=0
INSTANCE_COUNT=0
for pid in $SURF_PIDS; do
    if kill -0 $pid 2>/dev/null; then
        MEMORY=$(measure_memory $pid)
        if [ "$MEMORY" != "0" ]; then
            MEMORY_TOTAL=$((MEMORY_TOTAL + MEMORY))
            INSTANCE_COUNT=$((INSTANCE_COUNT + 1))
        fi
    fi
done

if [ $INSTANCE_COUNT -gt 0 ]; then
    echo "✓ $INSTANCE_COUNT instances total memory: ${MEMORY_TOTAL}KB"
    AVG_MEMORY=$((MEMORY_TOTAL / INSTANCE_COUNT))
    echo "✓ Average memory per instance: ${AVG_MEMORY}KB"
else
    echo "⚠ Could not measure multi-instance memory usage"
fi

# Test startup time
echo "Testing startup time..."
START_TIME=$(date +%s%N)
surf about:blank &
SURF_PID=$!
wait $SURF_PID 2>/dev/null || true
END_TIME=$(date +%s%N)

if [ $START_TIME -lt $END_TIME ]; then
    STARTUP_TIME=$(( (END_TIME - START_TIME) / 1000000 ))
    echo "✓ Startup time: ${STARTUP_TIME}ms"
else
    echo "⚠ Could not measure startup time accurately"
fi

# Test D-Bus performance (if available)
echo "Testing D-Bus performance..."
if command -v dbus-send >/dev/null 2>&1; then
    # Start a surf instance for D-Bus testing
    surf about:blank &
    SURF_PID=$!
    sleep 3

    if kill -0 $SURF_PID 2>/dev/null; then
        # Measure D-Bus call time
        DBUS_START=$(date +%s%N)
        dbus-send --session --dest=org.freedesktop.DBus --print-reply \
                  /org/freedesktop/DBus org.freedesktop.DBus.ListNames \
                  >/dev/null 2>&1
        DBUS_END=$(date +%s%N)

        if [ $DBUS_START -lt $DBUS_END ]; then
            DBUS_TIME=$(( (DBUS_END - DBUS_START) / 1000000 ))
            echo "✓ D-Bus call time: ${DBUS_TIME}ms"
        else
            echo "⚠ Could not measure D-Bus call time"
        fi
    fi
else
    echo "⚠ dbus-send not available for D-Bus performance testing"
fi

# Cleanup
echo "Cleaning up test instances..."
for pid in $SURF_PIDS; do
    kill $pid 2>/dev/null || true
done
kill $SURF_PID 2>/dev/null || true
pkill -f "^surf" 2>/dev/null || true

# Wait for processes to terminate
sleep 2

# Final cleanup check
REMAINING=$(count_surf_instances)
if [ $REMAINING -gt 0 ]; then
    echo "⚠ $REMAINING surf instances still running after cleanup"
    pkill -9 -f "^surf" 2>/dev/null || true
else
    echo "✓ All test instances cleaned up successfully"
fi

echo ""
echo "Performance testing completed"

# Basic performance summary
echo ""
echo "Performance Summary:"
echo "- Single instance startup and memory measured"
echo "- Multi-instance memory usage measured"
echo "- D-Bus performance tested (if available)"
echo "- All processes cleaned up"