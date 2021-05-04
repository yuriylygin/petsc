#include "hip/hip_runtime.h"
/*
   Implements the sequential hip vectors.
*/

#define PETSC_SKIP_SPINLOCK
#define PETSC_SKIP_CXX_COMPLEX_FIX

#include <petscconf.h>
#include <petsc/private/vecimpl.h>
#include <../src/vec/vec/impls/dvecimpl.h>
#include <petsc/private/hipvecimpl.h>
/* TODO:  Why do I need this? */
#include <petschipblas.h>

#include <hip/hip_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/transform.h>
#include <thrust/functional.h>

/*
    Allocates space for the vector array on the GPU if it does not exist.
    Does NOT change the PetscHIPFlag for the vector
    Does NOT zero the HIP array

 */
PETSC_INTERN PetscErrorCode VecHIPAllocateCheck(Vec v)
{
  PetscErrorCode ierr;
  hipError_t    err;
  Vec_HIP       *vechip;
  PetscBool      option_set;

  PetscFunctionBegin;
  if (!v->spptr) {
    PetscReal pinned_memory_min;
    ierr = PetscMalloc(sizeof(Vec_HIP),&v->spptr);CHKERRQ(ierr);
    vechip = (Vec_HIP*)v->spptr;
    err = hipMalloc((void**)&vechip->GPUarray_allocated,sizeof(PetscScalar)*((PetscBLASInt)v->map->n));CHKERRHIP(err);
    vechip->GPUarray = vechip->GPUarray_allocated;
    vechip->stream = 0;  /* using default stream */
    if (v->offloadmask == PETSC_OFFLOAD_UNALLOCATED) {
      if (v->data && ((Vec_Seq*)v->data)->array) {
        v->offloadmask = PETSC_OFFLOAD_CPU;
      } else {
        v->offloadmask = PETSC_OFFLOAD_GPU;
      }
    }
    pinned_memory_min = 0;

    /* Need to parse command line for minimum size to use for pinned memory allocations on host here.
       Note: This same code duplicated in VecCreate_SeqHIP_Private() and VecCreate_MPIHIP_Private(). Is there a good way to avoid this? */
    ierr = PetscOptionsBegin(PetscObjectComm((PetscObject)v),((PetscObject)v)->prefix,"VECHIP Options","Vec");CHKERRQ(ierr);
    ierr = PetscOptionsReal("-vec_pinned_memory_min","Minimum size (in bytes) for an allocation to use pinned memory on host","VecSetPinnedMemoryMin",pinned_memory_min,&pinned_memory_min,&option_set);CHKERRQ(ierr);
    if (option_set) v->minimum_bytes_pinned_memory = pinned_memory_min;
    ierr = PetscOptionsEnd();CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/* Copies a vector from the CPU to the GPU unless we already have an up-to-date copy on the GPU */
PETSC_INTERN PetscErrorCode VecHIPCopyToGPU(Vec v)
{
  PetscErrorCode ierr;
  hipError_t    err;
  Vec_HIP       *vechip;
  PetscScalar    *varray;

  PetscFunctionBegin;
  PetscCheckTypeNames(v,VECSEQHIP,VECMPIHIP);
  ierr = VecHIPAllocateCheck(v);CHKERRQ(ierr);
  if (v->offloadmask == PETSC_OFFLOAD_CPU) {
    ierr               = PetscLogEventBegin(VEC_HIPCopyToGPU,v,0,0,0);CHKERRQ(ierr);
    vechip            = (Vec_HIP*)v->spptr;
    varray             = vechip->GPUarray;
    err                = hipMemcpy(varray,((Vec_Seq*)v->data)->array,v->map->n*sizeof(PetscScalar),hipMemcpyHostToDevice);CHKERRHIP(err);
    ierr               = PetscLogCpuToGpu((v->map->n)*sizeof(PetscScalar));CHKERRQ(ierr);
    ierr               = PetscLogEventEnd(VEC_HIPCopyToGPU,v,0,0,0);CHKERRQ(ierr);
    v->offloadmask = PETSC_OFFLOAD_BOTH;
  }
  PetscFunctionReturn(0);
}

/*
     VecHIPCopyFromGPU - Copies a vector from the GPU to the CPU unless we already have an up-to-date copy on the CPU
*/
PETSC_EXTERN PetscErrorCode VecHIPCopyFromGPU(Vec v)
{
  PetscErrorCode ierr;
  hipError_t    err;
  Vec_HIP       *vechip;
  PetscScalar    *varray;

  PetscFunctionBegin;
  PetscCheckTypeNames(v,VECSEQHIP,VECMPIHIP);
  ierr = VecHIPAllocateCheckHost(v);CHKERRQ(ierr);
  if (v->offloadmask == PETSC_OFFLOAD_GPU) {
    ierr               = PetscLogEventBegin(VEC_HIPCopyFromGPU,v,0,0,0);CHKERRQ(ierr);
    vechip            = (Vec_HIP*)v->spptr;
    varray             = vechip->GPUarray;
    err                = hipMemcpy(((Vec_Seq*)v->data)->array,varray,v->map->n*sizeof(PetscScalar),hipMemcpyDeviceToHost);CHKERRHIP(err);
    ierr               = PetscLogGpuToCpu((v->map->n)*sizeof(PetscScalar));CHKERRQ(ierr);
    ierr               = PetscLogEventEnd(VEC_HIPCopyFromGPU,v,0,0,0);CHKERRQ(ierr);
    v->offloadmask     = PETSC_OFFLOAD_BOTH;
  }
  PetscFunctionReturn(0);
}

/*MC
   VECSEQHIP - VECSEQHIP = "seqhip" - The basic sequential vector, modified to use HIP

   Options Database Keys:
. -vec_type seqhip - sets the vector type to VECSEQHIP during a call to VecSetFromOptions()

  Level: beginner

.seealso: VecCreate(), VecSetType(), VecSetFromOptions(), VecCreateSeqWithArray(), VECMPI, VecType, VecCreateMPI(), VecCreateSeq()
M*/

PETSC_INTERN PetscErrorCode VecAYPX_SeqHIP(Vec yin,PetscScalar alpha,Vec xin)
{
  const PetscScalar *xarray;
  PetscScalar       *yarray;
  PetscErrorCode    ierr;
  PetscBLASInt      one = 1,bn = 0;
  PetscScalar       sone = 1.0;
  hipblasHandle_t   hipblasv2handle;
  hipblasStatus_t   hberr;
  hipError_t       err;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(yin->map->n,&bn);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPGetArray(yin,&yarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  if (alpha == (PetscScalar)0.0) {
    err = hipMemcpy(yarray,xarray,bn*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
  } else if (alpha == (PetscScalar)1.0) {
    hberr = hipblasXaxpy(hipblasv2handle,bn,&alpha,xarray,one,yarray,one);CHKERRHIPBLAS(hberr);
    ierr = PetscLogGpuFlops(1.0*yin->map->n);CHKERRQ(ierr);
  } else {
    hberr = hipblasXscal(hipblasv2handle,bn,&alpha,yarray,one);CHKERRHIPBLAS(hberr);
    hberr = hipblasXaxpy(hipblasv2handle,bn,&sone,xarray,one,yarray,one);CHKERRHIPBLAS(hberr);
    ierr = PetscLogGpuFlops(2.0*yin->map->n);CHKERRQ(ierr);
  }
  err  = WaitForHIP();CHKERRHIP(err);
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArray(yin,&yarray);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode VecAXPY_SeqHIP(Vec yin,PetscScalar alpha,Vec xin)
{
  const PetscScalar *xarray;
  PetscScalar       *yarray;
  PetscErrorCode    ierr;
  PetscBLASInt      one = 1,bn = 0;
  hipblasHandle_t   hipblasv2handle;
  hipblasStatus_t   hberr;
  PetscBool         yiship,xiship;
  hipError_t       err;

  PetscFunctionBegin;
  if (alpha == (PetscScalar)0.0) PetscFunctionReturn(0);
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompareAny((PetscObject)yin,&yiship,VECSEQHIP,VECMPIHIP,"");CHKERRQ(ierr);
  ierr = PetscObjectTypeCompareAny((PetscObject)xin,&xiship,VECSEQHIP,VECMPIHIP,"");CHKERRQ(ierr);
  if (xiship && yiship) {
    ierr = PetscBLASIntCast(yin->map->n,&bn);CHKERRQ(ierr);
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPGetArray(yin,&yarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXaxpy(hipblasv2handle,bn,&alpha,xarray,one,yarray,one);CHKERRHIPBLAS(hberr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArray(yin,&yarray);CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(2.0*yin->map->n);CHKERRQ(ierr);
  } else {
    ierr = VecAXPY_Seq(yin,alpha,xin);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecPointwiseDivide_SeqHIP(Vec win, Vec xin, Vec yin)
{
  PetscInt                              n = xin->map->n;
  const PetscScalar                     *xarray=NULL,*yarray=NULL;
  PetscScalar                           *warray=NULL;
  thrust::device_ptr<const PetscScalar> xptr,yptr;
  thrust::device_ptr<PetscScalar>       wptr;
  PetscErrorCode                        ierr;
  hipError_t                           err;

  PetscFunctionBegin;
  ierr = VecHIPGetArrayWrite(win,&warray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  try {
    wptr = thrust::device_pointer_cast(warray);
    xptr = thrust::device_pointer_cast(xarray);
    yptr = thrust::device_pointer_cast(yarray);
    thrust::transform(xptr,xptr+n,yptr,wptr,thrust::divides<PetscScalar>());
    err  = WaitForHIP();CHKERRHIP(err);
  } catch (char *ex) {
    SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Thrust error: %s", ex);
  }
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  ierr = PetscLogGpuFlops(n);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayWrite(win,&warray);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecWAXPY_SeqHIP(Vec win,PetscScalar alpha,Vec xin, Vec yin)
{
  const PetscScalar *xarray=NULL,*yarray=NULL;
  PetscScalar       *warray=NULL;
  PetscErrorCode    ierr;
  PetscBLASInt      one = 1,bn = 0;
  hipblasHandle_t    hipblasv2handle;
  hipblasStatus_t    hberr;
  hipError_t       err;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(win->map->n,&bn);CHKERRQ(ierr);
  if (alpha == (PetscScalar)0.0) {
    ierr = VecCopy_SeqHIP(yin,win);CHKERRQ(ierr);
  } else {
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPGetArrayRead(yin,&yarray);CHKERRQ(ierr);
    ierr = VecHIPGetArrayWrite(win,&warray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    err = hipMemcpy(warray,yarray,win->map->n*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
    hberr = hipblasXaxpy(hipblasv2handle,bn,&alpha,xarray,one,warray,one);CHKERRHIPBLAS(hberr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(2*win->map->n);CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayRead(yin,&yarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayWrite(win,&warray);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecMAXPY_SeqHIP(Vec xin, PetscInt nv,const PetscScalar *alpha,Vec *y)
{
  PetscErrorCode ierr;
  hipError_t    err;
  PetscInt       n = xin->map->n,j,j_rem;
  PetscScalar    alpha0,alpha1,alpha2,alpha3;

  PetscFunctionBegin;
  ierr = PetscLogGpuFlops(nv*2.0*n);CHKERRQ(ierr);
  switch (j_rem=nv&0x3) {
    case 3:
      alpha0 = alpha[0];
      alpha1 = alpha[1];
      alpha2 = alpha[2];
      alpha += 3;
      ierr   = VecAXPY_SeqHIP(xin,alpha0,y[0]);CHKERRQ(ierr);
      ierr   = VecAXPY_SeqHIP(xin,alpha1,y[1]);CHKERRQ(ierr);
      ierr   = VecAXPY_SeqHIP(xin,alpha2,y[2]);CHKERRQ(ierr);
      y   += 3;
      break;
    case 2:
      alpha0 = alpha[0];
      alpha1 = alpha[1];
      alpha +=2;
      ierr   = VecAXPY_SeqHIP(xin,alpha0,y[0]);CHKERRQ(ierr);
      ierr   = VecAXPY_SeqHIP(xin,alpha1,y[1]);CHKERRQ(ierr);
      y +=2;
      break;
    case 1:
      alpha0 = *alpha++;
      ierr   = VecAXPY_SeqHIP(xin,alpha0,y[0]);CHKERRQ(ierr);
      y     +=1;
      break;
  }
  for (j=j_rem; j<nv; j+=4) {
    alpha0 = alpha[0];
    alpha1 = alpha[1];
    alpha2 = alpha[2];
    alpha3 = alpha[3];
    alpha += 4;
    ierr   = VecAXPY_SeqHIP(xin,alpha0,y[0]);CHKERRQ(ierr);
    ierr   = VecAXPY_SeqHIP(xin,alpha1,y[1]);CHKERRQ(ierr);
    ierr   = VecAXPY_SeqHIP(xin,alpha2,y[2]);CHKERRQ(ierr);
    ierr   = VecAXPY_SeqHIP(xin,alpha3,y[3]);CHKERRQ(ierr);
    y   += 4;
  }
  err  = WaitForHIP();CHKERRHIP(err);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecDot_SeqHIP(Vec xin,Vec yin,PetscScalar *z)
{
  const PetscScalar *xarray,*yarray;
  PetscErrorCode    ierr;
  PetscBLASInt      one = 1,bn = 0;
  hipblasHandle_t    hipblasv2handle;
  hipblasStatus_t    cerr;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(yin->map->n,&bn);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(yin,&yarray);CHKERRQ(ierr);
  /* arguments y, x are reversed because BLAS complex conjugates the first argument, PETSc the second */
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  cerr = hipblasXdot(hipblasv2handle,bn,yarray,one,xarray,one,z);CHKERRHIPBLAS(cerr);
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  if (xin->map->n >0) {
    ierr = PetscLogGpuFlops(2.0*xin->map->n-1);CHKERRQ(ierr);
  }
  ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(yin,&yarray);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

//
// HIP kernels for MDot to follow
//

// set work group size to be a power of 2 (128 is usually a good compromise between portability and speed)
#define MDOT_WORKGROUP_SIZE 128
#define MDOT_WORKGROUP_NUM  128

#if !defined(PETSC_USE_COMPLEX)
// M = 2:
__global__ void VecMDot_SeqHIP_kernel2(const PetscScalar *x,const PetscScalar *y0,const PetscScalar *y1,
                                        PetscInt size, PetscScalar *group_results)
{
  __shared__ PetscScalar tmp_buffer[2*MDOT_WORKGROUP_SIZE];
  PetscInt entries_per_group = (size - 1) / gridDim.x + 1;
  entries_per_group = (entries_per_group == 0) ? 1 : entries_per_group;  // for very small vectors, a group should still do some work
  PetscInt vec_start_index = blockIdx.x * entries_per_group;
  PetscInt vec_stop_index  = PetscMin((blockIdx.x + 1) * entries_per_group, size); // don't go beyond vec size

  PetscScalar entry_x    = 0;
  PetscScalar group_sum0 = 0;
  PetscScalar group_sum1 = 0;
  for (PetscInt i = vec_start_index + threadIdx.x; i < vec_stop_index; i += blockDim.x) {
    entry_x     = x[i];   // load only once from global memory!
    group_sum0 += entry_x * y0[i];
    group_sum1 += entry_x * y1[i];
  }
  tmp_buffer[threadIdx.x]                       = group_sum0;
  tmp_buffer[threadIdx.x + MDOT_WORKGROUP_SIZE] = group_sum1;

  // parallel reduction
  for (PetscInt stride = blockDim.x/2; stride > 0; stride /= 2) {
    __syncthreads();
    if (threadIdx.x < stride) {
      tmp_buffer[threadIdx.x                      ] += tmp_buffer[threadIdx.x+stride                      ];
      tmp_buffer[threadIdx.x + MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + MDOT_WORKGROUP_SIZE];
    }
  }

  // write result of group to group_results
  if (threadIdx.x == 0) {
    group_results[blockIdx.x]             = tmp_buffer[0];
    group_results[blockIdx.x + gridDim.x] = tmp_buffer[MDOT_WORKGROUP_SIZE];
  }
}

// M = 3:
__global__ void VecMDot_SeqHIP_kernel3(const PetscScalar *x,const PetscScalar *y0,const PetscScalar *y1,const PetscScalar *y2,
                                        PetscInt size, PetscScalar *group_results)
{
  __shared__ PetscScalar tmp_buffer[3*MDOT_WORKGROUP_SIZE];
  PetscInt entries_per_group = (size - 1) / gridDim.x + 1;
  entries_per_group = (entries_per_group == 0) ? 1 : entries_per_group;  // for very small vectors, a group should still do some work
  PetscInt vec_start_index = blockIdx.x * entries_per_group;
  PetscInt vec_stop_index  = PetscMin((blockIdx.x + 1) * entries_per_group, size); // don't go beyond vec size

  PetscScalar entry_x    = 0;
  PetscScalar group_sum0 = 0;
  PetscScalar group_sum1 = 0;
  PetscScalar group_sum2 = 0;
  for (PetscInt i = vec_start_index + threadIdx.x; i < vec_stop_index; i += blockDim.x) {
    entry_x     = x[i];   // load only once from global memory!
    group_sum0 += entry_x * y0[i];
    group_sum1 += entry_x * y1[i];
    group_sum2 += entry_x * y2[i];
  }
  tmp_buffer[threadIdx.x]                           = group_sum0;
  tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] = group_sum1;
  tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] = group_sum2;

  // parallel reduction
  for (PetscInt stride = blockDim.x/2; stride > 0; stride /= 2) {
    __syncthreads();
    if (threadIdx.x < stride) {
      tmp_buffer[threadIdx.x                          ] += tmp_buffer[threadIdx.x+stride                          ];
      tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride +     MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 2 * MDOT_WORKGROUP_SIZE];
    }
  }

  // write result of group to group_results
  if (threadIdx.x == 0) {
    group_results[blockIdx.x                ] = tmp_buffer[0];
    group_results[blockIdx.x +     gridDim.x] = tmp_buffer[    MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 2 * gridDim.x] = tmp_buffer[2 * MDOT_WORKGROUP_SIZE];
  }
}

// M = 4:
__global__ void VecMDot_SeqHIP_kernel4(const PetscScalar *x,const PetscScalar *y0,const PetscScalar *y1,const PetscScalar *y2,const PetscScalar *y3,
                                        PetscInt size, PetscScalar *group_results)
{
  __shared__ PetscScalar tmp_buffer[4*MDOT_WORKGROUP_SIZE];
  PetscInt entries_per_group = (size - 1) / gridDim.x + 1;
  entries_per_group = (entries_per_group == 0) ? 1 : entries_per_group;  // for very small vectors, a group should still do some work
  PetscInt vec_start_index = blockIdx.x * entries_per_group;
  PetscInt vec_stop_index  = PetscMin((blockIdx.x + 1) * entries_per_group, size); // don't go beyond vec size

  PetscScalar entry_x    = 0;
  PetscScalar group_sum0 = 0;
  PetscScalar group_sum1 = 0;
  PetscScalar group_sum2 = 0;
  PetscScalar group_sum3 = 0;
  for (PetscInt i = vec_start_index + threadIdx.x; i < vec_stop_index; i += blockDim.x) {
    entry_x     = x[i];   // load only once from global memory!
    group_sum0 += entry_x * y0[i];
    group_sum1 += entry_x * y1[i];
    group_sum2 += entry_x * y2[i];
    group_sum3 += entry_x * y3[i];
  }
  tmp_buffer[threadIdx.x]                           = group_sum0;
  tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] = group_sum1;
  tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] = group_sum2;
  tmp_buffer[threadIdx.x + 3 * MDOT_WORKGROUP_SIZE] = group_sum3;

  // parallel reduction
  for (PetscInt stride = blockDim.x/2; stride > 0; stride /= 2) {
    __syncthreads();
    if (threadIdx.x < stride) {
      tmp_buffer[threadIdx.x                          ] += tmp_buffer[threadIdx.x+stride                          ];
      tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride +     MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 2 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 3 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 3 * MDOT_WORKGROUP_SIZE];
    }
  }

  // write result of group to group_results
  if (threadIdx.x == 0) {
    group_results[blockIdx.x                ] = tmp_buffer[0];
    group_results[blockIdx.x +     gridDim.x] = tmp_buffer[    MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 2 * gridDim.x] = tmp_buffer[2 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 3 * gridDim.x] = tmp_buffer[3 * MDOT_WORKGROUP_SIZE];
  }
}

// M = 8:
__global__ void VecMDot_SeqHIP_kernel8(const PetscScalar *x,const PetscScalar *y0,const PetscScalar *y1,const PetscScalar *y2,const PetscScalar *y3,
                                          const PetscScalar *y4,const PetscScalar *y5,const PetscScalar *y6,const PetscScalar *y7,
                                          PetscInt size, PetscScalar *group_results)
{
  __shared__ PetscScalar tmp_buffer[8*MDOT_WORKGROUP_SIZE];
  PetscInt entries_per_group = (size - 1) / gridDim.x + 1;
  entries_per_group = (entries_per_group == 0) ? 1 : entries_per_group;  // for very small vectors, a group should still do some work
  PetscInt vec_start_index = blockIdx.x * entries_per_group;
  PetscInt vec_stop_index  = PetscMin((blockIdx.x + 1) * entries_per_group, size); // don't go beyond vec size

  PetscScalar entry_x    = 0;
  PetscScalar group_sum0 = 0;
  PetscScalar group_sum1 = 0;
  PetscScalar group_sum2 = 0;
  PetscScalar group_sum3 = 0;
  PetscScalar group_sum4 = 0;
  PetscScalar group_sum5 = 0;
  PetscScalar group_sum6 = 0;
  PetscScalar group_sum7 = 0;
  for (PetscInt i = vec_start_index + threadIdx.x; i < vec_stop_index; i += blockDim.x) {
    entry_x     = x[i];   // load only once from global memory!
    group_sum0 += entry_x * y0[i];
    group_sum1 += entry_x * y1[i];
    group_sum2 += entry_x * y2[i];
    group_sum3 += entry_x * y3[i];
    group_sum4 += entry_x * y4[i];
    group_sum5 += entry_x * y5[i];
    group_sum6 += entry_x * y6[i];
    group_sum7 += entry_x * y7[i];
  }
  tmp_buffer[threadIdx.x]                           = group_sum0;
  tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] = group_sum1;
  tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] = group_sum2;
  tmp_buffer[threadIdx.x + 3 * MDOT_WORKGROUP_SIZE] = group_sum3;
  tmp_buffer[threadIdx.x + 4 * MDOT_WORKGROUP_SIZE] = group_sum4;
  tmp_buffer[threadIdx.x + 5 * MDOT_WORKGROUP_SIZE] = group_sum5;
  tmp_buffer[threadIdx.x + 6 * MDOT_WORKGROUP_SIZE] = group_sum6;
  tmp_buffer[threadIdx.x + 7 * MDOT_WORKGROUP_SIZE] = group_sum7;

  // parallel reduction
  for (PetscInt stride = blockDim.x/2; stride > 0; stride /= 2) {
    __syncthreads();
    if (threadIdx.x < stride) {
      tmp_buffer[threadIdx.x                          ] += tmp_buffer[threadIdx.x+stride                          ];
      tmp_buffer[threadIdx.x +     MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride +     MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 2 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 2 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 3 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 3 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 4 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 4 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 5 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 5 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 6 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 6 * MDOT_WORKGROUP_SIZE];
      tmp_buffer[threadIdx.x + 7 * MDOT_WORKGROUP_SIZE] += tmp_buffer[threadIdx.x+stride + 7 * MDOT_WORKGROUP_SIZE];
    }
  }

  // write result of group to group_results
  if (threadIdx.x == 0) {
    group_results[blockIdx.x                ] = tmp_buffer[0];
    group_results[blockIdx.x +     gridDim.x] = tmp_buffer[    MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 2 * gridDim.x] = tmp_buffer[2 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 3 * gridDim.x] = tmp_buffer[3 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 4 * gridDim.x] = tmp_buffer[4 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 5 * gridDim.x] = tmp_buffer[5 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 6 * gridDim.x] = tmp_buffer[6 * MDOT_WORKGROUP_SIZE];
    group_results[blockIdx.x + 7 * gridDim.x] = tmp_buffer[7 * MDOT_WORKGROUP_SIZE];
  }
}
#endif /* !defined(PETSC_USE_COMPLEX) */

PETSC_INTERN PetscErrorCode VecMDot_SeqHIP(Vec xin,PetscInt nv,const Vec yin[],PetscScalar *z)
{
  PetscErrorCode    ierr;
  PetscInt          i,n = xin->map->n,current_y_index = 0;
  const PetscScalar *xptr,*y0ptr,*y1ptr,*y2ptr,*y3ptr,*y4ptr,*y5ptr,*y6ptr,*y7ptr;
  PetscScalar       *group_results_gpu;
#if !defined(PETSC_USE_COMPLEX)
  PetscInt          j;
  PetscScalar       group_results_cpu[MDOT_WORKGROUP_NUM * 8]; // we process at most eight vectors in one kernel
#endif
  hipError_t    hip_ierr;
  PetscBLASInt      one = 1,bn = 0;
  hipblasHandle_t hipblasv2handle;
  hipblasStatus_t hberr;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(xin->map->n,&bn);CHKERRQ(ierr);
  if (nv <= 0) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_LIB,"Number of vectors provided to VecMDot_SeqHIP not positive.");
  /* Handle the case of local size zero first */
  if (!xin->map->n) {
    for (i=0; i<nv; ++i) z[i] = 0;
    PetscFunctionReturn(0);
  }

  // allocate scratchpad memory for the results of individual work groups:
  hip_ierr = hipMalloc((void**)&group_results_gpu, sizeof(PetscScalar) * MDOT_WORKGROUP_NUM * 8);CHKERRHIP(hip_ierr);

  ierr = VecHIPGetArrayRead(xin,&xptr);CHKERRQ(ierr);

  while (current_y_index < nv)
  {
    switch (nv - current_y_index) {

      case 7:
      case 6:
      case 5:
      case 4:
        ierr = VecHIPGetArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+3],&y3ptr);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
        hberr = hipblasXdot(hipblasv2handle,bn,y0ptr,one,xptr,one,&z[current_y_index]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y1ptr,one,xptr,one,&z[current_y_index+1]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y2ptr,one,xptr,one,&z[current_y_index+2]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y3ptr,one,xptr,one,&z[current_y_index+3]);CHKERRHIPBLAS(hberr);
        ierr  = PetscLogGpuTimeEnd();CHKERRQ(ierr);
#else
        // run kernel:
        hipLaunchKernelGGL(VecMDot_SeqHIP_kernel4, dim3(MDOT_WORKGROUP_NUM), dim3(MDOT_WORKGROUP_SIZE), 0, 0, xptr,y0ptr,y1ptr,y2ptr,y3ptr,n,group_results_gpu);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);

        // copy results back to
        hip_ierr = hipMemcpy(group_results_cpu,group_results_gpu,sizeof(PetscScalar) * MDOT_WORKGROUP_NUM * 4,hipMemcpyDeviceToHost);CHKERRHIP(hip_ierr);

        // sum group results into z:
        for (j=0; j<4; ++j) {
          z[current_y_index + j] = 0;
          for (i=j*MDOT_WORKGROUP_NUM; i<(j+1)*MDOT_WORKGROUP_NUM; ++i) z[current_y_index + j] += group_results_cpu[i];
        }
#endif
        ierr = VecHIPRestoreArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+3],&y3ptr);CHKERRQ(ierr);
        current_y_index += 4;
        break;

      case 3:
        ierr = VecHIPGetArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);

        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
        hberr = hipblasXdot(hipblasv2handle,bn,y0ptr,one,xptr,one,&z[current_y_index]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y1ptr,one,xptr,one,&z[current_y_index+1]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y2ptr,one,xptr,one,&z[current_y_index+2]);CHKERRHIPBLAS(hberr);
        ierr  = PetscLogGpuTimeEnd();CHKERRQ(ierr);
#else
        // run kernel:
        hipLaunchKernelGGL(VecMDot_SeqHIP_kernel3, dim3(MDOT_WORKGROUP_NUM), dim3(MDOT_WORKGROUP_SIZE), 0, 0, xptr,y0ptr,y1ptr,y2ptr,n,group_results_gpu);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);

        // copy results back to
        hip_ierr = hipMemcpy(group_results_cpu,group_results_gpu,sizeof(PetscScalar) * MDOT_WORKGROUP_NUM * 3,hipMemcpyDeviceToHost);CHKERRHIP(hip_ierr);

        // sum group results into z:
        for (j=0; j<3; ++j) {
          z[current_y_index + j] = 0;
          for (i=j*MDOT_WORKGROUP_NUM; i<(j+1)*MDOT_WORKGROUP_NUM; ++i) z[current_y_index + j] += group_results_cpu[i];
        }
#endif
        ierr = VecHIPRestoreArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);
        current_y_index += 3;
        break;

      case 2:
        ierr = VecHIPGetArrayRead(yin[current_y_index],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
        hberr = hipblasXdot(hipblasv2handle,bn,y0ptr,one,xptr,one,&z[current_y_index]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y1ptr,one,xptr,one,&z[current_y_index+1]);CHKERRHIPBLAS(hberr);
        ierr  = PetscLogGpuTimeEnd();CHKERRQ(ierr);
#else
        // run kernel:
        hipLaunchKernelGGL(VecMDot_SeqHIP_kernel2, dim3(MDOT_WORKGROUP_NUM), dim3(MDOT_WORKGROUP_SIZE), 0, 0, xptr,y0ptr,y1ptr,n,group_results_gpu);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);

        // copy results back to
        hip_ierr = hipMemcpy(group_results_cpu,group_results_gpu,sizeof(PetscScalar) * MDOT_WORKGROUP_NUM * 2,hipMemcpyDeviceToHost);CHKERRHIP(hip_ierr);

        // sum group results into z:
        for (j=0; j<2; ++j) {
          z[current_y_index + j] = 0;
          for (i=j*MDOT_WORKGROUP_NUM; i<(j+1)*MDOT_WORKGROUP_NUM; ++i) z[current_y_index + j] += group_results_cpu[i];
        }
#endif
        ierr = VecHIPRestoreArrayRead(yin[current_y_index],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        current_y_index += 2;
        break;

      case 1:
        ierr = VecHIPGetArrayRead(yin[current_y_index],&y0ptr);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
        hberr = hipblasXdot(hipblasv2handle,bn,y0ptr,one,xptr,one,&z[current_y_index]);CHKERRHIPBLAS(hberr);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index],&y0ptr);CHKERRQ(ierr);
        current_y_index += 1;
        break;

      default: // 8 or more vectors left
        ierr = VecHIPGetArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+3],&y3ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+4],&y4ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+5],&y5ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+6],&y6ptr);CHKERRQ(ierr);
        ierr = VecHIPGetArrayRead(yin[current_y_index+7],&y7ptr);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
        hberr = hipblasXdot(hipblasv2handle,bn,y0ptr,one,xptr,one,&z[current_y_index]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y1ptr,one,xptr,one,&z[current_y_index+1]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y2ptr,one,xptr,one,&z[current_y_index+2]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y3ptr,one,xptr,one,&z[current_y_index+3]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y4ptr,one,xptr,one,&z[current_y_index+4]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y5ptr,one,xptr,one,&z[current_y_index+5]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y6ptr,one,xptr,one,&z[current_y_index+6]);CHKERRHIPBLAS(hberr);
        hberr = hipblasXdot(hipblasv2handle,bn,y7ptr,one,xptr,one,&z[current_y_index+7]);CHKERRHIPBLAS(hberr);
