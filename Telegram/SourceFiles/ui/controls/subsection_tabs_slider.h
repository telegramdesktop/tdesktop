/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"

namespace style {
struct ChatTabsVertical;
struct ChatTabsOutline;
} // namespace style

namespace Ui {

class DynamicImage;
class RippleAnimation;
class SubsectionButton;
struct ScrollToRequest;

struct SubsectionTab {
	TextWithEntities text;
	std::shared_ptr<DynamicImage> userpic;
	Dialogs::BadgesState badges;
};

struct SubsectionTabs {
	std::vector<SubsectionTab> tabs;
	Text::MarkedContext context;
	int fixed = 0;
	int pinned = 0;
	bool reorder = false;
};

class SubsectionButtonDelegate {
public:
	virtual bool buttonPaused() = 0;
	virtual float64 buttonActive(not_null<SubsectionButton*> button) = 0;
	virtual Text::MarkedContext buttonContext() = 0;
	virtual void buttonContextMenu(
		not_null<SubsectionButton*> button,
		not_null<QContextMenuEvent*> e) = 0;
};

class SubsectionButton : public RippleButton {
public:
	SubsectionButton(
		not_null<QWidget*> parent,
		not_null<SubsectionButtonDelegate*> delegate,
		SubsectionTab &&data);
	~SubsectionButton();

	void setData(SubsectionTab &&data);
	[[nodiscard]] DynamicImage *userpic() const;

	void setActiveShown(float64 activeShown);
	void setIsPinned(bool pinned);
	[[nodiscard]] bool isPinned() const;
	void setPinnedPosition(bool isFirst, bool isLast);
	[[nodiscard]] bool isFirstPinned() const;
	[[nodiscard]] bool isLastPinned() const;
	virtual void setBackgroundMargin(int margin);
	void setShift(int shift);

protected:
	virtual void dataUpdatedHook() = 0;
	virtual void invalidateCache() = 0;

	void contextMenuEvent(QContextMenuEvent *e) override;

	const not_null<SubsectionButtonDelegate*> _delegate;
	SubsectionTab _data;
	float64 _activeShown = 0.;
	bool _isPinned = false;
	bool _isFirstPinned = false;
	bool _isLastPinned = false;
	int _backgroundMargin = 0;
	int _shift = 0;

};

class SubsectionSlider
	: public RpWidget
	, public SubsectionButtonDelegate {
public:
	~SubsectionSlider();

	void setSections(
		SubsectionTabs sections,
		Fn<bool()> paused);
	void setActiveSectionFast(int active, bool ignoreScroll = false);

	[[nodiscard]] int sectionsCount() const;
	[[nodiscard]] rpl::producer<int> sectionActivated() const;
	[[nodiscard]] rpl::producer<int> sectionContextMenu() const;
	[[nodiscard]] int lookupSectionPosition(int index) const;

	bool buttonPaused() override;
	float64 buttonActive(not_null<SubsectionButton*> button) override;
	void buttonContextMenu(
		not_null<SubsectionButton*> button,
		not_null<QContextMenuEvent*> e) override;
	Text::MarkedContext buttonContext() override;
	[[nodiscard]] not_null<SubsectionButton*> buttonAt(int index);
	void setButtonShift(int index, int shift);
	void reorderButtons(int from, int to);
	void recalculatePinnedPositions();
	void recalculatePinnedPositionsByUI();

	[[nodiscard]] rpl::producer<ScrollToRequest> requestShown() const;

	void setIsReorderingCallback(Fn<bool()> callback);

	[[nodiscard]] bool isVertical() const {
		return _vertical;
	}

protected:
	struct Range {
		int from = 0;
		int size = 0;
	};

	SubsectionSlider(not_null<QWidget*> parent, bool vertical);
	void setupBar();

	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] int lookupSectionIndex(QPoint position) const;
	[[nodiscard]] Range getFinalActiveRange() const;
	[[nodiscard]] Range getCurrentActiveRange() const;
	void activate(int index);

	[[nodiscard]] virtual std::unique_ptr<SubsectionButton> makeButton(
		SubsectionTab &&data) = 0;

	const bool _vertical = false;

	const style::ChatTabsOutline &_barSt;
	RpWidget *_bar = nullptr;
	RoundRect _barRect;

	std::vector<std::unique_ptr<SubsectionButton>> _tabs;
	bool _tabsReorderedOnce = false;
	int _active = -1;
	int _pressed = -1;
	Animations::Simple _activeFrom;
	Animations::Simple _activeSize;

	//int _buttonIndexHint = 0;

	Text::MarkedContext _context;
	int _fixedCount = 0;
	int _pinnedCount = 0;
	bool _reorderAllowed = false;

	rpl::event_stream<int> _sectionActivated;
	rpl::event_stream<int> _sectionContextMenu;
	Fn<bool()> _paused;

	rpl::event_stream<ScrollToRequest> _requestShown;

	Fn<bool()> _isReorderingCallback;

};

class VerticalSlider final : public SubsectionSlider {
public:
	explicit VerticalSlider(not_null<QWidget*> parent);
	~VerticalSlider();

private:
	std::unique_ptr<SubsectionButton> makeButton(
		SubsectionTab &&data) override;

};

class HorizontalSlider final : public SubsectionSlider {
public:
	explicit HorizontalSlider(not_null<QWidget*> parent);
	~HorizontalSlider();

private:
	std::unique_ptr<SubsectionButton> makeButton(
		SubsectionTab &&data) override;

	const style::SettingsSlider &_st;

};

[[nodiscard]] std::shared_ptr<DynamicImage> MakeAllSubsectionsThumbnail(
	Fn<QColor()> textColor);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeNewChatSubsectionsThumbnail(
	Fn<QColor()> textColor);

} // namespace Ui
