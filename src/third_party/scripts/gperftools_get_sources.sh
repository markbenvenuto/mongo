#!/bin/bash
set -o verbose
set -o errexit

VERSION=2.4
NAME=gperftools
TARBALL=$NAME-$VERSION.tar.gz
TARBALL_DIR=$NAME-$VERSION
TARBALL_DEST_DIR=$NAME-$VERSION
TEMP_DIR=/tmp/temp-$NAME-$VERSION
DEST_DIR=$NAME-$VERSION
DEST_DIR=`git rev-parse --show-toplevel`/src/third_party/$NAME-$VERSION
UNAME=`uname | tr A-Z a-z`
UNAME_PROCESSOR=`uname -p`

TARGET_UNAME="$UNAME"_$UNAME_PROCESSOR
echo TARGET_UNAME: $TARGET_UNAME

if [ ! -f $TARBALL ]; then
    echo "Get tarball"
    wget https://github.com/gperftools/gperftools/releases/download/$NAME-$VERSION/$NAME-$VERSION.tar.gz
fi

echo $TARBALL
tar -zxvf $TARBALL

rm -rf $TEMP_DIR
mv $TARBALL_DIR $TEMP_DIR

# Do a deep copy
if [ ! -d $DEST_DIR ]; then
    cp -r $TEMP_DIR $DEST_DIR || true
fi

# Generate Config
cd $TEMP_DIR
./configure
mkdir $DEST_DIR/build_$TARGET_UNAME || true
cp src/config.h $DEST_DIR/build_$TARGET_UNAME

# Prune sources
echo "Prune tree"
cd $DEST_DIR
rm -rf $DEST_DIR/doc
rm -rf $DEST_DIR/m4
rm -rf $DEST_DIR/packages
rm -rf $DEST_DIR/src/tests
rm -rf $DEST_DIR/vsprojects
rm -f $DEST_DIR/Makefile* $DEST_DIR/config* $DEST_DIR/*sh
rm -f $DEST_DIR/compile* $DEST_DIR/depcomp $DEST_DIR/libtool
rm -f $DEST_DIR/test-driver $DEST_DIR/*.m4 $DEST_DIR/missing
rm -f $DEST_DIR/*.sln
#rm -f $DEST_DIR/*.cmake.in

