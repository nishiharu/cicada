Some note on MPI

Open MPI is a recommended MPI engine, since it is well tested by the author.
REMARK: it is recommended not to use memory managers in open-mpi, which may conflict with your
    	favorite memory managers, such as jemalloc/tcmalloc. To disable this, for instance, you
	can edit "openmpi-mca-params.conf" and add(or disable) mca by, "memory = ^ptmalloc2"
	For details, consult open-mpi documentation.

Wrapper scripts:

cicada-sh.py:
	Runs multiple command on multiple hosts, by one-line-per-one-command from stdin
	We use mpish/thrsh (see below) and pbs

cicada-pipe.py:
	Runs a single command on multiple hosts, stdin is automatically distributed, and merged into stdout
	We use mpipe/thrpe and pbs

Some utilities:

mpish [list of files or use stdin]
      Run multiple commands (one-command-per-line) in the files on multiple nodes. (if no args, use stdin)
      The commands are scheduled so that we will consume # of nodes
      specified by mpirun.
     
mpipe --command [command]
      Run command on multiple nodes, specified by mpirun, and distribute stdin (or file specified by --input) to each 
      workers, collect their stdout into single stdout (or file specified by --output).
      We assume workers consume exactly one line, then output one line.
      --even option will evenly split lines, otherwise, it is scheduled so that "faster" worker may consume more lines.

mpimap [list of command files]
       Run commands on multiple nodes, specified by # of command in files (if no args, use stdin).
       Then, distribute stdin (or file specified by --input) to each worker. We will not "reduce" stdout
       from each of worker, so it is your responsibility to merge appropriately. Remark, distribution is
       NOT even, and rather random, due to optimum scheduling (like those used in mpipe).
       (--even option to evenly split lines.)
	

Its thread variant:

thrsh [list of files or use stdin]
      Similar to mpish, but uses threads to run multiple processes

thrpe --command [command]
      Similar to mpipe, but uses threads to run multiple processes


