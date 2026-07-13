/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/emoji_config.h"
#include "ui/rp_widget.h"
#include "ui/text/text_custom_emoji.h"

#include <memory>
#include <optional>
#include <vector>

namespace Ui {

class RpWidget;
class ScrollArea;
struct ScrollToRequest;

struct LabeledEmojiTab {
	QString id;
	QString label;
	EmojiPtr emoji = nullptr;
	QString customEmojiData;
	const style::icon *icon = nullptr;
	const style::icon *iconActive = nullptr;
};

class LabeledEmojiScrollTabs;

class LabeledEmojiTabs final : public RpWidget {
	friend class LabeledEmojiScrollTabs;

public:
	LabeledEmojiTabs(
		QWidget *parent,
		std::vector<LabeledEmojiTab> descriptors,
		Text::CustomEmojiFactory factory);

	void setChangedCallback(Fn<void(int)> callback);
	void setContextMenuCallback(Fn<void(int, QPoint)> callback);
	void setActive(int index);
	void resizeForOuterWidth(int outerWidth);
	[[nodiscard]] QString currentId() const;
	[[nodiscard]] int buttonCount() const;
	[[nodiscard]] rpl::producer<ScrollToRequest> requestShown() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	class Button;

	std::vector<not_null<Button*>> _buttons;
	Fn<void(int)> _changed;
	Fn<void(int, QPoint)> _contextMenu;
	int _active = -1;
	rpl::event_stream<ScrollToRequest> _requestShown;

};

class LabeledEmojiScrollTabs final : public RpWidget {
public:
	LabeledEmojiScrollTabs(
		QWidget *parent,
		std::vector<LabeledEmojiTab> descriptors,
		Text::CustomEmojiFactory factory);
	~LabeledEmojiScrollTabs();

	void setChangedCallback(Fn<void(int)> callback);
	void setContextMenuCallback(Fn<void(int, QPoint)> callback);
	void setActive(int index);
	void setPaintOuterCorners(bool paint);
	void scrollToActive();
	[[nodiscard]] int scrollLeft() const;
	void setScrollLeft(int value);
	[[nodiscard]] QString currentId() const;
	[[nodiscard]] int buttonCount() const;
	[[nodiscard]] rpl::producer<ScrollToRequest> requestShown() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	class DragScroll;

	void updateFades();
	void scrollToButton(int buttonLeft, int buttonRight, bool animated);

	const not_null<ScrollArea*> _scroll;
	const not_null<LabeledEmojiTabs*> _inner;
	const not_null<RpWidget*> _fadeLeft;
	const not_null<RpWidget*> _fadeRight;
	const not_null<RpWidget*> _cornerLeft;
	const not_null<RpWidget*> _cornerRight;
	Animations::Simple _scrollAnimation;
	std::unique_ptr<DragScroll> _dragScroll;
	bool _paintOuterCorners = true;
	bool _scrollToActivePending = false;
	std::optional<int> _pendingScrollLeft;

};

} // namespace Ui
