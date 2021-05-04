/* Program usage: mpiexec -n 2 ./ex1 [-help] [all TAO options] */

/* ----------------------------------------------------------------------
min f(x) = (x0 - 2)^2 + (x1 - 2)^2 - 2*(x0 + x1)
s.t.  x0^2 + x1 - 2 = 0
      0  <= x0^2 - x1 <= 1
      -1 <= x0, x1 <= 2
-->
      g(x)  = 0
      h(x) >= 0
      -1 <= x0, x1 <= 2
where
      g(x) = x0^2 + x1 - 2
      h(x) = [x0^2 - x1
              1 -(x0^2 - x1)]
---------------------------------------------------------------------- */

#include <petsctao.h>

static  char help[]= "Solves constrained optimiztion problem using pdipm.\n\
Input parameters include:\n\
  -tao_type pdipm    : sets Tao solver\n\
  -no_eq             : removes the equaility constraints from the problem\n\
  -snes_fd           : snes with finite difference Jacobian (needed for pdipm)\n\
  -snes_compare_explicit : compare user Jacobian with finite difference Jacobian \n\
  -tao_cmonitor      : convergence monitor with constraint norm \n\
  -tao_view_solution : view exact solution at each itteration\n\
  Note: external package MUMPS is required to run pdipm. This is designed for a maximum of 2 processors, the code will error if size > 2.\n";

/*
   User-defined application context - contains data needed by the application
*/
typedef struct {
  PetscInt   n;  /* Global length of x */
  PetscInt   ne; /* Global number of equality constraints */
  PetscInt   ni; /* Global number of inequality constraints */
  PetscBool  noeqflag;
  Vec        x,xl,xu;
  Vec        ce,ci,bl,bu,Xseq;
  Mat        Ae,Ai,H;
  VecScatter scat;
} AppCtx;

/* -------- User-defined Routines --------- */
PetscErrorCode InitializeProblem(AppCtx *);
PetscErrorCode DestroyProblem(AppCtx *);
PetscErrorCode FormFunctionGradient(Tao,Vec,PetscReal *,Vec,void *);
PetscErrorCode FormHessian(Tao,Vec,Mat,Mat, void*);
PetscErrorCode FormInequalityConstraints(Tao,Vec,Vec,void*);
PetscErrorCode FormEqualityConstraints(Tao,Vec,Vec,void*);
PetscErrorCode FormInequalityJacobian(Tao,Vec,Mat,Mat, void*);
PetscErrorCode FormEqualityJacobian(Tao,Vec,Mat,Mat, void*);

