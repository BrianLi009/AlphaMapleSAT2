import subprocess
import multiprocessing
import os
import queue
import argparse

def run_command(command):
    process_id = os.getpid()
    print(f"Process {process_id}: Executing command: {command}")

    file_to_cube = command.split()[-1]

    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        stdout, stderr = process.communicate()

        if stderr:
            print(f"Error executing command: {stderr.decode()}")

        if "UNSAT" in stdout.decode():
            print("solved")
            # remove_related_files(file_to_cube)
            process.terminate()
        else:
            print("Continue cubing this subproblem...")
            command = f"cube('{file_to_cube}', 'N', 0, {mg}, '{orderg}', {numMCTSg}, queue, '{cutoffg}', {cutoffvg}, {dg}, 'True')"
            queue.put(command)

    except Exception as e:
        print(f"Failed to run command due to: {str(e)}")

def run_cube_command(command):
    print (command)
    eval(command)

def remove_related_files(new_file):
    files_to_remove = [
        new_file,
    ]

    for file in files_to_remove:
        try:
            os.remove(file)
            print(f"Removed: {file}")
        except OSError as e:
            print(f"Error: {e.strerror}. File: {file}")

def rename_file(filename):
    # Remove .simp from file name
    
    if filename.endswith('.simp'):
        filename = filename[:-5]
    
    return filename
    
def worker(queue):
    while True:
        args = queue.get()
        if args is None:
            queue.task_done()
            break
        if args.startswith("./solve"):
            run_command(args)
        else:
            run_cube_command(args)
        queue.task_done()

def cube(original_file, cube, index, m, order, numMCTS, queue, cutoff='d', cutoffv=5, d=0, extension="False"):
    global solving_mode_g, cubing_mode_g
    
    if cube != "N":
        if solving_mode_g == "satcas":
            command = f"./gen_cubes/apply.sh {original_file} {cube} {index} > {cube}{index}.cnf && ./simplification/simplify-by-conflicts.sh {cube}{index}.cnf {order} 10000 -cas"
        else:
            command = f"./gen_cubes/apply.sh {original_file} {cube} {index} > {cube}{index}.cnf && ./simplification/simplify-by-conflicts.sh {cube}{index}.cnf {order} 10000"
        file_to_cube = f"{cube}{index}.cnf.simp"
        simplog_file = f"{cube}{index}.cnf.simplog"
        file_to_check = f"{cube}{index}.cnf.ext"
    else:
        if solving_mode_g == "satcas":
            command = f"./simplification/simplify-by-conflicts.sh {original_file} {order} 10000 -cas"
        else:
            command = f"./simplification/simplify-by-conflicts.sh {original_file} {order} 10000"
        file_to_cube = f"{original_file}.simp"
        simplog_file = f"{original_file}.simplog"
        file_to_check = f"{original_file}.ext"
    subprocess.run(command, shell=True)

    # Check if the output contains "c exit 20"
    with open(simplog_file, "r") as file:
        if "c exit 20" in file.read():
            print("the cube is UNSAT")
            if cube != "N":
                os.remove(f'{cube}{index}.cnf')
            os.remove(file_to_cube)
            os.remove(file_to_check)
            return
    
    command = f"sed -E 's/.* 0 [-]*([0-9]*) 0$/\\1/' < {file_to_check} | awk '$0<={m}' | sort | uniq | wc -l"
    result = subprocess.run(command, shell=True, text=True, capture_output=True)
    var_removed = int(result.stdout.strip())
    if extension == "True":
        cutoffv = var_removed * 2

    print (f'{var_removed} variables removed from the cube')

    if cutoff == 'd':
        if d >= cutoffv:
            if solveaftercubeg == 'True':
                os.remove(f'{cube}{index}.cnf')
                if solving_mode_g == "satcas":
                    command = f"./solve.sh {order} -maplesat {timeout_g} -cas {file_to_cube}"
                else:
                    command = f"./solve.sh {order} -maplesat {timeout_g} {file_to_cube}"
                queue.put(command)
            return
    if cutoff == 'v':
        if var_removed >= cutoffv:
            if solveaftercubeg == 'True':
                os.remove(f'{cube}{index}.cnf')
                if solving_mode_g == "satcas":
                    command = f"./solve.sh {order} -maplesat {timeout_g} -cas {file_to_cube}"
                else:
                    command = f"./solve.sh {order} -maplesat {timeout_g} {file_to_cube}"
                queue.put(command)
            return

    # Select cubing method based on cubing_mode
    if cubing_mode_g == "march":
        subprocess.run(f"./march/march_cu {file_to_cube} -d 1 -m {m} -o {file_to_cube}.temp", shell=True)
    else:  # ams mode
        subprocess.run(f"python3 -u alpha-zero-general/main.py {file_to_cube} -d 1 -m {m} -o {file_to_cube}.temp -order {order} -prod -numMCTSSims {numMCTS}", shell=True)

    #output {file_to_cube}.temp with the cubes
    d += 1
    if cube != "N":
        subprocess.run(f'''sed -E "s/^a (.*)/$(head -n {index} {cube} | tail -n 1 | sed -E 's/(.*) 0/\\1/') \\1/" {file_to_cube}.temp > {cube}{index}''', shell=True)
        next_cube = f'{cube}{index}'
    else:
        subprocess.run(f'mv {file_to_cube}.temp {original_file}0', shell=True)
        next_cube = f'{original_file}0'
    if cube != "N":
        os.remove(f'{cube}{index}.cnf')
        os.remove(f'{file_to_cube}.temp')
    os.remove(file_to_cube)
    os.remove(file_to_check)
    command1 = f"cube('{original_file}', '{next_cube}', 1, {m}, '{order}', {numMCTS}, queue, '{cutoff}', {cutoffv}, {d})"
    command2 = f"cube('{original_file}', '{next_cube}', 2, {m}, '{order}', {numMCTS}, queue, '{cutoff}', {cutoffv}, {d})"
    queue.put(command1)
    queue.put(command2)

