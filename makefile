
GPP = g++ -m32 -std=c++11 -fno-stack-protector
GCC = gcc -m32 -fno-stack-protector

COMPILE_FLAGS = -c -O3 -fpack-struct=1 -fPIC -DLINUX -w -fpermissive -I../lib/sdk/ -I../lib/sdk/amx/

.ONESHELL:
all:
	mkdir -p build
	mkdir -p bin
	cd build
	-rm -f *~ *.o
	$(GPP) -g $(COMPILE_FLAGS) ../lib/sdk/*.cpp
	$(GPP) -g $(COMPILE_FLAGS) ../src/main.cpp
	$(GCC) -g -fshort-wchar -shared -o "../bin/raktimefix.so" *.o
	-rm -f *~ *.o
