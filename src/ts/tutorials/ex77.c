static char help[] = "Time-dependent reactive low Mach Flow in 2d and 3d channels with finite elements.\n\
We solve the reactive low Mach flow problem in a rectangular domain\n\
using a parallel unstructured mesh (DMPLEX) to discretize the flow\n\
and particles (DWSWARM) to discretize the chemical species.\n\n\n";

/*F
This low Mach flow is time-dependent isoviscous Navier-Stokes flow. We discretize using the
finite element method on an unstructured mesh. The weak form equations are

\begin{align*}
    < q, \nabla\cdot u > = 0
    <v, du/dt> + <v, u \cdot \nabla u> + < \nabla v, \nu (\nabla u + {\nabla u}^T) > - < \nabla\cdot v, p >  - < v, f  >  = 0
    < w, u \cdot \nabla T > + < \nabla w, \alpha \nabla T > - < w, Q > = 0
\end{align*}

where $\nu$ is the kinematic viscosity and $\alpha$ is thermal diffusivity.

For visualization, use

  -dm_view hdf5:$PWD/sol.h5 -sol_vec_view hdf5:$PWD/sol.h5::append -exact_vec_view hdf5:$PWD/sol.h5::append

The particles can be visualized using

  -part_dm_view draw -part_dm_view_swarm_radius 0.03

F*/

#include <petscdmplex.h>
#include <petscdmswarm.h>
#include <petscts.h>
#include <petscds.h>
#include <petscbag.h>

typedef enum {SOL_TRIG_TRIG, NUM_SOL_TYPES} SolType;
const char *solTypes[NUM_SOL_TYPES+1] = {"trig_trig",  "unknown"};

typedef enum {PART_LAYOUT_CELL, PART_LAYOUT_BOX, NUM_PART_LAYOUT_TYPES} PartLayoutType;
const char *partLayoutTypes[NUM_PART_LAYOUT_TYPES+1] = {"cell", "box",  "unknown"};

typedef struct {
  PetscReal nu;    /* Kinematic viscosity */
  PetscReal alpha; /* Thermal diffusivity */
  PetscReal T_in;  /* Inlet temperature*/
  PetscReal omega; /* Rotation speed in MMS benchmark */
} Parameter;

typedef struct {
  /* Problem definition */
  PetscBag       bag;          /* Holds problem parameters */
  SolType        solType;      /* MMS solution type */
  PartLayoutType partLayout;   /* Type of particle distribution */
  PetscInt       Npc;          /* The initial number of particles per cell */
  PetscReal      partLower[3]; /* Lower left corner of particle box */
  PetscReal      partUpper[3]; /* Upper right corner of particle box */
  PetscInt       Npb;          /* The initial number of particles per box dimension */
} AppCtx;

typedef struct {
  PetscReal ti; /* The time for ui, at the beginning of the advection solve */
  PetscReal tf; /* The time for uf, at the end of the advection solve */
  Vec       ui; /* The PDE solution field at ti */
  Vec       uf; /* The PDE solution field at tf */
  Vec       x0; /* The initial particle positions at t = 0 */
  PetscErrorCode (*exact)(PetscInt, PetscReal, const PetscReal[], PetscInt, PetscScalar *, void *);
  AppCtx   *ctx; /* Context for exact solution */
} AdvCtx;

static PetscErrorCode zero(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nc, PetscScalar *u, void *ctx)
{
  PetscInt d;
  for (d = 0; d < Nc; ++d) u[d] = 0.0;
  return 0;
}

static PetscErrorCode constant(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nc, PetscScalar *u, void *ctx)
{
  PetscInt d;
  for (d = 0; d < Nc; ++d) u[d] = 1.0;
  return 0;
}

/*
  CASE: trigonometric-trigonometric
  In 2D we use exact solution:

    x = r0 cos(w t + theta0)  r0     = sqrt(x0^2 + y0^2)
    y = r0 sin(w t + theta0)  theta0 = arctan(y0/x0)
    u = -w r0 sin(theta0) = -w y
    v =  w r0 cos(theta0) =  w x
    p = x + y - 1
    T = t + x + y
    f = <1, 1>
    Q = 1 + w (x - y)/r

  so that

    \nabla \cdot u = 0 + 0 = 0

  f = du/dt + u \cdot \nabla u - \nu \Delta u + \nabla p
    = <0, 0> + u_i d_i u_j - \nu 0 + <1, 1>
    = <1, 1> + w^2 <-y, x> . <<0, 1>, <-1, 0>>
    = <1, 1> + w^2 <-x, -y>
    = <1, 1> - w^2 <x, y>

  Q = dT/dt + u \cdot \nabla T - \alpha \Delta T
    = 1 + <u, v> . <1, 1> - \alpha 0
    = 1 + u + v
*/
static PetscErrorCode trig_trig_x(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *x, void *ctx)
{
  const PetscReal x0     = X[0];
  const PetscReal y0     = X[1];
  const PetscReal R0     = PetscSqrtReal(x0*x0 + y0*y0);
  const PetscReal theta0 = PetscAtan2Real(y0, x0);
  Parameter      *p      = (Parameter *) ctx;

  x[0] = R0*PetscCosReal(p->omega*time + theta0);
  x[1] = R0*PetscSinReal(p->omega*time + theta0);
  return 0;
}
static PetscErrorCode trig_trig_u(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx)
{
  Parameter *p = (Parameter *) ctx;

  u[0] = -p->omega*X[1];
  u[1] =  p->omega*X[0];
  return 0;
}
static PetscErrorCode trig_trig_u_t(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx)
{
  u[0] = 0.0;
  u[1] = 0.0;
  return 0;
}

static PetscErrorCode trig_trig_p(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *p, void *ctx)
{
  p[0] = X[0] + X[1] - 1.0;
  return 0;
}

static PetscErrorCode trig_trig_T(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx)
{
  T[0] = time + X[0] + X[1];
  return 0;
}
static PetscErrorCode trig_trig_T_t(PetscInt dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx)
{
  T[0] = 1.0;
  return 0;
}

static void f0_trig_trig_v(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                           const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                           const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                           PetscReal t, const PetscReal X[], PetscInt numConstants, const PetscScalar constants[], PetscScalar f0[])
{
  const PetscReal omega = PetscRealPart(constants[3]);
  PetscInt        Nc    = dim;
  PetscInt        c, d;

  for (d = 0; d < dim; ++d) f0[d] = u_t[uOff[0]+d];

  for (c = 0; c < Nc; ++c) {
    for (d = 0; d < dim; ++d) f0[c] += u[d]*u_x[c*dim+d];
  }
  f0[0] -= 1.0 - omega*omega*X[0];
  f0[1] -= 1.0 - omega*omega*X[1];
}

