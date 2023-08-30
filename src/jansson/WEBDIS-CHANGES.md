# Webdis changes to Jansson

Webdis imports Jansson by including its source code under `src/jansson`. This document lists the changes made to Jansson files, mostly made to accommodate the requirements of the Webdis build process.

Webdis currently imports Jansson version 2.14.

## Includes and compiler builtins

Added checks for various headers and compiler builtins. For example, `endian.h` is not available on macOS, although `machine/endian.h` is.

## Unused code

Marked `buf_to_uint32` as unused.

## Unused source files

To validate that all the C files imported are used in the build, we can compare the list of files in the Jansson source directory with the object files listed in the Webdis Makefile.

The following `diff` command should not show any output:

```sh
diff -u <(grep ^JANSSON_OBJ Makefile | head -1 | cut -f 2- -d = | tr ' ' '\n' | sed -Ee 's/\.o$/.c/g' | sort)\
    <(find src/jansson/src -name '*.c' | sort)
```

## Unused build files

Autotool build files can be removed, namely:
- `configure.ac`
- `jansson.pc.in`
- `Makefile.am` (2 copies)
- `jansson_config.h.in`

## Disabled warning

A `#pragma` is used in `src/jansson/src/load.c` to disable overly strict warnings about string truncation.
No overflow is possible, GCC is only warning about the possibility of a string being truncated if it doesn't fit in the destination buffer.
