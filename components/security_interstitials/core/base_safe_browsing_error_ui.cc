// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/security_interstitials/core/base_safe_browsing_error_ui.h"

namespace security_interstitials {

BaseSafeBrowsingErrorUI::BaseSafeBrowsingErrorUI(
    const GURL& request_url,
    const GURL& main_frame_url,
    BaseSafeBrowsingErrorUI::SBInterstitialReason reason,
    const BaseSafeBrowsingErrorUI::SBErrorDisplayOptions& display_options,
    const std::string& app_locale,
    const base::Time& time_triggered,
    ControllerClient* controller)
    : request_url_(request_url),
      main_frame_url_(main_frame_url),
      interstitial_reason_(reason),
      display_options_(display_options),
      app_locale_(app_locale),
      time_triggered_(time_triggered),
      controller_(controller) {}

BaseSafeBrowsingErrorUI::~BaseSafeBrowsingErrorUI() {}

BaseSafeBrowsingErrorUI::SBErrorDisplayOptions::SBErrorDisplayOptions(
    bool is_main_frame_load_blocked,
    bool is_extended_reporting_opt_in_allowed,
    bool is_off_the_record,
    bool is_extended_reporting_enabled,
    bool is_scout_reporting_enabled,
    bool is_proceed_anyway_disabled,
    bool is_resource_cancellable,
    const std::string& help_center_article_link)
    : is_main_frame_load_blocked(is_main_frame_load_blocked),
      is_extended_reporting_opt_in_allowed(
          is_extended_reporting_opt_in_allowed),
      is_off_the_record(is_off_the_record),
      is_extended_reporting_enabled(is_extended_reporting_enabled),
      is_scout_reporting_enabled(is_scout_reporting_enabled),
      is_proceed_anyway_disabled(is_proceed_anyway_disabled),
      is_resource_cancellable(is_resource_cancellable),
      help_center_article_link(help_center_article_link) {}

BaseSafeBrowsingErrorUI::SBErrorDisplayOptions::SBErrorDisplayOptions(
    const BaseSafeBrowsingErrorUI::SBErrorDisplayOptions& other)
    : is_main_frame_load_blocked(other.is_main_frame_load_blocked),
      is_extended_reporting_opt_in_allowed(
          other.is_extended_reporting_opt_in_allowed),
      is_off_the_record(other.is_off_the_record),
      is_extended_reporting_enabled(other.is_extended_reporting_enabled),
      is_scout_reporting_enabled(other.is_scout_reporting_enabled),
      is_proceed_anyway_disabled(other.is_proceed_anyway_disabled),
      is_resource_cancellable(other.is_resource_cancellable),
      help_center_article_link(other.help_center_article_link) {}

}  // security_interstitials
