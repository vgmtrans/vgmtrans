#!/bin/sh
set -e
set -x

info()
{
    local green="\033[1;32m"
    local normal="\033[0m"
    echo "[${green}build${normal}] $1"
}

ARCH="x86_64"
MINIMAL_OSX_VERSION="10.7"
OSX_VERSION=`xcrun --show-sdk-version`
SDKROOT=`xcode-select -print-path`/Platforms/MacOSX.platform/Developer/SDKs/MacOSX$OSX_VERSION.sdk

usage()
{
cat << EOF
usage: $0 [options]

Build VGMTrans in the current directory

OPTIONS:
   -h            Show some help
   -q            Be quiet
   -r            Rebuild everything (tools, contribs, VGMTrans)
   -k <sdk>      Use the specified sdk (default: $SDKROOT)
   -a <arch>     Use the specified arch (default: $ARCH)
EOF

}

spushd()
{
    pushd "$1" > /dev/null
}

spopd()
{
    popd > /dev/null
}

while getopts "hvrk:a:" OPTION
do
     case $OPTION in
         h)
             usage
             exit 1
             ;;
         q)
             set +x
             QUIET="yes"
         ;;
         r)
             REBUILD="yes"
         ;;
         a)
             ARCH=$OPTARG
         ;;
         k)
             SDKROOT=$OPTARG
         ;;
     esac
done
shift $(($OPTIND - 1))

if [ "x$1" != "x" ]; then
    usage
    exit 1
fi

#
# Various initialization
#

out="/dev/stdout"
if [ "$QUIET" = "yes" ]; then
    out="/dev/null"
fi

info "Building VGMTrans for Mac OS X"

spushd `dirname $0`/../../..
vgmtransroot=`pwd`
spopd

builddir=`pwd`

info "Building in \"$builddir\""

TRIPLET=$ARCH-apple-darwin11

export CC="xcrun clang"
export CXX="xcrun clang++"
export OBJC="xcrun clang"
export OSX_VERSION
export SDKROOT
export PATH="${vgmtransroot}/extras/tools/build/bin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:${vgmtransroot}/contrib/${TRIPLET}/bin:${PATH}"

#
# vgmtrans/extras/tools
#

info "Building building tools"
spushd "${vgmtransroot}/extras/tools"
./bootstrap > $out
if [ "$REBUILD" = "yes" ]; then
    make clean
fi
make > $out
spopd


#
# vgmtrans/contribs
#

info "Building contribs"
spushd "${vgmtransroot}/contrib"
mkdir -p contrib-$TRIPLET && cd contrib-$TRIPLET
../bootstrap --build=$TRIPLET --host=$TRIPLET > $out
if [ "$REBUILD" = "yes" ]; then
    make clean
fi
if [ ! -e "../$TRIPLET" ]; then
    make prebuilt > $out
fi
spopd


#
# make
#

core_count=`sysctl -n machdep.cpu.core_count`
let jobs=$core_count+1

if [ "$REBUILD" = "yes" ]; then
    info "Running make clean"
    make clean
fi

info "Running cmake and make"
cmake ..
make