static void f0_trig_trig_w(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                           const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                           const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                           PetscReal t, const PetscReal X[], PetscInt numConstants, const PetscScalar constants[], PetscScalar f0[])
{
  const PetscReal omega = PetscRealPart(constants[3]);
  PetscInt        d;

  for (d = 0, f0[0] = 0; d < dim; ++d) f0[0] += u[uOff[0]+d]*u_x[uOff_x[2]+d];
  f0[0] += u_t[uOff[2]] - (1.0 + omega*(X[0] - X[1]));
}

static void f0_q(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                 const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                 const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                 PetscReal t, const PetscReal X[], PetscInt numConstants, const PetscScalar constants[], PetscScalar f0[])
{
  PetscInt d;
  for (d = 0, f0[0] = 0.0; d < dim; ++d) f0[0] += u_x[d*dim+d];
}

/*f1_v = \nu[grad(u) + grad(u)^T] - pI */
static void f1_v(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                 const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                 const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                 PetscReal t, const PetscReal X[], PetscInt numConstants, const PetscScalar constants[], PetscScalar f1[])
{
  const PetscReal nu = PetscRealPart(constants[0]);
  const PetscInt    Nc = dim;
  PetscInt        c, d;

  for (c = 0; c < Nc; ++c) {
    for (d = 0; d < dim; ++d) {
      f1[c*dim+d] = nu*(u_x[c*dim+d] + u_x[d*dim+c]);
    }
    f1[c*dim+c] -= u[uOff[1]];
  }
}

static void f1_w(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                 const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                 const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                 PetscReal t, const PetscReal X[], PetscInt numConstants, const PetscScalar constants[], PetscScalar f1[])
{
  const PetscReal alpha = PetscRealPart(constants[1]);
  PetscInt d;
  for (d = 0; d < dim; ++d) f1[d] = alpha*u_x[uOff_x[2]+d];
}

/*Jacobians*/
static void g1_qu(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                 const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                 const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                 PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g1[])
{
  PetscInt d;
  for (d = 0; d < dim; ++d) g1[d*dim+d] = 1.0;
}

static void g0_vu(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g0[])
{
  PetscInt c, d;
  const PetscInt  Nc = dim;

  for (d = 0; d < dim; ++d) g0[d*dim+d] = u_tShift;

  for (c = 0; c < Nc; ++c) {
    for (d = 0; d < dim; ++d) {
      g0[c*Nc+d] += u_x[c*Nc+d];
    }
  }
}

static void g1_vu(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g1[])
{
  PetscInt NcI = dim;
  PetscInt NcJ = dim;
  PetscInt c, d, e;

  for (c = 0; c < NcI; ++c) {
    for (d = 0; d < NcJ; ++d) {
      for (e = 0; e < dim; ++e) {
        if (c == d) {
          g1[(c*NcJ+d)*dim+e] += u[e];
        }
      }
    }
  }
}


static void g2_vp(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g2[])
{
  PetscInt d;
  for (d = 0; d < dim; ++d) g2[d*dim+d] = -1.0;
}

static void g3_vu(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g3[])
{
   const PetscReal nu = PetscRealPart(constants[0]);
   const PetscInt  Nc = dim;
   PetscInt        c, d;

  for (c = 0; c < Nc; ++c) {
    for (d = 0; d < dim; ++d) {
      g3[((c*Nc+c)*dim+d)*dim+d] += nu; // gradU
      g3[((c*Nc+d)*dim+d)*dim+c] += nu; // gradU transpose
    }
  }
}

static void g0_wT(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g0[])
{
  PetscInt d;
  for (d = 0; d < dim; ++d) g0[d] = u_tShift;
}

static void g0_wu(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g0[])
{
  PetscInt d;
  for (d = 0; d < dim; ++d) g0[d] = u_x[uOff_x[2]+d];
}

static void g1_wT(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g1[])
{
  PetscInt d;
  for (d = 0; d < dim; ++d) g1[d] = u[uOff[0]+d];
}

static void g3_wT(PetscInt dim, PetscInt Nf, PetscInt NfAux,
                  const PetscInt uOff[], const PetscInt uOff_x[], const PetscScalar u[], const PetscScalar u_t[], const PetscScalar u_x[],
                  const PetscInt aOff[], const PetscInt aOff_x[], const PetscScalar a[], const PetscScalar a_t[], const PetscScalar a_x[],
                  PetscReal t, PetscReal u_tShift, const PetscReal x[], PetscInt numConstants, const PetscScalar constants[], PetscScalar g3[])
{
  const PetscReal alpha = PetscRealPart(constants[1]);
  PetscInt               d;

  for (d = 0; d < dim; ++d) g3[d*dim+d] = alpha;
}

