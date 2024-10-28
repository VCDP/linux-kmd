#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

U="$(sed -n '/edss-api.intel.com/,/password/p' ${HOME}/.netrc | grep login | awk '{print $2}')"
P="$(sed -n '/edss-api.intel.com/,/password/p' ${HOME}/.netrc | grep password | awk '{print $2}')"

## ./SignFile -vv -c 'Media Driver Production Sign 2022Q2' -s cl -cf ${tar_abspath}.sig ${tar_abspath}
## ./SignFile -vv xxx.rpm

if ( echo "$@" | grep -q 'tar.gz$' ); then
    ${SCRIPT_DIR}/SignFile -c 'Media Driver Production Sign TAR 2024Q3' -u ${U} -p ${P} $@
else
    ${SCRIPT_DIR}/SignFile -u ${U} -p ${P} $@
fi


