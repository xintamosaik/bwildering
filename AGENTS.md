# Coding preferences

- Prefer static allocation and arenas wherever possible. Avoid individual heap allocations and deallocations (`malloc`, `calloc`, `realloc`, `free`, and equivalents) when writing or changing code; choose storage with an appropriate capacity and lifetime.
- Use `const` wherever values are not modified, including local variables, parameters, and pointed-to data. Preserve mutability where it is required; apply const-correctness when writing or changing code.
- Avoid implicit narrowing conversions, including `int` to `float` and `size_t` to `double`. Use matching types and floating-point literals; where conversion is intentional and appropriate for the value range, make it explicit with a cast.
- Check changed C files with Clang-Tidy's `bugprone-narrowing-conversions` check before finishing, and resolve warnings introduced by the change.
