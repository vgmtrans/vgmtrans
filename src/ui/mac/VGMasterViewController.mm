//
//  VGMasterViewController.m
//  VGMTrans
//
//  Created by Mike on 8/27/14.
//
//

#import "VGMasterViewController.h"
#include "MacVGMRootCpp.hh"
#import "NSString+VGMTrans.h"
#import "VGRawFileTableView.h"
#import "MacVGMRoot.hh"
#include "VGMFile.h"
#include "VGMColl.h"

extern "C" MacVGMRootCpp macroot;

@interface VGMasterViewController () <NSTableViewDelegate, NSTableViewDataSource>
//@property (weak) IBOutlet VGRawFileTableView *rawFileTableView;
@property (weak) IBOutlet NSTableView *rawFileTableView;
@property (weak) IBOutlet NSTableView *vgmFileTableView;
@property (weak) IBOutlet NSTableView *collTableView;
@end

@implementation VGMasterViewController

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    if (self) {
//        self.rawFileTableView.delegate = self;
        // Initialization code here.
        NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
        [center addObserver:self
                   selector:@selector(didAddRawFile:)
                       name:kVGDidAddRawFileNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(didRemoveRawFile:)
                       name:kVGDidRemoveRawFileNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(didAddVGMFile:)
                       name:kVGDidAddVGMFileNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(didRemoveVGMFile:)
                       name:kVGDidRemoveVGMFileNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(didAddCollection:)
                       name:kVGDidAddCollectionNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(didRemoveCollection:)
                       name:kVGDidRemoveCollectionNotification
                     object:nil];
    }

    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}


- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    
    // Get a new ViewCell
    NSTableCellView *cellView = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
    
    // Since this is a single-column table view, this would not be necessary.
    // But it's a good practice to do it in order by remember it when a table is multicolumn.
    if (tableView == self.rawFileTableView) {
        if( [tableColumn.identifier isEqualToString:@"Filename"] )
        {
            RawFile* rawfile = macroot.vRawFile[row];

            cellView.textField.stringValue = [NSString stringFromWchar:rawfile->GetFileName()];
            return cellView;
        }
    }
    else if (tableView == self.vgmFileTableView) {
        VGMFile* vgmfile = macroot.vVGMFile[row];

        cellView.textField.stringValue = [NSString stringFromWchar:vgmfile->GetName()->c_str()];
        return cellView;
    }
    else if (tableView == self.collTableView) {
        VGMColl* coll = macroot.vVGMColl[row];

        cellView.textField.stringValue = [NSString stringFromWchar:coll->GetName()->c_str()];
        return cellView;
    }
    return cellView;
}


- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    if (tableView == self.rawFileTableView) {
        return macroot.vRawFile.size();
    }
    else if (tableView == self.vgmFileTableView) {
        return macroot.vVGMFile.size();
    }
    else if (tableView == self.collTableView) {
        return macroot.vVGMColl.size();
    }
    return 0;
}


#pragma mark - Notification handlers

- (void)didAddRawFile:(NSNotification*)notification
{
    [self.rawFileTableView reloadData];
}

- (void)didRemoveRawFile:(NSNotification*)notification
{
    [self.rawFileTableView reloadData];
}

- (void)didAddVGMFile:(NSNotification*)notification
{
    [self.vgmFileTableView reloadData];
}

- (void)didRemoveVGMFile:(NSNotification*)notification
{
    [self.vgmFileTableView reloadData];
}

- (void)didAddCollection:(NSNotification*)notification
{
    [self.collTableView reloadData];
}

- (void)didRemoveCollection:(NSNotification*)notification
{
    [self.collTableView reloadData];
}



@end
