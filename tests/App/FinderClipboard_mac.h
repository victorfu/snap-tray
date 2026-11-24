#pragma once

#include <QString>

// Writes what Finder puts on the general pasteboard when a file is copied
// (#414): a file reference URL, the file name as text and the file type icon
// as com.apple.icns, with no pixels of the file itself.
void writeFinderFileCopyToPasteboard(const QString& path);
