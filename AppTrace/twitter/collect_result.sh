root_path=$(pwd)
trace_paths=(
    ./round1/hot/
    # ./round1/cold/
)
script_paths=(
    ./scripts
)

for trace_path in ${trace_paths[@]};
do
    cd $root_path/$trace_path
    # echo "Current path: $(pwd)"
    for script_path in ${script_paths[@]};
    do
        python3 $root_path/$script_path/get_cov_acc.py
    done
done
