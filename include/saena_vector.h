#ifndef SAENA_SAENA_VECTOR_H
#define SAENA_SAENA_VECTOR_H

#include "aux_functions.h"

#include <set>

/**
 * @author Majid Rasouli
 * @breif Contains the basic structure for the Saena vector class (saena_vector).
 *
 * */


class saena_vector {

    // Steps of creating a vector of this class:
//    parameter	        type	                    reason
//    -----------------------------------------------------------------------------------
//    data_set	        std::set<vecEntry>		    add entries by set()
//    data_unsorted	    std::vector<vecEntry>	    switch from std::set to std::vector
//    data_sorted_dup   std::vector<vecEntry>		parallel sort
//    data		        std::vector<vecEntry>		remove duplicates

private:
//    index_t M     = 0;
//    index_t Mbig  = 0;

    std::set<vecEntry>    data_set;
    std::vector<vecEntry> data;
//    std::vector<double>   val;

    int numRecvProc = 0;
    int numSendProc = 0;
    index_t send_sz = 0;
    index_t recv_sz = 0;
    std::vector<int> vdispls;
    std::vector<int> rdispls;
    std::vector<int> recvCount;
    std::vector<int> recvCountScan;
    std::vector<int> sendCount;
    std::vector<int> sendCountScan;
    std::vector<int> recvProcRank;
    std::vector<int> recvProcCount;
    std::vector<int> sendProcRank;
    std::vector<int> sendProcCount;
    std::vector<index_t> recv_idx;
    std::vector<index_t> send_idx;
    std::vector<value_t> send_vals;
    std::vector<value_t> recv_vals;

public:
    MPI_Comm comm = MPI_COMM_WORLD;
    bool add_duplicates = false;
    int set_dup_flag(bool add);

    index_t idx_offset = 0;
    std::vector<index_t> orig_order; // save the input order
    std::vector<tuple1> remote_idx_tuple;
//    std::vector<index_t> remote_idx; // indices that should receive their value from other procs
    std::vector<index_t> split;
//    index_t *split; // point to the split of input matrix. size of it is (size of comm + 1).


    bool return_vec_prep    = false;        // to track if the parameters needed in return_vec() are computed.

    bool verbose_return_vec = false;

    saena_vector();
    explicit saena_vector(MPI_Comm com);
    ~saena_vector();

    void set_comm(MPI_Comm com);
    int set_idx_offset(index_t offset);
    void set(index_t idx, value_t val); // set one entry
//    int set_rep_dup(index_t row, value_t val); // replace duplicates
//    int set_add_dup(index_t row, value_t val); // add duplicates
//    int set(index_t *row, value_t *val, index_t size);
    void set(const index_t* idx, const value_t* val, index_t size);
    void set(const value_t* val, index_t size, index_t offset = 0);

    void remove_duplicates();
    void assemble();

    index_t get_size() const;
    void get_vec(value_t *&vec);
    int return_vec(const value_t *u1, value_t *&u2, std::vector<index_t> &final_split);
    int return_vec(value_t *&u, const index_t sz, std::vector<index_t> &final_split);

    int print_entry(int ran);
};

#endif //SAENA_SAENA_VECTOR_H
