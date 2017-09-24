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
#include "history/history_shared_media.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "messenger.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/effects/ripple_animation.h"
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

rpl::producer<int> MembersCountViewer(
		not_null<PeerData*> peer) {
	if (auto chat = peer->asChat()) {
		return PeerUpdateValue(
				peer,
				Notify::PeerUpdate::Flag::MembersChanged)
			| rpl::map([chat](auto&&) {
				return chat->amIn()
					? qMax(chat->count, chat->participants.size())
					: 0;
			});
	} else if (auto channel = peer->asChannel()) {
		return PeerUpdateValue(
				peer,
				Notify::PeerUpdate::Flag::MembersChanged)
			| rpl::map([channel](auto &&) {
				auto canViewCount = channel->canViewMembers()
					|| !channel->isMegagroup();
				return canViewCount
					? qMax(channel->membersCount(), 1)
					: 0;
			});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> SharedMediaCountViewer(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type) {
	auto initial = peer->migrateFrom() ? peer->migrateFrom() : peer;
	auto migrated = initial->migrateTo();
	auto aroundId = 0;
	auto limit = 0;
	return SharedMediaMergedViewer(
		SharedMediaMergedSlice::Key(
			peer->id,
			migrated ? migrated->id : 0,
			type,
			aroundId),
		limit,
		limit)
		| rpl::map([](const SharedMediaMergedSlice &slice) {
			return slice.fullCount();
		})
		| rpl::filter_optional();
}

rpl::producer<int> CommonGroupsCountViewer(
		not_null<UserData*> user) {
	return PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserCommonChatsChanged)
		| rpl::map([user](auto&&) {
			return user->commonChatsCount();
		});
}

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position)
: FloatingIcon(parent, icon, position, Tag{}) {
}

FloatingIcon::FloatingIcon(
	RpWidget *parent,
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
	parent->widthValue()
		| rpl::start(
			[this](auto&&) { moveToLeft(0, 0); },
			lifetime());
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
	st::infoLabeledOneLine,
	st::infoProfileLabeledPadding,
	true) {
}

LabeledLine::LabeledLine(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&label,
	rpl::producer<TextWithEntities> &&text,
	const style::FlatLabel &textSt,
	const style::margins &padding,
	bool doubleClickSelects)
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
	auto labeled = layout->add(object_ptr<Ui::FlatLabel>(
		this,
		std::move(nonEmptyText),
		textSt));
	labeled->setSelectable(true);
	labeled->setDoubleClickSelectsParagraph(doubleClickSelects);
	layout->add(Ui::CreateSkipWidget(this, st::infoLabelSkip));
	layout->add(object_ptr<Ui::FlatLabel>(
		this,
		std::move(label),
		st::infoLabel));
	finishAnimations();
};

Cover::Cover(QWidget *parent, not_null<PeerData*> peer)
: FixedHeightWidget(
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

	initViewers();
	initUserpicButton();
	refreshNameText();
	refreshStatusText();
	setupChildGeometry();
}

