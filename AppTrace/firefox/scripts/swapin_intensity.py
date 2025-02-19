def process_relative_timestamps(file_path):
    import re

    # Read the file and extract all timestamps
    timestamps = []
    with open(file_path, 'r') as file:
        for line in file:
            match = re.search(r'\[(\d+)\]', line)
            if match:
                timestamps.append(int(match.group(1)))

    # Determine the first timestamp to set as zero time
    first_timestamp = timestamps[0]

    # Dictionary to hold counts per second relative to the first timestamp
    counts_per_second = {}

    # Process each timestamp
    for timestamp in timestamps:
        # Calculate relative time in seconds
        relative_time_seconds = (timestamp - first_timestamp) // 1_000_000

        # Increment the count for this second
        if relative_time_seconds in counts_per_second:
            counts_per_second[relative_time_seconds] += 1
        else:
            counts_per_second[relative_time_seconds] = 1

    return counts_per_second

# Example usage
file_path = 'swapin_trace.txt'  # Update with your file path
result = process_relative_timestamps(file_path)
for second, count in result.items():
    print(second,count)