#else
        // run kernel:
        hipLaunchKernelGGL(VecMDot_SeqHIP_kernel8, dim3(MDOT_WORKGROUP_NUM), dim3(MDOT_WORKGROUP_SIZE), 0, 0, xptr,y0ptr,y1ptr,y2ptr,y3ptr,y4ptr,y5ptr,y6ptr,y7ptr,n,group_results_gpu);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);

        // copy results back to
        hip_ierr = hipMemcpy(group_results_cpu,group_results_gpu,sizeof(PetscScalar) * MDOT_WORKGROUP_NUM * 8,hipMemcpyDeviceToHost);CHKERRHIP(hip_ierr);

        // sum group results into z:
        for (j=0; j<8; ++j) {
          z[current_y_index + j] = 0;
          for (i=j*MDOT_WORKGROUP_NUM; i<(j+1)*MDOT_WORKGROUP_NUM; ++i) z[current_y_index + j] += group_results_cpu[i];
        }
#endif
        ierr = VecHIPRestoreArrayRead(yin[current_y_index  ],&y0ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+1],&y1ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+2],&y2ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+3],&y3ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+4],&y4ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+5],&y5ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+6],&y6ptr);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(yin[current_y_index+7],&y7ptr);CHKERRQ(ierr);
        current_y_index += 8;
        break;
    }
  }
  ierr = VecHIPRestoreArrayRead(xin,&xptr);CHKERRQ(ierr);

  hip_ierr = hipFree(group_results_gpu);CHKERRHIP(hip_ierr);
  ierr = PetscLogGpuFlops(PetscMax(nv*(2.0*n-1),0.0));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef MDOT_WORKGROUP_SIZE
