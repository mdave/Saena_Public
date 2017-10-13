//
// Created by abaris on 3/14/17.
//

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <set>
#include <mpi.h>
#include <usort/parUtils.h>

#include "saena_object.h"
#include "saena_matrix.h"
#include "strength_matrix.h"
#include "prolong_matrix.h"
#include "restrict_matrix.h"
#include "aux_functions.h"
#include "grid.h"


saena_object::saena_object(){
//    maxLevel = max_lev-1;
} //SaenaObject


saena_object::~saena_object(){}


int saena_object::destroy(){
    return 0;
}


void saena_object::set_parameters(int vcycle_n, double relT, std::string sm, int preSm, int postSm){
//    maxLevel = l-1; // maxLevel does not include fine level. fine level is 0.
    vcycle_num = vcycle_n;
    relative_tolerance  = relT;
    smoother = sm;
    preSmooth = preSm;
    postSmooth = postSm;
}


int saena_object::setup(saena_matrix* A) {
    MPI_Comm comm = A->comm;
    int nprocs, rank;
//    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int i;
    unsigned int M_current;
    float row_reduction_local, row_reduction_min;

    if(verbose) if(rank==0) std::cout << "_____________________________\n\n" << "size of matrix level 0: " << A->Mbig
                          << "\nnnz level 0: " << A->nnz_g << std::endl;

    grids.resize(max_level+1);
    grids[0] = Grid(A, max_level, 0);
    grids[0].comm = comm;
    for(i = 0; i < max_level; i++){
        level_setup(&grids[i]);
        grids[i+1] = Grid(&grids[i].Ac, max_level, i+1);
        grids[i].coarseGrid = &grids[i+1];
        grids[i+1].comm = grids[i].Ac.comm;

        if(verbose) if(rank==0) std::cout << "_____________________________\n\n" << "size of matrix level "
                              << grids[i+1].currentLevel << ": " << grids[i+1].A->Mbig
                              << "\nnnz level " << grids[i+1].currentLevel << ": " << grids[i+1].A->nnz_g << std::endl;

        //threshold to set maximum multigrid level
        MPI_Allreduce(&grids[i].Ac.M, &M_current, 1, MPI_UNSIGNED, MPI_MIN, grids[0].comm);
        row_reduction_local = (float)grids[i].Ac.M / grids[i].A->M;
        MPI_Allreduce(&row_reduction_local, &row_reduction_min, 1, MPI_FLOAT, MPI_MIN, grids[0].comm);

        // todo: talk to Hari about least_row_threshold and row_reduction.
        if( (M_current < least_row_threshold) || (row_reduction_min > row_reduction_threshold) ){
            if(row_reduction_min == 1.0){
                max_level = grids[i].currentLevel;
            } else{
                max_level = grids[i].currentLevel+1;
            }
            grids.resize(max_level);
//            if(rank==0) printf("\nset max level to %d\n", max_level);
            break;
        }
    }

    return 0;
}


int saena_object::level_setup(Grid* grid){

    MPI_Comm comm = grid->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);
//    bool verbose = false;

//    printf("\nrank = %d, level_setup: level = %d \n", rank, grid->currentLevel);

    // todo: think about a parameter for making the aggregation less or more aggressive.
    std::vector<unsigned long> aggregate(grid->A->M);
    double t1 = MPI_Wtime();
    find_aggregation(grid->A, aggregate, grid->P.splitNew);
    double t2 = MPI_Wtime();
    if(verbose) print_time(t1, t2, "Aggregation: level "+std::to_string(grid->currentLevel), comm);

//    if(rank==0)
//        for(auto i:aggregate)
//            std::cout << i << std::endl;

    // todo: delete this!!!!!!!!!!!
//    changeAggregation(grid->A, aggregate, grid->P.splitNew, comm);

    t1 = MPI_Wtime();
    create_prolongation(grid->A, aggregate, &grid->P);
    t2 = MPI_Wtime();
    if(verbose) print_time(t1, t2, "Prolongation: level "+std::to_string(grid->currentLevel), comm);

    t1 = MPI_Wtime();
    grid->R.transposeP(&grid->P);
    t2 = MPI_Wtime();
    if(verbose) print_time(t1, t2, "Restriction: level "+std::to_string(grid->currentLevel), comm);

    t1 = MPI_Wtime();
    coarsen(grid->A, &grid->P, &grid->R, &grid->Ac);
    t2 = MPI_Wtime();
    if(verbose) print_time(t1, t2, "Coarsening: level "+std::to_string(grid->currentLevel), comm);

    return 0;
}


// ******************** shrink cpus ********************
// this part was located in SaenaObject::AMGSetup function after findAggregation.
// todo: shrink only if it is required to go to the next multigrid level.
// todo: move the shrinking to after aggregation.

/*
// check if the cpus need to be shrinked
int threshold1 = 1000*nprocs;
int threshold2 = 100*nprocs;

if(R.Mbig < threshold1){
    int color = rank/4;
    MPI_Comm comm2;
    MPI_Comm_split(comm, color, rank, &comm2);

    int nprocs2, rank2;
    MPI_Comm_size(comm2, &nprocs2);
    MPI_Comm_rank(comm2, &rank2);

    bool active = false;
    if(rank2 == 0)
        active = true;
//        if(active) printf("rank=%d\n", rank2);
//        printf("rank=%d\trank2=%d\n", rank, rank2);

    // ******************** send the size from children to parent ********************

    unsigned long* nnzGroup = NULL;
    if(active)
        nnzGroup = (unsigned long*)malloc(sizeof(unsigned long)*4);
    MPI_Gather(&R.nnz_l, 1, MPI_UNSIGNED_LONG, nnzGroup, 1, MPI_UNSIGNED_LONG, 0, comm2);

    unsigned long* rdispls = NULL;
    if(active)
        rdispls = (unsigned long*)malloc(sizeof(unsigned long)*3);

    unsigned long nnzNew = 0;
    unsigned long nnzRecv = 0;
    if(active){
        rdispls[0] = 0;
        rdispls[1] = nnzGroup[1];
        rdispls[2] = rdispls[1] + nnzGroup[2];

        for(i=0; i<4; i++)
            nnzNew += nnzGroup[i];

        nnzRecv = nnzNew - R.nnz_l;
//            if(rank==0){
//                std::cout << "rdispls:" << std::endl;
//                for(i=0; i<3; i++)
//                    std::cout << rdispls[i] << std::endl;
//            }
    }

//        printf("rank=%d\tnnzNew=%lu\tnnzRecv=%lu\n", rank, nnzNew, nnzRecv);

    // ******************** allocate memory ********************

    cooEntry* sendData = NULL;
    if(!active){
        sendData = (cooEntry*)malloc(sizeof(cooEntry)*R.nnz_l);
        for(i=0; i<R.entry_local.size(); i++)
            sendData[i] = R.entry_local[i];
        for(i=0; i<R.entry_remote.size(); i++)
            sendData[i + R.entry_local.size()] = R.entry_remote[i];
    }

//        MPI_Barrier(comm2);
//        if(rank2==2) std::cout << "sendData:" << std::endl;
//        for(i=0; i<R.entry_local.size()+R.entry_remote.size(); i++)
//            if(rank2==2) std::cout << sendData[i].row << "\t" << sendData[i].col << "\t" << sendData[i].val << std::endl;

    cooEntry* recvData = NULL;
    if(active)
        recvData = (cooEntry*)malloc(sizeof(cooEntry)*nnzRecv);

    // ******************** send data from children to parent ********************

    int numRecvProc = 0;
    int numSendProc = 1;
    if(active){
        numRecvProc = 3;
        numSendProc = 0;
    }

    MPI_Request* requests = new MPI_Request[numSendProc+numRecvProc];
    MPI_Status* statuses = new MPI_Status[numSendProc+numRecvProc];

    for(int i = 0; i < numRecvProc; i++)
        MPI_Irecv(&recvData[rdispls[i]], (int)nnzGroup[i+1], cooEntry::mpi_datatype(), i+1, 1, comm2, &requests[i]);

    if(!active)
        MPI_Isend(sendData, (int)R.nnz_l, cooEntry::mpi_datatype(), 0, 1, comm2, &requests[numRecvProc]);

    MPI_Waitall(numSendProc+numRecvProc, requests, statuses);

//        if(active)
//            for(i=0; i<nnzRecv; i++)
//                std::cout << i << "\t" << recvData[i].row << "\t" << recvData[i].col << "\t" << recvData[i].val << std::endl;

    if(active)
        free(nnzGroup);
    if(!active)
        free(sendData);

    // update split?

    // update threshol1

    if(active){
        free(recvData);
        free(rdispls);
    }
    MPI_Comm_free(&comm2);
}//end of cpu shrinking
*/


int saena_object::find_aggregation(saena_matrix* A, std::vector<unsigned long>& aggregate, std::vector<unsigned long>& splitNew){
//    int nprocs, rank;
//    MPI_Comm_size(comm, &nprocs);
//    MPI_Comm_rank(comm, &rank);

    strength_matrix S;
    create_strength_matrix(A, &S);
//    S.print(0);

//    unsigned long aggSize = 0;
    aggregation(&S, aggregate, splitNew);
//    updateAggregation(aggregate, &aggSize);
//    printf("rank = %d \n", rank);

//    if(rank==0)
//        for(long i=0; i<S.M; i++)
//            std::cout << i << "\t" << aggregate[i] << std::endl;

    return 0;
} // end of SaenaObject::findAggregation


int saena_object::create_strength_matrix(saena_matrix* A, strength_matrix* S){

    MPI_Comm comm = A->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

//    if(rank==0) std::cout << "M = " << A->M << ", nnz_l = " << A->nnz_l << std::endl;

    // ******************************** compute max per row ********************************

    unsigned int i;
//    double maxPerRow[A->M];
    std::vector<double> maxPerRow(A->M);
    std::fill(&maxPerRow[0], &maxPerRow[A->M], 0);
    for(i=0; i<A->nnz_l; i++){
        if( A->entry[i].row != A->entry[i].col ){
            if(maxPerRow[A->entry[i].row - A->split[rank]] == 0) // use split to convert the index from global to local.
                maxPerRow[A->entry[i].row - A->split[rank]] = -A->entry[i].val;
            else if(maxPerRow[A->entry[i].row - A->split[rank]] < -A->entry[i].val)
                maxPerRow[A->entry[i].row - A->split[rank]] = -A->entry[i].val;
        }
    }

//    if(rank==0)
//        for(i=0; i<A->M; i++)
//            std::cout << i << "\t" << maxPerRow[i] << std::endl;

    // ******************************** compute S ********************************

    std::vector<unsigned long> Si;
    std::vector<unsigned long> Sj;
    std::vector<double> Sval;
    for(i=0; i<A->nnz_l; i++){
        if(A->entry[i].row == A->entry[i].col) {
            Si.push_back(A->entry[i].row);
            Sj.push_back(A->entry[i].col);
            Sval.push_back(1);
        }
        else if(maxPerRow[A->entry[i].row - A->split[rank]] != 0) {
//            if ( -A->values[i] / (maxPerRow[A->row[i] - A->split[rank]] ) > connStrength) {
            Si.push_back(A->entry[i].row);
            Sj.push_back(A->entry[i].col);
            Sval.push_back(  -A->entry[i].val / (maxPerRow[A->entry[i].row - A->split[rank]])  );
//                if(rank==0) std::cout << "A.val = " << -A->values[i] << ", max = " << maxPerRow[A->row[i] - A->split[rank]] << ", divide = " << (-A->values[i] / (maxPerRow[A->row[i] - A->split[rank]])) << std::endl;
//            }
        }
    }

/*    if(rank==0)
        for (i=0; i<Si.size(); i++)
            std::cout << "val = " << Sval[i] << std::endl;*/

    // ******************************** compute max per column - version 1 - for general matrices ********************************

//    double local_maxPerCol[A->Mbig];
    std::vector<double> local_maxPerCol(A->Mbig);
    double* local_maxPerCol_p = &(*local_maxPerCol.begin());
    local_maxPerCol.assign(A->Mbig,0);
//    fill(&local_maxPerCol[0], &local_maxPerCol[A->Mbig], 0);

    for(i=0; i<A->nnz_l; i++){
        if( A->entry[i].row != A->entry[i].col ){
            if(local_maxPerCol[A->entry[i].col] == 0)
                local_maxPerCol[A->entry[i].col] = -A->entry[i].val;
            else if(local_maxPerCol[A->entry[i].col] < -A->entry[i].val)
                local_maxPerCol[A->entry[i].col] = -A->entry[i].val;
        }
    }

//    double maxPerCol[A->Mbig];
    std::vector<double> maxPerCol(A->Mbig);
    double* maxPerCol_p = &(*maxPerCol.begin());
//    MPI_Allreduce(&local_maxPerCol, &maxPerCol, A->Mbig, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(local_maxPerCol_p, maxPerCol_p, A->Mbig, MPI_DOUBLE, MPI_MAX, comm);

//    if(rank==0)
//        for(i=0; i<A->Mbig; i++)
//            std::cout << i << "\t" << maxPerCol[i] << std::endl;

    // ******************************** compute ST - version 1 ********************************

    std::vector<long> STi;
    std::vector<long> STj;
    std::vector<double> STval;
    for(i=0; i<A->nnz_l; i++){
        if(A->entry[i].row == A->entry[i].col) {
            STi.push_back(A->entry[i].row - A->split[rank]);
            STj.push_back(A->entry[i].col - A->split[rank]);
            STval.push_back(1);
        }
        else{
//            if ( (-A->values[i] / maxPerCol[A->col[i]]) > connStrength) {
            STi.push_back(A->entry[i].row - A->split[rank]);
            STj.push_back(A->entry[i].col - A->split[rank]);
            STval.push_back( -A->entry[i].val / maxPerCol[A->entry[i].col] );
//            }
        }
    }

//    if(rank==1)
//        for(i=0; i<STi.size(); i++)
//            std::cout << "ST: " << "[" << STi[i]+1 << "," << STj[i]+1 << "] = " << STval[i] << std::endl;

    // ******************************** compute max per column - version 2 - of A is symmetric matrices ********************************

/*
    // since A is symmetric, use maxPerRow for local entries on each process. receive the remote ones like matvec.

    //vSend are maxPerCol for remote elements that should be sent to other processes.
    for(i=0;i<A->vIndexSize;i++)
        A->vSend[i] = maxPerRow[( A->vIndex[i] )];

    MPI_Request* requests = new MPI_Request[A->numSendProc+A->numRecvProc];
    MPI_Status* statuses = new MPI_Status[A->numSendProc+A->numRecvProc];

    //vecValues are maxperCol for remote elements that are received from other processes.
    // Do not recv from self.
    for(i = 0; i < A->numRecvProc; i++)
        MPI_Irecv(&A->vecValues[A->rdispls[A->recvProcRank[i]]], A->recvProcCount[i], MPI_DOUBLE, A->recvProcRank[i], 1, comm, &(requests[i]));

    // Do not send to self.
    for(i = 0; i < A->numSendProc; i++)
        MPI_Isend(&A->vSend[A->vdispls[A->sendProcRank[i]]], A->sendProcCount[i], MPI_DOUBLE, A->sendProcRank[i], 1, comm, &(requests[A->numRecvProc+i]));

    // ******************************** compute ST - version 2 ********************************

    std::vector<long> STi;
    std::vector<long> STj;
    std::vector<double> STval;

    // add OpenMP just like matvec.
    long iter = 0;
    long iter2 = 0;
//    for (i = 0; i < A->M; ++i, iter2++) {
//        for (unsigned int j = 0; j < A->nnzPerRow_local[i]; ++j, ++iter) {
//
//            // diagonal entry
//            if(i == A->col_local[A->indicesP_local[iter]]){
//                STi.push_back(iter2); // iter2 is actually i, but it was giving an error for using i.
//                STj.push_back(A->col_local[A->indicesP_local[iter]]);
//                STval.push_back(1);
//                continue;
//            }
//
//            STi.push_back(iter2); // iter2 is actually i, but it was giving an error for using i.
//            STj.push_back(A->col_local[A->indicesP_local[iter]]);
//            STval.push_back( -A->values_local[A->indicesP_local[iter]] / maxPerRow[A->col_local[A->indicesP_local[iter]]] );
//        }
//    }

    // local ST values
    for (i = 0; i < A->nnz_l_local; ++i, iter2++) {
        // diagonal entry
        if(A->row_local[i] == A->col_local[i]){
            STi.push_back(A->row_local[i]);
            STj.push_back(A->col_local[i]);
            STval.push_back(1);
            continue;
        }
        STi.push_back(A->row_local[i]);
        STj.push_back(A->col_local[i]);
        STval.push_back( -A->values_local[i] / maxPerRow[A->col_local[i]] );
    }

    MPI_Waitall(A->numSendProc+A->numRecvProc, requests, statuses);

    // add OpenMP just like matvec.
//    iter = 0;
//    for (i = 0; i < A->col_remote_size; ++i) {
//        for (unsigned int j = 0; j < A->nnz_col_remote[i]; ++j, ++iter) {
//            STi.push_back(A->row_remote[A->indicesP_remote[iter]]);
//            STj.push_back(A->col_remote2[A->indicesP_remote[iter]]);
//            STval.push_back( -A->values_remote[A->indicesP_remote[iter]] / A->vecValues[A->col_remote[A->indicesP_remote[iter]]] );
//        }
//    }

    // remote ST values
    // add OpenMP just like matvec.
    iter = 0;
    for (i = 0; i < A->vElement_remote.size(); ++i) {
        for (unsigned int j = 0; j < A->vElementRep_remote[i]; ++j, ++iter) {
//            w[A->row_remote[A->indicesP_remote[iter]]] += A->values_remote[A->indicesP_remote[iter]] * A->vecValues[A->col_remote[A->indicesP_remote[iter]]];
            STi.push_back(A->row_remote[iter]);
            STj.push_back(A->col_remote2[iter]);
            STval.push_back( -A->values_remote[iter] / A->vecValues[i] );
        }
    }

//    if(rank==0)
//        for(i=0; i<STi.size(); i++)
//            std::cout << "ST: " << "[" << STi[i]+1 << "," << STj[i]+1 << "] = " << STval[i] << std::endl;
*/

    // *************************** make S symmetric and apply the connection strength parameter ****************************

    std::vector<unsigned long> Si2;
    std::vector<unsigned long> Sj2;
    std::vector<double> Sval2;

    for(i=0; i<Si.size(); i++){
        if (Sval[i] <= connStrength && STval[i] <= connStrength)
            continue;
        else if (Sval[i] > connStrength && STval[i] <= connStrength){
            Si2.push_back(Si[i]);
            Sj2.push_back(Sj[i]);
            Sval2.push_back(0.5*Sval[i]);
        }
        else if (Sval[i] <= connStrength && STval[i] > connStrength){
            Si2.push_back(Si[i]);
            Sj2.push_back(Sj[i]);
            Sval2.push_back(0.5*STval[i]);
        }
        else{
            Si2.push_back(Si[i]);
            Sj2.push_back(Sj[i]);
            Sval2.push_back(0.5*(Sval[i] + STval[i]));
        }
    }

//    if(rank==1)
//        for(i=0; i<Si2.size(); i++){
//            std::cout << "S:  " << "[" << (Si2[i] - A->split[rank]) << "," << Sj2[i] << "] = \t" << Sval2[i] << std::endl;
//        }

    // S indices are local on each process, which means it starts from 0 on each process.
    S->strength_matrix_set(&(*(Si2.begin())), &(*(Sj2.begin())), &(*(Sval2.begin())), A->M, A->Mbig, Si2.size(), &(*(A->split.begin())), comm);

    return 0;
} // end of SaenaObject::createStrengthMatrix


