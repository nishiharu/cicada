#!/usr/bin/env python
#
#  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
#
###
### a wrapper script for running multiple commands
### inspired by mpish and thrsh, we support pbs 
### Actually, we will run by mpish, thrsh and pbs
###

import threading

import time
import sys
import os, os.path
import string
import re
import subprocess
import shutil

import UserList
import UserString
import cStringIO

from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
    ## max-malloc
    make_option("--max-malloc", default=4, action="store", type="float",
                metavar="MALLOC", help="maximum memory in GB (default: %default)"),

    # CICADA Toolkit directory
    make_option("--cicada-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="cicada directory"),
    # MPI Implementation.. if different from standard location...
    make_option("--mpi-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="MPI directory"),

    make_option("--threads", default=1, action="store", type="int",
                help="# of thrads for thread-based parallel processing"),
    # perform threading or MPI training    
    make_option("--mpi", default=0, action="store", type="int",
                help="# of processes for MPI-based parallel processing. Identical to --np for mpirun"),
    make_option("--mpi-host", default="", action="store", type="string",
                help="list of hosts to run job. Identical to --host for mpirun", metavar="HOSTS"),
    make_option("--mpi-host-file", default="", action="store", type="string",
                help="host list file to run job. Identical to --hostfile for mpirun", metavar="FILE"),
    make_option("--mpi-options", default="", action="store", type="string",
                metavar="OPTION", help="additional MPI options"),    
    make_option("--pbs", default=None, action="store_true",
                help="PBS for launching processes"),
    make_option("--pbs-name", default="cicada-sh", action="store", type="string",
                help="PBS process name (up to 15 characters!) (default: %default)", metavar="NAME"),
    make_option("--pbs-queue", default="", action="store", type="string",
                help="PBS queue for launching processes (default: %default)", metavar="NAME"),
    make_option("--pbs-after", default="", action="store", type="string",
                help="PBS for launching processes after this process id", metavar="ID"),
    make_option("--pbs-before", default="", action="store", type="string",
                help="PBS for launching processes before this process id", metavar="ID"),
    make_option("--pbs-non-block", action="store_true",
                help="lanch job in a non-blocking fashion"),

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

class QSUB:
    def __init__(self, script=""):
        self.script = script
        self.qsub = find_executable('qsub')
        
        if not self.qsub:
            raise ValueError, "no qsub in your executable path?"

    def start(self):
        self.run()

    def join(self):
        self.wait()
        
    ### actual implementations
    def run(self):
        self.popen = subprocess.Popen([self.qsub, '-S', '/bin/sh'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        
        ### reader therad
        self.stdout = threading.Thread(target=self._reader, args=[self.popen.stdout])
        self.stdout.start()
        
        ### feed script
        self.popen.stdin.write(self.script)
        self.popen.stdin.close()
        
    def wait(self):
        self.stdout.join()
        self.popen.wait()

    def _reader(self, fh):
        fh.read()
        
class PBS:
    def __init__(self, queue="", non_block=None):
        self.workers = []
        self.queue = queue
        self.non_block = non_block

    def wait(self):
        for worker in self.workers:
            worker.join()
            
    def run(self, command="", threads=1, memory=0.0, name="cicada-sh", logfile=None, after="", before=""):
        pipe = cStringIO.StringIO()
        
        pipe.write("#!/bin/sh\n")
        pipe.write("#PBS -S /bin/sh\n")
        pipe.write("#PBS -N %s\n" %(name))
        pipe.write("#PBS -e localhost:/dev/null\n")
        pipe.write("#PBS -o localhost:/dev/null\n")

        if not self.non_block:
            pipe.write("#PBS -W block=true\n")
        
        if after:
            pipe.write("#PBS -W depend=after:%s\n" %(after))

        if before:
            pipe.write("#PBS -W depend=before:%s\n" %(before))
        
        if self.queue:
            pipe.write("#PBS -q %s\n" %(self.queue))

        mem = ""
        if memory >= 1.0:
            mem=":mem=%dgb" %(int(memory))
        elif memory >= 0.001:
            mem=":mem=%dmb" %(int(memory * 1000))
        elif memory >= 0.000001:
            mem=":mem=%dkb" %(int(memory * 1000 * 1000))

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
        suffix = ''
        if logfile:
            suffix = " 2> %s" %(logfile)
        
        pipe.write(prefix + command + suffix + '\n')

        if self.non_block:
            qsub = QSUB(pipe.getvalue())
            qsub.start()
            qsub.join()
        else:
            self.workers.append(QSUB(pipe.getvalue()))
            self.workers[-1].start()

        sys.stderr.write(command+'\n')
        
        # sleep for safety...
        #time.sleep(0.1)

class Threads:
    
    def __init__(self, cicada=None, threads=1):
        self.popen = subprocess.Popen([cicada.thrsh, '--threads', str(threads), '--debug'], stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def wait(self):
        self.pipe.close()
        self.popen.wait()

    def run(self, command=""):
        self.pipe.write("%s\n" %(command))
        self.pipe.flush()

class MPI:
    
    def __init__(self, cicada=None, dir="", hosts="", hosts_file="", number=0, options=""):
        
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
        
        command = self.mpirun
        
        if self.number > 0:
            command += ' --np %d' %(self.number)
            
        if self.hosts:
            command += ' --host %s' %(self.hosts)
        elif self.hosts_file:
            command += ' --hostfile %s' %(self.hosts_file)

        if os.environ.has_key('TMPDIR_SPEC'):
            command += ' -x TMPDIR_SPEC'
        if os.environ.has_key('LD_LIBRARY_PATH'):
            command += ' -x LD_LIBRARY_PATH'
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            command += ' -x DYLD_LIBRARY_PATH'

        command += ' ' + self.options

        command += " %s" %(cicada.mpish)
        command += " --debug"
        
        self.popen = subprocess.Popen(command, shell=True, stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def wait(self):
        self.pipe.close()
        self.popen.wait()
        
    def run(self, command=""):
        self.pipe.write("%s\n" %(command))
        self.pipe.flush()


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
	
        for binprog in ('mpish', ### mpi-launcher
                        'thrsh', ### thread-launcher
                        ):
            
            prog = find_executable(binprog, bindirs)
            if not prog:
                raise ValueError, binprog + ' does not exist'
                
            setattr(self, binprog, prog)

if __name__ == '__main__':
    (options, args) = opt_parser.parse_args()

    if options.pbs:
        # we use pbs to run jobs
        pbs = PBS(queue=options.pbs_queue, non_block=options.pbs_non_block)
    
        for line in sys.stdin:
            line = line.strip()
            if line:
                pbs.run(command=line,
                        threads=options.threads,
                        memory=options.max_malloc,
                        name=options.pbs_name,
                        after=options.pbs_after,
                        before=options.pbs_before)

        pbs.wait()

    elif options.mpi:
        cicada = CICADA(options.cicada_dir)
        mpi = MPI(cicada=cicada,
                  dir=options.mpi_dir,
                  hosts=options.mpi_host,
                  hosts_file=options.mpi_host_file,
                  number=options.mpi,
                  options=options.mpi_options)
    
        for line in sys.stdin:
            line = line.strip()
            if line:
                mpi.run(command=line)

        mpi.wait()
    
    else:
        cicada = CICADA(options.cicada_dir)
        threads = Threads(cicada=cicada, threads=options.threads)
    
        for line in sys.stdin:
            line = line.strip()
            if line:
                threads.run(command=line)

        threads.wait()
