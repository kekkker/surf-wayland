#!/bin/sh
# D-Bus automation script for surf

command="$1"
instance_id="$2"
argument="$3"

show_usage() {
    echo "Usage: $0 {list|uri|go|find} [instance_id] [argument]"
    echo ""
    echo "Commands:"
    echo "  list                    - List all running surf instances"
    echo "  uri <instance_id>       - Get current URI of instance"
    echo "  go <instance_id> <url>  - Navigate instance to URL"
    echo "  find <instance_id> <text> - Find text on current page"
    echo ""
    echo "Examples:"
    echo "  $0 list"
    echo "  $0 uri surf-browser-1234-5678"
    echo "  $0 go surf-browser-1234-5678 https://example.com"
    echo "  $0 find surf-browser-1234-5678 search text"
}

case "$command" in
    list)
        dbus-send --session --dest=org.suckless.surf --print-reply \
                  /org/suckless/surf org.suckless.surf.ListInstances 2>/dev/null | \
            awk '/string/ {print $2}' | tr -d '"'
        ;;
    uri)
        if [ -z "$instance_id" ]; then
            echo "Error: Instance ID required"
            echo "Usage: $0 uri <instance_id>"
            echo ""
            echo "Available instances:"
            $0 list
            exit 1
        fi
        result=$(dbus-send --session --dest=org.suckless.surf --print-reply \
                  /org/suckless/surf org.suckless.surf.GetURI \
                  string:"$instance_id" 2>/dev/null | \
            awk '/string/ {print $2}' | tr -d '"')
        if [ -z "$result" ]; then
            echo "Error: Instance '$instance_id' not found or not responding"
            exit 1
        fi
        echo "$result"
        ;;
    go)
        if [ -z "$instance_id" ] || [ -z "$argument" ]; then
            echo "Error: Instance ID and URI required"
            echo "Usage: $0 go <instance_id> <uri>"
            echo ""
            echo "Available instances:"
            $0 list
            exit 1
        fi
        dbus-send --session --dest=org.suckless.surf \
                  /org/suckless/surf org.suckless.surf.Go \
                  string:"$instance_id" string:"$argument" 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "Navigation command sent to instance '$instance_id'"
        else
            echo "Error: Failed to navigate instance '$instance_id'"
            exit 1
        fi
        ;;
    find)
        if [ -z "$instance_id" ] || [ -z "$argument" ]; then
            echo "Error: Instance ID and search text required"
            echo "Usage: $0 find <instance_id> <text>"
            echo ""
            echo "Available instances:"
            $0 list
            exit 1
        fi
        dbus-send --session --dest=org.suckless.surf \
                  /org/suckless/surf org.suckless.surf.Find \
                  string:"$instance_id" string:"$argument" 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "Search command sent to instance '$instance_id'"
        else
            echo "Error: Failed to search in instance '$instance_id'"
            exit 1
        fi
        ;;
    help|--help|-h)
        show_usage
        ;;
    *)
        echo "Error: Unknown command '$command'"
        echo ""
        show_usage
        exit 1
        ;;
esac