#!/bin/bash
expname=$1
test_run_time=300
sleep_time=1
run_time=30
adb wait-for-device
adb root
source AppTest.sh  # include the common functions
adb shell "echo 3 > /proc/sys/vm/drop_caches"

if [ ! -d "./cold" ]; then
    mkdir cold
fi

if [ ! -d "./hot" ]; then
    mkdir hot
fi

function start_apps() {
    run_time=30
    sleep_time=1
    echo "5"
    start_activity com.google.earth/com.google.android.apps.earth.flutter.EarthFlutterActivity
    sleep 3
    loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    echo "3"
    start_activity com.twitter.android/com.twitter.app.main.MainActivity
    sleep 3
    loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    echo "4"
    start_activity org.mozilla.firefox/org.mozilla.fenix.HomeActivity
    sleep 3
    # loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    echo "6"
    start_activity com.rovio.dream/com.unity3d.player.UnityPlayerActivity
    sleep 3

    echo "7"
    start_activity com.google.android.apps.maps/com.google.android.maps.MapsActivity
    sleep 3
    # loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    echo "8"
    start_activity com.ss.android.ugc.trill/com.ss.android.ugc.aweme.splash.SplashActivity
    sleep 3
    loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    # echo "9"
    start_activity com.facebook.katana/com.facebook.katana.LoginActivity
    sleep 3
    # loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202

    # echo "10"
    start_activity com.microsoft.emmx/com.microsoft.ruby.Main
    loop_operation $run_time $sleep_time adb shell input swipe 340 865 370 202
    sleep 3
    enter_home  
}

sleep 10
adb shell am force-stop com.google.android.youtube
adb shell am force-stop com.microsoft.emmx
adb shell am force-stop com.ss.android.ugc.trill
adb shell am force-stop com.google.earth
adb shell am force-stop com.twitter.android
adb shell am force-stop com.rovio.dream
adb shell am force-stop com.google.android.apps.maps
adb shell am force-stop org.mozilla.firefox
adb shell am force-stop com.facebook.katana
# adb shell am force-stop com.bilibili.star.bili
adb shell rm /data/local/tmp/trace_swap/* # clear the trace files
sleep 3
adb shell swapoff /dev/block/zram0
sleep 1
adb shell swapon /dev/block/zram0
# adb shell swapon /data/swapfile
adb shell "echo 10123 > /sys/kernel/swap_tracing_on/app_id"
adb shell "echo 10123 > /sys/kernel/lru_active_anon/app_id"
adb shell source /data/local/tmp/rm_cache_files20.sh
adb shell "echo 3 > /proc/sys/vm/drop_caches"
sleep 30 #  wait for 30s after rebooting to ensure the device's state stable
adb shell input swipe 340 865 370 202
sleep 3
enter_home
sleep 3




adb shell "echo 1 > /sys/kernel/lru_active_anon/print_control"
echo "2"
start_activity com.google.android.youtube/com.google.android.apps.youtube.app.watchwhile.WatchWhileActivity
sleep 3
loop_operation $test_run_time $sleep_time adb shell input swipe 340 865 370 202
enter_home
adb shell /data/local/tmp/startTrace 7
sleep 3
adb shell cp /sdcard/record.log /data/local/tmp/trace_swap/cold/


# run 9 apps
start_apps


adb shell "echo 0 > /sys/kernel/lru_active_anon/print_control"
adb shell cp /sdcard/record.log /data/local/tmp/trace_swap/cold/record_bg.log
adb shell /data/local/tmp/startTrace 0
sleep 5


adb shell /data/local/tmp/startTrace 1
adb shell "echo 1 > /sys/kernel/lru_active_anon/print_control"
echo "2"
start_activity com.google.android.youtube/com.google.android.apps.youtube.app.watchwhile.WatchWhileActivity
adb shell /data/local/tmp/startTrace 5
sleep 10
adb shell /data/local/tmp/startTrace 9
sleep 5
loop_operation $test_run_time $sleep_time adb shell input swipe 340 865 370 202
adb shell /data/local/tmp/startTrace 0
echo "wait for the trace file's page cache writeback"
sleep 10 # wait for writeback
adb shell mv /data/local/tmp/trace_swap/swapout_trace.txt /data/local/tmp/trace_swap/cold/
adb shell mv /data/local/tmp/trace_swap/swapin_trace.txt /data/local/tmp/trace_swap/cold/
adb shell mv /data/local/tmp/trace_swap/ttid_swapin_trace.txt /data/local/tmp/trace_swap/cold/
adb shell mv /data/local/tmp/trace_swap/ttfd_swapin_trace.txt /data/local/tmp/trace_swap/cold/



enter_home
adb shell /data/local/tmp/startTrace 7
sleep 3
adb shell cp /sdcard/record.log /data/local/tmp/trace_swap/hot/

# run 9 apps
start_apps

adb shell "echo 0 > /sys/kernel/lru_active_anon/print_control"
adb shell cp /sdcard/record.log /data/local/tmp/trace_swap/hot/record_bg.log
adb shell /data/local/tmp/startTrace 0
sleep 5


adb shell /data/local/tmp/startTrace 1
echo "2"
start_activity com.google.android.youtube/com.google.android.apps.youtube.app.watchwhile.WatchWhileActivity
adb shell /data/local/tmp/startTrace 5
sleep 10
adb shell /data/local/tmp/startTrace 9
sleep 5
loop_operation $test_run_time $sleep_time adb shell input swipe 340 865 370 202
adb shell /data/local/tmp/startTrace 0
echo "wait for the trace file's page cache writeback"
sleep 10 # wait for writeback
adb shell mv /data/local/tmp/trace_swap/swapout_trace.txt /data/local/tmp/trace_swap/hot/
adb shell mv /data/local/tmp/trace_swap/swapin_trace.txt /data/local/tmp/trace_swap/hot/
adb shell mv /data/local/tmp/trace_swap/ttid_swapin_trace.txt /data/local/tmp/trace_swap/hot/
adb shell mv /data/local/tmp/trace_swap/ttfd_swapin_trace.txt /data/local/tmp/trace_swap/hot/


adb pull /data/local/tmp/trace_swap/cold ./ 
adb pull /data/local/tmp/trace_swap/hot ./ 


sleep 5
adb shell "echo 3 > /proc/sys/vm/drop_caches"
adb shell input keyevent 3
sleep 0.5
adb shell input keyevent KEYCODE_APP_SWITCH  
sleep 0.5
adb shell input swipe 0 1000 5000 1000
sleep 0.5
adb shell input tap 200 1100   
sleep 0.5
adb shell am force-stop com.google.android.youtube
adb shell am force-stop com.microsoft.emmx
adb shell am force-stop com.ss.android.ugc.trill
adb shell am force-stop com.google.earth
adb shell am force-stop com.twitter.android
adb shell am force-stop com.rovio.dream
adb shell am force-stop com.google.android.apps.maps
adb shell am force-stop org.mozilla.firefox
adb shell am force-stop com.facebook.katana
sleep 0.5
adb reboot # reboot the device



