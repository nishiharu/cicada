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
import cStringIO

from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
	
    # output directory/filename prefix
    make_option("--root-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="root directory for outputs"),
    make_option("--prefix", default="learn", action="store", type="string",
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
    
    make_option("--regularize-l1", default=0.0, action="store", type="float",
                metavar="REGULARIZER", help="L1 regularization"),
    make_option("--regularize-l2", default=0.0,action="store", type="float",
                metavar="REGULARIZER", help="L2 regularization"),
    make_option("--regularize-lambda", default=0.0,action="store", type="float",
                metavar="REGULARIZER", help="regularization hyperparameter"),
    make_option("--regularize-oscar", default=0.0,action="store", type="float",
                metavar="REGULARIZER", help="OSCAR regularization"),
        
    make_option("--scorer", default="bleu:order=4,exact=true", action="store", type="string",
                metavar="SCORER", help="scorer for oracle computation (default: %default)"),
    make_option("--scorer-cube", default=400, action="store", type="int",
                metavar="SIZE", help="cube size for oracle computation (default: %default)"),
    make_option("--learn", default="xbleu", action="store", type="string",
                metavar="LEARN", help="learning algorithms from [softmax, svm, linear, pegasos, mira, cw, arow, nherd, cp, mcp, xbleu] (default: %default)"),
    make_option("--learn-options", default="", action="store", type="string",
                metavar="OPTION", help="additional learning options"),
    
    make_option("--kbest", default=100, action="store", type="int",
                metavar="KBEST", help="kbest size (default: %default)"),
    make_option("--kbest-distinct", default=None, action="store_true",
                help="distinct kbest generation"),
    make_option("--bias-features", default="", action="store", type="string",
                 help="bias features"),
    make_option("--bias-weight", default=-1.0, action="store", type="float",
                 help="bias weight"),
    make_option("--merge", default=None, action="store_true",
                help="perform kbest merging"),
    make_option("--interpolate", default=0.0, action="store", type="float",
                help="perform weights interpolation"),
    make_option("--postprocess", default="", action="store", type="string",
                help="post-processing script after learning: \"cat learned-weight-file | postprocess > post-processed-weight-file\""),
        
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

class Pipe:
    def __str__(self,):
        return '|'

class Redirect:
    def __init__(self, arg):
        self.arg = arg
    
    def __str__(self,):
        return '> ' + '"' + self.arg + '"'
            
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
                        'cicada_learn_kbest',
                        'cicada_learn_kbest_mpi',
                        'cicada_oracle_kbest',
                        'cicada_oracle_kbest_mpi',
                        'cicada_eval',
                        'cicada_filter_config',
                        'cicada_filter_config_moses',
                        'cicada_filter_kbest_moses',
                        'cicada_filter_weights',):

            prog = find_executable(binprog, bindirs)
            if not prog:
                raise ValueError, binprog + ' does not exist'
                
            setattr(self, binprog, prog)

def learn_algorithms(command):
    
    pattern = re.compile(r"\s+--learn-(\S+)\s*")

    algs = []
    
    for line in cStringIO.StringIO(subprocess.check_output([command, "--help"])):
        result = pattern.search(line)
        if result:
            algs.append(result.group(1))
    return algs
    

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
    
    ### regularizer
    if options.regularize_l1 < 0.0:
        raise ValueError, "L1 regularization must be positive"
    if options.regularize_l2 < 0.0:
        raise ValueError, "L2 regularization must be positive"
    if options.regularize_lambda < 0.0:
        raise ValueError, "regularization constant must be positive"
    if options.regularize_oscar < 0.0:
        raise ValueError, "OSCAR regularization must be positive"

    regularizer = []
    if options.regularize_l1 > 0.0:
        regularizer.append(Option('--regularize-l1',     options.regularize_l1))
    if options.regularize_l2 > 0.0:
        regularizer.append(Option('--regularize-l2',     options.regularize_l2))
    if options.regularize_lambda > 0.0:
        regularizer.append(Option('--regularize-lambda', options.regularize_lambda))
    if options.regularize_oscar > 0.0:
        regularizer.append(Option('--regularize-oscar',  options.regularize_oscar))
    regularizer = ' '.join(map(str, regularizer))

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

    learn_kbest     = learn_algorithms(cicada.cicada_learn_kbest)
    learn_kbest_mpi = learn_algorithms(cicada.cicada_learn_kbest_mpi)
    learn_mpi       = None
    
    if options.learn not in learn_kbest or options.learn not in learn_kbest_mpi:
        raise ValueError, "learner %s is not supported by kbest learner" %(options.learn)
        
    if not mpi and options.learn not in learn_kbest:
        raise ValueError, "%s is not supported by non-mpi-kbest learner" %(options.learn)

    if mpi and options.learn in learn_kbest_mpi:
        learn_mpi = 1

    interpolate = None
    if options.interpolate > 0.0 and options.interpolate < 1.0:
        interpolate = 1
            
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
    orcset = []
    
    for iter in range(1, options.iteration_first):
        prefix = options.prefix + ".%d" %(iter)

        weights = os.path.join(options.root_dir, prefix + ".weights")
        decoded = os.path.join(options.root_dir, prefix + ".kbest")
        oracle  = os.path.join(options.root_dir, prefix + ".kbest.oracle")
        
        weiset.append(weights)
        tstset.append(decoded)
        orcset.append(oracle)

    for iter in range(options.iteration_first, options.iteration+1):
        print "iteration: %d" %(iter)
        
        ## setup output files
        prefix = options.prefix + ".%d" %(iter)
        
        weights       = os.path.join(options.root_dir, prefix + ".weights")
        weights_learn = os.path.join(options.root_dir, prefix + ".weights.learn")
        
        if len(weiset) > 0:
            weights_config = weiset[-1]
        
        decoded = os.path.join(options.root_dir, prefix + ".kbest")
        oracle  = os.path.join(options.root_dir, prefix + ".kbest.oracle")
        
        weiset.append(weights)
        tstset.append(decoded)
        orcset.append(oracle) 
        
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
        
        print "oracle %s @ %s" %(oracle, time.ctime())
        
        ### set up the oracle to compute...
        oracle_tstset = Option('--tstset', Quoted(decoded))
        if options.merge:
            oracle_tstset = Option('--tstset', ' '.join(map(lambda x: str(Quoted(x)), tstset)))

        if mpi:
            qsub.mpirun(Program(cicada.cicada_oracle_kbest_mpi,
                                Option('--refset', Quoted(options.refset)),
                                oracle_tstset,
                                Option('--output', Quoted(oracle)),
                                Option('--scorer', options.scorer),
                                Option('--directory'),
                                Option('--debug')),
                        name="oracle",
                        memory=options.max_malloc,
                        threads=options.threads,
                        logfile=Quoted(oracle+'.log'))
        else:
            qsub.run(Program(cicada.cicada_oracle_kbest,
                             Option('--refset', Quoted(options.refset)),
                             oracle_tstset,
                             Option('--output', Quoted(oracle)),
                             Option('--scorer', options.scorer),
                             Option('--directory'),
                             Option('--threads', options.threads),
                             Option('--debug'),),
                     name="oracle",
                     memory=options.max_malloc,
                     threads=options.threads,
                     logfile=Quoted(oracle+'.log'))
        
        print "learn %s @ %s" %(weights, time.ctime())

        ### training data, oracle data
        learn_input  = Option('--input', ' '.join(map(lambda x: str(Quoted(x)), tstset)))
        learn_oracle = Option('--oracle', ' '.join(map(lambda x: str(Quoted(x)), orcset)))
        learn_unite  = ''
        if options.merge:
            learn_oracle = Option('--oracle', Quoted(oracle))
            learn_unite  = Option('--unite')
        
        learn_weights = ''
        if len(weiset) > 1:
            learn_weights = Option('--weights', Quoted(weiset[-2]))
            
        do_interpolate = None
        if interpolate and len(weiset) > 1:
            do_interpolate = 1

        # if we interpolate, first, generate weights_learn then, dump...
        learn_output = Option('--output', Quoted(weights))
        if do_interpolate or options.postprocess::
            learn_output = Option('--output', Quoted(weights_learn))

        learn_algorithm = Option('--learn-' + options.learn)

        if learn_mpi and mpi:
            qsub.mpirun(Program(cicada.cicada_learn_kbest_mpi,
                                learn_input,
                                learn_oracle,
                                Option('--refset', options.refset),
                                Option('--scorer', options.scorer),
                                learn_unite,
                                learn_output,
                                learn_weights,
                                learn_algorithm,
                                options.learn_options,
                                regularizer,
                                Option('--debug', 2),),
                        name="learn",
                        memory=options.max_malloc,
                        threads=options.threads,
                        logfile=Quoted(weights+'.log'))
        else:
            qsub.run(Program(cicada.cicada_learn_kbest,
                             learn_input,
                             learn_oracle,
                             Option('--refset', options.refset),
                             Option('--scorer', options.scorer),
                             learn_unite,
                             learn_output,
                             learn_weights,
                             learn_algorithm,
                             options.learn_options,
                             regularizer,
                             Option('--threads', options.threads),
                             Option('--debug', 2),),
                     name="learn",
                     memory=options.max_malloc,
                     threads=options.threads,
                     logfile=Quoted(weights+'.log'))
            
        if do_interpolate and options.postprocess:
            print "interpolate+postprocess %s @ %s" %(weights, time.ctime())
            
            qsub.run(Program(cicada.cicada_filter_weights,
                             Option(weiset[-2] + ":scale=%g" %(1.0 - options.interpolate)),
                             Option(weights_learn + ":scale=%g" %(options.interpolate)),
                             Pipe(),
                             options.postprocess,
                             Redirect(weights)),
                     name="interpolate")
            
        elif options.postprocess:
            print "postprocess %s @ %s" %(weights, time.ctime())
            
            qsub.run(Program('cat',
                             Quoted(weights_learn),
                             Pipe(),
                             options.postprocess,
                             Redirect(weights)),
                     name="postprocess")
            
        elif do_interpolate:
            print "interpolate %s @ %s" %(weights, time.ctime())
            
            qsub.run(Program(cicada.cicada_filter_weights,
                             Option('--output', Quoted(weights)),
                             Option(weiset[-2] + ":scale=%g" %(1.0 - options.interpolate)),
                             Option(weights_learn + ":scale=%g" %(options.interpolate))),
                     name="interpolate")
