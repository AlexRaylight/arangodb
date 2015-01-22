#!/bin/bash
set -e

echo
echo '$0: loading precompiled libraries'

wget -q -O - "https://www.arangodb.com/support-files/travisCI/precompiled-libraries.tar.gz" | tar xzf - 

echo
echo '$0: setup make-system'

make setup

echo
echo "$0: configuring ArangoDB"
./configure --enable-relative

echo
echo "$0: compiling ArangoDB"

travis_wait make -j2

echo
echo "$0: done"
