#!/bin/bash
#
# install_service.sh - Install Keyhunt as a systemd service
#
# Usage:
#   sudo ./install_service.sh coordinator
#   sudo ./install_service.sh worker <coordinator_ip:port> [auth_token]
#

set -e

# Check root
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

# Configuration
INSTALL_DIR="/opt/keyhunt"
CONFIG_DIR="/etc/keyhunt"
LOG_DIR="/var/log/keyhunt"
SERVICE_USER="keyhunt"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

print_header() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${NC}  KEYHUNT SERVICE INSTALLER                                 ${CYAN}║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

create_user() {
    if ! id "$SERVICE_USER" &>/dev/null; then
        echo "Creating service user: $SERVICE_USER"
        useradd --system --no-create-home --shell /sbin/nologin "$SERVICE_USER"
    fi
}

create_directories() {
    echo "Creating directories..."
    mkdir -p "$INSTALL_DIR"
    mkdir -p "$CONFIG_DIR"
    mkdir -p "$LOG_DIR"

    chown "$SERVICE_USER:$SERVICE_USER" "$INSTALL_DIR"
    chown "$SERVICE_USER:$SERVICE_USER" "$CONFIG_DIR"
    chown "$SERVICE_USER:$SERVICE_USER" "$LOG_DIR"
}

install_binary() {
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local keyhunt_binary="$script_dir/../keyhunt"

    if [[ -f "$keyhunt_binary" ]]; then
        echo "Installing keyhunt binary..."
        cp "$keyhunt_binary" "$INSTALL_DIR/keyhunt"
        chmod +x "$INSTALL_DIR/keyhunt"
        chown "$SERVICE_USER:$SERVICE_USER" "$INSTALL_DIR/keyhunt"
    else
        echo -e "${RED}Warning: keyhunt binary not found at $keyhunt_binary${NC}"
        echo "Please compile keyhunt first with 'make' and run this script again"
    fi
}

install_coordinator() {
    echo "Installing coordinator service..."

    # Copy service file
    cp "$(dirname "$0")/keyhunt-coordinator.service" /etc/systemd/system/

    # Reload systemd
    systemctl daemon-reload

    echo ""
    echo -e "${GREEN}Coordinator service installed!${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Run the wizard to create keyhunt_wizard.json:"
    echo "     sudo -u $SERVICE_USER $INSTALL_DIR/keyhunt --wizard"
    echo ""
    echo "  2. Copy the config to the install directory:"
    echo "     sudo cp keyhunt_wizard.json $INSTALL_DIR/"
    echo "     sudo chown $SERVICE_USER:$SERVICE_USER $INSTALL_DIR/keyhunt_wizard.json"
    echo ""
    echo "  3. Enable and start the service:"
    echo "     sudo systemctl enable keyhunt-coordinator"
    echo "     sudo systemctl start keyhunt-coordinator"
    echo ""
    echo "  4. Check status:"
    echo "     sudo systemctl status keyhunt-coordinator"
    echo "     sudo journalctl -u keyhunt-coordinator -f"
}

install_worker() {
    local coordinator="$1"
    local auth_token="$2"

    if [[ -z "$coordinator" ]]; then
        echo -e "${RED}Error: Coordinator address required${NC}"
        echo "Usage: $0 worker <coordinator_ip:port> [auth_token]"
        exit 1
    fi

    echo "Installing worker service..."

    # Parse host and port
    local host="${coordinator%:*}"
    local port="${coordinator#*:}"
    if [[ "$host" == "$port" ]]; then
        port="7777"
    fi

    # Create service file with configured coordinator
    sed -e "s/COORDINATOR_HOST=.*/COORDINATOR_HOST=$host/" \
        -e "s/COORDINATOR_PORT=.*/COORDINATOR_PORT=$port/" \
        "$(dirname "$0")/keyhunt-worker.service" > /etc/systemd/system/keyhunt-worker.service

    # Create environment file for auth token
    if [[ -n "$auth_token" ]]; then
        echo "KEYHUNT_AUTH_TOKEN=$auth_token" > "$CONFIG_DIR/worker.env"
        chmod 600 "$CONFIG_DIR/worker.env"
        chown "$SERVICE_USER:$SERVICE_USER" "$CONFIG_DIR/worker.env"
    fi

    # Reload systemd
    systemctl daemon-reload

    echo ""
    echo -e "${GREEN}Worker service installed!${NC}"
    echo ""
    echo "Configuration:"
    echo "  Coordinator: $host:$port"
    if [[ -n "$auth_token" ]]; then
        echo "  Auth token: (configured in $CONFIG_DIR/worker.env)"
    fi
    echo ""
    echo "Commands:"
    echo "  Start:   sudo systemctl start keyhunt-worker"
    echo "  Stop:    sudo systemctl stop keyhunt-worker"
    echo "  Enable:  sudo systemctl enable keyhunt-worker"
    echo "  Status:  sudo systemctl status keyhunt-worker"
    echo "  Logs:    sudo journalctl -u keyhunt-worker -f"
}

uninstall() {
    echo "Uninstalling keyhunt services..."

    # Stop and disable services
    systemctl stop keyhunt-coordinator 2>/dev/null || true
    systemctl stop keyhunt-worker 2>/dev/null || true
    systemctl disable keyhunt-coordinator 2>/dev/null || true
    systemctl disable keyhunt-worker 2>/dev/null || true

    # Remove service files
    rm -f /etc/systemd/system/keyhunt-coordinator.service
    rm -f /etc/systemd/system/keyhunt-worker.service

    systemctl daemon-reload

    echo ""
    echo -e "${GREEN}Services uninstalled${NC}"
    echo "Note: Install directory $INSTALL_DIR was not removed"
}

# Main
main() {
    print_header

    if [[ $# -lt 1 ]]; then
        echo "Usage: $0 <coordinator|worker|uninstall> [options]"
        echo ""
        echo "Commands:"
        echo "  coordinator              Install coordinator service"
        echo "  worker <host:port> [token]  Install worker service"
        echo "  uninstall               Remove all keyhunt services"
        exit 1
    fi

    local command="$1"
    shift

    create_user
    create_directories
    install_binary

    case "$command" in
        coordinator)
            install_coordinator
            ;;
        worker)
            install_worker "$@"
            ;;
        uninstall)
            uninstall
            ;;
        *)
            echo -e "${RED}Unknown command: $command${NC}"
            exit 1
            ;;
    esac
}

main "$@"
