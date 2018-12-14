#ifndef SAENA_SAENA_OBJECT_H
#define SAENA_SAENA_OBJECT_H

#include "aux_functions.h"

#include <vector>
#include <string>

typedef unsigned int index_t;
typedef unsigned long nnz_t;
typedef double value_t;

class strength_matrix;
class saena_matrix;
class prolong_matrix;
class restrict_matrix;
class Grid;

class saena_object {
public:

    int max_level = 5; // fine grid is level 0.
    // coarsening will stop if the number of rows on one processor goes below 10.
    unsigned int least_row_threshold = 20;
    // coarsening will stop if the number of rows of last level divided by previous level is higher this value,
    // which means the number of rows was not reduced much.
    double row_reduction_up_thrshld = 0.90;
    double row_reduction_down_thrshld = 0.10;
    int vcycle_num = 500;
    double relative_tolerance = 1e-10;
    std::string smoother = "chebyshev"; // choices: "jacobi", "chebyshev"
    int preSmooth  = 3;
    int postSmooth = 3;
    std::string direct_solver = "SuperLU"; // options: 1- CG, 2- SuperLU
    std::vector<Grid> grids;
    float connStrength = 0.2; // connection strength parameter: control coarsening aggressiveness
    int CG_max_iter = 150; //150
    double CG_tol = 1e-14;
    bool repartition = false; // this parameter will be set to true if the partition of input matrix changed. it will be decided in set_repartition_rhs().
//    bool shrink_cpu = true;
    bool dynamic_levels = true;
    bool adaptive_coarsening = false;

    MPI_Comm get_orig_comm();

    const index_t matmat_size_thre = 1000000; // if(row * col) do the dense matmat default 1000000
//    const index_t min_size_threshold = 50; //default 50
    const index_t matmat_nnz_thre = 200; //default 200

    bool doSparsify = false;
    std::string sparsifier = "majid"; // options: 1- TRSL, 2- drineas, majid
    double sparse_epsilon = 1;
    double sample_sz_percent = 1.0;
    double sample_sz_percent_final = 1.0; // = sample_prcnt_numer / sample_prcnt_denom
    nnz_t sample_prcnt_numer = 0;
    nnz_t sample_prcnt_denom = 0;

    int set_shrink_levels(std::vector<bool> sh_lev_vec);
    std::vector<bool> shrink_level_vector;
    int set_shrink_values(std::vector<int> sh_val_vec);
    std::vector<int> shrink_values_vector;

    bool switch_repartition = false;
    int set_repartition_threshold(float thre);
    float repartition_threshold = 0.1;
    bool switch_to_dense = false;
    float dense_threshold = 0.1; // 0<dense_threshold<=1 decide when to switch to the dense structure.
                                 // dense_threshold should be greater than repartition_threshold, since it is more efficient on repartition based on the number of rows.

    // memory pool used in triple_mat_mult
    value_t *mempool1;
    index_t *mempool2;

    bool verbose                  = false;
    bool verbose_setup            = true;
    bool verbose_setup_steps      = false;
    bool verbose_level_setup      = false;
    bool verbose_coarsen          = false;
    bool verbose_coarsen2         = false;
    bool verbose_matmat           = false;
    bool verbose_matmat_recursive = false;
    bool verbose_matmat_A         = false;
    bool verbose_matmat_B         = false;
    bool verbose_solve            = true;
    bool verbose_vcycle           = true;
    bool verbose_vcycle_residuals = false;
    bool verbose_solve_coarse     = false;

    saena_object();
    ~saena_object();
    int destroy();

    void set_parameters(int vcycle_num, double relative_tolerance, std::string smoother, int preSmooth, int postSmooth);
    int setup(saena_matrix* A);
    int coarsen(Grid *grid);
    int triple_mat_mult(Grid *grid);
    int triple_mat_mult_old(Grid *grid);
    int coarsen_update_Ac(Grid *grid, std::vector<cooEntry> &diff);

