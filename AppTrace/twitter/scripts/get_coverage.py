import re

app_id = "10128"
stride = 1000
# read log.txt and swapout_trace.txt and swapin_trace.txt
lru_log_path = "./log.txt"
bg_log_path = "./bg_lru_list.txt"
ttid_swapin_trace_path = "./ttid_swapin_trace.txt"
ttfd_swapin_trace_path = "./ttfd_swapin_trace.txt"
swapin_trace_path = "./swapin_trace.txt"
swapout_trace_path = "./swapout_trace.txt.meta"

# read all log file from log1.txt to log9.txt
log_files = []
lru_pfn_numbers = []
# for i in range(1, 2):
#     log_files.append(open(f"./log{i}.txt", 'r'))
#     # add empty list to lru_pfn_numbers
#     print(f"./log{i}.txt")
log_files.append(open(f"./lru_list.txt", 'r'))
# List to store the extracted numbers for each file
# Process each line in the log file 
last_line = ""
skip_flag = True
for log_file in log_files:
    file_index = log_files.index(log_file)
    log_lines = log_file.readlines()
    if file_index > 0:
        # find the index of the last line in the current log file
        line_index = log_lines.index(last_line)
        # print(f"line_index: {line_index}")
        log_lines = log_lines[line_index+1:]
    for line in log_lines:
        search_pattern = f'{app_id},'
        if search_pattern in line:
            # Split the line at the identifier and get the number right after it
            parts = line.split(search_pattern)[-1]
            number = parts.strip().split()[0] 
            # Ensure any trailing spaces or newline characters are removed
            number = number.strip()
            lru_pfn_numbers.append(number)
    last_line = log_lines[-1]
    # print last_line
    # print(f"last_line: {last_line}")
    log_file.close()

# remove identical numbers in lru_pfn_numbers
# print(f"len(lru_pfn_numbers): {len(lru_pfn_numbers)}")
# open bg log file
bg_log_file = open(bg_log_path, 'r')
bg_log_lines = bg_log_file.readlines()
bg_lru_pfn_numbers = []
for line in bg_log_lines:
    line = line.strip()
    parts = line.split(',')
    if len(parts) < 2:
        continue
    pfn = parts[1]
    bg_lru_pfn_numbers.append(pfn)


# flatten lru_pfn_numbers
# lru_pfn_numbers = [item for sublist in lru_pfn_numbers for item in sublist]
# read swapout trace
swapout_file = open(swapout_trace_path, 'r')
swapout_lines = swapout_file.readlines()
swapout_pfn_numbers = []
swapout_sec_numbers = []
for line in swapout_lines:
    line = line.strip()
    parts = line.split(',')
    # print(f"parts: {parts}")
    # pfn = parts[1].split(',')[0]
    if len(parts) < 3:
        continue
    pfn = parts[1]
    sec = parts[2]
    swapout_pfn_numbers.append(pfn)
    swapout_sec_numbers.append(sec)
swapout_file.close()

# read launch swapin trace
ttid_swapin_file = open(ttid_swapin_trace_path, 'r')
ttid_swapin_lines = ttid_swapin_file.readlines()
ttid_swapin_sec_numbers = []
ttid_swapin_cnt = 0
for line in ttid_swapin_lines:
    line = line.strip()
    parts = line.split(',')
    sec = parts[1]
    ttid_swapin_sec_numbers.append(sec)
    ttid_swapin_cnt += 1
ttid_swapin_file.close()

ttfd_swapin_file = open(ttfd_swapin_trace_path, 'r')
ttfd_swapin_lines = ttfd_swapin_file.readlines()
ttfd_swapin_sec_numbers = []
ttfd_swapin_cnt = 0
for line in ttfd_swapin_lines:
    line = line.strip()
    parts = line.split(',')
    sec = parts[1]
    ttfd_swapin_sec_numbers.append(sec)
    ttfd_swapin_cnt += 1
ttfd_swapin_file.close()

# read swapin trace
swapin_file = open(swapin_trace_path, 'r')
swapin_lines = swapin_file.readlines()
swapin_sec_numbers = []
swapin_cnt = 0
for line in swapin_lines:
    line = line.strip()
    parts = line.split(',')
    sec = parts[1]
    swapin_sec_numbers.append(sec)
    swapin_cnt += 1
swapin_file.close()

# Check every swapin sec number, if it is in the swapout sec number, store the pfn corresponding to the sec number
swapout_pfn_numbers_matched = []
for sec_number in swapin_sec_numbers:
    if sec_number in swapout_sec_numbers:
        pfn = swapout_pfn_numbers[swapout_sec_numbers.index(sec_number)]
        swapout_pfn_numbers_matched.append(pfn)

