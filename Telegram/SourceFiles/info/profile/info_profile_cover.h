/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/checkbox.h"
#include "base/timer.h"
#include "base/flags.h"

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct InfoToggle;
struct InfoPeerBadge;
} // namespace style

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class AbstractButton;
class UserpicButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
class Controller;
class Section;
} // namespace Info

namespace Info {
namespace Profile {

enum class Badge {
	None = 0x00,
	Verified = 0x01,
	Premium = 0x02,
	Scam = 0x04,
	Fake = 0x08,
};
inline constexpr bool is_flag_type(Badge) { return true; }

class BadgeView final {
public:
	BadgeView(
		not_null<QWidget*> parent,
		const style::InfoPeerBadge &st,
		not_null<PeerData*> peer,
		Fn<bool()> animationPaused,
		base::flags<Badge> allowed = base::flags<Badge>::from_raw(-1));

	[[nodiscard]] Ui::RpWidget *widget() const;

	void setPremiumClickCallback(Fn<void()> callback);
	[[nodiscrd]] rpl::producer<> updated() const;
	void move(int left, int top, int bottom);

private:
	void setBadge(Badge badge, DocumentId emojiStatusId);

	const not_null<QWidget*> _parent;
	const style::InfoPeerBadge &_st;
	const not_null<PeerData*> _peer;
	DocumentId _emojiStatusId = 0;
	std::unique_ptr<Ui::Text::CustomEmoji> _emojiStatus;
	base::flags<Badge> _allowed;
	Badge _badge = Badge();
	Fn<void()> _premiumClickCallback;
	Fn<bool()> _animationPaused;
	object_ptr<Ui::AbstractButton> _view = { nullptr };
	rpl::event_stream<> _updated;
	rpl::lifetime _lifetime;

};

class EmojiStatusPanel final {
public:
	void show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button);

private:
	void create(not_null<Window::SessionController*> controller);

	base::unique_qptr<ChatHelpers::TabbedPanel> _panel;

};

class Cover final : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller);
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title);

	Cover *setOnlineCount(rpl::producer<int> &&count);

	rpl::producer<Section> showSection() const {
		return _showSection.events();
	}

	~Cover();

private:
	void setupChildGeometry();
	void initViewers(rpl::producer<QString> title);
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);
	void refreshUploadPhotoOverlay();

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	BadgeView _badge;
	EmojiStatusPanel _emojiStatusPanel;
	int _onlineCount = 0;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };
	base::Timer _refreshStatusTimer;

	rpl::event_stream<Section> _showSection;

};

} // namespace Profile
} // namespace Info
