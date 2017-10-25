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
#include "info/profile/info_profile_cover.h"

#include <rpl/never.h>
#include <rpl/combine.h>
#include "data/data_photo.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "observer_peer.h"
#include "messenger.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "profile/profile_userpic_button.h"

namespace Info {
namespace Profile {
namespace {

class SectionToggle : public Ui::AbstractCheckView {
public:
	SectionToggle(
		const style::InfoToggle &st,
		bool checked,
		base::lambda<void()> updateCallback);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	QSize rippleSize() const;

	const style::InfoToggle &_st;

};

SectionToggle::SectionToggle(
		const style::InfoToggle &st,
		bool checked,
		base::lambda<void()> updateCallback)
: AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(st) {
}

QSize SectionToggle::getSize() const {
	return QSize(_st.size, _st.size);
}

void SectionToggle::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) {
	auto sqrt2 = sqrt(2.);
	auto vLeft = rtlpoint(left + _st.skip, 0, outerWidth).x() + 0.;
	auto vTop = top + _st.skip + 0.;
	auto vWidth = _st.size - 2 * _st.skip;
	auto vHeight = _st.size - 2 * _st.skip;
	auto vStroke = _st.stroke / sqrt2;
	constexpr auto kPointCount = 6;
	std::array<QPointF, kPointCount> pathV = { {
		{ vLeft, vTop + (vHeight / 4.) + vStroke },
		{ vLeft + vStroke, vTop + (vHeight / 4.) },
		{ vLeft + (vWidth / 2.), vTop + (vHeight * 3. / 4.) - vStroke },
		{ vLeft + vWidth - vStroke, vTop + (vHeight / 4.) },
		{ vLeft + vWidth, vTop + (vHeight / 4.) + vStroke },
		{ vLeft + (vWidth / 2.), vTop + (vHeight * 3. / 4.) + vStroke },
	} };

	auto toggled = currentAnimationValue(ms);
	auto alpha = (toggled - 1.) * M_PI_2;
	auto cosalpha = cos(alpha);
	auto sinalpha = sin(alpha);
	auto shiftx = vLeft + (vWidth / 2.);
	auto shifty = vTop + (vHeight / 2.);
	for (auto &point : pathV) {
		auto x = point.x() - shiftx;
		auto y = point.y() - shifty;
		point.setX(shiftx + x * cosalpha - y * sinalpha);
		point.setY(shifty + y * cosalpha + x * sinalpha);
	}
	QPainterPath path;
	path.moveTo(pathV[0]);
	for (int i = 1; i != kPointCount; ++i) {
		path.lineTo(pathV[i]);
	}
	path.lineTo(pathV[0]);

	PainterHighQualityEnabler hq(p);
	p.fillPath(path, _st.color);
}

QImage SectionToggle::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(rippleSize());
}

QSize SectionToggle::rippleSize() const {
	return getSize() + 2 * QSize(
		_st.rippleAreaPadding,
		_st.rippleAreaPadding);
}

bool SectionToggle::checkRippleStartPosition(QPoint position) const {
	return QRect(QPoint(0, 0), rippleSize()).contains(position);

}

auto MembersStatusText(int count) {
	return lng_chat_status_members(lt_count, count);
};

auto OnlineStatusText(int count) {
	return lng_chat_status_online(lt_count, count);
};

auto ChatStatusText(int fullCount, int onlineCount, bool isGroup) {
	if (onlineCount > 1 && onlineCount <= fullCount) {
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

SectionWithToggle *SectionWithToggle::setToggleShown(
		rpl::producer<bool> &&shown) {
	_toggle.create(
		this,
		QString(),
		st::infoToggleCheckbox,
		std::make_unique<SectionToggle>(
			st::infoToggle,
			false,
			[this] { _toggle->updateCheck(); }));
	_toggle->hide();
	_toggle->lower();
	_toggle->setCheckAlignment(style::al_right);
	widthValue()
		| rpl::start_with_next([this](int newValue) {
			_toggle->setGeometry(0, 0, newValue, height());
		}, _toggle->lifetime());
	std::move(shown)
		| rpl::start_with_next([this](bool shown) {
			if (_toggle->isHidden() == shown) {
				_toggle->setVisible(shown);
				_toggleShown.fire_copy(shown);
			}
		}, lifetime());
	return this;
}

void SectionWithToggle::toggle(bool toggled) {
	if (_toggle) {
		_toggle->setChecked(toggled);
	}
}

bool SectionWithToggle::toggled() const {
	return _toggle ? _toggle->checked() : false;
}

rpl::producer<bool> SectionWithToggle::toggledValue() const {
	if (_toggle) {
		return rpl::single(_toggle->checked())
			| rpl::then(
				base::ObservableViewer(_toggle->checkedChanged));
	}
	return rpl::never<bool>();
}

rpl::producer<bool> SectionWithToggle::toggleShownValue() const {
	return _toggleShown.events_starting_with(
		_toggle && !_toggle->isHidden());
}

int SectionWithToggle::toggleSkip() const {
	return (!_toggle || _toggle->isHidden())
		? 0
		: st::infoToggleCheckbox.checkPosition.x()
			+ _toggle->checkRect().width();
}

Cover::Cover(QWidget *parent, not_null<PeerData*> peer)
: SectionWithToggle(
	parent,
	st::infoProfilePhotoTop
		+ st::infoProfilePhotoSize
		+ st::infoProfilePhotoBottom)
, _peer(peer)
, _userpic(this, _peer, st::infoProfilePhotoSize)
, _name(this, st::infoProfileNameLabel)
, _status(this, st::infoProfileStatusLabel) {
	_peer->updateFull();

	_name->setSelectable(true);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);

	initUserpicButton();
	initViewers();
	setupChildGeometry();
}

void Cover::setupChildGeometry() {
	using namespace rpl::mappers;
	rpl::combine(
		toggleShownValue(),
		widthValue(),
		$2)
		| rpl::start_with_next([this](int newWidth) {
			_userpic->moveToLeft(
				st::infoProfilePhotoLeft,
				st::infoProfilePhotoTop,
				newWidth);
			refreshNameGeometry(newWidth);
			refreshStatusGeometry(newWidth);
		}, lifetime());
}

Cover *Cover::setOnlineCount(rpl::producer<int> &&count) {
	std::move(count)
		| rpl::start_with_next([this](int count) {
			_onlineCount = count;
			refreshStatusText();
		}, lifetime());
	return this;
}

void Cover::initViewers() {
	using Flag = Notify::PeerUpdate::Flag;
	Notify::PeerUpdateValue(_peer, Flag::PhotoChanged)
		| rpl::start_with_next(
			[this] { this->refreshUserpicLink(); },
			lifetime());
	Notify::PeerUpdateValue(_peer, Flag::NameChanged)
		| rpl::start_with_next(
			[this] { this->refreshNameText(); },
			lifetime());
	Notify::PeerUpdateValue(_peer,
		Flag::UserOnlineChanged | Flag::MembersChanged)
		| rpl::start_with_next(
			[this] { this->refreshStatusText(); },
			lifetime());
}

void Cover::initUserpicButton() {
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
}

void Cover::refreshUserpicLink() {
	auto hasPhoto = (_peer->photoId != 0);
	auto knownPhoto = (_peer->photoId != UnknownPeerPhotoId);
	_userpic->setPointerCursor(hasPhoto && knownPhoto);
	if (!knownPhoto) {
		Auth().api().requestFullPeer(_peer);
	}
}

void Cover::refreshNameText() {
	_name->setText(App::peerName(_peer));
	refreshNameGeometry(width());
}

void Cover::refreshStatusText() {
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

Cover::~Cover() {
}

void Cover::refreshNameGeometry(int newWidth) {
	auto nameWidth = newWidth
		- st::infoProfileNameLeft
		- st::infoProfileNameRight
		- toggleSkip();
	_name->resizeToWidth(nameWidth);
	_name->moveToLeft(
		st::infoProfileNameLeft,
		st::infoProfileNameTop,
		newWidth);
}

void Cover::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth
		- st::infoProfileStatusLeft
		- st::infoProfileStatusRight
		- toggleSkip();
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(
		st::infoProfileStatusLeft,
		st::infoProfileStatusTop,
		newWidth);
}

QMargins SharedMediaCover::getMargins() const {
	return QMargins(0, 0, 0, st::infoSharedMediaBottomSkip);
}

SharedMediaCover::SharedMediaCover(QWidget *parent)
: SectionWithToggle(parent, st::infoSharedMediaCoverHeight) {
	createLabel();
}

void SharedMediaCover::createLabel() {
	using namespace rpl::mappers;
	auto label = object_ptr<Ui::FlatLabel>(
		this,
		Lang::Viewer(lng_profile_shared_media) | ToUpperValue(),
		st::infoBlockHeaderLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		toggleShownValue(),
		widthValue(),
		$2)
		| rpl::start_with_next([this, weak = label.data()](int newWidth) {
			auto availableWidth = newWidth
				- st::infoBlockHeaderPosition.x()
				- st::infoSharedMediaButton.padding.right()
				- toggleSkip();
			weak->resizeToWidth(availableWidth);
			weak->moveToLeft(
				st::infoBlockHeaderPosition.x(),
				st::infoBlockHeaderPosition.y(),
				newWidth);
		}, label->lifetime());
}

} // namespace Profile
} // namespace Info