#undef MDOT_WORKGROUP_NUM

PETSC_EXTERN PetscErrorCode VecSet_SeqHIP(Vec xin,PetscScalar alpha)
{
  PetscInt                        n = xin->map->n;
  PetscScalar                     *xarray=NULL;
  thrust::device_ptr<PetscScalar> xptr;
  PetscErrorCode                  ierr;
  hipError_t                     err;

  PetscFunctionBegin;
  ierr = VecHIPGetArrayWrite(xin,&xarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  if (alpha == (PetscScalar)0.0) {
    err = hipMemset(xarray,0,n*sizeof(PetscScalar));CHKERRHIP(err);
  } else {
    try {
      xptr = thrust::device_pointer_cast(xarray);
      thrust::fill(xptr,xptr+n,alpha);
    } catch (char *ex) {
      SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Thrust error: %s", ex);
    }
  }
  err  = WaitForHIP();CHKERRHIP(err);
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayWrite(xin,&xarray);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecScale_SeqHIP(Vec xin,PetscScalar alpha)
{
  PetscScalar    *xarray;
  PetscErrorCode ierr;
  PetscBLASInt   one = 1,bn = 0;
  hipblasHandle_t hipblasv2handle;
  hipblasStatus_t hberr;
  hipError_t    err;

  PetscFunctionBegin;
  if (alpha == (PetscScalar)0.0) {
    ierr = VecSet_SeqHIP(xin,alpha);CHKERRQ(ierr);
    err  = WaitForHIP();CHKERRHIP(err);
  } else if (alpha != (PetscScalar)1.0) {
    ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
    ierr = PetscBLASIntCast(xin->map->n,&bn);CHKERRQ(ierr);
    ierr = VecHIPGetArray(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXscal(hipblasv2handle,bn,&alpha,xarray,one);CHKERRHIPBLAS(hberr);
    ierr = VecHIPRestoreArray(xin,&xarray);CHKERRQ(ierr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  }
  ierr = PetscLogGpuFlops(xin->map->n);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecTDot_SeqHIP(Vec xin,Vec yin,PetscScalar *z)
{
  const PetscScalar *xarray,*yarray;
  PetscErrorCode    ierr;
  PetscBLASInt      one = 1,bn = 0;
  hipblasHandle_t    hipblasv2handle;
  hipblasStatus_t    cerr;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(xin->map->n,&bn);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  cerr = hipblasXdotu(hipblasv2handle,bn,xarray,one,yarray,one,z);CHKERRHIPBLAS(cerr);
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  if (xin->map->n > 0) {
    ierr = PetscLogGpuFlops(2.0*xin->map->n-1);CHKERRQ(ierr);
  }
  ierr = VecHIPRestoreArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode VecCopy_SeqHIP(Vec xin,Vec yin)
{
  const PetscScalar *xarray;
  PetscScalar       *yarray;
  PetscErrorCode    ierr;
  hipError_t       err;

  PetscFunctionBegin;
  if (xin != yin) {
    if (xin->offloadmask == PETSC_OFFLOAD_GPU) {
      PetscBool yiship;

      ierr = PetscObjectTypeCompareAny((PetscObject)yin,&yiship,VECSEQHIP,VECMPIHIP,"");CHKERRQ(ierr);
      ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
      if (yiship) {
        ierr = VecHIPGetArrayWrite(yin,&yarray);CHKERRQ(ierr);
      } else {
        ierr = VecGetArrayWrite(yin,&yarray);CHKERRQ(ierr);
      }
      ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
      if (yiship) {
        err = hipMemcpy(yarray,xarray,yin->map->n*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
      } else {
        err = hipMemcpy(yarray,xarray,yin->map->n*sizeof(PetscScalar),hipMemcpyDeviceToHost);CHKERRHIP(err);
      }
      err  = WaitForHIP();CHKERRHIP(err);
      ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
      ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
      if (yiship) {
        ierr = VecHIPRestoreArrayWrite(yin,&yarray);CHKERRQ(ierr);
      } else {
        ierr = VecRestoreArrayWrite(yin,&yarray);CHKERRQ(ierr);
      }
    } else if (xin->offloadmask == PETSC_OFFLOAD_CPU) {
      /* copy in CPU if we are on the CPU */
      ierr = VecCopy_SeqHIP_Private(xin,yin);CHKERRQ(ierr);
    } else if (xin->offloadmask == PETSC_OFFLOAD_BOTH) {
      /* if xin is valid in both places, see where yin is and copy there (because it's probably where we'll want to next use it) */
      if (yin->offloadmask == PETSC_OFFLOAD_CPU) {
        /* copy in CPU */
        ierr = VecCopy_SeqHIP_Private(xin,yin);CHKERRQ(ierr);
      } else if (yin->offloadmask == PETSC_OFFLOAD_GPU) {
        /* copy in GPU */
        ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
        ierr = VecHIPGetArrayWrite(yin,&yarray);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
        err  = hipMemcpy(yarray,xarray,yin->map->n*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayWrite(yin,&yarray);CHKERRQ(ierr);
      } else if (yin->offloadmask == PETSC_OFFLOAD_BOTH) {
        /* xin and yin are both valid in both places (or yin was unallocated before the earlier call to allocatecheck
           default to copy in GPU (this is an arbitrary choice) */
        ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
        ierr = VecHIPGetArrayWrite(yin,&yarray);CHKERRQ(ierr);
        ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
        err  = hipMemcpy(yarray,xarray,yin->map->n*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
        ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
        ierr = VecHIPRestoreArrayWrite(yin,&yarray);CHKERRQ(ierr);
      } else {
        ierr = VecCopy_SeqHIP_Private(xin,yin);CHKERRQ(ierr);
      }
    }
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecSwap_SeqHIP(Vec xin,Vec yin)
{
  PetscErrorCode ierr;
  PetscBLASInt   one = 1,bn;
  PetscScalar    *xarray,*yarray;
  hipblasHandle_t hipblasv2handle;
  hipblasStatus_t hberr;
  hipError_t    err;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(xin->map->n,&bn);CHKERRQ(ierr);
  if (xin != yin) {
    ierr = VecHIPGetArray(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPGetArray(yin,&yarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXswap(hipblasv2handle,bn,xarray,one,yarray,one);CHKERRHIPBLAS(hberr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = VecHIPRestoreArray(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArray(yin,&yarray);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecAXPBY_SeqHIP(Vec yin,PetscScalar alpha,PetscScalar beta,Vec xin)
{
  PetscErrorCode    ierr;
  PetscScalar       a = alpha,b = beta;
  const PetscScalar *xarray;
  PetscScalar       *yarray;
  PetscBLASInt      one = 1, bn;
  hipblasHandle_t    hipblasv2handle;
  hipblasStatus_t    hberr;
  hipError_t       err;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(yin->map->n,&bn);CHKERRQ(ierr);
  if (a == (PetscScalar)0.0) {
    ierr = VecScale_SeqHIP(yin,beta);CHKERRQ(ierr);
  } else if (b == (PetscScalar)1.0) {
    ierr = VecAXPY_SeqHIP(yin,alpha,xin);CHKERRQ(ierr);
  } else if (a == (PetscScalar)1.0) {
    ierr = VecAYPX_SeqHIP(yin,beta,xin);CHKERRQ(ierr);
  } else if (b == (PetscScalar)0.0) {
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPGetArray(yin,&yarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    err = hipMemcpy(yarray,xarray,yin->map->n*sizeof(PetscScalar),hipMemcpyDeviceToDevice);CHKERRHIP(err);
    hberr = hipblasXscal(hipblasv2handle,bn,&alpha,yarray,one);CHKERRHIPBLAS(hberr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(xin->map->n);CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArray(yin,&yarray);CHKERRQ(ierr);
  } else {
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPGetArray(yin,&yarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXscal(hipblasv2handle,bn,&beta,yarray,one);CHKERRHIPBLAS(hberr);
    hberr = hipblasXaxpy(hipblasv2handle,bn,&alpha,xarray,one,yarray,one);CHKERRHIPBLAS(hberr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = VecHIPRestoreArray(yin,&yarray);CHKERRQ(ierr);
    err  = WaitForHIP();CHKERRHIP(err);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(3.0*xin->map->n);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecAXPBYPCZ_SeqHIP(Vec zin,PetscScalar alpha,PetscScalar beta,PetscScalar gamma,Vec xin,Vec yin)
{
  PetscErrorCode ierr;
  hipError_t    err;
  PetscInt       n = zin->map->n;

  PetscFunctionBegin;
  if (gamma == (PetscScalar)1.0) {
    /* z = ax + b*y + z */
    ierr = VecAXPY_SeqHIP(zin,alpha,xin);CHKERRQ(ierr);
    ierr = VecAXPY_SeqHIP(zin,beta,yin);CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(4.0*n);CHKERRQ(ierr);
  } else {
    /* z = a*x + b*y + c*z */
    ierr = VecScale_SeqHIP(zin,gamma);CHKERRQ(ierr);
    ierr = VecAXPY_SeqHIP(zin,alpha,xin);CHKERRQ(ierr);
    ierr = VecAXPY_SeqHIP(zin,beta,yin);CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(5.0*n);CHKERRQ(ierr);
  }
  err  = WaitForHIP();CHKERRHIP(err);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecPointwiseMult_SeqHIP(Vec win,Vec xin,Vec yin)
{
  PetscInt                              n = win->map->n;
  const PetscScalar                     *xarray,*yarray;
  PetscScalar                           *warray;
  thrust::device_ptr<const PetscScalar> xptr,yptr;
  thrust::device_ptr<PetscScalar>       wptr;
  PetscErrorCode                        ierr;
  hipError_t                           err;

  PetscFunctionBegin;
  ierr = VecHIPGetArray(win,&warray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPGetArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  try {
    wptr = thrust::device_pointer_cast(warray);
    xptr = thrust::device_pointer_cast(xarray);
    yptr = thrust::device_pointer_cast(yarray);
    thrust::transform(xptr,xptr+n,yptr,wptr,thrust::multiplies<PetscScalar>());
    err  = WaitForHIP();CHKERRHIP(err);
  } catch (char *ex) {
    SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Thrust error: %s", ex);
  }
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArrayRead(yin,&yarray);CHKERRQ(ierr);
  ierr = VecHIPRestoreArray(win,&warray);CHKERRQ(ierr);
  ierr = PetscLogGpuFlops(n);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* should do infinity norm in hip */

PETSC_INTERN PetscErrorCode VecNorm_SeqHIP(Vec xin,NormType type,PetscReal *z)
{
  PetscErrorCode    ierr;
  PetscInt          n = xin->map->n;
  PetscBLASInt      one = 1, bn;
  const PetscScalar *xarray;
  hipblasHandle_t    hipblasv2handle;
  hipblasStatus_t    hberr;
  hipError_t       err;

  PetscFunctionBegin;
  ierr = PetscHIPBLASGetHandle(&hipblasv2handle);CHKERRQ(ierr);
  ierr = PetscBLASIntCast(n,&bn);CHKERRQ(ierr);
  if (type == NORM_2 || type == NORM_FROBENIUS) {
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXnrm2(hipblasv2handle,bn,xarray,one,z);CHKERRHIPBLAS(hberr);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(PetscMax(2.0*n-1,0.0));CHKERRQ(ierr);
  } else if (type == NORM_INFINITY) {
    int  i;
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasIXamax(hipblasv2handle,bn,xarray,one,&i);CHKERRHIPBLAS(hberr);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    if (bn) {
      PetscScalar zs;
      err = hipMemcpy(&zs,xarray+i-1,sizeof(PetscScalar),hipMemcpyDeviceToHost);CHKERRHIP(err);
      *z = PetscAbsScalar(zs);
    } else *z = 0.0;
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
  } else if (type == NORM_1) {
    ierr = VecHIPGetArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
    hberr = hipblasXasum(hipblasv2handle,bn,xarray,one,z);CHKERRHIPBLAS(hberr);
    ierr = VecHIPRestoreArrayRead(xin,&xarray);CHKERRQ(ierr);
    ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
    ierr = PetscLogGpuFlops(PetscMax(n-1.0,0.0));CHKERRQ(ierr);
  } else if (type == NORM_1_AND_2) {
    ierr = VecNorm_SeqHIP(xin,NORM_1,z);CHKERRQ(ierr);
    ierr = VecNorm_SeqHIP(xin,NORM_2,z+1);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecDotNorm2_SeqHIP(Vec s, Vec t, PetscScalar *dp, PetscScalar *nm)
{
  PetscErrorCode ierr;
  hipError_t    err;
  PetscReal      n = s->map->n;

  PetscFunctionBegin;
  ierr = VecDot_SeqHIP(s,t,dp);CHKERRQ(ierr);
  ierr = VecDot_SeqHIP(t,t,nm);CHKERRQ(ierr);
  err  = WaitForHIP();CHKERRHIP(err);
  ierr = PetscLogGpuFlops(4.0*n);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecDestroy_SeqHIP(Vec v)
{
  PetscErrorCode ierr;
  hipError_t    err;

  PetscFunctionBegin;
  if (v->spptr) {
    if (((Vec_HIP*)v->spptr)->GPUarray_allocated) {
      err = hipFree(((Vec_HIP*)v->spptr)->GPUarray_allocated);CHKERRHIP(err);
      ((Vec_HIP*)v->spptr)->GPUarray_allocated = NULL;
    }
    if (((Vec_HIP*)v->spptr)->stream) {
      err = hipStreamDestroy(((Vec_HIP*)v->spptr)->stream);CHKERRHIP(err);
    }
  }
  ierr = VecDestroy_SeqHIP_Private(v);CHKERRQ(ierr);
  ierr = PetscFree(v->spptr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#if defined(PETSC_USE_COMPLEX)
struct conjugate
{
  __host__ __device__
    PetscScalar operator()(PetscScalar x)
    {
      return PetscConj(x);
    }
};
#endif

PETSC_INTERN PetscErrorCode VecConjugate_SeqHIP(Vec xin)
{
#if defined(PETSC_USE_COMPLEX)
  PetscScalar                     *xarray;
  PetscErrorCode                  ierr;
  PetscInt                        n = xin->map->n;
  thrust::device_ptr<PetscScalar> xptr;
  hipError_t                     err;

  PetscFunctionBegin;
  ierr = VecHIPGetArray(xin,&xarray);CHKERRQ(ierr);
  ierr = PetscLogGpuTimeBegin();CHKERRQ(ierr);
  try {
    xptr = thrust::device_pointer_cast(xarray);
    thrust::transform(xptr,xptr+n,xptr,conjugate());
    err  = WaitForHIP();CHKERRHIP(err);
  } catch (char *ex) {
    SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Thrust error: %s", ex);
  }
  ierr = PetscLogGpuTimeEnd();CHKERRQ(ierr);
  ierr = VecHIPRestoreArray(xin,&xarray);CHKERRQ(ierr);
#else
  PetscFunctionBegin;
#endif
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecGetLocalVector_SeqHIP(Vec v,Vec w)
{
  PetscErrorCode ierr;
  hipError_t    err;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(v,VEC_CLASSID,1);
  PetscValidHeaderSpecific(w,VEC_CLASSID,2);
  PetscCheckTypeName(w,VECSEQHIP);
  PetscCheckTypeNames(v,VECSEQCUDA,VECMPICUDA);
  if (w->data) {
    if (((Vec_Seq*)w->data)->array_allocated) {
      if (w->pinned_memory) {
        ierr = PetscMallocSetHIPHost();CHKERRQ(ierr);
      }
      ierr = PetscFree(((Vec_Seq*)w->data)->array_allocated);CHKERRQ(ierr);
      if (w->pinned_memory) {
        ierr = PetscMallocResetHIPHost();CHKERRQ(ierr);
        w->pinned_memory = PETSC_FALSE;
      }
    }
    ((Vec_Seq*)w->data)->array = NULL;
    ((Vec_Seq*)w->data)->unplacedarray = NULL;
  }
  if (w->spptr) {
    PetscCheckTypeNames(v,VECSEQCUDA,VECMPICUDA);
    if (((Vec_HIP*)w->spptr)->GPUarray) {
      err = hipFree(((Vec_HIP*)w->spptr)->GPUarray);CHKERRHIP(err);
      ((Vec_HIP*)w->spptr)->GPUarray = NULL;
    }
    if (((Vec_HIP*)v->spptr)->stream) {
      err = hipStreamDestroy(((Vec_HIP*)w->spptr)->stream);CHKERRHIP(err);
    }
    ierr = PetscFree(w->spptr);CHKERRQ(ierr);
  }

  if (v->petscnative) {
    ierr = PetscFree(w->data);CHKERRQ(ierr);
    w->data = v->data;
    w->offloadmask = v->offloadmask;
    w->pinned_memory = v->pinned_memory;
    w->spptr = v->spptr;
    ierr = PetscObjectStateIncrease((PetscObject)w);CHKERRQ(ierr);
  } else {
    ierr = VecGetArray(v,&((Vec_Seq*)w->data)->array);CHKERRQ(ierr);
    w->offloadmask = PETSC_OFFLOAD_CPU;
    ierr = VecHIPAllocateCheck(w);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode VecRestoreLocalVector_SeqHIP(Vec v,Vec w)
{
  PetscErrorCode ierr;
  hipError_t    err;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(v,VEC_CLASSID,1);
  PetscValidHeaderSpecific(w,VEC_CLASSID,2);
  PetscCheckTypeNames(v,VECSEQHIP,VECMPIHIP);
  PetscCheckTypeName(w,VECSEQHIP);

  if (v->petscnative) {
    v->data = w->data;
    v->offloadmask = w->offloadmask;
    v->pinned_memory = w->pinned_memory;
    v->spptr = w->spptr;
    w->data = 0;
    w->offloadmask = PETSC_OFFLOAD_UNALLOCATED;
    w->spptr = 0;
  } else {
    ierr = VecRestoreArray(v,&((Vec_Seq*)w->data)->array);CHKERRQ(ierr);
    if ((Vec_HIP*)w->spptr) {
      err = hipFree(((Vec_HIP*)w->spptr)->GPUarray);CHKERRHIP(err);
      ((Vec_HIP*)w->spptr)->GPUarray = NULL;
      if (((Vec_HIP*)v->spptr)->stream) {
        err = hipStreamDestroy(((Vec_HIP*)w->spptr)->stream);CHKERRHIP(err);
      }
      ierr = PetscFree(w->spptr);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}
