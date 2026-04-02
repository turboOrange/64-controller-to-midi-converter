# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/nix/store/03dkps46n9h5n7bss8c13yqx3jqjjxss-pico-sdk-2.2.0/lib/pico-sdk/tools/pioasm")
  file(MAKE_DIRECTORY "/nix/store/03dkps46n9h5n7bss8c13yqx3jqjjxss-pico-sdk-2.2.0/lib/pico-sdk/tools/pioasm")
endif()
file(MAKE_DIRECTORY
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pioasm"
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pioasm-install"
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/tmp"
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp"
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/src"
  "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/cedrick/project/64-controller-to-midi-converter/firmware/build/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