// Using MIS(2) from the following paper by Luke Olson:
// EXPOSING FINE-GRAINED PARALLELISM IN ALGEBRAIC MULTIGRID METHODS
int saena_object::aggregation(strength_matrix* S, std::vector<unsigned long>& aggregate, std::vector<unsigned long>& splitNew) {

    // For each node, first assign it to a 1-distance root. If there is not any root in distance-1, find a distance-2 root.
    // If there is not any root in distance-2, that node should become a root.

    // variables used in this function:
    // weight[i]: the two most left bits store the status of node i, the other 62 bits store weight assigned to that node.
    //            status of a node: 1 for 01 not assigned, 0 for 00 assigned, 2 for 10 root
    //            the max value for weight is 2^63 - 1
    //            weight is first generated randomly by randomVector function and saved in initialWeight. During the
    //            aggregation process, it becomes the weight of the node's aggregate.

    // todo: idea: the fine matrix is divided in a way for the sake of work-balance. consider finding almost
    // todo: the same number of aggregates on different processors to keep it work-balanced for also coarse matrices.

    MPI_Comm comm = S->comm;

    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    unsigned long i, j;
    unsigned long size = S->M;

    std::vector<unsigned long> aggArray; // root nodes.
    std::vector<unsigned long> aggregate2(size);
//    std::vector<unsigned long> aggStatus2(size); // 1 for 01 not assigned, 0 for 00 assigned, 2 for 10 root
    std::vector<unsigned long> weight(size);
    std::vector<unsigned long> weight2(size);
    std::vector<unsigned long> initialWeight(size);

//    randomVector(initialWeight, S->Mbig, S, comm);
    randomVector3(initialWeight, S->Mbig, S, comm);
//    randomVector4(initialWeight, S->Mbig);

/*
    MPI_Barrier(comm);
    if(rank==0){
        std::cout << std::endl << "after initialization!" << std::endl;
        for (i = 0; i < size; ++i)
            std::cout << i << "\tinitialWeight = " << initialWeight[i] << std::endl;}
    MPI_Barrier(comm);
    if(rank==1){
        std::cout << std::endl << "after initialization!" << std::endl;
        for (i = 0; i < size; ++i)
            std::cout << i << "\tinitialWeight = " << initialWeight[i] << std::endl;}
    MPI_Barrier(comm);
    if(rank==2){
        std::cout << std::endl << "after initialization!" << std::endl;
        for (i = 0; i < size; ++i)
            std::cout << i << "\tinitialWeight = " << initialWeight[i] << std::endl;}
    MPI_Barrier(comm);
    if(rank==3){
        std::cout << std::endl << "after initialization!" << std::endl;
        for (i = 0; i < size; ++i)
            std::cout << i << "\tinitialWeight = " << initialWeight[i] << std::endl;}
    MPI_Barrier(comm);
*/

    const int wOffset = 62;
    const unsigned long weightMax = (1UL<<wOffset) - 1;
    const unsigned long UNDECIDED = 1UL<<wOffset;
    const unsigned long ROOT = 1UL<<(wOffset+1);
    const unsigned long UNDECIDED_OR_ROOT = 3UL<<wOffset;
    unsigned long weightTemp, aggregateTemp, aggStatusTemp;
    int* root_distance = (int*)malloc(sizeof(int)*size);
    // root_distance is initialized to 3(11). root = 0 (00), 1-distance root = 1 (01), 2-distance root = 2 (10).
    bool* dist1or2undecided = (bool*)malloc(sizeof(bool)*size);
    // if there is a distance-2 neighbor which is undecided, set this to true.
//    fill(&dist2undecided[0], &dist2undecided[size], 0);
    bool continueAggLocal;
    bool continueAgg = true;
    unsigned long i_remote, j_remote, weight_remote, agg_remote;
    unsigned long iter, col_index;
    int whileiter = 0;

    MPI_Request *requests = new MPI_Request[S->numSendProc + S->numRecvProc];
    MPI_Status *statuses  = new MPI_Status[S->numSendProc + S->numRecvProc];

    // initialization -> this part is merged to the first "for" loop in the following "while".
    for(i=0; i<size; i++) {
        aggregate[i] = i + S->split[rank];
//        aggStatus2[i] = 1;
        // Boundary nodes are the ones which only have one neighbor (so one nnzPerRow), which is the diagonal element. They are roots for every coarse-grid.
        if(S->nnzPerRow[i] == 1){
            weight[i] = ( 2UL<<wOffset | initialWeight[i] );
            root_distance[i] = 0;
            aggArray.push_back(aggregate[i]);
//            if(rank==0) std::cout << "boundary: " << i+S->split[rank] << std::endl;
        }
        else{
            weight[i] = ( 1UL<<wOffset | initialWeight[i] ); // status of each node is initialized to 1 and its weight to initialWeight.
//            if(rank==0) std::cout << "V[" << i+S->split[rank] << "] = " << initialWeight[i] << ";" << std::endl;
        }
    }

//    if(rank==0){
//        std::cout << "if(rank==0){" << std::endl;
//        for(i=0; i<size; i++)
//            std::cout << "V[" << i << "] = " << initialWeight[i] << ";" << std::endl;
//        std::cout << "}" << std::endl;
//    }
//    MPI_Barrier(comm);
//    if(rank==1){
//        std::cout << "if(rank==1){" << std::endl;
//        for(i=0; i<size; i++)
//            std::cout << "V[" << i << "] = " << initialWeight[i] << ";" << std::endl;
//        std::cout << "}" << std::endl;
//    }
//    MPI_Barrier(comm);

    while(continueAgg) {
        // ******************************* first round of max computation *******************************
        // first "compute max" is local. The second one is both local and remote.
        // for loop is of size "number of rows". it checks if a node is UNDECIDED. Then, it goes over its neighbors, which are nonzeros on that row.
        // if the neighbor is UNDECIDED or ROOT, and its weight is higher than weightTemp, then than node will be chosen for root.
        // UNDECIDED is also considered because it may become a root later, and it may be a better root for that node.
        // In the next "for" loop, weight and aggregate are updated, but not the status.

        iter = 0;
        for (i = 0; i < size; ++i) {
            if ( (weight[i] >> wOffset == 0) && (root_distance[i] == 2) ) {
                for (j = 0; j < S->nnzPerRow_local[i]; ++j, ++iter) {
                    col_index = S->col_local[S->indicesP_local[iter]] - S->split[rank];
                    if (weight[col_index] & ROOT) {
//                        std::cout << "$$$$$$$$$$$$$$$$$$$$$$$$$" << i << "\t col_index = " << col_index << "\t weight[col_index] = " << (weight[col_index] & weightMax) << "\t aggregate = " << S->col_local[S->indicesP_local[iter]] << std::endl;
                        weight[i] = (0UL << wOffset | (weight[col_index] & weightMax));
                        aggregate[i] = S->col_local[S->indicesP_local[iter]];
                        root_distance[i] = 1;
                    }
//                    break; todo: try to add this break.
                }
            }else
                iter += S->nnzPerRow_local[i];
        }

        // todo: check the aggregation for deadlock: two neighbors wait for each other to have a status other than 1, while both have status 1.
        //distance-1 aggregate
        iter = 0;
        for (i = 0; i < size; ++i) {
            if(weight[i]&UNDECIDED) {
//                if(i==25) std::cout << ">>>>>>>>>>>>>>>>>25 root ==================== " << root_distance[25] << "\taggregate = " << aggregate[25] << std::endl;
//            if(weight[i]>>wOffset <= 1) {
                root_distance[i] = 3; // initialization
                dist1or2undecided[i] = false; // initialization
                aggregateTemp = aggregate[i];
                weightTemp = weight[i]&weightMax;
//                weight2[i] = weight[i]&weightMax;
//                aggStatusTemp = 1UL; // this will be used for aggStatus2, and aggStatus2 will be used for the remote part.
                for (j = 0; j < S->nnzPerRow_local[i]; ++j, ++iter) {
                    col_index = S->col_local[S->indicesP_local[iter]] - S->split[rank];
                    if(weight[col_index] & ROOT){
                        weight[i] = (0UL << wOffset | (weight[col_index] & weightMax)); // 0UL << wOffset is not required.
                        weightTemp = weight[i];
                        aggregate[i] = S->col_local[S->indicesP_local[iter]];
                        aggregateTemp = aggregate[i];
                        root_distance[i] = 1;
                        dist1or2undecided[i] = false;

//                        if(rank==0) std::cout << i+S->split[rank] << "\t assigned to = " << aggregate[i] << " distance-1 local \t weight = " << weight[i] << std::endl;
//                        break; todo: try to add this break. You can define nnzPerRowScan_local.
                    } else if( (weight[col_index] & UNDECIDED) && (initialWeight[col_index] >= weightTemp) ){
                        // if there is an UNDECIDED neighbor, and its weight is bigger than this node, this node should
                        // wait until the next round of the "while" loop to see if that neighbor becomes root or assigned.
//                        if(rank==0) std::cout << i+S->split[rank] << "\t neighbor = " << S->col_local[S->indicesP_local[iter]] << std::endl;

                        if((initialWeight[col_index] > weightTemp) || ((initialWeight[col_index] == weightTemp) && (aggregate[col_index] > i+S->split[rank])) ){
//                            if(rank==0 && i==10) std::cout << "??????????? " << col_index << "\tweight = " << (weight[col_index]&weightMax) << "\tagg = " << aggregate[col_index] << std::endl;
                            weightTemp = (weight[col_index] & weightMax);
                            aggregateTemp = S->col_local[S->indicesP_local[iter]];
                            root_distance[i] = 1;
                            dist1or2undecided[i] = true;
                        }
                    }
                }
                weight2[i]    = weightTemp;
                aggregate2[i] = aggregateTemp;
//                if(rank==1) std::cout << i+S->split[rank] << "," << aggregate2[i] << "\t\t";
            }else
                iter += S->nnzPerRow_local[i];
        }
//        if(rank==0) std::cout << "1>>>>>>>>>>>>>>>>>10 root ==================== " << root_distance[10] << "\taggregate = " << aggregate[10] << std::endl;

        // todo: for distance-1 it is probably safe to remove this for loop, and change weight2 to weight and aggregate2 to aggregate at the end of the previous for loop.
        for (i = 0; i < size; ++i) {
            if( (S->nnzPerRow_local[i]!=0) && (weight[i]&UNDECIDED) && (root_distance[i]==1)) {
                weight[i] = (1UL << wOffset | weight2[i] & weightMax );
                aggregate[i] = aggregate2[i];
//                if(rank==0) std::cout << i+S->split[rank] << "\t" << aggregate[i] << "\t" << aggregate2[i] << std::endl;
            }
        }
//        if(rank==0) std::cout << "2>>>>>>>>>>>>>>>>>10 root ==================== " << root_distance[10] << "\taggregate = " << aggregate[10] << std::endl;

        //    if(rank==0){
        //        std::cout << std::endl << "after first max computation!" << std::endl;
        //        for (i = 0; i < size; ++i)
        //            std::cout << i << "\tweight = " << weight[i] << "\tindex = " << aggregate[i] << std::endl;
        //    }

        // ******************************* exchange remote max values for the second round of max computation *******************************

        // vSend[2*i]:   the first right 62 bits of vSend is maxPerCol for remote elements that should be sent to other processes.
        //               the first left 2 bits of vSend are aggStatus.
        // vSend[2*i+1]: the first right 62 bits of vSend is aggregate for remote elements that should be sent to other processes.
        //               the first left 2 bits are root_distance.
        //               root_distance is initialized to 3(11). root = 0 (00), 1-distance root = 1 (01), 2-distance root = 2 (10).

        // the following shows how the data are being stored in vecValues:
//        iter = 0;
//        if(rank==1)
//            for (i = 0; i < S->col_remote_size; ++i)
//                for (j = 0; j < S->nnz_col_remote[i]; ++j, ++iter){
//                    std::cout << "row:" << S->row_remote[iter]+S->split[rank] << "\tneighbor(col) = " << S->col_remote2[iter]
//                         << "\t weight of neighbor = "          << (S->vecValues[2*S->col_remote[iter]]&weightMax)
//                         << "\t\t status of neighbor = "        << (S->vecValues[2*S->col_remote[iter]]>>wOffset)
//                         << "\t root_distance of neighbor = " << (S->vecValues[2*S->col_remote[iter]+1]&weightMax)
//                         << "\t status of agg = "               << (S->vecValues[2*S->col_remote[iter]+1]>>wOffset)
//                         << std::endl;
//                }

//        if(rank==1) std::cout << std::endl << std::endl;
        for (i = 0; i < S->vIndexSize; i++){
//            S->vSend[i] = weight[(S->vIndex[i])];
            S->vSend[2*i] = weight[S->vIndex[i]];
            aggStatusTemp = (unsigned long)root_distance[S->vIndex[i]]; // this is root_distance of the neighbor's aggregate.
            S->vSend[2*i+1] = ( (aggStatusTemp<<wOffset) | (aggregate[S->vIndex[i]]&weightMax) );
//            if(rank==1) std::cout << "vsend: " << S->vIndex[i]+S->split[rank] << "\tw = " << (S->vSend[2*i]&weightMax) << "\tst = " << (S->vSend[2*i]>>wOffset) << "\tagg = " << (S->vSend[2*i+1]&weightMax) << "\t oneDis = " << (S->vSend[2*i+1]>>wOffset) << std::endl;
        }

        for (i = 0; i < S->numRecvProc; i++)
            MPI_Irecv(&S->vecValues[S->rdispls[S->recvProcRank[i]]], S->recvProcCount[i], MPI_UNSIGNED_LONG,
                      S->recvProcRank[i], 1, comm, &(requests[i]));

        for (i = 0; i < S->numSendProc; i++)
            MPI_Isend(&S->vSend[S->vdispls[S->sendProcRank[i]]], S->sendProcCount[i], MPI_UNSIGNED_LONG,
                      S->sendProcRank[i], 1, comm, &(requests[S->numRecvProc + i]));

        // ******************************* second round of max computation *******************************
        // "for" loop is of size "number of rows". it checks if a node is UNDECIDED and also if it does not have a root of distance one.
        // Roots of distance 1 have priority. Then, it goes over its neighbors, which are nonzeros on that row.
        // The neighbor should be UNDECIDED or assigned. Then, it should have a 1-distance root, because we are looking for a distance-2 root.
        // Finally, its weight should be higher than weightTemp, then than node will be chosen for root.
        // UNDECIDED is also considered because it may become have a 1-distance root later.
        // In the next "for" loop, weight and aggregate are updated, but not the status.

        // local part - distance-2 aggregate
        iter = 0;
        for (i = 0; i < size; ++i) {
//            if(i==34) std::cout << ">>>>>>>>>>>>>>>>>34 root ==================== " << root_distance[34] << "\taggregate = " << aggregate[34] << std::endl;
            if( (weight[i]&UNDECIDED) && root_distance[i]!=1) { // root_distance cannot be 2 or 0 here.
//                oneDistanceRoot[i] = false;
                aggregateTemp = aggregate[i];
                weightTemp    = weight[i];
//                aggStatusTemp = 1UL; // this will be used for aggStatus2, and aggStatus2 will be used for the remote part.
                for (j = 0; j < S->nnzPerRow_local[i]; ++j, ++iter) {
                    col_index = S->col_local[S->indicesP_local[iter]] - S->split[rank];
                    if( (weight[col_index]>>wOffset) <= 1 && root_distance[col_index]==1){
                        if( ((weight[col_index]&weightMax) > (weightTemp&weightMax)) ||
                                ( ((weight[col_index]&weightMax) == (weightTemp&weightMax)) && (aggregate[col_index] > i+S->split[rank]) ) ){

//                            if(rank==1 && i==2) std::cout << "??????????? " << col_index << "\tweight = " << (weight[col_index]&weightMax) << "\tagg = " << aggregate[col_index] << std::endl;
                            aggregateTemp = aggregate[col_index];
                            weightTemp    = weight[col_index];
                            root_distance[i] = 2;
                            if(weight[col_index]&UNDECIDED)
                                dist1or2undecided[i] = true;
                            else
                                dist1or2undecided[i] = false;

                        }
                    }
                }
                weight2[i]    = weightTemp;
                aggregate2[i] = aggregateTemp;
//                aggStatus2[i] = aggStatusTemp; // this is stored only to be compared with the remote one in the remote part.
            }else
                iter += S->nnzPerRow_local[i];
        }
//        if(rank==1) std::cout << "3>>>>>>>>>>>>>>>>>2 root ==================== " << root_distance[2] << "\taggregate = " << aggregate[2] << std::endl;

        for (i = 0; i < size; ++i) {
            if( (S->nnzPerRow_local[i]!=0) && (weight[i]&UNDECIDED) && (root_distance[i]==2) ) {
                aggregate[i] = aggregate2[i];
                aggStatusTemp = (weight[i]>>wOffset);
//                if (aggregate[i] < S->split[rank] || aggregate[i] >= S->split[rank+1]) // this is distance-2 to a remote root.
//                    aggStatusTemp = 0;
//                std::cout << "before: " << (weight[i]>>wOffset) << ",after: " << aggStatusTemp << std::endl;
                weight[i] = (aggStatusTemp<<wOffset | (weight2[i]&weightMax) );
            }
        }
//        if(rank==1) std::cout << "4>>>>>>>>>>>>>>>>>2 root ==================== " << root_distance[2] << "\taggregate = " << aggregate[2] << std::endl;

//        if(rank==1){
//            std::cout << std::endl << "after second max computation!" << std::endl;
//            for (i = 0; i < size; ++i)
//                std::cout << i << "\tweight = " << weight[i] << "\tindex = " << aggregate[i] << "\taggStatus = " << aggStatus[i] << std::endl;
//        }

        MPI_Waitall(S->numSendProc + S->numRecvProc, requests, statuses);

//        delete requests; // todo: delete requests and statuses in whole project, if it is required.
//        delete statuses;

//        MPI_Barrier(comm);
//        iter = 0;
//        if(rank==1)
//            for (i = 0; i < S->col_remote_size; ++i)
//                for (j = 0; j < S->nnz_col_remote[i]; ++j, ++iter){
//                    std::cout << "row:" << S->row_remote[iter]+S->split[rank] << "\tneighbor(col) = " << S->col_remote2[iter]
//                         << "\tweight of neighbor = "          << (S->vecValues[2*S->col_remote[iter]]&weightMax)
//                         << "\t\tstatus of neighbor = "        << (S->vecValues[2*S->col_remote[iter]]>>wOffset)
//                         << "\t root_distance of neighbor = "  << (S->vecValues[2*S->col_remote[iter]+1]&weightMax)
//                         << "\tstatus of agg = "               << (S->vecValues[2*S->col_remote[iter]+1]>>wOffset)
//                         << std::endl;
//                }
//        MPI_Barrier(comm);

        // remote part
        // store the max of rows of remote elements in weight2 and aggregate2.
        iter = 0;
        for (i = 0; i < S->col_remote_size; ++i) {
            for (j = 0; j < S->nnz_col_remote[i]; ++j, ++iter) {
                i_remote = S->row_remote[iter];
                j_remote = S->col_remote[iter];
                weight_remote = S->vecValues[2 * j_remote];
                agg_remote    = S->vecValues[2 * j_remote + 1];

                if (weight[i_remote] & UNDECIDED) {

                    //distance-1 aggregate
                    if (weight_remote & ROOT) {
                        weight[i_remote] = (0UL << wOffset | (weight_remote & weightMax));
//                        weight2[i_remote] = weight[i_remote];
                        aggregate[i_remote] = (agg_remote & weightMax);
                        root_distance[i_remote] = 1;
                        dist1or2undecided[i_remote] = false;
//                        if(rank==0) std::cout << i+S->split[rank] << "\t assigned to = " << aggregate[i_remote] << " distance-1 remote \t weight = " << weight[i_remote] << std::endl;
                    } else if (weight_remote & UNDECIDED) {

                        if (root_distance[i_remote] == 1)
                            weightTemp = (weight2[i_remote] & weightMax);
                        else{
                            weightTemp = initialWeight[i_remote];
                        }

                        if ( ( (weight_remote & weightMax) > weightTemp) ||
                                ( ( (weight_remote & weightMax) == weightTemp) && ( ((agg_remote & weightMax) > i_remote+S->split[rank]) )) ) {
//                            if(rank==1) std::cout << "first  before\t" << "row = " << S->row_remote[iter]+S->split[rank] << "  \tinitial weight = " << initialWeight[S->row_remote[iter]] << ",\taggregate = " << aggregate2[S->row_remote[iter]] << "\tremote weight = " << (weight2[S->row_remote[iter]]&weightMax) << "\tremote status = " << (S->vecValues[2*S->col_remote[iter]+1]>>wOffset) << "\taggStat2 = " << aggStatus2[S->row_remote[iter]] << std::endl;
                            weight2[i_remote] = (weight_remote & weightMax);
                            aggregate2[i_remote] = (agg_remote & weightMax);
                            root_distance[i_remote] = 1;
                            dist1or2undecided[i_remote] = true;
//                            aggStatus2[S->row_remote[iter]] = (S->vecValues[2 * S->col_remote[iter]] >> wOffset);
//                            if(rank==1) std::cout << "first  after \t" << "row = " << S->row_remote[iter]+S->split[rank] << "  \tinitial weight = " << initialWeight[S->row_remote[iter]] << ",\taggregate = " << aggregate2[S->row_remote[iter]] << "\t\tremote weight = " << (weight2[S->row_remote[iter]]&weightMax) << "\tremote status = " << (S->vecValues[2*S->col_remote[iter]+1]>>wOffset) << "\taggStat2 = " << aggStatus2[S->row_remote[iter]] << std::endl;
                        }
                    }

                    //distance-2 aggregate
                    if (weight_remote >> wOffset == 0) { // the neighbor is assigned (00).
                        if (root_distance[i_remote] != 1 && ((agg_remote >> wOffset) == 1) ){ // this is root_distance of the neighbor.

                            if( ( (weight_remote & weightMax) > (weight2[i_remote] & weightMax) ) ||
                                    (( (weight_remote & weightMax) == (weight2[i_remote] & weightMax) ) && (agg_remote & weightMax) > i_remote+S->split[rank] ) ){

                                weight2[i_remote] = (weight_remote & weightMax);
                                aggregate2[i_remote] = (agg_remote & weightMax);
//                            aggStatus2[S->row_remote[iter]] = (S->vecValues[2 * S->col_remote[iter]]>>wOffset); // this is always 0.
                                root_distance[i_remote] = 2;
                                dist1or2undecided[i_remote] = false;

                            }

                        }
                    } else if (weight_remote & UNDECIDED) { // the neighbor is UNDECIDED (01).
                        if (root_distance[i_remote] != 1 ) {

                            if( (weight_remote & weightMax) > (weight2[i_remote] & weightMax) ||
                                    ((weight_remote & weightMax) > (weight2[i_remote] & weightMax)) && (agg_remote & weightMax) > i_remote+S->split[rank] ){

                                weight2[i_remote] = (weight_remote & weightMax);
                                aggregate2[i_remote] = (agg_remote & weightMax);
//                            aggStatus2[S->row_remote[iter]] = (S->vecValues[2 * S->col_remote[iter]]>>wOffset); // this is always 0.
                                root_distance[i_remote] = 2;
                                dist1or2undecided[i_remote] = true;

                            }
                        }
                    }
                    // if the node is assigned and root_distance is 2, check to see if there is a root in distance-1
                } else if( (weight[i_remote]>>wOffset == 0) && (root_distance[i_remote] == 2) ){
                    if (weight_remote & ROOT){
                        weight[i_remote] = (0UL << wOffset | (weight_remote & weightMax));
                        aggregate[i_remote] = (agg_remote & weightMax);
                        root_distance[i_remote] = 1;
                    }
                }
            }
        }
//        if(rank==1) std::cout << "5>>>>>>>>>>>>>>>>>2 root ==================== " << root_distance[2] << "\taggregate = " << aggregate[2] << std::endl;

        // put weight2 in weight and aggregate2 in aggregate.
        // if a row does not have a remote element then (weight2[i]&weightMax) == (weight[i]&weightMax)
        // update aggStatus of remote elements at the same time
        for(i=0; i<size; i++){
            if( (weight[i]&UNDECIDED) && aggregate[i] != aggregate2[i] ){
                aggregate[i] = aggregate2[i];
                weight[i] = ( 1UL<<wOffset | weight2[i] );
//                if(aggStatus2[i] != 1) // if this is 1, it should go to the next aggregation round.
//                    weight[i] = (0UL<<wOffset | weight2[i]&weightMax);
            }
        }
//        if(rank==1) std::cout << "6>>>>>>>>>>>>>>>>>2 root ==================== " << root_distance[2] << "\taggregate = " << aggregate[2] << std::endl;

        // ******************************* Update Status *******************************
        // "for" loop is of size "number of rows". it checks if a node is UNDECIDED.
        // If aggregate of a node equals its index, that's a root.

//        if(rank==0) std::cout << "******************** Update Status ********************" << std::endl;
        for (i = 0; i < size; ++i) {
            if(weight[i]&UNDECIDED) {
//                if(rank==0) std::cout << "checking " << i << "\taggregate[i] = " << aggregate[i] << std::endl;
                // local
                if (aggregate[i] >= S->split[rank] && aggregate[i] < S->split[rank+1]) {
//                    if(rank==1) std::cout << "i = " << i << "\taggregate[i] = " << aggregate[i] << "\taggStatus[aggregate[i]] = " << aggStatus[aggregate[i]] << std::endl;
//                    if(rank==1) std::cout << "i = " << i+S->split[rank] << "\taggregate[i] = " << aggregate[i] << "\taggStatus[i] = " << (weight[i]>>wOffset) << std::endl;
                    if (aggregate[i] == i + S->split[rank]) { // this node is a root.
                        weight[i] = ( (2UL<<wOffset) | (weight[i]&weightMax) ); // change aggStatus of a root to 2.
                        root_distance[i] = 0;
                        aggArray.push_back(aggregate[i]);
//                        (*aggSize)++;
//                        if(rank==0) std::cout << "root " << "i = " << i+S->split[rank] << "\t weight = " << (weight[i]&weightMax) << std::endl;
                    } else if ( (root_distance[i] == 1 && (weight[aggregate[i] - S->split[rank]]&ROOT) && (!dist1or2undecided[i])) ||
                                (root_distance[i] == 2 && !dist1or2undecided[i]) ) {
//                        if(rank==0) std::cout << "assign " << "i = " << i+S->split[rank] << "\taggregate[i] = " << aggregate[i] << "\taggStatus[i] = " << (weight[i]>>wOffset) << std::endl;
                        weight[i] = ( (0UL<<wOffset) | (weight[i]&weightMax) );
//                        if(rank==0) std::cout << i+S->split[rank] << "\t assigned to = " << aggregate[i] << " distance-1 or 2 local final step, root_distance = " << root_distance[i] << "\t weight = " << weight[i]  << std::endl;
//                        if (root_distance[aggregate[i] - S->split[rank]] == 0) root_distance[i] = 1; // todo: this is WRONG!
                    }

                // remote
                }else{
//                    if(root_distance[i] == 1)
//                        continue;
                    if(root_distance[i] == 2 && !dist1or2undecided[i]){
                        weight[i] = (0UL<<wOffset | weight[i]&weightMax);
//                        if(rank==0) std::cout << i+S->split[rank] << "\t assigned to = " << aggregate[i] << " distance-2 remote final step" << std::endl;
                    }
//                    if(aggStatus2[i] != 1) // if it is 1, it should go to the next aggregation round.
//                        weight[i] = (0UL<<wOffset | weight2[i]&weightMax);
                }
            }
        }
//        if(rank==1) std::cout << "7>>>>>>>>>>>>>>>>>2 root ==================== " << root_distance[2] << "\taggregate = " << aggregate[2] << std::endl;

//        for(int k=0; k<nprocs; k++){
//            MPI_Barrier(comm);
//            if(rank==k){
//                std::cout << "final aggregate! rank:" << rank << ", iter = " << whileiter << std::endl;
//                for (i = 0; i < size; ++i){
//                    std::cout << "i = " << i+S->split[rank] << "\t\taggregate = " << aggregate[i] << "\t\taggStatus = "
//                         << (weight[i]>>wOffset) << "\t\tinitial weight = " << initialWeight[i]
//                         << "\t\tcurrent weight = " << (weight[i]&weightMax) << "\t\taggStat2 = " << aggStatus2[i]
//                         << "\t\toneDistanceRoot = " << oneDistanceRoot[i] << std::endl;
//                }
//            }
//            MPI_Barrier(comm);
//        }

        // todo: merge this loop with the previous one.
        continueAggLocal = false;
        for (i = 0; i < size; ++i) {
            // if any un-assigned node is available, continue aggregating.
            if(weight[i]&UNDECIDED) {
                continueAggLocal = true;
                break;
            }
        }

//        whileiter++;
//        if(whileiter==15) continueAggLocal = false;

        // check if every processor does not have any non-assigned node, otherwise all the processors should continue aggregating.
        MPI_Allreduce(&continueAggLocal, &continueAgg, 1, MPI_CXX_BOOL, MPI_LOR, comm);
//        MPI_Barrier(comm);
//        std::cout << rank << "\tcontinueAgg = " << continueAgg << std::endl;
//        MPI_Barrier(comm);

//        MPI_Barrier(comm); if(rank==0) std::cout << "UNDECIDED: " << whileiter << std::endl; MPI_Barrier(comm);
        if(continueAgg){
            for (i = 0; i < size; ++i) {
//                aggStatus2[i] = 1;
                if(weight[i]&UNDECIDED){
//                    printf("%lu\n", i+S->split[rank]);
                    weight[i] = ( 1UL<<wOffset | initialWeight[i] );
                    aggregate[i] = i+S->split[rank];
                }
            }
        }

    } //while(continueAgg)

//    MPI_Barrier(comm);
//    if(rank==nprocs-1) std::cout << "number of loops to find aggregation: " << whileiter << std::endl;
//    MPI_Barrier(comm);

//    for(i=0; i<size;i++)
//        if(rank==0) std::cout << "V[" << i+S->split[rank] << "] = " << initialWeight[i] << ";" << std::endl;

    free(root_distance);
    free(dist1or2undecided);

    // *************************** update aggregate to new indices ****************************

//    if(rank==2)
//        std::cout << std::endl << "S.M = " << S->M << ", S.nnz_l = " << S->nnz_l << ", S.nnz_l_local = " << S->nnz_l_local
//             << ", S.nnz_l_remote = " << S->nnz_l_remote << std::endl << std::endl;

//    if(rank==1){
//        std::cout << "aggregate:" << std::endl;
//        for(i=0; i<size; i++)
//            std::cout << i+S->split[rank] << "\t" << aggregate[i] << std::endl;
//        std::cout << std::endl;}

    // ************* write the aggregate values of all the nodes to a file *************

    // use this command to concatenate the output files:
    // cat aggregateSaena0.txt aggregateSaena1.txt > aggregateSaena.txt
//    for(i=0; i<aggregate.size(); i++)
//        aggregate[i]++;
//    writeVectorToFileul(aggregate, S->Mbig, "aggregateSaena", comm);
//    for(i=0; i<aggregate.size(); i++)
//        aggregate[i]--;

    // aggArray is the set of root nodes.
    sort(aggArray.begin(), aggArray.end());

//    if(rank==1){
//        std::cout << "aggArray:" << aggArray.size() << std::endl;
//        for(auto i:aggArray)
//            std::cout << i << std::endl;
//        std::cout << std::endl;}

    // ************* write the aggregate nodes to a file *************

    // use this command to concatenate the output files:
    // cat aggArraySaena0.txt aggArraySaena1.txt > aggArraySaena.txt
//    unsigned long aggArray_size, aggArray_size_total;
//    aggArray_size = aggArray.size();
//    MPI_Allreduce(&aggArray_size, &aggArray_size_total, 1, MPI_UNSIGNED_LONG, MPI_SUM, comm);
//    for(i=0; i<aggArray.size(); i++)
//        aggArray[i]++;
//    writeVectorToFileul(aggArray, aggArray_size_total, "aggArraySaena", comm);
//    for(i=0; i<aggArray.size(); i++)
//        aggArray[i]--;

    splitNew.resize(nprocs+1);
    fill(splitNew.begin(), splitNew.end(), 0);
    splitNew[rank] = aggArray.size();

    unsigned long* splitNewTemp = (unsigned long*)malloc(sizeof(unsigned long)*nprocs);
    MPI_Allreduce(&splitNew[0], splitNewTemp, nprocs, MPI_UNSIGNED_LONG, MPI_SUM, comm);

    // do scan on splitNew
    splitNew[0] = 0;
    for(i=1; i<nprocs+1; i++)
        splitNew[i] = splitNew[i-1] + splitNewTemp[i-1];

    free(splitNewTemp);

//    if(rank==0){
//        std::cout << "splitNew:" << std::endl;
//        for(i=0; i<nprocs+1; i++)
//            std::cout << S->split[i] << "\t" << splitNew[i] << std::endl;
//        std::cout << std::endl;}

    unsigned long procNum;
    std::vector<unsigned long> aggregateRemote;
    std::vector<unsigned long> recvProc;
    int* recvCount = (int*)malloc(sizeof(int)*nprocs);
    std::fill(recvCount, recvCount + nprocs, 0);

//    if(rank==1) std::cout << std::endl;
    bool* isAggRemote = (bool*)malloc(sizeof(bool)*size);
    // local: aggregate update to new values.
    for(i=0; i<size; i++){
        if(aggregate[i] >= S->split[rank] && aggregate[i] < S->split[rank+1]){
            aggregate[i] = lower_bound2(&*aggArray.begin(), &*aggArray.end(), aggregate[i]) + splitNew[rank];
//            if(rank==1) std::cout << aggregate[i] << std::endl;
            isAggRemote[i] = false;
        }else{
            isAggRemote[i] = true;
            aggregateRemote.push_back(aggregate[i]);
        }
    }

//    set<unsigned long> aggregateRemote2(aggregateRemote.begin(), aggregateRemote.end());
//    if(rank==1) std::cout << "i and procNum:" << std::endl;
//    for(auto i:aggregateRemote2){
//        procNum = lower_bound2(&S->split[0], &S->split[nprocs+1], i);
//        if(rank==1) std::cout << i << "\t" << procNum << std::endl;
//        recvCount[procNum]++;
//    }

    sort(aggregateRemote.begin(), aggregateRemote.end());
    auto last = unique(aggregateRemote.begin(), aggregateRemote.end());
    aggregateRemote.erase(last, aggregateRemote.end());
//    if(rank==1) std::cout << "i and procNum:" << std::endl;
    for(auto i:aggregateRemote){
        procNum = lower_bound2(&S->split[0], &S->split[nprocs], i);
        recvCount[procNum]++;
//        if(rank==1) std::cout << i << "\t" << procNum << std::endl;
    }

    int* vIndexCount = (int*)malloc(sizeof(int)*nprocs);
    MPI_Alltoall(recvCount, 1, MPI_INT, vIndexCount, 1, MPI_INT, comm);

//    if(rank==0){
//        std::cout << "vIndexCount:\t" << rank << std::endl;
//        for(i=0; i<nprocs; i++)
//            std::cout << vIndexCount[i] << std::endl;
//    }

    // this part is for isend and ireceive.
    std::vector<int> recvProcRank;
    std::vector<int> recvProcCount;
    std::vector<int> sendProcRank;
    std::vector<int> sendProcCount;
    int numRecvProc = 0;
    int numSendProc = 0;
    for(int i=0; i<nprocs; i++){
        if(recvCount[i]!=0){
            numRecvProc++;
            recvProcRank.push_back(i);
            recvProcCount.push_back(recvCount[i]);
        }
        if(vIndexCount[i]!=0){
            numSendProc++;
            sendProcRank.push_back(i);
            sendProcCount.push_back(vIndexCount[i]);
        }
    }

    std::vector<int> vdispls;
    std::vector<int> rdispls;
    vdispls.resize(nprocs);
    rdispls.resize(nprocs);
    vdispls[0] = 0;
    rdispls[0] = 0;

    for (int i=1; i<nprocs; i++){
        vdispls[i] = vdispls[i-1] + vIndexCount[i-1];
        rdispls[i] = rdispls[i-1] + recvCount[i-1];
    }
    int vIndexSize = vdispls[nprocs-1] + vIndexCount[nprocs-1];
    int recvSize   = rdispls[nprocs-1] + recvCount[nprocs-1];

    unsigned long* vIndex = (unsigned long*)malloc(sizeof(unsigned long)*vIndexSize); // indices to be sent. And aggregateRemote are indices to be received.
    MPI_Alltoallv(&*aggregateRemote.begin(), recvCount, &*rdispls.begin(), MPI_UNSIGNED_LONG, vIndex, vIndexCount, &*vdispls.begin(), MPI_UNSIGNED_LONG, comm);
//    MPI_Alltoallv(&*aggregateRemote2.begin(), recvCount, &*rdispls.begin(), MPI_UNSIGNED_LONG, vIndex, vIndexCount, &*vdispls.begin(), MPI_UNSIGNED_LONG, comm);

    unsigned long* aggSend = (unsigned long*)malloc(sizeof(unsigned long*) * vIndexSize);
    unsigned long* aggRecv = (unsigned long*)malloc(sizeof(unsigned long*) * recvSize);

//    if(rank==0) std::cout << std::endl << "vSend:\trank:" << rank << std::endl;
    for(long i=0;i<vIndexSize;i++){
        aggSend[i] = aggregate[( vIndex[i]-S->split[rank] )];
//        if(rank==0) std::cout << "vIndex = " << vIndex[i] << "\taggSend = " << aggSend[i] << std::endl;
    }

    // replace this alltoallv with isend and irecv.
//    MPI_Alltoallv(aggSend, vIndexCount, &*(vdispls.begin()), MPI_UNSIGNED_LONG, aggRecv, recvCount, &*(rdispls.begin()), MPI_UNSIGNED_LONG, comm);

    MPI_Request *requests2 = new MPI_Request[numSendProc + numRecvProc];
    MPI_Status  *statuses2 = new MPI_Status[numSendProc + numRecvProc];

    for(int i = 0; i < numRecvProc; i++)
        MPI_Irecv(&aggRecv[rdispls[recvProcRank[i]]], recvProcCount[i], MPI_UNSIGNED_LONG, recvProcRank[i], 1, comm, &(requests2[i]));

    //Next send the messages. Do not send to self.
    for(int i = 0; i < numSendProc; i++)
        MPI_Isend(&aggSend[vdispls[sendProcRank[i]]], sendProcCount[i], MPI_UNSIGNED_LONG, sendProcRank[i], 1, comm, &(requests2[numRecvProc+i]));

    MPI_Waitall(numSendProc+numRecvProc, requests2, statuses2);

//    if(rank==1) std::cout << "aggRemote received:" << std::endl;
//    set<unsigned long>::iterator it;
//    for(i=0; i<size; i++){
//        if(isAggRemote[i]){
//            it = aggregateRemote2.find(aggregate[i]);
//            if(rank==1) std::cout << aggRecv[ distance(aggregateRemote2.begin(), it) ] << std::endl;
//            aggregate[i] = aggRecv[ distance(aggregateRemote2.begin(), it) ];
//        }
//    }
//    if(rank==1) std::cout << std::endl;

//    if(rank==1) std::cout << "aggRemote received:" << std::endl;
    // remote
    for(i=0; i<size; i++){
        if(isAggRemote[i]){
            aggregate[i] = aggRecv[ lower_bound2(&*aggregateRemote.begin(), &*aggregateRemote.end(), aggregate[i]) ];
//            if(rank==1) std::cout << i << "\t" << aggRecv[ lower_bound2(&*aggregateRemote.begin(), &*aggregateRemote.end(), aggregate[i]) ] << std::endl;
        }
    }
//    if(rank==1) std::cout << std::endl;

//    set<unsigned long> aggArray2(&aggregate[0], &aggregate[size]);
//    if(rank==1){
//        std::cout << "aggArray2:" << std::endl;
//        for(auto i:aggArray2)
//            std::cout << i << std::endl;
//        std::cout << std::endl;
//    }
    // update aggregate to new indices
//    set<unsigned long>::iterator it;
//    for(i=0; i<size; i++){
//        it = aggArray.find(aggregate[i]);
//        aggregate[i] = distance(aggArray.begin(), it) + splitNew[rank];
//    }

    free(aggSend);
    free(aggRecv);
    free(isAggRemote);
    free(recvCount);
    free(vIndexCount);
    free(vIndex);
    return 0;
} // end of SaenaObject::Aggregation


