/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_color_box.h"

#include "apiwrap.h"
#include "api/api_peer_colors.h"
#include "api/api_peer_photo.h"
#include "base/unixtime.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/background_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/stickers_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document_media.h"
#include "data/data_emoji_statuses.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/profile/info_profile_top_bar.h"
#include "info/info_controller.h" // Key
#include "info/info_memento.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/boxes/boost_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/button_labels.h"
#include "ui/controls/sub_tabs.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/peer/color_sample.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info.h" // defaultSubTabs.
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace {

using namespace Settings;

constexpr auto kFakeChannelId = ChannelId(0xFFFFFFF000ULL);
constexpr auto kFakeWebPageId = WebPageId(0xFFFFFFFF00000000ULL);
constexpr auto kUnsetColorIndex = uint8(0xFF);

base::unique_qptr<Ui::RpWidget> CreateEmptyPlaceholder(
		not_null<Ui::RpWidget*> parent,
		int width,
		const QMargins &padding,
		Fn<void()> switchToNextTab) {
	const auto container = Ui::CreateChild<Ui::RpWidget>(parent);
	auto result = base::unique_qptr<Ui::RpWidget>{ container };

	auto icon = Settings::CreateLottieIcon(
		container,
		{
			.name = u"my_gifts_empty"_q,
			.sizeOverride = st::normalBoxLottieSize,
		},
		st::settingsBlockedListIconPadding);
	const auto iconWidget = icon.widget.data();
	iconWidget->show();

	const auto emptyLabel = Ui::CreateChild<Ui::FlatLabel>(
		container,
		tr::lng_gift_stars_tabs_my_empty(),
		st::giftBoxGiftEmptyLabel);
	emptyLabel->setTryMakeSimilarLines(true);
	emptyLabel->resizeToWidth(
		width - st::boxRowPadding.left() - st::boxRowPadding.right());
	emptyLabel->show();

	const auto emptyNextLabel = switchToNextTab
		? Ui::CreateChild<Ui::FlatLabel>(
			container,
			tr::lng_gift_stars_tabs_my_empty_next(
				lt_emoji,
				rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
				tr::link),
			st::giftBoxGiftEmptyLabel)
		: nullptr;
	if (emptyNextLabel) {
		emptyNextLabel->resizeToWidth(
			width - st::boxRowPadding.left() - st::boxRowPadding.right());
		emptyNextLabel->setClickHandlerFilter([=](auto...) {
			switchToNextTab();
			return false;
		});
	}

	icon.animate(anim::repeat::loop);

	const auto labelHeight = emptyLabel->height();
	const auto nextLabelHeight = emptyNextLabel
		? emptyNextLabel->height()
		: 0;
	const auto totalHeight = iconWidget->height()
		+ st::normalFont->height + labelHeight + nextLabelHeight
		+ (nextLabelHeight ? st::normalFont->height : 0)
		+ padding.top() + padding.bottom();
	container->resize(width, totalHeight);

	container->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto totalContentHeight = iconWidget->height()
			+ st::normalFont->height + emptyLabel->height()
			+ (emptyNextLabel
				? st::normalFont->height + emptyNextLabel->height()
				: 0);
		const auto iconY = (size.height() - totalContentHeight) / 2;
		iconWidget->move(
			(size.width() - iconWidget->width()) / 2,
			iconY);
		emptyLabel->move(
			(size.width() - emptyLabel->width()) / 2,
			iconY + iconWidget->height() + st::normalFont->height);
		if (emptyNextLabel) {
			emptyNextLabel->move(
				(size.width() - emptyNextLabel->width()) / 2,
				iconY + iconWidget->height() + st::normalFont->height
					+ emptyLabel->height() + st::normalFont->height);
		}
	}, container->lifetime());

	return result;
}

class PreviewDelegate final : public HistoryView::DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	HistoryView::Context elementContext() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme,
		not_null<PeerData*> peer,
		rpl::producer<uint8> colorIndexValue,
		rpl::producer<DocumentId> backgroundEmojiId,
		rpl::producer<std::optional<Ui::ColorCollectible>> colorCollectible);
	~PreviewWrap();

private:
	using Element = HistoryView::Element;

	void paintEvent(QPaintEvent *e) override;

	void initElements();

	const not_null<Ui::GenericBox*> _box;
	const not_null<PeerData*> _peer;
	const not_null<ChannelData*> _fake;
	const not_null<History*> _history;
	const not_null<WebPageData*> _webpage;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::shared_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	const not_null<HistoryItem*> _replyToItem;
	const not_null<HistoryItem*> _replyItem;
	std::unique_ptr<Element> _element;
	Ui::PeerUserpicView _userpic;
	QPoint _position;

};

class LevelBadge final : public Ui::RpWidget {
public:
	LevelBadge(
		not_null<QWidget*> parent,
		uint32 level,
		not_null<Main::Session*> session);

	void setMinimal(bool value);

private:
	void paintEvent(QPaintEvent *e) override;

	void updateText();

	const uint32 _level;
	const Ui::Text::MarkedContext _context;
	Ui::Text::String _text;
	bool _minimal = false;

};



PreviewWrap::PreviewWrap(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::ChatStyle> style,
	std::shared_ptr<Ui::ChatTheme> theme,
	not_null<PeerData*> peer,
	rpl::producer<uint8> colorIndexValue,
	rpl::producer<DocumentId> backgroundEmojiId,
	rpl::producer<std::optional<Ui::ColorCollectible>> colorCollectible)
: RpWidget(box)
, _box(box)
, _peer(peer)
, _fake(_peer->owner().channel(kFakeChannelId))
, _history(_fake->owner().history(_fake))
, _webpage(_peer->owner().webpage(
	kFakeWebPageId,
	WebPageType::Article,
	u"internal:peer-color-webpage-preview"_q,
	u"internal:peer-color-webpage-preview"_q,
	tr::lng_settings_color_link_name(tr::now),
	tr::lng_settings_color_link_title(tr::now),
	{ tr::lng_settings_color_link_description(tr::now) },
	nullptr, // photo
	nullptr, // document
	WebPageCollage(),
	nullptr, // iv
	nullptr, // stickerSet
	nullptr, // uniqueGift
	0, // duration
	QString(), // author
	false, // hasLargeMedia
	false, // photoIsVideoCover
	0)) // pendingTill
, _theme(theme)
, _style(style)
, _delegate(std::make_unique<PreviewDelegate>(box, _style.get(), [=] {
	update();
}))
, _replyToItem(_history->addNewLocalMessage({
	.id = _history->nextNonHistoryEntryId(),
	.flags = (MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId
		| MessageFlag::Post),
	.from = _fake->id,
	.date = base::unixtime::now(),
}, TextWithEntities{ _peer->isSelf()
	? tr::lng_settings_color_reply(tr::now)
	: tr::lng_settings_color_reply_channel(tr::now),
}, MTP_messageMediaEmpty()))
, _replyItem(_history->addNewLocalMessage({
	.id = _history->nextNonHistoryEntryId(),
	.flags = (MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId
		| MessageFlag::HasReplyInfo
		| MessageFlag::Post),
	.from = _fake->id,
	.replyTo = FullReplyTo{.messageId = _replyToItem->fullId() },
	.date = base::unixtime::now(),
}, TextWithEntities{ _peer->isSelf()
	? tr::lng_settings_color_text(tr::now)
	: tr::lng_settings_color_text_channel(tr::now),
}, MTP_messageMediaWebPage(
	MTP_flags(0),
	MTP_webPagePending(
		MTP_flags(0),
		MTP_long(_webpage->id),
		MTPstring(),
		MTP_int(0)))))
, _element(_replyItem->createView(_delegate.get()))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	_fake->setName(peer->name(), QString());
	std::move(colorIndexValue) | rpl::on_next([=](uint8 index) {
		if (index != kUnsetColorIndex) {
			_fake->changeColorIndex(index);
			update();
		}
	}, lifetime());
	std::move(backgroundEmojiId) | rpl::on_next([=](DocumentId id) {
		_fake->changeBackgroundEmojiId(id);
		update();
	}, lifetime());
	std::move(colorCollectible) | rpl::on_next([=](
			std::optional<Ui::ColorCollectible> &&collectible) {
		if (collectible) {
			_fake->changeColorCollectible(std::move(*collectible));
		} else {
			_fake->clearColorCollectible();
		}
		update();
	}, lifetime());

	const auto session = &_history->session();
	session->data().viewRepaintRequest(
	) | rpl::on_next([=](not_null<const Element*> view) {
		if (view == _element.get()) {
			update();
		}
	}, lifetime());

	initElements();
}

PreviewWrap::~PreviewWrap() {
	_element = nullptr;
	_replyItem->destroy();
	_replyToItem->destroy();
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();

	p.setClipRect(clip);
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(_box->width(), _box->window()->height()),
		clip);

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		clip,
		!window()->isActiveWindow());

	p.translate(_position);
	_element->draw(p, context);

	if (_element->displayFromPhoto()) {
		auto userpicBottom = height()
			- _element->marginBottom()
			- _element->marginTop();
		const auto userpicTop = userpicBottom - st::msgPhotoSize;
		_peer->paintUserpicLeft(
			p,
			_userpic,
			st::historyPhotoLeft,
			userpicTop,
			width(),
			st::msgPhotoSize);
	}
}

