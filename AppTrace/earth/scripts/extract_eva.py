def init_three_dimensional_list(a, b, c):
    return [[[0 for _ in range(c)] for _ in range(b)] for _ in range(a)]

def print_three_dimensional_list(lst, a, b, c):
    for k in range(c):
        for j in range(b):
            for i in range(a):
                print(f"{lst[i][j][k]} ", end="")
            print()  # New line after finishing 'i' loop for readability
        print()  # New line after finishing 'j' loop to separate dimensions
    return

def extract_compr_result(swapout_result_path):
    try:
        with open(swapout_result_path, 'r') as swapout_file:
            swapout_lines = swapout_file.readlines()
    except FileNotFoundError:
        print(f"File not found: {swapout_result_path}")
        return [0] * 8  # Default to zero if file is not found

    results = []
    search_patterns = [
        "small compression time = ",
        "medium compression time = ",
        "large compression time = ",
        "total compression time = ",
        "small_compr_size = ",
        "medium_compr_size = ",
        "large_compr_size = ",
        "total_compr_size = "
    ]

    for search_pattern in search_patterns:
        temp_results = []
        for line in swapout_lines:
            if search_pattern in line:
                parts = line.split(search_pattern)
                result = parts[-1].strip()
                try:
                    result = int(result.split()[0])
                    temp_results.append(result)
                except ValueError:
                    continue
        if temp_results:
            results.append(min(temp_results))
        else:
            results.append(0)  # Default to zero if no match is found

    return results

def extract_decompr_result(swapin_result_path):
    swapout_file = open(swapin_result_path, 'r')
    swapout_lines = swapout_file.readlines()
    results = []
    search_patterns = [
        "small total_decompr_time = ",
        "medium total_decompr_time = ",
        "large total_decompr_time = ",
        "total_decompr_time = ",
        "small_cnt = ",
        "medium_cnt = ",
        "large_cnt = ",
    ]
    
    for search_pattern in search_patterns:
        temp_results = []
        for line in swapout_lines:
            if search_pattern in line:
                parts = line.split(search_pattern)
                result = parts[-1]
                result = result.strip()
                result = result.split()[0]
                result = result.split(',')[0]
                result = int(result)
                temp_results.append(result)
        # average the temp_result and append to the results list
        result = min(temp_results)
        results.append(result)

    swapout_file.close()
    # calculate total decompr cnt and append to the results list
    results.append(results[4] + results[5] + results[6])
    results[3] = results[0] + results[1] + results[2]
    return results


