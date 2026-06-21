#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung


#include <string>
#include <vector>

namespace wininspect::update {

struct UpdateInfo {
  bool update_available = false;
  std::string current_version;
  std::string latest_version;
  std::string release_notes;
  std::string error; // empty on success

  // Download URLs for each distribution type
  std::string installer_url;    // NSIS .exe
  std::string portable_zip_url; // portable .zip
};

// Split "v1.2.3" into {1,2,3}. Returns empty vector on parse failure.
std::vector<int> parse_version(const std::string &tag);

// Compare two parsed versions: returns negative if a < b, 0 if equal, positive if a > b
int compare_versions(const std::vector<int> &a, const std::vector<int> &b);

// Query GitHub Releases API for the latest release of SemperSupra/WinInspect.
// Compares against the provided current_version string (e.g. "v0.0.1").
// Populates both installer_url and portable_zip_url from release assets.
[[nodiscard]] UpdateInfo check_for_update(const std::string &current_version);

// Download a release asset from the given URL to a temp file.
// The type_hint ("installer" or "portable") determines the output extension.
// Returns the local path on success, empty string on failure.
[[nodiscard]] std::string download_update(const std::string &url, const std::string &version_tag,
                            const std::string &type_hint = "installer");

} // namespace wininspect::update
