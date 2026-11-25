#include "Zip.h"

#include "PathUtil.h"
#include "src/Zip/ZipWriter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool Fail(std::string* error, const std::string& text) {
    if (error) *error = text;
    return false;
}

} // namespace

bool Zip::WriteFolder(const std::string& folder, const std::string& outFile, std::string* error) {
    std::error_code ec;
    const fs::path root = PathUtil::MakePath(folder);
    if (!fs::is_directory(root, ec)) return Fail(error, "folder not found: " + folder);

    std::vector<fs::path> files;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(root, ec)) {
        if (e.is_regular_file(ec)) files.push_back(e.path());
    }
    if (files.empty()) return Fail(error, "nothing to pack");
    std::sort(files.begin(), files.end());

    const fs::path target = PathUtil::MakePath(outFile);
    fs::create_directories(target.parent_path(), ec);

    Forge::ZipWriter writer;
    std::string zipError;
    if (!writer.Open(target, zipError)) return Fail(error, zipError);

    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            writer.Abort();
            return Fail(error, "cannot read " + PathUtil::U8(file));
        }
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        const auto relative = fs::relative(file, root, ec).generic_u8string();
        const std::string name(relative.begin(), relative.end());
        if (name.empty()) {
            writer.Abort();
            return Fail(error, "empty archive entry name");
        }
        if (!writer.Add(name, data.data(), data.size(), zipError)) {
            writer.Abort();
            return Fail(error, zipError);
        }
    }

    if (!writer.Finish(zipError)) {
        writer.Abort();
        return Fail(error, zipError);
    }
    return true;
}