    int fast_mm_nnz(cooEntry *A, cooEntry *B, std::vector<cooEntry> &C, nnz_t A_nnz, nnz_t B_nnz,
                index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                index_t B_col_size, index_t B_col_offset,
                index_t *nnzPerColScan_leftStart, index_t *nnzPerColScan_leftEnd,
                index_t *nnzPerColScan_rightStart, index_t *nnzPerColScan_rightEnd, MPI_Comm comm);
    int fast_mm_orig(cooEntry *A, cooEntry *B, std::vector<cooEntry> &C, nnz_t A_nnz, nnz_t B_nnz,
                index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                index_t B_col_size, index_t B_col_offset,
                index_t *nnzPerColScan_leftStart, index_t *nnzPerColScan_leftEnd,
                index_t *nnzPerColScan_rightStart, index_t *nnzPerColScan_rightEnd, MPI_Comm comm);


    int fast_mm(const cooEntry *A, const cooEntry *B, std::vector<cooEntry> &C,
                nnz_t A_nnz, nnz_t B_nnz,
                index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                index_t B_col_size, index_t B_col_offset,
                const index_t *nnzPerColScan_leftStart,  const index_t *nnzPerColScan_leftEnd,
                const index_t *nnzPerColScan_rightStart, const index_t *nnzPerColScan_rightEnd, MPI_Comm comm);
    int fast_mm_part1(const cooEntry *A, const cooEntry *B, std::vector<cooEntry> &C,
                      nnz_t A_nnz, nnz_t B_nnz,
                      index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                      index_t B_col_size, index_t B_col_offset,
                      const index_t *nnzPerColScan_leftStart,  const index_t *nnzPerColScan_leftEnd,
                      const index_t *nnzPerColScan_rightStart, const index_t *nnzPerColScan_rightEnd, MPI_Comm comm);
    int fast_mm_part2(const cooEntry *A, const cooEntry *B, std::vector<cooEntry> &C,
                      nnz_t A_nnz, nnz_t B_nnz,
                      index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                      index_t B_col_size, index_t B_col_offset,
                      const index_t *nnzPerColScan_leftStart,  const index_t *nnzPerColScan_leftEnd,
                      const index_t *nnzPerColScan_rightStart, const index_t *nnzPerColScan_rightEnd, MPI_Comm comm);
    int fast_mm_part3(const cooEntry *A, const cooEntry *B, std::vector<cooEntry> &C,
                      nnz_t A_nnz, nnz_t B_nnz,
                      index_t A_row_size, index_t A_row_offset, index_t A_col_size, index_t A_col_offset,
                      index_t B_col_size, index_t B_col_offset,
                      const index_t *nnzPerColScan_leftStart,  const index_t *nnzPerColScan_leftEnd,
                      const index_t *nnzPerColScan_rightStart, const index_t *nnzPerColScan_rightEnd, MPI_Comm comm);

    int find_aggregation(saena_matrix* A, std::vector<unsigned long>& aggregate, std::vector<index_t>& splitNew);
    int create_strength_matrix(saena_matrix* A, strength_matrix* S);
    int aggregation_1_dist(strength_matrix *S, std::vector<unsigned long> &aggregate, std::vector<unsigned long> &aggArray);
    int aggregation_2_dist(strength_matrix *S, std::vector<unsigned long> &aggregate, std::vector<unsigned long> &aggArray);
    int aggregate_index_update(strength_matrix* S, std::vector<unsigned long>& aggregate, std::vector<unsigned long>& aggArray, std::vector<index_t>& splitNew);
    int create_prolongation(saena_matrix* A, std::vector<unsigned long>& aggregate, prolong_matrix* P);
//    int sparsify_trsl1(std::vector<cooEntry_row> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, MPI_Comm comm);
//    int sparsify_trsl2(std::vector<cooEntry> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, MPI_Comm comm);
//    int sparsify_drineas(std::vector<cooEntry_row> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, MPI_Comm comm);
    int sparsify_majid(std::vector<cooEntry_row> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, double max_val, MPI_Comm comm);
//    int sparsify_majid(std::vector<cooEntry_row> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, double max_val, std::vector<index_t> &splitNew, MPI_Comm comm);
    int sparsify_majid_serial(std::vector<cooEntry> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, double max_val, MPI_Comm comm);
    int sparsify_majid_with_dup(std::vector<cooEntry> & A, std::vector<cooEntry>& A_spars, double norm_frob_sq, nnz_t sample_size, double max_val, MPI_Comm comm);
//    double spars_prob(cooEntry a, double norm_frob_sq);
    double spars_prob(cooEntry a);

