#!/bin/bash

function replay_trace {
    # Run the swapout trace
    window_size=$1
    medium_window_size=$2
    large_window_size=$3
    adb wait-for-device
    adb root
    # check if there is a swapout dir in results, if not create one
    if [ ! -d "swapout" ]; then
        mkdir swapout
    fi

    if [ ! -d "ttid_swapin" ]; then
        mkdir ttid_swapin
    fi

    adb shell "echo compression_trace.txt > /sys/kernel/compress_test/swapout_meta_path" 
    adb shell "echo swapout_trace.txt.data  > /sys/kernel/compress_test/swapout_data_path"
    adb shell "echo ${window_size} > /sys/kernel/compress_test/window_size"
    adb shell "echo ${medium_window_size} > /sys/kernel/compress_test/medium_window_size"
    adb shell "echo ${large_window_size} > /sys/kernel/compress_test/large_window_size"
    # clear dmesg
    adb shell "dmesg -C"
    # Run the swapout trace
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapout_trace"
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapout_trace"
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapout_trace"
    sleep 1
    # Print dmesg to a file with 
    adb shell "dmesg" > ./log
    mv ./log ./swapout/swapout_${window_size}_${medium_window_size}_${large_window_size}.txt

    adb shell "echo ttid_swapin_trace.txt.meta > /sys/kernel/compress_test/swapin_meta_path"
    adb shell "dmesg -C"
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapin_trace"
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapin_trace"
    adb shell "echo 1 > /sys/kernel/compress_test/replay_swapin_trace"
    # Print dmesg to a file with different name
    sleep 1
    adb shell "dmesg" > ./log
    mv ./log ./ttid_swapin/ttid_swapin_${window_size}_${medium_window_size}_${large_window_size}.txt
}

root_path=$(pwd)
trace_paths=(
    ./round1/hot/
    # ./round1/cold/
)
script_paths=(
    ./scripts
)

hot_list_length=(
    10000
)

warm_list_length=(
    25000
)

# Define specific combinations of small, medium, and large scale values
combinations=(
    "4 16 128"
    "8 16 128"
    "8 32 128"
    "2 16 256"
    # Add other combinations as needed
)

for trace_path in ${trace_paths[@]}; do
    full_path="$root_path/$trace_path"
    full_script_path="$root_path/$script_paths"
    cd $full_path
    echo "Current path: $(pwd)"
    adb wait-for-device
    adb root
    adb shell "rm -f /data/local/tmp/compr_test/*"

    adb push swapin_trace.txt.meta /data/local/tmp/compr_test/
    adb push ttid_swapin_trace.txt.meta /data/local/tmp/compr_test/
    adb push swapout_trace.txt.data /data/local/tmp/compr_test/

    if [ ! -d "results" ]; then
            mkdir results
    fi
    result_dir="$full_path/results"

    for hot_length in "${hot_list_length[@]}"; do
        for warm_length in "${warm_list_length[@]}"; do
            adb wait-for-device
            adb root
            python3 ${full_script_path}/compression_scale_identify.py $hot_length $warm_length
            adb push compression_trace.txt /data/local/tmp/compr_test/ 
            if [ ! -d "${result_dir}/list_length_${hot_length}_${warm_length}" ]; then
                mkdir "${result_dir}/list_length_${hot_length}_${warm_length}"
            fi
            result_dir="${result_dir}/list_length_${hot_length}_${warm_length}"
            cd "${result_dir}"
            for combination in "${combinations[@]}"; do
                read -r window_size medium_window_size large_window_size <<< "$combination"
                replay_trace $window_size $medium_window_size $large_window_size
            done
            replay_trace 32 32 32 # baseline
            cd "${full_path}"
            result_dir="${full_path}/results"
        done  
    done
done