static PetscErrorCode ProcessOptions(MPI_Comm comm, AppCtx *options)
{
  PetscInt       sol, pl, n;
  PetscErrorCode ierr;

  PetscFunctionBeginUser;
  options->solType    = SOL_TRIG_TRIG;
  options->partLayout = PART_LAYOUT_CELL;
  options->Npc        = 1;
  options->Npb        = 1;

  options->partLower[0] = options->partLower[1] = options->partLower[2] = 0.;
  options->partUpper[0] = options->partUpper[1] = options->partUpper[2] = 1.;
  ierr = PetscOptionsBegin(comm, "", "Low Mach flow Problem Options", "DMPLEX");CHKERRQ(ierr);
  sol  = options->solType;
  ierr = PetscOptionsEList("-sol_type", "The solution type", "ex77.c", solTypes, NUM_SOL_TYPES, solTypes[options->solType], &sol, NULL);CHKERRQ(ierr);
  options->solType = (SolType) sol;
  pl   = options->partLayout;
  ierr = PetscOptionsEList("-part_layout_type", "The particle layout type", "ex77.c", partLayoutTypes, NUM_PART_LAYOUT_TYPES, partLayoutTypes[options->partLayout], &pl, NULL);CHKERRQ(ierr);
  options->partLayout = (PartLayoutType) pl;
  ierr = PetscOptionsInt("-Npc", "The initial number of particles per cell", "ex77.c", options->Npc, &options->Npc, NULL);CHKERRQ(ierr);
  n    = 3;
  ierr = PetscOptionsRealArray("-part_lower", "The lower left corner of the particle box", "ex77.c", options->partLower, &n, NULL);CHKERRQ(ierr);
  n    = 3;
  ierr = PetscOptionsRealArray("-part_upper", "The upper right corner of the particle box", "ex77.c", options->partUpper, &n, NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-Npb", "The initial number of particles per box dimension", "ex77.c", options->Npb, &options->Npb, NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd();
  PetscFunctionReturn(0);
}

static PetscErrorCode SetupParameters(AppCtx *user)
{
  PetscBag       bag;
  Parameter     *p;
  PetscErrorCode ierr;

  PetscFunctionBeginUser;
  /* setup PETSc parameter bag */
  ierr = PetscBagGetData(user->bag, (void **) &p);CHKERRQ(ierr);
  ierr = PetscBagSetName(user->bag, "par", "Low Mach flow parameters");CHKERRQ(ierr);
  bag  = user->bag;
  ierr = PetscBagRegisterReal(bag, &p->nu,    1.0, "nu",    "Kinematic viscosity");CHKERRQ(ierr);
  ierr = PetscBagRegisterReal(bag, &p->alpha, 1.0, "alpha", "Thermal diffusivity");CHKERRQ(ierr);
  ierr = PetscBagRegisterReal(bag, &p->T_in,  1.0, "T_in",  "Inlet temperature");CHKERRQ(ierr);
  ierr = PetscBagRegisterReal(bag, &p->omega, 1.0, "omega", "Rotation speed in MMS benchmark");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode CreateMesh(MPI_Comm comm, AppCtx *user, DM *dm)
{
  PetscErrorCode ierr;

  PetscFunctionBeginUser;
  ierr = DMPlexCreateBoxMesh(comm, 2, PETSC_TRUE, NULL, NULL, NULL, NULL, PETSC_TRUE, dm);CHKERRQ(ierr);
  ierr = DMSetFromOptions(*dm);CHKERRQ(ierr);
  ierr = DMViewFromOptions(*dm, NULL, "-dm_view");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SetupProblem(DM dm, AppCtx *user)
{
  PetscErrorCode (*exactFuncs[3])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
  PetscErrorCode (*exactFuncs_t[3])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
  PetscDS          prob;
  DMLabel          label;
  Parameter       *ctx;
  PetscInt         id;
  PetscErrorCode   ierr;

  PetscFunctionBeginUser;
  ierr = DMGetLabel(dm, "marker", &label);CHKERRQ(ierr);
  ierr = DMGetDS(dm, &prob);CHKERRQ(ierr);
  switch(user->solType){
  case SOL_TRIG_TRIG:
    ierr = PetscDSSetResidual(prob, 0, f0_trig_trig_v, f1_v);CHKERRQ(ierr);
    ierr = PetscDSSetResidual(prob, 2, f0_trig_trig_w, f1_w);CHKERRQ(ierr);

    exactFuncs[0]   = trig_trig_u;
    exactFuncs[1]   = trig_trig_p;
    exactFuncs[2]   = trig_trig_T;
    exactFuncs_t[0] = trig_trig_u_t;
    exactFuncs_t[1] = NULL;
    exactFuncs_t[2] = trig_trig_T_t;
    break;
   default: SETERRQ2(PetscObjectComm((PetscObject) prob), PETSC_ERR_ARG_WRONG, "Unsupported solution type: %s (%D)", solTypes[PetscMin(user->solType, NUM_SOL_TYPES)], user->solType);
  }

  ierr = PetscDSSetResidual(prob, 1, f0_q, NULL);CHKERRQ(ierr);

  ierr = PetscDSSetJacobian(prob, 0, 0, g0_vu, g1_vu,  NULL,  g3_vu);CHKERRQ(ierr);
  ierr = PetscDSSetJacobian(prob, 0, 1, NULL, NULL,  g2_vp, NULL);CHKERRQ(ierr);
  ierr = PetscDSSetJacobian(prob, 1, 0, NULL, g1_qu, NULL,  NULL);CHKERRQ(ierr);
  ierr = PetscDSSetJacobian(prob, 2, 0, g0_wu, NULL, NULL,  NULL);CHKERRQ(ierr);
  ierr = PetscDSSetJacobian(prob, 2, 2, g0_wT, g1_wT, NULL,  g3_wT);CHKERRQ(ierr);
  /* Setup constants */
  {
    Parameter  *param;
    PetscScalar constants[4];

    ierr = PetscBagGetData(user->bag, (void **) &param);CHKERRQ(ierr);

    constants[0] = param->nu;
    constants[1] = param->alpha;
    constants[2] = param->T_in;
    constants[3] = param->omega;
    ierr = PetscDSSetConstants(prob, 4, constants);CHKERRQ(ierr);
  }
  /* Setup Boundary Conditions */
  ierr = PetscBagGetData(user->bag, (void **) &ctx);CHKERRQ(ierr);
  id   = 3;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "top wall velocity",    label, 1, &id, 0, 0, NULL, (void (*)(void)) exactFuncs[0], (void (*)(void)) exactFuncs_t[0], ctx, NULL);CHKERRQ(ierr);
  id   = 1;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "bottom wall velocity", label, 1, &id, 0, 0, NULL, (void (*)(void)) exactFuncs[0], (void (*)(void)) exactFuncs_t[0], ctx, NULL);CHKERRQ(ierr);
  id   = 2;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "right wall velocity",  label, 1, &id, 0, 0, NULL, (void (*)(void)) exactFuncs[0], (void (*)(void)) exactFuncs_t[0], ctx, NULL);CHKERRQ(ierr);
  id   = 4;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "left wall velocity",   label, 1, &id, 0, 0, NULL, (void (*)(void)) exactFuncs[0], (void (*)(void)) exactFuncs_t[0], ctx, NULL);CHKERRQ(ierr);
  id   = 3;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "top wall temp",    label, 1, &id, 2, 0, NULL, (void (*)(void)) exactFuncs[2], (void (*)(void)) exactFuncs_t[2], ctx, NULL);CHKERRQ(ierr);
  id   = 1;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "bottom wall temp", label, 1, &id, 2, 0, NULL, (void (*)(void)) exactFuncs[2], (void (*)(void)) exactFuncs_t[2], ctx, NULL);CHKERRQ(ierr);
  id   = 2;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "right wall temp",  label, 1, &id, 2, 0, NULL, (void (*)(void)) exactFuncs[2], (void (*)(void)) exactFuncs_t[2], ctx, NULL);CHKERRQ(ierr);
  id   = 4;
  ierr = PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "left wall temp",   label, 1, &id, 2, 0, NULL, (void (*)(void)) exactFuncs[2], (void (*)(void)) exactFuncs_t[2], ctx, NULL);CHKERRQ(ierr);

  /*setup exact solution.*/
  ierr = PetscDSSetExactSolution(prob, 0, exactFuncs[0], ctx);CHKERRQ(ierr);
  ierr = PetscDSSetExactSolution(prob, 1, exactFuncs[1], ctx);CHKERRQ(ierr);
  ierr = PetscDSSetExactSolution(prob, 2, exactFuncs[2], ctx);CHKERRQ(ierr);
  ierr = PetscDSSetExactSolutionTimeDerivative(prob, 0, exactFuncs_t[0], ctx);CHKERRQ(ierr);
  ierr = PetscDSSetExactSolutionTimeDerivative(prob, 1, exactFuncs_t[1], ctx);CHKERRQ(ierr);
  ierr = PetscDSSetExactSolutionTimeDerivative(prob, 2, exactFuncs_t[2], ctx);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* x_t = v

   Note that here we use the velocity field at t_{n+1} to advect the particles from
   t_n to t_{n+1}. If we use both of these fields, we could use Crank-Nicholson or
   the method of characteristics.
