/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include "data/data_photo.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/photo_editor_layer_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_block.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "api/api_peer_photo.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_chat_helpers.h"

namespace Info {
namespace Profile {
namespace {

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

BadgeView::BadgeView(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<PeerData*> peer,
	Fn<bool()> animationPaused,
	base::flags<Badge> allowed)
: _parent(parent)
, _st(st)
, _peer(peer)
, _allowed(allowed)
, _animationPaused(std::move(animationPaused)) {
	rpl::combine(
		BadgeValue(peer),
		EmojiStatusIdValue(peer)
	) | rpl::start_with_next([=](Badge badge, DocumentId emojiStatusId) {
		setBadge(badge, emojiStatusId);
	}, _lifetime);
}

Ui::RpWidget *BadgeView::widget() const {
	return _view.data();
}

void BadgeView::setBadge(Badge badge, DocumentId emojiStatusId) {
	if ((!_peer->session().premiumBadgesShown() && badge == Badge::Premium)
		|| !(_allowed & badge)) {
		badge = Badge::None;
	}
	if (!(_allowed & badge)) {
		badge = Badge::None;
	}
	if (badge != Badge::Premium) {
		emojiStatusId = 0;
	}
	if (_badge == badge && _emojiStatusId == emojiStatusId) {
		return;
	}
	_badge = badge;
	_emojiStatusId = emojiStatusId;
	_emojiStatus = nullptr;
	_view.destroy();
	if (_badge == Badge::None) {
		_updated.fire({});
		return;
	}
	_view.create(_parent);
	_view->show();
	switch (_badge) {
	case Badge::Verified:
	case Badge::Premium: {
		if (_emojiStatusId) {
			using SizeTag = Data::CustomEmojiManager::SizeTag;
			const auto tag = (_st.sizeTag == 2)
				? SizeTag::Isolated
				: (_st.sizeTag == 1)
				? SizeTag::Large
				: SizeTag::Normal;
			_emojiStatus = _peer->owner().customEmojiManager().create(
				_emojiStatusId,
				[raw = _view.data()]{ raw->update(); },
				tag);
			const auto emoji = Data::FrameSizeFromTag(tag)
				/ style::DevicePixelRatio();
			_view->resize(emoji, emoji);
			_view->paintRequest(
			) | rpl::start_with_next([=, check = _view.data()]{
				Painter p(check);
				_emojiStatus->paint(p, {
					.preview = st::windowBgOver->c,
					.now = crl::now(),
					.paused = _animationPaused && _animationPaused(),
				});
			}, _view->lifetime());
		} else {
			const auto icon = (_badge == Badge::Verified)
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
	case Badge::Scam:
	case Badge::Fake: {
		const auto fake = (_badge == Badge::Fake);
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

	if (_badge != Badge::Premium || !_premiumClickCallback) {
		_view->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		_view->setClickedCallback(_premiumClickCallback);
	}

	_updated.fire({});
}

void BadgeView::setPremiumClickCallback(Fn<void()> callback) {
	_premiumClickCallback = std::move(callback);
	if (_view && _badge == Badge::Premium) {
		if (!_premiumClickCallback) {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents);
		} else {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			_view->setClickedCallback(_premiumClickCallback);
		}
	}
}

rpl::producer<> BadgeView::updated() const {
	return _updated.events();
}

void BadgeView::move(int left, int top, int bottom) {
	if (!_view) {
		return;
	}
	const auto star = !_emojiStatus
		&& (_badge == Badge::Premium || _badge == Badge::Verified);
	const auto fake = !_emojiStatus && !star;
	const auto skip = fake ? 0 : _st.position.x();
	const auto badgeLeft = left + skip;
	const auto badgeTop = top
		+ (star
			? _st.position.y()
			: (bottom - top - _view->height()) / 2);
	_view->moveToLeft(badgeLeft, badgeTop);
}

void EmojiStatusPanel::show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button) {
	if (!_panel) {
		create(controller);

		const auto weak = Ui::MakeWeak(button.get());
		_panel->shownValue(
		) | rpl::filter([=](bool shown) {
			return !shown && weak;
		}) | rpl::start_with_next([=] {
			button->removeEventFilter(_panel.get());
		}, _panel->lifetime());
	}
	const auto parent = _panel->parentWidget();
	const auto global = button->mapToGlobal(QPoint());
	const auto local = parent->mapFromGlobal(global);
	_panel->moveTopRight(
		local.y() + button->height(),
		local.x() + button->width() * 3);
	_panel->toggleAnimated();
	button->installEventFilter(_panel.get());
}

void EmojiStatusPanel::create(
		not_null<Window::SessionController*> controller) {
	using Selector = ChatHelpers::TabbedSelector;
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		controller->window().widget()->bodyWidget(),
		controller,
		object_ptr<Selector>(
			nullptr,
			controller,
			Window::GifPauseReason::Layer,
			ChatHelpers::TabbedSelector::Mode::EmojiStatus));
	_panel->setDropDown(true);
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();
	_panel->selector()->setAllowEmojiWithoutPremium(false);

	auto statusChosen = _panel->selector()->customEmojiChosen(
	) | rpl::map([=](Selector::FileChosen data) {
		return data.document->id;
	});

	rpl::merge(
		std::move(statusChosen),
		_panel->selector()->emojiChosen() | rpl::map_to(DocumentId())
	) | rpl::start_with_next([=](DocumentId id) {
		controller->session().user()->setEmojiStatus(id);
		controller->session().api().request(MTPaccount_UpdateEmojiStatus(
			id ? MTP_emojiStatus(MTP_long(id)) : MTP_emojiStatusEmpty()
		)).send();
		_panel->hideAnimated();
	}, _panel->lifetime());

	_panel->selector()->showPromoForPremiumEmoji();
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
: FixedHeightWidget(
	parent,
	st::infoProfilePhotoTop
		+ st::infoProfilePhoto.size.height()
		+ st::infoProfilePhotoBottom)
, _controller(controller)
, _peer(peer)
, _badge(
	this,
	st::infoPeerBadge,
	peer,
	[=] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	})
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

	_badge.setPremiumClickCallback([=] {
		if (_peer->isSelf()) {
			_emojiStatusPanel.show(_controller, _badge.widget());
		} else {
			::Settings::ShowPremium(
				_controller,
				u"profile__%1"_q.arg(peerToUser(_peer->id).bare));
		}
	});
	_badge.updated() | rpl::start_with_next([=] {
		refreshNameGeometry(width());
	}, _name->lifetime());

	initViewers(std::move(title));
	setupChildGeometry();

	_userpic->uploadPhotoRequests(
	) | rpl::start_with_next([=] {
		_peer->session().api().peerPhoto().upload(
			_peer,
			_userpic->takeResultImage());
	}, _userpic->lifetime());
}

void Cover::setupChildGeometry() {
	widthValue(
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

void Cover::refreshStatusText() {
	auto hasMembersLink = [&] {
		if (auto megagroup = _peer->asMegagroup()) {
			return megagroup->canViewMembers();
		}
		return false;
	}();
	auto statusText = [&]() -> TextWithEntities {
		using namespace Ui::Text;
		auto currentTime = base::unixtime::now();
		if (auto user = _peer->asUser()) {
			const auto result = Data::OnlineTextFull(user, currentTime);
			const auto showOnline = Data::OnlineTextActive(user, currentTime);
			const auto updateIn = Data::OnlineChangeTimeout(user, currentTime);
			if (showOnline) {
				_refreshStatusTimer.callOnce(updateIn);
			}
			return showOnline
				? PlainLink(result)
				: TextWithEntities{ .text = result };
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return tr::lng_chat_status_unaccessible({}, WithEntities);
			}
			auto fullCount = std::max(
				chat->count,
				int(chat->participants.size()));
			return { .text = ChatStatusText(fullCount, _onlineCount, true) };
		} else if (auto channel = _peer->asChannel()) {
			auto fullCount = qMax(channel->membersCount(), 1);
			auto result = ChatStatusText(
				fullCount,
				_onlineCount,
				channel->isMegagroup());
			return hasMembersLink
				? PlainLink(result)
				: TextWithEntities{ .text = result };
		}
		return tr::lng_chat_status_unaccessible(tr::now, WithEntities);
	}();
	_status->setMarkedText(statusText);
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
		- st::infoProfileNameRight;
	if (const auto width = _badge.widget() ? _badge.widget()->width() : 0) {
		nameWidth -= st::infoVerifiedCheckPosition.x() + width;
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
	const auto badgeLeft = nameLeft + _name->width();
	const auto badgeTop = nameTop;
	const auto badgeBottom = nameTop + _name->height();
	_badge.move(badgeLeft, badgeTop, badgeBottom);
}

void Cover::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth
		- st::infoProfileStatusLeft
		- st::infoProfileStatusRight;
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(
		st::infoProfileStatusLeft,
		st::infoProfileStatusTop,
		newWidth);
}

} // namespace Profile
} // namespace Info
