#!/usr/bin/env bash
##############################################################################
## This is used to package .deb packages and upload them to the launchpad
## ppa servers for building.
##
## You can pass a branch name as argument to this script (which, if no argument is given,
## will default to "develop").
##
## If the given branch matches a release version tag, the resulting package will be uploaded to
## ethereum/ethereum PPA, or ethereum/ethereum-dev PPA otherwise.
##
## It will clone the Solidity git from github, determine the version,
## create a source archive and push it to the ubuntu ppa servers.
##
## To interact with launchpad, you need to set the variables $LAUNCHPAD_EMAIL
## and $LAUNCHPAD_KEYID in the file .release_ppa_auth in the root directory of
## the project to your launchpad email and pgp keyid.
## This could for example look like this:
##
##  LAUNCHPAD_EMAIL=your-launchpad-email@ethereum.org
##  LAUNCHPAD_KEYID=123ABCFFFFFFFF
##
## Additionally the following entries in /etc/dput.cf are required:
##
##  [ethereum-dev]
##  fqdn            = ppa.launchpad.net
##  method          = ftp
##  incoming        = ~ethereum/ethereum-dev
##  login           = anonymous
##
##  [ethereum]
##  fqdn            = ppa.launchpad.net
##  method          = ftp
##  incoming        = ~ethereum/ethereum
##  login           = anonymous
##
##  [ethereum-static]
##  fqdn            = ppa.launchpad.net
##  method          = ftp
##  incoming        = ~ethereum/ethereum-static
##  login           = anonymous
##
##############################################################################

set -e


REPO_ROOT="$(dirname "$0")/.."

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

if [ -z "$1" ]
then
    branch=develop
else
    branch=$1
fi

is_release() {
    [[ "${branch}" =~ ^v[0-9]+(\.[0-9]+)*$ ]]
}

sourcePPAConfig

packagename=solc

# This needs to be a still active release
static_build_distribution=noble

DISTRIBUTIONS="jammy noble oracular"

if is_release
then
    DISTRIBUTIONS="$DISTRIBUTIONS STATIC"

    # Sanity checks
    checkDputEntries "\[ethereum\]"
    checkDputEntries "\[ethereum-static\]"
else
    # Sanity check
    checkDputEntries "\[ethereum-dev\]"
fi

for distribution in $DISTRIBUTIONS
do
cd /tmp/
rm -rf "$distribution"
mkdir "$distribution"
cd "$distribution"

if [ "$distribution" = STATIC ]
then
    pparepo=ethereum-static
    SMTDEPENDENCY=""
    CMAKE_OPTIONS="-DSOLC_LINK_STATIC=On -DCMAKE_EXE_LINKER_FLAGS=-static"
else
    if is_release
    then
        pparepo=ethereum
    else
        pparepo=ethereum-dev
    fi
    if [ "$distribution" = focal ]
    then
        SMTDEPENDENCY="libz3-static-dev,
            libcvc4-dev,
            "
    elif [ "$distribution" = disco ]
    then
        SMTDEPENDENCY="libz3-static-dev,
            libcvc4-dev,
            "
    else
        SMTDEPENDENCY="libz3-static-dev,
            "
    fi
    CMAKE_OPTIONS=""
fi
ppafilesurl=https://launchpad.net/~ethereum/+archive/ubuntu/${pparepo}/+files

# Fetch source
git clone --depth 2 --recursive https://github.com/ethereum/solidity.git -b "$branch"
mv solidity solc

# Determine version
cd solc
version=$("$(dirname "$0")/get_version.sh")
commithash=$(git rev-parse --short=8 HEAD)
commitdate=$(git show --format=%ci HEAD | head -n 1 | cut - -b1-10 | sed -e 's/-0?/./' | sed -e 's/-0?/./')

echo "$commithash" > commit_hash.txt
if is_release
then
    debversion="$version"
    echo -n > prerelease.txt # proper release
else
    debversion="$version~develop-$commitdate-$commithash"
fi

# gzip will create different tars all the time and we are not allowed
# to upload the same file twice with different contents, so we only
# create it once.
if [ ! -e "/tmp/${packagename}_${debversion}.orig.tar.gz" ]
then
    tar --exclude .git -czf "/tmp/${packagename}_${debversion}.orig.tar.gz" .
fi
cp "/tmp/${packagename}_${debversion}.orig.tar.gz" ../

# Create debian package information

mkdir debian
echo 9 > debian/compat
cat <<EOF > debian/control
Source: solc
Section: science
Priority: extra
Maintainer: Christian (Buildserver key) <builds@ethereum.org>
Build-Depends: ${SMTDEPENDENCY}debhelper (>= 9.0.0),
               cmake,
               g++ (>= 5.0),
               git,
               libgmp-dev,
               libboost-all-dev,
               automake,
               libtool,
               scons
