#!/bin/bash

XLINGS_DIR=~/.xlings
PROJECT_DIR=`pwd`
XLINGS_CACHE_DIR=$PROJECT_DIR/.xlings

arg1=$1
arg2=$2

if [[ -z $arg1 || $arg1 == "help" || $arg1 == "-h" ]]; then
    cd $XLINGS_DIR/core
    xmake xlings
    exit 0
fi

if ! [ -f $PROJECT_DIR/config.xlings ]; then
    echo -e "\n\t**not found config.xlings in current folder**\n"
    cd $XLINGS_DIR/core
    xmake xlings
else
    # generate xlings cache file - .xlings
    mkdir -p $XLINGS_CACHE_DIR
    cp -f $PROJECT_DIR/config.xlings $XLINGS_CACHE_DIR/config.xlings.lua
    cp -f $XLINGS_DIR/xlings.template $XLINGS_CACHE_DIR/xmake.lua

    cd $XLINGS_CACHE_DIR

    xmake xlings $arg1 $arg2
fi