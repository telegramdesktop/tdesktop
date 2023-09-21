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
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "main/main_session.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

[[nodiscard]] rpl::producer<Badge::Content> ContentForPeer(
		not_null<PeerData*> peer) {
	return rpl::combine(
		BadgeValue(peer),
		EmojiStatusIdValue(peer)
	) | rpl::map([=](BadgeType badge, DocumentId emojiStatusId) {
		return Badge::Content{ badge, emojiStatusId };
	});
}

} // namespace

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<PeerData*> peer,
	EmojiStatusPanel *emojiStatusPanel,
	Fn<bool()> animationPaused,
	int customStatusLoopsLimit,
	base::flags<BadgeType> allowed)
: Badge(
	parent,
	st,
	&peer->session(),
	ContentForPeer(peer),
	emojiStatusPanel,
	std::move(animationPaused),
	customStatusLoopsLimit,
	allowed) {
}

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<Main::Session*> session,
	rpl::producer<Content> content,
	EmojiStatusPanel *emojiStatusPanel,
	Fn<bool()> animationPaused,
	int customStatusLoopsLimit,
	base::flags<BadgeType> allowed)
: _parent(parent)
, _st(st)
, _session(session)
, _emojiStatusPanel(emojiStatusPanel)
, _customStatusLoopsLimit(customStatusLoopsLimit)
, _allowed(allowed)
, _animationPaused(std::move(animationPaused)) {
	std::move(
		content
	) | rpl::start_with_next([=](Content content) {
		setContent(content);
	}, _lifetime);
}

Ui::RpWidget *Badge::widget() const {
	return _view.data();
}

void Badge::setContent(Content content) {
	if (!(_allowed & content.badge)
		|| (!_session->premiumBadgesShown()
			&& content.badge == BadgeType::Premium)) {
		content.badge = BadgeType::None;
	}
	if (!(_allowed & content.badge)) {
		content.badge = BadgeType::None;
	}
	if (content.badge != BadgeType::Premium) {
		content.emojiStatusId = 0;
	}
	if (_content == content) {
		return;
	}
	_content = content;
	_emojiStatus = nullptr;
	_view.destroy();
	if (_content.badge == BadgeType::None) {
		_updated.fire({});
		return;
	}
	_view.create(_parent);
	_view->show();
	switch (_content.badge) {
	case BadgeType::Verified:
	case BadgeType::Premium: {
		if (const auto id = _content.emojiStatusId) {
			_emojiStatus = _session->data().customEmojiManager().create(
				id,
				[raw = _view.data()] { raw->update(); },
				sizeTag());
			if (_customStatusLoopsLimit > 0) {
				_emojiStatus = std::make_unique<Ui::Text::LimitedLoopsEmoji>(
					std::move(_emojiStatus),
					_customStatusLoopsLimit);
			}
			const auto emoji = Data::FrameSizeFromTag(sizeTag())
				/ style::DevicePixelRatio();
			_view->resize(emoji, emoji);
			_view->paintRequest(
			) | rpl::start_with_next([=, check = _view.data()]{
				auto args = Ui::Text::CustomEmoji::Context{
					.textColor = _st.premiumFg->c,
					.now = crl::now(),
					.paused = ((_animationPaused && _animationPaused())
						|| On(PowerSaving::kEmojiStatus)),
				};
				if (!_emojiStatusPanel
					|| !_emojiStatusPanel->paintBadgeFrame(check)) {
					Painter p(check);
					_emojiStatus->paint(p, args);
				}
			}, _view->lifetime());
		} else {
			const auto icon = (_content.badge == BadgeType::Verified)
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
		const auto fake = (_content.badge == BadgeType::Fake);
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

	if (_content.badge != BadgeType::Premium || !_premiumClickCallback) {
		_view->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		_view->setClickedCallback(_premiumClickCallback);
	}

	_updated.fire({});
}

void Badge::setPremiumClickCallback(Fn<void()> callback) {
	_premiumClickCallback = std::move(callback);
	if (_view && _content.badge == BadgeType::Premium) {
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
		&& (_content.badge == BadgeType::Premium
			|| _content.badge == BadgeType::Verified);
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
