#!/usr/bin/env bash

set -e

cmake .
make
./halite --replay-directory replays/ -vvv --seed ${1:-$RANDOM} "./MyBot" "./MyBot_Nov6Inspire" "./MyBot_Nov6Inspire" "./MyBot_Nov6Inspire"
