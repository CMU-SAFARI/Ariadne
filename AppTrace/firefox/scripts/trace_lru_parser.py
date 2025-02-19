import re

app_id = "10130"
# read log.txt and swapout_trace.txt and swapin_trace.txt
lru_log_path = "./log.txt"
lru_trace_path = "./lru_list.txt"
bg_lru_trace_path = "./bg_lru_list.txt"
swapin_trace_path = "./swapin_trace.txt"
swapout_trace_path = "./swapout_trace.txt.meta"
bg_lru_log_path = "./record_bg.log"

# read all log file from log1.txt to log9.txt
log_files = []
lru_pfn_numbers = []
# for i in range(1, 2):
#     log_files.append(open(f"./log{i}.txt", 'r'))
#     # add empty list to lru_pfn_numbers
#     print(f"./log{i}.txt")
log_files.append(open(f"./record.log", 'r'))
# log_files.append(open(f"./record_bg.log", 'r'))
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
            number = number.split(',')
            if len(number) < 2:
                continue
            is_del = number[1]
            number = number[0]
            number = int(number.strip())
            # if last file, do not append the number to lru_pfn_numbers
            # if file_index == len(log_files) - 1:
            #     if number in lru_pfn_numbers:
            #         lru_pfn_numbers.remove(number)
            #     continue
            if is_del == "1":
                # find the number in the list and remove all the same number
                lru_pfn_numbers = [num for num in lru_pfn_numbers if num != number]
                continue
            lru_pfn_numbers.append(number)
    last_line = log_lines[-1]
    log_file.close()
print(len(lru_pfn_numbers))
print(len(set(lru_pfn_numbers)))

# read bg log file
bg_lru_pfn_numbers = []
bg_log_file = open(bg_lru_log_path, 'r')
bg_log_lines = bg_log_file.readlines()
for line in bg_log_lines:
    search_pattern = f'{app_id},'
    if search_pattern in line:
        # Split the line at the identifier and get the number right after it
        parts = line.split(search_pattern)[-1]
        number = parts.strip().split()[0] 
        # Ensure any trailing spaces or newline characters are removed
        number = number.split(',')
        if len(number) < 2:
            continue
        is_del = number[1]
        number = number[0]
        number = int(number.strip())
        lru_pfn_numbers = [num for num in lru_pfn_numbers if num != number]
        if is_del == "1":
            bg_lru_pfn_numbers = [num for num in bg_lru_pfn_numbers if num != number]
            continue  
        bg_lru_pfn_numbers.append(number)
bg_log_file.close()
    
def remove_duplicates_keep_last(lst):    
    # Create a new list based on the last occurrence indices while maintaining original order
    new_list = []
    seen = set()
    for index in reversed(range(len(lst))):
        value = lst[index]
        if value not in seen:
            seen.add(value)
            new_list.append(value)
    
    # Since we added the last occurrences in reverse order, we need to reverse the list back
    new_list.reverse()
    return new_list

# Example usage
result = remove_duplicates_keep_last(lru_pfn_numbers)

# read swapout trace
# swapout_file = open(swapout_trace_path, 'r')
# swapout_lines = swapout_file.readlines()
# swapout_pfn_numbers = []
# swapout_sec_numbers = []
# for line in swapout_lines:
#     line = line.strip()
#     parts = line.split(',')
#     # print(f"parts: {parts}")
#     # pfn = parts[1].split(',')[0]
#     if len(parts) < 3:
#         continue
#     pfn = parts[1]
#     sec = parts[2]
#     swapout_pfn_numbers.append(pfn)
#     swapout_sec_numbers.append(sec)
# swapout_file.close()

# remove pfn that not in swapout_pfn_numbers from result and keep the original reletive order
# result = [pfn for pfn in result if pfn in swapout_pfn_numbers]
print(len(result))

# print result to log_trace_path with format: appid,pfn
with open(lru_trace_path, 'w') as f:
    for item in result:
        f.write(f"{app_id},{item}\n")

with open(bg_lru_trace_path, 'w') as f:
    for item in bg_lru_pfn_numbers:
        f.write(f"{app_id},{item}\n")





