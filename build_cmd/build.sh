#!/bin/bash
cmake ../.
core_cnt=$(nproc --all)
make -j"$core_cnt" distributed