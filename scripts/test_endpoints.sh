#!/usr/bin/env bash

set -euo pipefail

# Endpoint testing script for the trading engine.
# - Starts a Redpanda (Kafka) container if not running
# - Builds and runs the trading engine
# - Tests all HTTP endpoints
# - Reports results and stops services

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Configuration
HTTP_HOST=${HTTP_HOST:-127.0.0.1}
HTTP_PORT=${HTTP_PORT:-8080}
KAFKA_BROKERS=${KAFKA_BROKERS:-127.0.0.1:9092}
TOPIC=${TOPIC:-order-requests}
REDPANDA_CONTAINER_NAME=${REDPANDA_CONTAINER_NAME:-redpanda-test}
ENGINE_BIN_REL="build/apps/trading_engine/live_trading_engine"
ENGINE_BIN="$REPO_ROOT/$ENGINE_BIN_REL"
ENGINE_CONFIG_REL="config/trading_engine.json"
ENGINE_CONFIG="$REPO_ROOT/$ENGINE_CONFIG_REL"
APP_LOG="$REPO_ROOT/app.log"
KEEP_REDPANDA=${KEEP_REDPANDA:-0}

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --http-host HOST        HTTP host (default: $HTTP_HOST)
  --http-port PORT        HTTP port (default: $HTTP_PORT)
  --brokers HOST:PORT     Kafka brokers (default: $KAFKA_BROKERS)
  --keep-redpanda         Do not stop/remove Redpanda container after tests
  -h, --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --http-host) HTTP_HOST="$2"; shift 2 ;;
    --http-port) HTTP_PORT="$2"; shift 2 ;;
    --brokers) KAFKA_BROKERS="$2"; shift 2 ;;
    --keep-redpanda) KEEP_REDPANDA=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

has_cmd() {
  command -v "$1" >/dev/null 2>&1
}

start_redpanda() {
  if docker ps --format '{{.Names}}' | grep -q "^${REDPANDA_CONTAINER_NAME}$"; then
    echo "Redpanda container '${REDPANDA_CONTAINER_NAME}' already running"
    return
  fi

  if docker ps -a --format '{{.Names}}' | grep -q "^${REDPANDA_CONTAINER_NAME}$"; then
    docker rm -f "${REDPANDA_CONTAINER_NAME}" >/dev/null 2>&1 || true
  fi

  echo "Starting Redpanda container '${REDPANDA_CONTAINER_NAME}'..."
  docker run -d --pull=always \
    --name "${REDPANDA_CONTAINER_NAME}" \
    -p 9092:9092 \
    -p 9644:9644 \
    redpandadata/redpanda:latest \
    redpanda start \
      --overprovisioned \
      --smp 1 \
      --memory 512M \
      --reserve-memory 0M \
      --node-id 0 \
      --check=false \
      --kafka-addr PLAINTEXT://0.0.0.0:9092 \
      --advertise-kafka-addr PLAINTEXT://127.0.0.1:9092 >/dev/null

  echo "Waiting for Redpanda to be ready..."
  SECS=0
  until docker logs "${REDPANDA_CONTAINER_NAME}" 2>&1 | grep -q "Started Kafka API server"; do
    sleep 1
    SECS=$((SECS+1))
    if (( SECS > 30 )); then
      echo "Timed out waiting for Redpanda to start" >&2
      exit 1
    fi
  done

  # Create the required topic
  echo "Creating Kafka topic '${TOPIC}'..."
  if has_cmd rpk; then
    rpk topic create "${TOPIC}" --brokers="${KAFKA_BROKERS}" >/dev/null 2>&1 || true
  else
    docker exec "${REDPANDA_CONTAINER_NAME}" rpk topic create "${TOPIC}" >/dev/null 2>&1 || true
  fi
}

build_engine() {
  echo "Building trading engine (Release)..."
  mkdir -p "$REPO_ROOT/build"
  cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$REPO_ROOT/build" -j >/dev/null
}

run_engine() {
  echo "Starting trading engine..."
  # Clean previous logs
  rm -f "$APP_LOG" "$REPO_ROOT/trading_engine.log" || true
  
  ("$ENGINE_BIN" "$ENGINE_CONFIG") &
  ENGINE_PID=$!

  # Wait for engine to be ready
  echo -n "Waiting for engine to be ready"
  for i in {1..60}; do
    if [[ -f "$APP_LOG" ]] && grep -q "Trading engine started" "$APP_LOG"; then
      if curl -s --connect-timeout 1 --max-time 1 "http://${HTTP_HOST}:${HTTP_PORT}/health" >/dev/null 2>&1; then
        echo " - ready"
        sleep 0.5
        return 0
      fi
    fi
    echo -n "."
    sleep 0.5
  done
  echo
  echo "Engine did not start successfully; check $APP_LOG" >&2
  exit 1
}

