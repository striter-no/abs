# ABS - Another Build System

Lightweight and easy-to-use configuration & building system. Supports:

- Different modes setting (debug/release)
- Globs for source and library files
- Modules building (multilayered builds)

## Building

Create `bin` folder before compilation with `make` or change destination file

```sh
make
```

or

```sh
gcc -o ./bin/main ./code/main.c -Icode/abs/include
```

## Usage

```
abs // like make
abs ./config.conf // specify path
abs -h // for quick help
abs -d // for documentation about configuration

abs gen // quick start (generate default config)
```

### Example configuration

You can also see `test/` directory for comprehensive example

```ini
[project]
name = project
version = 0.0.1

[files]
sources = main.c
output = main

[dirs]
output = .
src = .

[compiler]
cc = gcc

[modes]
active = debug

[mode.debug]
flags = -g -O0 -fsanitize=address

[flags]
common = -stc=c11 -Wall -Wextra -Wpedantic

```