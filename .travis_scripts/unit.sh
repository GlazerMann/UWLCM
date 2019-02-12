#!/usr/bin/env sh
set -ex

# UWLCM in RelWithDebInfo mode
cd UWLCM
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
VERBOSE=1 make
cd tests/unit
OMP_NUM_THREADS=1 make test || cat Testing/Temporary/LastTest.log / # "/" intentional! (just to make cat exit with an error code)
cd ../..

set +ex # see https://github.com/travis-ci/travis-ci/issues/6522