void PreviewWrap::initElements() {
	_element->initDimensions();

	widthValue(
	) | rpl::filter([=](int width) {
		return width > st::msgMinWidth;
	}) | rpl::on_next([=](int width) {
		const auto height = _position.y()
			+ _element->resizeGetHeight(width)
			+ st::msgMargin.top();
		resize(width, height);
	}, lifetime());
}

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(HistoryView::MakePathShiftGradient(st, update)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto PreviewDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

HistoryView::Context PreviewDelegate::elementContext() {
	return HistoryView::Context::AdminLog;
}

LevelBadge::LevelBadge(
	not_null<QWidget*> parent,
	uint32 level,
	not_null<Main::Session*> session)
: Ui::RpWidget(parent)
, _level(level) {
	updateText();
}

void LevelBadge::updateText() {
	auto text = Ui::Text::IconEmoji(&st::settingsLevelBadgeLock).append(' ');
	if (!_minimal) {
		text.append(tr::lng_boost_level(
			tr::now,
			lt_count,
			_level,
			tr::marked));
	} else {
		text.append(QString::number(_level));
	}
	const auto &st = st::settingsPremiumNewBadge.style;
	_text.setMarkedText(
		st,
		text,
		kMarkupTextOptions,
		Ui::Text::MarkedContext{ .repaint = [=] { update(); } });
	const auto &padding = st::settingsColorSamplePadding;
	QWidget::resize(
		_text.maxWidth() + rect::m::sum::h(padding),
		st.font->height + rect::m::sum::v(padding));
}

void LevelBadge::setMinimal(bool value) {
	if ((value != _minimal) && value) {
		_minimal = value;
		updateText();
		update();
	}
}

void LevelBadge::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto radius = height() / 2;
	p.setPen(Qt::NoPen);
	auto gradient = QLinearGradient(QPointF(0, 0), QPointF(width(), 0));
	gradient.setStops(Ui::Premium::ButtonGradientStops());
	p.setBrush(gradient);
	p.drawRoundedRect(rect(), radius, radius);

	p.setPen(st::premiumButtonFg);
	p.setBrush(Qt::NoBrush);

	const auto context = Ui::Text::PaintContext{
		.position = rect::m::pos::tl(st::settingsColorSamplePadding),
		.outerWidth = width(),
		.availableWidth = width(),
	};
	_text.draw(p, context);
}

struct SetValues {
	uint8 colorIndex = 0;
	DocumentId backgroundEmojiId = 0;
	std::optional<Ui::ColorCollectible> colorCollectible;
	EmojiStatusId statusId;
	TimeId statusUntil = 0;
	bool statusChanged = false;
	bool forProfile = false;
};
void Set(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SetValues values,
		bool showToast = true) {
	const auto wasIndex = values.forProfile
		? peer->colorProfileIndex().value_or(kUnsetColorIndex)
		: peer->colorIndex();
	const auto wasEmojiId = values.forProfile
		? peer->profileBackgroundEmojiId()
		: peer->backgroundEmojiId();
	const auto &wasColorCollectible = peer->colorCollectible();

	const auto setLocal = [=](
			uint8 index,
			DocumentId emojiId,
			std::optional<Ui::ColorCollectible> colorCollectible) {
		using UpdateFlag = Data::PeerUpdate::Flag;
		if (values.forProfile) {
			if (index == kUnsetColorIndex) {
				peer->clearColorProfileIndex();
			} else {
				peer->changeColorProfileIndex(index);
			}
			peer->changeProfileBackgroundEmojiId(emojiId);
		} else {
			if (index == kUnsetColorIndex) {
				peer->clearColorIndex();
			} else {
				peer->changeColorIndex(index);
			}
			if (colorCollectible) {
				peer->changeColorCollectible(*colorCollectible);
			} else {
				peer->clearColorCollectible();
			}
			peer->changeBackgroundEmojiId(emojiId);
		}
		peer->session().changes().peerUpdated(
			peer,
			(UpdateFlag::BackgroundEmoji
				| (values.forProfile
					? UpdateFlag::ColorProfile
					: UpdateFlag::Color)));
	};
	setLocal(
		values.colorIndex,
		values.backgroundEmojiId,
		values.colorCollectible);

	const auto done = [=] {
		if (showToast) {
			show->showToast(peer->isSelf()
				? (values.forProfile
					? tr::lng_settings_color_changed_profile(tr::now)
					: tr::lng_settings_color_changed(tr::now))
				: (values.forProfile
					? tr::lng_settings_color_changed_profile_channel(tr::now)
					: tr::lng_settings_color_changed_channel(tr::now)));
		}
	};
	const auto fail = [=](const MTP::Error &error) {
		const auto type = error.type();
		if (type != u"CHAT_NOT_MODIFIED"_q) {
			setLocal(wasIndex, wasEmojiId, wasColorCollectible
				? *wasColorCollectible
				: std::optional<Ui::ColorCollectible>());
			show->showToast(type);
		}
	};
	const auto send = [&](auto &&request) {
		peer->session().api().request(
			std::move(request)
		).done(done).fail(fail).send();
	};
	if (peer->isSelf()) {
		using Flag = MTPaccount_UpdateColor::Flag;
		using ColorFlag = MTPDpeerColor::Flag;
		send(MTPaccount_UpdateColor(
			MTP_flags((values.forProfile ? Flag::f_for_profile : Flag(0))
				| (((!values.forProfile && values.colorCollectible)
					|| (values.colorIndex != kUnsetColorIndex))
					? Flag::f_color
					: Flag(0))),
			((!values.forProfile && values.colorCollectible)
				? MTP_inputPeerColorCollectible(
					MTP_long(values.colorCollectible->collectibleId))
				: MTP_peerColor(
					MTP_flags(ColorFlag()
						| ColorFlag::f_color
						| (values.backgroundEmojiId
							? ColorFlag::f_background_emoji_id
							: ColorFlag(0))),
					MTP_int(values.colorIndex),
					MTP_long(values.backgroundEmojiId)))));
		if (values.statusChanged
			&& (values.statusId || peer->emojiStatusId())) {
			peer->owner().emojiStatuses().set(
				peer,
				values.statusId,
				values.statusUntil);
		}
	} else if (const auto channel = peer->asChannel()) {
		if (peer->isBroadcast()) {
			using Flag = MTPchannels_UpdateColor::Flag;
			send(MTPchannels_UpdateColor(
				MTP_flags((values.colorIndex != kUnsetColorIndex
						? Flag::f_color
						: Flag(0))
					| Flag::f_background_emoji_id
					| (values.forProfile ? Flag::f_for_profile : Flag(0))),
				channel->inputChannel(),
				MTP_int(values.colorIndex),
				MTP_long(values.backgroundEmojiId)));
		}
		if (values.statusChanged
			&& (values.statusId || peer->emojiStatusId())) {
			peer->owner().emojiStatuses().set(
				channel,
				values.statusId,
				values.statusUntil);
		}
	} else {
		Unexpected("Invalid peer type in Set(colorIndex).");
	}
}

bool ShowPremiumPreview(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	if (!peer->isSelf() || show->session().premium()) {
		return false;
	}
	if (const auto controller = show->resolveWindow()) {
		ShowPremiumPreviewBox(controller, PremiumFeature::PeerColors);
		return true;
	}
	return false;
}

void Apply(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SetValues values,
		Fn<void()> close,
		Fn<void()> cancel,
		bool showToast = true) {
	const auto currentColorIndex = values.forProfile
		? peer->colorProfileIndex().value_or(kUnsetColorIndex)
		: peer->colorIndex();
	const auto currentEmojiId = values.forProfile
		? peer->profileBackgroundEmojiId()
		: peer->backgroundEmojiId();

	const auto colorMatch = (currentColorIndex == values.colorIndex);
	const auto emojiMatch = (currentEmojiId == values.backgroundEmojiId);
	const auto collectibleMatch = values.forProfile
		|| ((!peer->colorCollectible() == !values.colorCollectible)
			&& (!peer->colorCollectible()
				|| (*peer->colorCollectible() == *values.colorCollectible)));

	if (colorMatch
		&& emojiMatch
		&& collectibleMatch
		&& !values.statusChanged) {
		close();
	} else if (peer->isSelf()) {
		Set(show, peer, values, showToast);
		close();
	} else {
		CheckBoostLevel(show, peer, [=](int level) {
			const auto peerColors = &peer->session().api().peerColors();
			const auto colorRequired = peerColors->requiredLevelFor(
				peer->id,
				values.colorIndex,
				peer->isMegagroup(),
				values.forProfile);
			const auto limits = Data::LevelLimits(&peer->session());
			const auto iconRequired = values.backgroundEmojiId
				? limits.channelBgIconLevelMin()
				: 0;
			const auto statusRequired = (values.statusChanged
				&& values.statusId)
				? limits.channelEmojiStatusLevelMin()
				: 0;
			const auto required = std::max({
				colorRequired,
				iconRequired,
				statusRequired,
			});
			if (level >= required) {
				Set(show, peer, values, showToast);
				close();
				return std::optional<Ui::AskBoostReason>();
			}
			const auto reason = [&]() -> Ui::AskBoostReason {
				if (level < statusRequired) {
					return { Ui::AskBoostEmojiStatus{
						statusRequired,
						peer->isMegagroup()
					} };
				} else if (level < iconRequired) {
					return { Ui::AskBoostChannelColor{ iconRequired } };
				}
				return { Ui::AskBoostChannelColor{ colorRequired } };
			}();
			return std::make_optional(reason);
		}, cancel);
	}
}



