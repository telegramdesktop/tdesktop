/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include <rpl/never.h>
#include <rpl/combine.h>
#include "data/data_photo.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "editor/photo_editor_layer_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {
namespace {

class SectionToggle : public Ui::AbstractCheckView {
public:
	SectionToggle(
		const style::InfoToggle &st,
		bool checked,
		Fn<void()> updateCallback);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	QSize rippleSize() const;

	const style::InfoToggle &_st;

};

SectionToggle::SectionToggle(
		const style::InfoToggle &st,
		bool checked,
		Fn<void()> updateCallback)
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
		int outerWidth) {
	auto sqrt2 = sqrt(2.);
	auto vLeft = style::rtlpoint(left + _st.skip, 0, outerWidth).x() + 0.;
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

	auto toggled = currentAnimationValue();
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
	return tr::lng_chat_status_members(tr::now, lt_count_decimal, count);
};

auto OnlineStatusText(int count) {
	return tr::lng_chat_status_online(tr::now, lt_count_decimal, count);
};

auto ChatStatusText(int fullCount, int onlineCount, bool isGroup) {
	if (onlineCount > 1 && onlineCount <= fullCount) {
		return tr::lng_chat_status_members_online(
			tr::now,
			lt_members_count,
			MembersStatusText(fullCount),
			lt_online_count,
			OnlineStatusText(onlineCount));
	} else if (fullCount > 0) {
		return isGroup
			? tr::lng_chat_status_members(
				tr::now,
				lt_count_decimal,
				fullCount)
			: tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				fullCount);
	}
	return isGroup
		? tr::lng_group_status(tr::now)
		: tr::lng_channel_status(tr::now);
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
	widthValue(
	) | rpl::start_with_next([this](int newValue) {
		_toggle->setGeometry(0, 0, newValue, height());
	}, _toggle->lifetime());
	std::move(
		shown
	) | rpl::start_with_next([this](bool shown) {
		if (_toggle->isHidden() == shown) {
			_toggle->setVisible(shown);
			_toggleShown.fire_copy(shown);
		}
	}, lifetime());
	return this;
}

void SectionWithToggle::toggle(bool toggled, anim::type animated) {
	if (_toggle) {
		_toggle->setChecked(toggled);
		if (animated == anim::type::instant) {
			_toggle->finishAnimating();
		}
	}
}

bool SectionWithToggle::toggled() const {
	return _toggle ? _toggle->checked() : false;
}

