/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/userpic_view.h"

class Painter;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
class ChatTheme;
class ChatStyle;
class PathShiftGradient;
} // namespace Ui

namespace Editor {

class MessageSource;

class MessageRenderer final {
public:
	explicit MessageRenderer(std::shared_ptr<MessageSource> source);
	~MessageRenderer();

	void setRepaintCallback(Fn<void()> callback);
	void setDark(std::optional<bool> dark);

	[[nodiscard]] bool ready() const;
	[[nodiscard]] QSize size();
	[[nodiscard]] QRect mediaRect();
	[[nodiscard]] QImage videoMask(int ratio);
	[[nodiscard]] QImage render(int ratio);

private:
	class Delegate;

	void createView();
	void layout();
	void repaint();
	[[nodiscard]] QImage renderFull(int ratio);
	[[nodiscard]] QRect elementMediaRect() const;
	void paintUserpic(Painter &p, int width, int height);

	rpl::lifetime _lifetime;
	const std::shared_ptr<MessageSource> _source;
	std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	const std::unique_ptr<Delegate> _delegate;
	std::unique_ptr<HistoryView::Element> _element;
	Fn<void()> _repaint;
	Ui::PeerUserpicView _userpic;
	QImage _base;
	QRect _bounds;
	std::optional<bool> _dark;
	int _width = 0;
	bool _layoutDirty = true;
	bool _recreate = false;

};

} // namespace Editor
