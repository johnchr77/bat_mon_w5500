# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.cmf"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.hex"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.hxl"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.mum"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.o"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.sdb"
  "C:\\Users\\John Christie\\MPLABProjects\\maple_w5500_test\\out\\maple_w5500_test\\default.sym"
  )
endif()
