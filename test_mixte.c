#pragma ProjetCA mpicoll_check main

#include <stdio.h>
#include <stdlib.h>

#include <mpi.h>

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int i;
    int a = 2;
    int b = 3;
    int c = 0;

    if (c < 10)
    {
        if (c < 5)
        {
            a = a * a + 1;
        }
        else
        {
            a = a * 3;
            MPI_Barrier(MPI_COMM_WORLD);
        }

        c += (a * 2);
    }
    else
    {
        b = b * 4;
        MPI_Barrier(MPI_COMM_WORLD);
    }

    c += (a + b);

    printf("c=%d\n", c);

    MPI_Finalize();
    return 1;
}
