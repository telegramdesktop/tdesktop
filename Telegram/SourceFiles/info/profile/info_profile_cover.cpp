/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include "api/api_user_privacy.h"
#include "base/timer_rpl.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/info_controller.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "boxes/report_messages_box.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "ui/boxes/show_or_premium_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "chat_helpers/stickers_lottie.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"

namespace Info::Profile {
namespace {

constexpr auto kWaitBeforeGiftBadge = crl::time(1000);
constexpr auto kGiftBadgeGlares = 3;
constexpr auto kGlareDurationStep = crl::time(320);
constexpr auto kGlareTimeout = crl::time(1000);

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

[[nodiscard]] const style::InfoProfileCover &CoverStyle(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Cover::Role role) {
	return (role == Cover::Role::EditContact)
		? st::infoEditContactCover
		: topic
		? st::infoTopicCover
		: peer->isMegagroup()
		? st::infoProfileMegagroupCover
		: st::infoProfileCover;
}

[[nodiscard]] QMargins LargeCustomEmojiMargins() {
	const auto ratio = style::DevicePixelRatio();
	const auto emoji = Ui::Emoji::GetSizeLarge() / ratio;
	const auto size = Data::FrameSizeFromTag(Data::CustomEmojiSizeTag::Large)
		/ ratio;
	const auto left = (size - emoji) / 2;
	const auto right = size - emoji - left;
	return { left, left, right, right };
}

} // namespace

class Cover::BadgeTooltip final : public Ui::RpWidget {
public:
	BadgeTooltip(
		not_null<QWidget*> parent,
		std::shared_ptr<Data::EmojiStatusCollectible> collectible,
		not_null<QWidget*> pointTo);

	void fade(bool shown);
	void finishAnimating();

	[[nodiscard]] crl::time glarePeriod() const;

private:
	void paintEvent(QPaintEvent *e) override;

	void setupGeometry(not_null<QWidget*> pointTo);
	void prepareImage();
	void showGlare();

	const style::ImportantTooltip &_st;
	std::shared_ptr<Data::EmojiStatusCollectible> _collectible;
	QString _text;
	const style::font &_font;
	QSize _inner;
	QSize _outer;
	int _stroke = 0;
	int _skip = 0;
	QSize _full;
	int _glareSize = 0;
	int _glareRange = 0;
	crl::time _glareDuration = 0;
	base::Timer _glareTimer;

	Ui::Animations::Simple _showAnimation;
	Ui::Animations::Simple _glareAnimation;

	QImage _image;
	int _glareRight = 0;
	int _imageGlareRight = 0;
	int _arrowMiddle = 0;
	int _imageArrowMiddle = 0;

