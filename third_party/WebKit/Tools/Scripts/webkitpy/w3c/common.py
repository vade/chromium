# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility functions used both when importing and exporting."""

import logging

from webkitpy.w3c.chromium_commit import ChromiumCommit


WPT_DEST_NAME = 'wpt'
CSS_DEST_NAME = 'csswg-test'

# TODO(qyearsley): This directory should be able to be constructed with
# WebKitFinder and WPT_DEST_NAME, plus the string "external".
CHROMIUM_WPT_DIR = 'third_party/WebKit/LayoutTests/external/wpt/'

# Our mirrors of the official w3c repos, which we pull from.
WPT_REPO_URL = 'https://chromium.googlesource.com/external/w3c/web-platform-tests.git'
CSS_REPO_URL = 'https://chromium.googlesource.com/external/w3c/csswg-test.git'

_log = logging.getLogger(__name__)


def exportable_commits_since(commit, host, local_wpt):
    """Lists exportable commits after a certain point.

    Args:
        commit: The SHA of the Chromium commit from which this method will look.
        host: A Host object.
        local_wpt: A LocalWPT instance, used to see whether a Chromium commit
            can be applied cleanly in the upstream repo.

    Returns:
        A list of ChromiumCommit objects for commits that are exportable since
        the given commit, in chronological order.
    """
    chromium_repo_root = host.executive.run_command(['git', 'rev-parse', '--show-toplevel']).strip()

    wpt_path = chromium_repo_root + '/' + CHROMIUM_WPT_DIR
    commit_hashes = host.executive.run_command(
        ['git', 'rev-list', '{}..HEAD'.format(commit), '--reverse', '--', wpt_path]).splitlines()

    chromium_commits = [ChromiumCommit(host, sha=sha) for sha in commit_hashes]
    return [c for c in chromium_commits if is_exportable(c, local_wpt)]


def is_exportable(chromium_commit, local_wpt):
    """Checks whether a given patch is exportable and can be applied."""
    patch = chromium_commit.format_patch()
    return (patch and
            local_wpt.test_patch(patch) and
            'NOEXPORT=true' not in chromium_commit.message() and
            not chromium_commit.message().startswith('Import '))