[[nodiscard]] auto ButtonStyleWithAddedPadding(
		not_null<Ui::RpWidget*> parent,
		const style::SettingsButton &basicSt,
		QMargins added) {
	const auto st = parent->lifetime().make_state<style::SettingsButton>(
		basicSt);
	st->padding += added;
	return st;
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateEmojiIconButton(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Ui::ChatStyle> style,
		not_null<PeerData*> peer,
		rpl::producer<uint8> colorIndexValue,
		rpl::producer<DocumentId> emojiIdValue,
		Fn<void(DocumentId)> emojiIdChosen,
		bool profileIndices) {
	const auto button = ButtonStyleWithRightEmoji(
		parent,
		tr::lng_settings_color_emoji_off(tr::now));
	auto result = Settings::CreateButtonWithIcon(
		parent,
		!profileIndices
			? tr::lng_settings_color_emoji()
			: peer->isChannel()
			? tr::lng_settings_color_profile_emoji_channel()
			: tr::lng_settings_color_profile_emoji(),
		*button.st,
		{ &st::menuBlueIconColorNames });
	const auto raw = result.data();

	const auto right = Ui::CreateChild<Ui::RpWidget>(raw);
	right->show();

	using namespace Info::Profile;
	struct State {
		EmojiStatusPanel panel;
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
		DocumentId emojiId = 0;
		uint8 index = 0;
	};
	const auto state = right->lifetime().make_state<State>();
	state->panel.someCustomChosen(
	) | rpl::on_next([=](EmojiStatusPanel::CustomChosen chosen) {
		emojiIdChosen(chosen.id.documentId);
	}, raw->lifetime());

	std::move(colorIndexValue) | rpl::on_next([=](uint8 index) {
		state->index = index;
		if (state->emoji) {
			right->update();
		}
	}, right->lifetime());

	const auto session = &show->session();
	const auto added = st::lineWidth * 2;
	std::move(emojiIdValue) | rpl::on_next([=](DocumentId emojiId) {
		state->emojiId = emojiId;
		state->emoji = emojiId
			? session->data().customEmojiManager().create(
				emojiId,
				[=] { right->update(); })
			: nullptr;
		right->resize(
			(emojiId ? button.emojiWidth : button.noneWidth) + button.added,
			right->height());
		right->update();
	}, right->lifetime());

	rpl::combine(
		raw->sizeValue(),
		right->widthValue()
	) | rpl::on_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::on_next([=] {
		if (state->panel.paintBadgeFrame(right)) {
			return;
		}
		auto p = QPainter(right);
		const auto height = right->height();
		if (state->emoji
			&& (state->index != kUnsetColorIndex || profileIndices)) {
			const auto profileSet = profileIndices
				? peer->session().api().peerColors().colorProfileFor(
					state->index)
				: std::nullopt;
			const auto textColor = profileSet && !profileSet->palette.empty()
				? profileSet->palette.front()
				: profileIndices
				? style->windowActiveTextFg()->c
				: style->coloredValues(false, state->index).name;
			state->emoji->paint(p, {
				.textColor = textColor,
				.position = QPoint(added, (height - button.emojiWidth) / 2),
				.internal = {
					.forceFirstFrame = true,
				},
			});
		} else {
			const auto &font = st::normalFont;
			p.setFont(font);
			p.setPen(style->windowActiveTextFg());
			p.drawText(
				QPoint(added, (height - font->height) / 2 + font->ascent),
				tr::lng_settings_color_emoji_off(tr::now));
		}
	}, right->lifetime());

	raw->setClickedCallback([=] {
		const auto customTextColor = [=] {
			if (state->index == kUnsetColorIndex) {
				return style->windowActiveTextFg()->c;
			}
			if (profileIndices) {
				const auto colorSet
					= peer->session().api().peerColors().colorProfileFor(
						state->index);
				if (colorSet && !colorSet->palette.empty()) {
					return colorSet->palette.front();
				}
			}
			return style->coloredValues(false, state->index).name;
		};
		const auto controller = show->resolveWindow();
		if (controller) {
			state->panel.show({
				.controller = controller,
				.button = right,
				.ensureAddedEmojiId = { state->emojiId },
				.customTextColor = customTextColor,
				.backgroundEmojiMode = true,
			});
		}
	});

	if (const auto channel = peer->asChannel()) {
		const auto limits = Data::LevelLimits(&channel->session());
		AddLevelBadge(
			profileIndices
				? limits.channelProfileBgIconLevelMin()
				: limits.channelBgIconLevelMin(),
			raw,
			right,
			channel,
			button.st->padding,
			tr::lng_settings_color_emoji());
	}

	return result;
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateEmojiStatusButton(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<ChannelData*> channel,
		rpl::producer<EmojiStatusId> statusIdValue,
		Fn<void(EmojiStatusId,TimeId)> statusIdChosen,
		bool group) {
	const auto button = ButtonStyleWithRightEmoji(
		parent,
		tr::lng_settings_color_emoji_off(tr::now));
	const auto &phrase = group
		? tr::lng_edit_channel_status_group
		: tr::lng_edit_channel_status;
	auto result = Settings::CreateButtonWithIcon(
		parent,
		phrase(),
		*button.st,
		{ &st::menuBlueIconEmojiStatus });
	const auto raw = result.data();

	const auto right = Ui::CreateChild<Ui::RpWidget>(raw);
	right->show();

	using namespace Info::Profile;
	struct State {
		EmojiStatusPanel panel;
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
		EmojiStatusId statusId;
	};
	const auto state = right->lifetime().make_state<State>();
	state->panel.someCustomChosen(
	) | rpl::on_next([=](EmojiStatusPanel::CustomChosen chosen) {
		statusIdChosen({ chosen.id }, chosen.until);
	}, raw->lifetime());

	const auto session = &show->session();
	std::move(statusIdValue) | rpl::on_next([=](EmojiStatusId id) {
		state->statusId = id;
		state->emoji = id
			? session->data().customEmojiManager().create(
				Data::EmojiStatusCustomId(id),
				[=] { right->update(); })
			: nullptr;
		right->resize(
			(id ? button.emojiWidth : button.noneWidth) + button.added,
			right->height());
		right->update();
	}, right->lifetime());

	rpl::combine(
		raw->sizeValue(),
		right->widthValue()
	) | rpl::on_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::on_next([=] {
		if (state->panel.paintBadgeFrame(right)) {
			return;
		}
		auto p = QPainter(right);
		const auto height = right->height();
		if (state->emoji) {
			state->emoji->paint(p, {
				.textColor = anim::color(
					st::stickerPanPremium1,
					st::stickerPanPremium2,
					0.5),
				.position = QPoint(
					button.added,
					(height - button.emojiWidth) / 2),
			});
		} else {
			const auto &font = st::normalFont;
			p.setFont(font);
			p.setPen(st::windowActiveTextFg);
			p.drawText(
				QPoint(
					button.added,
					(height - font->height) / 2 + font->ascent),
				tr::lng_settings_color_emoji_off(tr::now));
		}
	}, right->lifetime());

	raw->setClickedCallback([=] {
		const auto controller = show->resolveWindow();
		if (controller) {
			state->panel.show({
				.controller = controller,
				.button = right,
				.ensureAddedEmojiId = { state->statusId },
				.channelStatusMode = true,
			});
		}
	});

	const auto limits = Data::LevelLimits(&channel->session());
	AddLevelBadge(
		(group
			? limits.groupEmojiStatusLevelMin()
			: limits.channelEmojiStatusLevelMin()),
		raw,
		right,
		channel,
		button.st->padding,
		phrase());

	return result;
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateEmojiPackButton(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<ChannelData*> channel) {
	Expects(channel->mgInfo != nullptr);

	const auto button = ButtonStyleWithRightEmoji(
		parent,
		tr::lng_settings_color_emoji_off(tr::now));
	auto result = Settings::CreateButtonWithIcon(
		parent,
		tr::lng_group_emoji(),
		*button.st,
		{ &st::menuBlueIconEmojiPack });
	const auto raw = result.data();

	struct State {
		DocumentData *icon = nullptr;
		std::unique_ptr<Ui::Text::CustomEmoji> custom;
		QImage cache;
	};
	const auto state = parent->lifetime().make_state<State>();

	const auto right = Ui::CreateChild<Ui::RpWidget>(raw);
	right->show();
	right->resize(
		button.emojiWidth + button.added,
		right->height());

	rpl::combine(
		raw->sizeValue(),
		right->widthValue()
	) | rpl::on_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::filter([=] {
		return state->icon != nullptr;
	}) | rpl::on_next([=] {
		auto p = QPainter(right);
		const auto x = button.added;
		const auto y = (right->height() - button.emojiWidth) / 2;
		const auto active = right->window()->isActiveWindow();
		if (const auto emoji = state->icon) {
			if (!state->custom
				&& emoji->sticker()
				&& emoji->sticker()->setType == Data::StickersType::Emoji) {
				auto &manager = emoji->owner().customEmojiManager();
				state->custom = manager.create(
					emoji->id,
					[=] { right->update(); },
					{});
			}
			if (state->custom) {
				state->custom->paint(p, Ui::Text::CustomEmoji::Context{
					.textColor = st::windowFg->c,
					.now = crl::now(),
					.position = { x, y },
					.paused = !active,
				});
			}
		}
	}, right->lifetime());

	raw->setClickedCallback([=] {
		const auto isEmoji = true;
		show->showBox(Box<StickersBox>(show, channel, isEmoji));
	});

	channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::EmojiSet
	) | rpl::map([=]() -> rpl::producer<DocumentData*> {
		const auto id = channel->mgInfo->emojiSet.id;
		if (!id) {
			return rpl::single<DocumentData*>(nullptr);
		}
		const auto sets = &channel->owner().stickers().sets();
		auto wrapLoaded = [=](Data::StickersSets::const_iterator it) {
			return it->second->lookupThumbnailDocument();
		};
		const auto it = sets->find(id);
		if (it != sets->cend()
			&& !(it->second->flags & Data::StickersSetFlag::NotLoaded)) {
			return rpl::single(wrapLoaded(it));
		}
		return rpl::single<DocumentData*>(
			nullptr
		) | rpl::then(channel->owner().stickers().updated(
			Data::StickersType::Emoji
		) | rpl::filter([=] {
			const auto it = sets->find(id);
			return (it != sets->cend())
				&& !(it->second->flags & Data::StickersSetFlag::NotLoaded);
		}) | rpl::map([=] {
			return wrapLoaded(sets->find(id));
		}));
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](DocumentData *icon) {
		if (state->icon != icon) {
			state->icon = icon;
			state->custom = nullptr;
			right->update();
		}
	}, right->lifetime());

	AddLevelBadge(
		Data::LevelLimits(&channel->session()).groupEmojiStickersLevelMin(),
		raw,
		right,
		channel,
		button.st->padding,
		tr::lng_group_emoji());

	return result;
}

Fn<void()> AddColorGiftTabs(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		Fn<void(uint64 giftId)> chosen,
		bool profile) {
	using namespace Info::PeerGifts;

	struct State {
		rpl::variable<std::vector<Data::StarGift>> list;
		Ui::SubTabs *tabs = nullptr;
	};
	const auto state = container->lifetime().make_state<State>();

	GiftsStars(
		session,
		session->user()
	) | rpl::on_next([=](const std::vector<GiftTypeStars> &list) {
		auto filtered = std::vector<Data::StarGift>();
		for (const auto &gift : list) {
			if ((profile || gift.info.peerColorAvailable) && gift.resale) {
				filtered.push_back(gift.info);
			}
		}
		state->list = std::move(filtered);
	}, container->lifetime());

	state->list.value(
	) | rpl::on_next([=](const std::vector<Data::StarGift> &list) {
		auto tabs = std::vector<Ui::SubTabs::Tab>();
		tabs.push_back({
			.id = u"my"_q,
			.text = tr::lng_gift_stars_tabs_my(tr::now, tr::marked),
		});
		for (const auto &gift : list) {
			auto text = TextWithEntities();
			tabs.push_back({
				.id = QString::number(gift.id),
				.text = Data::SingleCustomEmoji(
					gift.document).append(' ').append(gift.resellTitle),
			});
		}
		const auto context = Core::TextContext({
			.session = session,
		});
		if (!state->tabs) {
			state->tabs = container->add(
				object_ptr<Ui::SubTabs>(
					container,
					st::defaultSubTabs,
					Ui::SubTabs::Options{
						.selected = u"my"_q,
						.centered = true,
					},
					std::move(tabs),
					context));

			state->tabs->activated(
			) | rpl::on_next([=](const QString &id) {
				state->tabs->setActiveTab(id);
				chosen(id.toULongLong());
			}, state->tabs->lifetime());
		} else {
			state->tabs->setTabs(std::move(tabs), context);
		}
		container->resizeToWidth(container->width());
	}, container->lifetime());

	return [=]() {
		const auto &list = state->list.current();
		if (!list.empty()) {
			if (state->tabs) {
				state->tabs->setActiveTab(QString::number(list.front().id));
			}
			chosen(list.front().id);
		}
	};
}

void AddGiftSelector(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		rpl::producer<uint64> showingGiftIdValue,
		Fn<void(std::shared_ptr<Data::UniqueGift> selected)> chosen,
		rpl::producer<uint64> selected,
		bool profile,
		rpl::producer<uint64> selectedGiftId = rpl::single(uint64(0)),
		Fn<void()> switchToNextTab = nullptr) {
	using namespace Info::PeerGifts;

	const auto raw = container->add(
		object_ptr<Ui::VisibleRangeWidget>(container));

	struct List {
		std::vector<GiftTypeStars> list;
		rpl::lifetime loading;
		QString offset;
		bool loaded = false;
	};
	struct State {
		std::optional<Delegate> delegate;
		rpl::variable<uint64> showingGiftId;
		base::flat_map<uint64, List> lists;
		List *current = nullptr;
		std::vector<bool> validated;
		std::vector<std::unique_ptr<GiftButton>> buttons;
		rpl::variable<Ui::VisibleRange> visibleRange;
		rpl::variable<uint64> selected;
		rpl::variable<uint64> selectedGiftId;
		int perRow = 1;
		base::unique_qptr<Ui::RpWidget> emptyPlaceholder;

		Fn<void()> loadMore;
		Fn<void()> resize;
		Fn<void()> rebuild;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->delegate.emplace(session, GiftButtonMode::Full);
	state->showingGiftId = std::move(showingGiftIdValue);
	state->selected = std::move(selected);
	state->selectedGiftId = std::move(selectedGiftId);
	const auto shadow = st::defaultDropdownMenu.wrap.shadow;
	const auto extend = shadow.extend;
	state->loadMore = [=] {
		const auto selfId = session->userPeerId();
		const auto shownGiftId = state->showingGiftId.current();
		if (state->current->loaded || state->current->loading) {
			return;
		} else if (shownGiftId) {
			state->current->loading = Data::ResaleGiftsSlice(
				session,
				shownGiftId,
				{},
				state->current->offset
			) | rpl::on_next([=](Data::ResaleGiftsDescriptor slice) {
				auto &entry = state->lists[shownGiftId];
				entry.loading.destroy();
				entry.offset = slice.offset;
				entry.loaded = entry.offset.isEmpty();
				if (state->showingGiftId.current() != shownGiftId) {
					return;
				}

				auto &list = state->current->list;
				for (const auto &gift : slice.list) {
					if (gift.unique && (profile || gift.unique->peerColor)) {
						list.push_back({
							.info = gift,
							.resale = true,
							.mine = (gift.unique->ownerId == selfId),
						});
					}
				}
				state->resize();
			});
		} else {
			state->current->loading = Data::MyUniqueGiftsSlice(
				session,
				Data::MyUniqueType::OwnedAndHosted,
				state->current->offset
			) | rpl::on_next([=](Data::MyGiftsDescriptor slice) {
				auto &entry = state->lists[shownGiftId];
				entry.loading.destroy();
				entry.offset = slice.offset;
				entry.loaded = entry.offset.isEmpty();
				if (state->showingGiftId.current() != shownGiftId) {
					return;
				}

				auto &list = state->current->list;
				for (const auto &gift : slice.list) {
					if (gift.info.unique
						&& (profile || gift.info.unique->peerColor)) {
						list.push_back({ .info = gift.info });
					}
				}
				state->resize();
			});
		}
	};
	state->rebuild = [=] {
		const auto shownGiftId = state->showingGiftId.current();
		const auto width = st::boxWideWidth;
		const auto padding = st::giftBoxPadding;
		const auto available = width - padding.left() - padding.right();
		const auto range = state->visibleRange.current();
		const auto count = int(state->current->list.size());

		auto &buttons = state->buttons;
		if (buttons.size() < count) {
			buttons.resize(count);
		}
		auto &validated = state->validated;
		validated.resize(count);

		auto x = padding.left();
		auto y = padding.top();
		const auto single = state->delegate->buttonSize();
		const auto perRow = state->perRow;
		const auto singlew = single.width() + st::giftBoxGiftSkip.x();
		const auto singleh = single.height() + st::giftBoxGiftSkip.y();
		const auto rowFrom = std::max(range.top - y, 0) / singleh;
		const auto rowTill = (std::max(range.bottom - y + st::giftBoxGiftSkip.y(), 0) + singleh - 1)
			/ singleh;
		Assert(rowTill >= rowFrom);
		const auto first = rowFrom * perRow;
		const auto last = std::min(rowTill * perRow, count);
		const auto selectedCollectibleId = state->selected.current();
		const auto selectedGiftId = state->selectedGiftId.current();
		auto checkedFrom = 0;
		auto checkedTill = int(buttons.size());
		const auto ensureButton = [&](int index) {
			auto &button = buttons[index];
			if (!button) {
				validated[index] = false;
				for (; checkedFrom != first; ++checkedFrom) {
					if (buttons[checkedFrom]) {
						button = std::move(buttons[checkedFrom]);
						break;
					}
				}
			}
			if (!button) {
				for (; checkedTill != last; ) {
					--checkedTill;
					if (buttons[checkedTill]) {
						button = std::move(buttons[checkedTill]);
						break;
					}
				}
			}
			if (!button) {
				const auto delegate = &*state->delegate;
				button = std::make_unique<GiftButton>(raw, delegate);
			}
			const auto raw = button.get();
			if (validated[index]) {
				return;
			}
			raw->show();
			validated[index] = true;
			const auto &gift = state->current->list[index];
			raw->setDescriptor({ gift }, shownGiftId
				? GiftButtonMode::Full
				: GiftButtonMode::Minimal);
			raw->setClickedCallback([=, unique = gift.info.unique] {
				chosen(unique);
			});
			raw->setGeometry(QRect(QPoint(x, y), single), extend);
			const auto isSelected = selectedCollectibleId
				? (gift.info.unique->id == selectedCollectibleId)
				: (gift.info.unique->id == selectedGiftId);
			raw->toggleSelected(
				isSelected,
				GiftSelectionMode::Inset,
				anim::type::instant);
		};
		y += rowFrom * singleh;
		for (auto row = rowFrom; row != rowTill; ++row) {
			for (auto col = 0; col != perRow; ++col) {
				const auto index = row * perRow + col;
				if (index >= count) {
					break;
				}
				const auto last = !((col + 1) % perRow);
				if (last) {
					x = padding.left() + available - single.width();
				}
				ensureButton(index);
				if (last) {
					x = padding.left();
					y += singleh;
				} else {
					x += singlew;
				}
			}
		}
		const auto till = std::min(int(buttons.size()), rowTill * perRow);
		for (auto i = count; i < till; ++i) {
			if (const auto button = buttons[i].get()) {
				button->hide();
			}
		}

		const auto find = [=](uint64 id) -> GiftButton* {
			if (!id) {
				return nullptr;
			}
			const auto count = int(state->current->list.size());
			for (auto i = 0; i != count; ++i) {
				const auto &gift = state->current->list[i];
				if (gift.info.unique->id == id) {
					return state->buttons[i].get();
				}
			}
			return nullptr;
		};

		state->selected.value(
		) | rpl::combine_previous() | rpl::on_next([=](
				uint64 wasCollectibleId,
				uint64 nowCollectibleId) {
			if (wasCollectibleId) {
				if (const auto button = find(wasCollectibleId)) {
					button->toggleSelected(false, GiftSelectionMode::Inset);
				}
			}
			if (nowCollectibleId) {
				if (const auto button = find(nowCollectibleId)) {
					button->toggleSelected(true, GiftSelectionMode::Inset);
				}
			}
		}, raw->lifetime());

		state->selectedGiftId.value(
		) | rpl::combine_previous() | rpl::on_next([=](
				uint64 wasGiftId,
				uint64 nowGiftId) {
			if (wasGiftId) {
				if (const auto button = find(wasGiftId)) {
					button->toggleSelected(false, GiftSelectionMode::Inset);
				}
			}
			if (nowGiftId) {
				if (const auto button = find(nowGiftId)) {
					button->toggleSelected(true, GiftSelectionMode::Inset);
				}
			}
		}, raw->lifetime());

		const auto page = range.bottom - range.top;
		if (page > 0 && range.bottom + page > raw->height()) {
			state->loadMore();
		}
	};

	const auto width = st::boxWideWidth;
	const auto padding = st::giftBoxPadding;
	const auto available = width - padding.left() - padding.right();
	state->perRow = available / state->delegate->buttonSize().width();

	state->resize = [=] {
		const auto count = int(state->current->list.size());
		state->validated.clear();

		if (count == 0 && state->showingGiftId.current() == 0) {
			if (!state->emptyPlaceholder) {
				state->emptyPlaceholder = CreateEmptyPlaceholder(
					raw,
					width,
					padding,
					switchToNextTab);
			}
			state->emptyPlaceholder->show();
			raw->resize(raw->width(), state->emptyPlaceholder->height());
			return;
		} else if (state->emptyPlaceholder) {
			state->emptyPlaceholder = nullptr;
		}

		const auto rows = (count + state->perRow - 1) / state->perRow;
		const auto height = padding.top()
			+ (rows * state->delegate->buttonSize().height())
			+ ((rows - 1) * st::giftBoxGiftSkip.y())
			+ padding.bottom();
		raw->resize(raw->width(), height);

		state->rebuild();
	};

	state->showingGiftId.value(
	) | rpl::on_next([=](uint64 showingId) {
		state->current = &state->lists[showingId];
		state->buttons.clear();
		if (state->emptyPlaceholder) {
			state->emptyPlaceholder = nullptr;
		}
		state->delegate.emplace(session, showingId
			? GiftButtonMode::Full
			: GiftButtonMode::Minimal);
		state->resize();
	}, raw->lifetime());

	state->visibleRange = raw->visibleRange();
	state->visibleRange.value(
	) | rpl::on_next(state->rebuild, raw->lifetime());
}

Fn<void(int)> CreateTabsWidget(
		not_null<Ui::VerticalLayout*> container,
		const std::vector<QString> &tabs,
		const std::vector<Fn<void()>> &callbacks) {
	struct State {
		int activeTab = 0;
		Ui::Animations::Simple animation;
		float64 animatedPosition = 0.;
		std::vector<int> tabWidths;
	};
	const auto tabsContainer = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding,
		style::al_top);
	const auto state = tabsContainer->lifetime().make_state<State>();
	const auto height = st::semiboldFont->height * 1.5;

	auto totalWidth = 0;
	state->tabWidths.reserve(tabs.size());
	for (const auto &text : tabs) {
		const auto width = st::semiboldFont->width(text) + height * 2;
		state->tabWidths.push_back(width);
		totalWidth += width;
	}

	tabsContainer->resize(totalWidth, height);
	tabsContainer->setMaximumWidth(tabsContainer->width());

	const auto switchTo = [=](int i) {
		if (state->activeTab != i && i >= 0 && i < state->tabWidths.size()) {
			auto targetPosition = 0.;
			for (auto j = 0; j < i; ++j) {
				targetPosition += state->tabWidths[j];
			}
			state->animation.stop();
			state->animation.start(
				[=](float64 v) {
					state->animatedPosition = v;
					tabsContainer->update();
				},
				state->animatedPosition,
				targetPosition,
				400,
				anim::easeOutQuint);
			state->activeTab = i;
		}
		if (i < callbacks.size() && callbacks[i]) {
			callbacks[i]();
		}
	};

	auto left = 0;
	for (auto i = 0; i < tabs.size(); ++i) {
		const auto tabButton = Ui::CreateChild<Ui::AbstractButton>(
			tabsContainer);
		tabButton->setGeometry(left, 0, state->tabWidths[i], height);
		tabButton->setClickedCallback([=] { switchTo(i); });
		left += state->tabWidths[i];
	}

	const auto penWidth = st::lineWidth * 2;

	tabsContainer->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(tabsContainer);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = tabsContainer->rect();
		auto pen = QPen(st::giftBoxTabBgActive);
		pen.setWidthF(penWidth);
		p.setPen(pen);
		const auto halfPen = penWidth / 2;
		p.drawRoundedRect(
			QRectF(
				halfPen,
				halfPen,
				r.width() - penWidth,
				r.height() - penWidth),
			height / 2,
			height / 2);
		p.setFont(st::semiboldFont);

		const auto animatedLeft = state->animatedPosition;
		const auto activeWidth = state->tabWidths[state->activeTab];
		p.setBrush(st::giftBoxTabBgActive);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(
			QRect(animatedLeft, 0, activeWidth, height),
			height / 2,
			height / 2);

		auto left = 0;
		for (auto i = 0; i < tabs.size(); ++i) {
			auto textPen = QPen(state->activeTab == i
				? st::giftBoxTabFgActive
				: st::giftBoxTabFg);
			textPen.setWidthF(penWidth);
			p.setPen(textPen);
			p.drawText(
				QRect(left, 0, state->tabWidths[i], height),
				tabs[i],
				style::al_center);
			left += state->tabWidths[i];
		}
	}, tabsContainer->lifetime());

	state->animatedPosition = 0.;

	return switchTo;
}

