#!/bin/sh
#
#  Copyright(C) 2011-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
#

### we assume PBSPro. If you want to apply this to other environgmnet, adjust 
### #PBS stuff and qsub related commands

me=`basename $0`

exit_missing_arg="\
echo \"$me: option \\\`\$1' requires an argument\" >&2
echo \"\$help\" >&2
exit 1"

### working dir..
workingdir=`pwd`

### this is a test, whether we can run under cluster or not...
qsub=`which qsub 2> /dev/null`

name=""
logfile=""
outfile=""
mpimode="no"
threads=""

## # of processes, # of cores
np=1
nc=1
openmpi=""
hosts=""
hosts_file=""

### qsubs
mem=1gb
queue=ltg

usage="\
$me [options] cicada-program args
  --mpi                     MPI implementation
  --host, --hosts           MPI hosts
  --hostfile, --host-file   MPI host file
  -q, --queue               PBS queue                (default: $queue)
  -n, --np                  # of processes to run    (default: $np)
  -c,--nc                   # of cores to run        (default: $nc)
  --mem                     memory used by each node (default: $mem)
  --log, --logfile, -l      logfile
  --out, --outfile, -o      stdout file
  --threads,-t              threading option
  --name                    process name

  -h, --help                help message
"

while test $# -gt 0; do
  case $1 in
  --mpi | --mpi-dir )
    test $# = 1 && eval "$exit_missing_arg"
    openmpi=$2
    shift; shift ;;
  --host | --hosts )
    test $# = 1 && eval "$exit_missing_arg"
    hosts=$2
    shift; shift ;;
  --hostfile | --host-file )
    test $# = 1 && eval "$exit_missing_arg"
    host_file=$2
    shift; shift ;;
  --queue | -q )
    test $# = 1 && eval "$exit_missing_arg"
    queue=$2
    shift; shift ;;
  --np | -n )
    test $# = 1 && eval "$exit_missing_arg"
    np=$2
    shift; shift ;;
  --nc | -c )
    test $# = 1 && eval "$exit_missing_arg"
    nc=$2
    shift; shift ;;
  --mem )
    test $# = 1 && eval "$exit_missing_arg"
    mem=$2
    shift; shift ;;
  --log | --logfile | -l )
    test $# = 1 && eval "$exit_missing_arg"
    logfile=$2
    shift; shift ;;
  --out | --outfile | -o )
    test $# = 1 && eval "$exit_missing_arg"
    outfile=$2
    shift; shift ;;
  -m )
    mpimode="yes"
    shift ;;
  --threads | -t )
    threads="yes"
    shift ;;
  --name )
    test $# = 1 && eval "$exit_missing_arg"
    name=$2
    shift; shift ;;

  --help | -h )
    echo "$usage"
    exit ;;
  -* )
    exec >&2
    echo "$me: invalid option $1"
    exit 1 ;;
  * )
    break ;;
  esac
done

stripped=`expr "$1" : '\(.*\)_mpi$'`
if test "$stripped" = ""; then
  stripped=$1
fi

if test "$mpimode" = "no"; then
  if test "$stripped" != "$1" -a $np -gt 1; then
    mpimode=yes
  fi
fi

if test "$threads" = "yes"; then
  threads=" --threads ${nc}"
fi

if test "$openmpi" != ""; then
  openmpi=`echo "${openmpi}/" | sed -e 's/\/\/$/\//'`
  
  if test ! -e ${openmpi}mpirun; then
    openmpi=${openmpi}bin/
    if test ! -e ${openmpi}mpirun; then
      echo "no mpirun?" >&2
    exit 1
    fi
  fi
fi

### check np and nc
if test $np -le 1; then
  np=1
fi
if test $nc -le 1; then
  nc=1
fi

mpinp=""
if test "$qsub" = ""; then
  mpinp="--np $np"
  if test "$hosts" != ""; then
    mpinp="$mpinp --host $hosts"
  fi
  if test "$host_file" != ""; then
    mipnp="$mpinp --hostfile $host_file"
  fi

  if test "$TMPDIR_SPEC" != ""; then
    mpinp="$mpinp -x TMPDIR_SPEC"
  fi
  if test "$LD_LIBRARY_PATH" != ""; then
    mpinp="$mpinp -x LD_LIBRARY_PATH"
  fi
  if test "$DYLD_LIBRARY_PATH" != ""; then
    mpinp="$mpinp -x DYLD_LIBRARY_PATH"
  fi
fi

argument() {
  if test $# -gt 1; then
    echo "\"$@\""
  else
    echo "$@"
  fi
}

arguments() {
  args__=""
  for arg in "$@"; do 
    args__="$args__ `argument $arg`"
  done
  echo $args__
}

out_option=""
if test "$outfile" != ""; then
  out_option="> $outfile"
fi
log_option=""
if test "$logfile" != ""; then
  log_option="2> $logfile"
fi

if test "$qsub" != ""; then
    
  if test "$name" = ""; then
    echo "no process name under qsub (specify by --name)" >&2
    exit 1
  fi

  (
    echo "#!/bin/sh"
    echo "#PBS -N $name"
    echo "#PBS -W block=true"
    echo "#PBS -e /dev/null"
    echo "#PBS -o /dev/null"
    echo "#PBS -q $queue"
    if test "$mpimode" = "yes"; then
      echo "#PBS -l select=${np}:ncpus=${nc}:mpiprocs=${nc}:mem=${mem}"
      echo "#PBS -l place=scatter"
    else
      echo "#PBS -l select=1:ncpus=${nc}:mem=${mem}"
    fi
      
    if test "$TMPDIR_SPEC" != ""; then
      echo "export TMPDIR_SPEC=$TMPDIR_SPEC"
    fi
    if test "$LD_LIBRARY_PATH" != ""; then
      echo "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    fi
    if test "$DYLD_LIBRARY_PATH" != ""; then
      echo "export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH"
    fi

    echo "cd $workingdir"

    if test "$mpimode" = "yes"; then
      echo "${openmpi}mpirun $mpinp `arguments "$@"` $out_option $log_option"
    else
      ## shift here!
      shift;
      echo "$stripped `arguments "$@"` $threads $out_option $log_option"
    fi
  ) |
  exec qsub -S /bin/sh
else
  if test "$mpimode" = "yes"; then
    eval "${openmpi}mpirun $mpinp `arguments "$@"` $out_option $log_option" || exit 1
  else
    shift
    eval "$stripped `arguments "$@"` $threads $out_option $log_option" || exit 1
  fi
fi
