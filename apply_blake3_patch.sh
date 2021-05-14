#!/bin/sh

# has the BLAKE3 library been installed?
if ! [ -s libs/libblake3.so ] || ! [ -s include/blake3.h ] ; then 

	git clone https://github.com/BLAKE3-team/BLAKE3.git
	( cd BLAKE3/c
	  gcc -fPIC -shared -O3 -o libblake3.so \
		  blake3.c blake3_dispatch.c blake3_portable.c \
		  blake3_sse2_x86-64_unix.S blake3_sse41_x86-64_unix.S \
		  blake3_avx2_x86-64_unix.S blake3_avx512_x86-64_unix.S
	  )
	cp BLAKE3/c/libblake3.so libs/
	cp BLAKE3/c/blake3.h include/

	fi

# has the source file been patched?
if [ ! -f .PATCHED ] ; then

	cp csprng.c csprng.c.original

	perl -i -ne '
	s/^#define VERSION .*/#define VERSION 37/;
	next if(/^#define FILTER_TYPE .*/);
	if(/int main \(/){
		print "int main () {";
		last;
		}
	else{ print; }
	' csprng.c

	if ! curl "https://raw.githubusercontent.com/hjhornbeck/fsg_token_speedup/main/para_blake3_64x_biomes_adj.diff" | \
		patch -l -i - csprng.c ; then

	echo 'An error occurred while patching! Compare csprng.c.original to csprng.c.rej, and try to fix the discrepancy.'

	else
		touch .PATCHED
	fi

fi

# finally, we compile
if [ -s libs/libblake3.so ] && [ -s include/blake3.h ] && [ -f .PATCHED ] ; then 

	gcc csprng.c -I./include -L./libs -O3 -mtune=native -lblake3 -L. \
		-lcubiomes -lm -pthread -Wl,-rpath=./libs/ -lminecraft_nether_gen_rs \
		-o seed

fi
