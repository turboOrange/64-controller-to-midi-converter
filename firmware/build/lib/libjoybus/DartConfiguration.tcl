# This file is configured by CMake automatically as DartConfiguration.tcl
# If you choose not to use CMake, this file may be hand configured, by
# filling in the required variables.


# Configuration directories and files
SourceDirectory: /home/cedrick/project/64-controller-to-midi-converter/firmware/lib/libjoybus
BuildDirectory: /home/cedrick/project/64-controller-to-midi-converter/firmware/build/lib/libjoybus

# Where to place the cost data store
CostDataFile: 

# Site is something like machine.domain, i.e. pragmatic.crd
Site: darter

# Build name is osname-revision-compiler, i.e. Linux-2.4.2-2smp-c++
BuildName: PICO-arm-none-eabi-g++

# Subprojects
LabelsForSubprojects: 

# Submission information
SubmitURL: http://
SubmitInactivityTimeout: 

# Dashboard start time
NightlyStartTime: 00:00:00 EDT

# Commands for the build/test/submit cycle
ConfigureCommand: "/nix/store/q800b7nrqa8csf2gcds2i83w1icsda5w-cmake-4.1.2/bin/cmake" "/home/cedrick/project/64-controller-to-midi-converter/firmware/lib/libjoybus"
MakeCommand: /nix/store/q800b7nrqa8csf2gcds2i83w1icsda5w-cmake-4.1.2/bin/cmake --build . --config "${CTEST_CONFIGURATION_TYPE}"
DefaultCTestConfigurationType: Release

# version control
UpdateVersionOnly: 

# CVS options
# Default is "-d -P -A"
CVSCommand: 
CVSUpdateOptions: 

# Subversion options
SVNCommand: 
SVNOptions: 
SVNUpdateOptions: 

# Git options
GITCommand: /nix/store/7yvcckar1lzhqnr0xx2n19nsdjd4qa4d-git-2.53.0/bin/git
GITInitSubmodules: 
GITUpdateOptions: 
GITUpdateCustom: 

# Perforce options
P4Command: 
P4Client: 
P4Options: 
P4UpdateOptions: 
P4UpdateCustom: 

# Generic update command
UpdateCommand: /nix/store/7yvcckar1lzhqnr0xx2n19nsdjd4qa4d-git-2.53.0/bin/git
UpdateOptions: 
UpdateType: git

# Compiler info
Compiler: /nix/store/jg6gb2j5gpycgmvhkcwq20ghfw6vdcyl-gcc-arm-embedded-15.2.rel1/bin/arm-none-eabi-g++
CompilerVersion: 15.2.1

# Dynamic analysis (MemCheck)
PurifyCommand: 
ValgrindCommand: 
ValgrindCommandOptions: 
DrMemoryCommand: 
DrMemoryCommandOptions: 
CudaSanitizerCommand: 
CudaSanitizerCommandOptions: 
MemoryCheckType: 
MemoryCheckSanitizerOptions: 
MemoryCheckCommand: /etc/profiles/per-user/cedrick/bin/valgrind
MemoryCheckCommandOptions: 
MemoryCheckSuppressionFile: 

# Coverage
CoverageCommand: /nix/store/lvwga6ivl1d4lnw0zis9ajs0rqx9gp4i-gcc-15.2.0/bin/gcov
CoverageExtraFlags: -l

# Testing options
# TimeOut is the amount of time in seconds to wait for processes
# to complete during testing.  After TimeOut seconds, the
# process will be summarily terminated.
# Currently set to 25 minutes
TimeOut: 1500

# During parallel testing CTest will not start a new test if doing
# so would cause the system load to exceed this value.
TestLoad: 

TLSVerify: 
TLSVersion: 

UseLaunchers: 
CurlOptions: 
# warning, if you add new options here that have to do with submit,
# you have to update cmCTestSubmitCommand.cxx

# For CTest submissions that timeout, these options
# specify behavior for retrying the submission
CTestSubmitRetryDelay: 5
CTestSubmitRetryCount: 3
