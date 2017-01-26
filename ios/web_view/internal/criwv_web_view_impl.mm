// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/internal/criwv_web_view_impl.h"

#include <memory>
#include <utility>

#import "base/ios/weak_nsobject.h"
#include "base/memory/ptr_util.h"
#include "base/strings/sys_string_conversions.h"
#include "ios/web/public/referrer.h"
#import "ios/web/public/navigation_manager.h"
#import "ios/web/public/web_state/ui/crw_web_delegate.h"
#import "ios/web/public/web_state/web_state.h"
#import "ios/web/public/web_state/web_state_delegate_bridge.h"
#import "ios/web/public/web_state/web_state_observer_bridge.h"
#include "ios/web_view/internal/criwv_browser_state.h"
#import "ios/web_view/internal/translate/criwv_translate_client.h"
#import "ios/web_view/public/criwv_web_view_delegate.h"
#import "net/base/mac/url_conversions.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

@interface CRIWVWebViewImpl ()<CRWWebStateDelegate, CRWWebStateObserver> {
  id<CRIWVWebViewDelegate> _delegate;
  ios_web_view::CRIWVBrowserState* _browserState;
  std::unique_ptr<web::WebState> _webState;
  std::unique_ptr<web::WebStateDelegateBridge> _webStateDelegate;
  std::unique_ptr<web::WebStateObserverBridge> _webStateObserver;
  CGFloat _loadProgress;
}

@end

@implementation CRIWVWebViewImpl

@synthesize delegate = _delegate;
@synthesize loadProgress = _loadProgress;

- (instancetype)initWithBrowserState:
    (ios_web_view::CRIWVBrowserState*)browserState {
  self = [super init];
  if (self) {
    _browserState = browserState;

    web::WebState::CreateParams webStateCreateParams(_browserState);
    _webState = web::WebState::Create(webStateCreateParams);
    _webState->SetWebUsageEnabled(true);

    _webStateObserver =
        base::MakeUnique<web::WebStateObserverBridge>(_webState.get(), self);
    _webStateDelegate = base::MakeUnique<web::WebStateDelegateBridge>(self);
    _webState->SetDelegate(_webStateDelegate.get());

    // Initialize Translate.
    ios_web_view::CRIWVTranslateClient::CreateForWebState(_webState.get());
  }
  return self;
}

- (UIView*)view {
  return _webState->GetView();
}

- (BOOL)canGoBack {
  return _webState && _webState->GetNavigationManager()->CanGoBack();
}

- (BOOL)canGoForward {
  return _webState && _webState->GetNavigationManager()->CanGoForward();
}

- (BOOL)isLoading {
  return _webState->IsLoading();
}

- (NSURL*)visibleURL {
  return net::NSURLWithGURL(_webState->GetVisibleURL());
}

- (NSString*)pageTitle {
  return base::SysUTF16ToNSString(_webState->GetTitle());
}

- (void)goBack {
  if (_webState->GetNavigationManager())
    _webState->GetNavigationManager()->GoBack();
}

- (void)goForward {
  if (_webState->GetNavigationManager())
    _webState->GetNavigationManager()->GoForward();
}

- (void)reload {
  _webState->GetNavigationManager()->Reload(true);
}

- (void)stopLoading {
  _webState->Stop();
}

- (void)loadURL:(NSURL*)URL {
  web::NavigationManager::WebLoadParams params(net::GURLWithNSURL(URL));
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  _webState->GetNavigationManager()->LoadURLWithParams(params);
}

- (void)evaluateJavaScript:(NSString*)javaScriptString
         completionHandler:(void (^)(id, NSError*))completionHandler {
  [_webState->GetJSInjectionReceiver() executeJavaScript:javaScriptString
                                       completionHandler:completionHandler];
}

- (void)setDelegate:(id<CRIWVWebViewDelegate>)delegate {
  _delegate = delegate;

  // Set up the translate delegate.
  ios_web_view::CRIWVTranslateClient* translateClient =
      ios_web_view::CRIWVTranslateClient::FromWebState(_webState.get());
  id<CRIWVTranslateDelegate> translateDelegate = nil;
  if ([_delegate respondsToSelector:@selector(translateDelegate)])
    translateDelegate = [_delegate translateDelegate];
  translateClient->set_translate_delegate(translateDelegate);
}

- (void)notifyDidUpdateWithChanges:(CRIWVWebViewUpdateType)changes {
  SEL selector = @selector(webView:didUpdateWithChanges:);
  if ([_delegate respondsToSelector:selector]) {
    [_delegate webView:self didUpdateWithChanges:changes];
  }
}

// -----------------------------------------------------------------------
// WebStateObserver implementation.

- (void)didStartProvisionalNavigationForURL:(const GURL&)URL {
  [self notifyDidUpdateWithChanges:CRIWVWebViewUpdateTypeURL];
}

- (void)didCommitNavigationWithDetails:
    (const web::LoadCommittedDetails&)details {
  [self notifyDidUpdateWithChanges:CRIWVWebViewUpdateTypeURL];
}

- (void)webState:(web::WebState*)webState didLoadPageWithSuccess:(BOOL)success {
  DCHECK_EQ(_webState.get(), webState);
  SEL selector = @selector(webView:didFinishLoadingWithURL:loadSuccess:);
  if ([_delegate respondsToSelector:selector]) {
    [_delegate webView:self
        didFinishLoadingWithURL:[self visibleURL]
                    loadSuccess:success];
  }
}

- (void)webState:(web::WebState*)webState
    didChangeLoadingProgress:(double)progress {
  [self notifyDidUpdateWithChanges:CRIWVWebViewUpdateTypeProgress];
}

@end
