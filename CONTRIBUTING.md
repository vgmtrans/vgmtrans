# Contributing to VGMTrans
## Licensing 
By making any contributions to VGMTrans you agree that any code you have contributed shall be licensed under the zlib/libpng license.

## General notes
When making UI changes, please include a screenshot.
This tool deals with proprietary formats, so keep your code well documented and *never* include copyrighted data directly (decryption keys and anything of the sort).

## Coding style
### General
- The indentation level uses two spaces
- Comments can be both single-line (`//`) or multi-line (`/* */`); use what seems appropriate
- Lines should contain no more than 120 characters
- References and pointers have their symbol against the type name (e.g. `double& volume`)
- No single-line conditionals or loops
- Conditional body must always be wrapped by brackets
- Opening brackets always go on the same line

### Naming conventions
- Use hierarchy-based names for variables:
  - `m_` for member variables
  - `s_` for static variables
  - `g_` for global variables
- Variable names should be lowercase
- Constant names should be uppercase
- Class, enum, function names should be CamelCase

### Guidelines
- POD types should be structs, not classes
- Class visibility layout is `public`, `protected`, `private` in this order. Only include the keyword once.
- Don't use global namespaces
- Use `nullptr` over `NULL`
- Use range-based for loops instead of iteratos whenever possible
- Use `auto` when the type can be deduced clearly from the context (or when it doesn't matter)
- Avoid manual memory management (`new`/`delete`)
- Remove unnecessary or redundant headers
- Use `#pragma once`
- Follow this style for the inclusion of headers, in alphabetical order:
  - Header for the source file
  - System and framework headers
  - Other headers from the project

In general, try to follow the [C++ core guidelines](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md).

## Git-specific aspects
- Don't include words like "fix" or "refactor" in commit messages. Actually describe your changes.
- Commit names should start with the class or feature name you've changed. If the changes is the same across different files, you may use a comma to separate them. You can use multiple names to be more specific
  - Wrong: `Add keyboard shortcuts for saving to the main window`
  - Correct: `VGMTransQt: MainWindow: add keyboard shortcuts for saving`
- If you must add a dependency, don't use a git submodule. Instead, copy it to the `lib` folder.
- Reorder and squash your fixup commits
- Try to keep the commits atomic and easily reverable or cherry-pickable
