# Contributing to VGMTrans
## Licensing 
By making any contributions to VGMTrans you agree that any code you have contributed
shall be licensed under the zlib/libpng license.

## General notes
Keep your commits as small as possible.
When working on a feature to merge, please use an appropriate branch name (e.g. if working on something NDS-related, then your branch should be named something like `vgmtrans/nds-fixes`).

The codebase currently being converted to `C++17`.
We support any `C++17`-compliant compiler, so be sure to test your changes across as many as possible (CI is supposed to help with this).
Keep your code portable and always prefer builtins over macros. Don't use global namespaces.
Use raw pointers only when strictly necessary (smart pointers are always preferred over manual memory management).
Range-based for-loops are preferred over iterators.

When making UI changes, please include a screenshot.

This tool deals with proprietary formats, so keep your code well-documented and *never*
include copyrighted data directly (decryption keys and anything of the sort).

## Coding style
The coding style is self-documented in `.clang-format`

### Naming
  - Use uppercase for compile-time constants (e.g. `constexpr int HAX = 1337;`)
  - *Never* use Hungarian notation (e.g `bool bItemChecked;`)
  - Separate words in variable names using underscores (e.g. `double your_fancy_number;`)
  - Class variables begin with `m_`
  - Staic variable begin with `s_`
  - Globals begin with `g_`
  - Everything else should be camel case
  - Use exact-size types when handling raw data (find their aliases in `main/common.h`)

### Classes and data
  - The class layout should be: `public, protected, private`
  - Use structs for plain-old-data types
  - Avoid non-portable data types
