static char help[] = "This example demonstrates the use of DMNetwork interface with subnetworks for solving a coupled nonlinear \n\
                      electric power grid and water pipe problem.\n\
                      The available solver options are in the ex1options file \n\
                      and the data files are in the datafiles of subdirectories.\n\
                      This example shows the use of subnetwork feature in DMNetwork. \n\
                      Run this program: mpiexec -n <n> ./ex1 \n\\n";

/* T
   Concepts: DMNetwork
   Concepts: PETSc SNES solver
*/

#include "power/power.h"
#include "water/water.h"

typedef struct{
  UserCtx_Power appctx_power;
  AppCtx_Water  appctx_water;
  PetscInt      subsnes_id; /* snes solver id */
  PetscInt      it;         /* iteration number */
  Vec           localXold;  /* store previous solution, used by FormFunction_Dummy() */
} UserCtx;

PetscErrorCode UserMonitor(SNES snes,PetscInt its,PetscReal fnorm ,void *appctx)
{
  PetscErrorCode ierr;
  UserCtx        *user = (UserCtx*)appctx;
  Vec            X,localXold = user->localXold;
  DM             networkdm;
  PetscMPIInt    rank;
  MPI_Comm       comm;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)snes,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);
#if 0
  if (!rank) {
    PetscInt       subsnes_id = user->subsnes_id;
    if (subsnes_id == 2) {
      ierr = PetscPrintf(PETSC_COMM_SELF," it %D, subsnes_id %D, fnorm %g\n",user->it,user->subsnes_id,(double)fnorm);CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(PETSC_COMM_SELF,"       subsnes_id %D, fnorm %g\n",user->subsnes_id,(double)fnorm);CHKERRQ(ierr);
    }
  }
