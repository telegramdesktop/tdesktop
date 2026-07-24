/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/flat_map.h"
#include "base/unique_qptr.h"
#include "info/profile/tabs/info_profile_tab_content.h"
#include "ui/rp_widget.h"

namespace Ui {
class PopupMenu;
class RpWidget;
class SlideAnimation;
struct ScrollToRequest;
} // namespace Ui

namespace Info::Profile {

extern const char kOptionProfileMediaTabs[];

[[nodiscard]] bool UseProfileMediaTabs();

class TabsStrip;

class TabsHost final : public Ui::RpWidget {
public:
	struct Descriptor {
		MediaTabContext context;
		std::vector<MediaTabDescriptor> tabs;
	};

	TabsHost(not_null<QWidget*> parent, Descriptor descriptor);
	~TabsHost();

	[[nodiscard]] rpl::producer<MediaTabContent*> activeTabValue() const;
	[[nodiscard]] rpl::producer<TabTopBarBindings> activeTabBindings();
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	[[nodiscard]] not_null<Ui::RpWidget*> stripWidget() const;
	void returnStrip();
	void trackVerticalScroll(rpl::producer<> scrolls);
	void setVisibleRegion(int top, int bottom);
	void setScrolledToTop(bool scrolledToTop);

	[[nodiscard]] QString activeId() const {
		return _activeId;
	}
	[[nodiscard]] bool searching() const {
		return _searching;
	}
	[[nodiscard]] bool searchContentFits() const {
		return _searchContentFits;
	}
	void activateTab(const QString &id, bool animated = true);
	void restoreActiveTab(const QString &id);

	[[nodiscard]] QRect bodyGeometry() const;
	[[nodiscard]] Fn<void()> prepareSwitch(bool toNextTab);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void wireStripTitles();
	void wireTabsVisibility();
	void wireMainTab();
	bool refreshOrder();
	[[nodiscard]] int displayPosition(int index) const;
	[[nodiscard]] int firstVisibleIndex() const;
	[[nodiscard]] bool canSetMainTab(Data::ProfileTab tab) const;
	[[nodiscard]] Fn<void()> openInWindowFor(
		const MediaTabDescriptor &tab) const;
	void showTabMenu(const QString &id);
	void setMainTab(Data::ProfileTab tab);
	[[nodiscard]] QString mediaTabShownId(Storage::SharedMediaType type) const;
	[[nodiscard]] auto activeMediaType() const
		-> std::optional<Storage::SharedMediaType>;
	[[nodiscard]] QString mediaSplitCounterpart() const;
	[[nodiscard]] bool mediaSplitSwitching() const;
	void syncStripTitles();
	void ensureActiveVisible();
	void pushViewportToActive();
	void scheduleBodySync();
	void scheduleHeightSync();
	void syncBodyNow();
	void syncHeightNow();
	void scrollToBodyTop();
	[[nodiscard]] QRect bodyVisibleRect() const;
	void startSlideAnimation(
		QPixmap wasCache,
		not_null<MediaTabContent*> now,
		bool slideLeft);

	const MediaTabContext _context;
	std::vector<MediaTabDescriptor> _tabs;
	std::vector<TextWithEntities> _stripTitles;
	std::vector<bool> _tabsShown;
	std::vector<int> _order;
	int _mainTabIndex = -1;

	TabsStrip *_strip = nullptr;
	base::weak_qptr<TabsStrip> _stripWeak;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::RpWidget *_body = nullptr;
	int _stripHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
	QRect _slideRect;

	base::flat_map<QString, std::unique_ptr<MediaTabContent>> _contents;
	base::flat_map<QString, int> _tabScrollTops;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	QString _activeId;
	QString _pendingRestoreId;
	bool _userChosenTab = false;
	bool _bodySyncQueued = false;
	bool _heightSyncQueued = false;
	bool _viewportPushPending = false;
	bool _scrolledToTop = true;
	bool _searching = false;
	bool _searchContentFits = false;
	int _keepMinHeight = 0;
	rpl::variable<MediaTabContent*> _activeTab = nullptr;

};

} // namespace Info::Profile
