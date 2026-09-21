#include "IinaApplication.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <QFileInfo>

const char* const IinaApplication::kBundleIdentifier = "com.colliderli.iina";
const char* const IinaApplication::kDownloadUrl = "https://iina.io";

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaApplication::Info IinaApplication::Discover()
{
  Info info;

  @autoreleasepool
  {
    NSURL* appUrl = [[NSWorkspace sharedWorkspace]
        URLForApplicationWithBundleIdentifier:@(kBundleIdentifier)];
    if (!appUrl)
      return info;

    NSBundle* bundle = [NSBundle bundleWithURL:appUrl];
    if (!bundle)
      return info;

    // Resolve symlinks so the reported path is stable across /Applications aliases.
    info.applicationPath = QFileInfo(QString::fromNSString([appUrl path])).canonicalFilePath();
    if (info.applicationPath.isEmpty())
      return info;

    NSString* version = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if (version)
      info.version = QString::fromNSString(version);

    // iina-cli lives next to the main executable inside the bundle. Using the bundled copy rather
    // than a /usr/local/bin symlink keeps us pinned to the app we actually discovered.
    const QString cli = info.applicationPath + QStringLiteral("/Contents/MacOS/iina-cli");
    const QFileInfo cliInfo(cli);
    if (cliInfo.isFile() && cliInfo.isExecutable())
      info.cliPath = cliInfo.canonicalFilePath();

    info.installed = true;
  }

  return info;
}
