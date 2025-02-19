#!/bin/bash

# Root directory path
root_dir=$(pwd)
# List of application directories
app_dirs=(
    "./youtube"
    "./twitter"
    "./earth"
    "./firefox"
    "./bangdream"
)
adb wait-for-device
adb root
# Loop through each application directory
for app_dir in "${app_dirs[@]}"; do
    # Construct full directory path
    full_path="${root_dir}/${app_dir}"

    # Change directory to the target directory
    cd "${full_path}"

    # Check if directory change was successful
    if [ $? -eq 0 ]; then
        echo "Working in directory: ${full_path}"
        adb wait-for-device
        adb root
        ./trace_replay.sh
    else
        echo "Failed to change directory to ${full_path}"
    fi
done