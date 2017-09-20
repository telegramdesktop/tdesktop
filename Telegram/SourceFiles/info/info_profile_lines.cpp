/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_profile_lines.h"

#include <rpl/filter.h>
#include <rpl/never.h>
#include <rpl/before_next.h>
#include <rpl/after_next.h>
#include <rpl/combine.h>
#include "styles/style_info.h"
#include "profile/profile_userpic_button.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "messenger.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "lang/lang_keys.h"

namespace Info {
namespace Profile {
namespace {

auto MembersStatusText(int count) {
	return lng_chat_status_members(lt_count, count);
};

auto OnlineStatusText(int count) {
	return lng_chat_status_online(lt_count, count);
};

auto ChatStatusText(int fullCount, int onlineCount, bool isGroup) {
	if (onlineCount > 0 && onlineCount <= fullCount) {
		return lng_chat_status_members_online(
			lt_members_count, MembersStatusText(fullCount),
			lt_online_count, OnlineStatusText(onlineCount));
	} else if (fullCount > 0) {
		return lng_chat_status_members(lt_count, fullCount);
	}
	return lang(isGroup
		? lng_group_status
		: lng_channel_status);
};

} // namespace

rpl::producer<Notify::PeerUpdate> PeerUpdateViewer(
		Notify::PeerUpdate::Flags flags) {
	return [=](const rpl::consumer<Notify::PeerUpdate> &consumer) {
		auto lifetime = rpl::lifetime();
		lifetime.make_state<base::Subscription>(
			Notify::PeerUpdated().add_subscription({ flags, [=](
					const Notify::PeerUpdate &update) {
				consumer.put_next_copy(update);
			}}));
		return lifetime;
	};
}

rpl::producer<Notify::PeerUpdate> PeerUpdateViewer(
		not_null<PeerData*> peer,
		Notify::PeerUpdate::Flags flags) {
	return PeerUpdateViewer(flags)
		| rpl::filter([=](const Notify::PeerUpdate &update) {
			return (update.peer == peer);
		});
}

rpl::producer<Notify::PeerUpdate> PeerUpdateValue(
		not_null<PeerData*> peer,
		Notify::PeerUpdate::Flags flags) {
	return rpl::single(Notify::PeerUpdate())
		| then(PeerUpdateViewer(peer, flags));
}

rpl::producer<TextWithEntities> PhoneViewer(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserPhoneChanged)
		| rpl::map([user](auto&&) {
			return App::formatPhone(user->phone());
		})
		| WithEmptyEntities();
}

rpl::producer<TextWithEntities> BioViewer(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::AboutChanged)
		| rpl::map([user](auto&&) { return user->about(); })
		| WithEmptyEntities();
}

rpl::producer<QString> PlainUsernameViewer(
		not_null<PeerData*> peer) {
	return PeerUpdateValue(
			peer,
			Notify::PeerUpdate::Flag::UsernameChanged)
		| rpl::map([peer](auto&&) {
			return peer->userName();
		});
}

rpl::producer<TextWithEntities> UsernameViewer(
		not_null<UserData*> user) {
	return PlainUsernameViewer(user)
		| rpl::map([](QString &&username) {
			return username.isEmpty()
				? QString()
				: ('@' + username);
		})
		| WithEmptyEntities();
}

rpl::producer<TextWithEntities> AboutViewer(
		not_null<PeerData*> peer) {
	if (auto channel = peer->asChannel()) {
		return PeerUpdateValue(
				channel,
				Notify::PeerUpdate::Flag::AboutChanged)
			| rpl::map([channel](auto&&) { return channel->about(); })
			| WithEmptyEntities();
	}
	return rpl::single(TextWithEntities{});
}

rpl::producer<TextWithEntities> LinkViewer(
		not_null<PeerData*> peer) {
	return PlainUsernameViewer(peer)
		| rpl::map([](QString &&username) {
			return username.isEmpty()
				? QString()
				: Messenger::Instance().createInternalLink(username);
		})
		| WithEmptyEntities();
}

