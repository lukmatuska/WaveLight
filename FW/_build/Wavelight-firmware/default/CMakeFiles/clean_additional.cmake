# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.cmf"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.hex"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.hxl"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.mum"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.o"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.sdb"
  "/home/lcrm/Documents/WaveLight/FW/out/Wavelight-firmware/default.sym"
  )
endif()
