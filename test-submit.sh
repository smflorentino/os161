#!/bin/bash
COMMIT_ID=3921f597
SUBMIT_DIR=/tmp/gridsubmit
SRC_DIR=$HOME/thegrid/src

cd $SRC_DIR
echo "Creating Diff of Current Work..."
git diff $COMMIT_ID > submit.patch

echo "Cleaning up old temp directory..."
rm -rf $SUBMIT_DIR
mkdir $SUBMIT_DIR
mv submit.patch $SUBMIT_DIR
echo "Cloning fresh repo from ops-class.org..."
cd $SUBMIT_DIR
git clone ssh://src@src.ops-class.org/src/os161 submit
cd submit
git checkout 3921f597
echo "Applying Patch..."
git apply $SUBMIT_DIR/submit.patch
echo "Copying Patch to Desktop..."
cp $SUBMIT_DIR/submit.patch $HOME/Desktop
