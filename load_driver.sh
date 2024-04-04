#! /usr/bin/env sh

CUR_DIR=$(dirname "$0")
cd "$CUR_DIR"/driver
rmmod svm_demo
insmod svm_demo.ko