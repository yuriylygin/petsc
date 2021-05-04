/*
 Implementation of the sequential hip vectors.

 This file contains the code that can be compiled with a C
 compiler.  The companion file vechip2.hip.cpp contains the code that
 must be compiled with hipcc compiler.
 */

#define PETSC_SKIP_SPINLOCK

#include <petscconf.h>
#include <petsc/private/vecimpl.h>          /*I <petscvec.h> I*/
#include <../src/vec/vec/impls/dvecimpl.h>
#include <petsc/private/hipvecimpl.h>

PetscErrorCode VecHIPGetArrays_Private(Vec v,const PetscScalar** x,const PetscScalar** x_d,PetscOffloadMask* flg)
{
  PetscCheckTypeNames(v,VECSEQHIP,VECMPIHIP);
  PetscFunctionBegin;
  if (x) {
    Vec_Seq *h = (Vec_Seq*)v->data;

    *x = h->array;
  }
  if (x_d) {
    Vec_HIP *d = (Vec_HIP*)v->spptr;

    *x_d = d ? d->GPUarray : NULL;
  }
  if (flg) *flg = v->offloadmask;
  PetscFunctionReturn(0);
}

/*
    Allocates space for the vector array on the Host if it does not exist.
    Does NOT change the PetscHIPFlag for the vector
    Does NOT zero the HIP array
 */