*/
static PetscErrorCode FreeStreaming(TS ts, PetscReal t, Vec X, Vec F, void *ctx)
{
  AdvCtx             *adv = (AdvCtx *) ctx;
  Vec                 u   = adv->ui;
  DM                  sdm, dm, vdm;
  Vec                 vel, locvel, pvel;
  IS                  vis;
  DMInterpolationInfo ictx;
  const PetscScalar  *coords, *v;
  PetscScalar        *f;
  PetscInt            vf[1] = {0};
  PetscInt            dim, Np;
  PetscErrorCode      ierr;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts, &sdm);CHKERRQ(ierr);
  ierr = DMSwarmGetCellDM(sdm, &dm);CHKERRQ(ierr);
  ierr = DMGetGlobalVector(sdm, &pvel);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(sdm, &Np);CHKERRQ(ierr);
  ierr = DMGetDimension(dm, &dim);CHKERRQ(ierr);
  /* Get local velocity */
  ierr = DMCreateSubDM(dm, 1, vf, &vis, &vdm);CHKERRQ(ierr);
  ierr = VecGetSubVector(u, vis, &vel);CHKERRQ(ierr);
  ierr = DMGetLocalVector(vdm, &locvel);CHKERRQ(ierr);
  ierr = DMPlexInsertBoundaryValues(vdm, PETSC_TRUE, locvel, adv->ti, NULL, NULL, NULL);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(vdm, vel, INSERT_VALUES, locvel);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(vdm, vel, INSERT_VALUES, locvel);CHKERRQ(ierr);
  ierr = VecRestoreSubVector(u, vis, &vel);CHKERRQ(ierr);
  ierr = ISDestroy(&vis);CHKERRQ(ierr);
  /* Interpolate velocity */
  ierr = DMInterpolationCreate(PETSC_COMM_SELF, &ictx);CHKERRQ(ierr);
  ierr = DMInterpolationSetDim(ictx, dim);CHKERRQ(ierr);
  ierr = DMInterpolationSetDof(ictx, dim);CHKERRQ(ierr);
  ierr = VecGetArrayRead(X, &coords);CHKERRQ(ierr);
  ierr = DMInterpolationAddPoints(ictx, Np, (PetscReal *) coords);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(X, &coords);CHKERRQ(ierr);
  /* Particles that lie outside the domain should be dropped,
     whereas particles that move to another partition should trigger a migration */
  ierr = DMInterpolationSetUp(ictx, vdm, PETSC_FALSE, PETSC_TRUE);CHKERRQ(ierr);
  ierr = VecSet(pvel, 0.);CHKERRQ(ierr);
  ierr = DMInterpolationEvaluate(ictx, vdm, locvel, pvel);CHKERRQ(ierr);
  ierr = DMInterpolationDestroy(&ictx);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(vdm, &locvel);CHKERRQ(ierr);
  ierr = DMDestroy(&vdm);CHKERRQ(ierr);

  ierr = VecGetArray(F, &f);CHKERRQ(ierr);
  ierr = VecGetArrayRead(pvel, &v);CHKERRQ(ierr);
  ierr = PetscArraycpy(f, v, Np*dim);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(pvel, &v);CHKERRQ(ierr);
  ierr = VecRestoreArray(F, &f);CHKERRQ(ierr);
  ierr = DMRestoreGlobalVector(sdm, &pvel);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SetInitialParticleConditions(TS ts, Vec u)
{
  AppCtx        *user;
  void          *ctx;
  DM             dm;
  PetscScalar   *coords;
  PetscReal      x[3], dx[3];
  PetscInt       n[3];
  PetscInt       Np, dim, d, i, j, k;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = TSGetApplicationContext(ts, &ctx);CHKERRQ(ierr);
  user = ((AdvCtx *) ctx)->ctx;
  ierr = TSGetDM(ts, &dm);CHKERRQ(ierr);
  ierr = DMGetDimension(dm, &dim);CHKERRQ(ierr);
  switch (user->partLayout) {
    case PART_LAYOUT_CELL:
      ierr = DMSwarmSetPointCoordinatesRandom(dm, user->Npc);CHKERRQ(ierr);
      break;
    case PART_LAYOUT_BOX:
      Np = 1;
      for (d = 0; d < dim; ++d) {
        n[d]  = user->Npb;
        dx[d] = (user->partUpper[d] - user->partLower[d])/PetscMax(1, n[d] - 1);
        Np   *= n[d];
      }
      ierr = VecGetArray(u, &coords);CHKERRQ(ierr);
      switch (dim) {
        case 2:
          x[0] = user->partLower[0];
          for (i = 0; i < n[0]; ++i, x[0] += dx[0]) {
            x[1] = user->partLower[1];
            for (j = 0; j < n[1]; ++j, x[1] += dx[1]) {
              const PetscInt p = j*n[0] + i;
              for (d = 0; d < dim; ++d) coords[p*dim + d] = x[d];
            }
          }
          break;
        case 3:
          x[0] = user->partLower[0];
          for (i = 0; i < n[0]; ++i, x[0] += dx[0]) {
            x[1] = user->partLower[1];
            for (j = 0; j < n[1]; ++j, x[1] += dx[1]) {
              x[2] = user->partLower[2];
              for (k = 0; k < n[2]; ++k, x[2] += dx[2]) {
                const PetscInt p = (k*n[1] + j)*n[0] + i;
                for (d = 0; d < dim; ++d) coords[p*dim + d] = x[d];
              }
            }
          }
          break;
        default: SETERRQ1(PetscObjectComm((PetscObject) ts), PETSC_ERR_SUP, "Do not support particle layout in dimension %D", dim);
      }
      ierr = VecRestoreArray(u, &coords);CHKERRQ(ierr);
      break;
    default: SETERRQ1(PetscObjectComm((PetscObject) ts), PETSC_ERR_ARG_WRONG, "Invalid particle layout type %s", partLayoutTypes[PetscMin(user->partLayout, NUM_PART_LAYOUT_TYPES)]);
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SetupDiscretization(DM dm, DM sdm, AppCtx *user)
{
  DM              cdm = dm;
  PetscFE         fe[3];
  Parameter      *param;
  PetscInt       *cellid, n[3];
  PetscReal       x[3], dx[3];
  PetscScalar    *coords;
  DMPolytopeType  ct;
  PetscInt        dim, d, cStart, cEnd, c, Np, p, i, j, k;
  PetscBool       simplex;
  MPI_Comm        comm;
  PetscErrorCode  ierr;

  PetscFunctionBeginUser;
  ierr = DMGetDimension(dm, &dim);CHKERRQ(ierr);
  ierr = DMPlexGetHeightStratum(dm, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = DMPlexGetCellType(dm, cStart, &ct);CHKERRQ(ierr);
  simplex = DMPolytopeTypeGetNumVertices(ct) == DMPolytopeTypeGetDim(ct)+1 ? PETSC_TRUE : PETSC_FALSE;
  /* Create finite element */
  ierr = PetscObjectGetComm((PetscObject) dm, &comm);CHKERRQ(ierr);
  ierr = PetscFECreateDefault(comm, dim, dim, simplex, "vel_", PETSC_DEFAULT, &fe[0]);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) fe[0], "velocity");CHKERRQ(ierr);

  ierr = PetscFECreateDefault(comm, dim, 1, simplex, "pres_", PETSC_DEFAULT, &fe[1]);CHKERRQ(ierr);
  ierr = PetscFECopyQuadrature(fe[0], fe[1]);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) fe[1], "pressure");CHKERRQ(ierr);

  ierr = PetscFECreateDefault(comm, dim, 1, simplex, "temp_", PETSC_DEFAULT, &fe[2]);CHKERRQ(ierr);
  ierr = PetscFECopyQuadrature(fe[0], fe[2]);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) fe[2], "temperature");CHKERRQ(ierr);

  /* Set discretization and boundary conditions for each mesh */
  ierr = DMSetField(dm, 0, NULL, (PetscObject) fe[0]);CHKERRQ(ierr);
  ierr = DMSetField(dm, 1, NULL, (PetscObject) fe[1]);CHKERRQ(ierr);
  ierr = DMSetField(dm, 2, NULL, (PetscObject) fe[2]);CHKERRQ(ierr);
  ierr = DMCreateDS(dm);CHKERRQ(ierr);
  ierr = SetupProblem(dm, user);CHKERRQ(ierr);
  ierr = PetscBagGetData(user->bag, (void **) &param);CHKERRQ(ierr);
  while (cdm) {
    ierr = DMCopyDisc(dm, cdm);CHKERRQ(ierr);
    ierr = DMGetCoarseDM(cdm, &cdm);CHKERRQ(ierr);
  }
  ierr = PetscFEDestroy(&fe[0]);CHKERRQ(ierr);
  ierr = PetscFEDestroy(&fe[1]);CHKERRQ(ierr);
  ierr = PetscFEDestroy(&fe[2]);CHKERRQ(ierr);

  {
    PetscObject  pressure;
    MatNullSpace nullspacePres;

    ierr = DMGetField(dm, 1, NULL, &pressure);CHKERRQ(ierr);
    ierr = MatNullSpaceCreate(PetscObjectComm(pressure), PETSC_TRUE, 0, NULL, &nullspacePres);CHKERRQ(ierr);
    ierr = PetscObjectCompose(pressure, "nullspace", (PetscObject) nullspacePres);CHKERRQ(ierr);
    ierr = MatNullSpaceDestroy(&nullspacePres);CHKERRQ(ierr);
  }

  /* Setup particle information */
  ierr = DMSwarmSetType(sdm, DMSWARM_PIC);CHKERRQ(ierr);
  ierr = DMSwarmRegisterPetscDatatypeField(sdm, "mass", 1, PETSC_REAL);CHKERRQ(ierr);
  ierr = DMSwarmFinalizeFieldRegister(sdm);CHKERRQ(ierr);
  switch (user->partLayout) {
    case PART_LAYOUT_CELL:
      ierr = DMSwarmSetLocalSizes(sdm, (cEnd - cStart) * user->Npc, 0);CHKERRQ(ierr);
      ierr = DMSetFromOptions(sdm);CHKERRQ(ierr);
      ierr = DMSwarmGetField(sdm, DMSwarmPICField_cellid, NULL, NULL, (void **) &cellid);CHKERRQ(ierr);
      for (c = cStart; c < cEnd; ++c) {
        for (p = 0; p < user->Npc; ++p) {
          const PetscInt n = c*user->Npc + p;

          cellid[n] = c;
        }
      }
      ierr = DMSwarmRestoreField(sdm, DMSwarmPICField_cellid, NULL, NULL, (void **) &cellid);CHKERRQ(ierr);
      ierr = DMSwarmSetPointCoordinatesRandom(sdm, user->Npc);CHKERRQ(ierr);
      break;
    case PART_LAYOUT_BOX:
      Np = 1;
      for (d = 0; d < dim; ++d) {
        n[d]  = user->Npb;
        dx[d] = (user->partUpper[d] - user->partLower[d])/PetscMax(1, n[d] - 1);
        Np   *= n[d];
      }
      ierr = DMSwarmSetLocalSizes(sdm, Np, 0);CHKERRQ(ierr);
      ierr = DMSetFromOptions(sdm);CHKERRQ(ierr);
      ierr = DMSwarmGetField(sdm, DMSwarmPICField_coor, NULL, NULL, (void **) &coords);CHKERRQ(ierr);
      switch (dim) {
        case 2:
          x[0] = user->partLower[0];
          for (i = 0; i < n[0]; ++i, x[0] += dx[0]) {
            x[1] = user->partLower[1];
            for (j = 0; j < n[1]; ++j, x[1] += dx[1]) {
              const PetscInt p = j*n[0] + i;
              for (d = 0; d < dim; ++d) coords[p*dim + d] = x[d];
            }
          }
          break;
        case 3:
          x[0] = user->partLower[0];
          for (i = 0; i < n[0]; ++i, x[0] += dx[0]) {
            x[1] = user->partLower[1];
            for (j = 0; j < n[1]; ++j, x[1] += dx[1]) {
              x[2] = user->partLower[2];
              for (k = 0; k < n[2]; ++k, x[2] += dx[2]) {
                const PetscInt p = (k*n[1] + j)*n[0] + i;
                for (d = 0; d < dim; ++d) coords[p*dim + d] = x[d];
              }
            }
          }
          break;
        default: SETERRQ1(comm, PETSC_ERR_SUP, "Do not support particle layout in dimension %D", dim);
      }
      ierr = DMSwarmRestoreField(sdm, DMSwarmPICField_coor, NULL, NULL, (void **) &coords);CHKERRQ(ierr);
      ierr = DMSwarmGetField(sdm, DMSwarmPICField_cellid, NULL, NULL, (void **) &cellid);CHKERRQ(ierr);
      for (p = 0; p < Np; ++p) cellid[p] = 0;
      ierr = DMSwarmRestoreField(sdm, DMSwarmPICField_cellid, NULL, NULL, (void **) &cellid);CHKERRQ(ierr);
      ierr = DMSwarmMigrate(sdm, PETSC_TRUE);CHKERRQ(ierr);
      break;
    default: SETERRQ1(comm, PETSC_ERR_ARG_WRONG, "Invalid particle layout type %s", partLayoutTypes[PetscMin(user->partLayout, NUM_PART_LAYOUT_TYPES)]);
  }
  ierr = PetscObjectSetName((PetscObject) sdm, "Particles");CHKERRQ(ierr);
  ierr = DMViewFromOptions(sdm, NULL, "-dm_view");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode CreatePressureNullSpace(DM dm, PetscInt ofield, PetscInt nfield, MatNullSpace *nullSpace)
{
  Vec              vec;
  PetscErrorCode (*funcs[3])(PetscInt, PetscReal, const PetscReal[], PetscInt, PetscScalar *, void *) = {zero, zero, zero};
  PetscErrorCode   ierr;

  PetscFunctionBeginUser;
  if (ofield != 1) SETERRQ1(PetscObjectComm((PetscObject) dm), PETSC_ERR_ARG_WRONG, "Nullspace must be for pressure field at index 1, not %D", ofield);
  funcs[nfield] = constant;
  ierr = DMCreateGlobalVector(dm, &vec);CHKERRQ(ierr);
  ierr = DMProjectFunction(dm, 0.0, funcs, NULL, INSERT_ALL_VALUES, vec);CHKERRQ(ierr);
  ierr = VecNormalize(vec, NULL);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) vec, "Pressure Null Space");CHKERRQ(ierr);
  ierr = VecViewFromOptions(vec, NULL, "-pressure_nullspace_view");CHKERRQ(ierr);
  ierr = MatNullSpaceCreate(PetscObjectComm((PetscObject) dm), PETSC_FALSE, 1, &vec, nullSpace);CHKERRQ(ierr);
  ierr = VecDestroy(&vec);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode RemoveDiscretePressureNullspace_Private(TS ts, Vec u)
{
  DM             dm;
  MatNullSpace   nullsp;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = TSGetDM(ts, &dm);CHKERRQ(ierr);
  ierr = CreatePressureNullSpace(dm, 1, 1, &nullsp);CHKERRQ(ierr);
  ierr = MatNullSpaceRemove(nullsp, u);CHKERRQ(ierr);
  ierr = MatNullSpaceDestroy(&nullsp);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* Make the discrete pressure discretely divergence free */
static PetscErrorCode RemoveDiscretePressureNullspace(TS ts)
{
  Vec            u;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = TSGetSolution(ts, &u);CHKERRQ(ierr);
  ierr = RemoveDiscretePressureNullspace_Private(ts, u);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SetInitialConditions(TS ts, Vec u)
{
  DM             dm;
  PetscReal      t;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = TSGetDM(ts, &dm);CHKERRQ(ierr);
  ierr = TSGetTime(ts, &t);CHKERRQ(ierr);
  ierr = DMComputeExactSolution(dm, t, u, NULL);CHKERRQ(ierr);
  ierr = RemoveDiscretePressureNullspace_Private(ts, u);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode MonitorError(TS ts, PetscInt step, PetscReal crtime, Vec u, void *ctx)
{
  PetscErrorCode (*exactFuncs[3])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
  void            *ctxs[3];
  DM               dm;
  PetscDS          ds;
  Vec              v;
  PetscReal        ferrors[3];
  PetscInt         tl, l, f;
  PetscErrorCode   ierr;

  PetscFunctionBeginUser;
  ierr = TSGetDM(ts, &dm);CHKERRQ(ierr);
  ierr = DMGetDS(dm, &ds);CHKERRQ(ierr);

  for (f = 0; f < 3; ++f) {ierr = PetscDSGetExactSolution(ds, f, &exactFuncs[f], &ctxs[f]);CHKERRQ(ierr);}
  ierr = DMComputeL2FieldDiff(dm, crtime, exactFuncs, ctxs, u, ferrors);CHKERRQ(ierr);
  ierr = PetscObjectGetTabLevel((PetscObject) ts, &tl);CHKERRQ(ierr);
  for (l = 0; l < tl; ++l) {ierr = PetscPrintf(PETSC_COMM_WORLD, "\t");CHKERRQ(ierr);}
  ierr = PetscPrintf(PETSC_COMM_WORLD, "Timestep: %04d time = %-8.4g \t L_2 Error: [%2.3g, %2.3g, %2.3g]\n", (int) step, (double) crtime, (double) ferrors[0], (double) ferrors[1], (double) ferrors[2]);CHKERRQ(ierr);

  ierr = DMGetGlobalVector(dm, &u);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) u, "Numerical Solution");CHKERRQ(ierr);
  ierr = VecViewFromOptions(u, NULL, "-sol_vec_view");CHKERRQ(ierr);
  ierr = DMRestoreGlobalVector(dm, &u);CHKERRQ(ierr);

  ierr = DMGetGlobalVector(dm, &v);CHKERRQ(ierr);
  ierr = DMProjectFunction(dm, 0.0, exactFuncs, ctxs, INSERT_ALL_VALUES, v);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) v, "Exact Solution");CHKERRQ(ierr);
  ierr = VecViewFromOptions(v, NULL, "-exact_vec_view");CHKERRQ(ierr);
  ierr = DMRestoreGlobalVector(dm, &v);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

