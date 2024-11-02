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

	[[nodiscard]] rpl::producer<int> contextMenuRequested() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	[[nodiscard]] QImage cacheUnreadCount(int count) const;

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
	std::optional<Ui::RoundRect> _bar;
	std::optional<Ui::RoundRect> _barActive;

	rpl::event_stream<int> _contextMenuRequested;

};

} // namespace Ui
