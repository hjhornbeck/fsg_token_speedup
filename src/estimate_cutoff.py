#!/usr/bin/env python3

from math import ceil, log, log1p
import re
import sys

token = re.compile(r'^(..)(..)-[\da-f]{16}-[\da-f]{16}-(.*)-(.*)-[\da-f]+$', re.IGNORECASE)
db = dict()

for line in sys.stdin:

    match = token.search(line)
    if match is None:
        continue

    version = int( match.group(1), 16 )
    filterS = int( match.group(2), 16 )
    biomes  = int( match.group(4), 16 )
    structs = int( match.group(3), 16 ) - biomes

    if version not in db:
        db[version] = dict()
    if filterS not in db[version]:
        db[version][filterS] = [0.5, 0, 0.5]

    db[version][filterS][0] += 1   # alpha for both conjugate priors
    db[version][filterS][1] += ((biomes - 1)  % 65536) + 1 # beta, finding biome
    db[version][filterS][2] += max((biomes-1) // 65536, 0) # beta, biome possible

    # print out the updated estimate for this version
    print( f"uint32_t biome_cutoffs[{len(db[version])+1}] = " + "{", end='' )

    for key in sorted( db[version].keys() ):

        fb_mean = db[version][key][0] / db[version][key][1]
        bp_mean = db[version][key][0] / (db[version][key][0] + db[version][key][2])
        try:
            cutoff = ceil( (log1p( -bp_mean ) - log( bp_mean ))/log1p( -fb_mean ) )
        except:
            cutoff = 65536
        print( f"{cutoff}, ", end='' )

    print( "0};\t// " + f"VERSION == {version}", flush=True )
#    print( f"// DEBUG: ({version},{key}) = {db[version][key]}" )
