# Gera Standard Library

This is the repository for the standard library of the Gera programming language ([website](https://geralang.netlify.app), [repository](https://github.com/typesafeschwalbe/gerac)).

### Structure

The standard library is split into multiple parts, depending on the targets they support:
- `./target-any` works on any compilation target and only depends the functionality provided by the `core`-module.
- `./target-c` requires the target to be C
- `./target-js` requires the target to be Javascript

### Usage (no package manager)

To use this standard library, pass all `.gera`- and `.gem`-files in each of the folders you want to include (see above for more info on each) along with your program's files to the compiler as command line arguments.
On Unix-like operating systems, the `find`-utility may be used to find and list these files:
```bash
find ./target-any -type f \( -iname \*.gera -o -iname \*.gem \)
```

#### Using `./target-c`

When compiling generated C and including `./target-c`, all the functions and variables defined in all of the `.c`-files in the `./target-c`-directory must be available. One way to achieve this is to pass the `.c`-files to the C compiler along with the output file generated by the Gera compiler.

#### Using `./target-js`

When using generated Javascript and including `./target-js`, all the functions and variables defined in all of the `.js`-files in the `./target-js`-directory must be available to the generated Javascript code.

#### Example

Let's assume we have the following file structure:
```bash
- example/   # our working directory
    - out/   # we can put output files here
    - gerastd/   # a clone of this repository
        - target-any/
        - target-c/
          ...
    - example.gera   # the file we want to compile to C and run
```
To compile `example.gera` to C (including the functionality for targets `any` and `c`) and run it, we can use the following commands (Bash, Unix-like system):
```bash
# find all .gera and .gem files in
# ./gerastd/target-any and ./gerastd/target-c
stdlib_any_gera=$(find ./gerastd/target-any -type f \( -iname \*.gera -o -iname \*.gem \))
stdlib_c_gera=$(find ./gerastd/target-c -type f \( -iname \*.gera -o -iname \*.gem \))
# compile them along with the actual program file
gerac $stdlib_any_gera $stdlib_c_gera example.gera -m example::main -t c -o ./out/example.c

# find all .c files in ./gerastd/target-c
stdlib_c_c=$(find ./gerastd/target-c -name "*.c")
# compile them along with the output file
cc $stdlib_c_c ./out/example.c -lm -o ./out/example

# run the generated executable
./out/example
```

### Progress

- [ ] target = any
    - [x] `std::arr` - Array utilities
    - [x] `std::iter` - Iterators
    - [ ] `std::map` - Hash maps
    - [ ] `std::set` - Hash sets
    - [x] `std::math` - Math constants and simple functions
    - [x] `std::opt` - Utilities for optionals
    - [ ] `std::res` - Utilities for results
    - [x] `std::str` - String utilities
    - [x] `std::vec` - Vectors
    - [ ] `std::json` - JSON
    - [ ] `std::regex` - RegEx
- [ ] target = c
    - [x] `std::bw` - Bitwise operations
    - [x] `std::math` - Complex math functions
    - [x] `std::io` - I/O
    - [ ] `std::env` - Environment
        - [ ] Environment variables
        - [ ] Program arguments
        - [ ] Running commands
    - [ ] `std::thread` - Threads
    - [ ] `std::time` - Time
    - [ ] `std::rng` - Random number generation
- [ ] target = js
    - [x] `std::io` - Printing
    - [x] `std::math` - Complex math functions
    - [ ] `std::time` - Time
    - [ ] `std::rng` - Random number generation 