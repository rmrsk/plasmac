import os
import argparse
import sys
import configparser
import subprocess
#from subprocess import DEVNULL, STDOUT, check_call
sys.path.append('./python')

tests_file       = "tests.ini"
regression_rules = "regression_rules.py"

# Arguments for this script
parser = argparse.ArgumentParser()
parser.add_argument('-all', '--all',          help="Run all tests", action='store_true', )
parser.add_argument('-compile', '--compile',  help="Compile executables", action='store_true')
parser.add_argument('-build_procs',           help="Number of processors to use for compiling compiling", type=int, default=1)
parser.add_argument('--benchmark',            help="Generate benchmark files only", action='store_true')
parser.add_argument('-tests',                 help="Run one or more regression tests", nargs='+', required=False)
parser.add_argument('--silent',               help="Turn off unnecessary output", action='store_true')
parser.add_argument('--clean',                help="Do a clean compile", action='store_true')
parser.add_argument('-dim',                   help="Regression suite dimensions", type=int, default=2)
parser.add_argument('-run',                   help="MPI run command", type=str, default="mpirun")

# Read arguments and configuration files
args = parser.parse_args()
config = configparser.ConfigParser()
config.read(tests_file)
baseDir = os.getcwd()

def pre_check(silent):
    """ Check that PLASMAC has been appropriately set up with an environment variable. 
        Print some error messages and what to do if we can't run anything. """
    print("Running " + __file__ + "...")
    plasmac_home = os.environ.get("PLASMAC_HOME")
    if not plasmac_home:
        print("Error: Cannot run regression tests because the PLASMAC_HOME environment variable has not been set.")
        print("Please set PLASMAC_HOME, for example: '> export  PLASMAC_HOME=<directory>'")
        print("Aborting regtest suite")
        exit()
    else:
        if not silent:
            print("CWD          = " + os.getcwd())
            print("PLASMAC_HOME = " + plasmac_home)


def compile_test(silent, build_procs, dim, clean):
    """ Set up and run a compilation of the target test. """
    if args.compile:
        makeCommand = "make "
        if silent:
            makeCommand += "-s "
        makeCommand += "-j" + str(build_procs) + " "
        makeCommand += "DIM=" + str(dim) + " "
        if clean:
            makeCommand += "clean "
        makeCommand += "main"

        print("\t Compiling with = '" + str(makeCommand) + "'\n")
        os.system(makeCommand)

# Do a sanity check before trying tests. 
pre_check(args.silent)


