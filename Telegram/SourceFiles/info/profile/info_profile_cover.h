/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info::Profile {

[[nodiscard]] QMargins LargeCustomEmojiMargins();

class TopicIconView final {
public:
	TopicIconView(
		not_null<Data::ForumTopic*> topic,
		Fn<bool()> paused,
		Fn<void()> update);
	TopicIconView(
		not_null<Data::ForumTopic*> topic,
		Fn<bool()> paused,
		Fn<void()> update,
		const style::color &generalIconFg);

	void paintInRect(
		QPainter &p,
		QRect rect,
		QColor textColor = QColor(0, 0, 0, 0));

private:
	using StickerPlayer = HistoryView::StickerPlayer;

	void setup(not_null<Data::ForumTopic*> topic);
	void setupPlayer(not_null<Data::ForumTopic*> topic);
	void setupImage(not_null<Data::ForumTopic*> topic);

	const not_null<Data::ForumTopic*> _topic;
	const style::color &_generalIconFg;
	Fn<bool()> _paused;
	Fn<void()> _update;
	std::shared_ptr<StickerPlayer> _player;
	bool _playerUsesTextColor = false;
	QImage _image;
	rpl::lifetime _lifetime;

};

class TopicIconButton final : public Ui::AbstractButton {
public:
	TopicIconButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::ForumTopic*> topic);
	TopicIconButton(
		QWidget *parent,
		not_null<Data::ForumTopic*> topic,
		Fn<bool()> paused);

private:
	TopicIconView _view;

};

} // namespace Info::Profile
