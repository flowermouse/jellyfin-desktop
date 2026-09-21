#ifndef IINAAPPLICATION_H
#define IINAAPPLICATION_H

#include <QString>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Locates the official IINA application. Never hardcodes /Applications/IINA.app: the user may have
// installed it anywhere, and LaunchServices already knows where it is.
namespace IinaApplication
{
  struct Info
  {
    bool installed = false;
    QString applicationPath;
    QString cliPath;
    // Diagnostics only. Feature decisions must never be gated on the version string.
    QString version;
  };

  // The bundle identifier of the official stable IINA release.
  extern const char* const kBundleIdentifier;

  // The download page used when IINA is missing.
  extern const char* const kDownloadUrl;

  Info Discover();
}

#endif // IINAAPPLICATION_H
