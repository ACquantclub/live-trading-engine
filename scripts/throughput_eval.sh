#!/usr/bin/env bash

set -euo pipefail

# Throughput evaluation for the trading engine.
# - Starts a Redpanda (Kafka) container if not running
# - Builds and runs the trading engine
# - Sends orders at a specified rate for a given duration
# - Waits for processing to complete and reports throughput

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Defaults (overridable via env or flags)
DURATION=${DURATION:-30} # seconds
RATE=${RATE:-100} # orders per second
CONCURRENCY=${CONCURRENCY:-10}
USERS=${USERS:-10}
SYMBOL=${SYMBOL:-AAPL}
PRICE=${PRICE:-150.50}
QTY=${QTY:-1.0}
HTTP_HOST=${HTTP_HOST:-127.0.0.1}
HTTP_PORT=${HTTP_PORT:-8080}
HTTP_ORDERS_PATH=${HTTP_ORDERS_PATH:-/order}
HTTP_HEALTH_PATH=${HTTP_HEALTH_PATH:-/health}
KAFKA_BROKERS=${KAFKA_BROKERS:-127.0.0.1:9092}
TOPIC=${TOPIC:-order-requests}
REDPANDA_CONTAINER_NAME=${REDPANDA_CONTAINER_NAME:-redpanda-sim}
ENGINE_BIN_REL="build/apps/trading_engine/live_trading_engine"
ENGINE_BIN="$REPO_ROOT/$ENGINE_BIN_REL"
ENGINE_CONFIG_REL="config/trading_engine.json"
ENGINE_CONFIG="$REPO_ROOT/$ENGINE_CONFIG_REL"
APP_LOG="$REPO_ROOT/app.log"

MODE="auto" # auto|http|queue
KEEP_RED_PANDA=${KEEP_RED_PANDA:-0}

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  -d, --duration D        Test duration in seconds (default: $DURATION)
  -r, --rate R            Orders per second (default: $RATE)
  -c, --concurrency N     Concurrent clients (default: $CONCURRENCY)
  -u, --users N           Distinct userIds to rotate (default: $USERS)
  -s, --symbol SYM        Symbol (default: $SYMBOL)
  -p, --price P           Price (default: $PRICE)
  -q, --quantity Q        Quantity (default: $QTY)
  --http-host HOST        HTTP host (default: $HTTP_HOST)
  --http-port PORT        HTTP port (default: $HTTP_PORT)
  --http-orders PATH      HTTP orders path (default: $HTTP_ORDERS_PATH)
  --http-health PATH      HTTP health path (default: $HTTP_HEALTH_PATH)
  --brokers HOST:PORT     Kafka brokers (default: $KAFKA_BROKERS)
  -t, --topic NAME        Kafka topic (default: $TOPIC)
  -m, --mode MODE         auto|http|queue (default: auto)
  --keep-redpanda         Do not stop/remove Redpanda container after run
  -h, --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--duration) DURATION="$2"; shift 2 ;;
    -r|--rate) RATE="$2"; shift 2 ;;
    -c|--concurrency) CONCURRENCY="$2"; shift 2 ;;
    -u|--users) USERS="$2"; shift 2 ;;
    -s|--symbol) SYMBOL="$2"; shift 2 ;;
    -p|--price) PRICE="$2"; shift 2 ;;
    -q|--quantity) QTY="$2"; shift 2 ;;
    --http-host) HTTP_HOST="$2"; shift 2 ;;
    --http-port) HTTP_PORT="$2"; shift 2 ;;
    --http-orders) HTTP_ORDERS_PATH="$2"; shift 2 ;;
    --http-health) HTTP_HEALTH_PATH="$2"; shift 2 ;;
    --brokers) KAFKA_BROKERS="$2"; shift 2 ;;
    -t|--topic) TOPIC="$2"; shift 2 ;;
    -m|--mode) MODE="$2"; shift 2 ;;
    --keep-redpanda) KEEP_RED_PANDA=1; shift ;;
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
  # Clean previous logs to simplify counting
  rm -f "$APP_LOG" "$REPO_ROOT/trading_engine.log" || true
  echo "Command: $ENGINE_BIN $ENGINE_CONFIG"
  ("$ENGINE_BIN" "$ENGINE_CONFIG") &
  ENGINE_PID=$!

  # Wait for engine to report started
  echo -n "Waiting for engine to be ready"
  for i in {1..60}; do
    if [[ -f "$APP_LOG" ]] && grep -q "Trading engine started" "$APP_LOG"; then
      # Also check if HTTP server is responding
      if curl -s --connect-timeout 1 --max-time 1 "http://${HTTP_HOST}:${HTTP_PORT}${HTTP_HEALTH_PATH}" >/dev/null 2>&1; then
        echo " - ready"
        # Small delay to ensure server is fully stable
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
    kill "$ENGINE_PID" >/dev/null 2>&1 || true
    wait "$ENGINE_PID" 2>/dev/null || true
  fi
}

