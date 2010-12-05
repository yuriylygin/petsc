#define PETSCMAT_DLL
#include "../src/mat/impls/aij/mpi/mpiaij.h"

EXTERN_C_BEGIN
#undef __FUNCT__  
#define __FUNCT__ "MatGetDiagonalBlock_MPIAIJ"
PetscErrorCode  MatGetDiagonalBlock_MPIAIJ(Mat A,PetscBool  *iscopy,MatReuse reuse,Mat *a)
{
  PetscFunctionBegin;
  *a      = ((Mat_MPIAIJ *)A->data)->A;
  *iscopy = PETSC_FALSE;
  PetscFunctionReturn(0);
}
EXTERN_C_END
