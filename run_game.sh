#!/usr/bin/env bash

set -e

cmake .
make
./halite --replay-directory replays/ -vvv --seed ${1:-$RANDOM} "./MyBot" "./MyBot_S_Cost" "./MyBot_7" "./MyBot_S"
