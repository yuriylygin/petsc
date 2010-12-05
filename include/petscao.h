/* 
  An application ordering is mapping between an application-centric
  ordering (the ordering that is "natural" for the application) and 
  the parallel ordering that PETSc uses.
*/
#if !defined(__PETSCAO_H)
#define __PETSCAO_H
#include "petscis.h"
#include "petscmat.h"
PETSC_EXTERN_CXX_BEGIN

typedef enum {AO_BASIC=0, AO_ADVANCED=1, AO_MAPPING=2} AOType;

/*S
     AO - Abstract PETSc object that manages mapping between different global numbering

   Level: intermediate

  Concepts: global numbering

.seealso:  AOCreateBasic(), AOCreateBasicIS(), AOPetscToApplication(), AOView(), AOApplicationToPetsc()
S*/
typedef struct _p_AO* AO;

/* Logging support */
extern PetscClassId PETSCDM_DLLEXPORT AO_CLASSID;

extern PetscErrorCode PETSCDM_DLLEXPORT AOInitializePackage(const char[]);

extern PetscErrorCode PETSCDM_DLLEXPORT AOCreateBasic(MPI_Comm,PetscInt,const PetscInt[],const PetscInt[],AO*);
extern PetscErrorCode PETSCDM_DLLEXPORT AOCreateBasicIS(IS,IS,AO*);

extern PetscErrorCode PETSCDM_DLLEXPORT AOCreateMapping(MPI_Comm,PetscInt,const PetscInt[],const PetscInt[],AO*);
extern PetscErrorCode PETSCDM_DLLEXPORT AOCreateMappingIS(IS,IS,AO*);

extern PetscErrorCode PETSCDM_DLLEXPORT AOView(AO,PetscViewer);
extern PetscErrorCode PETSCDM_DLLEXPORT AODestroy(AO);

extern PetscErrorCode PETSCDM_DLLEXPORT AORegister(const char [], const char [], const char [], PetscErrorCode (*)(AO));
#if defined(PETSC_USE_DYNAMIC_LIBRARIES)
#define AORegisterDynamic(a,b,c,d) AORegister(a,b,c,0)
#else
#define AORegisterDynamic(a,b,c,d) AORegister(a,b,c,d)
#endif

extern PetscErrorCode PETSCDM_DLLEXPORT AOPetscToApplication(AO,PetscInt,PetscInt[]);
extern PetscErrorCode PETSCDM_DLLEXPORT AOApplicationToPetsc(AO,PetscInt,PetscInt[]);
extern PetscErrorCode PETSCDM_DLLEXPORT AOPetscToApplicationIS(AO,IS);
extern PetscErrorCode PETSCDM_DLLEXPORT AOApplicationToPetscIS(AO,IS);

extern PetscErrorCode PETSCDM_DLLEXPORT AOPetscToApplicationPermuteInt(AO, PetscInt, PetscInt[]);
extern PetscErrorCode PETSCDM_DLLEXPORT AOApplicationToPetscPermuteInt(AO, PetscInt, PetscInt[]);
extern PetscErrorCode PETSCDM_DLLEXPORT AOPetscToApplicationPermuteReal(AO, PetscInt, PetscReal[]);
extern PetscErrorCode PETSCDM_DLLEXPORT AOApplicationToPetscPermuteReal(AO, PetscInt, PetscReal[]);

extern PetscErrorCode PETSCDM_DLLEXPORT AOMappingHasApplicationIndex(AO, PetscInt, PetscBool  *);
extern PetscErrorCode PETSCDM_DLLEXPORT AOMappingHasPetscIndex(AO, PetscInt, PetscBool  *);

/* ----------------------------------------------------*/
PETSC_EXTERN_CXX_END
#endif
