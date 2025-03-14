#!/bin/bash

# Check if the correct number of arguments is provided
if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <input_file> <order> <num_conflicts> [-cas|-exhaustive-no-cas]"
    exit 1
fi

input_file=$1
order=$2
num_conflicts=$3
mode=""

# Check if a mode is specified
if [ "$#" -eq 4 ]; then
    mode=$4
fi

# Create output file names
output_file="${input_file}.simp"
output_log="${input_file}.simplog"
output_ext="${input_file}.ext"

# Run the appropriate solver based on the mode
if [ "$mode" = "-cas" ]; then
    echo "Running simplification with CAS mode"
    ./cadical-ks/build/cadical-ks "$input_file" --order "$order" -c "$num_conflicts" -o "$output_file" -e "$output_ext" | tee "$output_log"
    # Output final simplified instance
    ./gen_cubes/concat-edge.sh $order "$output_file" "$output_ext" > "${output_file}.tmp" && mv "${output_file}.tmp" "$output_file"
elif [ "$mode" = "-exhaustive-no-cas" ]; then
    echo "Running simplification with exhaustive search mode (no CAS)"
    ./cadical-ks/build/cadical-ks "$input_file" --order "$order" --exhaustive -c "$num_conflicts" -o "$output_file" -e "$output_ext" | tee "$output_log"
    # Output final simplified instance
    ./gen_cubes/concat.sh "$output_file" "$output_ext" > "${output_file}.tmp" && mv "${output_file}.tmp" "$output_file"
else
    echo "Running standard simplification"
    ./cadical-ks/build/cadical-ks "$input_file" -c "$num_conflicts" -o "$output_file" -e "$output_ext" | tee "$output_log"
    ./gen_cubes/concat.sh "$output_file" "$output_ext" > "${output_file}.tmp" && mv "${output_file}.tmp" "$output_file"
fi