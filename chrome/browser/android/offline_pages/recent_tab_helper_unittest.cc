// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/recent_tab_helper.h"

#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/android/offline_pages/offline_page_model_factory.h"
#include "chrome/browser/android/offline_pages/request_coordinator_factory.h"
#include "chrome/browser/android/offline_pages/test_offline_page_model_builder.h"
#include "chrome/browser/android/offline_pages/test_request_coordinator_builder.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/offline_pages/core/client_namespace_constants.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/offline_pages/core/offline_page_item.h"
#include "components/offline_pages/core/offline_page_model.h"
#include "components/offline_pages/core/offline_page_test_archiver.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace offline_pages {

namespace {
const GURL kTestPageUrl("http://mystery.site/foo.html");
const GURL kTestPageUrlOther("http://crazy.site/foo_other.html");
const int kTabId = 153;
}  // namespace

class TestDelegate: public RecentTabHelper::Delegate {
 public:
  const size_t kArchiveSizeToReport = 1234;

  explicit TestDelegate(
      OfflinePageTestArchiver::Observer* observer,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      int tab_id,
      bool tab_id_result);
  ~TestDelegate() override {}

  std::unique_ptr<OfflinePageArchiver> CreatePageArchiver(
        content::WebContents* web_contents) override;
  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() override;
    // There is no expectations that tab_id is always present.
  bool GetTabId(content::WebContents* web_contents, int* tab_id) override;

  void set_archive_result(
      offline_pages::OfflinePageArchiver::ArchiverResult result) {
    archive_result_ = result;
  }

  void set_archive_size(int64_t size) { archive_size_ = size; }

 private:
  OfflinePageTestArchiver::Observer* observer_;  // observer owns this.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  int tab_id_;
  bool tab_id_result_;

  // These values can be updated so that new OfflinePageTestArchiver instances
  // will return different results.
  offline_pages::OfflinePageArchiver::ArchiverResult archive_result_ =
      offline_pages::OfflinePageArchiver::ArchiverResult::SUCCESSFULLY_CREATED;
  int64_t archive_size_ = kArchiveSizeToReport;
};

class RecentTabHelperTest
    : public ChromeRenderViewHostTestHarness,
      public OfflinePageModel::Observer,
      public OfflinePageTestArchiver::Observer {
 public:
  RecentTabHelperTest();
  ~RecentTabHelperTest() override {}

  void SetUp() override;
  const std::vector<OfflinePageItem>& GetAllPages();

  void FailLoad(const GURL& url);

  // Runs default thread.
  void RunUntilIdle();
  // Moves forward the snapshot controller's task runner.
  void FastForwardSnapshotController();

  // Navigates to the URL and commit as if it has been typed in the address bar.
  // Note: we need this to simulate navigations to the same URL that more like a
  // reload and not same page. NavigateAndCommit simulates a click on a link
  // and when reusing the same URL that will be considered a same page
  // navigation.
  void NavigateAndCommitTyped(const GURL& url);

  RecentTabHelper* recent_tab_helper() const { return recent_tab_helper_; }

  OfflinePageModel* model() const { return model_; }

  // Returns a OfflinePageItem pointer from |all_pages| that matches the
  // provided |offline_id|. If a match is not found returns nullptr.
  const OfflinePageItem* FindPageForOfflineId(int64_t offline_id) {
    for (const OfflinePageItem& page : GetAllPages()) {
      if (page.offline_id == offline_id)
        return &page;
    }
    return nullptr;
  }

  scoped_refptr<base::TestMockTimeTaskRunner>& task_runner() {
    return task_runner_;
  }

  size_t page_added_count() { return page_added_count_; }
  size_t model_removed_count() { return model_removed_count_; }

  // OfflinePageModel::Observer
  void OfflinePageModelLoaded(OfflinePageModel* model) override {
    all_pages_needs_updating_ = true;
  }
  void OfflinePageAdded(OfflinePageModel* model,
                        const OfflinePageItem& added_page) override {
    page_added_count_++;
    all_pages_needs_updating_ = true;
  }
  void OfflinePageDeleted(int64_t, const offline_pages::ClientId&) override {
    model_removed_count_++;
    all_pages_needs_updating_ = true;
  }

  // OfflinePageTestArchiver::Observer
  void SetLastPathCreatedByArchiver(const base::FilePath& file_path) override {}

 private:
  void OnGetAllPagesDone(const std::vector<OfflinePageItem>& result);

  RecentTabHelper* recent_tab_helper_;  // Owned by WebContents.
  OfflinePageModel* model_;  // Keyed service
  size_t page_added_count_;
  size_t model_removed_count_;
  std::vector<OfflinePageItem> all_pages_;
  bool all_pages_needs_updating_;
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_;
  base::test::ScopedFeatureList scoped_feature_list_;

  base::WeakPtrFactory<RecentTabHelperTest> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(RecentTabHelperTest);
};