not_null<Info::Profile::TopBar*> CreateProfilePreview(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	const auto preview = container->add(
		object_ptr<Info::Profile::TopBar>(
			container,
			Info::Profile::TopBar::Descriptor {
				.controller = show->resolveWindow(),
				.key = Info::Key(peer),
				.wrap = rpl::single(Info::Wrap::Side),
				.source = Info::Profile::TopBar::Source::Preview,
				.peer = peer,
				.backToggles = rpl::single(false),
				.showFinished = box->showFinishes(),
			}
		));
	preview->resize(
		container->width(),
		st::infoProfileTopBarNoActionsHeightMax);
	preview->setAttribute(Qt::WA_TransparentForMouseEvents);
	return preview;
}

void ProcessButton(not_null<Ui::RoundButton*> button) {
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	// Raise to be above right emoji from buttons.
	crl::on_main(button, [=] { button->raise(); });
}

void CreateBoostLevelContainer(
		not_null<Ui::VerticalLayout*> container,
		int levelHint,
		rpl::producer<std::optional<QColor>> colorProducer,
		Fn<void()> callback) {
	const auto boostLevelContainer = container->add(
		object_ptr<Ui::RpWidget>(container));
	boostLevelContainer->resize(
		0,
		st::infoProfileTopBarBoostFooter.style.font->height * 1.5);

	struct State {
		base::unique_qptr<Ui::FlatLabel> label;
		std::optional<QColor> currentColor;
	};
	const auto state = boostLevelContainer->lifetime().make_state<State>();

	boostLevelContainer->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(boostLevelContainer);
		const auto bg = state->currentColor.value_or(st::boxDividerBg->c);
		p.fillRect(boostLevelContainer->rect(), bg);
		p.fillRect(boostLevelContainer->rect(), st::shadowFg);
	}, boostLevelContainer->lifetime());

	std::move(colorProducer) | rpl::on_next([=](
			std::optional<QColor> color) {
		const auto colorChanged = (state->currentColor != color)
			|| !state->label;
		state->currentColor = color;
		boostLevelContainer->update();

		if (colorChanged) {
			const auto &style = color
				? st::infoProfileTopBarBoostFooterColored
				: st::infoProfileTopBarBoostFooter;
			state->label = base::make_unique_q<Ui::FlatLabel>(
				boostLevelContainer,
				tr::lng_settings_color_group_boost_footer(
					lt_count,
					rpl::single(levelHint) | tr::to_count(),
					lt_link,
					tr::lng_settings_color_group_boost_footer_link(
					) | rpl::map([=](QString t) {
						using namespace Ui::Text;
						return Link(std::move(t), u"internal:"_q);
					}),
					tr::rich),
				style);
			state->label->show();
			boostLevelContainer->sizeValue(
			) | rpl::on_next([=](QSize s) {
				state->label->moveToLeft(
					(s.width() - state->label->width()) / 2,
					(s.height() - state->label->height()) / 2);
			}, state->label->lifetime());
			state->label->setClickHandlerFilter([=](auto...) {
				callback();
				return false;
			});
		}
	}, boostLevelContainer->lifetime());
}

} // namespace

