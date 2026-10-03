set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
find_program(CMAKE_C_COMPILER NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES x86_64-w64-mingw32-windres REQUIRED)
# Also supports extracted Debian toolchains without update-alternatives symlinks.
set(CMAKE_RC_FLAGS_INIT "--preprocessor=${CMAKE_C_COMPILER} --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED")
