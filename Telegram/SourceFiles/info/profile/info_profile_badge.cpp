/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "main/main_session.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

} // namespace

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<PeerData*> peer,
	EmojiStatusPanel *emojiStatusPanel,
	Fn<bool()> animationPaused,
	int customStatusLoopsLimit,
	base::flags<BadgeType> allowed)
: _parent(parent)
, _st(st)
, _peer(peer)
, _emojiStatusPanel(emojiStatusPanel)
, _customStatusLoopsLimit(customStatusLoopsLimit)
, _allowed(allowed)
, _animationPaused(std::move(animationPaused)) {
	rpl::combine(
		BadgeValue(peer),
		EmojiStatusIdValue(peer)
	) | rpl::start_with_next([=](BadgeType badge, DocumentId emojiStatusId) {
		setBadge(badge, emojiStatusId);
	}, _lifetime);
}

Ui::RpWidget *Badge::widget() const {
	return _view.data();
}

void Badge::setBadge(BadgeType badge, DocumentId emojiStatusId) {
	if (!(_allowed & badge)
		|| (!_peer->session().premiumBadgesShown()
			&& badge == BadgeType::Premium)) {
		badge = BadgeType::None;
	}
	if (!(_allowed & badge)) {
		badge = BadgeType::None;
	}
	if (badge != BadgeType::Premium) {
		emojiStatusId = 0;
	}
	if (_badge == badge && _emojiStatusId == emojiStatusId) {
		return;
	}
	_badge = badge;
	_emojiStatusId = emojiStatusId;
	_emojiStatus = nullptr;
	_emojiStatusColored = nullptr;
	_view.destroy();
	if (_badge == BadgeType::None) {
		_updated.fire({});
		return;
	}
	_view.create(_parent);
	_view->show();
	switch (_badge) {
	case BadgeType::Verified:
	case BadgeType::Premium: {
		if (_emojiStatusId) {
			_emojiStatus = _peer->owner().customEmojiManager().create(
				_emojiStatusId,
				[raw = _view.data()] { raw->update(); },
				sizeTag());
			if (_customStatusLoopsLimit > 0) {
				_emojiStatus = std::make_unique<Ui::Text::LimitedLoopsEmoji>(
					std::move(_emojiStatus),
					_customStatusLoopsLimit);
			}
			_emojiStatusColored = std::make_unique<
				Ui::Text::CustomEmojiColored
			>();
			const auto emoji = Data::FrameSizeFromTag(sizeTag())
				/ style::DevicePixelRatio();
			_view->resize(emoji, emoji);
			_view->paintRequest(
			) | rpl::start_with_next([=, check = _view.data()]{
				_emojiStatusColored->color = _st.premiumFg->c;
				auto args = Ui::Text::CustomEmoji::Context{
					.preview = st::windowBgOver->c,
					.colored = _emojiStatusColored.get(),
					.now = crl::now(),
					.paused = _animationPaused && _animationPaused(),
				};
				if (!_emojiStatusPanel
					|| !_emojiStatusPanel->paintBadgeFrame(check)) {
					Painter p(check);
					_emojiStatus->paint(p, args);
				}
			}, _view->lifetime());
		} else {
			const auto icon = (_badge == BadgeType::Verified)
				? &_st.verified
				: &_st.premium;
			_view->resize(icon->size());
			_view->paintRequest(
			) | rpl::start_with_next([=, check = _view.data()]{
				Painter p(check);
				icon->paint(p, 0, 0, check->width());
			}, _view->lifetime());
		}
	} break;
	case BadgeType::Scam:
	case BadgeType::Fake: {
		const auto fake = (_badge == BadgeType::Fake);
		const auto size = Ui::ScamBadgeSize(fake);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_view->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_view->paintRequest(
		) | rpl::start_with_next([=, badge = _view.data()]{
			Painter p(badge);
			Ui::DrawScamBadge(
				fake,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				st::attentionButtonFg);
			}, _view->lifetime());
	} break;
	}

	if (_badge != BadgeType::Premium || !_premiumClickCallback) {
		_view->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		_view->setClickedCallback(_premiumClickCallback);
	}

	_updated.fire({});
}

void Badge::setPremiumClickCallback(Fn<void()> callback) {
	_premiumClickCallback = std::move(callback);
	if (_view && _badge == BadgeType::Premium) {
		if (!_premiumClickCallback) {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents);
		} else {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			_view->setClickedCallback(_premiumClickCallback);
		}
	}
}

rpl::producer<> Badge::updated() const {
	return _updated.events();
}

void Badge::move(int left, int top, int bottom) {
	if (!_view) {
		return;
	}
	const auto star = !_emojiStatus
		&& (_badge == BadgeType::Premium || _badge == BadgeType::Verified);
	const auto fake = !_emojiStatus && !star;
	const auto skip = fake ? 0 : _st.position.x();
	const auto badgeLeft = left + skip;
	const auto badgeTop = top
		+ (star
			? _st.position.y()
			: (bottom - top - _view->height()) / 2);
	_view->moveToLeft(badgeLeft, badgeTop);
}

Data::CustomEmojiSizeTag Badge::sizeTag() const {
	using SizeTag = Data::CustomEmojiSizeTag;
	return (_st.sizeTag == 2)
		? SizeTag::Isolated
		: (_st.sizeTag == 1)
		? SizeTag::Large
		: SizeTag::Normal;
}

} // namespace Info::Profile