cleanup() {
  stop_engine || true
  if [[ "$KEEP_RED_PANDA" != "1" ]]; then
    docker rm -f "$REDPANDA_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
}

require_cmd docker
require_cmd cmake

start_redpanda
build_engine
run_engine

# Detect HTTP availability when in auto mode
if [[ "$MODE" == "auto" ]]; then
  if curl -sSf "http://${HTTP_HOST}:${HTTP_PORT}${HTTP_HEALTH_PATH}" >/dev/null 2>&1; then
    MODE="http"
  else
    echo "HTTP endpoint not responding; will publish directly to Kafka"
    MODE="queue"
  fi
fi

echo "Mode: $MODE"

# Calculate total orders
ORDERS_COUNT=$((RATE * DURATION))

generate_orders() {
  local count="$1"
  local users="$2"
  local symbol="$3"
  local price="$4"
  local qty="$5"
  local out_file="$6"

  : > "$out_file"
  local half=$((count/2))
  for ((i=0;i<count;i++)); do
    local user_idx=$(( (i % users) + 1 ))
    local side
    if (( i < half )); then side="SELL"; else side="BUY"; fi
    local id="SIM_${i}"
    local user_id="trader-${user_idx}"
    local json
    json=$(printf '{"id":"%s","userId":"%s","symbol":"%s","type":"LIMIT","side":"%s","quantity":%.6f,"price":%.2f}' \
      "$id" "$user_id" "$symbol" "$side" "$qty" "$price")
    echo "${user_id}|${json}" >> "$out_file"
  done
}

# Prepare HTTP payloads into a file (one JSON per line)
prepare_payloads_http() {
  local count="$1"
  local users="$2"
  local symbol="$3"
  local price="$4"
  local qty="$5"
  local out_file="$6"

  : > "$out_file"
  local half=$((count/2))
  for ((i=0;i<count;i++)); do
    local user_idx=$(( (i % users) + 1 ))
    local side
    if (( i < half )); then side="SELL"; else side="BUY"; fi
    local id="SIM_${i}"
    local user_id="trader-${user_idx}"
    printf '%s\n' \
      "$(printf '{"id":"%s","userId":"%s","symbol":"%s","type":"LIMIT","side":"%s","quantity":%.6f,"price":%.2f}' \
        "$id" "$user_id" "$symbol" "$side" "$qty" "$price")" >> "$out_file"
  done
}

produce_with_kcat_rated() {
  local input_file="$1"
  local rate="$2"
  if has_cmd kcat; then
    kcat -b "$KAFKA_BROKERS" -t "$TOPIC" -K '|' -r "$rate" < "$input_file"
  else
    docker run --rm -i --network host edenhill/kcat:1.7.1 \
      -b "$KAFKA_BROKERS" -t "$TOPIC" -K '|' -r "$rate" < "$input_file"
  fi
}

send_orders_http_rated() {
  local payload_file="$1"
  local rate="$2"
  local duration="$3"
  local concurrency="$4"

  echo "Sending HTTP orders at $rate/sec for $duration seconds..."
  LAST_HTTP_STATUS_FILE=$(mktemp)
  
  # Calculate delay between requests in microseconds
  local delay_us=$((1000000 / rate))
  
  # Send each request with rate limiting
  local count=0
  local start_time=$(date +%s%6N)
  
  while IFS= read -r payload; do
    # Calculate when this request should be sent
    local target_time=$(( start_time + count * delay_us ))
    local current_time=$(date +%s%6N)
    
    # Sleep if we're ahead of schedule
    if (( current_time < target_time )); then
      local sleep_us=$(( target_time - current_time ))
      usleep $sleep_us 2>/dev/null || sleep $(awk "BEGIN {printf \"%.6f\", $sleep_us/1000000}")
    fi
    
    timeout 5s curl -s -o /dev/null -w "%{http_code}\n" \
      -X POST -H "Content-Type: application/json" \
      --connect-timeout 2 --max-time 3 \
      "http://${HTTP_HOST}:${HTTP_PORT}${HTTP_ORDERS_PATH}" \
      -d "$payload" >> "$LAST_HTTP_STATUS_FILE" || echo "000" >> "$LAST_HTTP_STATUS_FILE"
    
    count=$((count + 1))
    
    # Stop after duration seconds
    local elapsed_us=$(( $(date +%s%6N) - start_time ))
    if (( elapsed_us >= duration * 1000000 )); then
      break
    fi
  done < "$payload_file"
  
  echo "All HTTP requests completed"
}