rpl::producer<bool> NotificationsEnabledViewer(
		not_null<PeerData*> peer) {
	return PeerUpdateValue(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled)
		| rpl::map([peer](auto&&) { return !peer->isMuted(); });
}

rpl::producer<bool> IsContactViewer(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserIsContact)
		| rpl::map([user](auto&&) { return user->isContact(); });
}

rpl::producer<bool> CanShareContactViewer(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserCanShareContact)
		| rpl::map([user](auto&&) {
			return user->canShareThisContact();
		});
}

rpl::producer<bool> CanAddContactViewer(
		not_null<UserData*> user) {
	using namespace rpl::mappers;
	return rpl::combine(
			IsContactViewer(user),
			CanShareContactViewer(user),
			!$1 && $2);
}

FloatingIcon::FloatingIcon(
	QWidget *parent,
	not_null<RpWidget*> above,
	const style::icon &icon)
: FloatingIcon(parent, above, icon, st::infoIconPosition, Tag{}) {
}

FloatingIcon::FloatingIcon(
	QWidget *parent,
	not_null<RpWidget*> above,
	const style::icon &icon,
	QPoint position)
: FloatingIcon(parent, above, icon, position, Tag{}) {
}

FloatingIcon::FloatingIcon(
	QWidget *parent,
	const style::icon &icon)
: FloatingIcon(parent, nullptr, icon, st::infoIconPosition, Tag{}) {
}

FloatingIcon::FloatingIcon(
	QWidget *parent,
	const style::icon &icon,
	QPoint position)
: FloatingIcon(parent, nullptr, icon, position, Tag{}) {
}

FloatingIcon::FloatingIcon(
	QWidget *parent,
	RpWidget *above,
	const style::icon &icon,
	QPoint position,
	const Tag &)
: RpWidget(parent)
, _icon(&icon)
, _point(position) {
	resize(
		_point.x() + _icon->width(),
		_point.y() + _icon->height());
	setAttribute(Qt::WA_TransparentForMouseEvents);
	if (above) {
		above->geometryValue()
			| rpl::start([this](const QRect &geometry) {
				auto topLeft = rtlpoint(
					geometry.topLeft(),
					parentWidget()->width());
				moveToLeft(topLeft.x(), topLeft.y() + geometry.height());
			}, lifetime());
	} else {
		moveToLeft(0, 0);
	}
}

void FloatingIcon::paintEvent(QPaintEvent *e) {
	Painter p(this);
	_icon->paint(p, _point, width());
}

LabeledLine::LabeledLine(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&label,
	rpl::producer<TextWithEntities> &&text)
: LabeledLine(
	parent,
	std::move(label),
	std::move(text),
	st::infoLabeled,
	st::infoProfileLabeledPadding) {
}

LabeledLine::LabeledLine(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&label,
	rpl::producer<TextWithEntities> &&text,
	const style::FlatLabel &textSt,
	const style::margins &padding)
: SlideWrap<Ui::VerticalLayout>(
	parent,
	object_ptr<Ui::VerticalLayout>(parent),
	padding
) {
	auto layout = entity();
	auto nonEmptyText = std::move(text)
		| rpl::before_next([this](const TextWithEntities &value) {
			if (value.text.isEmpty()) {
				hideAnimated();
			}
		})
		| rpl::filter([this](const TextWithEntities &value) {
			return !value.text.isEmpty();
		})
		| rpl::after_next([this](const TextWithEntities &value) {
			showAnimated();
		});
	layout->add(object_ptr<Ui::FlatLabel>(
		this,
		std::move(nonEmptyText),
		textSt));
	layout->add(object_ptr<Ui::FlatLabel>(
		this,
		std::move(label),
		st::infoLabel));
	finishAnimations();
};

