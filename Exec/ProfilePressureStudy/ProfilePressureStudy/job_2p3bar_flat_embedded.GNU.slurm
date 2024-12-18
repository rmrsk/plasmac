#!/bin/bash
#SBATCH --account=nn9887k
#SBATCH --job-name=flat_2p3bar_embedded
#SBATCH --time=0-08:00:00
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=128



## Safety settings, I don't know why this is recommended in the documentation but fine
set -o errexit
set -o nounset

## Software modules
module restore system
module load foss/2022a
module load HDF5/1.12.2-gompi-2022a

# Program to run and input script
executable=program2d.Linux.64.mpic++.gfortran.OPTHIGH.MPI.ex

# Input file
input_file=myplasmamodel_flat_2p3bar_embedded.inputs
chemistry_file=simple_air_chemistry_2p3bar.json
initial_particles_file=initial_particles.dat
N2_molar_fraction_file=N2_molar_fraction.dat
bolsig_file=bolsig_air.dat

# Create directory, copy executable and input script
workdir=$USERWORK/$SLURM_JOB_NAME
if [ ! -d $workdir ]; then mkdir -p $workdir; fi

# Only pout.0
export CH_OUTPUT_INTERVAL=9999999

# Lustre striping
lfs setstripe --stripe-count 4 --stripe-size 32M $workdir

# Copy executables to work directory
cp -f $executable $workdir/$executable
cp -f $input_file $workdir/$input_file
cp -f $chemistry_file  $workdir/$chemistry_file
cp -f $bolsig_file  $workdir/$bolsig_file
cp -f $N2_molar_fraction_file  $workdir/$N2_molar_fraction_file
cp -f $initial_particles_file  $workdir/$initial_particles_file

# Navigate to directory and run
cd $workdir
mpirun ./$executable $input_file
