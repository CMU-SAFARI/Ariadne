root_path=$(pwd)
trace_paths=(
    ./round1/hot/
)
script_paths=(
    ./scripts
)

for trace_path in ${trace_paths[@]};
do
    # cd $root_path/$trace_path
    # echo "Current path: $(pwd)"
    for script_path in ${script_paths[@]};
    do
        python3 $root_path/$script_path/extract_eva.py
    done
done