PetscErrorCode main(int argc,char **argv)
{
  PetscErrorCode ierr;  /* used to check for functions returning nonzeros */
  Tao            tao;
  KSP            ksp;
  PC             pc;
  AppCtx         user;  /* application context */
  Vec            x;
  PetscMPIInt    rank;
  TaoType        type;
  PetscBool      pdipm;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRMPI(ierr);
  if (rank>1) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_WRONG_MPI_SIZE,"More than 2 processors detected. Example written to use max of 2 processors.\n");

  ierr = PetscPrintf(PETSC_COMM_WORLD,"---- Constrained Problem -----\n");CHKERRQ(ierr);
  ierr = InitializeProblem(&user);CHKERRQ(ierr); /* sets up problem, function below */
  ierr = TaoCreate(PETSC_COMM_WORLD,&tao);CHKERRQ(ierr);
  ierr = TaoSetType(tao,TAOPDIPM);CHKERRQ(ierr);
  ierr = TaoSetInitialVector(tao,user.x);CHKERRQ(ierr); /* gets solution vector from problem */
  ierr = TaoSetVariableBounds(tao,user.xl,user.xu);CHKERRQ(ierr); /* sets lower upper bounds from given solution */
  ierr = TaoSetObjectiveAndGradientRoutine(tao,FormFunctionGradient,(void*)&user);CHKERRQ(ierr);

  if (!user.noeqflag){
    ierr = TaoSetEqualityConstraintsRoutine(tao,user.ce,FormEqualityConstraints,(void*)&user);CHKERRQ(ierr);
  }
  ierr = TaoSetInequalityConstraintsRoutine(tao,user.ci,FormInequalityConstraints,(void*)&user);CHKERRQ(ierr);
  if (!user.noeqflag){
    ierr = TaoSetJacobianEqualityRoutine(tao,user.Ae,user.Ae,FormEqualityJacobian,(void*)&user);CHKERRQ(ierr); /* equality jacobian */
  }
  ierr = TaoSetJacobianInequalityRoutine(tao,user.Ai,user.Ai,FormInequalityJacobian,(void*)&user);CHKERRQ(ierr); /* inequality jacobian */
  ierr = TaoSetTolerances(tao,1.e-6,1.e-6,1.e-6);CHKERRQ(ierr);
  ierr = TaoSetConstraintTolerances(tao,1.e-6,1.e-6);CHKERRQ(ierr);

  ierr = TaoGetKSP(tao,&ksp);CHKERRQ(ierr);
  ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
  ierr = PCSetType(pc,PCCHOLESKY);CHKERRQ(ierr);
  /*
      This algorithm produces matrices with zeros along the diagonal therefore we use
    MUMPS which provides solver for indefinite matrices
  */
  ierr = PCFactorSetMatSolverType(pc,MATSOLVERMUMPS);CHKERRQ(ierr);  /* requires mumps to solve pdipm */
  ierr = KSPSetType(ksp,KSPPREONLY);CHKERRQ(ierr);
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);

  ierr = TaoSetFromOptions(tao);CHKERRQ(ierr);
  ierr = TaoGetType(tao,&type);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)tao,TAOPDIPM,&pdipm);CHKERRQ(ierr);
  if (pdipm) {
    ierr = TaoSetHessianRoutine(tao,user.H,user.H,FormHessian,(void*)&user);CHKERRQ(ierr);
  }

  ierr = TaoSolve(tao);CHKERRQ(ierr);
  ierr = TaoGetSolutionVector(tao,&x);CHKERRQ(ierr);
  ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* Free objects */
  ierr = DestroyProblem(&user);CHKERRQ(ierr);
  ierr = TaoDestroy(&tao);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

