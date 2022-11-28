#!/bin/bash
cd gumbo-source
./autogen.sh
./configure
make
cp .libs/libgumbo.a src/*.h ../gumbo/