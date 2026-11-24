#include "FinderClipboard_mac.h"

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {

NSData* fileTypeIconData(NSURL* url)
{
    NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:url.path];
    NSMutableData* data = [NSMutableData data];
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(
        (__bridge CFMutableDataRef)data, (__bridge CFStringRef)UTTypeICNS.identifier, 2, nullptr);
    for (const CGFloat side : {256.0, 512.0}) {
        NSRect rect = NSMakeRect(0, 0, side, side);
        CGImageRef image = [icon CGImageForProposedRect:&rect context:nil hints:nil];
        CGImageDestinationAddImage(destination, image, nullptr);
    }
    CGImageDestinationFinalize(destination);
    CFRelease(destination);
    return data;
}

} // namespace

void writeFinderFileCopyToPasteboard(const QString& path)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:path.toNSString()];
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        [item setString:url.fileReferenceURL.absoluteString forType:NSPasteboardTypeFileURL];
        [item setString:url.lastPathComponent forType:NSPasteboardTypeString];
        [item setData:fileTypeIconData(url) forType:UTTypeICNS.identifier];
        NSPasteboard* board = [NSPasteboard generalPasteboard];
        [board clearContents];
        [board writeObjects:@[item]];
    }
}
