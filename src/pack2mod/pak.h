// Reader and writer for Risen's packed archives (G3V0 container: *.pak and the patch volumes *.p00, *.p01, ...; docs/RE.md section 43).
// Port of tools/g3pak.py (which stays the test oracle).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "pack2mod/util.h"

namespace p2m {

struct PakEntry {
    std::string path;  // inside the archive, '/' separated, original case
    uint64_t offset = 0;
    uint32_t stored = 0;  // size in the archive
    uint32_t size = 0;    // size after inflating (equal = stored raw)
    uint64_t mtime = 0;   // FILETIME of the last write
    bool compressed() const { return stored != size; }
};

class Pak {
public:
    // throws Error when the file is not a G3V0 archive or the table of contents is damaged
    static std::unique_ptr<Pak> open(const std::string& path);
    ~Pak();
    Pak(const Pak&) = delete;
    Pak& operator=(const Pak&) = delete;

    const std::string& path() const { return path_; }
    const std::vector<PakEntry>& entries() const { return entries_; }
    const PakEntry* find(const std::string& path) const;  // case-insensitive full path
    Bytes read(const PakEntry& e) const;                  // throws Error on a damaged member

private:
    Pak() = default;
    void parse_toc(const Bytes& toc, uint64_t total);
    size_t dir(const Bytes& t, std::string prefix, uint32_t count, size_t p);

    std::string path_;
    FILE* f_ = nullptr;
    std::vector<PakEntry> entries_;
    std::unordered_map<std::string, size_t> by_path_;
};

// Builds an archive in memory (tests, round trips). Files may be stored compressed. Directories follow from the paths.
struct PakFile {
    std::string path;
    Bytes data;
    bool compress = false;
};
Bytes build_pak(const std::vector<PakFile>& files);

}  // namespace p2m