TestDelegate::TestDelegate(
    OfflinePageTestArchiver::Observer* observer,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    int tab_id,
    bool tab_id_result)
    : observer_(observer),
      task_runner_(task_runner),
      tab_id_(tab_id),
      tab_id_result_(tab_id_result) {
}

std::unique_ptr<OfflinePageArchiver> TestDelegate::CreatePageArchiver(
    content::WebContents* web_contents) {
  std::unique_ptr<OfflinePageTestArchiver> archiver(new OfflinePageTestArchiver(
      observer_, web_contents->GetLastCommittedURL(), archive_result_,
      base::string16(), kArchiveSizeToReport,
      base::ThreadTaskRunnerHandle::Get()));
  return std::move(archiver);
}

scoped_refptr<base::SingleThreadTaskRunner> TestDelegate::GetTaskRunner() {
  return task_runner_;
}
  // There is no expectations that tab_id is always present.
bool TestDelegate::GetTabId(content::WebContents* web_contents, int* tab_id) {
  *tab_id = tab_id_;
  return tab_id_result_;
}

RecentTabHelperTest::RecentTabHelperTest()
    : recent_tab_helper_(nullptr),
      model_(nullptr),
      page_added_count_(0),
      model_removed_count_(0),
      all_pages_needs_updating_(true),
      task_runner_(new base::TestMockTimeTaskRunner),
      weak_ptr_factory_(this) {}

void RecentTabHelperTest::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();

  scoped_feature_list_.InitAndEnableFeature(kOffliningRecentPagesFeature);
  // Sets up the factories for testing.
  OfflinePageModelFactory::GetInstance()->SetTestingFactoryAndUse(
      browser_context(), BuildTestOfflinePageModel);
  RunUntilIdle();
  RequestCoordinatorFactory::GetInstance()->SetTestingFactoryAndUse(
      browser_context(), BuildTestRequestCoordinator);
  RunUntilIdle();

  RecentTabHelper::CreateForWebContents(web_contents());
  recent_tab_helper_ = RecentTabHelper::FromWebContents(web_contents());

  recent_tab_helper_->SetDelegate(base::MakeUnique<TestDelegate>(
      this, task_runner(), kTabId, true));

  model_ = OfflinePageModelFactory::GetForBrowserContext(browser_context());
  model_->AddObserver(this);
}

void RecentTabHelperTest::FailLoad(const GURL& url) {
  controller().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                       std::string());
  content::RenderFrameHostTester::For(main_rfh())->SimulateNavigationStart(url);
  content::RenderFrameHostTester::For(main_rfh())->
      SimulateNavigationError(url, net::ERR_INTERNET_DISCONNECTED);
  content::RenderFrameHostTester::For(main_rfh())->
      SimulateNavigationErrorPageCommit();
}

const std::vector<OfflinePageItem>& RecentTabHelperTest::GetAllPages() {
  if (all_pages_needs_updating_) {
    model()->GetAllPages(base::Bind(&RecentTabHelperTest::OnGetAllPagesDone,
                                    weak_ptr_factory_.GetWeakPtr()));
    RunUntilIdle();
    all_pages_needs_updating_ = false;
  }
  return all_pages_;
}

void RecentTabHelperTest::OnGetAllPagesDone(
    const std::vector<OfflinePageItem>& result) {
  all_pages_ = result;
}

void RecentTabHelperTest::RunUntilIdle() {
  base::RunLoop().RunUntilIdle();
}

void RecentTabHelperTest::FastForwardSnapshotController() {
  const size_t kLongDelayMs = 100*1000;
  task_runner_->FastForwardBy(base::TimeDelta::FromMilliseconds(kLongDelayMs));
}

void RecentTabHelperTest::NavigateAndCommitTyped(const GURL& url) {
  controller().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                       std::string());
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->CommitPendingNavigation();
}

// Checks the test setup.
TEST_F(RecentTabHelperTest, RecentTabHelperInstanceExists) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.Init();
  EXPECT_NE(nullptr, recent_tab_helper());
}

