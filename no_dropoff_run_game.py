import os
import sys
from random import randint, shuffle

os.system('set -e')
os.system('cmake .')
os.system('make')

bots = ['./MyBot'] + ['./bots/Dec19NoDropoff'] * 3
# bots = ['./MyBot'] * 2

seed = randint(0, (1 << 31) - 1) if len(sys.argv) == 1 else sys.argv[1]

os.system('./halite --replay-directory replays/ --width 64 --height 64 --no-timeout -vvv --seed {} {}'.format(seed, ' '.join(bots)))
