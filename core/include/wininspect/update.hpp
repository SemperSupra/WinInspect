#pragma once
#include <string>
#include <vector>

namespace wininspect::update {

struct UpdateInfo {
  bool update_available = false;
  std::string current_version;
  std::string latest_version;
  std::string download_url;
  std::string release_notes;
  std::string error; // empty on success
};

// Split "v1.2.3" into {1,2,3}. Returns empty vector on parse failure.
std::vector<int> parse_version(const std::string &tag);

// Compare two parsed versions: returns negative if a < b, 0 if equal, positive if a > b
int compare_versions(const std::vector<int> &a, const std::vector<int> &b);

// Query GitHub Releases API for the latest release of SemperSupra/WinInspect.
// Compares against the provided current_version string (e.g. "v0.0.1").
UpdateInfo check_for_update(const std::string &current_version);

// Download a release asset from the given URL to a temp file.
// Returns the local path on success, empty string on failure.
std::string download_update(const std::string &url, const std::string &version_tag);

} // namespace wininspect::update
