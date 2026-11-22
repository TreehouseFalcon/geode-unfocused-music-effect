#import <AppKit/AppKit.h>

bool isGameFocused() {
    return [[NSApplication sharedApplication] isActive];
}
