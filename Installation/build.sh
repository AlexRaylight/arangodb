#!/bin/bash

echo "########################################################"
echo "#                     build.sh                         #"
echo "########################################################"
echo

. config/detect_distro.sh

OPTIONS="--disable-dependency-tracking --disable-relative"
PREFIX="--prefix=/usr --localstatedir=/var --sysconfdir=/etc"
RESULTS="arangod arangosh arangoimp"
USE_ICECC="no"

export CPPFLAGS=""
export LDFLAGS=""
export MAKEJ=2
export LDD_INFO="no"

while [ 0 -lt "$#" ];  do
  opt="$1"
  shift

  case "$opt" in
    --enable-icecc)
      USE_ICECC="yes"
      ;;
    *)
      echo "$0: unknown option '$opt'"
      exit 1
      ;;
  esac
done

HAS_ICECC=$(ps aux | grep -v "grep" | grep iceccd)

if [ "x$HAS_ICECC" != "x" -a "x$USE_ICECC" == "xyes" ] ; then
  export PATH=/usr/lib/icecc/bin/:/opt/icecream/bin/:$PATH
  export MAKEJ=14

  echo "########################################################"
  echo "Using ICECC"
  echo "   PATH=$PATH"
  echo "########################################################"
fi

echo
echo "########################################################"
echo "Building on $TRI_OS_LONG"
echo "########################################################"
echo

case $TRI_OS_LONG in

  Linux-ArchLinux*)
    echo "Using configuration for Arch Linux"
    OPTIONS="$OPTIONS --enable-mruby"
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Linux-LinuxMint-13*)
    echo "Using configuration for LinuxMint 13"
    OPTIONS="$OPTIONS --enable-mruby"
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Linux-openSUSE-12*)
    echo "Using configuration for openSuSE 12.X"
    OPTIONS="$OPTIONS --enable-flex --enable-bison --enable-mruby "
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Linux-openSUSE-11*)
    echo "Using configuration for openSuSE 11.X"
    OPTIONS="$OPTIONS --enable-mruby"
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Linux-Debian-6*)
    echo "Using configuration for Debian"
    OPTIONS="$OPTIONS --enable-all-in-one-libev --enable-all-in-one-v8 --enable-mruby"
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Linux-Debian*)
    echo "Using configuration for Debian"
    OPTIONS="$OPTIONS --enable-all-in-one-libev --enable-all-in-one-v8 --enable-all-in-one-icu --enable-mruby"
    LDD_INFO="yes"
    ;;

  Linux-CentOS-*)
    echo "Using configuration for Centos"
    OPTIONS="$OPTIONS --enable-all-in-one-libev --enable-all-in-one-v8 --enable-all-in-one-icu --enable-mruby"
    LDD_INFO="yes"
    ;;

  Linux-Ubuntu-*)
    echo "Using configuration for Ubuntu"
    OPTIONS="$OPTIONS --enable-all-in-one-libev --enable-all-in-one-v8 --enable-mruby"
    LDD_INFO="yes"
    RESULTS="$RESULTS arangoirb"
    ;;

  Darwin*)
    echo "Using configuration for DARWIN"
    CPPFLAGS='-isystem /usr/include -isystem /opt/local/include -Wno-deprecated-declarations'
    LDFLAGS='-L/usr/lib -L/opt/local/lib' # need to use OpenSSL from system
    OPTIONS="$OPTIONS --enable-mruby"
    RESULTS="$RESULTS arangoirb"
    ;;

  *)
    echo "Using default configuration"
    OPTIONS="$OPTIONS --enable-error-on-warning"
    ;;

esac

if [ ! -f configure ] ; then
echo "########################################################"
echo "create configure script:"
echo "   make setup"
echo "########################################################"
echo

make setup || exit 1
fi


echo
echo "########################################################"
echo "CPPFLAGS: $CPPFLAGS"
echo "LDFLAGS: $LDFLAGS"
echo "OPTIONS: $OPTIONS"
echo "########################################################"
echo
echo "########################################################"
echo "configure:"
echo "   ./configure $PREFIX $OPTIONS"
echo "########################################################"
echo

./configure $PREFIX $OPTIONS || exit 1

echo
echo "########################################################"
echo "compile:"
echo "    make -j $MAKEJ"
echo "########################################################"
echo

make -j $MAKEJ || exit 1

for result in $RESULTS;  do
  echo
  echo "########################################################"
  echo "$result"
  echo "########################################################"
  echo

  ident "bin/$result"

  if test "x$LDD_INFO" = "xyes";  then
    echo
    ldd "bin/$result"
    echo
  fi
done