def main():
    # print("Extracting results from the log files")
    # Define the specific combinations you want to read
    specific_combinations = [
        (32, 32, 32, "4K-4K-4K"),
        (4, 16, 128, "512-2K-16K"),
        (8, 16, 128, "1K-2K-16K"),
        (8, 32, 128, "1K-4K-16K"),
        (2, 16, 256, "256-2K-32K"),
    ]

    original_size = 251871232		
    app_name = "Earth"
    DRAM_launch_time = 86
    ZRAM_launch_time = 170

    # Determine the lengths for initializing lists
    a, b, c = len(specific_combinations), 1, 1

    # Initialize lists for compression results
    small_compr_time = init_three_dimensional_list(a, b, c)
    medium_compr_time = init_three_dimensional_list(a, b, c)
    large_compr_time = init_three_dimensional_list(a, b, c)
    total_compr_time = init_three_dimensional_list(a, b, c)
    small_compr_size = init_three_dimensional_list(a, b, c)
    medium_compr_size = init_three_dimensional_list(a, b, c)
    large_compr_size = init_three_dimensional_list(a, b, c)
    total_compr_size = init_three_dimensional_list(a, b, c)

    # Initialize lists for decompression results
    small_decompr_time = init_three_dimensional_list(a, b, c)
    medium_decompr_time = init_three_dimensional_list(a, b, c)
    large_decompr_time = init_three_dimensional_list(a, b, c)
    total_decompr_time = init_three_dimensional_list(a, b, c)
    small_decompr_cnt = init_three_dimensional_list(a, b, c)
    medium_decompr_cnt = init_three_dimensional_list(a, b, c)
    large_decompr_cnt = init_three_dimensional_list(a, b, c)
    total_decompr_cnt = init_three_dimensional_list(a, b, c)

    swapout_result_dir_path = "./round1/hot/results/list_length_5000_20000/swapout"
    swapin_result_dir_path = "./round1/hot/results/list_length_5000_20000/ttid_swapin"

    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        # print(f"Extracting results for {scale}")
        swapout_result_path = f"{swapout_result_dir_path}/swapout_{small_scale}_{medium_scale}_{large_scale}.txt"
        results = extract_compr_result(swapout_result_path)
        small_compr_time[idx][0][0] = results[0]
        medium_compr_time[idx][0][0] = results[1]
        large_compr_time[idx][0][0] = results[2]
        total_compr_time[idx][0][0] = results[3]
        small_compr_size[idx][0][0] = results[4]
        medium_compr_size[idx][0][0] = results[5]
        large_compr_size[idx][0][0] = results[6]
        total_compr_size[idx][0][0] = results[7]

        swapin_result_path = f"{swapin_result_dir_path}/ttid_swapin_{small_scale}_{medium_scale}_{large_scale}.txt"
        results = extract_decompr_result(swapin_result_path)
        small_decompr_time[idx][0][0] = results[0]
        medium_decompr_time[idx][0][0] = results[1]
        large_decompr_time[idx][0][0] = results[2]
        total_decompr_time[idx][0][0] = results[3]
        small_decompr_cnt[idx][0][0] = results[4]
        medium_decompr_cnt[idx][0][0] = results[5]
        large_decompr_cnt[idx][0][0] = results[6]
        total_decompr_cnt[idx][0][0] = results[7]

    # print("Results extracted successfully")

    # Print compression results
    # print("Small Compression Time")
    # print_three_dimensional_list(small_compr_time, a, b, c)
    # print("Small Compression Size")
    # print_three_dimensional_list(small_compr_size, a, b, c)
    # print("Medium Compression Time")
    # print_three_dimensional_list(medium_compr_time, a, b, c)
    # print("Medium Compression Size")
    # print_three_dimensional_list(medium_compr_size, a, b, c)
    # print("Large Compression Time")
    # print_three_dimensional_list(large_compr_time, a, b, c)
    # print("Large Compression Size")
    # print_three_dimensional_list(large_compr_size, a, b, c)
    # print("Total Compression Time")
    # print_three_dimensional_list(total_compr_time, a, b, c)
    # print("Total Compression Size")
    # print_three_dimensional_list(total_compr_size, a, b, c)

    # Print decompression results
    # print("Small Decompression Time")
    # print_three_dimensional_list(small_decompr_time, a, b, c)
    # print("Small Decompression Count")
    # print_three_dimensional_list(small_decompr_cnt, a, b, c)
    # print("Medium Decompression Time")
    # print_three_dimensional_list(medium_decompr_time, a, b, c)
    # print("Medium Decompression Count")
    # print_three_dimensional_list(medium_decompr_cnt, a, b, c)
    # print("Large Decompression Time")
    # print_three_dimensional_list(large_decompr_time, a, b, c)
    # print("Large Decompression Count")
    # print_three_dimensional_list(large_decompr_cnt, a, b, c)
    # print("Total Decompression Time")
    # print_three_dimensional_list(total_decompr_time, a, b, c)
    # print("Total Decompression Count")
    # print_three_dimensional_list(total_decompr_cnt, a, b, c)

    # Calculate the evaluation metrics
    compr_lat_al = init_three_dimensional_list(a, b, c)
    decompr_lat_al = init_three_dimensional_list(a, b, c)
    compr_ratio_al = init_three_dimensional_list(a, b, c)
    cpu_time_al = init_three_dimensional_list(a, b, c)
    launch_time_al = init_three_dimensional_list(a, b, c)

    for i in range(a):
        for j in range(b):
            for k in range(c):
                # Calculate the compression latency
                compr_lat_al[i][j][k] = total_compr_time[i][j][k] / 1000
                # Calculate the decompression latency
                decompr_lat_al[i][j][k] = total_decompr_time[i][j][k] / 1000
                # Calculate the compression ratio
                compr_ratio_al[i][j][k] = total_compr_size[i][j][k] / original_size
                # Calculate the CPU time
                cpu_time_al[i][j][k] = (total_compr_time[i][j][k] + total_decompr_time[i][j][k]) / (total_compr_time[0][0][0] + total_decompr_time[0][0][0])
                # Calculate the launch time
                launch_time_al[i][j][k] = DRAM_launch_time + (total_decompr_time[i][j][k]) / 1000
    launch_time_al[0][0][0] = ZRAM_launch_time

    # print("Compression Latency AL")
    # print_three_dimensional_list(compr_lat_al, a, b, c)
    # print("Decompression Latency AL")
    # print_three_dimensional_list(decompr_lat_al, a, b, c)
    # print("Compression Ratio AL")
    # print_three_dimensional_list(compr_ratio_al, a, b, c)
    # print("CPU Time AL")
    # print_three_dimensional_list(cpu_time_al, a, b, c)
    # print("Launch Time AL")
    # print_three_dimensional_list(launch_time_al, a, b, c)

    compr_lat_ehl = init_three_dimensional_list(a, b, c)
    decompr_lat_ehl = init_three_dimensional_list(a, b, c)
    compr_ratio_ehl = init_three_dimensional_list(a, b, c)
    cpu_time_ehl = init_three_dimensional_list(a, b, c)
    launch_time_ehl = init_three_dimensional_list(a, b, c)

    for i in range(a):
        for j in range(b):
            for k in range(c):
                # Calculate the compression latency
                compr_lat_ehl[i][j][k] = (large_compr_time[i][j][k] + medium_compr_time[i][j][k]) / 1000
                # Calculate the decompression latency
                decompr_lat_ehl[i][j][k] = (large_decompr_time[i][j][k] + medium_decompr_time[i][j][k]) / 1000
                # Calculate the compression ratio
                compr_ratio_ehl[i][j][k] = (large_compr_size[i][j][k] + medium_compr_size[i][j][k])  / ((large_compr_size[0][0][0] + medium_compr_size[0][0][0]) / compr_ratio_al[0][0][0])
                # Calculate the CPU time
                cpu_time_ehl[i][j][k] = 1000 * (compr_lat_ehl[i][j][k] + decompr_lat_ehl[i][j][k]) / (total_compr_time[0][0][0] + total_decompr_time[0][0][0])
                # Calculate the launch time
                launch_time_ehl[i][j][k] = DRAM_launch_time + (large_decompr_time[i][j][k] + medium_decompr_time[i][j][k]) / 1000

    # print("Compression Latency EHL")
    # print_three_dimensional_list(compr_lat_ehl, a, b, c)
    # print("Decompression Latency EHL")
    # print_three_dimensional_list(decompr_lat_ehl, a, b, c)
    # print("Compression Ratio EHL")
    # print_three_dimensional_list(compr_ratio_ehl, a, b, c)
    # print("CPU Time EHL")
    # print_three_dimensional_list(cpu_time_ehl, a, b, c)
    # print("Launch Time EHL")
    # print_three_dimensional_list(launch_time_ehl, a, b, c)
    print("Launch Time", "for", app_name)
    print("ZRAM", launch_time_al[0][0][0])
    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        if idx == 0:
            continue
        print(f"{scale:<15} AL: {launch_time_al[idx][0][0]:<10} EHL: {launch_time_ehl[idx][0][0]:<10}")
    print("DRAM", DRAM_launch_time, '\n')

    print("CPU Time/Baseline", "for", app_name)
    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        if idx == 0:
            continue
        print(f"{scale:<15} AL: {cpu_time_al[idx][0][0]:<10.4f} EHL: {cpu_time_ehl[idx][0][0]:<10.4f}")
    print('\n')

    print("Compression Latency", "for", app_name)
    print("ZRAM", compr_lat_al[0][0][0])
    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        if idx == 0:
            continue
        print(f"{scale:<15} AL: {compr_lat_al[idx][0][0]:<10} EHL: {compr_lat_ehl[idx][0][0]:<10}")
    print('\n')

    print("Decompression Latency", "for", app_name)
    print("ZRAM", decompr_lat_al[0][0][0])
    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        if idx == 0:
            continue
        print(f"{scale:<15} AL: {decompr_lat_al[idx][0][0]:<10} EHL: {decompr_lat_ehl[idx][0][0]:<10}")
    print('\n')

    print("Compression Ratio", "for", app_name)
    print("ZRAM", f"{compr_ratio_al[0][0][0]:.4f}")
    for idx, (small_scale, medium_scale, large_scale, scale) in enumerate(specific_combinations):
        if idx == 0:
            continue
        print(f"{scale:<15} AL: {compr_ratio_al[idx][0][0]:<10.4f} EHL: {compr_ratio_ehl[idx][0][0]:<10.4f}")
    return

if __name__ == "__main__":
    main()