	bool _shown = false;

};

Cover::BadgeTooltip::BadgeTooltip(
	not_null<QWidget*> parent,
	std::shared_ptr<Data::EmojiStatusCollectible> collectible,
	not_null<QWidget*> pointTo)
: Ui::RpWidget(parent)
, _st(st::infoGiftTooltip)
, _collectible(std::move(collectible))
, _text(_collectible->title)
, _font(st::infoGiftTooltipFont)
, _inner(_font->width(_text), _font->height)
, _outer(_inner.grownBy(_st.padding))
, _stroke(st::lineWidth)
, _skip(2 * _stroke)
, _full(_outer + QSize(2 * _skip, _st.arrow + 2 * _skip))
, _glareSize(_outer.height() * 3)
, _glareRange(_outer.width() + _glareSize)
, _glareDuration(_glareRange * kGlareDurationStep / _glareSize)
, _glareTimer([=] { showGlare(); }) {
	resize(_full + QSize(0, _st.shift));
	setupGeometry(pointTo);
}

void Cover::BadgeTooltip::fade(bool shown) {
	if (_shown == shown) {
		return;
	}
	show();
	_shown = shown;
	_showAnimation.start([=] {
		update();
		if (!_showAnimation.animating()) {
			if (!_shown) {
				hide();
			} else {
				showGlare();
			}
		}
	}, _shown ? 0. : 1., _shown ? 1. : 0., _st.duration, anim::easeInCirc);
}

void Cover::BadgeTooltip::showGlare() {
	_glareAnimation.start([=] {
		update();
		if (!_glareAnimation.animating()) {
			_glareTimer.callOnce(kGlareTimeout);
		}
	}, 0., 1., _glareDuration);
}

void Cover::BadgeTooltip::finishAnimating() {
	_showAnimation.stop();
	if (!_shown) {
		hide();
	}
}

crl::time Cover::BadgeTooltip::glarePeriod() const {
	return _glareDuration + kGlareTimeout;
}

void Cover::BadgeTooltip::paintEvent(QPaintEvent *e) {
	const auto glare = _glareAnimation.value(0.);
	_glareRight = anim::interpolate(0, _glareRange, glare);
	prepareImage();

	auto p = QPainter(this);
	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	p.setOpacity(shown);
	const auto imageHeight = _image.height() / _image.devicePixelRatio();
	const auto top = anim::interpolate(0, height() - imageHeight, shown);
	p.drawImage(0, top, _image);
}

void Cover::BadgeTooltip::setupGeometry(not_null<QWidget*> pointTo) {
	auto widget = pointTo.get();
	const auto parent = parentWidget();

	const auto refresh = [=] {
		const auto rect = Ui::MapFrom(parent, pointTo, pointTo->rect());
		const auto point = QPoint(rect.center().x(), rect.y());
		const auto left = point.x() - (width() / 2);
		const auto skip = _st.padding.left();
		setGeometry(
			std::min(std::max(left, skip), parent->width() - width() - skip),
			std::max(point.y() - height() - _st.margin.bottom(), skip),
			width(),
			height());
		const auto arrowMiddle = point.x() - x();
		if (_arrowMiddle != arrowMiddle) {
			_arrowMiddle = arrowMiddle;
			update();
		}
	};
	refresh();
	while (widget && widget != parent) {
		base::install_event_filter(this, widget, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Resize || e->type() == QEvent::Move || e->type() == QEvent::ZOrderChange) {
				refresh();
				raise();
			}
			return base::EventFilterResult::Continue;
		});
		widget = widget->parentWidget();
	}
}

void Cover::BadgeTooltip::prepareImage() {
	const auto ratio = style::DevicePixelRatio();
	const auto arrow = _st.arrow;
	const auto size = _full * ratio;
	if (_image.size() != size) {
		_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_image.setDevicePixelRatio(ratio);
	} else if (_imageGlareRight == _glareRight
		&& _imageArrowMiddle == _arrowMiddle) {
		return;
	}
	_imageGlareRight = _glareRight;
	_imageArrowMiddle = _arrowMiddle;
	_image.fill(Qt::transparent);

	const auto gfrom = _imageGlareRight - _glareSize;
	const auto gtill = _imageGlareRight;

	auto path = QPainterPath();
	const auto width = _outer.width();
	const auto height = _outer.height();
	const auto radius = (height + 1) / 2;
	const auto diameter = height;
	path.moveTo(radius, 0);
	path.lineTo(width - radius, 0);
	path.arcTo(
		QRect(QPoint(width - diameter, 0), QSize(diameter, diameter)),
		90,
		-180);
	const auto xarrow = _arrowMiddle - _skip;
	if (xarrow - arrow <= radius || xarrow + arrow >= width - radius) {
		path.lineTo(radius, height);
	} else {
		path.lineTo(xarrow + arrow, height);
		path.lineTo(xarrow, height + arrow);
		path.lineTo(xarrow - arrow, height);
		path.lineTo(radius, height);
	}
	path.arcTo(
		QRect(QPoint(0, 0), QSize(diameter, diameter)),
		-90,
		-180);
	path.closeSubpath();

	auto p = QPainter(&_image);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	if (gtill > 0) {
		auto gradient = QLinearGradient(gfrom, 0, gtill, 0);
		gradient.setStops({
			{ 0., _collectible->edgeColor },
			{ 0.5, _collectible->centerColor },
			{ 1., _collectible->edgeColor },
		});
		p.setBrush(gradient);
	} else {
		p.setBrush(_collectible->edgeColor);
	}
	p.translate(_skip, _skip);
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setBrush(Qt::NoBrush);
	auto copy = _collectible->textColor;
	copy.setAlpha(0);
	if (gtill > 0) {
		auto gradient = QLinearGradient(gfrom, 0, gtill, 0);
		gradient.setStops({
			{ 0., copy },
			{ 0.5, _collectible->textColor },
			{ 1., copy },
		});
		p.setPen(QPen(gradient, _stroke));
	} else {
		p.setPen(QPen(copy, _stroke));
	}
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setFont(_font);
	p.setPen(QColor(255, 255, 255));
	p.drawText(_st.padding.left(), _st.padding.top() + _font->ascent, _text);
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update)
: TopicIconView(
	topic,
	std::move(paused),
	std::move(update),
	st::windowSubTextFg) {
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update,
	const style::color &generalIconFg)
