//
//  VGAppDelegate.m
//  VGMTrans
//
//  Created by Mike on 8/24/14.
//
//

#import "VGAppDelegate.h"
#import "VGMasterViewController.h"
#import "MacVGMRoot.hh"

@interface VGAppDelegate() <NSDraggingDestination>
@property (nonatomic, strong) IBOutlet VGMasterViewController *masterViewController;
@end

@implementation VGAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    [[MacVGMRoot sharedInstance] Init];

    self.masterViewController = [[VGMasterViewController alloc] initWithNibName:@"VGMasterViewController" bundle:nil];
    
    [self.window.contentView addSubview:self.masterViewController.view];
    [self.window registerForDraggedTypes:[NSArray arrayWithObjects: NSFilenamesPboardType, nil]];
    self.window.delegate = self;
    self.masterViewController.view.frame = ((NSView*)self.window.contentView).bounds;
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender {
    NSPasteboard *pboard;
    NSDragOperation sourceDragMask;

    sourceDragMask = [sender draggingSourceOperationMask];
    pboard = [sender draggingPasteboard];

//    if ( [[pboard types] containsObject:NSColorPboardType] ) {
//        if (sourceDragMask & NSDragOperationGeneric) {
//            return NSDragOperationGeneric;
//        }
//    }
    if ( [[pboard types] containsObject:NSFilenamesPboardType] ) {
        if (sourceDragMask & NSDragOperationLink) {
            return NSDragOperationLink;
        } else if (sourceDragMask & NSDragOperationCopy) {
            return NSDragOperationCopy;
        }
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender {
    NSPasteboard *pboard;
    NSDragOperation sourceDragMask;

    sourceDragMask = [sender draggingSourceOperationMask];
    pboard = [sender draggingPasteboard];

    if ( [[pboard types] containsObject:NSFilenamesPboardType] ) {
        NSArray *files = [pboard propertyListForType:NSFilenamesPboardType];

        // Depending on the dragging source and modifier keys,
        // the file data may be copied or linked
        if (sourceDragMask & NSDragOperationLink) {
            for (NSString *filename in files) {
                [[MacVGMRoot sharedInstance] OpenRawFile:filename];
            }
//            [self addLinkToFiles:files];
        } else {
//            [self addDataFromFiles:files];
        }
    }
    return YES;
}

@end
