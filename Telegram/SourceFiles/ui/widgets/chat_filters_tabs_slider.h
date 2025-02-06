/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/unread_badge_paint.h"
#include "ui/widgets/discrete_sliders.h"

namespace style {
struct SettingsSlider;
} // namespace style

namespace Ui {

class RpWidget;
class SettingsSlider;

class ChatsFiltersTabsReorder;

class ChatsFiltersTabs final : public Ui::SettingsSlider {
public:
	ChatsFiltersTabs(
		not_null<Ui::RpWidget*> parent,
		const style::SettingsSlider &st);

	bool setSectionsAndCheckChanged(
		std::vector<TextWithEntities> &&sections,
		const std::any &context,
		Fn<bool()> paused);

	void fitWidthToSections() override;
	void setUnreadCount(int index, int unreadCount, bool muted);
	void setLockedFrom(int index);

	[[nodiscard]] rpl::producer<int> contextMenuRequested() const;
	[[nodiscard]] rpl::producer<> lockedClicked() const;

	void setHorizontalShift(int index, int shift);
	void setRaised(int index);
	[[nodiscard]] int count() const;
	void reorderSections(int oldIndex, int newIndex);
	[[nodiscard]] not_null<DiscreteSlider::Section*> widgetAt(int i) const;
	void setReordering(int value);
	[[nodiscard]] int reordering() const;

	void stopAnimation();

protected:
	struct ShiftedSection {
		not_null<Ui::DiscreteSlider::Section*> section;
		int horizontalShift = 0;
		bool raise = false;
	};
	friend class ChatsFiltersTabsReorder;

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	std::vector<ShiftedSection> _sections;

private:
	[[nodiscard]] QImage cacheUnreadCount(int count, bool muted) const;
	[[nodiscard]] int calculateLockedFromX() const;

	using Index = int;
	struct Unread final {
		QImage cache;
		ushort count = 0;
		bool muted = false;
	};
	base::flat_map<Index, Unread> _unreadCounts;
	const style::SettingsSlider &_st;
	const UnreadBadgeStyle _unreadSt;
	const QString _unreadMaxString;
	const int _unreadSkip;
	std::vector<int> _cachedBadgeWidths;
	int _cachedBadgeHeight = 0;
	int _lockedFrom = 0;
	int _lockedFromX = 0;
	bool _lockedPressed = false;
	std::optional<Ui::RoundRect> _bar;
	std::optional<Ui::RoundRect> _barActive;
	std::optional<QImage> _lockCache;
	Fn<bool()> _emojiPaused;

	int _reordering = 0;

	rpl::lifetime _paletteLifetime;
	rpl::event_stream<int> _contextMenuRequested;
	rpl::event_stream<> _lockedClicked;

};

} // namespace Ui