// Decoupled Aggregation - not complete
/*
int SaenaObject::Aggregation(CSRMatrix* S){
    // At the end just set P here, which is in SaenaObject.

    std::vector<long> P(S->M);
    P.assign(S->M,-1);
    long nc = -1;
    bool isInN1[S->M];
    bool aggregated;
    unsigned int i;
    double tau_sbar = tau * S->average_sparsity;
    for(i=0; i<S->M; i++){
        if( S->rowIndex[i+1] - S->rowIndex[i] <= tau_sbar ){
            isInN1[1] = true;
        }
    }

    // ************************************* first pass *************************************

    for (i=0; i<S->M; i++){
        aggregated = true;
        if(isInN1[i] == false)
            continue;

        for(long j=S->rowIndex[i]; j<S->rowIndex[i+1]; j++){
            if(P[S->col[j]] == -1)
                break;
            aggregated = false;
        }

        if(aggregated==false) {
            nc++;
            for (long j = S->rowIndex[i]; j < S->rowIndex[i+1]; j++) {
                if(isInN1[S->col[i]] == true){
                    P[S->col[i]] = nc;
                }
            }
        }
    }

    // ************************************* second pass *************************************

    for (i=0; i<S->M; i++){
        aggregated = true;
        if(isInN1[i] == true)
            continue;

        for(long j=S->rowIndex[i]; j<S->rowIndex[i+1]; j++){
            if(P[S->col[j]] == -1)
                break;
            aggregated = false;
        }

        if(aggregated==false) {
            nc++;
            for (long j = S->rowIndex[i]; j < S->rowIndex[i+1]; j++) {
                P[S->col[i]] = nc;
            }
        }
    }

    // ************************************* third pass *************************************

    nc++;

    return 0;
};
 */


