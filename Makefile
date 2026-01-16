all: tdbg example

tdbg: main.cpp
	clang++ main.cpp -o tdbg -I/usr/lib/llvm/21/include -L/usr/lib/llvm/21/lib -Wl,-rpath,/usr/lib/llvm/21/lib -llldb -std=c++17

example: example.c
	clang -g -o example example.c
