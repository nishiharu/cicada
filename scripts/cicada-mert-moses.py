#!/usr/bin/env python
#
#  Copyright(C) 2012-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
#

import threading
import multiprocessing

import time
import sys
import os, os.path
import string
import re
import subprocess


from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
	
    # output directory/filename prefix
    make_option("--root-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="root directory for outputs"),
    make_option("--prefix", default="mert", action="store", type="string",
                metavar="PREFIX", help="prefix for outputs (default: %default)"),

    
    make_option("--srcset", default="", action="store", type="string",
                metavar="FILE", help="training data"),
    make_option("--refset", default="", action="store", type="string",
                metavar="FILE", help="reference translations"),

    make_option("--moses", default="", action="store", type="string",
                metavar="MOSES", help="moses binary"),
    make_option("--config", default="", action="store", type="string",
                metavar="CONFIG", help="moses config file"),
    make_option("--options", default="", action="store", type="string",
                metavar="OPTIONS", help="moses options"),

    make_option("--iteration", default=10, action="store", type="int",
                metavar="ITERATION", help="# of iterations (default: %default)"),
    make_option("--iteration-first", default=1, action="store", type="int",
                metavar="ITERATION", help="The first iteration (default: %default)"),
    make_option("--weights", default="", action="store", type="string",
                metavar="FILE", help="initial weights"),
    make_option('--weights-default', default=None, action="store_true",
                help="initial weights from moses.ini"),
    
    make_option("--bound-lower", default="", action="store", type="string",
                metavar="FILE", help="lower bounds for weights"),
    make_option("--bound-upper", default="", action="store", type="string",
                metavar="FILE", help="upper bounds for weights"),
    make_option("--parameter-lower", default=-1.0, action="store", type="float",
                help="lower parameter value (default: %default)"),
    make_option("--parameter-upper", default=1.0, action="store", type="float",
                help="upper parameter value (default: %default)"),
    make_option("--mert-options", default='', action="store", type="string",
                help="other MERT options"),

    make_option("--direction", default=8, action="store", type="int",
                help="# of random directions (default: %default)"),
    make_option("--restart", default=2, action="store", type="int",
                help="# of random restarts (default: %default)"),
    make_option("--scorer", default="bleu:order=4,exact=true", action="store", type="string",
                metavar="SCORER", help="scorer for oracle computation (default: %default)"),
    make_option("--kbest", default=100, action="store", type="int",
                metavar="KBEST", help="kbest size (default: %default)"),
    make_option("--kbest-distinct", default=None, action="store_true",
                help="distinct kbest generation"),
    make_option("--bias-features", default="", action="store", type="string",
                 help="bias features"),
    make_option("--bias-weight", default=-1.0, action="store", type="float",
                 help="bias weight"),
    make_option("--iterative", default=None, action="store_true",
                help="perform iterative learning"),
        
    ## max-malloc
    make_option("--max-malloc", default=8, action="store", type="float",
                metavar="MALLOC", help="maximum memory in GB (default: %default)"),

    # CICADA Toolkit directory
    make_option("--cicada-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="cicada directory"),
    # MPI Implementation.. if different from standard location...
    make_option("--mpi-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="MPI directory"),

    # perform threading or MPI training    
    make_option("--mpi", default=0, action="store", type="int",
                help="# of processes for MPI-based parallel processing. Identical to --np for mpirun"),
    make_option("--mpi-host", default="", action="store", type="string",
                help="list of hosts to run job. Identical to --host for mpirun", metavar="HOSTS"),
    make_option("--mpi-host-file", default="", action="store", type="string",
                help="host list file to run job. Identical to --hostfile for mpirun", metavar="FILE"),
    make_option("--mpi-options", default="", action="store", type="string",
                metavar="OPTION", help="additional MPI options"),    
    make_option("--threads", default=1, action="store", type="int",
                help="# of thrads for thread-based parallel processing"),
    
    make_option("--pbs", default=None, action="store_true",
                help="PBS for launching processes"),
    make_option("--pbs-queue", default="", action="store", type="string",
                help="PBS queue for launching processes (default: %default)", metavar="NAME"),

    ## debug messages
    make_option("--debug", default=0, action="store", type="int"),
    ])

def find_executable(executable, paths=[]):
    ### taken from distutils.spawn
    
    paths += os.environ['PATH'].split(os.pathsep)
    
    base, ext = os.path.splitext(executable)

    if (sys.platform.startswith('win') or sys.platform.startswith('os2')) and (ext != '.exe'):
        executable = executable + '.exe'

    if not os.path.isfile(executable):
        for p in paths:
            f = os.path.join(p, executable)
            if os.path.isfile(f):
                # the file exists, we have a shot at spawn working
                return f
        return None
    else:
        return executable

def run_command(command):
    try:
        retcode = subprocess.call(command, shell=True)
        if retcode:
            sys.exit(retcode)
    except:
        raise ValueError, "subprocess.call failed: %s" %(command)

def compressed_file(file):
    if not file:
        return file
    if os.path.exists(file):
        return file
    if os.path.exists(file+'.gz'):
	return file+'.gz'
    if os.path.exists(file+'.bz2'):
	return file+'.bz2'
    (base, ext) = os.path.splitext(file)
    if ext == '.gz' or ext == '.bz2':
	if os.path.exists(base):
	    return base
    return file

class Quoted:
    def __init__(self, arg):
        self.arg = arg
        
    def __str__(self):
        return '"' + str(self.arg) + '"'

class Option:
    def __init__(self, arg, value=None):
        self.arg = arg
        self.value = value

    def __str__(self,):
        option = self.arg
        
        if self.value is not None:
            if isinstance(self.value, int):
                option += " %d" %(self.value)
            elif isinstance(self.value, long):
                option += " %d" %(self.value)
            elif isinstance(self.value, float):
                option += " %.20g" %(self.value)
            else:
                option += " %s" %(str(self.value))
        return option

            
class Program:
    def __init__(self, *args):
        self.args = list(args[:])

    def __str__(self,):
        return ' '.join(map(str, self.args))
    
    def __iadd__(self, other):
        self.args.append(other)
        return self
        
class PBS:
    def __init__(self, queue=""):
        self.queue = queue
        self.qsub = find_executable('qsub')
        
        if not self.qsub:
            raise ValueError, "no qsub in your executable path?"

    def run(self, command="", name="name", memory=0.0, mpi=None, threads=1, logfile=None):

        popen = subprocess.Popen([self.qsub, '-S', '/bin/sh'], stdin=subprocess.PIPE)
        pipe = popen.stdin
        
        pipe.write("#!/bin/sh\n")
        pipe.write("#PBS -S /bin/sh\n")
        pipe.write("#PBS -N %s\n" %(name))
        pipe.write("#PBS -W block=true\n")
        pipe.write("#PBS -e localhost:/dev/null\n")
        pipe.write("#PBS -o localhost:/dev/null\n")
        
        if self.queue:
            pipe.write("#PBS -q %s\n" %(self.queue))
            
        mem = ""
        if memory >= 1.0:
            mem=":mem=%dgb" %(int(memory))
        elif memory >= 0.001:
            mem=":mem=%dmb" %(int(memory * 1000))
        elif memory >= 0.000001:
            mem=":mem=%dkb" %(int(memory * 1000 * 1000))

        if mpi:
            pipe.write("#PBS -l select=%d:ncpus=%d:mpiprocs=1%s\n" %(mpi.number, threads, mem))
        else:
            pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1%s\n" %(threads, mem))
        
        # setup variables
        if os.environ.has_key('TMPDIR_SPEC'):
            pipe.write("export TMPDIR_SPEC=%s\n" %(os.environ['TMPDIR_SPEC']))
        if os.environ.has_key('LD_LIBRARY_PATH'):
            pipe.write("export LD_LIBRARY_PATH=%s\n" %(os.environ['LD_LIBRARY_PATH']))
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            pipe.write("export DYLD_LIBRARY_PATH=%s\n" %(os.environ['DYLD_LIBRARY_PATH']))
        
        pipe.write("if test \"$PBS_O_WORKDIR\" != \"\"; then\n")
        pipe.write("  cd $PBS_O_WORKDIR\n")
        pipe.write("fi\n")

        prefix = ''
        if mpi:
            prefix = mpi.mpirun

            if os.environ.has_key('TMPDIR_SPEC'):
                prefix += ' -x TMPDIR_SPEC'
            if os.environ.has_key('LD_LIBRARY_PATH'):
                prefix += ' -x LD_LIBRARY_PATH'
            if os.environ.has_key('DYLD_LIBRARY_PATH'):
                prefix += ' -x DYLD_LIBRARY_PATH'
            
            prefix += ' ' + mpi.options
            prefix += ' '
        
        suffix = ''
        if logfile:
            suffix = " 2> %s" %(logfile)

        pipe.write(prefix + command + suffix + '\n')
        
        pipe.close()
        popen.wait()
        
class MPI:
    
    def __init__(self, dir="", hosts="", hosts_file="", number=0, options=""):
        
	self.dir = dir
	self.hosts = hosts
        self.hosts_file = hosts_file
        self.number = number
        self.options = options
	
        if self.dir:
            if not os.path.exists(self.dir):
                raise ValueError, self.dir + " does not exist"
            self.dir = os.path.realpath(self.dir)

        if self.hosts_file:
            if not os.path.exists(self.hosts_file):
                raise ValueError, self.hosts_file + " does no exist"
            self.hosts_file = os.path.realpath(hosts_file)

        self.bindir = self.dir

        paths = []
        if self.bindir:
            paths = [os.path.join(self.bindir, 'bin'), self.bindir]
        
        binprog = find_executable('openmpirun', paths)
        if not binprog:
            binprog = find_executable('mpirun', paths)

        if not binprog:
            raise ValueError, "no openmpirun nor mpirun?"

        setattr(self, 'mpirun', binprog)
        
    def run(self, command, logfile=None):
        mpirun = self.mpirun
        if self.number > 0:
            mpirun += ' --np %d' %(self.number)
        if self.hosts:
            mpirun += ' --host %s' %(self.hosts)
        elif self.hosts_file:
            mpirun += ' --hostfile "%s"' %(self.hosts_file)

        if os.environ.has_key('TMPDIR_SPEC'):
            mpirun += ' -x TMPDIR_SPEC'
        if os.environ.has_key('LD_LIBRARY_PATH'):
            mpirun += ' -x LD_LIBRARY_PATH'
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            mpirun += ' -x DYLD_LIBRARY_PATH'

        mpirun += ' ' + self.options
	mpirun += ' ' + command

        if logfile:
            mpirun += " 2> %s" %(logfile)
        
	run_command(mpirun)

class QSub:
    def __init__(self, mpi=None, pbs=None):
        self.mpi = mpi
        self.pbs = pbs
        
    def run(self, command, name="name", memory=0.0, threads=1, logfile=None):
        if logfile:
            print str(command), '2> %s' %(logfile)
        else:
            print str(command)

        if self.pbs:
            self.pbs.run(str(command), name=name, memory=memory, threads=threads, logfile=logfile)
        else:
            if logfile:
                run_command(str(command) + " 2> %s" %(logfile))
            else:
                run_command(str(command))
    
    def mpirun(self, command, name="name", memory=0.0, threads=1, logfile=None):
        if not self.mpi:
            raise ValueError, "no mpi?"

        if logfile:
            print str(command), '2> %s' %(logfile)
        else:
            print str(command)

        if self.pbs:
            self.pbs.run(str(command), name=name, memory=memory, mpi=self.mpi, logfile=logfile)
        else:
            self.mpi.run(str(command), logfile=logfile)

class CICADA:
    def __init__(self, dir=""):
        bindirs = []
        
        if not dir:
            dir = os.path.abspath(os.path.dirname(__file__))
            bindirs.append(dir)
            parent = os.path.dirname(dir)
            if parent:
                dir = parent
        else:
            dir = os.path.realpath(dir)
            if not os.path.exists(dir):
                raise ValueError, dir + " does not exist"
            bindirs.append(dir)
        
	for subdir in ('bin', 'progs', 'scripts'): 
	    bindir = os.path.join(dir, subdir)
	    if os.path.exists(bindir) and os.path.isdir(bindir):
		bindirs.append(bindir)
	
        for binprog in ('cicada',
                        'cicada_mpi',
                        'cicada_mert',
                        'cicada_mert_mpi',
                        'cicada_mert_kbest',
                        'cicada_mert_kbest_mpi',
                        'cicada_eval',
                        'cicada_filter_config',
                        'cicada_filter_config_moses',
                        'cicada_filter_kbest_moses',):
	    
            prog = find_executable(binprog, bindirs)
            if not prog:
                raise ValueError, binprog + ' does not exist'
                
            setattr(self, binprog, prog)

if __name__ == '__main__':
    (options, args) = opt_parser.parse_args()
    
    ### dump to stderr
    stdout = sys.stdout
    sys.stdout = sys.stderr

    print "for moses options, we will recommend: -beam-threshold 0 -translation-option-threshold 0 -early-discarding-threshold 0"

    ### moses
    if not os.path.exists(options.moses):
        raise ValueError, "no moses?: %s" %(options.moses)

    ### srcset
    if not os.path.exists(options.srcset):
        raise ValueError, "no developtment file: %s" %(options.srcset)

    ### refset
    if not os.path.exists(options.refset):
        raise ValueError, "no reference translation file: %s" %(options.refset)

    ### config
    if not os.path.exists(options.config):
        raise ValueError, "no config file: %s" %(options.config) 
   
    ### root-dir
    if options.root_dir:
        if not os.path.exists(options.root_dir):
            os.makedirs(options.root_dir)
    
    ### cicada
    cicada = CICADA(dir=options.cicada_dir)
    
    ### MPI
    mpi = None
    if options.mpi_host or options.mpi_host_file or options.mpi > 0:
        mpi = MPI(dir=options.mpi_dir,
                  hosts=options.mpi_host,
                  hosts_file=options.mpi_host_file,
                  number=options.mpi,
                  options=options.mpi_options)
    
    ### PBS
    pbs = None
    if options.pbs:
        pbs = PBS(queue=options.pbs_queue)
    
    ### QSUB
    qsub = QSub(mpi=mpi, pbs=pbs)
    
    ### iterations
    if options.iteration_first <= 0:
        options.iteration_first = 1
    if options.iteration_first > options.iteration:
        raise ValueError, "invalid iterations"

    ## check kbests
    if options.kbest <= 0:
        raise ValueError, "invalid kbest"
    
    if options.bound_lower:
        if not os.path.exists(options.bound_lower):
            raise ValueError, "no lower-bound file? %s" %(options.bound_lower)

    if options.bound_upper:
        if not os.path.exists(options.bound_upper):
            raise ValueError, "no upper-bound file? %s" %(options.bound_upper)

    ### how to handle this...!
    weights_config = ''
    if options.weights and options.weights_default:
        raise ValueError, "both --weights and --weights-default?"

    if options.weights:
        if not os.path.exists(options.weights):
            raise ValueError, "no initiali weights? %s" %(options.weights)
        weights_config = options.weights
    elif options.weights_default:
        weights_config = os.path.join(options.root_dir, options.prefix + ".0.weights")
        
        qsub.run(Program(cicada.cicada_filter_config_moses,
                         Option('--input', options.config),
                         Option('--output', weights_config)),
                 name="config")
    else:
        weights_config = os.path.join(options.root_dir, options.prefix + ".0.weights")
        
        open(weights_config, 'w').close()
    
    weiset = []
    tstset = []
    
    for iter in range(1, options.iteration_first):
        prefix = options.prefix + ".%d" %(iter)

        weights = os.path.join(options.root_dir, prefix + ".weights")
        decoded = os.path.join(options.root_dir, prefix + ".kbest")
        
        weiset.append(weights)
        tstset.append(decoded)

    for iter in range(options.iteration_first, options.iteration+1):
        print "iteration: %d" %(iter)
        
        ## setup output files
        prefix = options.prefix + ".%d" %(iter)
        
        weights = os.path.join(options.root_dir, prefix + ".weights")
        
        if len(weiset) > 0:
            weights_config = weiset[-1]
        
        decoded = os.path.join(options.root_dir, prefix + ".kbest")
        
        weiset.append(weights)
        tstset.append(decoded)
        
        config = os.path.join(options.root_dir, prefix + ".config")
        mteval = os.path.join(options.root_dir, prefix + ".eval")
        
        print "generate config file %s @ %s" %(config, time.ctime())
        
        config_bias_features = ''
        config_bias_weight   =  ''
        if options.bias_features:
            config_bias_features = Option('--bias-features', options.bias_features)
            config_bias_weight   = Option('--bias-weight', options.bias_weight)

        qsub.run(Program(cicada.cicada_filter_config_moses,
                         Option('--weights', weights_config),
                         config_bias_features,
                         config_bias_weight,
                         Option('--input', Quoted(options.config)),
                         Option('--output', Quoted(config))),
                 name="config")
        
        print "moses %s @ %s" %(decoded, time.ctime())
        
        moses_erase_features = ''
        if options.bias_features:
            moses_erase_features = Option('--erase-features', options.bias_features)

        moses_kbest = Option('-n-best-list', "- %d" %(options.kbest))
        if options.kbest_distinct:
            moses_kbest = Option('-n-best-list', "- %d distinct" %(options.kbest))
        
        qsub.run(Program('(',
                         options.moses,
                         Option('-input-file', Quoted(options.srcset)),
                         Option('-config', Quoted(config)),
                         options.options,
                         moses_kbest,
                         Option('-threads', options.threads),
                         '|',
                         cicada.cicada_filter_kbest_moses,
                         Option('--output', Quoted(decoded)),
                         Option('--directory'),
                         moses_erase_features,
                         ')',),
                 name="moses",
                 memory=options.max_malloc,
                 threads=options.threads,
                 logfile=Quoted(decoded+'.log'))

        
        print "evaluate %s @ %s" %(mteval, time.ctime())
        
        qsub.run(Program(cicada.cicada_eval,
                         Option('--refset', Quoted(options.refset)),
                         Option('--tstset', Quoted(decoded)),
                         Option('--output', Quoted(mteval)),
                         Option('--scorer', options.scorer)),
                 name="evaluate")
        
        print "mert %s @ %s" %(weights, time.ctime())

        ### training data, oracle data
        mert_tstset  = Option('--tstset', ' '.join(map(lambda x: str(Quoted(x)), tstset)))
        
        mert_weights = ''
        if len(weiset) > 1:
            mert_weights = Option('--feature-weights', ' '.join(map(lambda x: str(Quoted(x)), weiset[:-1])))
        
        mert_iterative = ''
        if options.iterative:
            mert_iterative = Option('--iterative')

        mert_lower = ''
        mert_upper = ''

        if options.bound_lower:
            mert_lower = Option('--bound-lower', Quoted(options.bound_lower))

        if options.bound_upper:
            mert_upper = Option('--bound-upper', Quoted(options.bound_upper))

        if mpi:
            qsub.mpirun(Program(cicada.cicada_mert_kbest_mpi,
                                mert_tstset,
                                mert_weights,
                                Option('--refset', Quoted(options.refset)),
                                Option('--scorer', Quoted(options.scorer)),
                                Option('--output', Quoted(weights)),
                                Option('--samples-directions', options.direction),
                                Option('--samples-restarts', options.restart),
                                Option('--value-lower', options.parameter_lower),
                                Option('--value-upper', options.parameter_upper),
                                mert_lower,
                                mert_upper,
                                mert_iterative,
                                Option('--normalize-l1'),
                                Option('--initial-average'),
                                options.mert_options,
                                Option('--debug', 2),),
                        name="mert",
                        memory=options.max_malloc,
                        threads=options.threads,
                        logfile=Quoted(weights+'.log'))
        else:
            qsub.run(Program(cicada.cicada_mert_kbest,
                             mert_tstset,
                             mert_weights,
                             Option('--refset', Quoted(options.refset)),
                             Option('--scorer', Quoted(options.scorer)),
                             Option('--output', Quoted(weights)),
                             Option('--samples-directions', options.direction),
                             Option('--samples-restarts', options.restart),
                             Option('--value-lower', options.parameter_lower),
                             Option('--value-upper', options.parameter_upper),
                             mert_lower,
                             mert_upper,
                             mert_iterative,
                             Option('--normalize-l1'),
                             Option('--initial-average'),
                             options.mert_options,
                             Option('--threads', options.threads),
                             Option('--debug', 2),),
                     name="mert",
                     memory=options.max_malloc,
                     threads=options.threads,
                     logfile=Quoted(weights+'.log'))
