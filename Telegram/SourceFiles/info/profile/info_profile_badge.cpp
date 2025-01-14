/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge.h"

#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
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

[[nodiscard]] bool HasPremiumClick(const Badge::Content &content) {
	return content.badge == BadgeType::Premium
		|| (content.badge == BadgeType::Verified && content.emojiStatusId);
}

} // namespace

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

Badge::~Badge() = default;

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
	case BadgeType::BotVerified:
	case BadgeType::Premium: {
		const auto id = _content.emojiStatusId;
		const auto emoji = id
			? (Data::FrameSizeFromTag(sizeTag())
				/ style::DevicePixelRatio())
			: 0;
		const auto icon = (_content.badge == BadgeType::Verified)
			? &_st.verified
			: id
			? nullptr
			: &_st.premium;
		if (id) {
			_emojiStatus = _session->data().customEmojiManager().create(
				Data::EmojiStatusCustomId(id),
				[raw = _view.data()] { raw->update(); },
				sizeTag());
			if (_customStatusLoopsLimit > 0) {
				_emojiStatus = std::make_unique<Ui::Text::LimitedLoopsEmoji>(
					std::move(_emojiStatus),
					_customStatusLoopsLimit);
			}
		}
		const auto width = emoji + (icon ? icon->width() : 0);
		const auto height = std::max(emoji, icon ? icon->height() : 0);
		_view->resize(width, height);
		_view->paintRequest(
		) | rpl::start_with_next([=, check = _view.data()]{
			if (_emojiStatus) {
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
			}
			if (icon) {
				Painter p(check);
				icon->paint(p, emoji, 0, check->width());
			}
		}, _view->lifetime());
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

	if (!HasPremiumClick(_content) || !_premiumClickCallback) {
		_view->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		_view->setClickedCallback(_premiumClickCallback);
	}

	_updated.fire({});
}

void Badge::setPremiumClickCallback(Fn<void()> callback) {
	_premiumClickCallback = std::move(callback);
	if (_view && HasPremiumClick(_content)) {
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

rpl::producer<Badge::Content> BadgeContentForPeer(not_null<PeerData*> peer) {
	const auto statusOnlyForPremium = peer->isUser();
	return rpl::combine(
		BadgeValue(peer),
		EmojiStatusIdValue(peer)
	) | rpl::map([=](BadgeType badge, EmojiStatusId emojiStatusId) {
		if (badge == BadgeType::Verified) {
			badge = BadgeType::None;
		}
		if (statusOnlyForPremium && badge != BadgeType::Premium) {
			emojiStatusId = EmojiStatusId();
		} else if (emojiStatusId && badge == BadgeType::None) {
			badge = BadgeType::Premium;
		}
		return Badge::Content{ badge, emojiStatusId };
	});
}

rpl::producer<Badge::Content> VerifiedContentForPeer(
		not_null<PeerData*> peer) {
	return BadgeValue(peer) | rpl::map([=](BadgeType badge) {
		if (badge != BadgeType::Verified) {
			badge = BadgeType::None;
		}
		return Badge::Content{ badge };
	});
}

} // namespace Info::Profile
