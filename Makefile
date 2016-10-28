CXX=arm-frc-linux-gnueabi-g++
CXXFLAGS=-std=c++11 -Werror -Wextra -pedantic -O2

.DUMMY: ipk clean

netconsole-host: main.cpp
	${CXX} ${CXXFLAGS} -o $@ $<

netconsole-host.ipk: netconsole-host debian/control debian/changelog debian/copyright
	mkdir -p data/usr/local/frc/bin
	cp netconsole-host data/usr/local/frc/bin
	cd debian && tar czvf ../control.tar.gz control changelog copyright
	cd data && tar czvf ../data.tar.gz .
	echo 2.0 > debian-binary
	ar r $@ control.tar.gz data.tar.gz debian-binary
	rm debian-binary control.tar.gz data.tar.gz
	rm -rf data

ipk: netconsole-host.ipk

clean:
	rm netconsole-host.ipk
	rm netconsole-host