/* Note that adv->x0 will not be correct after migration */
static PetscErrorCode ComputeParticleError(TS ts, Vec u, Vec e)
{
  AdvCtx            *adv;
  DM                 sdm;
  Parameter         *param;
  const PetscScalar *xp0, *xp;
  PetscScalar       *ep;
  PetscReal          time;
  PetscInt           dim, Np, p;
  MPI_Comm           comm;
  PetscErrorCode     ierr;

  PetscFunctionBeginUser;
  ierr = TSGetTime(ts, &time);CHKERRQ(ierr);
  ierr = TSGetApplicationContext(ts, (void **) &adv);CHKERRQ(ierr);
  ierr = PetscBagGetData(adv->ctx->bag, (void **) &param);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject) ts, &comm);CHKERRQ(ierr);
  ierr = TSGetDM(ts, &sdm);CHKERRQ(ierr);
  ierr = DMGetDimension(sdm, &dim);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(sdm, &Np);CHKERRQ(ierr);
  ierr = VecGetArrayRead(adv->x0, &xp0);CHKERRQ(ierr);
  ierr = VecGetArrayRead(u, &xp);CHKERRQ(ierr);
  ierr = VecGetArrayWrite(e, &ep);CHKERRQ(ierr);
  for (p = 0; p < Np; ++p) {
    PetscScalar x[3];
    PetscReal   x0[3];
    PetscInt    d;

    for (d = 0; d < dim; ++d) x0[d] = PetscRealPart(xp0[p*dim+d]);
    ierr = adv->exact(dim, time, x0, 1, x, param);CHKERRQ(ierr);
    for (d = 0; d < dim; ++d) ep[p*dim+d] += x[d] - xp[p*dim+d];
  }
  ierr = VecRestoreArrayRead(adv->x0, &xp0);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(u, &xp);CHKERRQ(ierr);
  ierr = VecRestoreArrayWrite(e, &ep);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode MonitorParticleError(TS ts, PetscInt step, PetscReal time, Vec u, void *ctx)
{
  AdvCtx            *adv = (AdvCtx *) ctx;
  DM                 sdm;
  Parameter         *param;
  const PetscScalar *xp0, *xp;
  PetscReal          error = 0.0;
  PetscInt           dim, tl, l, Np, p;
  MPI_Comm           comm;
  PetscErrorCode     ierr;

  PetscFunctionBeginUser;
  ierr = PetscBagGetData(adv->ctx->bag, (void **) &param);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject) ts, &comm);CHKERRQ(ierr);
  ierr = TSGetDM(ts, &sdm);CHKERRQ(ierr);
  ierr = DMGetDimension(sdm, &dim);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(sdm, &Np);CHKERRQ(ierr);
  ierr = VecGetArrayRead(adv->x0, &xp0);CHKERRQ(ierr);
  ierr = VecGetArrayRead(u, &xp);CHKERRQ(ierr);
  for (p = 0; p < Np; ++p) {
    PetscScalar x[3];
    PetscReal   x0[3];
    PetscReal   perror = 0.0;
    PetscInt    d;

    for (d = 0; d < dim; ++d) x0[d] = PetscRealPart(xp0[p*dim+d]);
    ierr = adv->exact(dim, time, x0, 1, x, param);CHKERRQ(ierr);
    for (d = 0; d < dim; ++d) perror += PetscSqr(PetscRealPart(x[d] - xp[p*dim+d]));
    error += perror;
  }
  ierr = VecRestoreArrayRead(adv->x0, &xp0);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(u, &xp);CHKERRQ(ierr);
  ierr = PetscObjectGetTabLevel((PetscObject) ts, &tl);CHKERRQ(ierr);
  for (l = 0; l < tl; ++l) {ierr = PetscPrintf(PETSC_COMM_WORLD, "\t");CHKERRQ(ierr);}
  ierr = PetscPrintf(comm, "Timestep: %04d time = %-8.4g \t L_2 Particle Error: [%2.3g]\n", (int) step, (double) time, (double) error);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode AdvectParticles(TS ts)
{
  TS             sts;
  DM             sdm;
  Vec            coordinates;
  AdvCtx        *adv;
  PetscReal      time;
  PetscBool      lreset, reset;
  PetscInt       dim, n, N, newn, newN;
  PetscErrorCode ierr;

  PetscFunctionBeginUser;
  ierr = PetscObjectQuery((PetscObject) ts, "_SwarmTS",  (PetscObject *) &sts);CHKERRQ(ierr);
  ierr = TSGetDM(sts, &sdm);CHKERRQ(ierr);
  ierr = TSGetRHSFunction(sts, NULL, NULL, (void **) &adv);CHKERRQ(ierr);
  ierr = DMGetDimension(sdm, &dim);CHKERRQ(ierr);
  ierr = DMSwarmGetSize(sdm, &N);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(sdm, &n);CHKERRQ(ierr);
  ierr = DMSwarmCreateGlobalVectorFromField(sdm, DMSwarmPICField_coor, &coordinates);CHKERRQ(ierr);
  ierr = TSGetTime(ts, &time);CHKERRQ(ierr);
  ierr = TSSetMaxTime(sts, time);CHKERRQ(ierr);
  adv->tf = time;
  ierr = TSSolve(sts, coordinates);CHKERRQ(ierr);
  ierr = DMSwarmDestroyGlobalVectorFromField(sdm, DMSwarmPICField_coor, &coordinates);CHKERRQ(ierr);
  ierr = VecCopy(adv->uf, adv->ui);CHKERRQ(ierr);
  adv->ti = adv->tf;

  ierr = DMSwarmMigrate(sdm, PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMSwarmGetSize(sdm, &newN);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(sdm, &newn);CHKERRQ(ierr);
  lreset = (n != newn || N != newN) ? PETSC_TRUE : PETSC_FALSE;
  ierr = MPI_Allreduce(&lreset, &reset, 1, MPIU_BOOL, MPI_LOR, PetscObjectComm((PetscObject) sts));CHKERRMPI(ierr);
  if (reset) {
    ierr = TSReset(sts);CHKERRQ(ierr);
    ierr = DMSwarmVectorDefineField(sdm, DMSwarmPICField_coor);CHKERRQ(ierr);
  }
  ierr = DMViewFromOptions(sdm, NULL, "-dm_view");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc, char **argv)
{
  DM              dm, sdm;
  TS              ts, sts;
  Vec             u, xtmp;
  AppCtx          user;
  AdvCtx          adv;
  PetscReal       t;
  PetscInt        dim;
  PetscErrorCode  ierr;

  ierr = PetscInitialize(&argc, &argv, NULL,help);if (ierr) return ierr;
  ierr = ProcessOptions(PETSC_COMM_WORLD, &user);CHKERRQ(ierr);
  ierr = PetscBagCreate(PETSC_COMM_WORLD, sizeof(Parameter), &user.bag);CHKERRQ(ierr);
  ierr = SetupParameters(&user);CHKERRQ(ierr);
  ierr = TSCreate(PETSC_COMM_WORLD, &ts);CHKERRQ(ierr);
  ierr = CreateMesh(PETSC_COMM_WORLD, &user, &dm);CHKERRQ(ierr);
  ierr = TSSetDM(ts, dm);CHKERRQ(ierr);
  ierr = DMSetApplicationContext(dm, &user);CHKERRQ(ierr);
  /* Discretize chemical species */
  ierr = DMCreate(PETSC_COMM_WORLD, &sdm);CHKERRQ(ierr);
  ierr = PetscObjectSetOptionsPrefix((PetscObject) sdm, "part_");CHKERRQ(ierr);
  ierr = DMSetType(sdm, DMSWARM);CHKERRQ(ierr);
  ierr = DMGetDimension(dm, &dim);CHKERRQ(ierr);
  ierr = DMSetDimension(sdm, dim);CHKERRQ(ierr);
  ierr = DMSwarmSetCellDM(sdm, dm);CHKERRQ(ierr);
  /* Setup problem */
  ierr = SetupDiscretization(dm, sdm, &user);CHKERRQ(ierr);
  ierr = DMPlexCreateClosureIndex(dm, NULL);CHKERRQ(ierr);

  ierr = DMCreateGlobalVector(dm, &u);CHKERRQ(ierr);
  ierr = DMSetNullSpaceConstructor(dm, 1, CreatePressureNullSpace);CHKERRQ(ierr);

  ierr = DMTSSetBoundaryLocal(dm, DMPlexTSComputeBoundary, &user);CHKERRQ(ierr);
  ierr = DMTSSetIFunctionLocal(dm, DMPlexTSComputeIFunctionFEM, &user);CHKERRQ(ierr);
  ierr = DMTSSetIJacobianLocal(dm, DMPlexTSComputeIJacobianFEM, &user);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts, TS_EXACTFINALTIME_MATCHSTEP);CHKERRQ(ierr);
  ierr = TSSetPreStep(ts, RemoveDiscretePressureNullspace);CHKERRQ(ierr);
  ierr = TSMonitorSet(ts, MonitorError, &user, NULL);CHKERRQ(ierr);CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);

  ierr = TSSetComputeInitialCondition(ts, SetInitialConditions);CHKERRQ(ierr); /* Must come after SetFromOptions() */
  ierr = SetInitialConditions(ts, u);CHKERRQ(ierr);
  ierr = TSGetTime(ts, &t);CHKERRQ(ierr);
  ierr = DMSetOutputSequenceNumber(dm, 0, t);CHKERRQ(ierr);
  ierr = DMTSCheckFromOptions(ts, u);CHKERRQ(ierr);

  /* Setup particle position integrator */
  ierr = TSCreate(PETSC_COMM_WORLD, &sts);CHKERRQ(ierr);
  ierr = PetscObjectSetOptionsPrefix((PetscObject) sts, "part_");CHKERRQ(ierr);
  ierr = PetscObjectIncrementTabLevel((PetscObject) sts, (PetscObject) ts, 1);CHKERRQ(ierr);
  ierr = TSSetDM(sts, sdm);CHKERRQ(ierr);
  ierr = TSSetProblemType(sts, TS_NONLINEAR);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(sts, TS_EXACTFINALTIME_MATCHSTEP);CHKERRQ(ierr);
  ierr = TSMonitorSet(sts, MonitorParticleError, &adv, NULL);CHKERRQ(ierr);CHKERRQ(ierr);
  ierr = TSSetFromOptions(sts);CHKERRQ(ierr);
  ierr = TSSetApplicationContext(sts, &adv);CHKERRQ(ierr);
  ierr = TSSetComputeExactError(sts, ComputeParticleError);CHKERRQ(ierr);
  ierr = TSSetComputeInitialCondition(sts, SetInitialParticleConditions);CHKERRQ(ierr);
  adv.ti = t;
  adv.uf = u;
  ierr = VecDuplicate(adv.uf, &adv.ui);
  ierr = VecCopy(u, adv.ui);CHKERRQ(ierr);
  ierr = TSSetRHSFunction(sts, NULL, FreeStreaming, &adv);CHKERRQ(ierr);
  ierr = TSSetPostStep(ts, AdvectParticles);CHKERRQ(ierr);
  ierr = PetscObjectCompose((PetscObject) ts, "_SwarmTS", (PetscObject) sts);CHKERRQ(ierr);
  ierr = DMSwarmVectorDefineField(sdm, DMSwarmPICField_coor);CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(sdm, &adv.x0);CHKERRQ(ierr);
  ierr = DMSwarmCreateGlobalVectorFromField(sdm, DMSwarmPICField_coor, &xtmp);CHKERRQ(ierr);
  ierr = VecCopy(xtmp, adv.x0);CHKERRQ(ierr);
  ierr = DMSwarmDestroyGlobalVectorFromField(sdm, DMSwarmPICField_coor, &xtmp);CHKERRQ(ierr);
  switch(user.solType){
    case SOL_TRIG_TRIG: adv.exact = trig_trig_x;break;
    default: SETERRQ2(PetscObjectComm((PetscObject) sdm), PETSC_ERR_ARG_WRONG, "Unsupported solution type: %s (%D)", solTypes[PetscMin(user.solType, NUM_SOL_TYPES)], user.solType);
  }
  adv.ctx = &user;

  ierr = TSSolve(ts, u);CHKERRQ(ierr);
  ierr = DMTSCheckFromOptions(ts, u);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) u, "Numerical Solution");CHKERRQ(ierr);

  ierr = VecDestroy(&u);CHKERRQ(ierr);
  ierr = VecDestroy(&adv.x0);CHKERRQ(ierr);
  ierr = VecDestroy(&adv.ui);CHKERRQ(ierr);
  ierr = DMDestroy(&dm);CHKERRQ(ierr);
  ierr = DMDestroy(&sdm);CHKERRQ(ierr);
  ierr = TSDestroy(&ts);CHKERRQ(ierr);
  ierr = TSDestroy(&sts);CHKERRQ(ierr);
  ierr = PetscBagDestroy(&user.bag);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

  # Swarm does not work with complex
  test:
    suffix: 2d_tri_p2_p1_p1_tconvp
    requires: triangle !single !complex
    args: -dm_plex_separate_marker -sol_type trig_trig -dm_refine 2 \
      -vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 \
      -dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 -ts_monitor_cancel \
      -ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged \
      -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full \
        -fieldsplit_0_pc_type lu \
        -fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi \
      -omega 0.5 -part_layout_type box -part_lower 0.25,0.25 -part_upper 0.75,0.75 -Npb 5 \
      -part_ts_max_steps 2 -part_ts_dt 0.05 -part_ts_convergence_estimate -convest_num_refine 1 -part_ts_monitor_cancel
  test:
    suffix: 2d_tri_p2_p1_p1_exit
    requires: triangle !single !complex
    args: -dm_plex_separate_marker -sol_type trig_trig -dm_refine 1 \
      -vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 \
      -dmts_check .001 -ts_max_steps 10 -ts_dt 0.1 \
      -ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged \
      -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full \
        -fieldsplit_0_pc_type lu \
        -fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi \
      -omega 0.5 -part_layout_type box -part_lower 0.25,0.25 -part_upper 0.75,0.75 -Npb 5 \
      -part_ts_max_steps 20 -part_ts_dt 0.05

TEST*/
