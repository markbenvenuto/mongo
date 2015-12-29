#!/bin/bash
# This script download and imports boost via the boost bcp utility.
# It can be run on Linux or Mac OS X.
# Actual integration into the build system is not done by this script.
#
set -o errexit
set -o verbose

SRC_ROOT=/tmp
TARBALL=boost_1_60_0.7z
SRC=$SRC_ROOT/boost_1_60_0
DEST=boost-1.60.0
DEST_DIR=`pwd`/$DEST

cd $SRC_ROOT

if [ ! -f $TARBALL ]; then
    echo "Get tarball"
    wget http://downloads.sourceforge.net/project/boost/boost/1.60.0/$TARBALL
fi

if [ ! -d $SRC ]; then
    echo "Extract tarball"
    7z x $TARBALL
fi

# Build the bcp tool
# The bcp tool is a boost specific tool that allows importing a subset of boost
# The downside is that it copies a lot of unnecessary stuff in libs
# and does not understand #ifdefs
#
cd $SRC

./bootstrap.sh

./b2 tools/bcp

test -d $_DEST_DIR || mkdir $DEST_DIR
$SRC/dist/bin/bcp --boost=$SRC/ algorithm array bind chrono config container date_time filesystem function integer intrusive noncopyable optional program_options random smart_ptr static_assert thread unordered utility $DEST_DIR

# Trim files
cd $DEST_DIR

rm -f Jamroot boost.png
rm -rf doc

# Trim misc directories from libs that bcp pulled in
find libs -type d -name test | xargs rm -rf
find libs -type d -name doc | xargs rm -rf
find libs -type d -name build | xargs rm -rf
find libs -type d -name examples | xargs rm -rf
find libs -type d -name example | xargs rm -rf
find libs -type d -name meta | xargs rm -rf
find libs -type d -name tutorial | xargs rm -rf
find libs -type d -name performance | xargs rm -rf
find libs -type d -name bench | xargs rm -rf
find libs -type d -name perf | xargs rm -rf
find libs -type d -name proj | xargs rm -rf
find libs -type d -name xmldoc | xargs rm -rf
find libs -type d -name tools | xargs rm -rf
find libs -type d -name extra | xargs rm -rf
find libs -type d -name bug | xargs rm -rf

find libs -name "*.html" | xargs rm -f
find libs -name "*.htm" | xargs rm -f
find libs -name "*.zip" | xargs rm -f
find libs -name "*.gif" | xargs rm -f

find libs -type d -empty | xargs rmdir

# Full of unneeded code
rm -rf libs/algorithm
rm -rf libs/config
rm -rf libs/static_assert

# Trim the include directory for the stuff bcp dragged in and we do not need
# since they are 1+ MB each
rm -f boost/typeof/vector100.hpp
rm -f boost/typeof/vector150.hpp
rm -f boost/typeof/vector200.hpp

# Remove compat files for compilers we do not support
find boost -type d -name dmc | xargs rm -rf
find boost -type d -name "bcc*" | xargs rm -rf
find boost -type d -name mwcw | xargs rm -rf
find boost -type d -name msvc60 | xargs rm -rf
find boost -type d -name msvc70 | xargs rm -rf

echo "Done"
