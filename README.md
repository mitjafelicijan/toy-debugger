# Toy Debugger

This project demonstrates how to use the LLDB C++ API to build a very basic
debugger.

https://github.com/user-attachments/assets/677f69fd-c5fe-402c-9342-3304547b29aa

## Requirements

```sh
sudo xbps-install -S lldb21-devel clang llvm llvm-devel
```

After you clone the repository build the debugger and a sample program with
`make`.

Then run the debugger with example program with `./tdbg example`.

## Available commands

- `c` - Continue execution until the next breakpoint or stop
- `s` - Step into the next instruction/function
- `n` - Step over the next instruction/function
- `bt` - Print a backtrace (call stack) of the current thread
- `v` - Print local variables in the current stack frame
- `q` - Kill the debugged process and exit
