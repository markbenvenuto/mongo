#!/bin/bash
set -o verbose

VERSION=2.4
NAME=gperftools
TARBALL=$NAME-$VERSION.tar.gz
TARBALL_DEST_DIR=$NAME-$VERSION
DEST_DIR=$NAME-$VERSION
PLATFORM=`uname | tr A-Z a-z`

if [ ! -f $TARBALL ]; then
    echo "Get tarball"
    wget https://github.com/gperftools/gperftools/releases/download/gperftools-$VERSION/gperftools-$VERSION.tar.gz
fi

#echo $TARBALL
#tar -zxvf $TARBALL

#mv $TARBALL_DEST_DIR $DEST_DIR

# Generate Config
cd $DEST_DIR
#./configure
mkdir build_$PLATFORM
#cp src/config.h build_$PLATFORM
rm src/config.h
cd ..

# Prune sources
echo "Prune tree"
rm -rf $DEST_DIR/doc
rm -rf $DEST_DIR/m4
rm -rf $DEST_DIR/packages
rm -rf $DEST_DIR/vsprojects
rm -f $DEST_DIR/Makefile* $DEST_DIR/config* $DEST_DIR/*sh
rm -f $DEST_DIR/compile* $DEST_DIR/depcomp $DEST_DIR/libtool
rm -f $DEST_DIR/test-driver $DEST_DIR/*.m4 $DEST_DIR/missing
rm -f $DEST_DIR/*.sln
##rm -f $DEST_DIR/*.cmake.in

## Note: There are no config.h or other build artifacts to generate