int saena_object::create_prolongation(saena_matrix* A, std::vector<unsigned long>& aggregate, prolong_matrix* P){
    // formula for the prolongation matrix from Irad Yavneh's paper:
    // P = (I - 4/(3*rhoDA) * DA) * P_t

    // todo: check when you should update new aggregate values: before creating prolongation or after.

    // Here P is computed: P = A_w * P_t; in which P_t is aggregate, and A_w = I - w*Q*A, Q is inverse of diagonal of A.
    // Here A_w is computed on the fly, while adding values to P. Diagonal entries of A_w are 0, so they are skipped.
    // todo: think about A_F which is A filtered.
    // todo: think about smoothing preconditioners other than damped jacobi. check the following paper:
    // todo: Eran Treister and Irad Yavneh, Non-Galerkin Multigrid based on Sparsified Smoothed Aggregation. page22.

    P->comm = A->comm;
    MPI_Comm comm = A->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);
    unsigned int i, j;
    float omega = 0.67; // todo: receive omega as user input. it is usually 2/3 for 2d and 6/7 for 3d.

    P->Mbig = A->Mbig;
    P->Nbig = P->splitNew[nprocs]; // This is the number of aggregates, which is the number of columns of P.
    P->M = A->M;

    // store remote elements from aggregate in vSend to be sent to other processes.
    // todo: is it ok to use vSend instead of vSendULong? vSend is double and vSendULong is unsigned long.
    // todo: the same question for vecValues and Isend and Ireceive.
    for(i=0; i<A->vIndexSize; i++){
        A->vSendULong[i] = aggregate[( A->vIndex[i] )];
//        if(rank==1) std::cout <<  A->vIndex[i] << "\t" << A->vSend[i] << std::endl;
    }

    MPI_Request* requests = new MPI_Request[A->numSendProc+A->numRecvProc];
    MPI_Status*  statuses = new MPI_Status[A->numSendProc+A->numRecvProc];

    for(i = 0; i < A->numRecvProc; i++)
        MPI_Irecv(&A->vecValuesULong[A->rdispls[A->recvProcRank[i]]], A->recvProcCount[i], MPI_UNSIGNED_LONG, A->recvProcRank[i], 1, comm, &(requests[i]));

    for(i = 0; i < A->numSendProc; i++)
        MPI_Isend(&A->vSendULong[A->vdispls[A->sendProcRank[i]]], A->sendProcCount[i], MPI_UNSIGNED_LONG, A->sendProcRank[i], 1, comm, &(requests[A->numRecvProc+i]));

    std::vector<cooEntry> PEntryTemp;

    // P = (I - 4/(3*rhoDA) * DA) * P_t
    // aggreagte is used as P_t in the following "for" loop.
    // local
    long iter = 0;
    for (i = 0; i < A->M; ++i) {
        for (j = 0; j < A->nnzPerRow_local[i]; ++j, ++iter) {
            if(A->row_local[A->indicesP_local[iter]] == A->col_local[A->indicesP_local[iter]]-A->split[rank]){ // diagonal element
                PEntryTemp.push_back(cooEntry(A->row_local[A->indicesP_local[iter]],
                                              aggregate[ A->col_local[A->indicesP_local[iter]] - A->split[rank] ],
                                              1 - omega));
            }else{
                PEntryTemp.push_back(cooEntry(A->row_local[A->indicesP_local[iter]],
                                              aggregate[ A->col_local[A->indicesP_local[iter]] - A->split[rank] ],
                                              -omega * A->values_local[A->indicesP_local[iter]] * A->invDiag[A->row_local[A->indicesP_local[iter]]]));
            }
//            if(rank==1) std::cout << A->row_local[A->indicesP_local[iter]] << "\t" << aggregate[A->col_local[A->indicesP_local[iter]] - A->split[rank]] << "\t" << A->values_local[A->indicesP_local[iter]] * A->invDiag[A->row_local[A->indicesP_local[iter]]] << std::endl;
        }
    }

    MPI_Waitall(A->numSendProc+A->numRecvProc, requests, statuses);

    // remote
    iter = 0;
    for (i = 0; i < A->col_remote_size; ++i) {
        for (j = 0; j < A->nnzPerCol_remote[i]; ++j, ++iter) {
            PEntryTemp.push_back(cooEntry(A->row_remote[iter],
                                          A->vecValuesULong[A->col_remote[iter]],
                                          -omega * A->values_remote[iter] * A->invDiag[A->row_remote[iter]]));
//            P->values.push_back(A->values_remote[iter]);
//            if(rank==1) std::cout << A->row_remote[iter] << "\t" << A->vecValuesULong[A->col_remote[iter]] << "\t" << A->values_remote[iter] * A->invDiag[A->row_remote[iter]] << std::endl;
        }
    }

    std::sort(PEntryTemp.begin(), PEntryTemp.end());

//    if(rank==1)
//        for(i=0; i<PEntryTemp.size(); i++)
//            std::cout << PEntryTemp[i].row << "\t" << PEntryTemp[i].col << "\t" << PEntryTemp[i].val << std::endl;

    // remove duplicates.
    for(i=0; i<PEntryTemp.size(); i++){
        P->entry.push_back(PEntryTemp[i]);
        while(i<PEntryTemp.size()-1 && PEntryTemp[i] == PEntryTemp[i+1]){ // values of entries with the same row and col should be added.
            P->entry.back().val += PEntryTemp[i+1].val;
            i++;
        }
    }

//    if(rank==1)
//        for(i=0; i<P->entry.size(); i++)
//            std::cout << P->entry[i].row << "\t" << P->entry[i].col << "\t" << P->entry[i].val << std::endl;

    PEntryTemp.clear();

    P->nnz_l = P->entry.size();
    MPI_Allreduce(&P->nnz_l, &P->nnz_g, 1, MPI_UNSIGNED_LONG, MPI_SUM, comm);

    P->split = A->split;

    P->findLocalRemote(&*P->entry.begin());
//    P->findLocalRemote(&*P->row.begin(), &*P->col.begin(), &*P->values.begin(), comm);

    return 0;
}// end of SaenaObject::createProlongation