: _topic(topic)
, _generalIconFg(generalIconFg)
, _paused(std::move(paused))
, _update(std::move(update)) {
	setup(topic);
}

void TopicIconView::paintInRect(QPainter &p, QRect rect) {
	const auto paint = [&](const QImage &image) {
		const auto size = image.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				rect.x() + (rect.width() - size.width()) / 2,
				rect.y() + (rect.height() - size.height()) / 2,
				size.width(),
				size.height()),
			image);
	};
	if (_player && _player->ready()) {
		const auto colored = _playerUsesTextColor
			? st::windowFg->c
			: QColor(0, 0, 0, 0);
		paint(_player->frame(
			st::infoTopicCover.photo.size,
			colored,
			false,
			crl::now(),
			_paused()).image);
		_player->markFrameShown();
	} else if (!_topic->iconId() && !_image.isNull()) {
		paint(_image);
	}
}

void TopicIconView::setup(not_null<Data::ForumTopic*> topic) {
	setupPlayer(topic);
	setupImage(topic);
}

void TopicIconView::setupPlayer(not_null<Data::ForumTopic*> topic) {
	IconIdValue(
		topic
	) | rpl::map([=](DocumentId id) -> rpl::producer<DocumentData*> {
		if (!id) {
			return rpl::single((DocumentData*)nullptr);
		}
		return topic->owner().customEmojiManager().resolve(
			id
		) | rpl::map([=](not_null<DocumentData*> document) {
			return document.get();
		}) | rpl::map_error_to_done();
	}) | rpl::flatten_latest(
	) | rpl::map([=](DocumentData *document)
	-> rpl::producer<std::shared_ptr<StickerPlayer>> {
		if (!document) {
			return rpl::single(std::shared_ptr<StickerPlayer>());
		}
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();

		return rpl::single() | rpl::then(
			document->owner().session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::map([=] {
			auto result = std::shared_ptr<StickerPlayer>();
			const auto sticker = document->sticker();
			if (sticker->isLottie()) {
				result = std::make_shared<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::StickerSet,
						st::infoTopicCover.photo.size,
						Lottie::Quality::High));
			} else if (sticker->isWebm()) {
				result = std::make_shared<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			} else {
				result = std::make_shared<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			}
			result->setRepaintCallback(_update);
			_playerUsesTextColor = media->owner()->emojiUsesTextColor();
			return result;
		});
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](std::shared_ptr<StickerPlayer> player) {
		_player = std::move(player);
		if (!_player) {
			_update();
		}
	}, _lifetime);
}

void TopicIconView::setupImage(not_null<Data::ForumTopic*> topic) {
	using namespace Data;
	if (topic->isGeneral()) {
		rpl::single(rpl::empty) | rpl::then(
			style::PaletteChanged()
		) | rpl::start_with_next([=] {
			_image = ForumTopicGeneralIconFrame(
				st::infoForumTopicIcon.size,
				_generalIconFg->c);
			_update();
		}, _lifetime);
		return;
	}
	rpl::combine(
		TitleValue(topic),
		ColorIdValue(topic)
	) | rpl::map([=](const QString &title, int32 colorId) {
		return ForumTopicIconFrame(colorId, title, st::infoForumTopicIcon);
	}) | rpl::start_with_next([=](QImage &&image) {
		_image = std::move(image);
		_update();
	}, _lifetime);
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::ForumTopic*> topic)
: TopicIconButton(parent, topic, [=] {
	return controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
}) {
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused)
: AbstractButton(parent)
, _view(topic, paused, [=] { update(); }) {
	resize(st::infoTopicCover.photo.size);
	paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(this);
		_view.paintInRect(p, rect());
	}, lifetime());
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Fn<not_null<QWidget*>()> parentForTooltip)
: Cover(
	parent,
	controller,
	peer,
	nullptr,
	Role::Info,
	NameValue(peer),
	parentForTooltip) {
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::ForumTopic*> topic)
: Cover(
	parent,
	controller,
	topic->channel(),
	topic,
	Role::Info,
	TitleValue(topic),
	nullptr) {
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Role role,
	rpl::producer<QString> title)
