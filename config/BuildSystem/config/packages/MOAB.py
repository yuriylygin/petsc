import config.package

class Configure(config.package.GNUPackage):
  def __init__(self, framework):
    config.package.GNUPackage.__init__(self, framework)
    # To track MOAB.git, update gitcommit to 'git describe --always' or 'git rev-parse HEAD'
    self.gitcommit         = 'c4eed56fd6d2' # June 09, 2017, MOAB 5.0 release tag
    self.download          = ['git://https://bitbucket.org/fathomteam/moab.git','ftp://ftp.mcs.anl.gov/pub/fathom/moab-5.0.0.tar.gz']
    self.downloaddirnames  = ['moab']
    # Check for moab::Core and includes/libraries to verify build
    self.functions         = ['Core']
    self.functionsCxx      = [1, 'namespace moab {class Core {public: Core();};}','moab::Core *mb = new moab::Core()']
    self.includes          = ['moab/Core.hpp']
    self.liblist           = [['libiMesh.a', 'libMOAB.a'],['libMOAB.a']]
    self.cxx               = 1
    self.precisions        = ['single','double']
    self.hastests          = 1
    return

  def setupDependencies(self, framework):
    config.package.GNUPackage.setupDependencies(self, framework)
    self.compilerFlags  = framework.require('config.compilerFlags', self)
    self.blasLapack     = framework.require('config.packages.BlasLapack',self)
    self.mpi            = framework.require('config.packages.MPI', self)
    self.eigen          = framework.require('config.packages.eigen', self)
    self.hdf5           = framework.require('config.packages.hdf5', self)
    self.netcdf         = framework.require('config.packages.netcdf', self)
    self.metis          = framework.require('config.packages.metis',self)
    self.parmetis       = framework.require('config.packages.parmetis',self)
    self.ptscotch       = framework.require('config.packages.PTScotch',self)
    self.zoltan         = framework.require('config.packages.Zoltan', self)
    self.deps           = [self.mpi,self.blasLapack]
    self.odeps          = [self.eigen,self.hdf5,self.netcdf,self.metis,self.parmetis,self.ptscotch,self.zoltan]
    return

  def gitPreReqCheck(self):
    '''MOAB from the git repository needs the GNU autotools'''
    return self.programs.autoreconf and self.programs.libtoolize

  def formGNUConfigureArgs(self):
    '''Add MOAB specific configure arguments'''
    args = config.package.GNUPackage.formGNUConfigureArgs(self)
    if self.compilerFlags.debugging:
      args.append('--enable-debug')
    else:
      args.append('--enable-optimize')
    args.append('--with-mpi="'+self.mpi.directory+'"')
    args.append('--with-blas="'+self.libraries.toString(self.blasLapack.dlib)+'"')
    args.append('--with-lapack="'+self.libraries.toString(self.blasLapack.dlib)+'"')
    args.append('--enable-tools')
    args.append('--enable-imesh')
    if self.hdf5.found:
      args.append('--with-hdf5="'+self.hdf5.directory+'"')
    else:
      args.append('--without-hdf5')
    if self.netcdf.found:
      args.append('--with-netcdf="'+self.netcdf.directory+'"')
    else:
      args.append('--without-netcdf')
    if self.eigen.found:
      args.append('--with-eigen3="'+self.eigen.directory+'"')
    if self.metis.found:
      args.append('--with-metis="'+self.metis.directory+'"')
    if self.parmetis.found:
      args.append('--with-parmetis="'+self.parmetis.directory+'"')
    if self.ptscotch.found:
      args.append('--with-scotch="'+self.ptscotch.directory+'"')
    if self.zoltan.found:
      args.append('--with-zoltan="'+self.zoltan.directory+'"')

    return args

