#!/bin/sh
# Test external tool compatibility

echo "Testing external tool compatibility..."

# Test surf-open.sh
echo "Testing surf-open.sh..."
if [ -f "./surf-open.sh" ]; then
    echo "✓ surf-open.sh exists"

    # Test script syntax
    if sh -n ./surf-open.sh 2>/dev/null; then
        echo "✓ surf-open.sh syntax is valid"
    else
        echo "✗ surf-open.sh has syntax errors"
    fi
else
    echo "✗ surf-open.sh not found"
fi

# Test surf-dbus.sh
echo "Testing surf-dbus.sh..."
if [ -f "./surf-dbus.sh" ]; then
    echo "✓ surf-dbus.sh exists"

    # Test help functionality
    if ./surf-dbus.sh help >/dev/null 2>&1; then
        echo "✓ surf-dbus.sh help works"
    else
        echo "⚠ surf-dbus.sh help may have issues"
    fi

    # Test list functionality (may fail if surf not running)
    echo "Testing surf-dbus.sh list (may fail if no surf instances)..."
    ./surf-dbus.sh list >/dev/null 2>&1 && echo "✓ surf-dbus.sh list works" || echo "⚠ surf-dbus.sh list failed (expected if no surf running)"
else
    echo "✗ surf-dbus.sh not found"
fi

# Test migration documentation
echo "Testing migration documentation..."
if [ -f "./MIGRATION.md" ]; then
    echo "✓ MIGRATION.md exists"
else
    echo "✗ MIGRATION.md not found"
fi

# Test D-Bus interface definition
echo "Testing D-Bus interface definition..."
if [ -f "./dbus-interface.xml" ]; then
    echo "✓ dbus-interface.xml exists"

    # Basic XML syntax check
    if command -v xmllint >/dev/null 2>&1; then
        if xmllint --noout ./dbus-interface.xml 2>/dev/null; then
            echo "✓ dbus-interface.xml is valid XML"
        else
            echo "⚠ dbus-interface.xml may have XML issues"
        fi
    else
        echo "⚠ xmllint not available for XML validation"
    fi
else
    echo "✗ dbus-interface.xml not found"
fi

# Test for required build dependencies
echo "Testing build dependencies..."

# Check for pkg-config
if command -v pkg-config >/dev/null 2>&1; then
    echo "✓ pkg-config is available"
else
    echo "✗ pkg-config not found"
fi

# Check for common Wayland packages
for pkg in wayland-client wayland-cursor dbus-1 gtk+-3.0; do
    if pkg-config --exists $pkg 2>/dev/null; then
        echo "✓ $pkg development files found"
    else
        echo "⚠ $pkg development files not found"
    fi
done

# Test build system
echo "Testing build system..."
if [ -f "Makefile" ]; then
    echo "✓ Makefile exists"

    # Test clean build
    if make clean >/dev/null 2>&1; then
        echo "✓ make clean works"
    else
        echo "⚠ make clean had issues"
    fi

    # Test Wayland build (may fail due to dependencies)
    echo "Testing Wayland build..."
    if make WAYLAND=1 >/dev/null 2>&1; then
        echo "✓ Wayland build successful"
        make clean >/dev/null 2>&1
    else
        echo "⚠ Wayland build failed (may be due to missing dependencies)"
    fi

    # Test X11 build
    echo "Testing X11 build..."
    if make X11=1 >/dev/null 2>&1; then
        echo "✓ X11 build successful"
        make clean >/dev/null 2>&1
    else
        echo "⚠ X11 build failed (may be due to missing dependencies)"
    fi
else
    echo "✗ Makefile not found"
fi

echo ""
echo "External tool testing completed"