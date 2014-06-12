#!/bin/bash

dist="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
version=$(grep "APP_VERSION.*$" ${dist}/../src/src.pro| sed 's/^.*APP_VERSION.*\([0-9]\.[0-9]\.[0-9]\).*$/\1/')
target=${dist}"/../../knowthelist-"${version}

rm -rf ${target} && mkdir ${target}
cp -R ${dist}/../../knowthelist/* ${target}
rm ${target}/knowthelist.pro.user
cd ${target}


dpkg-buildpackage -k${GPGKEY}

# Suse / Fedora spec
sed -i 's/Version: 1/Version: '${version}'/g' ${target}/dist/knowthelist.spec
mv ${target}/dist/knowthelist.spec ${dist}/../../knowthelist_${version}.spec