void AddLevelBadge(
		int level,
		not_null<Ui::SettingsButton*> button,
		Ui::RpWidget *right,
		not_null<ChannelData*> channel,
		const QMargins &padding,
		rpl::producer<QString> text) {
	if (channel->levelHint() >= level) {
		return;
	}
	const auto badge = Ui::CreateChild<LevelBadge>(
		button.get(),
		level,
		&channel->session());
	badge->show();
	const auto sampleLeft = st::settingsColorSamplePadding.left();
	const auto badgeLeft = padding.left() + sampleLeft;
	rpl::combine(
		button->sizeValue(),
		std::move(text)
	) | rpl::on_next([=](const QSize &s, const QString &) {
		if (s.isNull()) {
			return;
		}
		badge->moveToLeft(
			button->fullTextWidth() + badgeLeft,
			(s.height() - badge->height()) / 2);
		const auto rightEdge = right ? right->pos().x() : button->width();
		badge->setMinimal((rect::right(badge) + sampleLeft) > rightEdge);
		badge->setVisible((rect::right(badge) + sampleLeft) < rightEdge);
	}, badge->lifetime());
}

void EditPeerColorSection(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::RoundButton*> button,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme) {
	ProcessButton(button);
	const auto group = peer->isMegagroup();

	struct State {
		rpl::variable<uint8> index;
		rpl::variable<DocumentId> emojiId;
		rpl::variable<EmojiStatusId> statusId;
		rpl::variable<std::optional<Ui::ColorCollectible>> collectible;
		rpl::variable<uint64> showingGiftId;
		rpl::variable<uint8> profileIndex;
		rpl::variable<DocumentId> profileEmojiId;
		std::shared_ptr<Data::UniqueGift> buyCollectible;
		Info::Profile::TopBar *preview = nullptr;
		TimeId statusUntil = 0;
		bool statusChanged = false;
		bool changing = false;
		bool applying = false;
	};
	const auto state = button->lifetime().make_state<State>();
	state->index = peer->colorCollectible()
		? kUnsetColorIndex
		: peer->colorIndex();
	state->emojiId = peer->backgroundEmojiId();
	state->statusId = peer->emojiStatusId();
	state->collectible = peer->colorCollectible()
		? *peer->colorCollectible()
		: std::optional<Ui::ColorCollectible>();
	state->profileIndex = peer->colorProfileIndex().value_or(
		kUnsetColorIndex);
	state->profileEmojiId = peer->profileBackgroundEmojiId();

	const auto appendProfileSettings = [=](
			not_null<Ui::VerticalLayout*> container,
			not_null<ChannelData*> channel) {
		state->preview = CreateProfilePreview(box, container, show, peer);
		if (state->profileIndex.current() != kUnsetColorIndex) {
			state->preview->setColorProfileIndex(
				state->profileIndex.current());
		}
		if (state->profileEmojiId.current()) {
			state->preview->setPatternEmojiId(
				state->profileEmojiId.current());
		}
		state->statusId.value() | rpl::on_next([=](EmojiStatusId id) {
			state->preview->setLocalEmojiStatusId(std::move(id));
		}, state->preview->lifetime());
		const auto peerColors = &peer->session().api().peerColors();
		const auto profileIndices = peerColors->profileColorIndices();

		if (group) {
			auto colorProducer = state->profileIndex.value(
			) | rpl::map([=, colors = &peer->session().api().peerColors()](
					uint8 index) {
				const auto colorSet = colors->colorProfileFor(index);
				return (colorSet && !colorSet->bg.empty())
					? std::make_optional(colorSet->bg.front())
					: std::optional<QColor>();
			});
			CreateBoostLevelContainer(
				container,
				channel->levelHint(),
				std::move(colorProducer),
				[=] {
					if (const auto strong = show->resolveWindow()) {
						strong->resolveBoostState(channel);
					}
				});
		}

		const auto profileMargin = st::settingsColorRadioMargin;
		const auto profileSkip = st::settingsColorRadioSkip;
		const auto selector = container->add(
			object_ptr<Ui::ColorSelector>(
				container,
				profileIndices,
				state->profileIndex.current(),
				[=](uint8 index) {
					state->profileIndex = index;
					if (state->preview) {
						state->preview->setColorProfileIndex(
							index == kUnsetColorIndex
								? std::nullopt
								: std::make_optional(index));
					}
				},
				[=](uint8 index) {
					return peerColors->colorProfileFor(index).value_or(
						Data::ColorProfileSet{});
				}),
			{ profileMargin, profileSkip, profileMargin, profileSkip });

		container->add(CreateEmojiIconButton(
			container,
			show,
			style,
			peer,
			state->profileIndex.value(),
			state->profileEmojiId.value(),
			[=](DocumentId id) {
				state->profileEmojiId = id;
				if (state->preview) {
					state->preview->setPatternEmojiId(id);
				}
			},
			true));

		state->profileIndex.value(
		) | rpl::on_next([=](uint8 index) {
			selector->updateSelection(index);
		}, selector->lifetime());

		Ui::AddSkip(container, st::settingsColorSampleSkip);

		const auto resetWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto resetInner = resetWrap->entity();
		const auto resetButton = resetInner->add(
			object_ptr<Ui::SettingsButton>(
				resetInner,
				tr::lng_settings_color_reset(),
				st::settingsButtonLightNoIcon));
		Ui::AddSkip(resetInner, st::settingsColorSampleSkip);
		resetButton->setClickedCallback([=] {
			state->profileIndex = kUnsetColorIndex;
			state->profileEmojiId = 0;
			if (state->preview) {
				state->preview->setColorProfileIndex(std::nullopt);
				state->preview->setPatternEmojiId(0);
			}
			resetWrap->toggle(false, anim::type::normal);
		});

		resetWrap->toggleOn(state->profileIndex.value(
		) | rpl::map(rpl::mappers::_1 != kUnsetColorIndex));
		resetWrap->finishAnimating();

		Ui::AddDividerText(
			container,
			group
				? tr::lng_settings_color_choose_group()
				: tr::lng_settings_color_choose_channel());
		Ui::AddSkip(container, st::settingsColorSampleSkip);

		container->add(CreateEmojiStatusButton(
			container,
			show,
			channel,
			state->statusId.value(),
			[=](EmojiStatusId id, TimeId until) {
				state->statusId = id;
				state->statusUntil = until;
				state->statusChanged = true;
			},
			group));
		Ui::AddSkip(container, st::settingsColorSampleSkip);
		Ui::AddDividerText(
			container,
			(group
				? tr::lng_edit_channel_status_about_group()
				: tr::lng_edit_channel_status_about()),
			st::peerAppearanceDividerTextMargin);
	};

	if (group) {
		appendProfileSettings(container, peer->asChannel());
	} else {
		container->add(object_ptr<PreviewWrap>(
			box,
			style,
			theme,
			peer,
			state->index.value(),
			state->emojiId.value(),
			state->collectible.value()));

		auto indices = peer->session().api().peerColors().suggestedValue();
		const auto margin = st::settingsColorRadioMargin;
		const auto skip = st::settingsColorRadioSkip;
		container->add(
			object_ptr<Ui::ColorSelector>(
				container,
				style,
				std::move(indices),
				state->index.value(),
				[=](uint8 index) {
					if (state->collectible.current()) {
						state->buyCollectible = nullptr;
						state->collectible = std::nullopt;
						state->emojiId = 0;
					}
					state->index = index;
				}),
			{ margin, skip, margin, skip });

		Ui::AddDividerText(
			container,
			(peer->isSelf()
				? tr::lng_settings_color_about()
				: tr::lng_settings_color_about_channel()),
			st::peerAppearanceDividerTextMargin);

		const auto iconWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto iconInner = iconWrap->entity();

		Ui::AddSkip(iconInner, st::settingsColorSampleSkip);
		iconInner->add(CreateEmojiIconButton(
			iconInner,
			show,
			style,
			peer,
			state->index.value(),
			state->emojiId.value(),
			[=](DocumentId id) { state->emojiId = id; },
			false));

		Ui::AddSkip(iconInner, st::settingsColorSampleSkip);
		Ui::AddDividerText(
			iconInner,
			(peer->isSelf()
				? tr::lng_settings_color_emoji_about()
				: tr::lng_settings_color_emoji_about_channel()),
			st::peerAppearanceDividerTextMargin);

		iconWrap->toggleOn(state->collectible.value(
		) | rpl::map([](const std::optional<Ui::ColorCollectible> &value) {
			return !value.has_value();
		}));
		iconWrap->finishAnimating();
	}

	if (const auto channel = peer->asChannel()) {
		Ui::AddSkip(container, st::settingsColorSampleSkip);
		const auto &phrase = group
			? tr::lng_edit_channel_wallpaper_group
			: tr::lng_edit_channel_wallpaper;
		const auto button = Settings::AddButtonWithIcon(
			container,
			phrase(),
			st::peerAppearanceButton,
			{ &st::menuBlueIconWallpaper }
		);
		button->setClickedCallback([=] {
			if (const auto strong = show->resolveWindow()) {
				show->show(Box<BackgroundBox>(strong, channel));
			}
		});

		{
			const auto limits = Data::LevelLimits(&channel->session());
			AddLevelBadge(
				group
					? limits.groupCustomWallpaperLevelMin()
					: limits.channelCustomWallpaperLevelMin(),
				button,
				nullptr,
				channel,
				st::peerAppearanceButton.padding,
				phrase());
		}

		Ui::AddSkip(container, st::settingsColorSampleSkip);
		Ui::AddDividerText(
			container,
			(group
				? tr::lng_edit_channel_wallpaper_about_group()
				: tr::lng_edit_channel_wallpaper_about()),
			st::peerAppearanceDividerTextMargin);

		if (group) {
			Ui::AddSkip(container, st::settingsColorSampleSkip);

			container->add(CreateEmojiPackButton(
				container,
				show,
				channel));

			Ui::AddSkip(container, st::settingsColorSampleSkip);
			Ui::AddDividerText(
				container,
				tr::lng_group_emoji_description(),
				st::peerAppearanceDividerTextMargin);
		}

		// Preload exceptions list.
		const auto peerPhoto = &channel->session().api().peerPhoto();
		[[maybe_unused]] auto list = peerPhoto->emojiListValue(
			Api::PeerPhoto::EmojiListType::NoChannelStatus
		);

		const auto statuses = &channel->owner().emojiStatuses();
		statuses->refreshChannelDefault();
		statuses->refreshChannelColored();

		if (!state->preview) {
			Ui::AddSkip(container);
			appendProfileSettings(container, channel);
		}
	} else if (peer->isSelf()) {
		Ui::AddSkip(container, st::settingsColorSampleSkip);

		const auto session = &peer->session();
		const auto switchToNextTab = AddColorGiftTabs(
			container,
			session,
			[=](uint64 giftId) { state->showingGiftId = giftId; },
			false);

		auto showingGiftId = state->showingGiftId.value();
		AddGiftSelector(
			container,
			session,
			std::move(showingGiftId),
			[=](std::shared_ptr<Data::UniqueGift> selected) {
				state->index = selected->peerColor ? kUnsetColorIndex : 0;
				state->emojiId = selected->peerColor
					? selected->peerColor->backgroundEmojiId
					: 0;
				state->buyCollectible = (selected->peerColor
					&& (selected->ownerId != session->userPeerId())
					&& selected->starsForResale > 0)
					? selected
					: nullptr;
				state->collectible = selected->peerColor
					? *selected->peerColor
					: std::optional<Ui::ColorCollectible>();
			},
			state->collectible.value() | rpl::map([](
					const std::optional<Ui::ColorCollectible> &value) {
				return value ? value->collectibleId : 0;
			}),
			false,
			rpl::single(uint64(0)),
			switchToNextTab);
	}

	button->setClickedCallback([=] {
		if (state->applying) {
			return;
		} else if (ShowPremiumPreview(show, peer)) {
			return;
		}
		const auto values = SetValues{
			state->index.current(),
			state->emojiId.current(),
			state->collectible.current(),
			state->statusId.current(),
			state->statusUntil,
			state->statusChanged,
		};
		const auto profileValues = SetValues{
			.colorIndex = state->profileIndex.current(),
			.backgroundEmojiId = state->profileEmojiId.current(),
			.colorCollectible = state->collectible.current(),
			.statusId = {},
			.statusUntil = 0,
			.statusChanged = false,
			.forProfile = true,
		};
		if (const auto buy = state->buyCollectible) {
			const auto done = [=, weak = base::make_weak(box)](bool ok) {
				if (ok) {
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
					Apply(show, peer, values, [] {}, [] {});
				}
			};
			const auto to = peer->session().user();
			ShowBuyResaleGiftBox(show, buy, false, to, done);
			return;
		}
		state->applying = true;
		if (peer->isChannel()) {
			// First request: regular color data (without toast)
			Apply(show, peer, values, [=] {
				// Second request: profile color data (with toast)
				Apply(show, peer, profileValues, crl::guard(box, [=] {
					box->closeBox();
				}), crl::guard(box, [=] {
					state->applying = false;
				}), true);
			}, crl::guard(box, [=] {
				state->applying = false;
			}), false);
			return;
		}
		Apply(show, peer, values, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=] {
			state->applying = false;
		}));
	});
	state->collectible.value(
	) | rpl::on_next([=] {
		const auto buy = state->buyCollectible.get();
		while (!button->children().isEmpty()) {
			delete button->children().first();
		}
		if (!buy) {
			button->setText(rpl::combine(
				tr::lng_settings_color_apply(),
				Data::AmPremiumValue(&peer->session())
			) | rpl::map([=](const QString &text, bool premium) {
				auto result = TextWithEntities();
				if (!premium && peer->isSelf()) {
					result.append(Ui::Text::IconEmoji(&st::giftBoxLock));
				}
				result.append(text);
				return result;
			}));
		} else if (buy->onlyAcceptTon) {
			button->setText(rpl::single(QString()));
			Ui::SetButtonTwoLabels(
				button,
				tr::lng_gift_buy_resale_button(
					lt_cost,
					rpl::single(Data::FormatGiftResaleTon(*buy)),
					tr::marked),
				tr::lng_gift_buy_resale_equals(
					lt_cost,
					rpl::single(Ui::Text::IconEmoji(
						&st::starIconEmojiSmall
					).append(Lang::FormatCountDecimal(buy->starsForResale))),
					tr::marked),
				st::resaleButtonTitle,
				st::resaleButtonSubtitle);
		} else {
			button->setText(tr::lng_gift_buy_resale_button(
				lt_cost,
				rpl::single(Ui::Text::IconEmoji(&st::starIconEmoji).append(
					Lang::FormatCountDecimal(buy->starsForResale))),
				tr::marked));
		}
	}, button->lifetime());
}

