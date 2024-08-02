if libunwind.found()
  softmmu_ss.add(libunwind)
endif

if libdw.found()
  softmmu_ss.add(libdw)
endif

softmmu_ss.add(files(
  'backward-cpp/backward.cpp'
))
