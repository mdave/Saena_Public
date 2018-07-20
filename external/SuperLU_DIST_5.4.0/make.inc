############################################################################
#
#  Program:         SuperLU_DIST
#
#  Module:          make.inc
#
#  Purpose:         Top-level Definitions
#
#  Creation date:   March 1, 2016	version 5.0.0
#
#  Modified:	    October 13, 2017    version 5.2.1
#		    
#
############################################################################
#
#  The name of the libraries to be created/linked to
#
SuperLUroot = /home/boss/Dropbox/Projects/Saena_base/build/build_superlu
DSUPERLULIB = $(SuperLUroot)/SRC/libsuperlu_dist.a
INCLUDEDIR  = $(SuperLUroot)/include

LIBS 	    = $(DSUPERLULIB) /usr/lib/libopenblas.so /home/boss/softwares/parmetis-4.0.3//build/Linux-x86_64/libparmetis/libparmetis.a /home/boss/softwares/parmetis-4.0.3//build/Linux-x86_64/libmetis/libmetis.a
LIBS 	    += 

#
#  The archiver and the flag(s) to use when building archive (library)
#  If your system has no ranlib, set RANLIB = echo.
#
ARCH         = /usr/bin/ar
ARCHFLAGS    = cr
RANLIB       = /usr/bin/ranlib

CC           = /usr/bin/cc
CFLAGS 	     = -O3 -DNDEBUG -I/home/boss/softwares/parmetis-4.0.3//metis/include -I/home/boss/softwares/parmetis-4.0.3//include -DUSE_VENDOR_BLAS -fopenmp  
#CFLAGS      += -D
# CFLAGS     +=  
CXX          = 
CXXFLAGS     = -O3   
XSDK_INDEX_SIZE=
HAVE_PARMETIS=TRUE
HAVE_COMBBLAS=
NOOPTS       = -O0
FORTRAN	     = /usr/bin/gfortran

LOADER       = $(CC)
LOADOPTS     =  -Wl,-rpath -Wl,/usr/local/lib -Wl,--enable-new-dtags -fopenmp