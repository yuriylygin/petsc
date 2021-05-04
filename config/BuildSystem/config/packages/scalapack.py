import config.package

class Configure(config.package.Package):
  def __init__(self, framework):
    config.package.Package.__init__(self, framework)
    self.gitcommit        = 'v2.1.0-p2'  # modification to avoid calling zdotc, zladiv on MacOS
    self.download         = ['git://https://bitbucket.org/petsc/pkg-scalapack','https://bitbucket.org/petsc/pkg-scalapack/get/'+self.gitcommit+'.tar.gz']
    self.downloaddirnames = ['petsc-pkg-scalapack','scalapack']
    self.includes         = []
    self.liblist          = [['libscalapack.a'],
                             ['libmkl_scalapack_lp64.a','libmkl_blacs_intelmpi_lp64.a'],
                             ['libmkl_scalapack_lp64.a','libmkl_blacs_mpich_lp64.a'],
                             ['libmkl_scalapack_lp64.a','libmkl_blacs_sgimpt_lp64.a'],
                             ['libmkl_scalapack_lp64.a','libmkl_blacs_openmpi_lp64.a']]
    self.functions        = ['pssytrd']
    self.functionsFortran = 1
    self.fc               = 1
    self.precisions       = ['single','double']
    self.downloadonWindows= 1
    return

  def setupDependencies(self, framework):
    config.package.Package.setupDependencies(self, framework)
    self.flibs      = framework.require('config.packages.flibs',self)
    self.blasLapack = framework.require('config.packages.BlasLapack',self)
    self.mpi        = framework.require('config.packages.MPI',self)
    self.deps       = [self.mpi, self.blasLapack, self.flibs]
    return

  def Install(self):
    import os
    g = open(os.path.join(self.packageDir,'SLmake.inc'),'w')
    g.write('SCALAPACKLIB = '+'libscalapack.'+self.setCompilers.AR_LIB_SUFFIX+' \n')
    g.write('LIBS         = '+self.libraries.toString(self.blasLapack.dlib)+'\n')
    g.write('MPIINC       = '+self.headers.toString(self.mpi.include)+'\n')
    # this mangling information is for both BLAS and the Fortran compiler so cannot use the BlasLapack mangling flag
    if self.compilers.fortranManglingDoubleUnderscore:
      fdef = '-Df77IsF2C -DFortranIsF2C'
    elif self.compilers.fortranMangling == 'underscore':
      fdef = '-DAdd_'
    elif self.compilers.fortranMangling == 'caps':
      fdef = '-DUpCase'
    else:
      fdef = '-DNoChange'
    g.write('CDEFS        = '+fdef+'\n')
    self.pushLanguage('FC')
    g.write('FC           = '+self.getCompiler()+'\n')
    g.write('FCFLAGS      = '+self.updatePackageFFlags(self.getCompilerFlags())+'\n')
    g.write('FCLOADER     = '+self.getLinker()+'\n')
    g.write('FCLOADFLAGS  = '+self.getLinkerFlags()+'\n')
    self.popLanguage()
    self.pushLanguage('C')
    g.write('CC           = '+self.getCompiler()+'\n')
    g.write('CCFLAGS      = '+self.updatePackageCFlags(self.getCompilerFlags())+' $(MPIINC)\n')
    noopt = self.checkNoOptFlag()
    g.write('CFLAGS       = '+noopt+ ' '+self.getSharedFlag(self.getCompilerFlags())+' '+self.getPointerSizeFlag(self.getCompilerFlags())+' '+self.getWindowsNonOptFlags(self.getCompilerFlags())+'\n')

    g.write('CCLOADER     = '+self.getLinker()+'\n')
    g.write('CCLOADFLAGS  = '+self.getLinkerFlags()+'\n')
    self.popLanguage()
    g.write('ARCH         = '+self.setCompilers.AR+'\n')
    g.write('ARCHFLAGS    = '+self.setCompilers.AR_FLAGS+'\n')
    g.write('RANLIB       = '+self.setCompilers.RANLIB+'\n')
    g.close()

    if self.installNeeded('SLmake.inc'):
      try:
        output,err,ret  = config.package.Package.executeShellCommand('cd '+self.packageDir+' && '+self.make.make+' -f Makefile.parallel cleanlib', timeout=60, log = self.log)
      except RuntimeError as e:
        pass
      try:
        self.logPrintBox('Compiling and installing Scalapack; this may take several minutes')
        self.installDirProvider.printSudoPasswordMessage()
        libDir = os.path.join(self.installDir, self.libdir)
        output,err,ret  = config.package.Package.executeShellCommand('cd '+self.packageDir+' && '+self.make.make_jnp+' -f Makefile.parallel lib && '+self.installSudo+'mkdir -p '+libDir+' && '+self.installSudo+'cp libscalapack.* '+libDir, timeout=2500, log = self.log)
      except RuntimeError as e:
        self.logPrint('Error running make on SCALAPACK: '+str(e))
        raise RuntimeError('Error running make on SCALAPACK')
      self.postInstall(output,'SLmake.inc')
    return self.installDir

def getSearchDirectories(self):
  '''Generate list of possible locations of Scalapack'''
  yield ''
  if os.getenv('MKLROOT'): yield os.getenv('MKLROOT')
