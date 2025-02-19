import re
import struct
import sys
def main():
    # give a list of app_id
    app_ids = [10123, 10124, 10128, 10129, 10130]
    # read log.txt and swapout_trace.txt and swapin_trace.txt
    lru_log_path = "./lru_list.txt"
    bg_lru_log_path = "./bg_lru_list.txt"
    swapin_trace_path = "./swapin_trace.txt"
    swapout_trace_path = "./swapout_trace.txt.meta"
    compression_trace_path = "./compression_trace.txt"

    # read all log file from log1.txt to log9.txt
    log_files = []
    bg_log_files = []
    lru_pfn_numbers = []
    bg_lru_pfn_numbers = []
    small_lru_pfn_numbers = []
    medium_lru_pfn_numbers = []
    log_files.append(open(lru_log_path, 'r'))
    bg_log_files.append(open(bg_lru_log_path, 'r'))
    if len(sys.argv) != 3:
        print("Usage: python compression_scale_identify.py <hot_list_length> <warm_list_length>")
        return

    hot_list_length = int(sys.argv[1])
    warm_list_length = int(sys.argv[2])
    # print(f"hot_list_length: {hot_list_length}")
    # print(f"warm_list_length: {warm_list_length}")
    # List to store the extracted numbers for each file
    # Process each line in the log file 
    for app_id in app_ids:
        # print(app_ids.index(app_id))
        lru_pfn_numbers.append([])  # Initialize the list for each app_id
        bg_lru_pfn_numbers.append([])  # Initialize the list for each app_id
        small_lru_pfn_numbers.append([])  # Initialize the list for each app_id
        medium_lru_pfn_numbers.append([])  # Initialize the list for each app_id
        for log_file in log_files:
            # print(f"log_file: {log_file}")
            file_index = log_files.index(log_file)
            log_lines = log_file.readlines()
            # reset the file pointer to the beginning of the file
            log_file.seek(0)
            for line in log_lines:
                search_pattern = f'{app_id},'
                if search_pattern in line:
                    parts = line.split(search_pattern)[-1]
                    number = parts.strip().split()[0] 
                    number = int(number.strip())
                    lru_pfn_numbers[app_ids.index(app_id)].append(number)
        for bg_log_file in bg_log_files:
            # print(f"bg_log_file: {bg_log_file}")
            bg_file_index = bg_log_files.index(bg_log_file)
            bg_log_lines = bg_log_file.readlines()
            # reset the file pointer to the beginning of the file
            bg_log_file.seek(0)
            for line in bg_log_lines:
                search_pattern = f'{app_id},'
                if search_pattern in line:
                    parts = line.split(search_pattern)[-1]
                    number = parts.strip().split()[0] 
                    number = int(number.strip())
                    bg_lru_pfn_numbers[app_ids.index(app_id)].append(number)
    for log_file in log_files:       
        log_file.close()
    for bg_log_file in bg_log_files:
        bg_log_file.close()

    # keep first 45000 entries of lru_pfn_numbers for each app_id
    for app_id in app_ids:
        small_lru_pfn_numbers[app_ids.index(app_id)] = lru_pfn_numbers[app_ids.index(app_id)][:hot_list_length]
        medium_lru_pfn_numbers[app_ids.index(app_id)] = lru_pfn_numbers[app_ids.index(app_id)][hot_list_length:warm_list_length]
        medium_lru_pfn_numbers[app_ids.index(app_id)].extend(bg_lru_pfn_numbers[app_ids.index(app_id)])
    # flatten lru_pfn_numbers
    # lru_pfn_numbers = [item for sublist in lru_pfn_numbers for item in sublist]
    # read swapout trace
    # create a comression_trace.txt file
    compression_file = open(compression_trace_path, 'wb')
    swapout_file = open(swapout_trace_path, 'r')
    swapout_lines = swapout_file.readlines()
    swapout_appid_numbers = []
    swapout_pfn_numbers = []
    swapout_sec_numbers = []
    for line in swapout_lines:
        line = line.strip()
        parts = line.split(',')
        if len(parts) < 3:
            continue
        compression_scale = 0
        if parts[0].isdigit() and parts[1].isdigit() and parts[2].isdigit():
            app_id = int(parts[0])
            pfn = int(parts[1].split(',')[0])
            sec = int(parts[2])
        else:
            print(f"Invalid line: {line}")
            app_id = 0
            pfn = 0
            sec = 0
            # compression_file.write(f"{app_id},{pfn},{sec},{compression_scale}\n")
            continue
        if pfn in small_lru_pfn_numbers[app_ids.index(app_id)]:
            compression_scale = 0
        elif pfn in medium_lru_pfn_numbers[app_ids.index(app_id)]:
            compression_scale = 1
        else:
            compression_scale = 2
        app_id_bytes = app_id.to_bytes(8, byteorder='little', signed=False)
        pfn_bytes = pfn.to_bytes(8, byteorder='little', signed=False)
        sec_bytes = sec.to_bytes(8, byteorder='little', signed=False)
        compression_scale_bytes = compression_scale.to_bytes(8, byteorder='little', signed=False)
        compression_file.write(app_id_bytes)
        compression_file.write(pfn_bytes)
        compression_file.write(sec_bytes)
        compression_file.write(compression_scale_bytes)
        # print(f"app_id: {app_id}, pfn: {pfn}, sec: {sec}, compression_scale: {compression_scale}")
        swapout_appid_numbers.append(app_id)
        swapout_pfn_numbers.append(pfn)
        swapout_sec_numbers.append(sec)
    swapout_file.close()
    compression_file.close()


    # print(f"len(lru_pfn_numbers): {len(lru_pfn_numbers)}")
    # print(f"len(swapout_appid_numbers): {len(swapout_appid_numbers)}")
    # print(f"len(set(lru_pfn_numbers)): {len(set(lru_pfn_numbers))}")

if __name__ == "__main__":
    main()