#pragma once

#include "saena_matrix.h"

inline void saena_matrix::matvec(const value_t *v, value_t *w){
    if(use_dense){
        dense_matrix->matvec(v, w);
    }else{
        if(use_double) matvec_sparse(v, w);
        else matvec_sparse_float(v, w);
//        matvec_sparse_zfp(v, w);
    }
}

// Vector res = A * u - rhs;
inline void saena_matrix::residual(const value_t *u, const value_t *rhs, value_t *&res){
    matvec(&u[0], &res[0]);
    const index_t sz = M;
//#pragma omp parallel for simd aligned(res, rhs: ALIGN_SZ)
    for(index_t i = 0; i < sz; ++i){
        res[i] -= rhs[i];
    }
}

// Vector res = rhs - A * u
inline void saena_matrix::residual_negative(const value_t *u, const value_t *rhs, value_t *&res){
    matvec(&u[0], &res[0]);
    const index_t sz = M;
//#pragma omp parallel for simd aligned(res, rhs: ALIGN_SZ)
    for(index_t i = 0; i < sz; ++i){
        res[i] = rhs[i] - res[i];
    }
}

inline void saena_matrix::residual_multiply(const value_t *u, const value_t *rhs, value_t *&res,
                                            const value_t *w, const value_t &c){
    matvec(&u[0], &res[0]);
    const index_t sz = M;
//#pragma omp parallel for simd aligned(res, rhs, w: ALIGN_SZ)
    for(index_t i = 0; i < sz; ++i){
        res[i] = c * w[i] * (rhs[i] - res[i]);
    }
}