# Toy Debugger

This project demonstrates how to use the LLDB C++ API to build a very basic
debugger.

https://github.com/user-attachments/assets/a42057f5-1899-416a-a408-e38efef89866

## Requirements

```sh
sudo xbps-install -S lldb21-devel clang llvm llvm-devel
```

After you clone the repository build the debugger and a sample program with
`make all -B`.

## How to use

Run the debugger with: `./tdbg <target_executable>`

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
| `q` | **Quit** debugger                                                 |

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

