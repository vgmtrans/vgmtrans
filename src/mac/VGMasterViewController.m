//
//  VGMasterViewController.m
//  VGMTrans
//
//  Created by Michael Lowin on 8/27/14.
//
//

#import "VGMasterViewController.h"
#include "MacVGMRoot.h"

extern "C" MacVGMRoot macroot;

@interface VGMasterViewController ()

@end

@implementation VGMasterViewController

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    if (self) {
        // Initialization code here.
    }
    return self;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    
    // Get a new ViewCell
    NSTableCellView *cellView = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
    
    // Since this is a single-column table view, this would not be necessary.
    // But it's a good practice to do it in order by remember it when a table is multicolumn.
    if( [tableColumn.identifier isEqualToString:@"Filename"] )
    {
//        ScaryBugDoc *bugDoc = [self.bugs objectAtIndex:row];
//        cellView.imageView.image = bugDoc.thumbImage;
//        cellView.textField.stringValue = bugDoc.data.title;
        return cellView;
    }
    return cellView;
}


- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {

    return [self.bugs count];
}



@end
