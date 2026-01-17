# Toy Debugger

This project demonstrates how to use the LLDB C++ API to build a very basic
debugger.

https://github.com/user-attachments/assets/cf618ec7-44e0-4155-9869-e99337ce3f77

## Requirements

> [!IMPORTANT]
> You need to have `llvm` version 21 installed. You can install it with your
> package manager. Include paths in `Makefile` are specific for this version
> and have been tested on Void Linux and macOS.

```sh
# Void Linux
sudo xbps-install -S lldb21-devel clang llvm llvm-devel
# macOS
brew install llvm
```

After you clone the repository build the debugger and a sample program with
`make all -B`.

## How to use

Run the debugger with: `./tdbg <target_executable>`

Example with arguments and environment variables:

```sh
./tdbg ./example arg1 arg2 arg3
./tdbg ./example -e MYENV=qwe -- arg1 arg2 arg3
```

Example with breakpoints:

```sh
./tdbg ./example -b main
./tdbg ./example -b example.c:54 -b example.c:60
```

### Interactive Commands

| Key | Action                                                            |
| :-- | :---------------------------------------------------------------- |
| `r` | **Run** the program (auto-breaks on `main` if no breakpoints set) |
| `b` | Add a **breakpoint** (enter name/file:line)                       |
| `p` | **Print** variable value                                          |
| `n` | **Step over**                                                     |
| `s` | **Step into**                                                     |
| `o` | **Step out**                                                      |
| `c` | **Continue** execution                                            |
| `w` | **Watch** expression                                              |
| `q` | **Quit** debugger                                                 |
| `>` | **Reduces sidebar width**                                         |
| `<` | **Increases sidebar width**                                       |

### Input Mode

When adding breakpoints or printing variables:
- `Enter`: Confirm input
- `Esc`: Cancel and return to normal mode

### Tips & Troubleshooting

- **Logs**: The debugger redirects `stderr` to `tdbg.log`. Check this file if
  something isn't working as expected.
- **Missing Source**: If the debugger enters a function without source (like a
  library call), it will fallback to disassembly. Use `n` (Step Over) or `o`
  (Step Out) to get back to your code.

### Type mappings

| Type          | Mapping                   |
| :------------ | :------------------------ |
| Pointer       | `p`                       |
| Reference     | `&`                       |
| Array         | `a`                       |
| Integer (int) | `i`                       |
| Char          | `c`                       |
| Float         | `f`                       |
| Double        | `d`                       |
| Bool          | `b`                       |
| Long          | `l`                       |
| Short         | `s`                       |
| Struct        | `s`                       |
| Class         | `c`                       |
| Void          | `v`                       |
| Enumeration   | `e`                       |
| Default       | First letter of type name |

> [!NOTE]
> Some symbols are shared (e.g., `s` for both Short and Struct, `c` for both
> Char and Class).

## Acknowledgment

- https://github.com/termbox/termbox2