CoverLine::CoverLine(QWidget *parent, not_null<PeerData*> peer)
: RpWidget(parent)
, _peer(peer)
, _userpic(this, _peer, st::infoProfilePhotoSize)
, _name(this, st::infoProfileNameLabel)
, _status(this, st::infoProfileStatusLabel) {
	_peer->updateFull();

	_name->setSelectable(true);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);

	initViewers();
	initUserpicButton();
	refreshNameText();
	refreshStatusText();
}

void CoverLine::setOnlineCount(int onlineCount) {
	_onlineCount = onlineCount;
	refreshStatusText();
}

void CoverLine::setHasToggle(bool hasToggle) {
	if (hasToggle && !_toggle) {
		_toggle.create(this, QString());
	} else if (!hasToggle && _toggle) {
		_toggle.destroy();
	}
}

void CoverLine::initViewers() {
	using Flag = Notify::PeerUpdate::Flag;
	PeerUpdateViewer(_peer, Flag::PhotoChanged)
		| rpl::start(
			[this](auto&&) { this->refreshUserpicLink(); },
			_lifetime);
	PeerUpdateViewer(_peer, Flag::NameChanged)
		| rpl::start(
			[this](auto&&) { this->refreshNameText(); },
			_lifetime);
	PeerUpdateViewer(_peer,
		Flag::UserOnlineChanged | Flag::MembersChanged)
		| rpl::start(
			[this](auto&&) { this->refreshStatusText(); },
			_lifetime);
}

void CoverLine::initUserpicButton() {
	_userpic->setClickedCallback([this] {
		auto hasPhoto = (_peer->photoId != 0);
		auto knownPhoto = (_peer->photoId != UnknownPeerPhotoId);
		if (hasPhoto && knownPhoto) {
			if (auto photo = App::photo(_peer->photoId)) {
				if (photo->date) {
					Messenger::Instance().showPhoto(photo, _peer);
				}
			}
		}
	});
	refreshUserpicLink();
}

void CoverLine::refreshUserpicLink() {
	auto hasPhoto = (_peer->photoId != 0);
	auto knownPhoto = (_peer->photoId != UnknownPeerPhotoId);
	_userpic->setPointerCursor(hasPhoto && knownPhoto);
	if (!knownPhoto) {
		Auth().api().requestFullPeer(_peer);
	}
}

void CoverLine::refreshNameText() {
	_name->setText(App::peerName(_peer));
	refreshNameGeometry(width());
}

void CoverLine::refreshStatusText() {
	auto statusText = [this] {
		auto currentTime = unixtime();
		if (auto user = _peer->asUser()) {
			auto result = App::onlineText(user, currentTime, true);
			return App::onlineColorUse(user, currentTime)
				? textcmdLink(1, result)
				: result;
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return lang(lng_chat_status_unaccessible);
			}
			auto fullCount = qMax(
				chat->count,
				chat->participants.size());
			return ChatStatusText(fullCount, _onlineCount, true);
		} else if (auto channel = _peer->asChannel()) {
			auto fullCount = qMax(channel->membersCount(), 1);
			return ChatStatusText(
				fullCount,
				_onlineCount,
				channel->isMegagroup());
		}
		return lang(lng_chat_status_unaccessible);
	}();
	_status->setRichText(statusText);
	refreshStatusGeometry(width());
}

void CoverLine::refreshNameGeometry(int newWidth) {
	auto nameWidth = newWidth
		- st::infoProfileNameLeft
		- st::infoProfileNameRight;
	if (_toggle) {
		nameWidth -= _toggle->width() + st::infoProfileToggleRight;
	}
	_name->resizeToWidth(nameWidth);
	_name->moveToLeft(
		st::infoProfileNameLeft,
		st::infoProfileNameTop,
		newWidth);
}