int saena_object::coarsen(saena_matrix* A, prolong_matrix* P, restrict_matrix* R, saena_matrix* Ac){

    // todo: to improve the performance of this function, consider using the arrays used for RA also for RAP.
    // todo: this way allocating and freeing memory will be halved.

    MPI_Comm comm = P->comm;

    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    unsigned long i, j;
    prolong_matrix RA_temp(comm); // RA_temp is being used to remove duplicates while pushing back to RA.

    // ************************************* RA_temp - A local *************************************
    // Some local and remote elements of RA_temp are computed here using local R and local A.

    unsigned int AMaxNnz, AMaxM;
    MPI_Allreduce(&A->nnz_l, &AMaxNnz, 1, MPI_UNSIGNED, MPI_MAX, comm);
    MPI_Allreduce(&A->M, &AMaxM, 1, MPI_UNSIGNED, MPI_MAX, comm);
//    MPI_Barrier(comm); printf("rank=%d, AMaxNnz=%d \n", rank, AMaxNnz); MPI_Barrier(comm);
    // todo: is this way better than using the previous Allreduce? reduce on processor 0, then broadcast to other processors.

    unsigned int* AnnzPerRow = (unsigned int*)malloc(sizeof(unsigned int)*AMaxM);
    std::fill(&AnnzPerRow[0], &AnnzPerRow[AMaxM], 0);
    for(i=0; i<A->nnz_l; i++)
        AnnzPerRow[A->entry[i].row - A->split[rank]]++;

//    if(rank==0)
//        for(i=0; i<A->M; i++)
//            std::cout << i << "\t" << AnnzPerRow[i] << std::endl;

    unsigned int* AnnzPerRowScan = (unsigned int*)malloc(sizeof(unsigned int)*(AMaxM+1));
    AnnzPerRowScan[0] = 0;
    for(i=0; i<A->M; i++){
        AnnzPerRowScan[i+1] = AnnzPerRowScan[i] + AnnzPerRow[i];
//        if(rank==0) printf("i=%lu, AnnzPerRow=%d, AnnzPerRowScan = %d\n", i, AnnzPerRow[i], AnnzPerRowScan[i]);
    }

    // todo: combine indicesP and indicesPRecv together.
    // find row-wise ordering for A and save it in indicesP
    unsigned long* indicesP = (unsigned long*)malloc(sizeof(unsigned long)*A->nnz_l);
    for(unsigned long i=0; i<A->nnz_l; i++)
        indicesP[i] = i;
    std::sort(indicesP, &indicesP[A->nnz_l], sort_indices2(&*A->entry.begin()));

    for(i=0; i<R->nnz_l_local; i++){
        for(j = AnnzPerRowScan[R->entry_local[i].col - P->split[rank]]; j < AnnzPerRowScan[R->entry_local[i].col - P->split[rank] + 1]; j++){
//            if(rank==0) std::cout << A->entry[indicesP[j]].row << "\t" << A->entry[indicesP[j]].col << "\t" << A->entry[indicesP[j]].val
//                             << "\t" << R->entry_local[i].col << "\t" << R->entry_local[i].col - P->split[rank] << std::endl;
            RA_temp.entry.push_back(cooEntry(R->entry_local[i].row,
                                        A->entry[indicesP[j]].col,
                                        R->entry_local[i].val * A->entry[indicesP[j]].val));
        }
    }

//    if(rank==1)
//        for(i=0; i<RA_temp.entry.size(); i++)
//            std::cout << RA_temp.entry[i].row << "\t" << RA_temp.entry[i].col << "\t" << RA_temp.entry[i].val << std::endl;

    free(indicesP);

    // ************************************* RA_temp - A remote *************************************

    // find the start and end of each block of R.
    unsigned int* RBlockStart = (unsigned int*)malloc(sizeof(unsigned int)*(nprocs+1));
    std::fill(RBlockStart, &RBlockStart[nprocs+1], 0);

//    MPI_Barrier(comm); printf("rank=%d here!!!!!!!! \n", rank); MPI_Barrier(comm);
//    MPI_Barrier(comm); printf("rank=%d entry = %ld \n", rank, R->entry_remote[0].col); MPI_Barrier(comm);
    long procNum = -1;
    if(R->entry_remote.size() > 0)
        procNum = lower_bound2(&*A->split.begin(), &*A->split.end(), R->entry_remote[0].col);
//    MPI_Barrier(comm); printf("rank=%d procNum = %ld \n", rank, procNum); MPI_Barrier(comm);

    unsigned int nnzIter = 1;
    for(i=1; i<R->entry_remote.size(); i++){
        nnzIter++;
        if(R->entry_remote[i].col >= A->split[procNum+1]){
            RBlockStart[procNum+1] = nnzIter-1;
            procNum = lower_bound2(&*A->split.begin(), &*A->split.end(), R->entry_remote[i].col);
        }
//        if(rank==2) std::cout << "procNum = " << procNum << "\tcol = " << R->entry_remote[i].col << "\tnnzIter = " << nnzIter << std::endl;
    }
    RBlockStart[rank+1] = RBlockStart[rank]; // there is not any nonzero of R_remote on the local processor.
    std::fill(&RBlockStart[procNum+1], &RBlockStart[nprocs+1], nnzIter);

//    if(rank==1){
//        std::cout << "RBlockStart: " << std::endl;
//        for(i=0; i<nprocs+1; i++)
//            std::cout << RBlockStart[i] << std::endl;}

    unsigned long* indicesPRecv = (unsigned long*)malloc(sizeof(unsigned long)*AMaxNnz);

    //    printf("rank=%d A.nnz=%u \n", rank, A->nnz_l);
    cooEntry* Arecv = (cooEntry*)malloc(sizeof(cooEntry)*AMaxNnz);
    int left, right;
    unsigned int nnzRecv;
    long ARecvM;
    MPI_Status sendRecvStatus;

    // todo: change the algorithm so every processor sends data only to the next one and receives from the previous one in each iteration.
    for(int i = 1; i < nprocs; i++) {
        // send A to the right processor, recieve A from the left processor. "left" decreases by one in each iteration. "right" increases by one.
        right = (rank + i) % nprocs;
        left = rank - i;
        if (left < 0)
            left += nprocs;

        // *************************** RA_temp - A remote - sendrecv(size) ****************************

        // use sender rank for send and receive tags.
        MPI_Sendrecv(&A->nnz_l, 1, MPI_UNSIGNED, right, rank, &nnzRecv, 1, MPI_UNSIGNED, left, left, comm, &sendRecvStatus);
//        int MPI_Sendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype, int dest, int sendtag, void *recvbuf,
//                         int recvcount, MPI_Datatype recvtype, int source, int recvtag, MPI_Comm comm, MPI_Status *status)
//        if(rank==1) printf("i=%d, rank=%d, left=%d, right=%d \n", i, rank, left, right);

//        if(rank==2) std::cout << "own A->nnz_l = " << A->nnz_l << "\tnnzRecv = " << nnzRecv << std::endl;

        // *************************** RA_temp - A remote - sendrecv(A) ****************************

        // use sender rank for send and receive tags.
        MPI_Sendrecv(&A->entry[0], A->nnz_l, cooEntry::mpi_datatype(), right, rank, Arecv, nnzRecv, cooEntry::mpi_datatype(), left, left, comm, &sendRecvStatus);
//        if(rank==1) for(int j=0; j<nnzRecv; j++)
//                        printf("j=%d \t %lu \t %lu \t %f \n", j, Arecv[j].row, Arecv[j].col, Arecv[j].val);

        // *************************** RA_temp - A remote - multiplication ****************************

        ARecvM = A->split[left+1] - A->split[left];
        std::fill(&AnnzPerRow[0], &AnnzPerRow[ARecvM], 0);
        for(j=0; j<nnzRecv; j++){
            AnnzPerRow[Arecv[j].row - A->split[left]]++;
        }

//      if(rank==2)
//          for(i=0; i<A->M; i++)
//              std::cout << AnnzPerRow[i] << std::endl;

        AnnzPerRowScan[0] = 0;
        for(j=0; j<ARecvM; j++){
            AnnzPerRowScan[j+1] = AnnzPerRowScan[j] + AnnzPerRow[j];
//            if(rank==2) printf("i=%d, AnnzPerRow=%d, AnnzPerRowScan = %d\n", i, AnnzPerRow[i], AnnzPerRowScan[i]);
        }

        // find row-wise ordering for Arecv and save it in indicesPRecv
        for(unsigned long i=0; i<nnzRecv; i++)
            indicesPRecv[i] = i;
        std::sort(indicesPRecv, &indicesPRecv[nnzRecv], sort_indices2(Arecv));

//        if(rank==1) std::cout << "block start = " << RBlockStart[left] << "\tend = " << RBlockStart[left+1] << "\tleft rank = " << left << "\t i = " << i << std::endl;
        for(j=RBlockStart[left]; j<RBlockStart[left+1]; j++){
//            if(rank==1) std::cout << "col = " << R->entry_remote[j].col << "\tcol-split = " << R->entry_remote[j].col - P->split[left] << "\tstart = " << AnnzPerRowScan[R->entry_remote[j].col - P->split[left]] << "\tend = " << AnnzPerRowScan[R->entry_remote[j].col - P->split[left] + 1] << std::endl;
            for(unsigned long k = AnnzPerRowScan[R->entry_remote[j].col - P->split[left]]; k < AnnzPerRowScan[R->entry_remote[j].col - P->split[left] + 1]; k++){
//                if(rank==0) std::cout << Arecv[indicesPRecv[k]].row << "\t" << Arecv[indicesPRecv[k]].col << "\t" << Arecv[indicesPRecv[k]].val << std::endl;
                RA_temp.entry.push_back(cooEntry(R->entry_remote[j].row,
                                            Arecv[indicesPRecv[k]].col,
                                            R->entry_remote[j].val * Arecv[indicesPRecv[k]].val));
            }
        }


    } //for i
//    MPI_Barrier(comm); printf("rank=%d here!!!!!!!! \n", rank); MPI_Barrier(comm);

    free(indicesPRecv);
    free(AnnzPerRow);
    free(AnnzPerRowScan);
    free(Arecv);
    free(RBlockStart);

    // todo: check this: since entries of RA_temp with these row indices only exist on this processor,
    // todo: duplicates happen only on this processor, so sorting should be done locally.
    std::sort(RA_temp.entry.begin(), RA_temp.entry.end());

//    if(rank==1)
//        for(j=0; j<RA_temp.entry.size(); j++)
//            std::cout << RA_temp.entry[j].row << "\t" << RA_temp.entry[j].col << "\t" << RA_temp.entry[j].val << std::endl;

    prolong_matrix RA(comm);

    // remove duplicates.
    for(i=0; i<RA_temp.entry.size(); i++){
        RA.entry.push_back(RA_temp.entry[i]);
//        if(rank==1) std::cout << std::endl << "start:" << std::endl << RA_temp.entry[i].val << std::endl;
        while(i<RA_temp.entry.size()-1 && RA_temp.entry[i] == RA_temp.entry[i+1]){ // values of entries with the same row and col should be added.
            RA.entry.back().val += RA_temp.entry[i+1].val;
            i++;
//            if(rank==1) std::cout << RA_temp.entry[i+1].val << std::endl;
        }
//        if(rank==1) std::cout << std::endl << "final: " << std::endl << RA.entry[RA.entry.size()-1].val << std::endl;
        // todo: pruning. don't hard code tol. does this make the matrix non-symmetric?
//        if( abs(RA.entry.back().val) < 1e-6)
//            RA.entry.pop_back();
//        if(rank==1) std::cout << "final: " << std::endl << RA.entry.back().val << std::endl;
    }

//    if(rank==1)
//        for(j=0; j<RA.entry.size(); j++)
//            std::cout << RA.entry[j].row << "\t" << RA.entry[j].col << "\t" << RA.entry[j].val << std::endl;

    // ************************************* RAP_temp - P local *************************************
    // Some local and remote elements of RAP_temp are computed here.

    prolong_matrix RAP_temp(comm); // RAP_temp is being used to remove duplicates while pushing back to RAP.
    unsigned int P_max_M;
    MPI_Allreduce(&P->M, &P_max_M, 1, MPI_UNSIGNED, MPI_MAX, comm);
//    MPI_Barrier(comm); printf("rank=%d, PMaxNnz=%d \n", rank, PMaxNnz); MPI_Barrier(comm);
    // todo: is this way better than using the previous Allreduce? reduce on processor 0, then broadcast to other processors.

    unsigned int* PnnzPerRow = (unsigned int*)malloc(sizeof(unsigned int)*P_max_M);
    std::fill(&PnnzPerRow[0], &PnnzPerRow[P->M], 0);
    for(i=0; i<P->nnz_l; i++){
        PnnzPerRow[P->entry[i].row]++;
    }

//    if(rank==1)
//        for(i=0; i<P->M; i++)
//            std::cout << PnnzPerRow[i] << std::endl;

    unsigned int* PnnzPerRowScan = (unsigned int*)malloc(sizeof(unsigned int)*(P_max_M+1));
    PnnzPerRowScan[0] = 0;
    for(i = 0; i < P->M; i++){
        PnnzPerRowScan[i+1] = PnnzPerRowScan[i] + PnnzPerRow[i];
//        if(rank==2) printf("i=%lu, PnnzPerRow=%d, PnnzPerRowScan = %d\n", i, PnnzPerRow[i], PnnzPerRowScan[i]);
    }

    // find the start and end of each block of R.
    unsigned int* RABlockStart = (unsigned int*)malloc(sizeof(unsigned int)*(nprocs+1));
    std::fill(RABlockStart, &RABlockStart[nprocs+1], 0);
    procNum = lower_bound2(&P->split[0], &P->split[nprocs], RA.entry[0].col);
    nnzIter = 1;
    for(i=1; i<RA.entry.size(); i++){
        nnzIter++;
        if(RA.entry[i].col >= P->split[procNum+1]){
            RABlockStart[procNum+1] = nnzIter-1;
            procNum = lower_bound2(&P->split[0], &P->split[nprocs], RA.entry[i].col);
        }
//        if(rank==2) std::cout << "procNum = " << procNum << "\tcol = " << R->entry_remote[i].col << "\tnnzIter = " << nnzIter << std::endl;
    }
//    RABlockStart[rank+1] = RABlockStart[rank]; // there is not any nonzero of R_remote on the local processor.
    std::fill(&RABlockStart[procNum+1], &RABlockStart[nprocs+1], nnzIter);

//    if(rank==1){
//        std::cout << "RABlockStart: " << std::endl;
//        for(i=0; i<nprocs+1; i++)
//            std::cout << RABlockStart[i] << std::endl;}

    // todo: combine indicesP_Prolong and indicesP_ProlongRecv together.
    // find row-wise ordering for A and save it in indicesP
    unsigned long* indicesP_Prolong = (unsigned long*)malloc(sizeof(unsigned long)*P->nnz_l);
    for(unsigned long i=0; i<P->nnz_l; i++)
        indicesP_Prolong[i] = i;
    std::sort(indicesP_Prolong, &indicesP_Prolong[P->nnz_l], sort_indices2(&*P->entry.begin()));

//    MPI_Barrier(comm); std::cout << "here111111111111111!!!!!!!" << std::endl; MPI_Barrier(comm);

    for(i=RABlockStart[rank]; i<RABlockStart[rank+1]; i++){
        for(j = PnnzPerRowScan[RA.entry[i].col - P->split[rank]]; j < PnnzPerRowScan[RA.entry[i].col - P->split[rank] + 1]; j++){

//            if(rank==3) std::cout << RA.entry[i].row + P->splitNew[rank] << "\t" << P->entry[indicesP_Prolong[j]].col << "\t" << RA.entry[i].val * P->entry[indicesP_Prolong[j]].val << std::endl;

            RAP_temp.entry.emplace_back(cooEntry(RA.entry[i].row + P->splitNew[rank],  // Ac.entry should have global indices at the end.
                                             P->entry[indicesP_Prolong[j]].col,
                                             RA.entry[i].val * P->entry[indicesP_Prolong[j]].val));
        }
    }
//    MPI_Barrier(comm); std::cout << "here22222222222222!!!!!!!" << std::endl; MPI_Barrier(comm);

//    if(rank==1)
//        for(i=0; i<RAP_temp.entry.size(); i++)
//            std::cout << RAP_temp.entry[i].row << "\t" << RAP_temp.entry[i].col << "\t" << RAP_temp.entry[i].val << std::endl;

    free(indicesP_Prolong);

    // ************************************* RAP_temp - P remote *************************************

    unsigned int PMaxNnz;
    MPI_Allreduce(&P->nnz_l, &PMaxNnz, 1, MPI_UNSIGNED_LONG, MPI_MAX, comm);

    unsigned long* indicesP_ProlongRecv = (unsigned long*)malloc(sizeof(unsigned long)*PMaxNnz);
    cooEntry* Precv = (cooEntry*)malloc(sizeof(cooEntry)*PMaxNnz);
    long PrecvM;

    for(int i = 1; i < nprocs; i++) {
        // send P to the right processor, receive P from the left processor. "left" decreases by one in each iteration. "right" increases by one.
        right = (rank + i) % nprocs;
        left = rank - i;
        if (left < 0)
            left += nprocs;

        // *************************** RA_temp - A remote - sendrecv(size) ****************************

        // use sender rank for send and receive tags.
        MPI_Sendrecv(&P->nnz_l, 1, MPI_UNSIGNED_LONG, right, rank, &nnzRecv, 1, MPI_UNSIGNED_LONG, left, left, comm, &sendRecvStatus);
//        int MPI_Sendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype, int dest, int sendtag, void *recvbuf,
//                         int recvcount, MPI_Datatype recvtype, int source, int recvtag, MPI_Comm comm, MPI_Status *status)

//        if(rank==2) std::cout << "own P->nnz_l = " << P->nnz_l << "\tnnzRecv = " << nnzRecv << std::endl;

        // *************************** RA_temp - A remote - sendrecv(P) ****************************

        // use sender rank for send and receive tags.
        MPI_Sendrecv(&P->entry[0], P->nnz_l, cooEntry::mpi_datatype(), right, rank, Precv, nnzRecv, cooEntry::mpi_datatype(), left, left, comm, &sendRecvStatus);

//        if(rank==1) for(int j=0; j<P->nnz_l; j++)
//                        printf("j=%d \t %lu \t %lu \t %f \n", j, P->entry[j].row, P->entry[j].col, P->entry[j].val);
//        if(rank==1) for(int j=0; j<nnzRecv; j++)
//                        printf("j=%d \t %lu \t %lu \t %f \n", j, Precv[j].row, Precv[j].col, Precv[j].val);

        // *************************** RA_temp - A remote - multiplication ****************************

        PrecvM = P->split[left+1] - P->split[left];
        std::fill(&PnnzPerRow[0], &PnnzPerRow[PrecvM], 0);
        for(j=0; j<nnzRecv; j++)
            PnnzPerRow[Precv[j].row]++;

//        if(rank==1) std::cout << "PrecvM = " << PrecvM << std::endl;

//        if(rank==1)
//            for(j=0; j<PrecvM; j++)
//                std::cout << PnnzPerRow[i] << std::endl;

        PnnzPerRowScan[0] = 0;
        for(j=0; j<PrecvM; j++){
            PnnzPerRowScan[j+1] = PnnzPerRowScan[j] + PnnzPerRow[j];
//            if(rank==1) printf("j=%lu, PnnzPerRow=%d, PnnzPerRowScan = %d\n", j, PnnzPerRow[j], PnnzPerRowScan[j]);
        }

        // find row-wise ordering for Arecv and save it in indicesPRecv
        for(unsigned long i=0; i<nnzRecv; i++)
            indicesP_ProlongRecv[i] = i;
        std::sort(indicesP_ProlongRecv, &indicesP_ProlongRecv[nnzRecv], sort_indices2(Precv));

//        if(rank==1) std::cout << "block start = " << RBlockStart[left] << "\tend = " << RBlockStart[left+1] << "\tleft rank = " << left << "\t i = " << i << std::endl;
        for(j=RABlockStart[left]; j<RABlockStart[left+1]; j++){
//            if(rank==1) std::cout << "col = " << R->entry_remote[j].col << "\tcol-split = " << R->entry_remote[j].col - P->split[left] << "\tstart = " << AnnzPerRowScan[R->entry_remote[j].col - P->split[left]] << "\tend = " << AnnzPerRowScan[R->entry_remote[j].col - P->split[left] + 1] << std::endl;
            for(unsigned long k = PnnzPerRowScan[RA.entry[j].col - P->split[left]]; k < PnnzPerRowScan[RA.entry[j].col - P->split[left] + 1]; k++){
//                if(rank==0) std::cout << Precv[indicesP_ProlongRecv[k]].row << "\t" << Precv[indicesP_ProlongRecv[k]].col << "\t" << Precv[indicesP_ProlongRecv[k]].val << std::endl;
                RAP_temp.entry.push_back(cooEntry(RA.entry[j].row + P->splitNew[rank], // Ac.entry should have global indices at the end.
                                                Precv[indicesP_ProlongRecv[k]].col,
                                                RA.entry[j].val * Precv[indicesP_ProlongRecv[k]].val));
            }
        }

    } //for i

    free(indicesP_ProlongRecv);
    free(PnnzPerRow);
    free(PnnzPerRowScan);
    free(Precv);
    free(RABlockStart);

    std::sort(RAP_temp.entry.begin(), RAP_temp.entry.end());

//    if(rank==2)
//        for(j=0; j<RAP_temp.entry.size(); j++)
//            std::cout << RAP_temp.entry[j].row << "\t" << RAP_temp.entry[j].col << "\t" << RAP_temp.entry[j].val << std::endl;

    // remove duplicates.
//    std::vector<cooEntry> Ac_temp;
    for(i=0; i<RAP_temp.entry.size(); i++){
        Ac->entry.push_back(RAP_temp.entry[i]);
        while(i<RAP_temp.entry.size()-1 && RAP_temp.entry[i] == RAP_temp.entry[i+1]){ // values of entries with the same row and col should be added.
            Ac->entry.back().val += RAP_temp.entry[i+1].val;
            i++;
        }
        // todo: pruning. don't hard code tol. does this make the matrix non-symmetric?
//        if( abs(Ac->entry.back().val) < 1e-6)
//            Ac->entry.pop_back();
    }

//    par::sampleSort(Ac_temp, Ac->entry, comm);
//    Ac->entry = Ac_temp;

//    if(rank==1){
//        std::cout << "after sort:" << std::endl;
//        for(j=0; j<Ac->entry.size(); j++)
//            std::cout << Ac->entry[j] << std::endl;
//    }

    Ac->nnz_l = Ac->entry.size();
    MPI_Allreduce(&Ac->nnz_l, &Ac->nnz_g, 1, MPI_UNSIGNED, MPI_SUM, comm);
    Ac->Mbig = P->Nbig;
    Ac->comm = P->comm;
    Ac->comm_old = Ac->comm;
    Ac->cpu_shrink_thre1 = A->cpu_shrink_thre1;
    Ac->split = P->splitNew;
    Ac->M = P->splitNew[rank+1] - P->splitNew[rank];


//    MPI_Barrier(comm);
//    if(rank==0){
//        for(i = 0; i < Ac->nnz_l; i++)
//            std::cout << i << "\t" << Ac->entry[i] << std::endl;
//        std::cout << std::endl;}
//    MPI_Barrier(comm);
//    if(rank==1){
//        for(i = 0; i < Ac->nnz_l; i++)
//            std::cout << i << "\t" << Ac->entry[i] << std::endl;
//        std::cout << std::endl;}
//    MPI_Barrier(comm);
//    if(rank==2){
//        for(i = 0; i < Ac->nnz_l; i++)
//            std::cout << i << "\t" << Ac->entry[i] << std::endl;
//        std::cout << std::endl;}
//    MPI_Barrier(comm);
//    if(rank==3){
//        for(i = 0; i < Ac->nnz_l; i++)
//            std::cout << i << "\t" << Ac->entry[i] << std::endl;
//        std::cout << std::endl;}
//    MPI_Barrier(comm);


//    printf("rank=%d \tA: Mbig=%u, nnz_g = %u, nnz_l = %u, M = %u \tAc: Mbig=%u, nnz_g = %u, nnz_l = %u, M = %u \n",
//            rank, A->Mbig, A->nnz_g, A->nnz_l, A->M, Ac->Mbig, Ac->nnz_g, Ac->nnz_l, Ac->M);
//    MPI_Barrier(comm);
//    if(rank==1)
//        for(i=0; i<nprocs+1; i++)
//            std::cout << Ac->split[i] << std::endl;


    // ********** check for cpu shrinking **********
    // if number of rows on Ac < threshold*number of rows on A, then shrink.
    // redistribute Ac from processes 4k+1, 4k+2 and 4k+3 to process 4k.

//    if(rank==0)
//        printf("A->Mbig = %u, Ac->Mbig = %u, A->Mbig * A->cpu_shrink_thre1 = %f \n", A->Mbig, Ac->Mbig, A->Mbig * A->cpu_shrink_thre1);

    if(nprocs >= Ac->cpu_shrink_thre2 && Ac->Mbig <= (A->Mbig * A->cpu_shrink_thre1)){
        cpu_shrink(Ac, P->splitNew);
    }

    // ********** setup matrix **********

//    MPI_Barrier(comm); printf("rank = %d, before Ac->matrix_setup()!!! \n", rank); MPI_Barrier(comm);

    Ac->matrix_setup();

//    MPI_Barrier(comm); printf("rank = %d, after  Ac->matrix_setup()!!! \n", rank); MPI_Barrier(comm);

    return 0;
} // end of SaenaObject::coarsen


