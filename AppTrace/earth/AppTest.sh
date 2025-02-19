
# This function will run the give command passed as arguments every $sleep_time seconds for $run_time seconds.
# Usage: wait_time "run_time" "sleep_time" "command"
function loop_operation() {
    run_time=$1 # 5min
    shift
    sleep_time=$1
    shift
    start_time=$(date +%s)
    while true; do
        now_time=$(date +%s)
        elapsed_time=$((now_time - start_time))
        if [ $elapsed_time -gt $run_time ]; then
            break
        fi
        $@
        sleep "$sleep_time"
    done
}

function start_activity() {
    activity_name=$1
    adb shell am start -W -n $activity_name
}

function enter_home() {
    adb shell input keyevent 3
}

function enter_back() {
    adb shell input keyevent 4
}
