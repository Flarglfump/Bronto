/*
 * Compile-time check that the AppKit constants hard-coded in
 * src/platform/platform_macos.c (which is plain C and cannot import the
 * Objective-C headers) match the real SDK. Compile only:
 *   clang -fsyntax-only -fobjc-arc tests/platform/macos_constants_check.m
 */
#import <AppKit/AppKit.h>
#import <IOKit/hidsystem/IOLLEvent.h>

#define SAME(a, b) _Static_assert((unsigned long long)(a) == (unsigned long long)(b), #a " != " #b)

SAME(NSWindowStyleMaskTitled, 1ul << 0);
SAME(NSWindowStyleMaskClosable, 1ul << 1);
SAME(NSWindowStyleMaskMiniaturizable, 1ul << 2);
SAME(NSWindowStyleMaskResizable, 1ul << 3);
SAME(NSBackingStoreBuffered, 2);
SAME(NSWindowCollectionBehaviorFullScreenPrimary, 1ul << 7);
SAME(NSApplicationActivationPolicyRegular, 0);
SAME(NSTerminateCancel, 0);
SAME(NSEventMaskAny, ~0ull);
SAME(NSTrackingMouseEnteredAndExited, 0x01);
SAME(NSTrackingActiveAlways, 0x80);
SAME(NSTrackingInVisibleRect, 0x200);

SAME(NSEventTypeLeftMouseDown, 1);
SAME(NSEventTypeLeftMouseUp, 2);
SAME(NSEventTypeRightMouseDown, 3);
SAME(NSEventTypeRightMouseUp, 4);
SAME(NSEventTypeMouseMoved, 5);
SAME(NSEventTypeLeftMouseDragged, 6);
SAME(NSEventTypeRightMouseDragged, 7);
SAME(NSEventTypeKeyDown, 10);
SAME(NSEventTypeKeyUp, 11);
SAME(NSEventTypeFlagsChanged, 12);
SAME(NSEventTypeScrollWheel, 22);
SAME(NSEventTypeOtherMouseDown, 25);
SAME(NSEventTypeOtherMouseUp, 26);
SAME(NSEventTypeOtherMouseDragged, 27);

SAME(NSEventModifierFlagCapsLock, 1ul << 16);
SAME(NSEventModifierFlagShift, 1ul << 17);
SAME(NSEventModifierFlagControl, 1ul << 18);
SAME(NSEventModifierFlagOption, 1ul << 19);
SAME(NSEventModifierFlagCommand, 1ul << 20);

SAME(NX_DEVICELCTLKEYMASK, 0x0001);
SAME(NX_DEVICELSHIFTKEYMASK, 0x0002);
SAME(NX_DEVICERSHIFTKEYMASK, 0x0004);
SAME(NX_DEVICELCMDKEYMASK, 0x0008);
SAME(NX_DEVICERCMDKEYMASK, 0x0010);
SAME(NX_DEVICELALTKEYMASK, 0x0020);
SAME(NX_DEVICERALTKEYMASK, 0x0040);
SAME(NX_DEVICERCTLKEYMASK, 0x2000);

/* CGRect is what NSRect is on 64-bit, so passing CGRect by value to
 * objc_msgSend matches -[NSWindow initWithContentRect:...]. */
_Static_assert(sizeof(NSRect) == sizeof(CGRect), "NSRect layout");
_Static_assert(sizeof(NSUInteger) == sizeof(unsigned long), "NSUInteger");
_Static_assert(sizeof(CGFloat) == sizeof(double), "CGFloat");