Standards-Version: 3.9.5
Homepage: https://ethereum.org
Vcs-Git: https://github.com/ethereum/solidity.git
Vcs-Browser: https://github.com/ethereum/solidity

Package: solc
Architecture: any-amd64
Multi-Arch: same
Depends: \${shlibs:Depends}, \${misc:Depends}
Conflicts: libethereum (<= 1.2.9)
Description: Solidity compiler.
 The commandline interface to the Solidity smart contract compiler.
EOF
cat <<EOF > debian/rules
#!/usr/bin/make -f
# -*- makefile -*-
# Sample debian/rules that uses debhelper.
#
# This file was originally written by Joey Hess and Craig Small.
# As a special exception, when this file is copied by dh-make into a
# dh-make output file, you may use that output file without restriction.
# This special exception was added by Craig Small in version 0.37 of dh-make.
#
# Modified to make a template file for a multi-binary package with separated
# build-arch and build-indep targets  by Bill Allombert 2001

# Uncomment this to turn on verbose mode.
export DH_VERBOSE=1

# This has to be exported to make some magic below work.
export DH_OPTIONS


%:
	dh \$@ --buildsystem=cmake #--with sphinxdoc

override_dh_auto_test:

#override_dh_installdocs:
#    make -C docs html
#    dh_installdocs docs/_build/html

override_dh_shlibdeps:
	dh_shlibdeps --dpkg-shlibdeps-params=--ignore-missing-info

override_dh_auto_configure:
	dh_auto_configure -- -DTESTS=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=OFF ${CMAKE_OPTIONS}
EOF
cat <<EOF > debian/copyright
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: solc
Source: https://github.com/ethereum/solidity

Files: *
Copyright: 2014-2016 Ethereum
License: GPL-3.0+

Files: debian/*
Copyright: 2016 Ethereum
License: GPL-3.0+

License: GPL-3.0+
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 This package is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
 .
 On Debian systems, the complete text of the GNU General
 Public License version 3 can be found in "/usr/share/common-licenses/GPL-3".
EOF
cat <<EOF > debian/changelog
solc (0.0.1-0ubuntu1) saucy; urgency=low

  * Initial release.

 -- Christian <build@ethereum.org>  Mon, 03 Feb 2016 14:50:20 +0000
EOF
echo docs > debian/docs
mkdir debian/source
echo "3.0 (quilt)" > debian/source/format
chmod +x debian/rules

versionsuffix=0ubuntu1~${distribution}
# bump version / add entry to changelog
EMAIL="$LAUNCHPAD_EMAIL" dch -v "1:${debversion}-${versionsuffix}" "git build of ${commithash}"


# build source package
# If packages is rejected because original source is already present, add
# -sd to remove it from the .changes file
# -d disables the build dependencies check
debuild -S -d -sa -us -uc

# prepare .changes file for Launchpad
if [ "$distribution" = STATIC ]
then
    sed -i -e "s/UNRELEASED/${static_build_distribution}/" -e s/urgency=medium/urgency=low/ ../*.changes
else
    sed -i -e "s/UNRELEASED/${distribution}/" -e s/urgency=medium/urgency=low/ ../*.changes
fi

# check if ubuntu already has the source tarball
(
cd ..
orig="${packagename}_${debversion}.orig.tar.gz"
# shellcheck disable=SC2012
orig_size=$(ls -l "$orig" | cut -d ' ' -f 5)
orig_sha1=$(sha1sum "$orig" | cut -d ' ' -f 1)
orig_sha256=$(sha256sum "$orig" | cut -d ' ' -f 1)
orig_md5=$(md5sum "$orig" | cut -d ' ' -f 1)

if wget --quiet -O "$orig-tmp" "$ppafilesurl/$orig"
then
    echo "[WARN] Original tarball found in Ubuntu archive, using it instead"
    mv "$orig-tmp" "$orig"
    # shellcheck disable=SC2012
    new_size=$(ls -l ./*.orig.tar.gz | cut -d ' ' -f 5)
    new_sha1=$(sha1sum "$orig" | cut -d ' ' -f 1)
    new_sha256=$(sha256sum "$orig" | cut -d ' ' -f 1)
    new_md5=$(md5sum "$orig" | cut -d ' ' -f 1)
    sed -i -e "s,$orig_sha1,$new_sha1,g" -e "s,$orig_sha256,$new_sha256,g" -e "s,$orig_size,$new_size,g" -e "s,$orig_md5,$new_md5,g" ./*.dsc
    sed -i -e "s,$orig_sha1,$new_sha1,g" -e "s,$orig_sha256,$new_sha256,g" -e "s,$orig_size,$new_size,g" -e "s,$orig_md5,$new_md5,g" ./*.changes
fi
)

# sign the package
debsign --re-sign -k "${LAUNCHPAD_KEYID}" "../${packagename}_${debversion}-${versionsuffix}_source.changes"

# upload
dput "${pparepo}" "../${packagename}_${debversion}-${versionsuffix}_source.changes"

done
