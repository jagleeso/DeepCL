#!/bin/bash

# for maintainer's usage only
# no documentation (though raise an issue to ask for some if you want :-) )

cd $(dirname $0)
ROOT=$(readlink -e $PWD/../..)
rm -Rf build dist *.egg-info PyDeepCL.cxx
(cd ../build; make -j 4 install)
pip uninstall -y DeepCL
mkdir -p $ROOT/build/local/lib/python2.7/site-packages
source ../dist/bin/activate.sh
python setup.py install --prefix=$ROOT/build/local || exit 1