wait_until_processed() {
  local expected="$1"
  local timeout_sec=${2:-60}
  local waited=0
  echo -n "Waiting for $expected orders to be processed"
  while (( waited < timeout_sec )); do
    if [[ -f "$APP_LOG" ]]; then
      # Count lines that indicate an order was picked up from queue
      local processed
      processed=$(grep -c "Processing order from" "$APP_LOG" || true)
      if (( processed >= expected )); then
        echo " - done ($processed)"
        return 0
      fi
    fi
    echo -n "."
    sleep 1
    waited=$((waited+1))
  done
  echo
  echo "Timed out waiting for orders to be processed. Check $APP_LOG" >&2
}

sum_trades() {
  if [[ -f "$APP_LOG" ]]; then
    # Sum numbers in lines like: "generated X trades"
    awk '/generated [0-9]+ trades/ { for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) sum+=$i } END { print sum+0 }' "$APP_LOG"
  else
    echo 0
  fi
}

EXPECTED_PROCESSED=$ORDERS_COUNT
HTTP_SUCCESS_COUNT=0
HTTP_FAIL_COUNT=0

case "$MODE" in
  http)
    echo "Generating $ORDERS_COUNT HTTP payloads for $DURATION seconds..."
    tmp_payloads=$(mktemp)
    prepare_payloads_http "$ORDERS_COUNT" "$USERS" "$SYMBOL" "$PRICE" "$QTY" "$tmp_payloads"
    echo "Starting to send $ORDERS_COUNT orders via HTTP..."
    START_TS=$(date +%s%3N)
    send_orders_http_rated "$tmp_payloads" "$RATE" "$DURATION" "$CONCURRENCY"
    # The sending function is now the one that takes time, so we record end time after it.
    END_TS=$(date +%s%3N)
    echo "Finished sending orders"
    # Evaluate HTTP status codes (202 is success)
    if [[ -n "${LAST_HTTP_STATUS_FILE:-}" && -f "$LAST_HTTP_STATUS_FILE" ]]; then
      HTTP_SUCCESS_COUNT=$(grep -c '^202$' "$LAST_HTTP_STATUS_FILE" || true)
      total_sent=$(wc -l < "$LAST_HTTP_STATUS_FILE" || echo 0)
      HTTP_FAIL_COUNT=$(( total_sent - HTTP_SUCCESS_COUNT ))
      EXPECTED_PROCESSED=$HTTP_SUCCESS_COUNT
    fi
    rm -f "$tmp_payloads"
    ;;
  queue)
    echo "Generating $ORDERS_COUNT Kafka orders for $DURATION seconds..."
    tmp_orders=$(mktemp)
    generate_orders "$ORDERS_COUNT" "$USERS" "$SYMBOL" "$PRICE" "$QTY" "$tmp_orders"
    echo "Starting to send $ORDERS_COUNT orders via Kafka at $RATE/sec..."
    START_TS=$(date +%s%3N)
    produce_with_kcat_rated "$tmp_orders" "$RATE"
    END_TS=$(date +%s%3N)
    echo "Finished sending orders"
    rm -f "$tmp_orders"
    ;;
  *) echo "Invalid MODE: $MODE" >&2; exit 1 ;;
esac

echo "Starting to wait for processing..."
# Extend wait time: duration of test + 60s grace period
wait_until_processed "$EXPECTED_PROCESSED" $((DURATION + 120))
echo "Finished waiting for processing"
PROCESSING_END_TS=$(date +%s%3N)

duration_ms=$((END_TS - START_TS))
duration_s=$(awk "BEGIN { printf \"%.3f\", $duration_ms/1000 }")

processing_duration_ms=$((PROCESSING_END_TS - START_TS))
processing_duration_s=$(awk "BEGIN { printf \"%.3f\", $processing_duration_ms/1000 }")

processed_count=$(grep -c "Processing order from" "$APP_LOG" || echo 0)
orders_per_sec=$(awk -v duration="$processing_duration_ms" -v count="$processed_count" 'BEGIN { if (duration>0) printf "%.2f", (count*1000)/duration; else print 0 }')
trades_total=$(sum_trades)
trades_per_sec=$(awk -v duration="$processing_duration_ms" -v trades="$trades_total" 'BEGIN { if (duration>0) printf "%.2f", (trades*1000)/duration; else print 0 }')

echo ""
echo "===== Throughput Evaluation Results ====="
echo "Target rate        : $RATE orders/sec for $DURATION seconds"
echo "Total orders sent  : $ORDERS_COUNT"
echo "Processed orders   : $processed_count"
if [[ "$MODE" == "http" ]]; then
  echo "HTTP 202 count     : $HTTP_SUCCESS_COUNT"
  echo "HTTP fail count    : $HTTP_FAIL_COUNT"
fi
echo "Trades executed    : $trades_total"
echo "Send duration (s)  : $duration_s"
echo "Total time (s)     : $processing_duration_s (send + processing)"
echo "Orders/sec         : $orders_per_sec (end-to-end)"
echo "Trades/sec         : $trades_per_sec (end-to-end)"
echo "Log file           : $APP_LOG"
echo "========================================="



# Clean up
cleanup
