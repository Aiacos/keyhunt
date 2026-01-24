#!/bin/bash
#
# deploy_workers.sh - Automatic Worker Deployment for Keyhunt Distributed Mode
#
# Usage:
#   ./deploy_workers.sh deploy <hosts_file> <coordinator_ip:port> [auth_token]
#   ./deploy_workers.sh start <hosts_file> <coordinator_ip:port> [auth_token]
#   ./deploy_workers.sh stop <hosts_file>
#   ./deploy_workers.sh status <hosts_file>
#
# Hosts file format (one per line):
#   user@hostname
#   user@ip_address
#   # Comments are allowed
#

set -e

# Configuration
KEYHUNT_BINARY="./keyhunt"
REMOTE_DIR="\$HOME/keyhunt"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10 -o BatchMode=yes"
PARALLEL_JOBS=10

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${NC}  KEYHUNT WORKER DEPLOYMENT TOOL                            ${CYAN}║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_usage() {
    echo "Usage: $0 <command> <hosts_file> [options]"
    echo ""
    echo "Commands:"
    echo "  deploy <hosts_file> <coordinator:port> [auth_token]"
    echo "      Copy keyhunt binary and start workers on all hosts"
    echo ""
    echo "  start <hosts_file> <coordinator:port> [auth_token]"
    echo "      Start workers on hosts (assumes keyhunt is already deployed)"
    echo ""
    echo "  stop <hosts_file>"
    echo "      Stop all running workers on hosts"
    echo ""
    echo "  status <hosts_file>"
    echo "      Check worker status on all hosts"
    echo ""
    echo "Hosts file format:"
    echo "  user@hostname"
    echo "  user@192.168.1.100"
    echo "  # Comments are ignored"
    echo ""
    echo "Example:"
    echo "  $0 deploy workers.txt 192.168.1.1:7777 my-secret-token"
}

# Read hosts from file, skipping comments and empty lines
read_hosts() {
    local file="$1"
    if [[ ! -f "$file" ]]; then
        echo -e "${RED}Error: Hosts file not found: $file${NC}" >&2
        exit 1
    fi
    grep -v '^#' "$file" | grep -v '^[[:space:]]*$'
}

# Check SSH connectivity
check_ssh() {
    local host="$1"
    ssh $SSH_OPTS "$host" "echo ok" &>/dev/null
    return $?
}

# Deploy keyhunt to a single host
deploy_single() {
    local host="$1"
    local coordinator="$2"
    local auth_token="$3"

    echo -e "  [${CYAN}DEPLOY${NC}] $host"

    # Check SSH connectivity
    if ! check_ssh "$host"; then
        echo -e "  [${RED}FAILED${NC}] $host - SSH connection failed"
        return 1
    fi

    # Create remote directory
    ssh $SSH_OPTS "$host" "mkdir -p $REMOTE_DIR" 2>/dev/null

    # Copy binary
    if [[ -f "$KEYHUNT_BINARY" ]]; then
        scp $SSH_OPTS "$KEYHUNT_BINARY" "$host:$REMOTE_DIR/keyhunt" 2>/dev/null
        ssh $SSH_OPTS "$host" "chmod +x $REMOTE_DIR/keyhunt" 2>/dev/null
    else
        echo -e "  [${YELLOW}WARN${NC}] $host - Binary not found locally, assuming already deployed"
    fi

    # Start worker
    start_single "$host" "$coordinator" "$auth_token"
}

# Start worker on a single host
start_single() {
    local host="$1"
    local coordinator="$2"
    local auth_token="$3"

    echo -e "  [${CYAN}START${NC}] $host -> $coordinator"

    # Check SSH connectivity
    if ! check_ssh "$host"; then
        echo -e "  [${RED}FAILED${NC}] $host - SSH connection failed"
        return 1
    fi

    # Build command with optional auth token
    local env_vars=""
    if [[ -n "$auth_token" ]]; then
        env_vars="KEYHUNT_AUTH_TOKEN='$auth_token'"
    fi

    # Stop any existing worker first
    ssh $SSH_OPTS "$host" "pkill -f 'keyhunt.*wizard-client' 2>/dev/null || true" 2>/dev/null

    # Start worker in background using nohup
    ssh $SSH_OPTS "$host" "cd $REMOTE_DIR && $env_vars nohup ./keyhunt --wizard-client $coordinator > worker.log 2>&1 &" 2>/dev/null

    # Verify it started
    sleep 1
    if ssh $SSH_OPTS "$host" "pgrep -f 'keyhunt.*wizard-client'" &>/dev/null; then
        echo -e "  [${GREEN}OK${NC}] $host - Worker started"
        return 0
    else
        echo -e "  [${RED}FAILED${NC}] $host - Worker failed to start"
        echo "    Check $host:$REMOTE_DIR/worker.log for details"
        return 1
    fi
}

