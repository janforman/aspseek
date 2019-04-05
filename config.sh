./cvsprep
export CFLAGS="-march=i686"
export CXXFLAGS="-march=i686"
# Uncomment below for RedHat/Cent/Fedora gcc-compat
#export CC="gcc32"
#export CXX="g++32"
./configure --enable-charset-guesser --disable-ext-conv --with-mysql