// Fully loads a page then simulates the tab being hidden. Verifies that a
// snapshot is created only when the latter happens.
TEST_F(RecentTabHelperTest, LastNCaptureAfterLoad) {
  // Navigate and finish loading. Nothing should be saved.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  // Move the snapshot controller's time forward so it gets past timeouts.
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  EXPECT_EQ(0U, page_added_count());
  ASSERT_EQ(0U, GetAllPages().size());

  // Tab is hidden with a fully loaded page. A snapshot save should happen.
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  EXPECT_EQ(1U, page_added_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  EXPECT_EQ(kLastNNamespace, GetAllPages()[0].client_id.name_space);
}

// Simulates the tab being hidden too early in the page loading so that a
// snapshot should not be created.
TEST_F(RecentTabHelperTest, NoLastNCaptureIfTabHiddenTooEarlyInPageLoad) {
  // Commit the navigation and hide the tab. Nothing should be saved.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  EXPECT_EQ(0U, page_added_count());
  ASSERT_EQ(0U, GetAllPages().size());

  // Then allow the page to fully load. Nothing should be saved.
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  // Move the snapshot controller's time forward so it gets past timeouts.
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(0U, page_added_count());
  ASSERT_EQ(0U, GetAllPages().size());
}

// Checks that WebContents with no tab IDs have snapshot requests properly
// ignored from both last_n and downloads.
TEST_F(RecentTabHelperTest, NoTabIdNoCapture) {
  // Create delegate that returns 'false' as TabId retrieval result.
  recent_tab_helper()->SetDelegate(base::MakeUnique<TestDelegate>(
      this, task_runner(), kTabId, false));

  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  recent_tab_helper()->ObserveAndDownloadCurrentPage(
      ClientId("download", "id2"), 123L);
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  // No page should be captured.
  EXPECT_EQ(0U, page_added_count());
  ASSERT_EQ(0U, GetAllPages().size());
}

// Triggers two last_n snapshot captures during a single page load. Should end
// up with one snapshot, the 1st being replaced by the 2nd.
TEST_F(RecentTabHelperTest, TwoCapturesSamePageLoad) {
  NavigateAndCommit(kTestPageUrl);
  // Set page loading state to the 1st snapshot-able stage. No capture so far.
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  EXPECT_EQ(0U, page_added_count());

  // Tab is hidden and a snapshot should be saved.
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  int64_t first_offline_id = GetAllPages()[0].offline_id;

  // Set page loading state to the 2nd and last snapshot-able stage. No new
  // capture should happen.
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  // Tab is hidden again. At this point a higher quality snapshot is expected so
  // a new one should be captured and replace the previous one.
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  EXPECT_NE(first_offline_id, GetAllPages()[0].offline_id);
}

// Triggers two last_n captures during a single page load, where the 2nd capture
// fails. Should end up with one offline page (the 1st, successful snapshot
// should be kept).
// TODO(carlosk): re-enable once https://crbug.com/655697 is fixed, again.
TEST_F(RecentTabHelperTest, DISABLED_TwoCapturesWhere2ndFailsSamePageLoad) {
  // Navigate and load until the 1st stage. Tab hidden should trigger a capture.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  int64_t first_offline_id = GetAllPages()[0].offline_id;

  // Sets a new delegate that will make the second snapshot fail.
  TestDelegate* failing_delegate =
      new TestDelegate(this, task_runner(), kTabId, true);
  failing_delegate->set_archive_size(-1);
  failing_delegate->set_archive_result(
      offline_pages::OfflinePageArchiver::ArchiverResult::
          ERROR_ARCHIVE_CREATION_FAILED);
  recent_tab_helper()->SetDelegate(
      std::unique_ptr<TestDelegate>(failing_delegate));

  // Advance loading to the 2nd and final stage and then hide the tab. A new
  // capture is requested but its creation will fail. The exact same snapshot
  // from before should still be available.
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  EXPECT_EQ(first_offline_id, GetAllPages()[0].offline_id);
}

// Triggers two last_n captures for two different loads of the same URL (aka
// reload). Should end up with a single snapshot (from the 2nd load).
TEST_F(RecentTabHelperTest, TwoCapturesDifferentPageLoadsSameUrl) {
  // Fully load the page. Hide the tab and check for a snapshot.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  int64_t first_offline_id = GetAllPages()[0].offline_id;

  // Navigate with the same URL until the page is minimally loaded then hide the
  // tab. The previous snapshot should be removed and a new one taken.
  NavigateAndCommitTyped(kTestPageUrl);
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  EXPECT_NE(first_offline_id, GetAllPages()[0].offline_id);
}

// Triggers two last_n captures for two different page loads of the same URL
// (aka reload), where the 2nd capture fails. Should end up with no offline
// pages (a privacy driven decision).
TEST_F(RecentTabHelperTest, TwoCapturesWhere2ndFailsDifferentPageLoadsSameUrl) {
  // Fully load the page then hide the tab. A capture is expected.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);

  // Sets a new delegate that will make the second snapshot fail.
  TestDelegate* failing_delegate =
      new TestDelegate(this, task_runner(), kTabId, true);
  failing_delegate->set_archive_size(-1);
  failing_delegate->set_archive_result(
      offline_pages::OfflinePageArchiver::ArchiverResult::
          ERROR_ARCHIVE_CREATION_FAILED);
  recent_tab_helper()->SetDelegate(
      std::unique_ptr<TestDelegate>(failing_delegate));

  // Fully load the page once more then hide the tab again. A capture happens
  // and fails but no snapshot should remain.
  NavigateAndCommitTyped(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(0U, GetAllPages().size());
}

// Triggers two last_n captures for two different page loads of different URLs.
// Should end up with a single snapshot of the last page.
TEST_F(RecentTabHelperTest, TwoCapturesDifferentPageLoadsDifferentUrls) {
  // Fully load the first URL then hide the tab and check for a snapshot.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);

  // Fully load the second URL then hide the tab and check for a single snapshot
  // of the new page.
  NavigateAndCommitTyped(kTestPageUrlOther);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrlOther, GetAllPages()[0].url);
}

// Fully loads a page where last_n captures two snapshots. Then triggers two
// snapshot requests by downloads. Should end up with three offline pages: one
// from last_n (2nd replaces the 1st) and two from downloads (which shouldn't
// replace each other).
TEST_F(RecentTabHelperTest, TwoLastNAndTwoDownloadCapturesSamePage) {
  // Fully loads the page with intermediary steps where the tab is hidden. Then
  // check that two last_n snapshots were created but only one was kept.
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  EXPECT_EQ(kTestPageUrl, GetAllPages()[0].url);
  int64_t first_offline_id = GetAllPages()[0].offline_id;

  // First snapshot request by downloads. Two offline pages are expected.
  const int64_t second_offline_id = first_offline_id + 1;
  const ClientId second_client_id("download", "id2");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(second_client_id,
                                                     second_offline_id);
  RunUntilIdle();
  EXPECT_EQ(3U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(2U, GetAllPages().size());
  EXPECT_NE(nullptr, FindPageForOfflineId(first_offline_id));
  const OfflinePageItem* second_page = FindPageForOfflineId(second_offline_id);
  ASSERT_NE(nullptr, second_page);
  EXPECT_EQ(kTestPageUrl, second_page->url);
  EXPECT_EQ(second_client_id, second_page->client_id);

  // Second snapshot request by downloads. Three offline pages are expected.
  const int64_t third_offline_id = first_offline_id + 2;
  const ClientId third_client_id("download", "id2");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(third_client_id,
                                                     third_offline_id);
  RunUntilIdle();
  EXPECT_EQ(4U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(3U, GetAllPages().size());
  EXPECT_NE(nullptr, FindPageForOfflineId(first_offline_id));
  EXPECT_NE(nullptr, FindPageForOfflineId(second_offline_id));
  const OfflinePageItem* third_page = FindPageForOfflineId(third_offline_id);
  ASSERT_NE(nullptr, third_page);
  EXPECT_EQ(kTestPageUrl, third_page->url);
  EXPECT_EQ(third_client_id, third_page->client_id);
}

// Simulates an error (disconnection) during the load of a page. Should end up
// with no offline pages for any requester.
TEST_F(RecentTabHelperTest, NoCaptureOnErrorPage) {
  FailLoad(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  recent_tab_helper()->ObserveAndDownloadCurrentPage(
      ClientId("download", "id1"), 123L);
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  ASSERT_EQ(0U, GetAllPages().size());
}

// Checks that last_n snapshots are not created if the feature is disabled.
// Download requests should still work.
TEST_F(RecentTabHelperTest, LastNFeatureNotEnabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.Init();
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  // No page should be captured.
  ASSERT_EQ(0U, GetAllPages().size());

  recent_tab_helper()->ObserveAndDownloadCurrentPage(
      ClientId("download", "id1"), 123L);
  RunUntilIdle();
  // No page should be captured.
  ASSERT_EQ(1U, GetAllPages().size());
}

// Simulates a download request to offline the current page made early during
// loading. Should execute two captures but only the final one is kept.
TEST_F(RecentTabHelperTest, DownloadRequestEarlyInLoad) {
  // Commit the navigation and request the snapshot from downloads. No captures
  // so far.
  NavigateAndCommit(kTestPageUrl);
  const ClientId client_id("download", "id1");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(client_id, 153L);
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  ASSERT_EQ(0U, GetAllPages().size());

  // Minimally load the page. First capture should occur.
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  ASSERT_EQ(1U, GetAllPages().size());
  const OfflinePageItem& early_page = GetAllPages()[0];
  EXPECT_EQ(kTestPageUrl, early_page.url);
  EXPECT_EQ(client_id, early_page.client_id);
  EXPECT_EQ(153L, early_page.offline_id);

  // Fully load the page. A second capture should replace the first one.
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
  const OfflinePageItem& later_page = GetAllPages()[0];
  EXPECT_EQ(kTestPageUrl, later_page.url);
  EXPECT_EQ(client_id, later_page.client_id);
  EXPECT_EQ(153L, later_page.offline_id);
}

// Simulates a download request to offline the current page made when the page
// is minimally loaded. Should execute two captures but only the final one is
// kept.
TEST_F(RecentTabHelperTest, DownloadRequestLaterInLoad) {
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  ASSERT_EQ(0U, GetAllPages().size());

  const ClientId client_id("download", "id1");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(client_id, 153L);
  RunUntilIdle();
  ASSERT_EQ(1U, GetAllPages().size());
  const OfflinePageItem& page = GetAllPages()[0];
  EXPECT_EQ(kTestPageUrl, page.url);
  EXPECT_EQ(client_id, page.client_id);
  EXPECT_EQ(153L, page.offline_id);

  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
}

// Simulates a download request to offline the current page made after loading
// is completed. Should end up with one offline pages.
TEST_F(RecentTabHelperTest, DownloadRequestAfterFullyLoad) {
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  RunUntilIdle();
  EXPECT_TRUE(model()->is_loaded());
  ASSERT_EQ(0U, GetAllPages().size());

  const ClientId client_id("download", "id1");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(client_id, 153L);
  RunUntilIdle();
  ASSERT_EQ(1U, GetAllPages().size());
  const OfflinePageItem& page = GetAllPages()[0];
  EXPECT_EQ(kTestPageUrl, page.url);
  EXPECT_EQ(client_id, page.client_id);
  EXPECT_EQ(153L, page.offline_id);
}

// Simulates requests coming from last_n and downloads at the same time for a
// fully loaded page.
TEST_F(RecentTabHelperTest, SimultaneousCapturesFromLastNAndDownloads) {
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  const int64_t download_offline_id = 153L;
  const ClientId download_client_id("download", "id1");
  recent_tab_helper()->ObserveAndDownloadCurrentPage(download_client_id,
                                                     download_offline_id);
  RunUntilIdle();
  ASSERT_EQ(2U, GetAllPages().size());

  const OfflinePageItem* downloads_page =
      FindPageForOfflineId(download_offline_id);
  ASSERT_TRUE(downloads_page);
  EXPECT_EQ(kTestPageUrl, downloads_page->url);
  EXPECT_EQ(download_client_id, downloads_page->client_id);

  const OfflinePageItem& last_n_page =
      GetAllPages()[0].offline_id != download_offline_id ? GetAllPages()[0]
                                                         : GetAllPages()[1];
  EXPECT_EQ(kTestPageUrl, last_n_page.url);
  EXPECT_EQ(kLastNNamespace, last_n_page.client_id.name_space);
}

// Simulates multiple tab hidden events -- triggers for last_n snapshots --
// happening at the same loading stages. The duplicate events should not cause
// new snapshots to be saved.
TEST_F(RecentTabHelperTest, DuplicateTabHiddenEventsShouldNotTriggerSnapshots) {
  NavigateAndCommit(kTestPageUrl);
  recent_tab_helper()->DocumentAvailableInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(1U, page_added_count());
  EXPECT_EQ(0U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  recent_tab_helper()->DocumentOnLoadCompletedInMainFrame();
  FastForwardSnapshotController();
  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());

  recent_tab_helper()->WasHidden();
  RunUntilIdle();
  EXPECT_EQ(2U, page_added_count());
  EXPECT_EQ(1U, model_removed_count());
  ASSERT_EQ(1U, GetAllPages().size());
}

}  // namespace offline_pages
