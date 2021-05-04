
#include <petsc/private/pcimpl.h>   /*I "petscpc.h" I*/

static PetscErrorCode PCApply_Mat(PC pc,Vec x,Vec y)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatMult(pc->pmat,x,y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCMatApply_Mat(PC pc,Mat X,Mat Y)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatMatMult(pc->pmat,X,MAT_REUSE_MATRIX,PETSC_DEFAULT,&Y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApplyTranspose_Mat(PC pc,Vec x,Vec y)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = MatMultTranspose(pc->pmat,x,y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCDestroy_Mat(PC pc)
{
  PetscFunctionBegin;
  PetscFunctionReturn(0);
}

/*MC
     PCMAT - A preconditioner obtained by multiplying by the preconditioner matrix supplied
             in PCSetOperators() or KSPSetOperators()

   Notes:
    This one is a little strange. One rarely has an explict matrix that approximates the
         inverse of the matrix they wish to solve for.

   Level: intermediate

.seealso:  PCCreate(), PCSetType(), PCType (for list of available types), PC,
           PCSHELL

M*/

PETSC_EXTERN PetscErrorCode PCCreate_Mat(PC pc)
{
  PetscFunctionBegin;
  pc->ops->apply               = PCApply_Mat;
  pc->ops->matapply            = PCMatApply_Mat;
  pc->ops->applytranspose      = PCApplyTranspose_Mat;
  pc->ops->setup               = NULL;
  pc->ops->destroy             = PCDestroy_Mat;
  pc->ops->setfromoptions      = NULL;
  pc->ops->view                = NULL;
  pc->ops->applyrichardson     = NULL;
  pc->ops->applysymmetricleft  = NULL;
  pc->ops->applysymmetricright = NULL;
  PetscFunctionReturn(0);
}