: Cover(
	parent,
	controller,
	peer,
	nullptr,
	role,
	std::move(title),
	nullptr) {
}

[[nodiscard]] rpl::producer<Badge::Content> BotVerifyBadgeForPeer(
		not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
	) | rpl::map([=] {
		const auto info = peer->botVerifyDetails();
		return Badge::Content{
			.badge = info ? BadgeType::BotVerified : BadgeType::None,
			.emojiStatusId = { info ? info->iconId : DocumentId() },
		};
	});
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Role role,
	rpl::producer<QString> title,
	Fn<not_null<QWidget*>()> parentForTooltip)
: FixedHeightWidget(parent, CoverStyle(peer, topic, role).height)
, _st(CoverStyle(peer, topic, role))
, _role(role)
, _controller(controller)
, _peer(peer)
, _emojiStatusPanel(peer->isSelf()
	? std::make_unique<EmojiStatusPanel>()
	: nullptr)
, _botVerify(
	std::make_unique<Badge>(
		this,
		st::infoPeerBadge,
		&peer->session(),
		BotVerifyBadgeForPeer(peer),
		nullptr,
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _badgeContent(BadgeContentForPeer(peer))
, _badge(
	std::make_unique<Badge>(
		this,
		st::infoPeerBadge,
		&peer->session(),
		_badgeContent.value(),
		_emojiStatusPanel.get(),
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _verified(
	std::make_unique<Badge>(
		this,
		st::infoPeerBadge,
		&peer->session(),
		VerifiedContentForPeer(peer),
		_emojiStatusPanel.get(),
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _parentForTooltip(std::move(parentForTooltip))
, _badgeTooltipHide([=] { hideBadgeTooltip(); })
, _userpic(topic
	? nullptr
	: object_ptr<Ui::UserpicButton>(
		this,
		controller,
		_peer,
		Ui::UserpicButton::Role::OpenPhoto,
		Ui::UserpicButton::Source::PeerPhoto,
		_st.photo))
, _changePersonal((role == Role::Info
	|| topic
	|| !_peer->isUser()
	|| _peer->isSelf()
	|| _peer->asUser()->isBot())
	? nullptr
	: CreateUploadSubButton(this, _peer->asUser(), controller).get())
, _iconButton(topic
	? object_ptr<TopicIconButton>(this, controller, topic)
	: nullptr)
, _name(this, _st.name)
, _status(this, _st.status)
, _showLastSeen(this, tr::lng_status_lastseen_when(), _st.showLastSeen)
, _refreshStatusTimer([this] { refreshStatusText(); }) {
	_peer->updateFull();

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	if (!_peer->isMegagroup()) {
		_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	setupShowLastSeen();

	_badge->setPremiumClickCallback([=] {
		if (const auto panel = _emojiStatusPanel.get()) {
			panel->show(_controller, _badge->widget(), _badge->sizeTag());
		} else {
			::Settings::ShowEmojiStatusPremium(_controller, _peer);
		}
	});
	rpl::merge(
		_botVerify->updated(),
		_badge->updated(),
		_verified->updated()
	) | rpl::start_with_next([=] {
		refreshNameGeometry(width());
	}, _name->lifetime());

	initViewers(std::move(title));
	setupChildGeometry();
	setupUniqueBadgeTooltip();

	if (_userpic) {
	} else if (topic->canEdit()) {
		_iconButton->setClickedCallback([=] {
			_controller->show(Box(
				EditForumTopicBox,
				_controller,
				topic->history(),
				topic->rootId()));
		});
	} else {
		_iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void Cover::setupShowLastSeen() {
	const auto user = _peer->asUser();
	if (_st.showLastSeenVisible
		&& user
		&& !user->isSelf()
		&& !user->isBot()
		&& !user->isServiceUser()
		&& user->session().premiumPossible()) {
		if (user->session().premium()) {
			if (user->lastseen().isHiddenByMe()) {
				user->updateFullForced();
			}
			_showLastSeen->hide();
			return;
		}

		rpl::combine(
			user->session().changes().peerFlagsValue(
				user,
				Data::PeerUpdate::Flag::OnlineStatus),
			Data::AmPremiumValue(&user->session())
		) | rpl::start_with_next([=](auto, bool premium) {
			const auto wasShown = !_showLastSeen->isHidden();
			const auto hiddenByMe = user->lastseen().isHiddenByMe();
			const auto shown = hiddenByMe
				&& !user->lastseen().isOnline(base::unixtime::now())
				&& !premium
				&& user->session().premiumPossible();
			_showLastSeen->setVisible(shown);
			if (wasShown && premium && hiddenByMe) {
				user->updateFullForced();
			}
		}, _showLastSeen->lifetime());

		_controller->session().api().userPrivacy().value(
			Api::UserPrivacy::Key::LastSeen
		) | rpl::filter([=](Api::UserPrivacy::Rule rule) {
			return (rule.option == Api::UserPrivacy::Option::Everyone);
		}) | rpl::start_with_next([=] {
			if (user->lastseen().isHiddenByMe()) {
				user->updateFullForced();
			}
		}, _showLastSeen->lifetime());
	} else {
		_showLastSeen->hide();
	}

	using TextTransform = Ui::RoundButton::TextTransform;
	_showLastSeen->setTextTransform(TextTransform::NoTransform);
	_showLastSeen->setFullRadius(true);

	_showLastSeen->setClickedCallback([=] {
		const auto type = Ui::ShowOrPremium::LastSeen;
		auto box = Box(Ui::ShowOrPremiumBox, type, user->shortName(), [=] {
			_controller->session().api().userPrivacy().save(
				::Api::UserPrivacy::Key::LastSeen,
				{});
		}, [=] {
			::Settings::ShowPremium(_controller, u"lastseen_hidden"_q);
		});
		_controller->show(std::move(box));
	});
}

void Cover::setupChildGeometry() {
	widthValue(
	) | rpl::start_with_next([this](int newWidth) {
		if (_userpic) {
			_userpic->moveToLeft(_st.photoLeft, _st.photoTop, newWidth);
		} else {
			_iconButton->moveToLeft(_st.photoLeft, _st.photoTop, newWidth);
		}
		if (_changePersonal) {
			_changePersonal->moveToLeft(
				(_st.photoLeft
					+ _st.photo.photoSize
					- _changePersonal->width()
					+ st::infoEditContactPersonalLeft),
				(_userpic->y()
					+ _userpic->height()
					- _changePersonal->height()));
		}
		refreshNameGeometry(newWidth);
		refreshStatusGeometry(newWidth);
	}, lifetime());
}

Cover *Cover::setOnlineCount(rpl::producer<int> &&count) {
	_onlineCount = std::move(count);
	return this;
}

std::optional<QImage> Cover::updatedPersonalPhoto() const {
	return _personalChosen;
}

void Cover::initViewers(rpl::producer<QString> title) {
	using Flag = Data::PeerUpdate::Flag;
	std::move(
		title
	) | rpl::start_with_next([=](const QString &title) {
		_name->setText(title);
		refreshNameGeometry(width());
	}, lifetime());

	rpl::combine(
		_peer->session().changes().peerFlagsValue(
			_peer,
			Flag::OnlineStatus | Flag::Members),
		_onlineCount.value()
	) | rpl::start_with_next([=] {
		refreshStatusText();
	}, lifetime());

	_peer->session().changes().peerFlagsValue(
		_peer,
		(_peer->isUser() ? Flag::IsContact : Flag::Rights)
	) | rpl::start_with_next([=] {
		refreshUploadPhotoOverlay();
	}, lifetime());

	setupChangePersonal();
}

void Cover::refreshUploadPhotoOverlay() {
	if (!_userpic) {
		return;
	} else if (_role == Role::EditContact) {
		_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		return;
	}

	const auto canChange = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		} else if (const auto user = _peer->asUser()) {
			return user->isSelf()
				|| (user->isContact()
					&& !user->isInaccessible()
					&& !user->isServiceUser());
		}
		Unexpected("Peer type in Info::Profile::Cover.");
	}();

	_userpic->switchChangePhotoOverlay(canChange, [=](
			Ui::UserpicButton::ChosenImage chosen) {
		using ChosenType = Ui::UserpicButton::ChosenType;
		auto result = Api::PeerPhoto::UserPhoto{
			base::take<QImage>(chosen.image), // Strange MSVC bug with take.
			chosen.markup.documentId,
			chosen.markup.colors,
		};
		switch (chosen.type) {
		case ChosenType::Set:
			_userpic->showCustom(base::duplicate(result.image));
			_peer->session().api().peerPhoto().upload(
				_peer,
				std::move(result));
			break;
		case ChosenType::Suggest:
			_peer->session().api().peerPhoto().suggest(
				_peer,
				std::move(result));
			break;
		}
	});

	const auto canReport = [=, peer = _peer] {
		if (!peer->hasUserpic()) {
			return false;
		}
		const auto user = peer->asUser();
		if (!user) {
			if (canChange) {
				return false;
			}
		} else if (user->hasPersonalPhoto()
				|| user->isSelf()
				|| user->isInaccessible()
				|| user->isRepliesChat()
				|| user->isVerifyCodes()
				|| (user->botInfo && user->botInfo->canEditInformation)
				|| user->isServiceUser()) {
			return false;
		}
		return true;
	};

	const auto contextMenu = _userpic->lifetime()
		.make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=, peer = _peer, controller = _controller](
			not_null<Ui::RpWidget*> parent) {
		if (!canReport()) {
			return false;
		}
		*contextMenu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::popupMenuWithIcons);
		contextMenu->get()->addAction(tr::lng_profile_report(tr::now), [=] {
			controller->show(
				ReportProfilePhotoBox(
					peer,
					peer->owner().photo(peer->userpicPhotoId())),
				Ui::LayerOption::CloseOther);
		}, &st::menuIconReport);
		contextMenu->get()->popup(QCursor::pos());
		return true;
	};
	base::install_event_filter(_userpic, [showMenu, raw = _userpic.data()](
			not_null<QEvent*> e) {
		return (e->type() == QEvent::ContextMenu && showMenu(raw))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});

	if (const auto user = _peer->asUser()) {
		_userpic->resetPersonalRequests(
		) | rpl::start_with_next([=] {
			user->session().api().peerPhoto().clearPersonal(user);
			_userpic->showSource(Ui::UserpicButton::Source::PeerPhoto);
		}, lifetime());
	}
}

void Cover::setupChangePersonal() {
	if (!_changePersonal) {
		return;
	}

	_changePersonal->chosenImages(
	) | rpl::start_with_next([=](Ui::UserpicButton::ChosenImage &&chosen) {
		if (chosen.type == Ui::UserpicButton::ChosenType::Suggest) {
			_peer->session().api().peerPhoto().suggest(
				_peer,
				{
					std::move(chosen.image),
					chosen.markup.documentId,
					chosen.markup.colors,
				});
		} else {
			_personalChosen = std::move(chosen.image);
			_userpic->showCustom(base::duplicate(*_personalChosen));
			_changePersonal->overrideHasPersonalPhoto(true);
			_changePersonal->showSource(
				Ui::UserpicButton::Source::NonPersonalIfHasPersonal);
		}
	}, _changePersonal->lifetime());

	_changePersonal->resetPersonalRequests(
	) | rpl::start_with_next([=] {
		_personalChosen = QImage();
		_userpic->showSource(
			Ui::UserpicButton::Source::NonPersonalPhoto);
		_changePersonal->overrideHasPersonalPhoto(false);
		_changePersonal->showCustom(QImage());
	}, _changePersonal->lifetime());
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
				? Ui::Text::Colorized(result)
				: TextWithEntities{ .text = result };
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return tr::lng_chat_status_unaccessible({}, WithEntities);
			}
			const auto onlineCount = _onlineCount.current();
			const auto fullCount = std::max(
				chat->count,
				int(chat->participants.size()));
			return { .text = ChatStatusText(fullCount, onlineCount, true) };
		} else if (auto channel = _peer->asChannel()) {
			const auto onlineCount = _onlineCount.current();
			const auto fullCount = qMax(channel->membersCount(), 1);
			auto result = ChatStatusText(
				fullCount,
				onlineCount,
				channel->isMegagroup());
			return hasMembersLink
				? Ui::Text::Link(result)
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
	base::take(_badgeTooltip);
	base::take(_badgeOldTooltips);
}

void Cover::refreshNameGeometry(int newWidth) {
	auto nameWidth = newWidth - _st.nameLeft - _st.rightSkip;
	const auto verifiedWidget = _verified->widget();
	const auto badgeWidget = _badge->widget();
	if (verifiedWidget) {
		nameWidth -= verifiedWidget->width();
	}
	if (badgeWidget) {
		nameWidth -= badgeWidget->width();
	}
	if (verifiedWidget || badgeWidget) {
		nameWidth -= st::infoVerifiedCheckPosition.x();
	}
	auto nameLeft = _st.nameLeft;
	const auto badgeTop = _st.nameTop;
	const auto badgeBottom = _st.nameTop + _name->height();
	const auto margins = LargeCustomEmojiMargins();

	_botVerify->move(nameLeft - margins.left(), badgeTop, badgeBottom);
	if (const auto widget = _botVerify->widget()) {
		const auto skip = widget->width()
			+ st::infoVerifiedCheckPosition.x();
		nameLeft += skip;
		nameWidth -= skip;
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, _st.nameTop, newWidth);
	const auto badgeLeft = nameLeft + _name->width();
	_badge->move(badgeLeft, badgeTop, badgeBottom);
	_verified->move(
		badgeLeft + (badgeWidget ? badgeWidget->width() : 0),
		badgeTop,
		badgeBottom);
}

void Cover::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth - _st.statusLeft - _st.rightSkip;
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(_st.statusLeft, _st.statusTop, newWidth);
	const auto left = _st.statusLeft + _status->textMaxWidth();
	_showLastSeen->moveToLeft(
		left + _st.showLastSeenPosition.x(),
		_st.showLastSeenPosition.y(),
		newWidth);
}

void Cover::hideBadgeTooltip() {
	_badgeTooltipHide.cancel();
	if (auto old = base::take(_badgeTooltip)) {
		const auto raw = old.get();
		_badgeOldTooltips.push_back(std::move(old));

		raw->fade(false);
		raw->shownValue(
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::start_with_next([=] {
			const auto i = ranges::find(
				_badgeOldTooltips,
				raw,
				&std::unique_ptr<BadgeTooltip>::get);
			if (i != end(_badgeOldTooltips)) {
				_badgeOldTooltips.erase(i);
			}
		}, raw->lifetime());
	}
}

void Cover::setupUniqueBadgeTooltip() {
	base::timer_once(kWaitBeforeGiftBadge) | rpl::then(
		_badge->updated()
	) | rpl::start_with_next([=] {
		const auto widget = _badge->widget();
		const auto &content = _badgeContent.current();
		const auto &collectible = content.emojiStatusId.collectible;
		const auto premium = (content.badge == BadgeType::Premium);
		const auto id = (collectible && widget && premium)
			? collectible->id
			: uint64();
		if (_badgeCollectibleId == id) {
			return;
		}
		hideBadgeTooltip();
		if (!collectible) {
			return;
		}
		const auto parent = _parentForTooltip
			? _parentForTooltip()
			: _controller->window().widget()->bodyWidget();
		_badgeTooltip = std::make_unique<BadgeTooltip>(
			parent,
			collectible,
			widget);
		const auto raw = _badgeTooltip.get();
		raw->fade(true);
		_badgeTooltipHide.callOnce(kGiftBadgeGlares * raw->glarePeriod()
			- st::infoGiftTooltip.duration * 1.5);
	}, lifetime());

	if (const auto raw = _badgeTooltip.get()) {
		raw->finishAnimating();
	}
}

} // namespace Info::Profile
