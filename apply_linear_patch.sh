#!/bin/sh

# has the source file been patched?
if [ ! -f .PATCHED ] ; then

	echo Backing up the original source file...
	cp csprng.c linear.c

	echo Tidying up the original to allow a clean patch...
	perl -i -ne '
	s/^#define VERSION .*/#define VERSION 37/;
	next if(/^#define FILTER_TYPE .*/);
	if(/int main \(/){
		print "int main () {";
		last;
		}
	else{ print; }
	' linear.c

	echo Downloading and applying the patch...
	if ! curl "https://raw.githubusercontent.com/hjhornbeck/fsg_token_speedup/main/para_linear_biomes.diff" | \
		patch -l -i - linear.c ; then

	echo 'An error occurred while patching! Compare csprng.c to linear.c.rej, and try to fix the discrepancy.'

	else
		touch .PATCHED
	fi

fi

# finally, we compile
if [ -f .PATCHED ] ; then 

	echo Compiling the patched code...
	if gcc linear.c -I./include -L./libs -O3 -mtune=native -L. \
		-lcubiomes -lm -pthread -Wl,-rpath=./libs/ -lminecraft_nether_gen_rs \
		-o seed ; then

		echo "The code compiled successfully! Please edit linear.c to use the"
	        echo " appropriate version number, and re-run this script."

	fi	# compilation should be noisy enough to signify something went wrong
fi
