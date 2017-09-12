#ifndef SAENA_GRID_H
#define SAENA_GRID_H

#include "saena_matrix.h"
#include "prolong_matrix.h"
#include "restrict_matrix.h"

class Grid{
private:

public:
    saena_matrix* A;
    saena_matrix  Ac;
    prolong_matrix P;
    restrict_matrix R;
    int currentLevel, maxLevel;
    Grid* coarseGrid;

    MPI_Comm comm;

    Grid();
    Grid(saena_matrix* A, int maxLevel, int currentLevel);
    ~Grid();
};

#endif //SAENA_GRID_H