PETSC_EXTERN PetscErrorCode VecHIPAllocateCheckHost(Vec v)
{
  PetscErrorCode ierr;
  PetscScalar    *array;
  Vec_Seq        *s = (Vec_Seq*)v->data;
  PetscInt       n = v->map->n;

  PetscFunctionBegin;
  if (!s) {
    ierr = PetscNewLog((PetscObject)v,&s);CHKERRQ(ierr);
    v->data = s;
  }
  if (!s->array) {
    if (n*sizeof(PetscScalar) > v->minimum_bytes_pinned_memory) {
      ierr = PetscMallocSetHIPHost();CHKERRQ(ierr);
      v->pinned_memory = PETSC_TRUE;
    }
    ierr = PetscMalloc1(n,&array);CHKERRQ(ierr);
    ierr = PetscLogObjectMemory((PetscObject)v,n*sizeof(PetscScalar));CHKERRQ(ierr);
    s->array           = array;
    s->array_allocated = array;
    if (n*sizeof(PetscScalar) > v->minimum_bytes_pinned_memory) {
      ierr = PetscMallocResetHIPHost();CHKERRQ(ierr);
    }
    if (v->offloadmask == PETSC_OFFLOAD_UNALLOCATED) {
      v->offloadmask = PETSC_OFFLOAD_CPU;
    }
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecCopy_SeqHIP_Private(Vec xin,Vec yin)
{
  PetscScalar       *ya;
  const PetscScalar *xa;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = VecHIPAllocateCheckHost(xin);CHKERRQ(ierr);
  ierr = VecHIPAllocateCheckHost(yin);CHKERRQ(ierr);
  if (xin != yin) {
    ierr = VecGetArrayRead(xin,&xa);CHKERRQ(ierr);
    ierr = VecGetArray(yin,&ya);CHKERRQ(ierr);
    ierr = PetscArraycpy(ya,xa,xin->map->n);CHKERRQ(ierr);
    ierr = VecRestoreArrayRead(xin,&xa);CHKERRQ(ierr);
    ierr = VecRestoreArray(yin,&ya);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecSetRandom_SeqHIP_Private(Vec xin,PetscRandom r)
{
  PetscErrorCode ierr;
  PetscInt       n = xin->map->n,i;
  PetscScalar    *xx;

  PetscFunctionBegin;
  ierr = VecGetArray(xin,&xx);CHKERRQ(ierr);
  for (i=0; i<n; i++) { ierr = PetscRandomGetValue(r,&xx[i]);CHKERRQ(ierr); }
  ierr = VecRestoreArray(xin,&xx);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecDestroy_SeqHIP_Private(Vec v)
{
  Vec_Seq        *vs = (Vec_Seq*)v->data;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscObjectSAWsViewOff(v);CHKERRQ(ierr);
#if defined(PETSC_USE_LOG)
  PetscLogObjectState((PetscObject)v,"Length=%D",v->map->n);
#endif
  if (vs) {
    if (vs->array_allocated) {
      if (v->pinned_memory) {
        ierr = PetscMallocSetHIPHost();CHKERRQ(ierr);
      }
      ierr = PetscFree(vs->array_allocated);CHKERRQ(ierr);
      if (v->pinned_memory) {
        ierr = PetscMallocResetHIPHost();CHKERRQ(ierr);
        v->pinned_memory = PETSC_FALSE;
      }
    }
    ierr = PetscFree(vs);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecResetArray_SeqHIP_Private(Vec vin)
{
  Vec_Seq *v = (Vec_Seq*)vin->data;

  PetscFunctionBegin;
  v->array         = v->unplacedarray;
  v->unplacedarray = 0;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecSetRandom_SeqHIP(Vec xin,PetscRandom r)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecSetRandom_SeqHIP_Private(xin,r);CHKERRQ(ierr);
  xin->offloadmask = PETSC_OFFLOAD_CPU;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecResetArray_SeqHIP(Vec vin)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecHIPCopyFromGPU(vin);CHKERRQ(ierr);
  ierr = VecResetArray_SeqHIP_Private(vin);CHKERRQ(ierr);
  vin->offloadmask = PETSC_OFFLOAD_CPU;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecPlaceArray_SeqHIP(Vec vin,const PetscScalar *a)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecHIPCopyFromGPU(vin);CHKERRQ(ierr);
  ierr = VecPlaceArray_Seq(vin,a);CHKERRQ(ierr);
  vin->offloadmask = PETSC_OFFLOAD_CPU;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecReplaceArray_SeqHIP(Vec vin,const PetscScalar *a)
{
  PetscErrorCode ierr;
  Vec_Seq        *vs = (Vec_Seq*)vin->data;

  PetscFunctionBegin;
  if (vs->array != vs->array_allocated) {
    /* make sure the users array has the latest values */
    ierr = VecHIPCopyFromGPU(vin);CHKERRQ(ierr);
  }
  if (vs->array_allocated) {
    if (vin->pinned_memory) {
      ierr = PetscMallocSetHIPHost();CHKERRQ(ierr);
    }
    ierr = PetscFree(vs->array_allocated);CHKERRQ(ierr);
    if (vin->pinned_memory) {
      ierr = PetscMallocResetHIPHost();CHKERRQ(ierr);
    }
  }
  vin->pinned_memory = PETSC_FALSE;
  vs->array_allocated = vs->array = (PetscScalar*)a;
  vin->offloadmask = PETSC_OFFLOAD_CPU;
  PetscFunctionReturn(0);
}

/*@
 VecCreateSeqHIP - Creates a standard, sequential array-style vector.

 Collective

 Input Parameter:
 +  comm - the communicator, should be PETSC_COMM_SELF
 -  n - the vector length

 Output Parameter:
 .  v - the vector

 Notes:
 Use VecDuplicate() or VecDuplicateVecs() to form additional vectors of the
 same type as an existing vector.

 Level: intermediate

 .seealso: VecCreateMPI(), VecCreate(), VecDuplicate(), VecDuplicateVecs(), VecCreateGhost()
 @*/
PETSC_EXTERN PetscErrorCode VecCreateSeqHIP(MPI_Comm comm,PetscInt n,Vec *v)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecCreate(comm,v);CHKERRQ(ierr);
  ierr = VecSetSizes(*v,n,n);CHKERRQ(ierr);
  ierr = VecSetType(*v,VECSEQHIP);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecDuplicate_SeqHIP(Vec win,Vec *V)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecCreateSeqHIP(PetscObjectComm((PetscObject)win),win->map->n,V);CHKERRQ(ierr);
  ierr = PetscLayoutReference(win->map,&(*V)->map);CHKERRQ(ierr);
  ierr = PetscObjectListDuplicate(((PetscObject)win)->olist,&((PetscObject)(*V))->olist);CHKERRQ(ierr);
  ierr = PetscFunctionListDuplicate(((PetscObject)win)->qlist,&((PetscObject)(*V))->qlist);CHKERRQ(ierr);
  (*V)->stash.ignorenegidx = win->stash.ignorenegidx;
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode VecCreate_SeqHIP(Vec V)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscHIPInitializeCheck();CHKERRQ(ierr);
  ierr = PetscLayoutSetUp(V->map);CHKERRQ(ierr);
  ierr = VecHIPAllocateCheck(V);CHKERRQ(ierr);
  ierr = VecCreate_SeqHIP_Private(V,((Vec_HIP*)V->spptr)->GPUarray_allocated);CHKERRQ(ierr);
  ierr = VecHIPAllocateCheckHost(V);CHKERRQ(ierr);
  ierr = VecSet(V,0.0);CHKERRQ(ierr);
  ierr = VecSet_Seq(V,0.0);CHKERRQ(ierr);
  V->offloadmask = PETSC_OFFLOAD_BOTH;
  PetscFunctionReturn(0);
}

/*@C
   VecCreateSeqHIPWithArray - Creates a HIP sequential array-style vector,
   where the user provides the array space to store the vector values. The array
   provided must be a GPU array.

   Collective

   Input Parameter:
+  comm - the communicator, should be PETSC_COMM_SELF
.  bs - the block size
.  n - the vector length
-  array - GPU memory where the vector elements are to be stored.

   Output Parameter:
.  V - the vector

   Notes:
   Use VecDuplicate() or VecDuplicateVecs() to form additional vectors of the
   same type as an existing vector.

   If the user-provided array is NULL, then VecHIPPlaceArray() can be used
   at a later stage to SET the array for storing the vector values.

   PETSc does NOT free the array when the vector is destroyed via VecDestroy().
   The user should not free the array until the vector is destroyed.

   Level: intermediate

.seealso: VecCreateMPIHIPWithArray(), VecCreate(), VecDuplicate(), VecDuplicateVecs(),
          VecCreateGhost(), VecCreateSeq(), VecHIPPlaceArray(), VecCreateSeqWithArray(),
          VecCreateMPIWithArray()
@*/
PETSC_EXTERN PetscErrorCode  VecCreateSeqHIPWithArray(MPI_Comm comm,PetscInt bs,PetscInt n,const PetscScalar array[],Vec *V)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscHIPInitializeCheck();CHKERRQ(ierr);
  ierr = VecCreate(comm,V);CHKERRQ(ierr);
  ierr = VecSetSizes(*V,n,n);CHKERRQ(ierr);
  ierr = VecSetBlockSize(*V,bs);CHKERRQ(ierr);
  ierr = VecCreate_SeqHIP_Private(*V,array);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
   VecCreateSeqHIPWithArrays - Creates a HIP sequential array-style vector,
   where the user provides the array space to store the vector values.

   Collective

   Input Parameter:
+  comm - the communicator, should be PETSC_COMM_SELF
.  bs - the block size
.  n - the vector length
-  cpuarray - CPU memory where the vector elements are to be stored.
-  gpuarray - GPU memory where the vector elements are to be stored.

   Output Parameter:
.  V - the vector

   Notes:
   If both cpuarray and gpuarray are provided, the caller must ensure that
   the provided arrays have identical values.

   PETSc does NOT free the provided arrays when the vector is destroyed via
   VecDestroy(). The user should not free the array until the vector is
   destroyed.

   Level: intermediate

.seealso: VecCreateMPIHIPWithArrays(), VecCreate(), VecCreateSeqWithArray(),
          VecHIPPlaceArray(), VecCreateSeqHIPWithArray(),
          VecHIPAllocateCheckHost()
@*/
PETSC_EXTERN PetscErrorCode  VecCreateSeqHIPWithArrays(MPI_Comm comm,PetscInt bs,PetscInt n,const PetscScalar cpuarray[],const PetscScalar gpuarray[],Vec *V)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  // set V's gpuarray to be gpuarray, do not allocate memory on host yet.
  ierr = VecCreateSeqHIPWithArray(comm,bs,n,gpuarray,V);CHKERRQ(ierr);

  if (cpuarray && gpuarray) {
    Vec_Seq *s = (Vec_Seq*)((*V)->data);
    s->array = (PetscScalar*)cpuarray;
    (*V)->offloadmask = PETSC_OFFLOAD_BOTH;
  } else if (cpuarray) {
    Vec_Seq *s = (Vec_Seq*)((*V)->data);
    s->array = (PetscScalar*)cpuarray;
    (*V)->offloadmask = PETSC_OFFLOAD_CPU;
  } else if (gpuarray) {
    (*V)->offloadmask = PETSC_OFFLOAD_GPU;
  } else {
    (*V)->offloadmask = PETSC_OFFLOAD_UNALLOCATED;
  }

  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecGetArray_SeqHIP(Vec v,PetscScalar **a)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (v->offloadmask == PETSC_OFFLOAD_GPU) {
    ierr = VecHIPCopyFromGPU(v);CHKERRQ(ierr);
  } else {
    ierr = VecHIPAllocateCheckHost(v);CHKERRQ(ierr);
  }
  *a = *((PetscScalar**)v->data);
  PetscFunctionReturn(0);
}

PetscErrorCode VecRestoreArray_SeqHIP(Vec v,PetscScalar **a)
{
  PetscFunctionBegin;
  v->offloadmask = PETSC_OFFLOAD_CPU;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecGetArrayWrite_SeqHIP(Vec v,PetscScalar **a)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecHIPAllocateCheckHost(v);CHKERRQ(ierr);
  *a   = *((PetscScalar**)v->data);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecGetArrayAndMemType_SeqHIP(Vec v,PetscScalar** a,PetscMemType *mtype)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (v->offloadmask & PETSC_OFFLOAD_GPU) { /* Prefer working on GPU when offloadmask is PETSC_OFFLOAD_BOTH */
    *a = ((Vec_HIP*)v->spptr)->GPUarray;
    v->offloadmask    = PETSC_OFFLOAD_GPU; /* Change the mask once GPU gets write access, don't wait until restore array */
    if (mtype) *mtype = PETSC_MEMTYPE_HIP;
  } else {
    ierr = VecHIPAllocateCheckHost(v);CHKERRQ(ierr);
    *a = *((PetscScalar**)v->data);
    if (mtype) *mtype = PETSC_MEMTYPE_HOST;
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecRestoreArrayAndMemType_SeqHIP(Vec v,PetscScalar** a)
{
  PetscFunctionBegin;
  if (v->offloadmask & PETSC_OFFLOAD_GPU) {
    v->offloadmask = PETSC_OFFLOAD_GPU;
  } else {
    v->offloadmask = PETSC_OFFLOAD_CPU;
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecBindToCPU_SeqHIP(Vec V,PetscBool pin)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  V->boundtocpu = pin;
  if (pin) {
    ierr = VecHIPCopyFromGPU(V);CHKERRQ(ierr);
    V->offloadmask                 = PETSC_OFFLOAD_CPU; /* since the CPU code will likely change values in the vector */
    V->ops->dot                    = VecDot_Seq;
    V->ops->norm                   = VecNorm_Seq;
    V->ops->tdot                   = VecTDot_Seq;
    V->ops->scale                  = VecScale_Seq;
    V->ops->copy                   = VecCopy_Seq;
    V->ops->set                    = VecSet_Seq;
    V->ops->swap                   = VecSwap_Seq;
    V->ops->axpy                   = VecAXPY_Seq;
    V->ops->axpby                  = VecAXPBY_Seq;
    V->ops->axpbypcz               = VecAXPBYPCZ_Seq;
    V->ops->pointwisemult          = VecPointwiseMult_Seq;
    V->ops->pointwisedivide        = VecPointwiseDivide_Seq;
    V->ops->setrandom              = VecSetRandom_Seq;
    V->ops->dot_local              = VecDot_Seq;
    V->ops->tdot_local             = VecTDot_Seq;
    V->ops->norm_local             = VecNorm_Seq;
    V->ops->mdot_local             = VecMDot_Seq;
    V->ops->mtdot_local            = VecMTDot_Seq;
    V->ops->maxpy                  = VecMAXPY_Seq;
    V->ops->mdot                   = VecMDot_Seq;
    V->ops->mtdot                  = VecMTDot_Seq;
    V->ops->aypx                   = VecAYPX_Seq;
    V->ops->waxpy                  = VecWAXPY_Seq;
    V->ops->dotnorm2               = NULL;
    V->ops->placearray             = VecPlaceArray_Seq;
    V->ops->replacearray           = VecReplaceArray_SeqHIP;
    V->ops->resetarray             = VecResetArray_Seq;
    V->ops->duplicate              = VecDuplicate_Seq;
    V->ops->conjugate              = VecConjugate_Seq;
    V->ops->getlocalvector         = NULL;
    V->ops->restorelocalvector     = NULL;
    V->ops->getlocalvectorread     = NULL;
    V->ops->restorelocalvectorread = NULL;
    V->ops->getarraywrite          = NULL;
  } else {
    V->ops->dot                    = VecDot_SeqHIP;
    V->ops->norm                   = VecNorm_SeqHIP;
    V->ops->tdot                   = VecTDot_SeqHIP;
    V->ops->scale                  = VecScale_SeqHIP;
    V->ops->copy                   = VecCopy_SeqHIP;
    V->ops->set                    = VecSet_SeqHIP;
    V->ops->swap                   = VecSwap_SeqHIP;
    V->ops->axpy                   = VecAXPY_SeqHIP;
    V->ops->axpby                  = VecAXPBY_SeqHIP;
    V->ops->axpbypcz               = VecAXPBYPCZ_SeqHIP;
    V->ops->pointwisemult          = VecPointwiseMult_SeqHIP;
    V->ops->pointwisedivide        = VecPointwiseDivide_SeqHIP;
    V->ops->setrandom              = VecSetRandom_SeqHIP;
    V->ops->dot_local              = VecDot_SeqHIP;
    V->ops->tdot_local             = VecTDot_SeqHIP;
    V->ops->norm_local             = VecNorm_SeqHIP;
    V->ops->mdot_local             = VecMDot_SeqHIP;
    V->ops->maxpy                  = VecMAXPY_SeqHIP;
    V->ops->mdot                   = VecMDot_SeqHIP;
    V->ops->aypx                   = VecAYPX_SeqHIP;
    V->ops->waxpy                  = VecWAXPY_SeqHIP;
    V->ops->dotnorm2               = VecDotNorm2_SeqHIP;
    V->ops->placearray             = VecPlaceArray_SeqHIP;
    V->ops->replacearray           = VecReplaceArray_SeqHIP;
    V->ops->resetarray             = VecResetArray_SeqHIP;
    V->ops->destroy                = VecDestroy_SeqHIP;
    V->ops->duplicate              = VecDuplicate_SeqHIP;
    V->ops->conjugate              = VecConjugate_SeqHIP;
    V->ops->getlocalvector         = VecGetLocalVector_SeqHIP;
    V->ops->restorelocalvector     = VecRestoreLocalVector_SeqHIP;
    V->ops->getlocalvectorread     = VecGetLocalVector_SeqHIP;
    V->ops->restorelocalvectorread = VecRestoreLocalVector_SeqHIP;
    V->ops->getarraywrite          = VecGetArrayWrite_SeqHIP;
    V->ops->getarray               = VecGetArray_SeqHIP;
    V->ops->restorearray           = VecRestoreArray_SeqHIP;
    V->ops->getarrayandmemtype     = VecGetArrayAndMemType_SeqHIP;
    V->ops->restorearrayandmemtype = VecRestoreArrayAndMemType_SeqHIP;
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecCreate_SeqHIP_Private(Vec V,const PetscScalar *array)
{
  PetscErrorCode ierr;
  Vec_HIP       *vechip;
  PetscMPIInt    size;
  PetscBool      option_set;

  PetscFunctionBegin;
  ierr = MPI_Comm_size(PetscObjectComm((PetscObject)V),&size);CHKERRMPI(ierr);
  if (size > 1) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Cannot create VECSEQHIP on more than one process");
  ierr = VecCreate_Seq_Private(V,0);CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)V,VECSEQHIP);CHKERRQ(ierr);
  ierr = VecBindToCPU_SeqHIP(V,PETSC_FALSE);CHKERRQ(ierr);
  V->ops->bindtocpu = VecBindToCPU_SeqHIP;

  /* Later, functions check for the Vec_HIP structure existence, so do not create it without array */
  if (array) {
    if (!V->spptr) {
      PetscReal pinned_memory_min;
      ierr = PetscMalloc(sizeof(Vec_HIP),&V->spptr);CHKERRQ(ierr);
      vechip = (Vec_HIP*)V->spptr;
      vechip->stream = 0; /* using default stream */
      vechip->GPUarray_allocated = 0;
      V->offloadmask = PETSC_OFFLOAD_UNALLOCATED;

      pinned_memory_min = 0;
      /* Need to parse command line for minimum size to use for pinned memory allocations on host here.
         Note: This same code duplicated in VecHIPAllocateCheck() and VecCreate_MPIHIP_Private(). Is there a good way to avoid this? */
      ierr = PetscOptionsBegin(PetscObjectComm((PetscObject)V),((PetscObject)V)->prefix,"VECHIP Options","Vec");CHKERRQ(ierr);
      ierr = PetscOptionsReal("-vec_pinned_memory_min","Minimum size (in bytes) for an allocation to use pinned memory on host","VecSetPinnedMemoryMin",pinned_memory_min,&pinned_memory_min,&option_set);CHKERRQ(ierr);
      if (option_set) V->minimum_bytes_pinned_memory = pinned_memory_min;
      ierr = PetscOptionsEnd();CHKERRQ(ierr);
    }
    vechip = (Vec_HIP*)V->spptr;
    vechip->GPUarray = (PetscScalar*)array;
    V->offloadmask = PETSC_OFFLOAD_GPU;

  }
  PetscFunctionReturn(0);
}