rpl::producer<bool> SectionWithToggle::toggledValue() const {
	if (_toggle) {
		return _toggle->checkedValue();
	}
	return nullptr;
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

Cover::Cover(
	QWidget *parent,
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller)
: Cover(parent, peer, controller, NameValue(
	peer
) | rpl::map([=](const TextWithEntities &name) {
	return name.text;
})) {
}

Cover::Cover(
	QWidget *parent,
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title)
: SectionWithToggle(
	parent,
	st::infoProfilePhotoTop
		+ st::infoProfilePhoto.size.height()
		+ st::infoProfilePhotoBottom)
, _peer(peer)
, _userpic(
	this,
	controller,
	_peer,
	Ui::UserpicButton::Role::OpenPhoto,
	st::infoProfilePhoto)
, _name(this, st::infoProfileNameLabel)
, _status(
	this,
	_peer->isMegagroup()
		? st::infoProfileMegagroupStatusLabel
		: st::infoProfileStatusLabel)
, _refreshStatusTimer([this] { refreshStatusText(); }) {
	_peer->updateFull();

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	if (!_peer->isMegagroup()) {
		_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	initViewers(std::move(title));
	setupChildGeometry();

	_userpic->uploadPhotoRequests(
	) | rpl::start_with_next([=] {
		_peer->session().api().uploadPeerPhoto(
			_peer,
			_userpic->takeResultImage());
	}, _userpic->lifetime());
}

void Cover::setupChildGeometry() {
	using namespace rpl::mappers;
	rpl::combine(
		toggleShownValue(),
		widthValue(),
		_2
	) | rpl::start_with_next([this](int newWidth) {
		_userpic->moveToLeft(
			st::infoProfilePhotoLeft,
			st::infoProfilePhotoTop,
			newWidth);
		refreshNameGeometry(newWidth);
		refreshStatusGeometry(newWidth);
	}, lifetime());
}

Cover *Cover::setOnlineCount(rpl::producer<int> &&count) {
	std::move(
		count
	) | rpl::start_with_next([this](int count) {
		_onlineCount = count;
		refreshStatusText();
	}, lifetime());
	return this;
}

void Cover::initViewers(rpl::producer<QString> title) {
	using Flag = Data::PeerUpdate::Flag;
	std::move(
		title
	) | rpl::start_with_next([=](const QString &title) {
		_name->setText(title);
		refreshNameGeometry(width());
	}, lifetime());

	_peer->session().changes().peerFlagsValue(
		_peer,
		Flag::OnlineStatus | Flag::Members
	) | rpl::start_with_next(
		[=] { refreshStatusText(); },
		lifetime());
	if (!_peer->isUser()) {
		_peer->session().changes().peerFlagsValue(
			_peer,
			Flag::Rights
		) | rpl::start_with_next(
			[=] { refreshUploadPhotoOverlay(); },
			lifetime());
	} else if (_peer->isSelf()) {
		refreshUploadPhotoOverlay();
	}
	BadgeValue(
		_peer
	) | rpl::start_with_next([=](Badge badge) {
		setBadge(badge);
	}, lifetime());
}

void Cover::refreshUploadPhotoOverlay() {
	_userpic->switchChangePhotoOverlay([&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		}
		return _peer->isSelf();
	}());
}

void Cover::setBadge(Badge badge) {
	if (_badge == badge) {
		return;
	}
	_badge = badge;
	_verifiedCheck.destroy();
	_scamFakeBadge.destroy();
	switch (_badge) {
	case Badge::Verified:
		_verifiedCheck.create(this);
		_verifiedCheck->show();
		_verifiedCheck->resize(st::infoVerifiedCheck.size());
		_verifiedCheck->paintRequest(
		) | rpl::start_with_next([check = _verifiedCheck.data()]{
			Painter p(check);
			st::infoVerifiedCheck.paint(p, 0, 0, check->width());
			}, _verifiedCheck->lifetime());
		break;
	case Badge::Scam:
	case Badge::Fake: {
		const auto fake = (_badge == Badge::Fake);
		const auto size = Ui::ScamBadgeSize(fake);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_scamFakeBadge.create(this);
		_scamFakeBadge->show();
		_scamFakeBadge->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_scamFakeBadge->paintRequest(
		) | rpl::start_with_next([=, badge = _scamFakeBadge.data()]{
			Painter p(badge);
			Ui::DrawScamBadge(
				fake,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				st::attentionButtonFg);
			}, _scamFakeBadge->lifetime());
	} break;
	}
	refreshNameGeometry(width());
}

void Cover::refreshStatusText() {
	auto hasMembersLink = [&] {
		if (auto megagroup = _peer->asMegagroup()) {
			return megagroup->canViewMembers();
		}
		return false;
	}();
	auto statusText = [&] {
		auto currentTime = base::unixtime::now();
		if (auto user = _peer->asUser()) {
			const auto result = Data::OnlineTextFull(user, currentTime);
			const auto showOnline = Data::OnlineTextActive(user, currentTime);
			const auto updateIn = Data::OnlineChangeTimeout(user, currentTime);
			if (showOnline) {
				_refreshStatusTimer.callOnce(updateIn);
			}
			return showOnline
				? textcmdLink(1, result)
				: result;
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return tr::lng_chat_status_unaccessible(tr::now);
			}
			auto fullCount = std::max(
				chat->count,
				int(chat->participants.size()));
			return ChatStatusText(fullCount, _onlineCount, true);
		} else if (auto channel = _peer->asChannel()) {
			auto fullCount = qMax(channel->membersCount(), 1);
			auto result = ChatStatusText(
				fullCount,
				_onlineCount,
				channel->isMegagroup());
			return hasMembersLink ? textcmdLink(1, result) : result;
		}
		return tr::lng_chat_status_unaccessible(tr::now);
	}();
	_status->setRichText(statusText);
	if (hasMembersLink) {
		_status->setLink(1, std::make_shared<LambdaClickHandler>([=] {
			_showSection.fire(Section::Type::Members);
		}));
	}
	refreshStatusGeometry(width());
}

Cover::~Cover() {
}

void Cover::refreshNameGeometry(int newWidth) {
	auto nameLeft = st::infoProfileNameLeft;
	auto nameTop = st::infoProfileNameTop;
	auto nameWidth = newWidth
		- nameLeft
		- st::infoProfileNameRight
		- toggleSkip();
	if (_verifiedCheck) {
		nameWidth -= st::infoVerifiedCheckPosition.x()
			+ _verifiedCheck->width();
	} else if (_scamFakeBadge) {
		nameWidth -= st::infoVerifiedCheckPosition.x()
			+ _scamFakeBadge->width();
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
	if (_verifiedCheck) {
		const auto checkLeft = nameLeft
			+ _name->width()
			+ st::infoVerifiedCheckPosition.x();
		const auto checkTop = nameTop
			+ st::infoVerifiedCheckPosition.y();
		_verifiedCheck->moveToLeft(checkLeft, checkTop, newWidth);
	} else if (_scamFakeBadge) {
		const auto skip = st::infoVerifiedCheckPosition.x();
		const auto badgeLeft = nameLeft
			+ _name->width()
			+ st::infoVerifiedCheckPosition.x()
			- skip;
		const auto badgeTop = nameTop
			+ (_name->height() - _scamFakeBadge->height()) / 2;
		_scamFakeBadge->moveToLeft(badgeLeft, badgeTop, newWidth);
	}
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

SharedMediaCover *SharedMediaCover::setToggleShown(rpl::producer<bool> &&shown) {
	return static_cast<SharedMediaCover*>(
		SectionWithToggle::setToggleShown(std::move(shown)));
}

void SharedMediaCover::createLabel() {
	using namespace rpl::mappers;
	auto label = object_ptr<Ui::FlatLabel>(
		this,
		tr::lng_profile_shared_media() | Ui::Text::ToUpper(),
		st::infoBlockHeaderLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		toggleShownValue(),
		widthValue(),
		_2
	) | rpl::start_with_next([this, weak = label.data()](int newWidth) {
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
