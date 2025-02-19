def read_data(file_path):
    """Reads data from a file and returns a list of sector numbers."""
    sector_numbers = []
    with open(file_path, 'r') as file:
        for line in file:
            parts = line.strip().split(',')
            if len(parts) == 2:  # Ensure correct format
                sector_numbers.append(int(parts[1]))
    return sector_numbers

def check_adjacent_sequence(window, sequence_length):
    """Checks for the presence of adjacent sequences of the given length where each element differs by 8."""
    count = 0
    # sort the window
    window.sort()
    # check if first seqence_length elements form a sequence, 
    # if so increment count and delete the sequence; if not, delete the first element
    while len(window) >= sequence_length:
        if all(window[i] + 8 == window[i + 1] for i in range(sequence_length - 1)):
            count += 1
            del window[:sequence_length]
        else:
            del window[0]
    return count

def count_adjacent_sequences(sector_numbers, window_size, sequence_lengths):
    """Calculates adjacent sequences within specified window sizes for various lengths."""
    sequence_counts = {length: [] for length in sequence_lengths}
    # Loop through the list with a sliding window
    for length in sequence_lengths:
        for i in range(len(sector_numbers) - window_size + 1):
            window = sector_numbers[i:i + window_size]
            sequence_count = check_adjacent_sequence(window, length)
            sequence_counts[length].append(sequence_count)
    # print(sequence_counts)
    return sequence_counts

def calculate_average(counts):
    """Calculates the average of the given counts."""
    return sum(counts) / len(counts) if counts else 0

def main():
    file_path = 'swapin_trace.txt'  # Update this with the actual file path
    window_size = 256  # Define the window size as needed
    sequence_lengths = [2, 4, 8]  # Lengths of sequences to check
    sector_numbers = read_data(file_path)
    
    # Count adjacent sequences in windows
    sequence_counts = count_adjacent_sequences(sector_numbers, window_size, sequence_lengths)
    
    # Calculate and print averages for each sequence length
    for length in sequence_lengths:
        average_count = calculate_average(sequence_counts[length])
        # calculate overall count for each length
        # count = sum(sequence_counts[length])
        # print(f"Total number of adjacent sequences of length {length} per window of size {window_size}: {count}")
        print(f"{average_count:.2f}")    

if __name__ == "__main__":
    main()
