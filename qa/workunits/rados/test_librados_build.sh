#!/bin/bash -ex
#
# Compile and run a librados application outside of the ceph build system, so
# that we can be sure librados.h[pp] is still usable and hasn't accidentally
# started depending on internal headers.
#
# The script assumes all dependencies - e.g. curl, make, gcc, librados headers,
# libradosstriper headers, boost headers, etc. - are already installed.
#

source $(dirname $0)/../ceph-helpers-root.sh

trap cleanup EXIT

SOURCES="hello_radosstriper.cc
hello_world_c.c
hello_world.cc
Makefile
"
BINARIES_TO_RUN="hello_world_c
hello_world_cpp
"
BINARIES="${BINARIES_TO_RUN}hello_radosstriper_cpp
"
# parse output like "octopus (dev)"
case $(librados-config --release | grep -Po ' \(\K[^\)]+') in
    dev)
        BRANCH=main;;
    rc|stable)
        BRANCH=$(librados-config --release | cut -d' ' -f1);;
    *)
        echo "unknown release '$(librados-config --release)'" >&2
        return 1;;
esac
DL_PREFIX="https://raw.githubusercontent.com/Matan-B/ceph/wip-matanb-librados-20-test/examples/librados/"
DESTDIR=$(pwd)

function cleanup () {
    for f in $BINARIES$SOURCES ; do
        rm -f "${DESTDIR}/$f"
    done
}

function get_sources () {
    for s in $SOURCES ; do
        curl --progress-bar --output $s ${DL_PREFIX}$s
    done
}

function check_sources () {
    for s in $SOURCES ; do
        test -f $s
    done
}

function check_binaries () {
    for b in $BINARIES ; do
        file $b
        test -f $b
    done
}

function run_binaries () {
    for b in $BINARIES_TO_RUN ; do
        ./$b -c /etc/ceph/ceph.conf
    done
}

pushd $DESTDIR
case $(distro_id) in
    fedora|rhel)
        install devtoolset-11 gcc-c++ make libradospp-devel librados-devel
        scl enable gcc-toolset-11 bash;;
    centos)
        install centos-release-scl gcc-c++ make libradospp-devel librados-devel
        scl enable gcc-toolset-11 bash;;
    ubuntu|debian|devuan|softiron)
        install gcc-11 g++-11 make libradospp-dev librados-dev;;
    opensuse*|suse|sles)
        install gcc-c++ make libradospp-devel librados-devel;;
    *)
        echo "$(distro_id) is unknown, $@ will have to be installed manually."
esac
get_sources
check_sources
make all-system
check_binaries
run_binaries
popd
