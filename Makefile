
gsl = /usr/local/Cellar/gsl/1.16
fftw = /usr/local/Cellar/fftw/3.3.4_1
hdf5 = /usr/local/Cellar/hdf5/1.8.14

default: 
	mpic++ -Wall -O3 -c kd_alloc.cc
	mpic++ -Wall -O3 -c parameter_file.cc
	mpic++ -Wall -O3 -c log.cc
	mpic++ -Wall -O3 -c initialize.cc
	mpic++ -Wall -O3 -c main.cc -I$(fftw)/include -I$(hdf5)/include
	mpic++ -Wall -O3 kd_alloc.o parameter_file.o log.o initialize.o main.o -L$(fftw)/lib -L$(hdf5)/lib -lfftw3_mpi -lfftw3 -lhdf5

