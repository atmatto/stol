#!/bin/bash
cd cpr-source
cmake -DCMAKE_BUILD_TYPE=Release -DCPR_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF
make
cp -r lib/* include/cpr/* cpr_generated_includes/cpr/* ../cpr
cd ../cpr
find . -type f -name '*.h' | xargs sed -i 's/\"cpr\//\"/g'