int saena_object::solve_coarsest(saena_matrix* A, std::vector<double>& u, std::vector<double>& rhs){
    // this is CG.
    // u is zero in the beginning. At the end, it is the solution.

    MPI_Comm comm = A->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    long i, j;

    // res = A*u - rhs
    MPI_Comm_rank(comm, &rank);
    std::vector<double> res(A->M);
    A->residual(u, rhs, res);

    // make res = rhs - A*u
    for(i=0; i<res.size(); i++)
        res[i] = -res[i];

//    if(rank==0){
//        std::cout << "\nsolveCoarsest: initial res" << std::endl;
//        for(auto i:res)
//            std::cout << i << std::endl;}

    double dot;
    dotProduct(res, res, &dot, comm);
//    double initialNorm = sqrt(dot);
//    if(rank==0) std::cout << "\nsolveCoarsest: initial norm(res) = " << initialNorm << std::endl;

    int max_iter = CG_max_iter;
    if (dot < CG_tol*CG_tol)
        max_iter = 0;

    std::vector<double> dir(A->M);
    dir = res;

    double factor, dot_prev;
    std::vector<double> matvecTemp(A->M);
    i = 0;
    while (i < max_iter) {
//        if(rank==0) std::cout << "starting iteration of CG = " << i << std::endl;
        // factor = sq_norm/ (dir' * A * dir)
        A->matvec(dir, matvecTemp);
//        if(rank==1){
//            std::cout << "\nsolveCoarsest: A*dir" << std::endl;
//            for(auto i:matvecTemp)
//                std::cout << i << std::endl;}


        dotProduct(dir, matvecTemp, &factor, comm);
        factor = dot / factor;
//        if(rank==1) std::cout << "\nsolveCoarsest: factor = " << factor << std::endl;

        for(j = 0; j < A->M; j++)
            u[j] += factor * dir[j];
//        if(rank==1){
//            std::cout << "\nsolveCoarsest: u" << std::endl;
//            for(auto i:u)
//                std::cout << i << std::endl;}

        // update residual
        for(j = 0; j < A->M; j++)
            res[j] -= factor * matvecTemp[j];
//        if(rank==1){
//            std::cout << "\nsolveCoarsest: update res" << std::endl;
//            for(auto i:res)
//                std::cout << i << std::endl;}

        dot_prev = dot;

        dotProduct(res, res, &dot, comm);
//        if(rank==0) std::cout << "absolute norm(res) = " << sqrt(dot) << "\t( r_i / r_0 ) = " << sqrt(dot)/initialNorm << "  \t( r_i / r_i-1 ) = " << sqrt(dot)/sqrt(dot_prev) << std::endl;
//        if(rank==0) std::cout << sqrt(dot)/initialNorm << std::endl;

        if (dot < CG_tol*CG_tol)
            break;

        factor = dot / dot_prev;
//        if(rank==1) std::cout << "\nsolveCoarsest: update factor = " << factor << std::endl;

        // update direction
        for(j = 0; j < A->M; j++)
            dir[j] = res[j] + factor * dir[j];
//        if(rank==1){
//            std::cout << "\nsolveCoarsest: update dir" << std::endl;
//            for(auto i:dir)
//                std::cout << i << std::endl;}

        i++;
    }
//    if(rank==0) std::cout << "end of solve_coarsest!" << std::endl;

    return 0;
}


// int SaenaObject::solveCoarsest(SaenaMatrix* A, std::vector<double>& x, std::vector<double>& b, int& max_iter, double& tol, MPI_Comm comm){
/*
int SaenaObject::solveCoarsest(SaenaMatrix* A, std::vector<double>& x, std::vector<double>& b, int& max_iter, double& tol, MPI_Comm comm){
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    long i, j;

    double normb_l, normb;
    normb_l = 0;
    for(i=0; i<A->M; i++)
        normb_l += b[i] * b[i];
    MPI_Allreduce(&normb_l, &normb, 1, MPI_DOUBLE, MPI_SUM, comm);
    normb = sqrt(normb);
//    if(rank==1) std::cout << normb << std::endl;

//    Vector r = b - A*x;
    std::vector<double> matvecTemp(A->M);
    A->matvec(&*x.begin(), &*matvecTemp.begin(), comm);
//    if(rank==1)
//        for(i=0; i<matvecTemp.size(); i++)
//            std::cout << matvecTemp[i] << std::endl;

    std::vector<double> r(A->M);
    for(i=0; i<matvecTemp.size(); i++)
        r[i] = b[i] - matvecTemp[i];

    if (normb == 0.0)
        normb = 1;

    double resid_l, resid;
    resid_l = 0;
    for(i=0; i<A->M; i++)
        resid_l += r[i] * r[i];
    MPI_Allreduce(&resid_l, &resid, 1, MPI_DOUBLE, MPI_SUM, comm);
    resid = sqrt(resid_l);

    if ((resid / normb) <= tol) {
        tol = resid;
        max_iter = 0;
        return 0;
    }

    double alpha, beta, rho, rho1, tempDot;
    std::vector<double> z(A->M);
    std::vector<double> p(A->M);
    std::vector<double> q(A->M);
    for (i = 0; i < max_iter; i++) {
//        z = M.solve(r);
        // todo: write this part.

//        rho(0) = dot(r, z);
        rho = 0;
        for(j = 0; j < A->M; j++)
            rho += r[j] * z[j];

//        if (i == 1)
//            p = z;
//        else {
//            beta(0) = rho(0) / rho_1(0);
//            p = z + beta(0) * p;
//        }

        if(i == 0)
            p = z;
        else{
            beta = rho / rho1;
            for(j = 0; j < A->M; j++)
                p[j] = z[j] + (beta * p[j]);
        }

//        q = A*p;
        A->matvec(&*p.begin(), &*q.begin(), comm);

//        alpha(0) = rho(0) / dot(p, q);
        tempDot = 0;
        for(j = 0; j < A->M; j++)
            tempDot += p[j] * q[j];
        alpha = rho / tempDot;

//        x += alpha(0) * p;
//        r -= alpha(0) * q;
        for(j = 0; j < A->M; j++){
            x[j] += alpha * p[j];
            r[j] -= alpha * q[j];
        }

        resid_l = 0;
        for(j = 0; j < A->M; j++)
            resid_l += r[j] * r[j];
        MPI_Allreduce(&resid_l, &resid, 1, MPI_DOUBLE, MPI_SUM, comm);
        resid = sqrt(resid_l);

        if ((resid / normb) <= tol) {
            tol = resid;
            max_iter = i;
            return 0;
        }

        rho1 = rho;
    }

    return 0;
}
*/


int saena_object::vcycle(Grid* grid, std::vector<double>& u, std::vector<double>& rhs){

    // ********** shrink rhs and u  **********
    // shrink rhs and u for the whole level, in each vcycle iteration
    // check if shrinking is required.
    int rank, nprocs;
    MPI_Comm_size(grid->A->comm_old, &nprocs);
    MPI_Comm_rank(grid->A->comm_old, &rank);

    int rank_new, nprocs_new;
    if(grid->A->comm != grid->A->comm_old){

        MPI_Comm_size(grid->A->comm_horizontal, &nprocs_new);
        MPI_Comm_rank(grid->A->comm_horizontal, &rank_new);

        unsigned long offset = 0;
        if(grid->A->active){
            offset = rhs.size();
//            u.resize(grid->A->M);
            u.assign(grid->A->M, 0);
            rhs.resize(grid->A->M);
        }

        int neigbor_rank;
        unsigned int recv_size = 0;
        unsigned int local_size = rhs.size();
        for(neigbor_rank = 1; neigbor_rank < grid->A->cpu_shrink_thre2; neigbor_rank++){

            if(rank_new == 0 && (rank + neigbor_rank >= nprocs) ){
//            printf("rank = %d, rank_new = %d, neighbor rank = %d -> break!!! \n", rank, rank_new, neigbor_rank);
                break;
            }

            // 3 - send and receive size of rhs.
//        if(rank_new == 0)
//            MPI_Recv(&recv_size, 1, MPI_UNSIGNED, neigbor_rank, 0, comm_new, MPI_STATUS_IGNORE);
//
//        if(rank_new == neigbor_rank)
//            MPI_Send(&local_size, 1, MPI_UNSIGNED, 0, 0, comm_new);

            // 4 - send and receive rhs and u.
//            if(rank_new == 0)
//                MPI_Recv(&*(u.begin() + offset), recv_size, MPI_DOUBLE, neigbor_rank, 0, grid->A->comm_horizontal, MPI_STATUS_IGNORE);
//
//            if(rank_new == neigbor_rank)
//                MPI_Send(&*u.begin(), local_size, MPI_DOUBLE, 0, 0, grid->A->comm_horizontal);

            if(rank_new == 0){
//                printf("rank = %d, grid->A->split_old[rank + neigbor_rank + 1] = %lu, grid->A->split_old[rank + neigbor_rank] = %lu \n",
//                       rank, grid->A->split_old[rank + neigbor_rank + 1], grid->A->split_old[rank + neigbor_rank]);
//                printf("rank = %d, neigbor_rank = %d, recv_size = %u \n", rank, neigbor_rank, recv_size);

                recv_size = grid->A->split_old[rank + neigbor_rank + 1] - grid->A->split_old[rank + neigbor_rank];
                MPI_Recv(&*(rhs.begin() + offset), recv_size, MPI_DOUBLE, neigbor_rank, 1, grid->A->comm_horizontal, MPI_STATUS_IGNORE);
                offset += recv_size; // set offset for the next iteration
            }

            if(rank_new == neigbor_rank){
//                printf("rank = %d, neigbor_rank = %d, local_size = %u \n", rank, neigbor_rank, local_size);
                MPI_Send(&*rhs.begin(), local_size, MPI_DOUBLE, 0, 1, grid->A->comm_horizontal);
            }
        }
    }

    if(grid->A->active){
        MPI_Barrier(grid->A->comm_old);
        if(rank==0){
//            printf("\nrank = %d, current level = %d, size = %lu \n", rank, grid->currentLevel, rhs.size());
//            for(unsigned long i=0; i<rhs.size(); i++)
//                printf("rhs[%lu] = %f \n", i, rhs[i]);
        }
//        MPI_Barrier(grid->A->comm_old);
//        if(rank==1){
//            printf("\nrank = %d, current level = %d, size = %lu \n", rank, grid->currentLevel, rhs.size());
//            for(unsigned long i=0; i<rhs.size(); i++)
//                printf("rhs[%lu] = %f \n", i, rhs[i]);
//        }
//        MPI_Barrier(grid->A->comm_old);
    }




    // ****************** vcycle ******************

    if(grid->A->active) {

        MPI_Comm_size(grid->A->comm, &nprocs);
        MPI_Comm_rank(grid->A->comm, &rank);
        long i;
        double t1, t2;
        std::string func_name;

//        printf("\nrank = %d, current level = %d \n", rank, grid->currentLevel);
//        printf("rank = %d, A->M = %u, u.size = %lu, rhs.size = %lu \n", rank, grid->A->M, u.size(), rhs.size());

        if (grid->currentLevel == max_level) {
            if(rank==0 && verbose) std::cout << "current level = " << grid->currentLevel << ", Solving the coarsest level!" << std::endl;
            t1 = MPI_Wtime();

            solve_coarsest(grid->A, u, rhs);

            t2 = MPI_Wtime();
            func_name = "Vcycle: level " + std::to_string(grid->currentLevel) + ": solve coarsest";
            if (verbose) print_time(t1, t2, func_name, grid->A->comm);
            return 0;
        }

        double dot;
        std::vector<double> res(grid->A->M);
        //    residual(grid->A, u, rhs, res);
        //    dotProduct(res, res, &dot, comm);
        //    if(rank==0) std::cout << "current level = " << grid->currentLevel << ", vcycle start      = " << sqrt(dot) << std::endl;

        // **************************************** 1. pre-smooth ****************************************

        t1 = MPI_Wtime();

        for (i = 0; i < preSmooth; i++)
            grid->A->jacobi(u, rhs);

        t2 = MPI_Wtime();
        func_name = "Vcycle: level " + std::to_string(grid->currentLevel) + ": pre";
        if (verbose) print_time(t1, t2, func_name, grid->A->comm);

        //    if(rank==1) std::cout << "\n1. pre-smooth: u, currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(auto i:u)
        //            std::cout << i << std::endl;

        // **************************************** 2. compute residual ****************************************

        grid->A->residual(u, rhs, res);

        //    if(rank==1) std::cout << "\n2. compute residual: res, currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(auto i:res)
        //            std::cout << i << std::endl;

        //    dotProduct(res, res, &dot, comm);
        //    if(rank==0) std::cout << "current level = " << grid->currentLevel << ", after pre-smooth  = " << sqrt(dot) << std::endl;

        // **************************************** 3. restrict ****************************************

        t1 = MPI_Wtime();

        std::vector<double> rCoarse(grid->Ac.M);
        grid->R.matvec(res, rCoarse);

        t2 = MPI_Wtime();
        func_name = "Vcycle: level " + std::to_string(grid->currentLevel) + ": restriction";
        if (verbose) print_time(t1, t2, func_name, grid->A->comm);

        //    if(rank==0){
        //        std::cout << "\n3. restriction: rCoarse = R*res, currentLevel = " << grid->currentLevel << std::endl;
        //        for(auto i:rCoarse)
        //            std::cout << i << std::endl;}

        // **************************************** 4. recurse ****************************************

        //    if(rank==1) std::cout << "\n\n\nenter recursive vcycle = " << grid->currentLevel << std::endl;
        std::vector<double> uCorrCoarse(grid->Ac.M);
        uCorrCoarse.assign(grid->Ac.M, 0);
        vcycle(grid->coarseGrid, uCorrCoarse, rCoarse);

        //    if(rank==1) std::cout << "\n4. uCorrCoarse, currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(auto i:uCorrCoarse)
        //            std::cout << i << std::endl;

        // **************************************** 5 & 6. prolong and correct ****************************************

        t1 = MPI_Wtime();

        std::vector<double> uCorr(grid->A->M);
        grid->P.matvec(uCorrCoarse, uCorr);

        t2 = MPI_Wtime();
        func_name = "Vcycle: level " + std::to_string(grid->currentLevel) + ": prolongation";
        if (verbose) print_time(t1, t2, func_name, grid->A->comm);

        //    if(rank==1) std::cout << "\n5. prolongation: uCorr = P*uCorrCoarse , currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(i=0; i<u.size(); i++)
        //            std::cout << uCorr[i] << std::endl;

        for (i = 0; i < u.size(); i++)
            u[i] -= uCorr[i];

        //    if(rank==1) std::cout << "\n6. correct: u -= uCorr, currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(i=0; i<u.size(); i++)
        //            std::cout << u[i] << std::endl;

        //    residual(grid->A, u, rhs, res);
        //    dotProduct(res, res, &dot, comm);
        //    if(rank==0) std::cout << "current level = " << grid->currentLevel << ", after correction  = " << sqrt(dot) << std::endl;

        // **************************************** 7. post-smooth ****************************************

        t1 = MPI_Wtime();

        for (i = 0; i < postSmooth; i++)
            grid->A->jacobi(u, rhs);

        t2 = MPI_Wtime();
        func_name = "Vcycle: level " + std::to_string(grid->currentLevel) + ": post";
        if (verbose) print_time(t1, t2, func_name, grid->A->comm);

        //    if(rank==1) std::cout << "\n7. post-smooth: u, currentLevel = " << grid->currentLevel << std::endl;
        //    if(rank==1)
        //        for(auto i:u)
        //            std::cout << i << std::endl;

        //    residual(grid->A, u, rhs, res);
        //    dotProduct(res, res, &dot, comm);
        //    if(rank==0) std::cout << "current level = " << grid->currentLevel << ", after post-smooth = " << sqrt(dot) << std::endl;

    } // end of if(active)
    return 0;
}


