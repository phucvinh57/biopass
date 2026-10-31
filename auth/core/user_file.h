#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace biopass {

// Reads a whole file, but only if it is a regular file of at most
// `max_bytes`. Devices, FIFOs, sockets and directories are refused before a
// byte is read: the open never waits for a FIFO writer (O_NONBLOCK), never
// takes a terminal as controlling tty (O_NOCTTY), and fstat() on the opened
// fd has the last word. Symlinks are followed: what counts is the file they
// lead to. nullopt on any failure.
std::optional<std::string> readRegularFile(const std::string& path, size_t max_bytes);

// readRegularFile() with the filesystem calls run as `username` (see
// fs_user.h). Use it in the root helper for any path the user controls, so
// a symlink can only lead to files the user could read anyway.
std::optional<std::string> readUserFile(const std::string& username, const std::string& path,
                                        size_t max_bytes);

}  // namespace biopass
