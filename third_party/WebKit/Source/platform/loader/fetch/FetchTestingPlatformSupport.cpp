// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/loader/fetch/FetchTestingPlatformSupport.h"

#include "platform/loader/fetch/MockFetchContext.h"
#include "platform/network/ResourceError.h"
#include "platform/testing/weburl_loader_mock_factory_impl.h"
#include "public/platform/Platform.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLLoader.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include <memory>

namespace blink {

FetchTestingPlatformSupport::FetchTestingPlatformSupport()
    : m_urlLoaderMockFactory(new WebURLLoaderMockFactoryImpl(this)) {}

FetchTestingPlatformSupport::~FetchTestingPlatformSupport() {
  // Shutdowns WebURLLoaderMockFactory gracefully, serving all pending requests
  // first, then flushing all registered URLs.
  m_urlLoaderMockFactory->serveAsynchronousRequests();
  m_urlLoaderMockFactory->unregisterAllURLs();
}

MockFetchContext* FetchTestingPlatformSupport::context() {
  if (!m_context) {
    m_context = MockFetchContext::create(
        MockFetchContext::kShouldLoadNewResource,
        currentThread()->scheduler()->loadingTaskRunner());
  }
  return m_context;
}

WebURLError FetchTestingPlatformSupport::cancelledError(
    const WebURL& url) const {
  return ResourceError(errorDomainBlinkInternal, -1, url.string(),
                       "cancelledError for testing");
}

WebURLLoaderMockFactory*
FetchTestingPlatformSupport::getURLLoaderMockFactory() {
  return m_urlLoaderMockFactory.get();
}

WebURLLoader* FetchTestingPlatformSupport::createURLLoader() {
  return m_urlLoaderMockFactory->createURLLoader(nullptr);
}

}  // namespace blink