# Run all tests
for test in config.sections():

    # --------------------------------------------------
    # Check that the configuration parser has the
    # appropriate keys
    # --------------------------------------------------
    do_test = True
    if not config.has_option(str(test), 'directory'):
        print(tests_file + " does not contain option [" + str(test) + "][directory]. Skipping this test")
        do_test = False
    if not config.has_option(str(test), 'exec'):
        print(tests_file + " does not contain option [" + str(test) + "][exec]. Skipping this test")
        do_test = False
    if not config.has_option(str(test), 'input'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][input]. Skipping this test")
    if not config.has_option(str(test), 'output'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][output]. Skipping this test")
    if not config.has_option(str(test), 'num_procs'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][num_procs]. Skipping this test")
    if not config.has_option(str(test), 'benchmark'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][benchmark]. Skipping this test")
    if not config.has_option(str(test), 'nsteps'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][nsteps]. Skipping this test")
    if not config.has_option(str(test), 'plot_interval'):
        do_test = False
        print(tests_file + " does not contain option [" + str(test) + "][plot_interval]. Skipping this test")
        
    # --------------------------------------------------
    # If moron check passed, try to run the test
    # --------------------------------------------------
    if do_test:
        # --------------------------------------------------
        # Get test suite parameters from .ini file and
        # convert them to the types that they represent. 
        # --------------------------------------------------
        directory  = str(config[str(test)]['directory'])
        executable = str(config[str(test)]['exec']) + str(args.dim) + "d.*.ex"
        input      = str(config[str(test)]['input']) + str(args.dim) + "d.inputs"
        nplot      = int(config[str(test)]['plot_interval'])
        nsteps     = int(config[str(test)]['nsteps'])
        cores      = int(config[str(test)]['num_procs'])
        if args.benchmark:
            output     = str(config[str(test)]['benchmark'])
        else:
            output     = str(config[str(test)]['output'])
        
            # --------------------------------------------------
            # Print some information about the regression test 
            # being run. 
            # --------------------------------------------------
            print("Running regression test '" + str(test) + "' with dim=" + str(args.dim))
            if not args.silent:
                if args.benchmark:
                    print("\t Running benchmark!")
                    print("\t Directory is  = " + directory)
                    print("\t Input file is = " + input)
                    print("\t Output files are = " + str(output) + ".stepXXXXXXX." + str(args.dim) + "d.hdf5")

            # --------------------------------------------------
            # Now change to test directory
            # --------------------------------------------------
            os.chdir(baseDir + "/" + directory) 

            # --------------------------------------------------
            # Compile test if user has called for it
            # --------------------------------------------------
            if args.compile:
                compile_test(silent=args.silent,
                             build_procs=args.build_procs,
                             dim=args.dim,
                             clean=args.clean)

            # --------------------------------------------------
            # Set up the run command
            # --------------------------------------------------
            runCommand = args.run   + " -np "                  + str(cores) + " " + executable + " " + input
            runCommand = runCommand + " driver.output_names="  + str(output)
            runCommand = runCommand + " driver.plot_interval=" + str(nplot)
            runCommand = runCommand + " driver.max_steps="     + str(nsteps)
            if not args.silent:
                print("\t Executing with '" + str(runCommand) + "'")

            # --------------------------------------------------
            # Run the executable and print the exit code
            # --------------------------------------------------
            exit_code = os.system(runCommand)
            # exit_code = subprocess.call([str(runCommand)],shell=True)
            # exit_code = subprocess.call([str(args.run),
            #                              '-np',
            #                              str(cores),
            #                              str(executable),
            #                              str(input)],
            #                             shell=True)
            if not exit_code is 0:
                print("\t Test run failed with exit code = " + str(exit_code))
            else:
                # --------------------------------------------------
                # Do file comparison if the test ran successfully
                # --------------------------------------------------
                if args.benchmark:
                    print("Regression test '" + str(test) + "' has generated benchmark files.")
                else:
                    # --------------------------------------------------
                    # Loop through all files that were generated and
                    # compare them with h5diff. Print an error message
                    # --------------------------------------------------
                    for i in range (0, nsteps+nplot, nplot):

                        # --------------------------------------------------
                        # Get the two files that will be compared
                        # --------------------------------------------------
                        regFile =  "plt/" + str(config[str(test)]['output'])
                        benFile =  "plt/" + str(config[str(test)]['benchmark'])
                        
                        regFile = regFile + (".step{0:07}.".format(i)) + str(args.dim) + "d.hdf5"
                        benFile = benFile + (".step{0:07}.".format(i)) + str(args.dim) + "d.hdf5"

                        if not args.silent:
                            print("\t Comparing files " + regFile +  " and " + str(benFile))


                        # --------------------------------------------------
                        # Run h5diff and compare the two files. Print a
                        # petite message if they match, and a huge-ass 
                        # warning if they don't. 
                        # --------------------------------------------------
                        compare_code = os.system("h5diff " + regFile + " " + benFile)
                        
                        if not compare_code is 0:
                            print("\t FILES '" + regFile +  "' AND '" + benFile + "' DO NOT MATCH - REGRESSION TEST FAILED")
                        else:
                            if not args.silent:
                                print("\t Benchmark test succeded for files " + regFile +  " and " + str(benFile))