void EditPeerProfileColorSection(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::RoundButton*> button,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme,
		Fn<void()> aboutCallback) {
	Expects(peer->isSelf());

	ProcessButton(button);

	const auto preview = CreateProfilePreview(box, container, show, peer);

	const auto peerColors = &peer->session().api().peerColors();
	const auto indices = peerColors->profileColorIndices();

	struct State {
		rpl::variable<uint8> index = kUnsetColorIndex;
		rpl::variable<DocumentId> patternEmojiId;
		rpl::variable<EmojiStatusId> wearable;
		rpl::variable<uint64> showingGiftId;
		rpl::variable<uint64> selectedGiftId;
		std::shared_ptr<Data::UniqueGift> buyCollectible;
		Ui::ColorSelector *selector = nullptr;
	};
	const auto state = button->lifetime().make_state<State>();
	state->patternEmojiId = peer->profileBackgroundEmojiId();
	state->wearable = peer->emojiStatusId();

	const auto resetUnique = [=] {
		preview->setLocalEmojiStatusId({});
		state->buyCollectible = nullptr;
		state->wearable = {};
	};

	const auto setIndex = [=](uint8 index) {
		state->index = index;
		if (index != kUnsetColorIndex) {
			resetUnique();
		}
		preview->setColorProfileIndex(index == kUnsetColorIndex
			? std::nullopt
			: std::make_optional(index));
		preview->setPatternEmojiId(index == kUnsetColorIndex
			? std::nullopt
			: std::make_optional(state->patternEmojiId.current()));
	};
	setIndex(peer->colorProfileIndex().value_or(kUnsetColorIndex));

	const auto margin = st::settingsColorRadioMargin;
	const auto skip = st::settingsColorRadioSkip;
	state->selector = container->add(
		object_ptr<Ui::ColorSelector>(
			box,
			indices,
			state->index.current(),
			setIndex,
			[=](uint8 index) {
				return peerColors->colorProfileFor(index).value_or(
					Data::ColorProfileSet{});
			}),
		{ margin, skip, margin, skip });

	Ui::AddSkip(container, st::settingsColorSampleSkip);
	container->add(CreateEmojiIconButton(
		container,
		show,
		style,
		peer,
		state->index.value(),
		state->patternEmojiId.value(),
		[=](DocumentId id) {
			state->patternEmojiId = id;
			preview->setPatternEmojiId(id);
			resetUnique();
		},
		true));

	const auto resetWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto resetInner = resetWrap->entity();

	Ui::AddSkip(resetInner, st::settingsColorSampleSkip);
	const auto resetButton = resetInner->add(
		object_ptr<Ui::SettingsButton>(
			resetInner,
			tr::lng_settings_color_reset(),
			st::settingsButtonLightNoIcon));
	resetButton->setClickedCallback([=] {
		state->index = kUnsetColorIndex;
		state->patternEmojiId = 0;
		preview->setColorProfileIndex(std::nullopt);
		preview->setPatternEmojiId(0);
		resetUnique();
		resetWrap->toggle(false, anim::type::normal);
	});

	resetWrap->toggleOn(state->index.value(
	) | rpl::map([](uint8 index) { return index != kUnsetColorIndex; }));
	resetWrap->finishAnimating();

	Ui::AddSkip(container, st::settingsColorSampleSkip);
	const auto about = Ui::AddDividerText(
		container,
		tr::lng_settings_color_profile_about(
			lt_link,
			tr::lng_settings_color_profile_about_link(
				lt_emoji,
				rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
				tr::rich
			) | rpl::map([=](TextWithEntities t) {
				return tr::link(std::move(t), u"internal:"_q);
			}),
			tr::rich));
	Ui::AddSkip(container, st::settingsColorSampleSkip);
	about->setClickHandlerFilter([=](auto...) {
		aboutCallback();
		return false;
	});

	state->index.value(
	) | rpl::on_next([=](uint8 index) {
		if (state->selector) {
			state->selector->updateSelection(index);
		}
	}, button->lifetime());

	if (peer->isSelf()) {
		Ui::AddSkip(container, st::settingsColorSampleSkip);

		const auto session = &peer->session();
		const auto switchToNextTab = AddColorGiftTabs(
			container,
			session,
			[=](uint64 giftId) { state->showingGiftId = giftId; },
			true);

		auto showingGiftId = state->showingGiftId.value();
		AddGiftSelector(
			container,
			session,
			std::move(showingGiftId),
			[=](std::shared_ptr<Data::UniqueGift> selected) {
				state->selectedGiftId = selected->id;
				state->index = kUnsetColorIndex;
				state->patternEmojiId = 0;
				state->buyCollectible = (selected->peerColor
					&& (selected->ownerId != session->userPeerId())
					&& selected->starsForResale > 0)
					? selected
					: nullptr;
				const auto statuses = &peer->owner().emojiStatuses();
				state->wearable = statuses->fromUniqueGift(*selected);
				preview->setColorProfileIndex(std::nullopt);
				preview->setPatternEmojiId(selected->pattern.document->id);
				preview->setLocalEmojiStatusId(state->wearable.current());
				resetWrap->toggle(true, anim::type::normal);
			},
			state->wearable.value() | rpl::map([=](const EmojiStatusId &value) {
				return value.collectible ? value.collectible->id : 0;
			}),
			true,
			state->selectedGiftId.value(),
			switchToNextTab);
	}

	struct ProfileState {
		bool applying = false;
	};
	const auto profileState = button->lifetime().make_state<ProfileState>();

	button->setClickedCallback([=] {
		if (profileState->applying) {
			return;
		} else if (ShowPremiumPreview(show, peer)) {
			return;
		}
		const auto statusId = peer->emojiStatusId();
		const auto wearable = state->wearable.current();
		const auto statusChanged = wearable.collectible
			? (!statusId.collectible
				|| statusId.collectible->id != wearable.collectible->id)
			: (statusId.collectible != nullptr);
		const auto values = SetValues{
			.colorIndex = state->index.current(),
			.backgroundEmojiId = state->patternEmojiId.current(),
			.colorCollectible = std::nullopt,
			.statusId = state->wearable.current(),
			.statusUntil = 0,
			.statusChanged = statusChanged,
			.forProfile = true,
		};
		if (const auto buy = state->buyCollectible) {
			const auto done = [=, weak = base::make_weak(box)](bool ok) {
				if (ok) {
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
					Apply(show, peer, values, [] {}, [] {});
				}
			};
			const auto to = peer->session().user();
			ShowBuyResaleGiftBox(show, buy, false, to, done);
			return;
		}
		profileState->applying = true;
		Apply(show, peer, values, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=] {
			profileState->applying = false;
		}));
	});
	state->wearable.value(
	) | rpl::on_next([=](EmojiStatusId id) {
		const auto buy = state->buyCollectible.get();
		while (!button->children().isEmpty()) {
			delete button->children().first();
		}
		if (!buy) {
			button->setText(rpl::combine(
				(id.collectible
					? tr::lng_settings_color_wear()
					: tr::lng_settings_color_apply()),
				Data::AmPremiumValue(&peer->session())
			) | rpl::map([=](const QString &text, bool premium) {
				auto result = TextWithEntities();
				if (!premium && peer->isSelf()) {
					result.append(Ui::Text::IconEmoji(&st::giftBoxLock));
				}
				result.append(text);
				return result;
			}));
		} else if (buy->onlyAcceptTon) {
			button->setText(rpl::single(QString()));
			Ui::SetButtonTwoLabels(
				button,
				tr::lng_gift_buy_resale_button(
					lt_cost,
					rpl::single(Data::FormatGiftResaleTon(*buy)),
					tr::marked),
				tr::lng_gift_buy_resale_equals(
					lt_cost,
					rpl::single(Ui::Text::IconEmoji(
						&st::starIconEmojiSmall
					).append(Lang::FormatCountDecimal(buy->starsForResale))),
					tr::marked),
				st::resaleButtonTitle,
				st::resaleButtonSubtitle);
		} else {
			button->setText(tr::lng_gift_buy_resale_button(
				lt_cost,
				rpl::single(Ui::Text::IconEmoji(&st::starIconEmoji).append(
					Lang::FormatCountDecimal(buy->starsForResale))),
				tr::marked));
		}
	}, button->lifetime());
}

void EditPeerColorBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme) {
	box->setTitle(peer->isSelf()
		? tr::lng_settings_color_title()
		: tr::lng_edit_channel_color());
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giftBox);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});
	if (peer->isChannel()) {
		const auto button = box->addButton(
			tr::lng_settings_color_apply(),
			[] {});
		EditPeerColorSection(box, box->verticalLayout(), button, show, peer, style, theme);
		return;
	}
	const auto buttonContainer = box->addButton(
		rpl::single(QString()),
		[] {});
	const auto content = box->verticalLayout();

	const auto profileButton = Ui::CreateChild<Ui::RoundButton>(
		buttonContainer,
		tr::lng_settings_color_apply(),
		box->getDelegate()->style().button);
	const auto nameButton = Ui::CreateChild<Ui::RoundButton>(
		buttonContainer,
		tr::lng_settings_color_apply(),
		box->getDelegate()->style().button);
	rpl::combine(
		buttonContainer->widthValue(),
		profileButton->sizeValue(),
		nameButton->sizeValue()
	) | rpl::on_next([=](int w, QSize, QSize) {
		profileButton->resizeToWidth(w);
		nameButton->resizeToWidth(w);
	}, buttonContainer->lifetime());

	auto nameOwned = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		content,
		object_ptr<Ui::VerticalLayout>(content));
	auto profileOwned = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		content,
		object_ptr<Ui::VerticalLayout>(content));
	const auto nameWrap = nameOwned.get();
	const auto profileWrap = profileOwned.get();
	const auto name = nameWrap->entity();
	const auto profile = profileWrap->entity();

	const auto showName = [=] {
		nameWrap->toggle(true, anim::type::instant);
		profileWrap->toggle(false, anim::type::instant);
		profileButton->hide();
		nameButton->show();
	};

	const auto switchTab = CreateTabsWidget(
		content,
		{
			tr::lng_settings_color_tab_profile(tr::now),
			tr::lng_settings_color_tab_name(tr::now),
		},
		{
			[=] {
				nameWrap->toggle(false, anim::type::instant);
				profileWrap->toggle(true, anim::type::instant);
				nameButton->hide();
				profileButton->show();
			},
			showName,
		});

	Ui::AddSkip(content);
	nameWrap->toggle(false, anim::type::instant);
	profileWrap->toggle(true, anim::type::instant);
	nameButton->hide();
	content->add(std::move(profileOwned));
	content->add(std::move(nameOwned));

	EditPeerProfileColorSection(
		box,
		profile,
		profileButton,
		show,
		peer,
		style,
		theme,
		[=] { switchTab(1); });

	EditPeerColorSection(box, name, nameButton, show, peer, style, theme);
}