#endif
  ierr = SNESGetSolution(snes,&X);CHKERRQ(ierr);
  ierr = SNESGetDM(snes,&networkdm);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(networkdm,X,INSERT_VALUES,localXold);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(networkdm,X,INSERT_VALUES,localXold);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode FormJacobian_subPower(SNES snes,Vec X, Mat J,Mat Jpre,void *appctx)
{
  PetscErrorCode ierr;
  DM             networkdm;
  Vec            localX;
  PetscInt       nv,ne,i,j,offset,nvar,row;
  const PetscInt *vtx,*edges;
  PetscBool      ghostvtex;
  PetscScalar    one = 1.0;
  PetscMPIInt    rank;
  MPI_Comm       comm;

  PetscFunctionBegin;
  ierr = SNESGetDM(snes,&networkdm);CHKERRQ(ierr);
  ierr = DMGetLocalVector(networkdm,&localX);CHKERRQ(ierr);

  ierr = PetscObjectGetComm((PetscObject)networkdm,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  ierr = DMGlobalToLocalBegin(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  ierr = MatZeroEntries(J);CHKERRQ(ierr);

  /* Power subnetwork: copied from power/FormJacobian_Power() */
  ierr = DMNetworkGetSubnetwork(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = FormJacobian_Power_private(networkdm,localX,J,nv,ne,vtx,edges,appctx);CHKERRQ(ierr);

  /* Water subnetwork: Identity */
  ierr = DMNetworkGetSubnetwork(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  for (i=0; i<nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghostvtex);CHKERRQ(ierr);
    if (ghostvtex) continue;

    ierr = DMNetworkGetGlobalVecOffset(networkdm,vtx[i],ALL_COMPONENTS,&offset);CHKERRQ(ierr);
    ierr = DMNetworkGetComponent(networkdm,vtx[i],ALL_COMPONENTS,NULL,NULL,&nvar);CHKERRQ(ierr);
    for (j=0; j<nvar; j++) {
      row = offset + j;
      ierr = MatSetValues(J,1,&row,1,&row,&one,ADD_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  ierr = DMRestoreLocalVector(networkdm,&localX);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Dummy equation localF(X) = localX - localXold */
PetscErrorCode FormFunction_Dummy(DM networkdm,Vec localX, Vec localF,PetscInt nv,PetscInt ne,const PetscInt* vtx,const PetscInt* edges,void* appctx)
{
  PetscErrorCode    ierr;
  const PetscScalar *xarr,*xoldarr;
  PetscScalar       *farr;
  PetscInt          i,j,offset,nvar;
  PetscBool         ghostvtex;
  UserCtx           *user = (UserCtx*)appctx;
  Vec               localXold = user->localXold;

  PetscFunctionBegin;
  ierr = VecGetArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecGetArrayRead(localXold,&xoldarr);CHKERRQ(ierr);
  ierr = VecGetArray(localF,&farr);CHKERRQ(ierr);

  for (i=0; i<nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghostvtex);CHKERRQ(ierr);
    if (ghostvtex) continue;

    ierr = DMNetworkGetLocalVecOffset(networkdm,vtx[i],ALL_COMPONENTS,&offset);CHKERRQ(ierr);
    ierr = DMNetworkGetComponent(networkdm,vtx[i],ALL_COMPONENTS,NULL,NULL,&nvar);CHKERRQ(ierr);
    for (j=0; j<nvar; j++) {
      farr[offset+j] = xarr[offset+j] - xoldarr[offset+j];
    }
  }

  ierr = VecRestoreArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(localXold,&xoldarr);CHKERRQ(ierr);
  ierr = VecRestoreArray(localF,&farr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode FormFunction(SNES snes,Vec X,Vec F,void *appctx)
{
  PetscErrorCode ierr;
  DM             networkdm;
  Vec            localX,localF;
  PetscInt       nv,ne,v;
  const PetscInt *vtx,*edges;
  PetscMPIInt    rank;
  MPI_Comm       comm;
  UserCtx        *user = (UserCtx*)appctx;
  UserCtx_Power  appctx_power = (*user).appctx_power;
  AppCtx_Water   appctx_water = (*user).appctx_water;

  PetscFunctionBegin;
  ierr = SNESGetDM(snes,&networkdm);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject)networkdm,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRMPI(ierr);

  ierr = DMGetLocalVector(networkdm,&localX);CHKERRQ(ierr);
  ierr = DMGetLocalVector(networkdm,&localF);CHKERRQ(ierr);
  ierr = VecSet(F,0.0);CHKERRQ(ierr);
  ierr = VecSet(localF,0.0);CHKERRQ(ierr);

  ierr = DMGlobalToLocalBegin(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  /* Form Function for power subnetwork */
  ierr = DMNetworkGetSubnetwork(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (user->subsnes_id == 1) { /* snes_water only */
    ierr = FormFunction_Dummy(networkdm,localX,localF,nv,ne,vtx,edges,user);CHKERRQ(ierr);
  } else {
    ierr = FormFunction_Power(networkdm,localX,localF,nv,ne,vtx,edges,&appctx_power);CHKERRQ(ierr);
  }

  /* Form Function for water subnetwork */
  ierr = DMNetworkGetSubnetwork(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (user->subsnes_id == 0) { /* snes_power only */
    ierr = FormFunction_Dummy(networkdm,localX,localF,nv,ne,vtx,edges,user);CHKERRQ(ierr);
  } else {
    ierr = FormFunction_Water(networkdm,localX,localF,nv,ne,vtx,edges,NULL);CHKERRQ(ierr);
  }

  /* Illustrate how to access the coupling vertex of the subnetworks without doing anything to F yet */
  ierr = DMNetworkGetSharedVertices(networkdm,&nv,&vtx);CHKERRQ(ierr);
  for (v=0; v<nv; v++) {
    PetscInt       key,ncomp,nvar,nconnedges,k,e,keye,goffset[3];
    void*          component;
    const PetscInt *connedges;

    ierr = DMNetworkGetComponent(networkdm,vtx[v],ALL_COMPONENTS,NULL,NULL,&nvar);CHKERRQ(ierr);
    ierr = DMNetworkGetNumComponents(networkdm,vtx[v],&ncomp);CHKERRQ(ierr);
    /* printf("  [%d] coupling vertex[%D]: v %D, ncomp %D; nvar %D\n",rank,v,vtx[v], ncomp,nvar); */

    for (k=0; k<ncomp; k++) {
      ierr = DMNetworkGetComponent(networkdm,vtx[v],k,&key,&component,&nvar);CHKERRQ(ierr);
      ierr = DMNetworkGetGlobalVecOffset(networkdm,vtx[v],k,&goffset[k]);CHKERRQ(ierr);

      /* Verify the coupling vertex is a powernet load vertex or a water vertex */
      switch (k) {
      case 0:
        if (key != appctx_power.compkey_bus || nvar != 2) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"key %D not a power bus vertex or nvar %D != 2",key,nvar);
        break;
      case 1:
        if (key != appctx_power.compkey_load || nvar != 0 || goffset[1] != goffset[0]+2) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Not a power load vertex");
        break;
      case 2:
        if (key != appctx_water.compkey_vtx || nvar != 1 || goffset[2] != goffset[1]) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Not a water vertex");
        break;
      default: SETERRQ1(PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "k %D is wrong",k);
      }
      /* printf("  [%d] coupling vertex[%D]: key %D; nvar %D, goffset %D\n",rank,v,key,nvar,goffset[k]); */
    }

    /* Get its supporting edges */
    ierr = DMNetworkGetSupportingEdges(networkdm,vtx[v],&nconnedges,&connedges);CHKERRQ(ierr);
    /* printf("\n[%d] coupling vertex: nconnedges %D\n",rank,nconnedges);CHKERRQ(ierr); */
    for (k=0; k<nconnedges; k++) {
      e = connedges[k];
      ierr = DMNetworkGetNumComponents(networkdm,e,&ncomp);CHKERRQ(ierr);
      /* printf("\n  [%d] connected edge[%D]=%D has ncomp %D\n",rank,k,e,ncomp); */
      ierr = DMNetworkGetComponent(networkdm,e,0,&keye,&component,NULL);CHKERRQ(ierr);
      if (keye == appctx_water.compkey_edge) { /* water_compkey_edge */
        EDGE_Water        edge=(EDGE_Water)component;
        if (edge->type == EDGE_TYPE_PUMP) {
          /* printf("  connected edge[%D]=%D has keye=%D, is appctx_water.compkey_edge with EDGE_TYPE_PUMP\n",k,e,keye); */
        }
      } else { /* ower->compkey_branch */
        if (keye != appctx_power.compkey_branch) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Not a power branch");
      }
    }
  }

  ierr = DMRestoreLocalVector(networkdm,&localX);CHKERRQ(ierr);

  ierr = DMLocalToGlobalBegin(networkdm,localF,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(networkdm,localF,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(networkdm,&localF);CHKERRQ(ierr);
#if 0
  if (!rank) printf("F:\n");
  ierr = VecView(F,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
#endif
  PetscFunctionReturn(0);
}

PetscErrorCode SetInitialGuess(DM networkdm,Vec X,void* appctx)
{
  PetscErrorCode ierr;
  PetscInt       nv,ne,i,j,ncomp,offset,key;
  const PetscInt *vtx,*edges;
  UserCtx        *user = (UserCtx*)appctx;
  Vec            localX = user->localXold;
  UserCtx_Power  appctx_power = (*user).appctx_power;
  AppCtx_Water   appctx_water = (*user).appctx_water;
  PetscBool      ghost;
  PetscScalar    *xarr;
  VERTEX_Power   bus;
  VERTEX_Water   vertex;
  void*          component;
  GEN            gen;

  PetscFunctionBegin;
  ierr = VecSet(X,0.0);CHKERRQ(ierr);
  ierr = VecSet(localX,0.0);CHKERRQ(ierr);

  /* Set initial guess for power subnetwork */
  ierr = DMNetworkGetSubnetwork(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = SetInitialGuess_Power(networkdm,localX,nv,ne,vtx,edges,&appctx_power);CHKERRQ(ierr);

  /* Set initial guess for water subnetwork */
  ierr = DMNetworkGetSubnetwork(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = SetInitialGuess_Water(networkdm,localX,nv,ne,vtx,edges,NULL);CHKERRQ(ierr);

  /* Set initial guess at the coupling vertex */
  ierr = VecGetArray(localX,&xarr);CHKERRQ(ierr);
  ierr = DMNetworkGetSharedVertices(networkdm,&nv,&vtx);CHKERRQ(ierr);
  for (i=0; i<nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghost);CHKERRQ(ierr);
    if (ghost) continue;

    ierr = DMNetworkGetNumComponents(networkdm,vtx[i],&ncomp);CHKERRQ(ierr);
    for (j=0; j<ncomp; j++) {
      ierr = DMNetworkGetLocalVecOffset(networkdm,vtx[i],j,&offset);CHKERRQ(ierr);
      ierr = DMNetworkGetComponent(networkdm,vtx[i],j,&key,(void**)&component,NULL);CHKERRQ(ierr);
      if (key == appctx_power.compkey_bus) {
        bus = (VERTEX_Power)(component);
        xarr[offset]   = bus->va*PETSC_PI/180.0;
        xarr[offset+1] = bus->vm;
      } else if (key == appctx_power.compkey_gen) {
        gen = (GEN)(component);
        if (!gen->status) continue;
        xarr[offset+1] = gen->vs;
      } else if (key == appctx_water.compkey_vtx) {
        vertex = (VERTEX_Water)(component);
        if (vertex->type == VERTEX_TYPE_JUNCTION) {
          xarr[offset] = 100;
        } else if (vertex->type == VERTEX_TYPE_RESERVOIR) {
          xarr[offset] = vertex->res.head;
        } else {
          xarr[offset] = vertex->tank.initlvl + vertex->tank.elev;
        }
      }
    }
  }
  ierr = VecRestoreArray(localX,&xarr);CHKERRQ(ierr);

  ierr = DMLocalToGlobalBegin(networkdm,localX,ADD_VALUES,X);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(networkdm,localX,ADD_VALUES,X);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc,char **argv)
{
  PetscErrorCode   ierr;
  DM               networkdm;
  PetscLogStage    stage[4];
  PetscMPIInt      rank,size;
  PetscInt         Nsubnet=2,numVertices[2],numEdges[2],i,j,nv,ne,it_max=10;
  const PetscInt   *vtx,*edges;
  Vec              X,F;
  SNES             snes,snes_power,snes_water;
  Mat              Jac;
  PetscBool        ghost,viewJ=PETSC_FALSE,viewX=PETSC_FALSE,viewDM=PETSC_FALSE,test=PETSC_FALSE,distribute=PETSC_TRUE,flg;
  UserCtx          user;
  SNESConvergedReason reason;

  /* Power subnetwork */
  UserCtx_Power       *appctx_power  = &user.appctx_power;
  char                pfdata_file[PETSC_MAX_PATH_LEN] = "power/case9.m";
  PFDATA              *pfdata = NULL;
  PetscInt            genj,loadj,*edgelist_power = NULL,power_netnum;
  PetscScalar         Sbase = 0.0;

  /* Water subnetwork */
  AppCtx_Water        *appctx_water = &user.appctx_water;
  WATERDATA           *waterdata = NULL;
  char                waterdata_file[PETSC_MAX_PATH_LEN] = "water/sample1.inp";
  PetscInt            *edgelist_water = NULL,water_netnum;

  /* Shared vertices between subnetworks */
  PetscInt           power_svtx,water_svtx;

  ierr = PetscInitialize(&argc,&argv,"ex1options",help);if (ierr) return ierr;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRMPI(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRMPI(ierr);

  /* (1) Read Data - Only rank 0 reads the data */
  /*--------------------------------------------*/
  ierr = PetscLogStageRegister("Read Data",&stage[0]);CHKERRQ(ierr);
  ierr = PetscLogStagePush(stage[0]);CHKERRQ(ierr);

  for (i=0; i<Nsubnet; i++) {
    numVertices[i] = 0;
    numEdges[i]    = 0;
  }

  /* All processes READ THE DATA FOR THE FIRST SUBNETWORK: Electric Power Grid */
  /* Used for shared vertex, because currently the coupling info must be available in all processes!!! */
  ierr = PetscOptionsGetString(NULL,NULL,"-pfdata",pfdata_file,PETSC_MAX_PATH_LEN-1,NULL);CHKERRQ(ierr);
  ierr = PetscNew(&pfdata);CHKERRQ(ierr);
  ierr = PFReadMatPowerData(pfdata,pfdata_file);CHKERRQ(ierr);
  if (!rank) {
    ierr = PetscPrintf(PETSC_COMM_SELF,"Power network: nb = %D, ngen = %D, nload = %D, nbranch = %D\n",pfdata->nbus,pfdata->ngen,pfdata->nload,pfdata->nbranch);CHKERRQ(ierr);
  }
  Sbase = pfdata->sbase;
  if (rank == 0) { /* proc[0] will create Electric Power Grid */
    numEdges[0]    = pfdata->nbranch;
    numVertices[0] = pfdata->nbus;

    ierr = PetscMalloc1(2*numEdges[0],&edgelist_power);CHKERRQ(ierr);
    ierr = GetListofEdges_Power(pfdata,edgelist_power);CHKERRQ(ierr);
  }
  /* Broadcast power Sbase to all processors */
  ierr = MPI_Bcast(&Sbase,1,MPIU_SCALAR,0,PETSC_COMM_WORLD);CHKERRMPI(ierr);
  appctx_power->Sbase     = Sbase;
  appctx_power->jac_error = PETSC_FALSE;
  /* If external option activated. Introduce error in jacobian */
  ierr = PetscOptionsHasName(NULL,NULL, "-jac_error", &appctx_power->jac_error);CHKERRQ(ierr);

  /* All processes READ THE DATA FOR THE SECOND SUBNETWORK: Water */
  /* Used for shared vertex, because currently the coupling info must be available in all processes!!! */
  ierr = PetscNew(&waterdata);CHKERRQ(ierr);
  ierr = PetscOptionsGetString(NULL,NULL,"-waterdata",waterdata_file,PETSC_MAX_PATH_LEN-1,NULL);CHKERRQ(ierr);
  ierr = WaterReadData(waterdata,waterdata_file);CHKERRQ(ierr);
  if (size == 1 || (size > 1 && rank == 1)) {
    ierr = PetscCalloc1(2*waterdata->nedge,&edgelist_water);CHKERRQ(ierr);
    ierr = GetListofEdges_Water(waterdata,edgelist_water);CHKERRQ(ierr);
    numEdges[1]    = waterdata->nedge;
    numVertices[1] = waterdata->nvertex;
  }
  PetscLogStagePop();

  /* (2) Create a network consist of two subnetworks */
  /*-------------------------------------------------*/
  ierr = PetscLogStageRegister("Net Setup",&stage[1]);CHKERRQ(ierr);
  ierr = PetscLogStagePush(stage[1]);CHKERRQ(ierr);

  ierr = PetscOptionsGetBool(NULL,NULL,"-viewDM",&viewDM,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-test",&test,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-distribute",&distribute,NULL);CHKERRQ(ierr);

  /* Create an empty network object */
  ierr = DMNetworkCreate(PETSC_COMM_WORLD,&networkdm);CHKERRQ(ierr);

  /* Register the components in the network */
  ierr = DMNetworkRegisterComponent(networkdm,"branchstruct",sizeof(struct _p_EDGE_Power),&appctx_power->compkey_branch);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"busstruct",sizeof(struct _p_VERTEX_Power),&appctx_power->compkey_bus);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"genstruct",sizeof(struct _p_GEN),&appctx_power->compkey_gen);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"loadstruct",sizeof(struct _p_LOAD),&appctx_power->compkey_load);CHKERRQ(ierr);

  ierr = DMNetworkRegisterComponent(networkdm,"edge_water",sizeof(struct _p_EDGE_Water),&appctx_water->compkey_edge);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"vertex_water",sizeof(struct _p_VERTEX_Water),&appctx_water->compkey_vtx);CHKERRQ(ierr);
#if 0
  ierr = PetscPrintf(PETSC_COMM_WORLD,"power->compkey_branch %d\n",appctx_power->compkey_branch);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"power->compkey_bus    %d\n",appctx_power->compkey_bus);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"power->compkey_gen    %d\n",appctx_power->compkey_gen);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"power->compkey_load   %d\n",appctx_power->compkey_load);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"water->compkey_edge   %d\n",appctx_water->compkey_edge);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"water->compkey_vtx    %d\n",appctx_water->compkey_vtx);CHKERRQ(ierr);
#endif
  ierr = PetscSynchronizedPrintf(PETSC_COMM_WORLD,"[%d] Total local nvertices %D + %D = %D, nedges %D + %D = %D\n",rank,numVertices[0],numVertices[1],numVertices[0]+numVertices[1],numEdges[0],numEdges[1],numEdges[0]+numEdges[1]);CHKERRQ(ierr);
  ierr = PetscSynchronizedFlush(PETSC_COMM_WORLD,PETSC_STDOUT);CHKERRQ(ierr);

  ierr = DMNetworkSetNumSubNetworks(networkdm,PETSC_DECIDE,Nsubnet);CHKERRQ(ierr);
  ierr = DMNetworkAddSubnetwork(networkdm,"power",numVertices[0],numEdges[0],edgelist_power,&power_netnum);CHKERRQ(ierr);
  ierr = DMNetworkAddSubnetwork(networkdm,"water",numVertices[1],numEdges[1],edgelist_water,&water_netnum);CHKERRQ(ierr);

  /* vertex subnet[0].4 shares with vertex subnet[1].0 */
  power_svtx = 4; water_svtx = 0;
  ierr = DMNetworkAddSharedVertices(networkdm,power_netnum,water_netnum,1,&power_svtx,&water_svtx);CHKERRQ(ierr);

  /* Set up the network layout */
  ierr = DMNetworkLayoutSetUp(networkdm);CHKERRQ(ierr);

  /* ADD VARIABLES AND COMPONENTS FOR THE POWER SUBNETWORK */
  /*-------------------------------------------------------*/
  genj = 0; loadj = 0;
  ierr = DMNetworkGetSubnetwork(networkdm,power_netnum,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);

  for (i = 0; i < ne; i++) {
    ierr = DMNetworkAddComponent(networkdm,edges[i],appctx_power->compkey_branch,&pfdata->branch[i],0);CHKERRQ(ierr);
  }

  for (i = 0; i < nv; i++) {
    ierr = DMNetworkIsSharedVertex(networkdm,vtx[i],&flg);CHKERRQ(ierr);
    if (flg) continue;

    ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_power->compkey_bus,&pfdata->bus[i],2);CHKERRQ(ierr);
    if (pfdata->bus[i].ngen) {
      for (j = 0; j < pfdata->bus[i].ngen; j++) {
        ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_power->compkey_gen,&pfdata->gen[genj++],0);CHKERRQ(ierr);
      }
    }
    if (pfdata->bus[i].nload) {
      for (j=0; j < pfdata->bus[i].nload; j++) {
        ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_power->compkey_load,&pfdata->load[loadj++],0);CHKERRQ(ierr);
      }
    }
  }

  /* ADD VARIABLES AND COMPONENTS FOR THE WATER SUBNETWORK */
  /*-------------------------------------------------------*/
  ierr = DMNetworkGetSubnetwork(networkdm,water_netnum,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  for (i = 0; i < ne; i++) {
    ierr = DMNetworkAddComponent(networkdm,edges[i],appctx_water->compkey_edge,&waterdata->edge[i],0);CHKERRQ(ierr);
  }

  for (i = 0; i < nv; i++) {
    ierr = DMNetworkIsSharedVertex(networkdm,vtx[i],&flg);CHKERRQ(ierr);
    if (flg) continue;

    ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_water->compkey_vtx,&waterdata->vertex[i],1);CHKERRQ(ierr);
  }

  /* ADD VARIABLES AND COMPONENTS AT THE SHARED VERTEX: net[0].4 coupls with net[1].0 -- only the owner of the vertex does this */
  /*----------------------------------------------------------------------------------------------------------------------------*/
  ierr = DMNetworkGetSharedVertices(networkdm,&nv,&vtx);CHKERRQ(ierr);
  for (i = 0; i < nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghost);CHKERRQ(ierr);
    /* printf("[%d] coupling info: nv %d; sv[0] %d; ghost %d\n",rank,nv,vtx[0],ghost);CHKERRQ(ierr); */
    if (ghost) continue;

    /* power */
    ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_power->compkey_bus,&pfdata->bus[4],2);CHKERRQ(ierr);
    /* bus[4] is a load, add its component */
    ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_power->compkey_load,&pfdata->load[0],0);CHKERRQ(ierr);

    /* water */
    ierr = DMNetworkAddComponent(networkdm,vtx[i],appctx_water->compkey_vtx,&waterdata->vertex[0],1);CHKERRQ(ierr);
  }

  /* Set up DM for use */
  ierr = DMSetUp(networkdm);CHKERRQ(ierr);
  if (viewDM) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"\nAfter DMSetUp, DMView:\n");CHKERRQ(ierr);
    ierr = DMView(networkdm,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }

  /* Free user objects */
  ierr = PetscFree(edgelist_power);CHKERRQ(ierr);
  ierr = PetscFree(pfdata->bus);CHKERRQ(ierr);
  ierr = PetscFree(pfdata->gen);CHKERRQ(ierr);
  ierr = PetscFree(pfdata->branch);CHKERRQ(ierr);
  ierr = PetscFree(pfdata->load);CHKERRQ(ierr);
  ierr = PetscFree(pfdata);CHKERRQ(ierr);

  ierr = PetscFree(edgelist_water);CHKERRQ(ierr);
  ierr = PetscFree(waterdata->vertex);CHKERRQ(ierr);
  ierr = PetscFree(waterdata->edge);CHKERRQ(ierr);
  ierr = PetscFree(waterdata);CHKERRQ(ierr);

  /* Re-distribute networkdm to multiple processes for better job balance */
  if (size >1 && distribute) {
    ierr = DMNetworkDistribute(&networkdm,0);CHKERRQ(ierr);
    if (viewDM) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"\nAfter DMNetworkDistribute, DMView:\n");CHKERRQ(ierr);
      ierr = DMView(networkdm,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
    }
  }

  /* Test DMNetworkGetSubnetwork() and DMNetworkGetSubnetworkSharedVertices() */
  if (test) {
    PetscInt  v,gidx;
    ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRMPI(ierr);
    for (i=0; i<Nsubnet; i++) {
      ierr = DMNetworkGetSubnetwork(networkdm,i,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
      ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] After distribute, subnet[%d] ne %d, nv %d\n",rank,i,ne,nv);
      ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRMPI(ierr);

      for (v=0; v<nv; v++) {
        ierr = DMNetworkIsGhostVertex(networkdm,vtx[v],&ghost);CHKERRQ(ierr);
        ierr = DMNetworkGetGlobalVertexIndex(networkdm,vtx[v],&gidx);CHKERRQ(ierr);
        ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] subnet[%d] v %d %d; ghost %d\n",rank,i,vtx[v],gidx,ghost);
      }
      ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRMPI(ierr);
    }
    ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRMPI(ierr);

    ierr = DMNetworkGetSharedVertices(networkdm,&nv,&vtx);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] After distribute, num of shared vertices nsv = %d\n",rank,nv);
    for (v=0; v<nv; v++) {
      ierr = DMNetworkGetGlobalVertexIndex(networkdm,vtx[v],&gidx);CHKERRQ(ierr);
      ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] sv %d, gidx=%d\n",rank,vtx[v],gidx);
    }
    ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRMPI(ierr);
  }

  /* Create solution vector X */
  ierr = DMCreateGlobalVector(networkdm,&X);CHKERRQ(ierr);
  ierr = VecDuplicate(X,&F);CHKERRQ(ierr);
  ierr = DMGetLocalVector(networkdm,&user.localXold);CHKERRQ(ierr);
  PetscLogStagePop();

  /* (3) Setup Solvers */
  /*-------------------*/
  ierr = PetscOptionsGetBool(NULL,NULL,"-viewJ",&viewJ,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-viewX",&viewX,NULL);CHKERRQ(ierr);

  ierr = PetscLogStageRegister("SNES Setup",&stage[2]);CHKERRQ(ierr);
  ierr = PetscLogStagePush(stage[2]);CHKERRQ(ierr);

  ierr = SetInitialGuess(networkdm,X,&user);CHKERRQ(ierr);

  /* Create coupled snes */
  /*-------------------- */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"SNES_coupled setup ......\n");CHKERRQ(ierr);
  user.subsnes_id = Nsubnet;
  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);
  ierr = SNESSetDM(snes,networkdm);CHKERRQ(ierr);
  ierr = SNESSetOptionsPrefix(snes,"coupled_");CHKERRQ(ierr);
  ierr = SNESSetFunction(snes,F,FormFunction,&user);CHKERRQ(ierr);
  ierr = SNESMonitorSet(snes,UserMonitor,&user,NULL);CHKERRQ(ierr);
  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  if (viewJ) {
    /* View Jac structure */
    ierr = SNESGetJacobian(snes,&Jac,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = MatView(Jac,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
  }

  if (viewX) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Solution:\n");CHKERRQ(ierr);
    ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }

  if (viewJ) {
    /* View assembled Jac */
    ierr = MatView(Jac,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
  }

  /* Create snes_power */
  /*-------------------*/
  ierr = PetscPrintf(PETSC_COMM_WORLD,"SNES_power setup ......\n");CHKERRQ(ierr);

  user.subsnes_id = 0;
  ierr = SNESCreate(PETSC_COMM_WORLD,&snes_power);CHKERRQ(ierr);
  ierr = SNESSetDM(snes_power,networkdm);CHKERRQ(ierr);
  ierr = SNESSetOptionsPrefix(snes_power,"power_");CHKERRQ(ierr);
  ierr = SNESSetFunction(snes_power,F,FormFunction,&user);CHKERRQ(ierr);
  ierr = SNESMonitorSet(snes_power,UserMonitor,&user,NULL);CHKERRQ(ierr);

  /* Use user-provide Jacobian */
  ierr = DMCreateMatrix(networkdm,&Jac);CHKERRQ(ierr);
  ierr = SNESSetJacobian(snes_power,Jac,Jac,FormJacobian_subPower,&user);CHKERRQ(ierr);
  ierr = SNESSetFromOptions(snes_power);CHKERRQ(ierr);

  if (viewX) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Power Solution:\n");CHKERRQ(ierr);
    ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }
  if (viewJ) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Power Jac:\n");CHKERRQ(ierr);
    ierr = SNESGetJacobian(snes_power,&Jac,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = MatView(Jac,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
    /* ierr = MatView(Jac,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr); */
  }

  /* Create snes_water */
  /*-------------------*/
  ierr = PetscPrintf(PETSC_COMM_WORLD,"SNES_water setup......\n");CHKERRQ(ierr);

  user.subsnes_id = 1;
  ierr = SNESCreate(PETSC_COMM_WORLD,&snes_water);CHKERRQ(ierr);
  ierr = SNESSetDM(snes_water,networkdm);CHKERRQ(ierr);
  ierr = SNESSetOptionsPrefix(snes_water,"water_");CHKERRQ(ierr);
  ierr = SNESSetFunction(snes_water,F,FormFunction,&user);CHKERRQ(ierr);
  ierr = SNESMonitorSet(snes_water,UserMonitor,&user,NULL);CHKERRQ(ierr);
  ierr = SNESSetFromOptions(snes_water);CHKERRQ(ierr);

  if (viewX) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Water Solution:\n");CHKERRQ(ierr);
    ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }
  ierr = PetscLogStagePop();CHKERRQ(ierr);

  /* (4) Solve */
  /*-----------*/
  ierr = PetscLogStageRegister("SNES Solve",&stage[3]);CHKERRQ(ierr);
  ierr = PetscLogStagePush(stage[3]);CHKERRQ(ierr);
  user.it = 0;
  reason  = SNES_DIVERGED_DTOL;
  while (user.it < it_max && (PetscInt)reason<0) {
#if 0
    user.subsnes_id = 0;
    ierr = SNESSolve(snes_power,NULL,X);CHKERRQ(ierr);

    user.subsnes_id = 1;
    ierr = SNESSolve(snes_water,NULL,X);CHKERRQ(ierr);
#endif
    user.subsnes_id = Nsubnet;
    ierr = SNESSolve(snes,NULL,X);CHKERRQ(ierr);

    ierr = SNESGetConvergedReason(snes,&reason);CHKERRQ(ierr);
    user.it++;
  }
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Coupled_SNES converged in %D iterations\n",user.it);CHKERRQ(ierr);
  if (viewX) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Final Solution:\n");CHKERRQ(ierr);
    ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }
  ierr = PetscLogStagePop();CHKERRQ(ierr);

  /* Free objects */
  /* -------------*/
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = VecDestroy(&F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(networkdm,&user.localXold);CHKERRQ(ierr);

  ierr = SNESDestroy(&snes);CHKERRQ(ierr);
  ierr = MatDestroy(&Jac);CHKERRQ(ierr);
  ierr = SNESDestroy(&snes_power);CHKERRQ(ierr);
  ierr = SNESDestroy(&snes_water);CHKERRQ(ierr);

  ierr = DMDestroy(&networkdm);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

   build:
     requires: !complex double define(PETSC_HAVE_ATTRIBUTEALIGNED)
     depends: power/PFReadData.c power/pffunctions.c water/waterreaddata.c water/waterfunctions.c

   test:
      args: -coupled_snes_converged_reason -options_left no -viewDM
      localrunfiles: ex1options power/case9.m water/sample1.inp
      output_file: output/ex1.out

   test:
      suffix: 2
      nsize: 3
      args: -coupled_snes_converged_reason -options_left no -petscpartitioner_type parmetis
      localrunfiles: ex1options power/case9.m water/sample1.inp
      output_file: output/ex1_2.out
      requires: parmetis

#   test:
#      suffix: 3
#      nsize: 3
#      args: -coupled_snes_converged_reason -options_left no -distribute false
#      localrunfiles: ex1options power/case9.m water/sample1.inp
#      output_file: output/ex1_2.out

   test:
      suffix: 4
      nsize: 4
      args: -coupled_snes_converged_reason -options_left no -petscpartitioner_type simple -viewDM
      localrunfiles: ex1options power/case9.m water/sample1.inp
      output_file: output/ex1_4.out

TEST*/