# Stop worker on a single host
stop_single() {
    local host="$1"

    echo -e "  [${CYAN}STOP${NC}] $host"

    if ! check_ssh "$host"; then
        echo -e "  [${RED}FAILED${NC}] $host - SSH connection failed"
        return 1
    fi

    # Send SIGTERM first for graceful shutdown
    ssh $SSH_OPTS "$host" "pkill -TERM -f 'keyhunt.*wizard-client' 2>/dev/null || true" 2>/dev/null
    sleep 2

    # Force kill if still running
    ssh $SSH_OPTS "$host" "pkill -KILL -f 'keyhunt.*wizard-client' 2>/dev/null || true" 2>/dev/null

    echo -e "  [${GREEN}OK${NC}] $host - Worker stopped"
}

# Check status on a single host
status_single() {
    local host="$1"

    if ! check_ssh "$host"; then
        echo -e "  [${RED}OFFLINE${NC}] $host - SSH connection failed"
        return 1
    fi

    local pid
    pid=$(ssh $SSH_OPTS "$host" "pgrep -f 'keyhunt.*wizard-client' 2>/dev/null" 2>/dev/null || true)

    if [[ -n "$pid" ]]; then
        # Get CPU usage
        local cpu
        cpu=$(ssh $SSH_OPTS "$host" "ps -p $pid -o %cpu= 2>/dev/null" 2>/dev/null | tr -d ' ')
        echo -e "  [${GREEN}RUNNING${NC}] $host - PID: $pid, CPU: ${cpu}%"
    else
        echo -e "  [${YELLOW}STOPPED${NC}] $host - No worker running"
    fi
}

# Main deployment function (parallel)
deploy_all() {
    local hosts_file="$1"
    local coordinator="$2"
    local auth_token="$3"

    echo -e "${GREEN}Deploying workers...${NC}"
    echo ""

    local hosts
    hosts=$(read_hosts "$hosts_file")
    local count=0
    local success=0

    for host in $hosts; do
        if deploy_single "$host" "$coordinator" "$auth_token"; then
            ((success++))
        fi
        ((count++))
    done

    echo ""
    echo -e "${GREEN}Deployment complete: $success/$count workers started${NC}"
}

# Start all workers (parallel)
start_all() {
    local hosts_file="$1"
    local coordinator="$2"
    local auth_token="$3"

    echo -e "${GREEN}Starting workers...${NC}"
    echo ""

    local hosts
    hosts=$(read_hosts "$hosts_file")
    local count=0
    local success=0

    for host in $hosts; do
        if start_single "$host" "$coordinator" "$auth_token"; then
            ((success++))
        fi
        ((count++))
    done

    echo ""
    echo -e "${GREEN}Start complete: $success/$count workers running${NC}"
}

# Stop all workers
stop_all() {
    local hosts_file="$1"

    echo -e "${YELLOW}Stopping workers...${NC}"
    echo ""

    local hosts
    hosts=$(read_hosts "$hosts_file")

    for host in $hosts; do
        stop_single "$host"
    done

    echo ""
    echo -e "${GREEN}All workers stopped${NC}"
}

# Check status of all workers
status_all() {
    local hosts_file="$1"

    echo -e "${CYAN}Worker Status:${NC}"
    echo ""

    local hosts
    hosts=$(read_hosts "$hosts_file")
    local running=0
    local stopped=0
    local failed=0

    for host in $hosts; do
        result=$(status_single "$host")
        echo "$result"

        if [[ "$result" == *"RUNNING"* ]]; then
            ((running++))
        elif [[ "$result" == *"STOPPED"* ]]; then
            ((stopped++))
        else
            ((failed++))
        fi
    done

    echo ""
    echo -e "Summary: ${GREEN}$running running${NC}, ${YELLOW}$stopped stopped${NC}, ${RED}$failed unreachable${NC}"
}

# Main
main() {
    print_header

    if [[ $# -lt 2 ]]; then
        print_usage
        exit 1
    fi

    local command="$1"
    local hosts_file="$2"
    local coordinator="${3:-}"
    local auth_token="${4:-}"

    case "$command" in
        deploy)
            if [[ -z "$coordinator" ]]; then
                echo -e "${RED}Error: Coordinator address required for deploy${NC}"
                print_usage
                exit 1
            fi
            deploy_all "$hosts_file" "$coordinator" "$auth_token"
            ;;
        start)
            if [[ -z "$coordinator" ]]; then
                echo -e "${RED}Error: Coordinator address required for start${NC}"
                print_usage
                exit 1
            fi
            start_all "$hosts_file" "$coordinator" "$auth_token"
            ;;
        stop)
            stop_all "$hosts_file"
            ;;
        status)
            status_all "$hosts_file"
            ;;
        *)
            echo -e "${RED}Unknown command: $command${NC}"
            print_usage
            exit 1
            ;;
    esac
}

main "$@"
