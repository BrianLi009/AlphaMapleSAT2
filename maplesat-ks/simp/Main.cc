/*****************************************************************************************[Main.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007,      Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include <errno.h>

#include <signal.h>
#include <zlib.h>
#include <sys/resource.h>

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
#include "simp/SimpSolver.h"

using namespace Minisat;

//=================================================================================================


void printStats(Solver& solver)
{
    double cpu_time = cpuTime();
    double mem_used = memUsedPeak();
    printf("database reductions   : %d\n", solver.reductions);
    printf("restarts              : %"PRIu64"\n", solver.starts);
    printf("conflicts             : %-12"PRIu64"   (%.0f /sec)\n", solver.conflicts   , solver.conflicts   /cpu_time);
    printf("decisions             : %-12"PRIu64"   (%4.2f %% random) (%.0f /sec)\n", solver.decisions, (float)solver.rnd_decisions*100 / (float)solver.decisions, solver.decisions   /cpu_time);
    printf("propagations          : %-12"PRIu64"   (%.0f /sec)\n", solver.propagations, solver.propagations/cpu_time);
    printf("conflict literals     : %-12"PRIu64"   (%4.2f %% deleted)\n", solver.tot_literals, (solver.max_literals - solver.tot_literals)*100 / (double)solver.max_literals);
    long double total_actual_rewards = 0;
    long double total_actual_count = 0;
    for (int i = 0; i < solver.nVars(); i++) {
        total_actual_rewards += solver.total_actual_rewards[i];
        total_actual_count += solver.total_actual_count[i];
    }
    printf("actual reward         : %Lf\n", total_actual_rewards / total_actual_count);
    if (mem_used != 0) printf("Memory used           : %.2f MB\n", mem_used);
    printf("CPU time              : %g s\n", cpu_time);
}


static Solver* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int signum) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
        printStats(*solver);
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }


//=================================================================================================
// Main:

int main(int argc, char** argv)
{
    try {
        setUsageHelp("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
        // printf("This is MiniSat 2.0 beta\n");
        
        // Extra options:
        //
        IntOption    verb   ("MAIN", "verb",   "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
        BoolOption   pre    ("MAIN", "pre",    "Completely turn on/off any preprocessing.", false);
        BoolOption   block_cubes    ("MAIN", "block-cubes",    "Add a conflict clause to block any skipped assumptions", true);
        StringOption dimacs ("MAIN", "dimacs", "If given, stop after preprocessing and write the result to this file.");
        StringOption assumptions ("MAIN", "assumptions", "If given, use the assumptions in the file.");
        IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", INT32_MAX, IntRange(0, INT32_MAX));
        IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", INT32_MAX, IntRange(0, INT32_MAX));
        IntOption    add_zeros("MAIN", "add-zeros","Number of initial variables to set to false.\n", 0, IntRange(0, INT32_MAX));
        IntOption    from_bound("MAIN", "from-bound","Start solving from this bound.\n", 0, IntRange(0, INT32_MAX));
        IntOption    to_bound  ("MAIN", "to-bound","Stop solving at this bound.\n", INT32_MAX, IntRange(0, INT32_MAX));

#if defined(__linux__) && defined(_FPU_EXTENDED) && defined(_FPU_DOUBLE) && defined(_FPU_GETCW)
        fpu_control_t oldcw, newcw;
        _FPU_GETCW(oldcw); newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE; _FPU_SETCW(newcw);
        if (verb > 1)
            printf("WARNING: for repeatability, setting FPU to use double precision\n");
#endif
        parseOptions(argc, argv, true);
        
        SimpSolver  S;
        double      initial_time = cpuTime();

        S.parsing = 1;
        if (!pre) S.eliminate(true);

        S.verbosity = verb;
        S.zerostoadd = add_zeros;
        
        solver = &S;
        // Use signal handlers that forcibly quit until the solver will be able to respond to
        // interrupts:
        signal(SIGINT, SIGINT_exit);
        signal(SIGXCPU,SIGINT_exit);

        // Set limit on CPU-time:
        if (cpu_lim != INT32_MAX){
            rlimit rl;
            getrlimit(RLIMIT_CPU, &rl);
            if (rl.rlim_max == RLIM_INFINITY || (rlim_t)cpu_lim < rl.rlim_max){
                rl.rlim_cur = cpu_lim;
                if (setrlimit(RLIMIT_CPU, &rl) == -1)
                    printf("WARNING! Could not set resource limit: CPU-time.\n");
            } }

        // Set limit on virtual memory:
        if (mem_lim != INT32_MAX){
            rlim_t new_mem_lim = (rlim_t)mem_lim * 1024*1024;
            rlimit rl;
            getrlimit(RLIMIT_AS, &rl);
            if (rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max){
                rl.rlim_cur = new_mem_lim;
                if (setrlimit(RLIMIT_AS, &rl) == -1)
                    printf("WARNING! Could not set resource limit: Virtual memory.\n");
            } }
        
        if (argc == 1)
            printf("Reading from standard input... Use '--help' for help.\n");

        gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(argv[1], "rb");
        if (in == NULL)
            printf("ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);
        
        if (S.verbosity > 0){
            printf("============================[ Problem Statistics ]=============================\n");
            printf("|                                                                             |\n"); }
        
        S.output = (argc >= 3) ? fopen(argv[2], "wb") : NULL;
        parse_DIMACS(in, S);
        // Specify upper-left matrix contains 0s
        for(int v=0; v<add_zeros; v++) {
            S.addClause(mkLit(v, true));
        }
#ifdef REMOVE_UNAPPEARING_VARS
        // Eliminate variables that do not appear in instance
        for(int v = 0; v < S.nVars(); v++) {
            if(S.appears[v] == false) {
                S.eliminated[v] = true;
                S.setDecisionVar(v, false);
                S.eliminated_vars++;
            }
        }
        S.appears.clear();
#endif
        gzclose(in);

        if (S.verbosity > 0){
            printf("|  Number of variables:  %12d                                         |\n", S.nVars());
            printf("|  Number of clauses:    %12d                                         |\n", S.nClauses()); }
        
        double parsed_time = cpuTime();
        if (S.verbosity > 0)
            printf("|  Parse time:           %12.2f s                                       |\n", parsed_time - initial_time);

        // Change to signal-handlers that will only notify the solver and allow it to terminate
        // voluntarily:
        signal(SIGINT, SIGINT_interrupt);
        signal(SIGXCPU,SIGINT_interrupt);

        S.parsing = 0;
        S.eliminate(true);
        double simplified_time = cpuTime();
        if (S.verbosity > 0){
            printf("|  Simplification time:  %12.2f s                                       |\n", simplified_time - parsed_time);
            printf("|                                                                             |\n"); }

        if (!S.okay()){
            if (S.output != NULL) fprintf(S.output, "0\n"), fclose(S.output);
            if (S.verbosity > 0){
                printf("===============================================================================\n");
                printf("Solved by simplification\n");
                printStats(S);
                printf("\n"); }
            printf("UNSATISFIABLE\n");
            exit(20);
        }

        if (dimacs){
            if (S.verbosity > 0)
                printf("==============================[ Writing DIMACS ]===============================\n");
            S.toDimacs((const char*)dimacs);
            if (S.verbosity > 0)
                printStats(S);
            exit(0);
        }

        int numsat = 0;
        FILE* outfile;
        lbool ret;
        vec<Lit> dummy;

        if (assumptions && block_cubes) {
            const char* file_name = assumptions;
            FILE* assertion_file = fopen (file_name, "r");
            if (assertion_file == NULL)
                printf("ERROR! Could not open file: %s\n", file_name), exit(1);
            int i = 0;
            int bound = 0;
            int tmp = fscanf(assertion_file, "a ");
            while (fscanf(assertion_file, "%d ", &i) == 1) {
                if(i==0)
                {
                  if(bound > to_bound || bound < from_bound) { // Add a clause that block any skipped assumptions
                      vec<Lit> block;
                      for(int j=0; j<dummy.size(); j++)
                          block.push(~dummy[j]);
                      S.addClause(block);
                  }
                  bound++;
                  dummy.clear();
                  tmp = fscanf(assertion_file, "a ");
                }
                else
                {
                  Var v = abs(i) - 1;
                  Lit l = i > 0 ? mkLit(v) : ~mkLit(v);
                  dummy.push(l);
                }
            }
            fclose(assertion_file);
        }

        if (assumptions) {
            const char* file_name = assumptions;
            FILE* assertion_file = fopen (file_name, "r");
            if (assertion_file == NULL)
                printf("ERROR! Could not open file: %s\n", file_name), exit(1);
            int i = 0;
            int bound = 0;
            int tmp = fscanf(assertion_file, "a ");
            while (fscanf(assertion_file, "%d ", &i) == 1) {
                if(i==0)
                {
                  if(bound > to_bound) break; // Stop solving once given to_bound is reached
                  if(bound < from_bound) { // Don't start solving until from_bound is reached
                      bound++;
                      dummy.clear();
                      tmp = fscanf(assertion_file, "a ");
                      continue;
                  }
                  if(S.verbosity > 0)
                  {  printf("Bound %d: ", bound);
                     printf("a ");
                     for( int i = 0; i < dummy.size(); i++)
                       printf("%s%d ", sign(dummy[i]) ? "-" : "", var(dummy[i])+1);
                     printf("0\n");
                  }
                  double start_time = cpuTime();
                  ret = S.solveLimited(dummy);
                  printf("Bound %d time: %.2f sec\n", bound, cpuTime() - start_time);
                  bound++;
                  if(S.verbosity > 0)
                    printf(ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
                  dummy.clear();
                  tmp = fscanf(assertion_file, "a ");
                  if(ret==l_True)
                    numsat++;
                  else if(ret==l_Undef)
                    break;
                }
                else
                {
                  Var v = abs(i) - 1;
                  Lit l = i > 0 ? mkLit(v) : ~mkLit(v);
                  dummy.push(l);
                }
            }
            fclose(assertion_file);
        }
        else
            ret = S.solveLimited(dummy);
        
        if (S.verbosity > 0){
            if(assumptions)
                printf("Number of satisfiable bounds: %d\n", numsat);
            printStats(S);
            printf("\n");
            //printf(ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
            printf(numsat > 0 || ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
        }
        if (S.output != NULL){
            if (ret == l_True){
                fclose(S.output);                 // Close the proof file
                S.output = fopen(argv[2], "wb");  // Clear it to put in the solution
                for (int i = 0; i < S.nVars(); i++)
                    if (S.model[i] != l_Undef)
                        fprintf(S.output, "%s%s%d", (i==0)?"":" ", (S.model[i]==l_True)?"":"-", i+1);
                fprintf(S.output, " 0\n");
            }else if (ret == l_False){
                if (!assumptions){
                    fprintf(S.output, "0\n");
                }
            }/*else{
                fprintf(S.output, "INDET\n");
            }*/
            fclose(S.output);
        }

#ifdef NDEBUG
        exit(ret == l_True ? 10 : ret == l_False ? 20 : 0);     // (faster than "return", which will invoke the destructor for 'Solver')
#else
        return (ret == l_True ? 10 : ret == l_False ? 20 : 0);
#endif
    } catch (OutOfMemoryException&){
        printf("===============================================================================\n");
        printf("INDETERMINATE\n");
        exit(0);
    }
}