void CoverLine::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth
		- st::infoProfileStatusLeft
		- st::infoProfileStatusRight;
	if (_toggle) {
		statusWidth -= _toggle->width() + st::infoProfileToggleRight;
	}
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(
		st::infoProfileStatusLeft,
		st::infoProfileStatusTop,
		newWidth);
}

int CoverLine::resizeGetHeight(int newWidth) {
	_userpic->moveToLeft(
		st::infoProfilePhotoLeft,
		st::infoProfilePhotoTop,
		newWidth);
	refreshNameGeometry(newWidth);
	refreshStatusGeometry(newWidth);
	if (_toggle) {
		_toggle->moveToRight(
			st::infoProfileToggleRight,
			st::infoProfileToggleTop,
			newWidth);
	}
	return st::infoProfilePhotoTop
		+ _userpic->height()
		+ st::infoProfilePhotoBottom;
}

rpl::producer<bool> CoverLine::toggled() const {
	return _toggle
		? base::ObservableViewer(_toggle->checkedChanged)
		: rpl::never<bool>();
}

Button::Button(
	QWidget *parent,
	rpl::producer<QString> &&text)
: Button(parent, std::move(text), st::infoProfileButton) {
}

Button::Button(
	QWidget *parent,
	rpl::producer<QString> &&text,
	const style::InfoProfileButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	std::move(text)
		| rpl::start([this](QString &&value) {
			setText(std::move(value));
		}, lifetime());
}

void Button::setToggled(bool toggled) {
	if (!_toggle) {
		_toggle = std::make_unique<Ui::ToggleView>(
			isOver() ? _st.toggleOver : _st.toggle,
			toggled,
			[this] { rtlupdate(toggleRect()); });
		clicks()
			| rpl::start([this](auto) {
				_toggle->setCheckedAnimated(!_toggle->checked());
			}, lifetime());
	} else {
		_toggle->setCheckedAnimated(toggled);
	}
}

rpl::producer<bool> Button::toggledValue() const {
	return _toggle ? _toggle->checkedValue() : rpl::never<bool>();
}

void Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto paintOver = (isOver() || isDown());
	p.fillRect(e->rect(), paintOver ? _st.textBgOver : _st.textBg);

	paintRipple(p, 0, 0, ms);

	auto outerw = width();
	p.setFont(_st.font);
	p.setPen(paintOver ? _st.textFgOver : _st.textFg);
	p.drawTextLeft(
		_st.padding.left(),
		_st.padding.top(),
		outerw,
		_text,
		_textWidth);

	if (_toggle) {
		auto rect = toggleRect();
		_toggle->paint(p, rect.left(), rect.top(), outerw, ms);
	}
}

QRect Button::toggleRect() const {
	Expects(_toggle != nullptr);
	auto size = _toggle->getSize();
	auto left = width() - _st.padding.right() - size.width();
	auto top = (height() - size.height()) / 2;
	return { QPoint(left, top), size };
}

int Button::resizeGetHeight(int newWidth) {
	updateVisibleText(newWidth);
	return _st.padding.top() + _st.height + _st.padding.bottom();
}

void Button::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (_toggle) {
		_toggle->setStyle(isOver() ? _st.toggleOver : _st.toggle);
	}
}

void Button::setText(QString &&text) {
	_original = std::move(text);
	_originalWidth = _st.font->width(_original);
	updateVisibleText(width());
}

void Button::updateVisibleText(int newWidth) {
	auto availableWidth = newWidth
		- _st.padding.left()
		- _st.padding.right();
	if (_toggle) {
		availableWidth -= _toggle->getSize().width()
			+ _st.padding.right();
	}
	accumulate_max(availableWidth, 0);
	if (availableWidth < _originalWidth) {
		_text = _st.font->elided(_original, availableWidth);
		_textWidth = _st.font->width(_text);
	} else {
		_text = _original;
		_textWidth = _originalWidth;
	}
	update();
}

} // namespace Profile
} // namespace Info
