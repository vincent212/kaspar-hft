<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Creating Makefiles in mk_kaspr

This guide explains how to create Makefiles for executables and libraries in the m2_kaspr build system.

## Overview

The mk_kaspr directory contains build templates:
- `glob_begin.mk` - Global definitions, paths, compiler flags
- `app_template.mk` - Template for building executables
- `lib_template.mk` - Template for building libraries

## Building an Executable

### Minimal Makefile

```makefile
include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

SRC = main.cpp myprogram.cpp
APPNAM = myprogram

include $(CHOPIN_PROJ)/mk_kaspr/app_template.mk
```

### With Additional Libraries

The default LIBSO/LIBSG includes all common libraries (see glob_begin.mk).
If you need extra libraries not already included, add them with `+=`:

```makefile
include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

SRC = main.cpp myprogram.cpp
APPNAM = myprogram

# Add specific libs needed (e.g., -lsqlite3 for SQLite)
LIBSO += -lsqlite3
LIBSG += -lsqlite3

include $(CHOPIN_PROJ)/mk_kaspr/app_template.mk
```

### Variables to Define

| Variable | Required | Description |
|----------|----------|-------------|
| `SRC` | Yes | List of .cpp source files |
| `APPNAM` | Yes | Output executable name |
| `LIBSO` | No | Additional optimized libs (use `+=`) |
| `LIBSG` | No | Additional debug libs (use `+=`) |
| `INCL` | No | Additional include paths (use `+=`) |
| `DEFINES_COMMON` | No | Additional defines (use `+=`) |

### Build Commands

```bash
# Set CHOPIN_PROJ first
export CHOPIN_PROJ=/home/vm/m2_kaspr

# Build optimized version
make opt

# Build debug version
make debug

# Clean build artifacts
make clean

# Install to bin directory
make install
```

### Output

- `opt` target: Creates `$(APPNAM)` (e.g., `myprogram`)
- `debug` target: Creates `$(APPNAM)g` (e.g., `myprogramg`)
- Object files go in `obj/` (opt) and `objg/` (debug)

## Building a Library

### Minimal Makefile

```makefile
include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

LIBSRC = file1.cpp file2.cpp file3.cpp
NAM = mylib

include $(CHOPIN_PROJ)/mk_kaspr/lib_template.mk
```

### Variables to Define

| Variable | Required | Description |
|----------|----------|-------------|
| `LIBSRC` | Yes | List of .cpp source files |
| `NAM` | Yes | Library name (without lib prefix or .a) |

### Output

- `opt` target: Creates `$(CHOPIN_PROJ)/lib/lib$(NAM).a`
- `debug` target: Creates `$(CHOPIN_PROJ)/libg/lib$(NAM)g.a`

## Adding Extra Include Paths

```makefile
include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

INCL += -I/path/to/extra/headers
INCL += -I$(CHOPIN_PROJ)/mycomponent/include

SRC = main.cpp
APPNAM = myprogram

include $(CHOPIN_PROJ)/mk_kaspr/app_template.mk
```

## Adding Compiler Flags

```makefile
include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

DEFINES_COMMON += -mavx2

SRC = main.cpp
APPNAM = myprogram

include $(CHOPIN_PROJ)/mk_kaspr/app_template.mk
```

## Complete Example

```makefile
# sim/src/Makefile - SimKaspr simulation executable

include $(CHOPIN_PROJ)/mk_kaspr/glob_begin.mk

SRC = sim_kaspr.cpp main.cpp
APPNAM = sim_kaspr

include $(CHOPIN_PROJ)/mk_kaspr/app_template.mk
```

## Library Naming Convention

- Optimized: `libname.a` (e.g., `libchutil.a`)
- Debug: `libnameg.a` (e.g., `libchutilg.a`)

Use `-lname` for optimized, `-lnameg` for debug.