void SetupPeerColorSample(
		not_null<Button*> button,
		not_null<PeerData*> peer,
		rpl::producer<QString> label,
		std::shared_ptr<Ui::ChatStyle> style) {
	auto colorIndexValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Color
	) | rpl::map([=] {
		return peer->colorIndex();
	});
	auto colorCollectibleValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Color
	) | rpl::map([=] {
		return peer->colorCollectible();
	});
	auto colorProfileIndexValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::ColorProfile
	) | rpl::map([=] {
		return peer->colorProfileIndex();
	});
	auto emojiStatusIdValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::map([=] {
		return peer->emojiStatusId();
	});
	const auto name = peer->shortName();

	const auto sampleSize = st::settingsColorSampleSize;

	const auto sample = Ui::CreateChild<Ui::ColorSample>(
		button.get(),
		[=] { return Core::TextContext({ .session = &peer->session() }); },
		[=](auto id) { return Data::SingleCustomEmoji(id); },
		style,
		rpl::duplicate(colorIndexValue),
		rpl::duplicate(colorCollectibleValue),
		name);
	sample->show();

	struct ProfileSampleState {
		Data::ColorProfileSet colorSet;
	};
	const auto profileState
		= button->lifetime().make_state<ProfileSampleState>();

	const auto profileSample = Ui::CreateChild<Ui::ColorSample>(
		button.get(),
		[=](uint8 index) { return profileState->colorSet; },
		0,
		false);
	profileSample->hide();
	profileSample->resize(sampleSize, sampleSize);

	const auto emojiStatusWidget = Ui::CreateChild<Ui::RpWidget>(
		button.get());
	emojiStatusWidget->hide();
	emojiStatusWidget->resize(sampleSize, sampleSize);
	button->lifetime().make_state<std::unique_ptr<Ui::Text::CustomEmoji>>();

	struct EmojiStatusState {
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
	};
	const auto emojiState = button->lifetime().make_state<EmojiStatusState>();

	rpl::combine(
		button->widthValue(),
		rpl::duplicate(label),
		rpl::duplicate(colorIndexValue),
		rpl::duplicate(colorProfileIndexValue),
		rpl::duplicate(emojiStatusIdValue)
	) | rpl::on_next([=](
			int width,
			const QString &buttonText,
			int colorIndex,
			std::optional<uint8> profileIndex,
			EmojiStatusId emojiStatusId) {
		const auto available = width
			- st::settingsButton.padding.left()
			- (st::settingsColorButton.padding.right() - sampleSize)
			- st::settingsButton.style.font->width(buttonText)
			- st::settingsButtonRightSkip;

		const auto hasEmojiStatus = emojiStatusId
			&& emojiStatusId.collectible;
		const auto hasProfile = profileIndex.has_value() || hasEmojiStatus;

		if (hasEmojiStatus && emojiStatusId.collectible) {
			const auto color = emojiStatusId.collectible->centerColor;
			profileState->colorSet.palette = { color };
			profileState->colorSet.bg = { color };
			profileState->colorSet.story = { color };
		} else if (hasProfile) {
			const auto peerColors = &peer->session().api().peerColors();
			profileState->colorSet
				= peerColors->colorProfileFor(peer).value_or(
					Data::ColorProfileSet{});
		}

		profileSample->setVisible(hasProfile);
		emojiStatusWidget->setVisible(hasEmojiStatus);

		if (hasEmojiStatus && !emojiState->emoji) {
			emojiState->emoji
				= peer->session().data().customEmojiManager().create(
					Data::EmojiStatusCustomId(emojiStatusId),
					[raw = emojiStatusWidget] { raw->update(); },
					Data::CustomEmojiSizeTag::Normal);
		} else if (!hasEmojiStatus) {
			emojiState->emoji = nullptr;
		}

		sample->setForceCircle(hasProfile);
		if (style->colorPatternIndex(colorIndex) || hasProfile) {
			sample->resize(sampleSize, sampleSize);
		} else {
			const auto padding = st::settingsColorSamplePadding;
			const auto wantedHeight = padding.top()
				+ st::semiboldFont->height
				+ padding.bottom();
			const auto wantedWidth = sample->naturalWidth();
			sample->resize(std::min(wantedWidth, available), wantedHeight);
		}
		sample->update();
		sample->setCutoutPadding(hasProfile
			? st::settingsColorSampleCutout
			: 0);
		profileSample->update();
		emojiStatusWidget->update();
	}, sample->lifetime());

	rpl::combine(
		button->sizeValue(),
		sample->sizeValue(),
		rpl::duplicate(colorIndexValue),
		rpl::duplicate(colorProfileIndexValue),
		rpl::duplicate(emojiStatusIdValue)
	) | rpl::on_next([=](
			QSize outer,
			QSize inner,
			int colorIndex,
			std::optional<uint8> profileIndex,
			EmojiStatusId emojiStatusId) {
		const auto hasColor = (colorIndex != 0);

		const auto right = st::settingsColorButton.padding.right()
			- st::settingsColorSampleSkip
			- st::settingsColorSampleSize
			- (style->colorPatternIndex(colorIndex)
				? 0
				: st::settingsColorSamplePadding.right());
		sample->move(
			outer.width() - right - inner.width(),
			(outer.height() - inner.height()) / 2);
		const auto profilePos = sample->pos()
			+ (hasColor
				? QPoint(st::settingsColorProfileSampleShift
					- st::settingsColorSampleSize
					- st::lineWidth, 0)
				: QPoint());
		profileSample->move(profilePos);
		emojiStatusWidget->move(profilePos);
	}, sample->lifetime());

	constexpr auto kScale = 0.7;
	emojiStatusWidget->paintOn([=](QPainter &p) {
		if (!emojiState->emoji) {
			return;
		}
		const auto size = emojiStatusWidget->size();
		const auto offset = (size * (1.0 - kScale)) / 2.0;
		p.translate(offset.width(), offset.height());
		p.scale(kScale, kScale);
		emojiState->emoji->paint(p, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
		});
	});

	sample->setAttribute(Qt::WA_TransparentForMouseEvents);
	profileSample->setAttribute(Qt::WA_TransparentForMouseEvents);
	emojiStatusWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void AddPeerColorButton(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const style::SettingsButton &st) {
	auto label = peer->isSelf()
		? tr::lng_settings_theme_name_color()
		: tr::lng_edit_channel_color();
	const auto button = AddButtonWithIcon(
		container,
		rpl::duplicate(label),
		st,
		{ &st::menuIconChangeColors });

	const auto style = std::make_shared<Ui::ChatStyle>(
		peer->session().colorIndicesValue());
	const auto theme = std::shared_ptr<Ui::ChatTheme>(
		Window::Theme::DefaultChatThemeOn(button->lifetime()));
	style->apply(theme.get());

	if (!peer->isMegagroup()) {
		SetupPeerColorSample(button, peer, rpl::duplicate(label), style);
	}

	{
		const auto badge = Ui::NewBadge::CreateNewBadge(
			button,
			tr::lng_premium_summary_new_badge()).get();
		rpl::combine(
			rpl::duplicate(label),
			button->widthValue()
		) | rpl::on_next([=](
				const QString &text,
				int width) {
			const auto space = st.style.font->spacew;
			const auto left = st.padding.left()
				+ st.style.font->width(text)
				+ space;
			const auto available = width - left - st.padding.right();
			badge->setVisible(available >= badge->width());
			if (!badge->isHidden()) {
				const auto top = st.padding.top()
					+ st.style.font->ascent
					- st::settingsPremiumNewBadge.style.font->ascent
					- st::settingsPremiumNewBadgePadding.top();
				badge->moveToLeft(left, top, width);
			}
		}, badge->lifetime());
	}

	button->setClickedCallback([=] {
		show->show(Box(EditPeerColorBox, show, peer, style, theme));
	});
}

void CheckBoostLevel(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		Fn<std::optional<Ui::AskBoostReason>(int level)> askMore,
		Fn<void()> cancel) {
	peer->session().api().request(MTPpremium_GetBoostsStatus(
		peer->input()
	)).done([=](const MTPpremium_BoostsStatus &result) {
		const auto &data = result.data();
		if (const auto channel = peer->asChannel()) {
			channel->updateLevelHint(data.vlevel().v);
		}
		const auto reason = askMore(data.vlevel().v);
		if (!reason) {
			return;
		}
		const auto openStatistics = [=] {
			if (const auto controller = show->resolveWindow()) {
				controller->showSection(Info::Boosts::Make(peer));
			}
		};
		auto counters = ParseBoostCounters(result);
		counters.mine = 0; // Don't show current level as just-reached.
		show->show(Box(Ui::AskBoostBox, Ui::AskBoostBoxData{
			.link = qs(data.vboost_url()),
			.boost = counters,
			.features = (peer->isChannel()
				? LookupBoostFeatures(peer->asChannel())
				: Ui::BoostFeatures()),
			.reason = *reason,
			.group = !peer->isBroadcast(),
		}, openStatistics, nullptr));
		cancel();
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		cancel();
	}).send();
}

ButtonWithEmoji ButtonStyleWithRightEmoji(
		not_null<Ui::RpWidget*> parent,
		const QString &noneString,
		const style::SettingsButton &parentSt) {
	const auto ratio = style::DevicePixelRatio();
	const auto emojiWidth = Data::FrameSizeFromTag({}) / ratio;

	const auto noneWidth = st::normalFont->width(noneString);

	const auto added = st::normalFont->spacew;
	const auto rightAdded = std::max(noneWidth, emojiWidth);
	return {
		.st = ButtonStyleWithAddedPadding(
			parent,
			parentSt,
			QMargins(0, 0, added + rightAdded, 0)),
		.emojiWidth = emojiWidth,
		.noneWidth = noneWidth,
		.added = added,
	};
}