PetscErrorCode InitializeProblem(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscMPIInt    size;
  PetscMPIInt    rank;
  PetscInt       nloc,neloc,niloc;

  PetscFunctionBegin;
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRMPI(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRMPI(ierr);
  user->noeqflag = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-no_eq",&user->noeqflag,NULL);CHKERRQ(ierr);

  if (!user->noeqflag) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Solution should be f(1,1)=-2\n");CHKERRQ(ierr);
  }

  /* create vector x and set initial values */
  user->n = 2; /* global length */
  nloc = (rank==0)?user->n:0;
  ierr = VecCreate(PETSC_COMM_WORLD,&user->x);CHKERRQ(ierr);
  ierr = VecSetSizes(user->x,nloc,user->n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->x);CHKERRQ(ierr);
  ierr = VecSet(user->x,0);CHKERRQ(ierr);

  /* create and set lower and upper bound vectors */
  ierr = VecDuplicate(user->x,&user->xl);CHKERRQ(ierr);
  ierr = VecDuplicate(user->x,&user->xu);CHKERRQ(ierr);
  ierr = VecSet(user->xl,-1.0);CHKERRQ(ierr);
  ierr = VecSet(user->xu,2.0);CHKERRQ(ierr);

  /* create scater to zero */
  ierr = VecScatterCreateToZero(user->x,&user->scat,&user->Xseq);CHKERRQ(ierr);

    user->ne = 1;
    user->ni = 2;
    neloc = (rank==0)?user->ne:0;
    niloc = (rank==0)?user->ni:0;

  if (!user->noeqflag){
    ierr = VecCreate(PETSC_COMM_WORLD,&user->ce);CHKERRQ(ierr); /* a 1x1 vec for equality constraints */
    ierr = VecSetSizes(user->ce,neloc,user->ne);CHKERRQ(ierr);
    ierr = VecSetFromOptions(user->ce);CHKERRQ(ierr);
    ierr = VecSetUp(user->ce);CHKERRQ(ierr);
  }

  ierr = VecCreate(PETSC_COMM_WORLD,&user->ci);CHKERRQ(ierr); /* a 2x1 vec for inequality constraints */
  ierr = VecSetSizes(user->ci,niloc,user->ni);CHKERRQ(ierr);
  ierr = VecSetFromOptions(user->ci);CHKERRQ(ierr);
  ierr = VecSetUp(user->ci);CHKERRQ(ierr);

  /* nexn & nixn matricies for equaly and inequalty constriants */
  if (!user->noeqflag){
    ierr = MatCreate(PETSC_COMM_WORLD,&user->Ae);CHKERRQ(ierr);
    ierr = MatSetSizes(user->Ae,neloc,nloc,user->ne,user->n);CHKERRQ(ierr);
    ierr = MatSetFromOptions(user->Ae);CHKERRQ(ierr);
    ierr = MatSetUp(user->Ae);CHKERRQ(ierr);
  }

  ierr = MatCreate(PETSC_COMM_WORLD,&user->Ai);CHKERRQ(ierr);
  ierr = MatSetSizes(user->Ai,niloc,nloc,user->ni,user->n);CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->Ai);CHKERRQ(ierr);
  ierr = MatSetUp(user->Ai);CHKERRQ(ierr);

  ierr = MatCreate(PETSC_COMM_WORLD,&user->H);CHKERRQ(ierr);
  ierr = MatSetSizes(user->H,nloc,nloc,user->n,user->n);CHKERRQ(ierr);
  ierr = MatSetFromOptions(user->H);CHKERRQ(ierr);
  ierr = MatSetUp(user->H);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode DestroyProblem(AppCtx *user)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (!user->noeqflag){
   ierr = MatDestroy(&user->Ae);CHKERRQ(ierr);
  }
  ierr = MatDestroy(&user->Ai);CHKERRQ(ierr);
  ierr = MatDestroy(&user->H);CHKERRQ(ierr);

  ierr = VecDestroy(&user->x);CHKERRQ(ierr);
  if (!user->noeqflag){
    ierr = VecDestroy(&user->ce);CHKERRQ(ierr);
  }
  ierr = VecDestroy(&user->ci);CHKERRQ(ierr);
  ierr = VecDestroy(&user->xl);CHKERRQ(ierr);
  ierr = VecDestroy(&user->xu);CHKERRQ(ierr);
  ierr = VecDestroy(&user->Xseq);CHKERRQ(ierr);
  ierr = VecScatterDestroy(&user->scat);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Evaluate
   f(x) = (x0 - 2)^2 + (x1 - 2)^2 - 2*(x0 + x1)
   G = grad f = [2*(x0 - 2) - 2;
                 2*(x1 - 2) - 2]
