#!/usr/bin/env bash
set -euo pipefail

echo "--- Linting ---"

failed=0

# C++ Linting
if command -v clang-format &> /dev/null; then
    echo "Running clang-format..."
    # Find all C++ source files
    # Exclude build directories and third_party if any
    files=$(find core daemon clients -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \))

    if [ -n "$files" ]; then
        # Capture output to avoid noise if successful, but show if failed
        if ! clang-format --dry-run --Werror $files; then
            echo "ERROR: clang-format failed. Run clang-format -i on the files to fix."
            failed=1
        else
            echo "clang-format passed."
        fi
    fi
else
    echo "WARNING: clang-format not found. Skipping C++ linting."
    # In CI, we expect tools to be present.
    if [ "${CI:-}" == "true" ]; then
        echo "ERROR: clang-format is required in CI."
        failed=1
    fi
fi

# Go Linting
if command -v go &> /dev/null; then
    echo "Running Go checks..."
    if [ -d "clients/portable" ]; then
        pushd "clients/portable" > /dev/null

        echo "Running go vet..."
        if ! go vet ./...; then
            echo "ERROR: go vet failed."
            failed=1
        fi

        echo "Running go fmt..."
        # go fmt returns the names of files it modified (or would modify)
        formatted_files=$(go fmt ./...)
        if [ -n "$formatted_files" ]; then
            echo "ERROR: go fmt found unformatted files:"
            echo "$formatted_files"
            failed=1
        fi

        popd > /dev/null
    fi
else
    echo "WARNING: go not found. Skipping Go linting."
    if [ "${CI:-}" == "true" ]; then
        echo "ERROR: go is required in CI."
        failed=1
    fi
fi

if [ $failed -ne 0 ]; then
    echo "Linting failed."
    exit 1
fi

echo "Linting passed."
