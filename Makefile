UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	LLVM_PREFIX := /usr/lib/llvm/21
	CXXFLAGS := -I$(LLVM_PREFIX)/include -std=c++17
	LDFLAGS := -L$(LLVM_PREFIX)/lib -Wl,-rpath,$(LLVM_PREFIX)/lib -llldb
endif

ifeq ($(UNAME_S),Darwin)
	LLVM_PREFIX := /System/Volumes/Data/opt/homebrew/Cellar/llvm/21.1.8
	SDKROOT := $(shell xcrun --show-sdk-path)
	CXXFLAGS := -isysroot $(SDKROOT) -I$(LLVM_PREFIX)/include -std=c++17 -stdlib=libc++
	LDFLAGS := -L$(LLVM_PREFIX)/lib -Wl,-rpath,$(LLVM_PREFIX)/lib -llldb
endif

all: tdbg example

tdbg: tdbg.cpp
	clang++ tdbg.cpp -o tdbg $(CXXFLAGS) $(LDFLAGS)

example: example.c
	clang -g -o example example.c

clean:
	rm -f tdbg example
