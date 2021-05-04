import args
import config.compile.processor
import config.compile.C
import config.framework
import config.libraries
import os
import sys

import config.setsOrdered as sets

'''
SYCL is a C++ compiler with extensions to support the SYCL programming model.
Because of it's slowness, and in some ways the exensions make it a new language,
we have a separate compiler for it.
We use the extension .sycl.cxx to denote these files similar to what is done
for HIP (which is also C++ and has similar issue).
'''
class Preprocessor(config.compile.processor.Processor):
  '''The SYCL preprocessor'''
  def __init__(self, argDB):
    config.compile.processor.Processor.__init__(self, argDB, 'SYCLPP', 'SYCLPPFLAGS', '.sycl.cxx', '.sycl.cxx')
    self.language        = 'SYCL'
    self.includeDirectories = sets.Set()
    return

class Compiler(config.compile.processor.Processor):
  '''The SYCL compiler'''
  def __init__(self, argDB, usePreprocessorFlags = False):
    config.compile.processor.Processor.__init__(self, argDB, 'SYCLCXX', 'SYCLCXXFLAGS', '.sycl.cxx', '.o')
    self.language        = 'SYCL'
    self.requiredFlags[-1]  = '-c'
    self.outputFlag         = '-o'
    self.includeDirectories = sets.Set()
    if usePreprocessorFlags:
      self.flagsName.extend(Preprocessor(argDB).flagsName)

    return

  def getTarget(self, source):
    '''Return None for header files'''
    import os

    base, ext = os.path.splitext(source)
    if ext in ['.h', '.hh', '.hpp']:
      return None
    return base+'.o'

  def getCommand(self, sourceFiles, outputFile = None):
    '''If no outputFile is given, do not execute anything'''
    if outputFile is None:
      return 'true'
    return config.compile.processor.Processor.getCommand(self, sourceFiles, outputFile)

class Linker(config.compile.C.Linker):
  '''The SYCLCXX linker'''
  def __init__(self, argDB):
    self.compiler        = Compiler(argDB, usePreprocessorFlags = False)
    self.configLibraries = config.libraries.Configure(config.framework.Framework(clArgs = '', argDB = argDB, tmpDir = os.getcwd()))
    config.compile.processor.Processor.__init__(self, argDB,
                                                [self.compiler.name], ['SYCLCXX_LINKER_FLAGS'], '.o', '.a')
    self.language   = 'SYCL'
    self.outputFlag = '-o'
    self.libraries  = sets.Set()
    return

  def getExtraArguments(self):
    if not hasattr(self, '_extraArguments'):
      return ''
    return self._extraArguments
  extraArguments = property(getExtraArguments, config.compile.processor.Processor.setExtraArguments, doc = 'Optional arguments for the end of the command')

class SharedLinker(config.compile.C.SharedLinker):
  '''The SYCLCXX shared linker: Just use Cxx linker for now'''
  def __init__(self, argDB):
    config.compile.Cxx.SharedLinker.__init__(self, argDB)
    self.language = 'SYCL'
    return

class StaticLinker(config.compile.C.StaticLinker):
  '''The SYCLCXX static linker, just use Cxx for now'''
  def __init__(self, argDB):
    config.compile.Cxx.StaticLinker.__init__(self, argDB)
    self.language = 'SYCL'
    return

class DynamicLinker(config.compile.C.DynamicLinker):
  '''The SYCL dynamic linker, just use Cxx for now'''
  def __init__(self, argDB):
    config.compile.Cxx.DynamicLinker.__init__(self, argDB)
    self.language = 'SYCL'
    return
