all:
	mkdir -p build/tests
	clang++ -o ./build/tests/LRUTest -I ./include/ -std=c++17 -O1 tests/LRUTest.cpp

clean:
	rm ./build/*