int saena_object::solve(std::vector<double>& u){
    MPI_Comm comm = grids[0].comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);
    long i;

    // repartition u
    repartition_u(u);

//    double temp;
//    dot(rhs, rhs, &temp, comm);
//    if(rank==0) std::cout << "norm(rhs) = " << sqrt(temp) << std::endl;

    std::vector<double> r(grids[0].A->M);
    grids[0].A->residual(u, grids[0].rhs, r);
    double initial_dot;
    dotProduct(r, r, &initial_dot, comm);
    if(rank==0) std::cout << "******************************************************" << std::endl;
    if(rank==0) std::cout << "\ninitial residual = " << sqrt(initial_dot) << std::endl << std::endl;

    double dot = initial_dot;
    for(i=0; i<vcycle_num; i++){
        vcycle(&grids[0], u, grids[0].rhs);
        grids[0].A->residual(u, grids[0].rhs, r);
        dotProduct(r, r, &dot, comm);
//        if(rank==0) printf("vcycle iteration = %ld, residual = %f \n\n", i, sqrt(dot));
        if( dot/initial_dot < relative_tolerance * relative_tolerance )
            break;
    }

    // set number of iterations that took to find the solution
    // only do the following if the end of the previous for loop was reached.
    if(i == vcycle_num)
        i--;

    if(rank==0){
        std::cout << "******************************************************" << std::endl;
        printf("\nfinal:\nstopped at iteration    = %ld \nfinal absolute residual = %e"
                       "\nrelative residual       = %e \n\n", ++i, sqrt(dot), sqrt(dot/initial_dot));
        std::cout << "******************************************************" << std::endl;
    }

//    MPI_Barrier(comm);
//    if(rank==0){
//        printf("\nrank = %d before \tu.size() = %lu \n", rank, u.size());
//        for(i = 0; i < u.size(); i++)
//            std::cout << u[i] << std::endl;}
//    MPI_Barrier(comm);
//    if(rank==1){
//        printf("\nrank = %d before \tu.size() = %lu \n", rank, u.size());
//        for(i = 0; i < u.size(); i++)
//            std::cout << u[i] << std::endl;}
//    MPI_Barrier(comm);
//    if(rank==2){
//        printf("\nrank = %d before \tu.size() = %lu \n", rank, u.size());
//        for(i = 0; i < u.size(); i++)
//            std::cout << u[i] << std::endl;}
//    MPI_Barrier(comm);

    // repartition u back
    repartition_back_u(u);

    MPI_Barrier(comm);
    if(rank==0){
        printf("\nThis is the solution u that is being passed to Nektar++:\n");
        printf("\nrank = %d \tu.size = %lu \n", rank, u.size());
        for(i = 0; i < u.size(); i++)
            std::cout << u[i] << std::endl;}
    MPI_Barrier(comm);
    if(rank==1){
        printf("\nrank = %d \tu.size = %lu \n", rank, u.size());
        for(i = 0; i < u.size(); i++)
            std::cout << u[i] << std::endl;}
    MPI_Barrier(comm);
//    if(rank==2){
//        printf("\nrank = %d \tu.size() = %lu \n", rank, u.size());
//        for(i = 0; i < u.size(); i++)
//            std::cout << u[i] << std::endl;}
//    MPI_Barrier(comm);

    return 0;
}


int saena_object::writeMatrixToFileA(saena_matrix* A, std::string name){
    // Create txt files with name Ac0.txt for processor 0, Ac1.txt for processor 1, etc.
    // Then, concatenate them in terminal: cat Ac0.txt Ac1.txt > Ac.txt
    // row and column indices of txt files should start from 1, not 0.

    // todo: check global or local index and see if A->split[rank] is required for rows.

    MPI_Comm comm = A->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    std::ofstream outFileTxt;
    std::string outFileNameTxt = "/home/abaris/Dropbox/Projects/Saena/build/writeMatrix/";
    outFileNameTxt += name;
    outFileNameTxt += std::to_string(rank);
    outFileNameTxt += ".txt";
    outFileTxt.open(outFileNameTxt);

    if(rank==0)
        outFileTxt << A->Mbig << "\t" << A->Mbig << "\t" << A->nnz_g << std::endl;
    for (long i = 0; i < A->nnz_l; i++) {
//        std::cout       << A->entry[i].row + 1 << "\t" << A->entry[i].col + 1 << "\t" << A->entry[i].val << std::endl;
        outFileTxt << A->entry[i].row + 1 << "\t" << A->entry[i].col + 1 << "\t" << A->entry[i].val << std::endl;
    }

    outFileTxt.clear();
    outFileTxt.close();

/*
    // this is the code for writing the result of jacobi to a file.
    char* outFileNameTxt = "jacobi_saena.bin";
    MPI_Status status2;
    MPI_File fh2;
    MPI_Offset offset2;
    MPI_File_open(comm, outFileNameTxt, MPI_MODE_CREATE| MPI_MODE_WRONLY, MPI_INFO_NULL, &fh2);
    offset2 = A.split[rank] * 8; // value(double=8)
    MPI_File_write_at(fh2, offset2, xp, A.M, MPI_UNSIGNED_LONG, &status2);
    int count2;
    MPI_Get_count(&status2, MPI_UNSIGNED_LONG, &count2);
    //printf("process %d wrote %d lines of triples\n", rank, count2);
    MPI_File_close(&fh2);
*/

/*
    // failed try to write this part.
    MPI_Status status;
    MPI_File fh;
    MPI_Offset offset;

//    const char* fileName = "/home/abaris/Dropbox/Projects/Saena/build/Ac.txt";
//    const char* fileName = (const char*)malloc(sizeof(const char)*49);
//    fileName = "/home/abaris/Dropbox/Projects/Saena/build/Ac" + "1.txt";
    std::string fileName = "/home/abaris/Acoarse/Ac";
    fileName += std::to_string(7);
    fileName += ".txt";

    int mpierror = MPI_File_open(comm, fileName.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);
    if (mpierror) {
        if (rank == 0) std::cout << "Unable to open the matrix file!" << std::endl;
        MPI_Finalize();
    }

//    std::vector<unsigned int> nnzScan(nprocs);
//    mpierror = MPI_Allgather(&A->nnz_l, 1, MPI_UNSIGNED, &nnzScan[1], 1, MPI_UNSIGNED, comm);
//    if (mpierror) {
//        if (rank == 0) std::cout << "Unable to gather!" << std::endl;
//        MPI_Finalize();
//    }

//    nnzScan[0] = 0;
//    for(unsigned long i=0; i<nprocs; i++){
//        nnzScan[i+1] = nnzScan[i] + nnzScan[i+1];
//        if(rank==1) std::cout << nnzScan[i] << std::endl;
//    }
//    offset = nnzScan[rank];
//    MPI_File_write_at_all(fh, rank, &nnzScan[rank])
//    unsigned int a = 1;
//    double b = 3;
//    MPI_File_write_at(fh, rank, &rank, 1, MPI_INT, &status);
//    MPI_File_write_at_all(fh, offset, &A->entry[0], A->nnz_l, cooEntry::mpi_datatype(), &status);
//    MPI_File_write_at_all(fh, &A->entry[0], A->nnz_l, cooEntry::mpi_datatype(), &status);
//    if (mpierror) {
//        if (rank == 0) std::cout << "Unable to write to the matrix file!" << std::endl;
//        MPI_Finalize();
//    }

//    int count;
//    MPI_Get_count(&status, MPI_UNSIGNED_LONG, &count);
    //printf("process %d read %d lines of triples\n", rank, count);
    MPI_File_close(&fh);
*/

    return 0;
}


int saena_object::writeMatrixToFileP(prolong_matrix* P, std::string name) {
    // Create txt files with name P0.txt for processor 0, P1.txt for processor 1, etc.
    // Then, concatenate them in terminal: cat P0.txt P1.txt > P.txt
    // row and column indices of txt files should start from 1, not 0.

    MPI_Comm comm = P->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    std::ofstream outFileTxt;
    std::string outFileNameTxt = "/home/abaris/Dropbox/Projects/Saena/build/writeMatrix/";
    outFileNameTxt += name;
    outFileNameTxt += std::to_string(rank);
    outFileNameTxt += ".txt";
    outFileTxt.open(outFileNameTxt);

    if (rank == 0)
        outFileTxt << P->Mbig << "\t" << P->Mbig << "\t" << P->nnz_g << std::endl;
    for (long i = 0; i < P->nnz_l; i++) {
//        std::cout       << P->entry[i].row + 1 + P->split[rank] << "\t" << P->entry[i].col + 1 << "\t" << P->entry[i].val << std::endl;
        outFileTxt << P->entry[i].row + 1 + P->split[rank] << "\t" << P->entry[i].col + 1 << "\t" << P->entry[i].val << std::endl;
    }

    outFileTxt.clear();
    outFileTxt.close();

    return 0;
}


int saena_object::writeMatrixToFileR(restrict_matrix* R, std::string name) {
    // Create txt files with name R0.txt for processor 0, R1.txt for processor 1, etc.
    // Then, concatenate them in terminal: cat R0.txt R1.txt > R.txt
    // row and column indices of txt files should start from 1, not 0.

    MPI_Comm comm = R->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    std::ofstream outFileTxt;
    std::string outFileNameTxt = "/home/abaris/Dropbox/Projects/Saena/build/writeMatrix/";
    outFileNameTxt += name;
    outFileNameTxt += std::to_string(rank);
    outFileNameTxt += ".txt";
    outFileTxt.open(outFileNameTxt);

    if (rank == 0)
        outFileTxt << R->Mbig << "\t" << R->Mbig << "\t" << R->nnz_g << std::endl;
    for (long i = 0; i < R->nnz_l; i++) {
//        std::cout       << R->entry[i].row + 1 + R->splitNew[rank] << "\t" << R->entry[i].col + 1 << "\t" << R->entry[i].val << std::endl;
        outFileTxt << R->entry[i].row + 1 +  R->splitNew[rank] << "\t" << R->entry[i].col + 1 << "\t" << R->entry[i].val << std::endl;
    }

    outFileTxt.clear();
    outFileTxt.close();

    return 0;
}


int saena_object::writeVectorToFileul(std::vector<unsigned long>& v, unsigned long vSize, std::string name, MPI_Comm comm) {

    // Create txt files with name name0.txt for processor 0, name1.txt for processor 1, etc.
    // Then, concatenate them in terminal: cat name0.txt name1.txt > V.txt

    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);

    std::ofstream outFileTxt;
    std::string outFileNameTxt = "/home/abaris/Dropbox/Projects/Saena/build/writeMatrix/";
    outFileNameTxt += name;
    outFileNameTxt += std::to_string(rank);
    outFileNameTxt += ".txt";
    outFileTxt.open(outFileNameTxt);

    if (rank == 0)
        outFileTxt << vSize << std::endl;
    for (long i = 0; i < v.size(); i++) {
//        std::cout       << R->entry[i].row + 1 + R->splitNew[rank] << "\t" << R->entry[i].col + 1 << "\t" << R->entry[i].val << std::endl;
        outFileTxt << v[i] << std::endl;
    }

    outFileTxt.clear();
    outFileTxt.close();

    return 0;
}


int saena_object::change_aggregation(saena_matrix* A, std::vector<unsigned long>& aggregate, std::vector<unsigned long>& splitNew){

    MPI_Comm comm = A->comm;
    int nprocs, rank;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);
    int i;

    MPI_Status status;
    MPI_File fh;
    MPI_Offset offset;

    std::string aggName = "/home/abaris/Dropbox/Projects/Saena/build/juliaAgg.bin";
    int mpiopen = MPI_File_open(comm, aggName.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
    if(mpiopen){
        if (rank==0) std::cout << "Unable to open the vector file!" << std::endl;
        MPI_Finalize();
        return -1;
    }

    // vector should have the following format: first line shows the value in row 0, second line shows the value in row 1
    offset = A->split[rank] * 8; // value(long=8)
    MPI_File_read_at(fh, offset, &*aggregate.begin(), A->M, MPI_UNSIGNED_LONG, &status);
    MPI_File_close(&fh);

//    for(auto i:aggregate)
//        std::cout << i << std::endl;

    MPI_Status status2;
    MPI_File fh2;
    MPI_Offset offset2;

    std::string aggName2 = "/home/abaris/Dropbox/Projects/Saena/build/juliaAggArray.bin";
    int mpiopen2 = MPI_File_open(comm, aggName2.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &fh2);
    if(mpiopen2){
        if (rank==0) std::cout << "Unable to open the vector file!" << std::endl;
        MPI_Finalize();
        return -1;
    }

    std::vector<unsigned long> aggArray(A->M);
    // vector should have the following format: first line shows the value in row 0, second line shows the value in row 1
    offset2 = A->split[rank] * 8; // value(long=8)
    MPI_File_read_at(fh2, offset2, &*aggArray.begin(), A->M, MPI_UNSIGNED_LONG, &status);
    MPI_File_close(&fh2);

//    for(auto i:aggArray)
//        std::cout << i << std::endl;

    unsigned long newSize = 0;
    for(auto i:aggArray)
        if(i == 1)
            newSize++;

//    if(rank==0)
//        std::cout << "newSize = " << newSize << std::endl;

    // set splitNew
    fill(splitNew.begin(), splitNew.end(), 0);
    splitNew[rank] = newSize;

    unsigned long* splitNewTemp = (unsigned long*)malloc(sizeof(unsigned long)*nprocs);
    MPI_Allreduce(&splitNew[0], splitNewTemp, nprocs, MPI_UNSIGNED_LONG, MPI_SUM, comm);

    // do scan on splitNew
    splitNew[0] = 0;
    for(i=1; i<nprocs+1; i++)
        splitNew[i] = splitNew[i-1] + splitNewTemp[i-1];

//    for(i=0; i<nprocs+1; i++)
//        std::cout << splitNew[i] << std::endl;

    free(splitNewTemp);

    return 0;
}


