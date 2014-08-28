//
//  VGAppDelegate.m
//  VGMTrans
//
//  Created by Mike on 8/24/14.
//
//

#import "VGAppDelegate.h"
#import "VGMasterViewController.h"

@interface VGAppDelegate()
@property (nonatomic, strong) IBOutlet VGMasterViewController *masterViewController;
@end

@implementation VGAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    self.masterViewController = [[VGMasterViewController alloc] initWithNibName:@"VGMasterViewController" bundle:nil];
    
    [self.window.contentView addSubview:self.masterViewController.view];
    self.masterViewController.view.frame = ((NSView*)self.window.contentView).bounds;
}

@end
