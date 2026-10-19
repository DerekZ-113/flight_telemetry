#pragma once

// A directory of the test's own under the system's temporary directory,
// removed with everything in it when the object goes (RAII). Test support
// for REQ-TEST-001.
//
// ctest runs every TEST() as its own process (gtest_discover_tests), so a
// name built from a counter that starts at 0 in each process is the same
// name in every process: under `ctest -j` two logging tests shared
// ftlg_test_0 and each one's setup removed the other's files
// (FTS-VR-001 Entry 20 §20.5). mkdtemp replaces the six X's with
// characters no other directory has, so the name is this object's alone,
// across processes and across worktrees on one machine.

#include <stdlib.h>   // mkdtemp (POSIX)

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

class ScratchDir {
public:
    explicit ScratchDir(const std::string& tag) {
        const std::string pattern =
            (std::filesystem::temp_directory_path() / ("ftlg_" + tag + "_XXXXXX")).string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        if (::mkdtemp(name.data()) == nullptr) {
            throw std::runtime_error("mkdtemp could not create " + pattern);
        }
        path_ = name.data();
    }

    // The error code form: a test may remove the directory itself, and a
    // destructor must not throw.
    ~ScratchDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};