int saena_object::cpu_shrink(saena_matrix* Ac, std::vector<unsigned long>& P_splitNew){

    // if number of rows on Ac < threshold*number of rows on A, then shrink.
    // redistribute Ac from processes 4k+1, 4k+2 and 4k+3 to process 4k.
    MPI_Comm comm = Ac->comm;
    int rank, nprocs;
    MPI_Comm_size(comm, &nprocs);
    MPI_Comm_rank(comm, &rank);
    unsigned long i;

    if(rank==0) printf("\n***********shrink*********** \n\n");
//    printf("rank = %d \tnnz_l = %u \n", rank, Ac->nnz_l);

    MPI_Barrier(comm);
    if(rank == 0){
        std::cout << "\nbefore shrinking!!!" <<std::endl;
        std::cout << "\nrank = " << rank << ", size = " << Ac->entry.size() <<std::endl;
        for(i=0; i < Ac->entry.size(); i++)
            std::cout << i << "\t" << Ac->entry[i]  <<std::endl;
    }
    MPI_Barrier(comm);
    if(rank == 1){
        std::cout << "\nrank = " << rank << ", size = " << Ac->entry.size() <<std::endl;
        for(i=0; i < Ac->entry.size(); i++)
            std::cout << i << "\t" << Ac->entry[i]  <<std::endl;
    }
    MPI_Barrier(comm);
    if(rank == 2){
        std::cout << "\nrank = " << rank << ", size = " << Ac->entry.size() <<std::endl;
        for(i=0; i < Ac->entry.size(); i++)
            std::cout << i << "\t" << Ac->entry[i]  <<std::endl;
    }
    MPI_Barrier(comm);
    if(rank == 3){
        std::cout << "\nrank = " << rank << ", size = " << Ac->entry.size() <<std::endl;
        for(i=0; i < Ac->entry.size(); i++)
            std::cout << i << "\t" << Ac->entry[i]  <<std::endl;
    }
    MPI_Barrier(comm);

    // assume cpu_shrink_thre2 is 4, just for easier description
    // 1 - create a new comm, consisting only of processes 4k, 4k+1, 4k+2 and 4k+3 (with new ranks 0,1,2,3)
    int color = rank / Ac->cpu_shrink_thre2;
    MPI_Comm_split(comm, color, rank, &Ac->comm_horizontal);

    int rank_new, nprocs_new;
    MPI_Comm_size(Ac->comm_horizontal, &nprocs_new);
    MPI_Comm_rank(Ac->comm_horizontal, &rank_new);
//    printf("rank = %d, rank_new = %d \n", rank, rank_new);

    // 2 - update the number of rows on process 4k, and resize "entry".
    unsigned int Ac_M_neighbors_total = 0;
    unsigned int Ac_nnz_neighbors_total = 0;
    MPI_Reduce(&Ac->M, &Ac_M_neighbors_total, 1, MPI_UNSIGNED, MPI_SUM, 0, Ac->comm_horizontal);
    MPI_Reduce(&Ac->nnz_l, &Ac_nnz_neighbors_total, 1, MPI_UNSIGNED, MPI_SUM, 0, Ac->comm_horizontal);

    if(rank_new == 0){
        Ac->M = Ac_M_neighbors_total;
        Ac->entry.resize(Ac_nnz_neighbors_total);
//        printf("rank = %d, Ac_M_neighbors = %d \n", rank, Ac_M_neighbors_total);
//        printf("rank = %d, Ac_nnz_neighbors = %d \n", rank, Ac_nnz_neighbors_total);
    }

    int neigbor_rank;
    unsigned int A_recv_nnz = 0;
    unsigned long offset = Ac->nnz_l;
    for(neigbor_rank = 1; neigbor_rank < Ac->cpu_shrink_thre2; neigbor_rank++){

        if(rank_new == 0 && (rank + neigbor_rank >= nprocs) ){
//            printf("rank = %d, rank_new = %d, neighbor rank = %d -> break!!! \n", rank, rank_new, neigbor_rank);
            break;
        }

        // 3 - send and receive size of Ac.
        if(rank_new == 0)
            MPI_Recv(&A_recv_nnz, 1, MPI_UNSIGNED, neigbor_rank, 0, Ac->comm_horizontal, MPI_STATUS_IGNORE);

        if(rank_new == neigbor_rank)
            MPI_Send(&Ac->nnz_l, 1, MPI_UNSIGNED, 0, 0, Ac->comm_horizontal);

        // 4 - send and receive Ac.
        if(rank_new == 0){
//            printf("rank = %d, neigbor_rank = %d, A_recv_nnz = %u, offset = %lu \n", rank, neigbor_rank, A_recv_nnz, offset);
            MPI_Recv(&*(Ac->entry.begin() + offset), A_recv_nnz, cooEntry::mpi_datatype(), neigbor_rank, 0, Ac->comm_horizontal, MPI_STATUS_IGNORE);
            offset += A_recv_nnz; // set offset for the next iteration
        }

        if(rank_new == neigbor_rank){
//            printf("rank = %d, neigbor_rank = %d, Ac->nnz_l = %u \n", rank, neigbor_rank, Ac->nnz_l);
            MPI_Send(&*Ac->entry.begin(), Ac->nnz_l, cooEntry::mpi_datatype(), 0, 0, Ac->comm_horizontal);
        }

        // update local index for rows
//        if(rank_new == 0)
//            for(i=0; i < A_recv_nnz; i++)
//                Ac->entry[offset + i].row += Ac->split[rank + neigbor_rank] - Ac->split[rank];
    }

    MPI_Barrier(comm); MPI_Barrier(Ac->comm_horizontal);
    if(rank == 0){
        std::cout << "\nafter shrinking!!!" <<std::endl;
        std::cout << "\nrank = " << rank << ", size = " << Ac->entry.size() <<std::endl;
        for(i=0; i < Ac->entry.size(); i++)
            std::cout << i << "\t" << Ac->entry[i]  <<std::endl;
    }
    MPI_Barrier(comm); MPI_Barrier(Ac->comm_horizontal);

    Ac->active = false;
    if(rank_new == 0){
        Ac->active = true;
//        printf("active: rank = %d, rank_new = %d \n", rank, rank_new);
    }

    // 5 - update 4k.nnz_l and split
    if(Ac->active){
        Ac->nnz_l = Ac->entry.size();
        Ac->split_old = Ac->split; // save the old split for shrinking rhs and u
        Ac->split.clear();
        for(i = 0; i < nprocs+1; i++){
//            if(rank==0) printf("P->splitNew[i] = %lu\n", P_splitNew[i]);
            if( i % Ac->cpu_shrink_thre2 == 0){
                Ac->split.push_back( P_splitNew[i] );
            }
        }
        Ac->split.push_back( P_splitNew[nprocs] );
        // assert M == split[rank+1] - split[rank]
    }

    // 6 - create a new comm including only processes with 4k rank.
    MPI_Group bigger_group;
    MPI_Comm_group(comm, &bigger_group);
    auto total_active_procs = (unsigned int)ceil((double)nprocs / Ac->cpu_shrink_thre2); // note: this is ceiling, not floor.
    std::vector<int> ranks;
    for(unsigned int i = 0; i < total_active_procs; i++)
        ranks.push_back(Ac->cpu_shrink_thre2 * i);

//    for(i=0; i<ranks.size(); i++)
//        if(rank==0) std::cout << ranks[i] << std::endl;

    MPI_Group group_new;
    MPI_Group_incl(bigger_group, total_active_procs, &*ranks.begin(), &group_new);

    MPI_Comm_create_group(comm, group_new, 0, &Ac->comm);

//    if(Ac->active){
//        MPI_Comm_size(Ac->comm, &nprocs);
//        MPI_Comm_rank(Ac->comm, &rank);
//        printf("\n\nrank = %d, nprocs = %d, M = %u, Ac->split[rank+1] = %lu, Ac->split[rank] = %lu \n",
//               rank, nprocs, Ac->M, Ac->split[rank+1], Ac->split[rank]);
//    }

    // todo: how should these be freed?
//    free(&bigger_group);
//    free(&group_new);
//    free(&comm_new2);

    // todo: update shrink threshold.
    return 0;
}

int saena_object::set_rhs(std::vector<double>& rhs0){

    int rank, nprocs;
    MPI_Comm_rank(grids[0].comm, &rank);
    MPI_Comm_size(grids[0].comm, &nprocs);
    unsigned long i;
    int ran = 0;


    MPI_Barrier(grids[0].comm);
    if(rank==0){
        printf("\nThis is how RHS is received from Nektar++: \n");
        printf("\nrank = %d \trhs.size = %lu\n", rank, rhs0.size());
        for(i = 0; i < rhs0.size(); i++)
            printf("%lu \t%f\n", i, rhs0[i]);
    }
    MPI_Barrier(grids[0].comm);
    if(rank==1){
        printf("\nrank = %d \trhs.size = %lu\n", rank, rhs0.size());
        for(i = 0; i < rhs0.size(); i++)
            printf("%lu \t%f\n", i+grids[0].A->split[rank], rhs0[i]);
    }
    MPI_Barrier(grids[0].comm);


    // ************** repartition rhs, based on A.split **************

    std::vector<unsigned long> rhs_init_partition;
    rhs_init_partition.resize(nprocs);
    rhs_init_partition[rank] = rhs0.size();
    unsigned long temp = rhs0.size();

    MPI_Allgather(&temp, 1, MPI_UNSIGNED_LONG, &*rhs_init_partition.begin(), 1, MPI_UNSIGNED_LONG, grids[0].comm);
//    MPI_Alltoall(&*grids[0].rhs_init_partition.begin(), 1, MPI_INT, &*grids[0].rhs_init_partition.begin(), 1, MPI_INT, grids[0].comm);

//    for(i = 0; i < rhs_init_partition.size(); i++)
//        if(rank==ran) printf("%lu \t rhs_init_partition = %lu\n", i, rhs_init_partition[i]);

    std::vector<unsigned long> init_partition_scan(nprocs+1);
    init_partition_scan[0] = 0;
    for(i = 1; i < nprocs+1; i++)
        init_partition_scan[i] = init_partition_scan[i-1] + rhs_init_partition[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs+1; i++)
//        if(rank==ran) printf("%lu \t init_partition_scan[i] = %lu\n", i, init_partition_scan[i]);


    unsigned long start, end, start_proc, end_proc;
    start = grids[0].A->split[rank];
    end   = grids[0].A->split[rank+1];
    start_proc = lower_bound2(&*init_partition_scan.begin(), &*init_partition_scan.end(), start);
    end_proc   = lower_bound2(&*init_partition_scan.begin(), &*init_partition_scan.end(), end);
    if(init_partition_scan[rank+1] == grids[0].A->split[rank+1])
        end_proc--;
//    if(rank == ran) printf("\nstart_proc = %lu, end_proc = %lu \n", start_proc, end_proc);

    grids[0].rcount.assign(nprocs, 0);
    if(start_proc < end_proc){
//        if(rank==ran) printf("start_proc = %lu, end_proc = %lu\n", start_proc, end_proc);
//        if(rank==ran) printf("init_partition_scan[start_proc+1] = %lu, grids[0].A->split[rank] = %lu\n", init_partition_scan[start_proc+1], grids[0].A->split[rank]);
        grids[0].rcount[start_proc] = init_partition_scan[start_proc+1] - grids[0].A->split[rank];
        grids[0].rcount[end_proc] = grids[0].A->split[rank+1] - init_partition_scan[end_proc];

        for(i = start_proc+1; i < end_proc; i++){
//            if(rank==ran) printf("init_partition_scan[i+1] = %lu, init_partition_scan[i] = %lu\n", init_partition_scan[i+1], init_partition_scan[i]);
            grids[0].rcount[i] = init_partition_scan[i+1] - init_partition_scan[i];
        }
    } else if(start_proc == end_proc)
        grids[0].rcount[start_proc] = grids[0].A->split[start_proc+1] - grids[0].A->split[start_proc];
    else{
        printf("error in set_rhs function: start_proc > end_proc\n");
        MPI_Finalize();
        return -1;
    }

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t rcount[i] = %d\n", i, grids[0].rcount[i]);

    start = init_partition_scan[rank];
    end   = init_partition_scan[rank+1];
    start_proc = lower_bound2(&*grids[0].A->split.begin(), &*grids[0].A->split.end(), start);
    end_proc   = lower_bound2(&*grids[0].A->split.begin(), &*grids[0].A->split.end(), end);
    if(init_partition_scan[rank+1] == grids[0].A->split[rank+1])
        end_proc--;
//    if(rank == ran) printf("\nstart_proc = %lu, end_proc = %lu \n", start_proc, end_proc);

    grids[0].scount.assign(nprocs, 0);
    if(end_proc > start_proc){
        grids[0].scount[start_proc] = grids[0].A->split[start_proc+1] - init_partition_scan[rank];
        grids[0].scount[end_proc] = init_partition_scan[rank+1] - grids[0].A->split[end_proc];

        for(i = start_proc+1; i < end_proc; i++)
            grids[0].scount[i] = grids[0].A->split[i+1] - grids[0].A->split[i];
    } else if(start_proc == end_proc)
        grids[0].scount[start_proc] = init_partition_scan[rank+1] - init_partition_scan[rank];
    else{
        printf("error in set_rhs function: start_proc > end_proc\n");
        MPI_Finalize();
        return -1;
    }

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t scount[i] = %d\n", i, scount[i]);


    std::vector<int> rdispls(nprocs);
    rdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        rdispls[i] = grids[0].rcount[i-1] + rdispls[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t rdispls[i] = %d\n", i, rdispls[i]);


    std::vector<int> sdispls(nprocs);
    sdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        sdispls[i] = sdispls[i-1] + grids[0].scount[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t sdispls[i] = %d\n", i, sdispls[i]);

    // check if repartition is required. it is not required if the number of rows on all processors does not change.
    bool repartition_local = true;
    if(start_proc == end_proc)
        repartition_local = false;
    MPI_Allreduce(&repartition_local, &repartition, 1, MPI_CXX_BOOL, MPI_LOR, grids[0].comm);
//    printf("rank = %d, repartition_local = %d, repartition = %d \n", rank, repartition_local, repartition);

    if(repartition){
        // todo: is clear required here? It has been used a couple of times in this file.
        grids[0].rhs.clear();
        grids[0].rhs.resize(grids[0].A->split[rank+1] - grids[0].A->split[rank]);
        MPI_Alltoallv(&*rhs0.begin(), &grids[0].scount[0], &sdispls[0], MPI_DOUBLE,
                      &*grids[0].rhs.begin(), &grids[0].rcount[0], &rdispls[0], MPI_DOUBLE, grids[0].comm);
//        printf("rank = %d repart!!!!!!!!!!!!\n", rank);
    } else{
        grids[0].rhs = rhs0;
    }

//    MPI_Barrier(grids[0].comm);
//    if(rank==0){
//        printf("\nafter rank = %d \trhs.size = %lu\n", rank, grids[0].rhs.size());
//        for(i = 0; i < grids[0].rhs.size(); i++)
//            printf("%lu \t grids[0].rhs = %f\n", i, grids[0].rhs[i]);
//    }
//    MPI_Barrier(grids[0].comm);
//    if(rank==1){
//        printf("\nrank = %d \trhs.size = %lu\n", rank, grids[0].rhs.size());
//        for(i = 0; i < grids[0].rhs.size(); i++)
//            printf("%lu \t grids[0].rhs = %f\n", i, grids[0].rhs[i]);
//    }
//    MPI_Barrier(grids[0].comm);

/*
    unsigned int rhs_size_temp;
    unsigned int rhs_recv_size = 0;
    std::vector<double> rhs_recv;
    int neigbor_rank = 0;

    if(max_level == 0)
        return 0;

//    MPI_Barrier(grids[0].comm); printf("\nbefore setting rhs for levels > 0 !!!\n"); MPI_Barrier(grids[0].comm);

    for(i = 1; i <= max_level; i++){
        if(grids[i].comm != grids[i-1].comm){
//            printf("\nshrink rhs!!!\n");

            // 1 - create a new comm, consisting only of processes 4k, 4k+1, 4k+2 and 4k+3 (with new ranks 0,1,2,3)
            MPI_Comm_rank(grids[i-1].comm, &rank);
            int color = rank / 4;
            MPI_Comm comm_new;
            MPI_Comm_split(grids[i-1].comm, color, rank, &comm_new);

            int rank_new, nprocs_new;
            MPI_Comm_size(comm_new, &nprocs_new);
            MPI_Comm_rank(comm_new, &rank_new);

            if(rank_new == 0)
                grids[i].rhs = grids[i-1].rhs;

            for(neigbor_rank = 1; neigbor_rank < 4; neigbor_rank++){

                // 3 - send and receive size of rhs, then resize rhs_recv.
                if(rank_new == 0)
                    MPI_Recv(&rhs_recv_size, 1, MPI_UNSIGNED, neigbor_rank, 0, comm_new, MPI_STATUS_IGNORE);

                if(rank_new != 0){
                    rhs_size_temp = grids[i-1].rhs.size();
                    MPI_Send(&rhs_size_temp , 1, MPI_UNSIGNED, 0, 0, comm_new);
                }

                if(rank_new == 0){
                    rhs_recv.clear();
                    rhs_recv.resize(rhs_recv_size);
                }

                // 4 - send and receive rhs. then, push back to rhs on 4k.
                if(rank_new == 0)
                    MPI_Recv(&*rhs_recv.begin(), rhs_recv_size, MPI_DOUBLE, neigbor_rank, 0, comm_new, MPI_STATUS_IGNORE);

                if(rank_new == neigbor_rank)
                    MPI_Send(&*grids[i-1].rhs.begin(), grids[i-1].rhs.size(), MPI_DOUBLE, 0, 0, comm_new);

                if(rank_new == 0) {
                    for (i = 0; i < rhs_recv_size; i++)
                        grids[i].rhs.push_back(rhs_recv[i]);
                }
            }
        }else{
            // todo: instead of copying, try to point to the previous level's rhs.
            grids[i].rhs = grids[i-1].rhs;
        }
    }
/*

/*
    if(rank==1){
        for(i=0; i<=max_level; i++){
            if(grids[i].A->active){
                printf("rhs: level %lu, size = %lu\n", i, grids[i].rhs.size());
                for(unsigned int j=0; j<grids[i].rhs.size(); j++)
                    std::cout << grids[i].rhs[j] << std::endl;
            }
        }
    }
*/

    return 0;
}


int saena_object::repartition_u(std::vector<double>& u0){

    int rank, nprocs;
    MPI_Comm_rank(grids[0].comm, &rank);
    MPI_Comm_size(grids[0].comm, &nprocs);
    unsigned long i;
//    int ran = 3;

    // make a copy of u0 to be used in Alltoall as sendbuf. u0 itself will be recvbuf there.
    std::vector<double> u_temp = u0;

    // ************** repartition u, based on A.split **************

    std::vector<int> rdispls(nprocs);
    rdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        rdispls[i] = grids[0].rcount[i-1] + rdispls[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==0) printf("%lu \t rdispls[i] = %d\n", i, rdispls[i]);


    std::vector<int> sdispls(nprocs);
    sdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        sdispls[i] = sdispls[i-1] + grids[0].scount[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t sdispls[i] = %d\n", i, sdispls[i]);

    u0.clear();
    u0.resize(grids[0].A->split[rank+1] - grids[0].A->split[rank]);
    MPI_Alltoallv(&*u_temp.begin(), &grids[0].scount[0], &sdispls[0], MPI_DOUBLE,
                  &*u0.begin(), &grids[0].rcount[0], &rdispls[0], MPI_DOUBLE, grids[0].comm);

//    if(rank==ran) printf("\nrank = %d \tu.size = %lu\n", rank, u0.size());
//    for(i = 0; i < u0.size(); i++)
//        if(rank==ran) printf("u[%lu] = %f\n", i, u0[i]);

    return 0;
}


int saena_object::repartition_back_u(std::vector<double>& u0){

    int rank, nprocs;
    MPI_Comm_rank(grids[0].comm, &rank);
    MPI_Comm_size(grids[0].comm, &nprocs);
    unsigned long i;
//    int ran = 1;

    // make a copy of u0 to be used in Alltoall as sendbuf. u0 itself will be recvbuf there.
    std::vector<double> u_temp = u0;

    // rdispls should be the opposite of the initial repartition function. So, rdispls should be the scan of scount.
    // the same for sdispls.
    std::vector<int> rdispls(nprocs);
    rdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        rdispls[i] = rdispls[i-1] + grids[0].scount[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t rdispls[i] = %d\n", i, rdispls[i]);

    std::vector<int> sdispls(nprocs);
    sdispls[0] = 0;
    for(i = 1; i < nprocs; i++)
        sdispls[i] = sdispls[i-1] + grids[0].rcount[i-1];

//    if(rank==ran) printf("\n");
//    for(i = 0; i < nprocs; i++)
//        if(rank==ran) printf("%lu \t sdispls[i] = %d\n", i, sdispls[i]);

    long rhs_init_size = rdispls[nprocs-1] + grids[0].scount[nprocs-1]; // this is the summation over all rcount values on each proc.
//    printf("rank = %d, rhs_init_size = %lu \n", rank, rhs_init_size);
    u0.clear();
    u0.resize(rhs_init_size);
    MPI_Alltoallv(&*u_temp.begin(), &grids[0].rcount[0], &sdispls[0], MPI_DOUBLE, &*u0.begin(), &grids[0].scount[0], &rdispls[0], MPI_DOUBLE, grids[0].comm);

//    MPI_Barrier(grids[0].comm);
//    if(rank==ran) printf("\nrank = %d \tu.size = %lu\n", rank, u0.size());
//    for(i = 0; i < u0.size(); i++)
//        if(rank==ran) printf("u[%lu] = %f\n", i, u0[i]);
//    MPI_Barrier(grids[0].comm);

    return 0;
}