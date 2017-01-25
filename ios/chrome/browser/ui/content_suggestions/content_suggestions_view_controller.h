// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/collection_view/collection_view_controller.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_expandable_item.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_favicon_item.h"

@protocol ContentSuggestionsCommands;

// CollectionViewController to display the suggestions items.
@interface ContentSuggestionsViewController
    : CollectionViewController<ContentSuggestionsExpandableCellDelegate,
                               ContentSuggestionsFaviconCellDelegate>

// Handler for the commands sent by the ContentSuggestionsViewController.
@property(nonatomic, weak) id<ContentSuggestionsCommands>
    suggestionCommandHandler;

// Adds a text item with a |title| and a |subtitle| in the section numbered
// |section|. If |section| is greater than the current number of section, it
// will add a new section at the end.
- (void)addTextItem:(NSString*)title
           subtitle:(NSString*)subtitle
          toSection:(NSInteger)inputSection;

@end

#endif  // IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_
