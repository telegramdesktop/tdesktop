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

class ChatsFiltersTabs final : public Ui::SettingsSlider {
public:
	ChatsFiltersTabs(
		not_null<Ui::RpWidget*> parent,
		const style::SettingsSlider &st);

	[[nodiscard]] int centerOfSection(int section) const;
	void fitWidthToSections();
	void setUnreadCount(int index, int unreadCount);
	void setLockedFrom(int index);

	[[nodiscard]] rpl::producer<int> contextMenuRequested() const;
	[[nodiscard]] rpl::producer<> lockedClicked() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	[[nodiscard]] QImage cacheUnreadCount(int count) const;
	[[nodiscard]] int calculateLockedFromX() const;

	using Index = int;
	struct Unread final {
		QImage cache;
		int count = 0;
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

	rpl::lifetime _paletteLifetime;
	rpl::event_stream<int> _contextMenuRequested;
	rpl::event_stream<> _lockedClicked;

};

} // namespace Ui
