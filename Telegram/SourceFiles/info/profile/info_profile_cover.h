/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/profile/info_profile_badge.h"
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
class RoundButton;
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

class Cover final : public Ui::FixedHeightWidget {
public:
	enum class Role {
		Info,
		EditContact,
	};

	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Fn<not_null<QWidget*>()> parentForTooltip = nullptr);
	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::ForumTopic*> topic);
	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Role role,
		rpl::producer<QString> title);
	~Cover();

	Cover *setOnlineCount(rpl::producer<int> &&count);

	[[nodiscard]] rpl::producer<Section> showSection() const {
		return _showSection.events();
	}
	[[nodiscard]] std::optional<QImage> updatedPersonalPhoto() const;

private:
	class BadgeTooltip;

	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Role role,
		rpl::producer<QString> title,
		Fn<not_null<QWidget*>()> parentForTooltip);

	void setupShowLastSeen();
	void setupChildGeometry();
	void initViewers(rpl::producer<QString> title);
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);
	void refreshUploadPhotoOverlay();
	void setupUniqueBadgeTooltip();
	void setupChangePersonal();
	void hideBadgeTooltip();

	const style::InfoProfileCover &_st;

	const Role _role = Role::Info;
	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const std::unique_ptr<EmojiStatusPanel> _emojiStatusPanel;
	const std::unique_ptr<Badge> _botVerify;
	rpl::variable<Badge::Content> _badgeContent;
	const std::unique_ptr<Badge> _badge;
	const std::unique_ptr<Badge> _verified;
	rpl::variable<int> _onlineCount;

	const Fn<not_null<QWidget*>()> _parentForTooltip;
	std::unique_ptr<BadgeTooltip> _badgeTooltip;
	std::vector<std::unique_ptr<BadgeTooltip>> _badgeOldTooltips;
	base::Timer _badgeTooltipHide;
	uint64 _badgeCollectibleId = 0;

	const object_ptr<Ui::UserpicButton> _userpic;
	Ui::UserpicButton *_changePersonal = nullptr;
	std::optional<QImage> _personalChosen;
	object_ptr<TopicIconButton> _iconButton;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	object_ptr<Ui::RoundButton> _showLastSeen = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };
	base::Timer _refreshStatusTimer;

	rpl::event_stream<Section> _showSection;

};

} // namespace Info::Profile