void Cover::setupChildGeometry() {
	widthValue()
		| rpl::start([this](int newWidth) {
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
		| rpl::start([this](int count) {
			_onlineCount = count;
			refreshStatusText();
		}, lifetime());
	return this;
}

Cover *Cover::setToggleShown(rpl::producer<bool> &&shown) {
	_toggle.create(
		this,
		QString(),
		st::infoToggleCheckbox,
		std::make_unique<SectionToggle>(
			st::infoToggle,
			false,
			[this] { _toggle->updateCheck(); }));
	_toggle->lower();
	_toggle->setCheckAlignment(style::al_right);
	widthValue()
		| rpl::start([this](int newValue) {
			_toggle->setGeometry(0, 0, newValue, height());
		}, _toggle->lifetime());
	std::move(shown)
		| rpl::start([this](bool shown) {
			if (_toggle->isHidden() == shown) {
				_toggle->setVisible(shown);
			}
		}, lifetime());
	return this;
}

void Cover::initViewers() {
	using Flag = Notify::PeerUpdate::Flag;
	PeerUpdateViewer(_peer, Flag::PhotoChanged)
		| rpl::start(
			[this](auto&&) { this->refreshUserpicLink(); },
			lifetime());
	PeerUpdateViewer(_peer, Flag::NameChanged)
		| rpl::start(
			[this](auto&&) { this->refreshNameText(); },
			lifetime());
	PeerUpdateViewer(_peer,
		Flag::UserOnlineChanged | Flag::MembersChanged)
		| rpl::start(
			[this](auto&&) { this->refreshStatusText(); },
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
	refreshUserpicLink();
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

void Cover::refreshNameGeometry(int newWidth) {
	auto nameWidth = newWidth
		- st::infoProfileNameLeft
		- st::infoProfileNameRight;
	if (_toggle) {
		nameWidth -= st::infoToggleCheckbox.checkPosition.x()
			+ _toggle->checkRect().width();
	}
	_name->resizeToWidth(nameWidth);
	_name->moveToLeft(
		st::infoProfileNameLeft,
		st::infoProfileNameTop,
		newWidth);
}

void Cover::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth
		- st::infoProfileStatusLeft
		- st::infoProfileStatusRight;
	if (_toggle) {
		statusWidth -= st::infoToggleCheckbox.checkPosition.x()
			+ _toggle->checkRect().width();
	}
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(
		st::infoProfileStatusLeft,
		st::infoProfileStatusTop,
		newWidth);
}

rpl::producer<bool> Cover::toggledValue() const {
	return _toggle
		? (rpl::single(_toggle->checked())
			| rpl::then(
				base::ObservableViewer(_toggle->checkedChanged)))
		: rpl::never<bool>();
}

QMargins SharedMediaCover::getMargins() const {
	return QMargins(0, 0, 0, st::infoSharedMediaBottomSkip);
}

SharedMediaCover::SharedMediaCover(QWidget *parent)
: FixedHeightWidget(parent, st::infoSharedMediaCoverHeight) {
	createLabel();
}

void SharedMediaCover::createLabel() {
	auto label = object_ptr<Ui::FlatLabel>(
		this,
		Lang::Viewer(lng_profile_shared_media) | ToUpperValue(),
		st::infoSharedMediaLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	widthValue()
		| rpl::start([weak = label.data()](int newWidth) {
			weak->resizeToNaturalWidth(newWidth
				- st::infoSharedMediaLabelPosition.x()
				- st::infoSharedMediaButton.padding.right());
			weak->moveToLeft(
				st::infoSharedMediaLabelPosition.x(),
				st::infoSharedMediaLabelPosition.y(),
				newWidth);
		}, label->lifetime());
}

SharedMediaCover *SharedMediaCover::setToggleShown(
		rpl::producer<bool> &&shown) {
	_toggle.create(
		this,
		QString(),
		st::infoToggleCheckbox,
		std::make_unique<SectionToggle>(
			st::infoToggle,
			false,
			[this] { _toggle->updateCheck(); }));
	_toggle->lower();
	_toggle->setCheckAlignment(style::al_right);
	widthValue()
		| rpl::start([this](int newValue) {
			_toggle->setGeometry(0, 0, newValue, height());
		}, _toggle->lifetime());
	std::move(shown)
		| rpl::start([this](bool shown) {
			if (_toggle->isHidden() == shown) {
				_toggle->setVisible(shown);
			}
		}, lifetime());
	return this;
}

rpl::producer<bool> SharedMediaCover::toggledValue() const {
	return _toggle
		? (rpl::single(_toggle->checked())
			| rpl::then(
				base::ObservableViewer(_toggle->checkedChanged)))
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

Button *Button::toggleOn(rpl::producer<bool> &&toggled) {
	_toggleOnLifetime.destroy();
	_toggle = std::make_unique<Ui::ToggleView>(
		isOver() ? _st.toggleOver : _st.toggle,
		false,
		[this] { rtlupdate(toggleRect()); });
	clicks()
		| rpl::start([this](auto) {
			_toggle->setCheckedAnimated(!_toggle->checked());
		}, _toggleOnLifetime);
	std::move(toggled)
		| rpl::start([this](bool toggled) {
			_toggle->setCheckedAnimated(toggled);
		}, _toggleOnLifetime);
	_toggle->finishAnimation();
	return this;
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
	auto left = width() - _st.toggleSkip - size.width();
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
		availableWidth -= (width() - toggleRect().x());
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

rpl::producer<bool> MultiLineTracker::atLeastOneShownValue() const {
	auto shown = std::vector<rpl::producer<bool>>();
	shown.reserve(_widgets.size());
	for (auto &widget : _widgets) {
		shown.push_back(widget->shownValue());
	}
	return rpl::combine(
		std::move(shown),
		[](const std::vector<bool> &values) {
			return base::find(values, true) != values.end();
		});
}

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

} // namespace Profile
} // namespace Info