swapout_ttid_pfn_numbers_matched = []
for sec_number in ttid_swapin_sec_numbers:
    if sec_number in swapout_sec_numbers:
        pfn = swapout_pfn_numbers[swapout_sec_numbers.index(sec_number)]
        swapout_ttid_pfn_numbers_matched.append(pfn)

swapout_ttfd_pfn_numbers_matched = []
for sec_number in ttfd_swapin_sec_numbers:
    if sec_number in swapout_sec_numbers:
        pfn = swapout_pfn_numbers[swapout_sec_numbers.index(sec_number)]
        swapout_ttfd_pfn_numbers_matched.append(pfn)

print(f"len(swapout_pfn_numbers_matched): {len(swapout_pfn_numbers_matched)}")
print(f"len(swapout_ttid_pfn_numbers_matched): {len(swapout_ttid_pfn_numbers_matched)}")
print(f"len(swapin_sec_numbers): {len(swapin_sec_numbers)}")
print(f"len(lru_pfn_numbers): {len(lru_pfn_numbers)}")

total_hot_ttid_page = 0
total_hot_ttfd_page = 0
total_warm_page = 0
total_cold_page = 0
total_not_swapout_page = 0


hot_ttid_page = 0
hot_ttfd_page = 0
warm_page = 0
cold_page = 0
not_swapout_page = 0
for window_size in range(stride, len(lru_pfn_numbers) + stride, stride):
    last_window_size = window_size - stride
    if window_size > len(lru_pfn_numbers):
        window_size = len(lru_pfn_numbers)
    lru_pfn_numbers_window = lru_pfn_numbers[last_window_size:window_size]
    hot_ttid_page = 0
    hot_ttfd_page = 0
    warm_page = 0
    cold_page = 0
    not_swapout_page = 0
    for pfn in lru_pfn_numbers_window:
        if pfn in swapout_ttid_pfn_numbers_matched:
            hot_ttid_page += 1
        elif pfn in swapout_ttfd_pfn_numbers_matched:
            hot_ttfd_page += 1
        elif pfn in swapout_pfn_numbers_matched:
            warm_page += 1
        elif pfn in swapout_pfn_numbers:
            cold_page += 1
    not_swapout_page = len(lru_pfn_numbers_window) - hot_ttid_page - hot_ttfd_page - warm_page - cold_page
    total_hot_ttid_page += hot_ttid_page
    total_hot_ttfd_page += hot_ttfd_page
    total_warm_page += warm_page
    total_cold_page += cold_page
    total_not_swapout_page += not_swapout_page
    print(f"{window_size} {hot_ttid_page} {hot_ttfd_page} {warm_page} {cold_page} {not_swapout_page}")

print(f"check from back")
for window_size in range(stride, 20000 + stride, stride):
    hot_ttid_page = 0
    hot_ttfd_page = 0
    warm_page = 0
    cold_page = 0
    not_swapout_page = 0
    lru_pfn_numbers_window = lru_pfn_numbers[-window_size:]
    for pfn in lru_pfn_numbers_window:
        if pfn in swapout_ttid_pfn_numbers_matched:
            hot_ttid_page += 1
        elif pfn in swapout_ttfd_pfn_numbers_matched:
            hot_ttfd_page += 1
        elif pfn in swapout_pfn_numbers_matched:
            warm_page += 1
        elif pfn in swapout_pfn_numbers:
            cold_page += 1
    not_swapout_page = len(lru_pfn_numbers_window) - hot_ttid_page - hot_ttfd_page - warm_page - cold_page
    print(f"{window_size} {hot_ttid_page} {hot_ttfd_page} {warm_page} {cold_page} {not_swapout_page}")



print(f"check bg lru")
hot_ttid_page = 0
hot_ttfd_page = 0
warm_page = 0
cold_page = 0
not_swapout_page = 0

for pfn in bg_lru_pfn_numbers:
    if pfn in swapout_ttid_pfn_numbers_matched:
        hot_ttid_page += 1
    elif pfn in swapout_ttfd_pfn_numbers_matched:
        hot_ttfd_page += 1
    elif pfn in swapout_pfn_numbers_matched:
        warm_page += 1
    elif pfn in swapout_pfn_numbers:
        cold_page += 1
    else:
        not_swapout_page += 1

print(f"bg lru {hot_ttid_page} {hot_ttfd_page} {warm_page} {cold_page} {not_swapout_page}")
