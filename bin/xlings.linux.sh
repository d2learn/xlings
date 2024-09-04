#!/bin/bash

XLINGS_DIR=~/.xlings
PROJECT_DIR=`pwd`
XLINGS_CACHE_DIR=$PROJECT_DIR/.xlings

arg=$1

if ! [ -f $PROJECT_DIR/config.xlings ]; then
    echo -e "xlings: not found config.xlings"
    exit 0
fi

# generate xlings cache file - .xlings
mkdir -p $XLINGS_CACHE_DIR
cp -f $PROJECT_DIR/config.xlings $XLINGS_CACHE_DIR/config.xlings.lua
cp -f $XLINGS_DIR/xlings.template $XLINGS_CACHE_DIR/xmake.lua

cd $XLINGS_CACHE_DIR

# if arg is null, then run checker
if [ -z $arg ]; then
    xmake xlings checker
elif [[ $arg == "help" || $arg == "-h" ]]; then
    xmake xlings
else
    xmake xlings $arg
fi