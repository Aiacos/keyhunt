# Contributing Guide

Thank you for your interest in contributing to keyhunt! This guide covers the process and guidelines for contributions.

## Getting Started

### Fork and Clone

```bash
# Fork on GitHub, then:
git clone https://github.com/YOUR_USERNAME/keyhunt.git
cd keyhunt
git remote add upstream https://github.com/albertobsd/keyhunt.git
```

### Set Up Development Environment

```bash
# Build
make clean && make

# Verify tests pass
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

## Contribution Types

### Bug Fixes

1. Create issue describing the bug
2. Fork and create branch: `git checkout -b fix/issue-123`
3. Write fix with tests
4. Submit pull request

### Features

1. Discuss in issue first (architecture decisions)
2. Fork and create branch: `git checkout -b feature/my-feature`
3. Implement with documentation
4. Submit pull request

### Documentation

- Fix typos, clarify explanations
- Add examples, improve tutorials
- No issue required for small docs changes

### Performance Improvements

1. Include benchmarks showing improvement
2. Document any trade-offs
3. Ensure no correctness regressions

## Code Style

### C/C++ Style

```cpp
// File header
/**
 * @file filename.cpp
 * @brief Brief description of the file
 */

// Include order: system, external, project
#include <stdio.h>
#include <vector>
#include "secp256k1/Int.h"
#include "hash/sha256.h"

// Naming conventions
class ClassName {              // PascalCase for classes
    int memberVariable;        // camelCase for members
    void functionName();       // camelCase for functions
};

#define CONSTANT_NAME 42       // UPPER_CASE for macros
static int g_globalVar;        // g_ prefix for globals

// Braces on same line
if (condition) {
    // code
} else {
    // code
}

// Functions
void functionName(int param) {
    // 4-space indentation
    for (int i = 0; i < n; i++) {
        // code
    }
}
```

### Comments

```cpp
// Single-line comment for brief explanations

/*
 * Multi-line comment for longer explanations
 * that span multiple lines
 */

/**
 * @brief Function documentation
 * @param input Description of parameter
 * @return Description of return value
 */
int documentedFunction(int input);
```

### Error Handling

```cpp
// Check return values
FILE *fp = fopen(filename, "r");
if (fp == NULL) {
    fprintf(stderr, "Error: cannot open %s\n", filename);
    return -1;
}

// Use consistent error patterns
if (!validate_input(input)) {
    output_error("Invalid input: %s", input);
    return false;
}
```

## Git Workflow

### Branch Naming

```
fix/issue-number-brief-description
feature/brief-description
docs/what-changed
perf/optimization-description
```

### Commit Messages

```
type: Brief description (50 chars max)

More detailed explanation if needed. Wrap at 72 characters.
Explain what and why, not how (code shows how).

Fixes #123
```

Types:
- `fix`: Bug fix
- `feat`: New feature
- `docs`: Documentation
- `perf`: Performance improvement
- `refactor`: Code restructuring
- `test`: Adding tests
- `chore`: Maintenance

Example:
```
feat: add AVX-512 RIPEMD160 implementation

Implements 16-way parallel RIPEMD160 using AVX-512 intrinsics.
Provides 2x speedup over AVX2 on supported CPUs.

Runtime detection ensures fallback on older CPUs.

Closes #456
```

### Pull Request Process

1. **Update your fork**:
   ```bash
   git fetch upstream
   git checkout main
   git merge upstream/main
   ```

2. **Create feature branch**:
   ```bash
   git checkout -b feature/my-feature
   ```

3. **Make changes and test**:
   ```bash
   # Build
   make clean && make

   # Test
   ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
   ```

4. **Commit**:
   ```bash
   git add specific_files.cpp
   git commit -m "feat: description"
   ```

5. **Push and create PR**:
   ```bash
   git push origin feature/my-feature
   # Then create PR on GitHub
   ```

### PR Description Template

```markdown
## Summary
Brief description of changes.

## Changes
- List of specific changes
- One per line

## Testing
How was this tested?
- [ ] Unit tests pass
- [ ] Manual testing performed
- [ ] Benchmark results (if performance-related)

## Screenshots
If applicable, add screenshots.

## Related Issues
Fixes #123
Related to #456
```

## Testing Requirements

### Before Submitting

```bash
# 1. Build cleanly
make clean && make

# 2. No compiler warnings
make 2>&1 | grep -i warning

# 3. Quick functionality test
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

# 4. Mode-specific tests
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x1000000 -q -s 1
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -s 1

# 5. Memory check (if touching memory management)
make CXXFLAGS="-O1 -g -fsanitize=address"
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

### For Performance Changes

Include benchmarks:

```
Before:
  Speed: 85.2 Mkeys/s

After:
  Speed: 102.4 Mkeys/s (+20%)

Tested on: AMD Ryzen 9 5900X, Ubuntu 22.04
```

## Documentation

### When to Update Docs

- New features: Add to relevant wiki pages
- Changed behavior: Update affected docs
- New options: Update CLI reference
- Bug fixes: Update FAQ if relevant

### Documentation Files

- `docs/wiki/`: User documentation
- `CLAUDE.md`: AI assistant guidance
- `README.md`: Project overview
- Code comments: Implementation details

## Architecture Decisions

Major changes should be discussed first:

1. **Create issue** describing proposed change
2. **Discuss** trade-offs and alternatives
3. **Get approval** before implementation
4. **Document** decision in code comments

Examples requiring discussion:
- New search modes
- Major algorithm changes
- New dependencies
- API changes
- File format changes

## Security Considerations

### Sensitive Code

Be careful with:
- Random number generation
- Private key handling
- Memory management (key material)
- Network communication

### Review Checklist

- [ ] No credentials/secrets in code
- [ ] Private keys cleared from memory after use
- [ ] Random sources properly seeded
- [ ] Input validation for all user data
- [ ] No buffer overflows

## Getting Help

### Questions

- Check existing documentation
- Search closed issues
- Ask in issue (with "question" label)

### Stuck on Implementation

- Describe what you're trying to do
- Share what you've tried
- Ask for guidance in issue

## Recognition

Contributors are recognized in:
- Git history (commit authors)
- Release notes (for significant contributions)
- README.md (major contributors)

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (MIT License).

## Code of Conduct

- Be respectful and constructive
- Focus on the code, not the person
- Help others learn
- Accept feedback gracefully

## See Also

- [Architecture](architecture.md) - Understand the codebase
- [Building](building.md) - Build system details
- [Testing](testing.md) - Test requirements
