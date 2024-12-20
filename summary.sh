#!/bin/bash

# Check if a directory name was provided
if [ $# -lt 2 ]; then
  echo "Usage: $0 <directory-name> <timeout>"
  exit 1
fi

# Change to the specified directory
cd "$1" || exit

# Initialize variables
solving_time=0
verification_time=0
leaf_cubes=0
total_cubes=0
timeouted_cubes=0

# Read the timeout parameter
timeout=$2

# Compute Solving Time and Verification Time together
while IFS= read -r file; do
    if [[ "$file" == *.log ]]; then
        # Count as a leaf cube file
        ((leaf_cubes++))
        
        # Check for CPU time in the file
        if grep -q "CPU time" "$file"; then
            # Add to solving time
            time=$(grep "CPU time" "$file" | awk '{total += $(NF-1)} END {print total}')
            solving_time=$(echo "$solving_time + $time" | bc)
        else
            # Count as a timeouted cube and add CPU time as 0
            ((timeouted_cubes++))
        fi
    elif [[ "$file" == *.verify ]]; then
        # Add to verification time
        time=$(grep 'c verification time:' "$file" | awk '{sum += $4} END {print sum}')
        verification_time=$(echo "$verification_time + $time" | bc)
    fi
done < <(find . -type f \( -name "*.log" -o -name "*.verify" \))

# Count total cubes (all nodes) using simplog files
total_cubes=$(find . -name "*.simplog" | wc -l)

# Compute Cubing Time - check both "Tool runtime" and "c time" formats
cubing_time=$(grep -E 'Tool runtime|c time = ' slurm-*.out | awk '
    /Tool runtime/ {sum += $3}
    /c time = / {sum += $4}
    END {print sum}
')

# Compute Simp Time
simp_time=$(grep 'c total process time since initialization:' *.simplog | awk '{SUM += $(NF-1)} END {print SUM}' | bc)

# Output the results
echo "Solving Time: $solving_time"
echo "Cubing Time: $cubing_time"
echo "Simp Time: $simp_time"
echo "Verification Time: $verification_time seconds"
echo "# of Leaf Cubes: $leaf_cubes"
echo "# of Total Cubes: $total_cubes"
echo "# of Timeouted Cubes: $timeouted_cubes"

# Compute and output solving time including timeouts
solving_time_with_timeout=$(echo "$solving_time + $timeouted_cubes * $timeout" | bc)
echo "Solving Time (including timeout): $solving_time_with_timeout"