    int solve(std::vector<value_t>& u);
    int solve_pcg(std::vector<value_t>& u);
    int vcycle(Grid* grid, std::vector<value_t>& u, std::vector<value_t>& rhs);
    int smooth(Grid* grid, std::string smoother, std::vector<value_t>& u, std::vector<value_t>& rhs, int iter);
    int solve_coarsest_CG(saena_matrix* A, std::vector<value_t>& u, std::vector<value_t>& rhs);
    int solve_coarsest_SuperLU(saena_matrix* A, std::vector<value_t>& u, std::vector<value_t>& rhs);
//    int solve_coarsest_Elemental(saena_matrix* A, std::vector<value_t>& u, std::vector<value_t>& rhs);

    int set_repartition_rhs(std::vector<value_t>& rhs);

    // if Saena needs to repartition the input A and rhs, then call repartition_u() at the start of the solving function.
    // then, repartition_back_u() at the end of the solve function to convert the solution to the initial partition.
    int repartition_u(std::vector<value_t>& u);
    int repartition_back_u(std::vector<value_t>& u);

    // if shrinking happens, u and rhs should be shrunk too.
    int repartition_u_shrink_prepare(Grid *grid);
    int repartition_u_shrink(std::vector<value_t> &u, Grid &grid);
    int repartition_back_u_shrink(std::vector<value_t> &u, Grid &grid);

    // if minor shrinking happens, u and rhs should be shrunk too.
//    int repartition_u_shrink_minor_prepare(Grid *grid);
//    int repartition_u_shrink_minor(std::vector<value_t> &u, Grid &grid);
//    int repartition_back_u_shrink_minor(std::vector<value_t> &u, Grid &grid);

    int shrink_cpu_A(saena_matrix* Ac, std::vector<index_t>& P_splitNew);
    int shrink_u_rhs(Grid* grid, std::vector<value_t>& u, std::vector<value_t>& rhs);
    int unshrink_u(Grid* grid, std::vector<value_t>& u);
    bool active(int l);

    int find_eig(saena_matrix& A);
//    int find_eig_Elemental(saena_matrix& A);
    int local_diff(saena_matrix &A, saena_matrix &B, std::vector<cooEntry> &C);
    int scale_vector(std::vector<value_t>& v, std::vector<value_t>& w);
    int transpose_locally(std::vector<cooEntry> &A, nnz_t size);
    int transpose_locally(std::vector<cooEntry> &A, nnz_t size, std::vector<cooEntry> &B);
    int transpose_locally(std::vector<cooEntry> &A, nnz_t size, index_t row_offset, std::vector<cooEntry> &B);

    // lazy update functions
    int update1(saena_matrix* A_new);
    int update2(saena_matrix* A_new);
    int update3(saena_matrix* A_new);
//    int solve_pcg_update1(std::vector<value_t>& u);
//    int solve_pcg_update2(std::vector<value_t>& u);
//    int solve_pcg_update3(std::vector<value_t>& u);

//    to write saena matrix to a file use related function from saena_matrix class.
//    int writeMatrixToFileA(saena_matrix* A, std::string name);
    int writeMatrixToFileP(prolong_matrix* P, std::string name);
    int writeMatrixToFileR(restrict_matrix* R, std::string name);
    int writeVectorToFileul(std::vector<unsigned long>& v, std::string name, MPI_Comm comm);
    int writeVectorToFileul2(std::vector<unsigned long>& v, std::string name, MPI_Comm comm);
    int writeVectorToFileui(std::vector<unsigned int>& v, std::string name, MPI_Comm comm);
//    template <class T>
//    int writeVectorToFile(std::vector<T>& v, unsigned long vSize, std::string name, MPI_Comm comm);
    int change_aggregation(saena_matrix* A, std::vector<index_t>& aggregate, std::vector<index_t>& splitNew);

    // double versions
//    int vcycle_d(Grid* grid, std::vector<value_d_t>& u_d, std::vector<value_d_t>& rhs_d);
//    int scale_vector_d(std::vector<value_d_t>& v, std::vector<value_t>& w);
//    int solve_coarsest_CG_d(saena_matrix* A, std::vector<value_t>& u, std::vector<value_t>& rhs);
};

#endif //SAENA_SAENA_OBJECT_H

