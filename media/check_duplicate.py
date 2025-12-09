import os
import sys
from typing import Dict
import xxhash

def check_duplicate(path_string: str):
    # Resolve relative path to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    abs_folder_path = os.path.abspath(os.path.join(script_dir, path_string))
    
    if not ( os.path.exists(abs_folder_path) and os.path.isdir(abs_folder_path) ):
        print(f"Error: '{abs_folder_path}' does not exist or is not a directory")
        sys.exit(1)
    
    # hash -> filename
    hash_dict: Dict[int, str] = {}
    
    # Traverse all files in directory
    file_count = 0
    for filename in os.listdir(abs_folder_path):
        filepath = os.path.join(abs_folder_path, filename)
        
        if not os.path.isfile(filepath):
            print(f"{filepath} is not a file, skipped")
            continue

        try:
            # Read entire file at once - acceptable for ~65MB files
            with open(filepath, 'rb') as f:
                file_data = f.read()
                
            # Compute XXH64 hash in one step
            file_hash = xxhash.xxh64(file_data).hexdigest()
                
            # Check for duplicate
            if file_hash in hash_dict:
                print(f"Duplicate found!\n"
                        f"File 1: {hash_dict[file_hash]}\n"
                        f"File 2: {filepath}")
                sys.exit(1)
                
            # Store new hash
            hash_dict[file_hash] = filepath
            file_count += 1

            # Explicitly clear memory to help GC (optional but good practice)
            del file_data
                
        except IOError as e:
            print(f"Error reading {filepath}: {str(e)}", file=sys.stderr)
        except MemoryError:
            print(f"Memory error processing {filepath} - file too large?", file=sys.stderr)
            sys.exit(1)
    
    print(f"No duplicates found in {file_count} files")
    sys.exit(0)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python check_duplicate.py <folder_path>")
        sys.exit(1)
    
    folder_path = sys.argv[1]

    check_duplicate(folder_path)
    