def main(order, file_name_solve, solving_mode="other", cubing_mode="march", numMCTS=2, cutoff='d', cutoffv=5, solveaftercube='True', timeout=3600):
    """
    Parameters:
    - order: the order of the graph
    - file_name_solve: input file name
    - solving_mode: 'satcas' (cadical simplification with cas, maplesat solving with cas) 
                   or 'other' (cadical simplification no cas, maplesat solving no cas)
    - cubing_mode: 'march' (use march_cu) or 'ams' (use alpha-zero-general)
    - numMCTS: number of MCTS simulations (only used with ams mode)
    - cutoff: 'd' for depth-based or 'v' for variable-based
    - cutoffv: cutoff value
    - solveaftercube: whether to solve after cubing
    - timeout: timeout in seconds (default: 1 hour)
    """
    # Validate input parameters
    if solving_mode not in ["satcas", "other"]:
        raise ValueError("solving_mode must be either 'satcas' or 'other'")
    if cubing_mode not in ["march", "ams"]:
        raise ValueError("cubing_mode must be either 'march' or 'ams'")

    d = 0
    cutoffv = int(cutoffv)
    m = int(int(order)*(int(order)-1)/2)
    
    # Update global variables
    global queue, orderg, numMCTSg, cutoffg, cutoffvg, dg, mg, solveaftercubeg, file_name_solveg, solving_mode_g, cubing_mode_g, timeout_g
    orderg, numMCTSg, cutoffg, cutoffvg, dg, mg, solveaftercubeg, file_name_solveg = order, numMCTS, cutoff, cutoffv, d, m, solveaftercube, file_name_solve
    solving_mode_g = solving_mode
    cubing_mode_g = cubing_mode
    timeout_g = timeout

    queue = multiprocessing.JoinableQueue()
    num_worker_processes = multiprocessing.cpu_count()

    # Start worker processes
    processes = [multiprocessing.Process(target=worker, args=(queue,)) for _ in range(num_worker_processes)]
    for p in processes:
        p.start()

    #file_name_solve is a file where each line is a filename to solve
    with open(file_name_solve, 'r') as file:
        first_line = file.readline().strip()  # Read the first line and strip whitespace

        # Check if the first line starts with 'p cnf'
        if first_line.startswith('p cnf'):
            print("input file is a CNF file")
            cube(file_name_solve, "N", 0, m, order, numMCTS, queue, cutoff, cutoffv, d)
        else:
            print("input file contains name of multiple CNF file, solving them first")
            # Prepend the already read first line to the list of subsequent lines
            instance_lst = [first_line] + [line.strip() for line in file]
            for instance in instance_lst:
                if solving_mode_g == "satcas":
                    command = f"./solve.sh {order} -maplesat {timeout_g} -cas {instance}"
                else:
                    command = f"./solve.sh {order} -maplesat {timeout_g} {instance}"
                queue.put(command)

    # Wait for all tasks to be completed
    queue.join()

    # Stop workers
    for _ in processes:
        queue.put(None)
    for p in processes:
        p.join()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        epilog='Example usage: python3 parallel-solve.py 17 instances/ks_17.cnf --solving-mode satcas --cubing-mode ams --timeout 7200'
    )
    parser.add_argument('order', type=int, help='Order of the graph')
    parser.add_argument('file_name_solve', help='Input file name')
    parser.add_argument('--solving-mode', choices=['satcas', 'other'], default='other',
                        help='Solving mode: satcas (cadical+cas) or other (default)')
    parser.add_argument('--cubing-mode', choices=['march', 'ams'], default='march',
                        help='Cubing mode: march (default) or ams (alpha-zero-general)')
    parser.add_argument('--numMCTS', type=int, default=2,
                        help='Number of MCTS simulations (only for ams mode)')
    parser.add_argument('--cutoff', choices=['d', 'v'], default='d',
                        help='Cutoff type: d (depth-based) or v (variable-based)')
    parser.add_argument('--cutoffv', type=int, default=5,
                        help='Cutoff value')
    parser.add_argument('--solveaftercube', choices=['True', 'False'], default='True',
                        help='Whether to solve after cubing')
    parser.add_argument('--timeout', type=int, default=3600,
                        help='Timeout in seconds (default: 3600)')

    args = parser.parse_args()
    main(args.order, args.file_name_solve, args.solving_mode, args.cubing_mode,
         args.numMCTS, args.cutoff, args.cutoffv, args.solveaftercube, args.timeout)