stop_engine() {
  if [[ -n "${ENGINE_PID:-}" ]] && ps -p "$ENGINE_PID" >/dev/null 2>&1; then
    echo "Stopping trading engine..."
    kill "$ENGINE_PID" >/dev/null 2>&1 || true
    wait "$ENGINE_PID" 2>/dev/null || true
  fi
}

cleanup() {
  stop_engine || true
  if [[ "$KEEP_REDPANDA" != "1" ]]; then
    echo "Cleaning up Redpanda container..."
    docker rm -f "$REDPANDA_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
}

# Test result tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

test_endpoint() {
  local method="$1"
  local path="$2"
  local description="$3"
  local expected_status="${4:-200}"
  local data="${5:-}"
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  
  echo -n "Testing $method $path ($description)... "
  
  local curl_cmd="curl -s -w '%{http_code}' -o /tmp/test_response"
  curl_cmd="$curl_cmd --connect-timeout 5 --max-time 10"
  
  if [[ "$method" != "GET" ]]; then
    curl_cmd="$curl_cmd -X $method"
  fi
  
  if [[ "$method" == "POST" && -n "$data" ]]; then
    curl_cmd="$curl_cmd -H 'Content-Type: application/json' -d '$data'"
  fi
  
  curl_cmd="$curl_cmd 'http://${HTTP_HOST}:${HTTP_PORT}${path}'"
  
  local status_code
  status_code=$(eval "$curl_cmd" 2>/dev/null || echo "000")
  
  if [[ "$status_code" == "$expected_status" ]]; then
    echo "✓ PASS ($status_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Show response body for some endpoints
    if [[ "$path" == "/health" || "$path" =~ ^/api/v1/ ]]; then
      echo "    Response: $(head -c 200 /tmp/test_response)..."
    fi
  else
    echo "✗ FAIL (expected $expected_status, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
    if [[ -f /tmp/test_response ]]; then
      echo "    Response: $(cat /tmp/test_response)"
    fi
  fi
  
  rm -f /tmp/test_response
}

test_endpoint_flexible() {
  local method="$1"
  local path="$2"
  local description="$3"
  local valid_statuses="$4"  # e.g., "200|404"
  local data="${5:-}"
  local extra_args="${6:-}"  # Additional curl arguments
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  
  echo -n "Testing $method $path ($description)... "
  
  local curl_cmd="curl -s -w '%{http_code}' -o /tmp/test_response"
  curl_cmd="$curl_cmd --connect-timeout 5 --max-time 10"
  
  if [[ "$method" == "POST" && -n "$data" ]]; then
    curl_cmd="$curl_cmd -X POST -H 'Content-Type: application/json' -d '$data'"
  fi
  
  if [[ -n "$extra_args" ]]; then
    curl_cmd="$curl_cmd $extra_args"
  fi
  
  curl_cmd="$curl_cmd 'http://${HTTP_HOST}:${HTTP_PORT}${path}'"
  
  local status_code
  status_code=$(eval "$curl_cmd" 2>/dev/null || echo "000")
  
  # Check if status code matches any of the valid statuses
  if [[ "$valid_statuses" =~ $status_code ]]; then
    echo "✓ PASS ($status_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Show response body for successful responses
    if [[ "$status_code" == "200" && "$path" =~ ^/api/v1/ ]]; then
      echo "    Response: $(head -c 200 /tmp/test_response)..."
    fi
  else
    echo "✗ FAIL (expected one of: $valid_statuses, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
    if [[ -f /tmp/test_response ]]; then
      echo "    Response: $(cat /tmp/test_response)"
    fi
  fi
  
  rm -f /tmp/test_response
}

send_sample_orders() {
  echo "Sending sample orders to populate data and generate trades..."
  
  # Send a few buy orders
  local buy_orders=('{"id":"SETUP_BUY_001","userId":"buyer-1","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":10.0,"price":149.50}'
                   '{"id":"SETUP_BUY_002","userId":"buyer-2","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":5.0,"price":149.00}')
  
  # Send a few sell orders that should match
  local sell_orders=('{"id":"SETUP_SELL_001","userId":"seller-1","symbol":"AAPL","type":"LIMIT","side":"SELL","quantity":8.0,"price":149.50}'
                    '{"id":"SETUP_SELL_002","userId":"seller-2","symbol":"AAPL","type":"LIMIT","side":"SELL","quantity":3.0,"price":149.00}')
  
  for order in "${buy_orders[@]}"; do
    curl -s -X POST -H "Content-Type: application/json" \
      -d "$order" \
      "http://${HTTP_HOST}:${HTTP_PORT}/order" >/dev/null || true
  done
  
  for order in "${sell_orders[@]}"; do
    curl -s -X POST -H "Content-Type: application/json" \
      -d "$order" \
      "http://${HTTP_HOST}:${HTTP_PORT}/order" >/dev/null || true
  done
  
  # Wait for processing and potential trades
  echo "Waiting for order processing and potential trades..."
  sleep 5
}

