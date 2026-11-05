#!/bin/bash
# End-to-end test: indiserver runs indi_openwheel against openwheel_sim; the INDI command line
# clients play the part of KStars/Ekos.
#   test_indiserver.sh <path/to/indi_openwheel> <path/to/openwheel_sim>
set -u
DRIVER=$1
SIM=$2
PORT=${INDI_TEST_PORT:-7625}
T=$(mktemp -d /tmp/owindi.XXXXXX)
export HOME=$T            # INDI keeps its config in ~/.indi: start clean
DEV=OpenWheel
FAILS=0
SIM_PID=
SERVER_PID=

cleanup()
{
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    [ -n "$SIM_PID" ] && kill "$SIM_PID" 2>/dev/null
    wait 2>/dev/null
    if [ "$FAILS" -ne 0 ]; then
        echo "---- indiserver log (tail)"; tail -40 "$T/server.log"
        echo "---- simulator log"; cat "$T/sim.log"
    fi
    [ -n "${OW_KEEP_LOGS:-}" ] && cp "$T"/*.log "$OW_KEEP_LOGS"/ 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

get() { indi_getprop -p "$PORT" -t 2 -1 "$DEV.$1" 2>/dev/null; }
set_() { indi_setprop -p "$PORT" "$DEV.$1"; }
ok() { echo "  ok   $1"; }
fail() { echo "  FAIL $1"; FAILS=$((FAILS + 1)); }

# wait_for <property.element> <expected> <seconds> <description>
wait_for()
{
    local end=$(( $(date +%s) + $3 )) v=
    while [ "$(date +%s)" -le "$end" ]; do
        v=$(get "$1")
        [ "$v" = "$2" ] && { ok "$4"; return 0; }
        sleep 0.1
    done
    fail "$4 (expected '$2', got '$v')"
    return 1
}

# wait_match <property.element> <grep -E pattern> <seconds> <description>
wait_match()
{
    local end=$(( $(date +%s) + $3 )) v=
    while [ "$(date +%s)" -le "$end" ]; do
        v=$(get "$1")
        echo "$v" | grep -qE "$2" && { ok "$4"; return 0; }
        sleep 0.1
    done
    fail "$4 (expected /$2/, got '$v')"
    return 1
}

sim_show() { echo show > "$T/ctl"; sleep 0.3; tail -1 "$T/sim.log"; }

start_sim()
{
    "$SIM" --slots 5 --ms-per-slot 150 --link "$T/wheel" --control "$T/ctl" >> "$T/sim.log" 2>&1 &
    SIM_PID=$!
    for _ in $(seq 50); do [ -e "$T/wheel" ] && [ -p "$T/ctl" ] && return 0; sleep 0.1; done
    echo "simulator did not start"; exit 1
}

echo "== start"
start_sim
indiserver -p "$PORT" -v "$DRIVER" > "$T/server.log" 2>&1 &
SERVER_PID=$!
wait_for "CONNECTION.CONNECT" "Off" 10 "driver loaded"

echo "== connect on a fixed port"
set_ "OPENWHEEL_DETECT.MANUAL=On"
set_ "DEVICE_PORT.PORT=$T/wheel"
set_ "CONNECTION.CONNECT=On"
wait_for "CONNECTION.CONNECT" "On" 5 "connected"
wait_for "OPENWHEEL_INFO.SERIAL" "3A00FF12ABCD" 3 "identity read (serial number)"
wait_for "OPENWHEEL_INFO.FIRMWARE" "FW1.2.0" 1 "firmware version"
wait_match "FILTER_SLOT.FILTER_SLOT_VALUE" "^1(\.0*)?$" 2 "at filter 1 (wheel slot 0)"
wait_for "FILTER_NAME.FILTER_SLOT_NAME_3" "Filter 3" 2 "empty wheel names shown as 'Filter N'"
wait_match "OPENWHEEL_HOUSEKEEPING.MCU_TEMP" "^31\.(2|19)" 3 "housekeeping published"
wait_match "OPENWHEEL_STATUS.STATE" "^Idle at filter 1" 2 "status text"

echo "== move"
set_ "FILTER_SLOT.FILTER_SLOT_VALUE=4"
wait_for "FILTER_SLOT._STATE" "Busy" 2 "slot property busy while moving"
wait_match "OPENWHEEL_STATUS.STATE" "^Moving to filter 4" 2 "status says moving"
wait_for "FILTER_SLOT._STATE" "Ok" 5 "move finished"
wait_match "FILTER_SLOT.FILTER_SLOT_VALUE" "^4(\.0*)?$" 1 "at filter 4"
sim_show | grep -q "slot 3 known 1" && ok "simulator is at slot 3" || fail "simulator is at slot 3"
set_ "FILTER_SLOT.FILTER_SLOT_VALUE=9"
wait_for "FILTER_SLOT._STATE" "Alert" 2 "out of range slot rejected"

echo "== filter names and focus offsets are stored in the wheel"
set_ "FILTER_NAME.FILTER_SLOT_NAME_1;FILTER_SLOT_NAME_2=Lum;Ha, 7nm"
wait_for "FILTER_NAME._STATE" "Ok" 5 "names saved"
wait_for "FILTER_NAME.FILTER_SLOT_NAME_2" "Ha- 7nm" 2 "forbidden ',' replaced as the firmware requires"
set_ "OPENWHEEL_FOCUS_OFFSETS.OFFSET_2=-25"
wait_for "OPENWHEEL_FOCUS_OFFSETS._STATE" "Ok" 5 "focus offset saved"
SHOW=$(sim_show)
echo "$SHOW" | grep -q "names \[Lum\] \[Ha- 7nm\] \[\] \[\] \[\]" && ok "names in the wheel" || fail "names in the wheel: $SHOW"
echo "$SHOW" | grep -q "offsets 0 -25 0" && ok "offsets in the wheel" || fail "offsets in the wheel: $SHOW"

echo "== device fault, clear error"
echo "failnext 6" > "$T/ctl"
set_ "FILTER_SLOT.FILTER_SLOT_VALUE=2"
wait_for "FILTER_SLOT._STATE" "Alert" 5 "fault ends the move with Alert"
wait_match "OPENWHEEL_STATUS.ERROR" "^6: no hall" 2 "error code and text shown"
wait_for "OPENWHEEL_HEALTH.FAULT" "Alert" 2 "fault light"
set_ "OPENWHEEL_ACTION.CLEAR_ERROR=On"
wait_for "OPENWHEEL_STATUS.ERROR" "none" 3 "error cleared"
wait_for "OPENWHEEL_HEALTH.FAULT" "Ok" 2 "fault light off"

echo "== home and halt"
set_ "OPENWHEEL_ACTION.HOME=On"
wait_for "OPENWHEEL_ACTION._STATE" "Busy" 2 "homing"
wait_for "OPENWHEEL_ACTION._STATE" "Ok" 5 "home found"
wait_match "FILTER_SLOT.FILTER_SLOT_VALUE" "^1(\.0*)?$" 1 "at filter 1 after homing"
echo "stuck 1" > "$T/ctl"          # keep the move running until it is halted
set_ "FILTER_SLOT.FILTER_SLOT_VALUE=5"
wait_for "FILTER_SLOT._STATE" "Busy" 2 "moving"
set_ "OPENWHEEL_ACTION.HALT=On"
wait_for "FILTER_SLOT._STATE" "Alert" 2 "halt leaves the position unknown"
wait_for "OPENWHEEL_HEALTH.POSITION_KNOWN" "Idle" 2 "position-known light off"
echo "stuck 0" > "$T/ctl"
set_ "FILTER_SLOT.FILTER_SLOT_VALUE=3"
wait_for "FILTER_SLOT._STATE" "Ok" 5 "a move after the halt works"
set_ "OPENWHEEL_ACTION.HALT=On"
wait_for "OPENWHEEL_ACTION._STATE" "Ok" 2 "halt while standing"
wait_for "FILTER_SLOT._STATE" "Ok" 2 "a standing wheel keeps its position"

echo "== calibration"
set_ "OPENWHEEL_ACTION.CALIBRATE=On"
wait_match "OPENWHEEL_STATUS.STATE" "^Calibrating" 2 "calibrating"
wait_for "OPENWHEEL_ACTION._STATE" "Ok" 6 "calibration done"

echo "== housekeeping warning"
echo "warn 1" > "$T/ctl"
wait_for "OPENWHEEL_HEALTH.WARNING" "Busy" 4 "warning light"
wait_match "OPENWHEEL_WARNINGS.TEXT" "below 4.75 V" 2 "warning text"
echo "warn 0" > "$T/ctl"

echo "== silent wheel: the driver gives up instead of hanging"
echo "silent 1" > "$T/ctl"
wait_for "CONNECTION.CONNECT" "Off" 6 "disconnected after repeated timeouts"
wait_for "CONNECTION._STATE" "Alert" 1 "connection property in Alert"
echo "silent 0" > "$T/ctl"

echo "== reconnect: names come from the wheel, not from INDI's config"
set_ "CONNECTION.CONNECT=On"
wait_for "CONNECTION.CONNECT" "On" 5 "reconnected"
wait_for "FILTER_NAME.FILTER_SLOT_NAME_1" "Lum" 2 "name read back from the wheel"
wait_match "OPENWHEEL_FOCUS_OFFSETS.OFFSET_2" "^-25" 2 "offset read back from the wheel"

echo "== unplug"
echo "unplug" > "$T/ctl"
wait_for "CONNECTION.CONNECT" "Off" 4 "disconnected after unplug"
wait "$SIM_PID" 2>/dev/null
SIM_PID=

echo "== auto mode: no wheel on USB, the configured port must still pass the identify check"
start_sim
set_ "OPENWHEEL_DETECT.AUTO=On"
set_ "CONNECTION.CONNECT=On"
wait_for "CONNECTION.CONNECT" "On" 5 "connected in auto mode via the configured port"
set_ "CONNECTION.DISCONNECT=On"
wait_for "CONNECTION.CONNECT" "Off" 3 "disconnected"
set_ "DEVICE_PORT.PORT=/dev/null"
set_ "OPENWHEEL_DETECT.MANUAL=On"
set_ "CONNECTION.CONNECT=On"
sleep 3
[ "$(get CONNECTION.CONNECT)" = "Off" ] && ok "a port that is not an OpenWheel is refused" || fail "a port that is not an OpenWheel is refused"

echo
if [ "$FAILS" -eq 0 ]; then echo "indiserver test passed"; exit 0; fi
echo "indiserver test: $FAILS failure(s)"
exit 1
