/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/flat_map.h"
#include "info/profile/tabs/info_profile_tab_content.h"
#include "ui/rp_widget.h"

namespace Ui {
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
	[[nodiscard]] rpl::producer<TabTopBarBindings> activeTabBindings() const;
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	[[nodiscard]] not_null<Ui::RpWidget*> stripWidget() const;
	void returnStrip();
	void setVisibleRegion(int top, int bottom);

	[[nodiscard]] QString activeId() const {
		return _activeId;
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
	void syncStripTitles();
	void ensureActiveVisible();
	void pushViewportToActive();
	void scheduleBodySync();
	void scheduleHeightSync();
	void syncBodyNow();
	void syncHeightNow();
	[[nodiscard]] QRect bodyVisibleRect() const;
	void startSlideAnimation(
		QPixmap wasCache,
		not_null<MediaTabContent*> now,
		bool slideLeft);

	const MediaTabContext _context;
	std::vector<MediaTabDescriptor> _tabs;
	std::vector<QString> _stripTitles;
	std::vector<bool> _tabsShown;

	TabsStrip *_strip = nullptr;
	base::weak_qptr<TabsStrip> _stripWeak;
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
	int _keepMinHeight = 0;
	rpl::variable<MediaTabContent*> _activeTab = nullptr;

};

} // namespace Info::Profile