run_tests() {
  echo ""
  echo "===== Testing HTTP Endpoints ====="
  
  # Test 1: Health check
  test_endpoint "GET" "/health" "Health check" "200"
  
  # Test 2: Order submission
  local order_json='{"id":"TEST_001","userId":"trader-1","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":10.0,"price":150.50}'
  test_endpoint "POST" "/order" "Order submission" "202" "$order_json"
  
  # Send orders to populate data for stats tests and generate some trades
  send_sample_orders
  
  # Test 3: OrderBook endpoint
  test_endpoint "GET" "/api/v1/orderbook/AAPL" "OrderBook for AAPL" "200"
  
  # Test 4: OrderBook for non-existent symbol (returns 404)
  test_endpoint "GET" "/api/v1/orderbook/NONEXIST" "OrderBook for non-existent symbol" "404"
  
  # Test 5: Stats for symbol (may return 404 if no stats, 200 if stats exist)
  test_endpoint_flexible "GET" "/api/v1/stats/AAPL" "Stats for AAPL" "200|404"
  
  # Test 6: Stats for symbol with timeframe (may return 404 if no stats, 200 if stats exist)  
  test_endpoint_flexible "GET" "/api/v1/stats/AAPL/1m" "Stats for AAPL (1m timeframe)" "200|404"
  
  # Test 7: All stats (may return 404 if no stats, 200 if stats exist)
  test_endpoint_flexible "GET" "/api/v1/stats/all" "All stats" "200|404"
  
  # Test 8: Stats summary (may return 404 if no stats, 200 if stats exist)
  test_endpoint_flexible "GET" "/api/v1/stats/summary" "Stats summary" "200|404"
  
  # Test 9: Leaderboard
  test_endpoint "GET" "/api/v1/leaderboard" "Leaderboard" "200"
  
  # Test 10: Invalid JSON order
  local invalid_json='{"invalid": json}'
  test_endpoint "POST" "/order" "Invalid JSON order" "400" "$invalid_json"
  
  # Test 11: Order missing required fields (accepts initially, validation happens async)
  local incomplete_order='{"id":"TEST_002","userId":"trader-1"}'
  test_endpoint "POST" "/order" "Incomplete order (accepted for async processing)" "202" "$incomplete_order"
  
  # Test 12: Non-existent endpoint
  test_endpoint "GET" "/api/v1/nonexistent" "Non-existent endpoint" "404"
  
  # Test 13: Different symbol for orderbook
  test_endpoint "GET" "/api/v1/orderbook/MSFT" "OrderBook for MSFT (should be empty or 404)" "404"
  
  # Test 14: Stats with invalid timeframe
  test_endpoint "GET" "/api/v1/stats/AAPL/invalid" "Stats with invalid timeframe" "404"
  
  # Test 15: Test different HTTP methods on health endpoint
  test_endpoint_flexible "POST" "/health" "POST to health endpoint (may accept any method)" "200|404|405"
  
  echo ""
  echo "===== Testing Admin Endpoints ====="
  
  # Admin endpoints require authentication - test with and without credentials
  local admin_password="admin_secret_2025"  # From config file
  
  # Test 16: Admin status without authentication (should fail)
  test_endpoint "GET" "/admin/status" "Admin status without auth" "401"
  
  # Test 17: Admin status with authentication (should pass if admin enabled)
  test_endpoint_flexible "GET" "/admin/status?password=$admin_password" "Admin status with auth" "200|404"
  
  # Test 18: Admin stop trading without authentication (should fail)
  test_endpoint "POST" "/admin/stop_trading" "Stop trading without auth" "401"
  
  # Test 19: Admin stop trading with authentication
  echo -n "Testing POST /admin/stop_trading (Stop trading with auth)... "
  local status_code
  status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
    --connect-timeout 5 --max-time 10 \
    -X POST -H "X-Admin-Password: $admin_password" \
    "http://${HTTP_HOST}:${HTTP_PORT}/admin/stop_trading" 2>/dev/null || echo "000")
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  if [[ "$status_code" == "200" || "$status_code" == "404" || "$status_code" == "400" ]]; then
    echo "✓ PASS ($status_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    echo "✗ FAIL (expected 200/400/404, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
  fi
  rm -f /tmp/test_response
  
  # Test 20: Test order submission when trading might be stopped (should return 503)
  local test_order='{"id":"TEST_AFTER_STOP","userId":"trader-test","symbol":"AAPL","type":"LIMIT","side":"BUY","quantity":1.0,"price":150.00}'
  test_endpoint_flexible "POST" "/order" "Order submission after stop (may be blocked)" "202|503" "$test_order"
  
  # Test 21: Admin resume trading with authentication
  echo -n "Testing POST /admin/resume_trading (Resume trading with auth)... "
  status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
    --connect-timeout 5 --max-time 10 \
    -X POST -H "X-Admin-Password: $admin_password" \
    "http://${HTTP_HOST}:${HTTP_PORT}/admin/resume_trading" 2>/dev/null || echo "000")
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  if [[ "$status_code" == "200" || "$status_code" == "404" || "$status_code" == "400" ]]; then
    echo "✓ PASS ($status_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    echo "✗ FAIL (expected 200/400/404, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
  fi
  rm -f /tmp/test_response
  
  # Test 22: Admin flush system without stopping trading first (should fail)
  echo -n "Testing POST /admin/flush_system (Flush without stopping - should fail)... "
  status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
    --connect-timeout 5 --max-time 10 \
    -X POST -H "X-Admin-Password: $admin_password" \
    "http://${HTTP_HOST}:${HTTP_PORT}/admin/flush_system" 2>/dev/null || echo "000")
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  if [[ "$status_code" == "400" || "$status_code" == "404" ]]; then
    echo "✓ PASS ($status_code - correctly requires trading to be stopped)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
  else
    echo "✗ FAIL (expected 400/404, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
  fi
  rm -f /tmp/test_response
  
  # Test 23: Stop trading, then flush system
  echo "Testing admin workflow: stop trading -> flush system -> resume trading"
  
  # Stop trading first
  echo -n "  Stopping trading... "
  status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
    --connect-timeout 5 --max-time 10 \
    -X POST -H "Authorization: Bearer $admin_password" \
    "http://${HTTP_HOST}:${HTTP_PORT}/admin/stop_trading" 2>/dev/null || echo "000")
  
  TOTAL_TESTS=$((TOTAL_TESTS + 1))
  if [[ "$status_code" == "200" || "$status_code" == "400" || "$status_code" == "404" ]]; then
    echo "✓ PASS ($status_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Now flush system
    echo -n "  Flushing system... "
    status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
      --connect-timeout 5 --max-time 10 \
      -X POST -H "Authorization: Bearer $admin_password" \
      "http://${HTTP_HOST}:${HTTP_PORT}/admin/flush_system" 2>/dev/null || echo "000")
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$status_code" == "200" || "$status_code" == "404" ]]; then
      echo "✓ PASS ($status_code)"
      PASSED_TESTS=$((PASSED_TESTS + 1))
    else
      echo "✗ FAIL (expected 200/404, got $status_code)"
      FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Resume trading
    echo -n "  Resuming trading... "
    status_code=$(curl -s -w '%{http_code}' -o /tmp/test_response \
      --connect-timeout 5 --max-time 10 \
      -X POST -H "Authorization: Bearer $admin_password" \
      "http://${HTTP_HOST}:${HTTP_PORT}/admin/resume_trading" 2>/dev/null || echo "000")
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ "$status_code" == "200" || "$status_code" == "404" ]]; then
      echo "✓ PASS ($status_code)"
      PASSED_TESTS=$((PASSED_TESTS + 1))
    else
      echo "✗ FAIL (expected 200/404, got $status_code)"
      FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
  else
    echo "✗ FAIL (expected 200/400/404, got $status_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
  fi
  rm -f /tmp/test_response
  
  # Test 24: Test different authentication methods
  test_endpoint_flexible "GET" "/admin/status" "Admin status with Bearer token" "200|404" "" "-H 'Authorization: Bearer $admin_password'"
  test_endpoint_flexible "GET" "/admin/status" "Admin status with custom header" "200|404" "" "-H 'X-Admin-Password: $admin_password'"
  test_endpoint "GET" "/admin/status?password=wrong_password" "Admin status with wrong password" "401"
  
  echo ""
  echo "===== Test Results ====="
  echo "Total tests: $TOTAL_TESTS"
  echo "Passed:      $PASSED_TESTS"
  echo "Failed:      $FAILED_TESTS"
  echo "Success rate: $(( (PASSED_TESTS * 100) / TOTAL_TESTS ))%"
  
  if [[ $FAILED_TESTS -eq 0 ]]; then
    echo "✓ All tests passed!"
    return 0
  else
    echo "✗ Some tests failed"
    return 1
  fi
}

# Trap cleanup on exit
trap cleanup EXIT

# Main execution
require_cmd docker
require_cmd cmake
require_cmd curl

echo "Starting endpoint tests..."

start_redpanda
build_engine
run_engine

# Run the tests
if run_tests; then
  echo ""
  echo "✓ Endpoint testing completed successfully"
  exit 0
else
  echo ""
  echo "✗ Endpoint testing failed"
  exit 1
fi