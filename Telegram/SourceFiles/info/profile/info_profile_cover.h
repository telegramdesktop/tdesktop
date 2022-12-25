/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/padding_wrap.h"
#include "ui/abstract_button.h"
#include "base/timer.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class UserpicButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info {
class Controller;
class Section;
} // namespace Info

namespace style {
struct InfoProfileCover;
} // namespace style

namespace Info::Profile {

class EmojiStatusPanel;
class Badge;

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

	void paintInRect(QPainter &p, QRect rect);

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
	QImage _image;
	rpl::lifetime _lifetime;

};

class TopicIconButton final : public Ui::AbstractButton {
public:
	TopicIconButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::ForumTopic*> topic);

private:
	TopicIconView _view;

};

class Cover final : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller);
	Cover(
		QWidget *parent,
		not_null<Data::ForumTopic*> topic,
		not_null<Window::SessionController*> controller);
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title);
	~Cover();

	Cover *setOnlineCount(rpl::producer<int> &&count);

	[[nodiscard]] rpl::producer<Section> showSection() const {
		return _showSection.events();
	}

private:
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title);

	void setupChildGeometry();
	void initViewers(rpl::producer<QString> title);
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);
	void refreshUploadPhotoOverlay();

	const style::InfoProfileCover &_st;

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const std::unique_ptr<EmojiStatusPanel> _emojiStatusPanel;
	const std::unique_ptr<Badge> _badge;
	int _onlineCount = 0;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<TopicIconButton> _iconButton;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };
	base::Timer _refreshStatusTimer;

	rpl::event_stream<Section> _showSection;

};

} // namespace Info::Profile
