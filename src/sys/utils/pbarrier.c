
#include <petsc/private/petscimpl.h>              /*I "petscsys.h" I*/

/* Logging support */
PetscLogEvent PETSC_Barrier;

/*@C
    PetscBarrier - Blocks until this routine is executed by all
                   processors owning the object obj.

   Input Parameters:
.  obj - PETSc object  (Mat, Vec, IS, SNES etc...)
        The object be caste with a (PetscObject). NULL can be used to indicate the barrier should be across MPI_COMM_WORLD

  Level: intermediate

  Notes:
  This routine calls MPI_Barrier with the communicator of the PETSc Object obj

  Fortran Usage:
    You may pass PETSC_NULL_VEC or any other PETSc null object, such as PETSC_NULL_MAT, to indicate the barrier should be
    across MPI_COMM_WORLD. You can also pass in any PETSc object, Vec, Mat, etc

@*/
PetscErrorCode  PetscBarrier(PetscObject obj)
{
  PetscErrorCode ierr;
  MPI_Comm       comm;

  PetscFunctionBegin;
  if (obj) PetscValidHeader(obj,1);
  ierr = PetscLogEventBegin(PETSC_Barrier,obj,0,0,0);CHKERRQ(ierr);
  if (obj) {
    ierr = PetscObjectGetComm(obj,&comm);CHKERRQ(ierr);
  } else comm = PETSC_COMM_WORLD;
  ierr = MPI_Barrier(comm);CHKERRMPI(ierr);
  ierr = PetscLogEventEnd(PETSC_Barrier,obj,0,0,0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