*/
PetscErrorCode FormFunctionGradient(Tao tao, Vec X, PetscReal *f, Vec G, void *ctx)
{
  PetscScalar       g;
  const PetscScalar *x;
  MPI_Comm          comm;
  PetscMPIInt       rank;
  PetscErrorCode    ierr;
  PetscReal         fin;
  AppCtx            *user=(AppCtx*)ctx;
  Vec               Xseq=user->Xseq;
  VecScatter        scat=user->scat;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  ierr = VecScatterBegin(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  fin = 0.0;
  if (!rank) {
    ierr = VecGetArrayRead(Xseq,&x);CHKERRQ(ierr);
    fin = (x[0]-2.0)*(x[0]-2.0) + (x[1]-2.0)*(x[1]-2.0) - 2.0*(x[0]+x[1]);
    g = 2.0*(x[0]-2.0) - 2.0;
    ierr = VecSetValue(G,0,g,INSERT_VALUES);CHKERRQ(ierr);
    g = 2.0*(x[1]-2.0) - 2.0;
    ierr = VecSetValue(G,1,g,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecRestoreArrayRead(Xseq,&x);CHKERRQ(ierr);
  }
  ierr = MPI_Allreduce(&fin,f,1,MPIU_REAL,MPIU_SUM,comm);CHKERRMPI(ierr);
  ierr = VecAssemblyBegin(G);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(G);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Evaluate
   H = fxx + grad (grad g^T*DI) - grad (grad h^T*DE)]
     = [ 2*(1+de[0]-di[0]+di[1]), 0;
                   0,             2]
*/
PetscErrorCode FormHessian(Tao tao, Vec x,Mat H, Mat Hpre, void *ctx)
{
  AppCtx            *user=(AppCtx*)ctx;
  Vec               DE,DI;
  const PetscScalar *de,*di;
  PetscInt          zero=0,one=1;
  PetscScalar       two=2.0;
  PetscScalar       val=0.0;
  Vec               Deseq,Diseq=user->Xseq;
  VecScatter        Descat,Discat=user->scat;
  PetscMPIInt       rank;
  MPI_Comm          comm;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = TaoGetDualVariables(tao,&DE,&DI);CHKERRQ(ierr);

  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  if (!user->noeqflag){
   ierr = VecScatterCreateToZero(DE,&Descat,&Deseq);CHKERRQ(ierr);
   ierr = VecScatterBegin(Descat,DE,Deseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
   ierr = VecScatterEnd(Descat,DE,Deseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  }
  ierr = VecScatterBegin(Discat,DI,Diseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(Discat,DI,Diseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  if (!rank){
    if (!user->noeqflag){
      ierr = VecGetArrayRead(Deseq,&de);CHKERRQ(ierr);  /* places equality constraint dual into array */
    }
    ierr = VecGetArrayRead(Diseq,&di);CHKERRQ(ierr);  /* places inequality constraint dual into array */

    if (!user->noeqflag){
      val = 2.0 * (1 + de[0] - di[0] + di[1]);
      ierr = VecRestoreArrayRead(Deseq,&de);CHKERRQ(ierr);
      ierr = VecRestoreArrayRead(Diseq,&di);CHKERRQ(ierr);
    } else {
      val = 2.0 * (1 - di[0] + di[1]);
    }
    ierr = VecRestoreArrayRead(Diseq,&di);CHKERRQ(ierr);
    ierr = MatSetValues(H,1,&zero,1,&zero,&val,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatSetValues(H,1,&one,1,&one,&two,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(H,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(H,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  if (!user->noeqflag){
    ierr = VecScatterDestroy(&Descat);CHKERRQ(ierr);
    ierr = VecDestroy(&Deseq);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/* Evaluate
   h = [ x0^2 - x1;
         1 -(x0^2 - x1)]
*/
PetscErrorCode FormInequalityConstraints(Tao tao,Vec X,Vec CI,void *ctx)
{
  const PetscScalar *x;
  PetscScalar       ci;
  PetscErrorCode    ierr;
  MPI_Comm          comm;
  PetscMPIInt       rank;
  AppCtx            *user=(AppCtx*)ctx;
  Vec               Xseq=user->Xseq;
  VecScatter        scat=user->scat;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  ierr = VecScatterBegin(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  if (!rank) {
    ierr = VecGetArrayRead(Xseq,&x);CHKERRQ(ierr);
    ci = x[0]*x[0] - x[1];
    ierr = VecSetValue(CI,0,ci,INSERT_VALUES);CHKERRQ(ierr);
    ci = -x[0]*x[0] + x[1] + 1.0;
    ierr = VecSetValue(CI,1,ci,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecRestoreArrayRead(Xseq,&x);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(CI);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(CI);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Evaluate
   g = [ x0^2 + x1 - 2]
*/
PetscErrorCode FormEqualityConstraints(Tao tao,Vec X,Vec CE,void *ctx)
{
  const PetscScalar *x;
  PetscScalar       ce;
  PetscErrorCode    ierr;
  MPI_Comm          comm;
  PetscMPIInt       rank;
  AppCtx            *user=(AppCtx*)ctx;
  Vec               Xseq=user->Xseq;
  VecScatter        scat=user->scat;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  ierr = VecScatterBegin(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  if (!rank) {
    ierr = VecGetArrayRead(Xseq,&x);CHKERRQ(ierr);
    ce = x[0]*x[0] + x[1] - 2.0;
    ierr = VecSetValue(CE,0,ce,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecRestoreArrayRead(Xseq,&x);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(CE);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(CE);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
  grad h = [  2*x0, -1;
             -2*x0,  1]
*/
PetscErrorCode FormInequalityJacobian(Tao tao, Vec X, Mat JI, Mat JIpre,  void *ctx)
{
  AppCtx            *user=(AppCtx*)ctx;
  PetscInt          cols[2],min,max,i;
  PetscScalar       vals[2];
  const PetscScalar *x;
  PetscErrorCode    ierr;
  Vec               Xseq=user->Xseq;
  VecScatter        scat=user->scat;
  MPI_Comm          comm;
  PetscMPIInt       rank;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);
  ierr = VecScatterBegin(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
  ierr = VecScatterEnd(scat,X,Xseq,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);

  ierr = VecGetArrayRead(Xseq,&x);CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(JI,&min,&max);CHKERRQ(ierr);

  cols[0] = 0; cols[1] = 1;
  for (i=min;i<max;i++) {
    if (i==0){
      vals[0] = 2*x[0]; vals[1] = -1.0;
      ierr = MatSetValues(JI,1,&i,2,cols,vals,INSERT_VALUES);CHKERRQ(ierr);
    }
    if (i==1) {
      vals[0] = -2*x[0]; vals[1] = 1.0;
      ierr = MatSetValues(JI,1,&i,2,cols,vals,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecRestoreArrayRead(Xseq,&x);CHKERRQ(ierr);

  ierr = MatAssemblyBegin(JI,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(JI,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
  grad g = [2*x0
             1.0 ]
*/
PetscErrorCode FormEqualityJacobian(Tao tao,Vec X,Mat JE,Mat JEpre,void *ctx)
{
  PetscInt          rows[2];
  PetscScalar       vals[2];
  const PetscScalar *x;
  PetscMPIInt       rank;
  MPI_Comm          comm;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)tao,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  if (!rank) {
    ierr = VecGetArrayRead(X,&x);CHKERRQ(ierr);
    rows[0] = 0;       rows[1] = 1;
    vals[0] = 2*x[0];  vals[1] = 1.0;
    ierr = MatSetValues(JE,1,rows,2,rows,vals,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecRestoreArrayRead(X,&x);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(JE,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(JE,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


/*TEST

   build:
      requires: !complex !define(PETSC_USE_CXX) mumps

   test:
      args: -tao_converged_reason -tao_pdipm_kkt_shift_pd

   test:
      suffix: 2
      nsize: 2
      args: -tao_converged_reason -tao_pdipm_kkt_shift_pd

   test:
      suffix: 3
      args: -tao_converged_reason -no_eq

   test:
      suffix: 4
      nsize: 2
      args: -tao_converged_reason -no_eq

   test:
      suffix: 5
      args: -tao_cmonitor -tao_type almm

   test:
      suffix: 6
      args: -tao_cmonitor -tao_type almm -tao_almm_type phr

   test:
      suffix: 7
      nsize: 2
      args: -tao_cmonitor -tao_type almm

   test:
      suffix: 8
      nsize: 2
      requires: cuda
      args: -tao_cmonitor -tao_type almm -vec_type cuda -mat_type aijcusparse

   test:
      suffix: 9
      nsize: 2
      args: -tao_cmonitor -tao_type almm -no_eq

   test:
      suffix: 10
      nsize: 2
      args: -tao_cmonitor -tao_type almm -tao_almm_type phr -no_eq

TEST*/
