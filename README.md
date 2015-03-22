libswdiag
=========

Software DIagnostics

Dependencies

check which you can get from http://downloads.sourceforge.net/project/check/check/0.9.14/check-0.9.14.tar.gz

Had to upgrade libtool from ipkg version to:

https://ftp.gnu.org/gnu/libtool/libtool-2.4.6.tar.gz

Had to upgrade automake from ipkg version to:

http://ftp.gnu.org/gnu/automake/automake-1.14.tar.gz

When running "make check" I had to setup the LD_LIBRARY_PATH beforehand to include /usr/local/lib:

export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

Added ipkg python so that we can run our tests

