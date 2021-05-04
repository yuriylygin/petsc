====================
Changes: Development
====================

   .. rubric:: General:

   -  Change ``MPIU_Allreduce()`` to always returns a MPI error code that
      should be checked with ``CHKERRMPI(ierr)``

   .. rubric:: Configure/Build:

   .. rubric:: Sys:

   .. rubric:: PetscViewer:

   -  ``PetscViewerHDF5PushGroup()``: if input path begins with ``/``, it is
      taken as absolute, otherwise relative to the current group
   -  Add ``PetscViewerHDF5HasDataset()``
   -  ``PetscViewerHDF5HasAttribute()``,
      ``PetscViewerHDF5ReadAttribute()``,
      ``PetscViewerHDF5WriteAttribute()``,
      ``PetscViewerHDF5HasDataset()`` and
      ``PetscViewerHDF5HasGroup()``
      support absolute paths (starting with ``/``)
      and paths relative to the current pushed group
   -  Add input argument to ``PetscViewerHDF5ReadAttribute()`` for default
      value that is used if attribute is not found in the HDF5 file
   -  Add ``PetscViewerHDF5PushTimestepping()``,
      ``PetscViewerHDF5PopTimestepping()`` and
      ``PetscViewerHDF5IsTimestepping()`` to control timestepping mode.
   -  One can call ``PetscViewerHDF5IncrementTimestep()``,
      ``PetscViewerHDF5SetTimestep()`` or ``PetscViewerHDF5GetTimestep()`` only
      if timestepping mode is active
   -  Error if timestepped dataset is read/written out of timestepping mode, or
      vice-versa

   .. rubric:: PetscDraw:

   .. rubric:: AO:

   .. rubric:: IS:

   .. rubric:: VecScatter / PetscSF:

   .. rubric:: PF:

   .. rubric:: Vec:

   .. rubric:: PetscSection:

   .. rubric:: PetscPartitioner:

   .. rubric:: Mat:

   -  Factorization types now provide their preferred ordering (which
      may be ``MATORDERINGEXTERNAL``) to prevent PETSc PCFactor from, by
      default, picking an ordering when it is not ideal
   -  Deprecate ``MatFactorGetUseOrdering()``; Use
      ``MatFactorGetCanUseOrdering()`` instead
   -  Add ``--download-htool`` to use hierarchical matrices with the new
      type ``MATHTOOL``
   -  Add ``MATCENTERING`` special matrix type that implements action of the
      centering matrix

   .. rubric:: PC:

   .. rubric:: KSP:

   .. rubric:: SNES:

   .. rubric:: SNESLineSearch:

   .. rubric:: TS:

   .. rubric:: TAO:

   .. rubric:: DM/DA:

   -  Change management of auxiliary data in DM from object composition
      to ``DMGetAuxiliaryVec()``/``DMSetAuxiliaryVec()``, ``DMCopyAuxiliaryVec()``
   -  Remove ``DMGetNumBoundary()`` and ``DMGetBoundary()`` in favor of DS
      counterparts
   -  Remove ``DMCopyBoundary()``
   -  Change interface for ``DMAddBoundary()``, ``PetscDSAddBoundary()``,
      ``PetscDSGetBoundary()``, ``PetscDSUpdateBoundary()``

   .. rubric:: DMSwarm:

   -  Add ``DMSwarmGetCellSwarm()`` and ``DMSwarmRestoreCellSwarm()``

   .. rubric:: DMPlex:

   -  Add a ``PETSCVIEWEREXODUSII`` viewer type for ``DMView()``/``DMLoad()`` and
      ``VecView()``/``VecLoad()``. Note that not all DMPlex can be saved in exodusII
      format since this file format requires that the numbering of cell
      sets be compact
   -  Add ``PetscViewerExodusIIOpen()`` convenience function
   -  Add ``PetscViewerExodusIISetOrder()`` to
      generate "2nd order" elements (i.e. tri6, tet10, hex27) when using
      ``DMView`` with a ``PETSCVIEWEREXODUSII`` viewer
   -  Change ``DMPlexComputeBdResidualSingle()`` and
      ``DMPlexComputeBdJacobianSingle()`` to take a form key

   .. rubric:: FE/FV:

   -  Change ``PetscFEIntegrateBdResidual()`` and
      ``PetscFEIntegrateBdJacobian()`` to take both ``PetscWeakForm`` and form
      key

   .. rubric:: DMNetwork:

   .. rubric:: DT:

   -  Add ``PetscWeakFormCopy()`` and ``PetscWeakFormRewriteKeys()``
   -  Add ``PetscDSDestroyBoundary()`` and ``PetscDSCopyExactSolutions()``

   .. rubric:: Fortran:
