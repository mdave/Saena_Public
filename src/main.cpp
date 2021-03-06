
// to run this example: ./Saena ../data/81s4x8o1mu1.bin

#include <iostream>
#include <algorithm>
#include "mpi.h"

#include "grid.h"
#include "saena.hpp"

typedef unsigned int index_t;
typedef unsigned long nnz_t;
typedef double value_t;

using namespace std;

int main(int argc, char* argv[]){

    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    if(argc != 2)
    {
        if(rank == 0)
        {
            cout << "Usage: ./Saena <MatrixA>" << endl;
            cout << "Matrix file should be in COO format with column-major order." << endl;
        }
        MPI_Finalize();
        return -1;
    }

    // *************************** initialize the matrix ****************************

    // there are two ways to create a matrix:

    // 1- read from file: pass as an argument in the command line
    // example: ./Saena ../data/81s4x8o1mu1.bin
    char* file_name(argv[1]);
    saena::matrix A (comm);
    A.read_file(file_name);

    // 2- use the set functions
//    saena::matrix A(comm); // comm: MPI_Communicator
//    A.add_duplicates(true); // in case of duplicates add the values.

    // 2-I) pass the entries one by one
//    A.set(0, 0, 0.1);
//    A.set(0, 1, 0.2);
//    A.set(1, 0, 0.3);
//    A.set(1, 1, 0.4);

    // 2-II) three vectors (or arrays) can be passed to the set functions
    // I: row values of type unsinged
    // J: column values of type unsinged
    // V: entry values of type double
//    std::vector<unsigned> I, J;
//    std::vector<double> V;
//    I = {0,0,1,1}; J = {0,1,0,1}; V = {0.1, 0.2, 0.3, 0.4};
//    A.set(&I[0], &J[0], &V[0], I.size());

    // after setting the matrix entries, the assemble function should be called.
    A.assemble();

    // the print function can be used to print the matrix entries on a specific processor (pass the
    // processor rank to the print function), or on all the processors (pass -1).
//    A.print(0);

    // *************************** set rhs ****************************
    // vector should have the following format:
    // first line shows the value in row 0, second line shows the value in row 1, ...
    // entries should be in double.

    // set the size of rhs as the local number of rows on each process
    unsigned int num_local_row = A.get_num_local_rows();
    std::vector <double> rhs(num_local_row);

    // generate a random vector for rhs for test purposes
    generate_rhs_old(rhs);

    // read the vector and set it in rhs
//    char* Vname(argv[2]);
//    saena::read_vector_file(rhs, A, Vname, comm);
//    read_vector_file(rhs, A.get_internal_matrix(), Vname, comm);

    // *************************** set u0 ****************************

    // u is the initial guess. at the end, it will be the solution.
    std::vector<double> u(num_local_row, 0); // initial guess = 0

    // *************************** AMG - Setup ****************************
    // There are 3 ways to set options:

    // 1- set them manually
//    int vcycle_num            = 300;
//    double relative_tolerance = 1e-8;
//    std::string smoother      = "jacobi";
//    int preSmooth             = 2;
//    int postSmooth            = 2;
//    saena::options opts(vcycle_num, relative_tolerance, smoother, preSmooth, postSmooth);

    // 2- read the options from an xml file
//    saena::options opts((char*)"options001.xml");

    // 3- use the default options
    saena::options opts;
    saena::amg solver;
    solver.set_matrix(&A, &opts);
    solver.set_rhs(rhs);

    // *************************** AMG - Solve ****************************
    // solve the system Au = rhs

    // solve the system using AMG as the solver
//    solver.solve(u, &opts);

    // solve the system, using AMG as the preconditioner. this is preconditioned conjugate gradient (PCG).
    solver.solve_pcg(u, &opts);

    // *************************** Destroy ****************************

    A.destroy();
    solver.destroy();
    MPI_Finalize();
    return 0;
}
