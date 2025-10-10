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
constexpr auto kSelectAnimationDuration = crl::time(150);
constexpr auto kUnsetColorIndex = uint8(0xFF);

class ColorSample final : public Ui::AbstractButton {
public:
	ColorSample(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::ChatStyle> style,
		rpl::producer<uint8> colorIndex,
		rpl::producer<std::shared_ptr<Ui::ColorCollectible>> collectible,
		const QString &name);
	ColorSample(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::ChatStyle> style,
		uint8 colorIndex,
		bool selected);

	[[nodiscard]] uint8 index() const;

	void setSelected(bool selected);

private:
	void paintEvent(QPaintEvent *e) override;

	std::shared_ptr<Ui::ChatStyle> _style;
	Ui::Text::String _name;
	uint8 _index = 0;
	std::shared_ptr<Ui::ColorCollectible> _collectible;
	Ui::Animations::Simple _selectAnimation;
	bool _selected = false;
	bool _simple = false;

};

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

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::ChatStyle> style,
	rpl::producer<uint8> colorIndex,
	rpl::producer<std::shared_ptr<Ui::ColorCollectible>> collectible,
	const QString &name)
: AbstractButton(parent)
, _style(style) {
	rpl::combine(
		std::move(colorIndex),
		std::move(collectible)
	) | rpl::start_with_next([=](
			uint8 index,
			std::shared_ptr<Ui::ColorCollectible> collectible) {
		_index = index;
		_collectible = std::move(collectible);
		if (const auto raw = _collectible.get()) {
			_name.setMarkedText(
				st::semiboldTextStyle,
				Data::SingleCustomEmoji(raw->giftEmojiId),
				kMarkupTextOptions,
				Core::TextContext({
					.session = session,
					.repaint = [=] { update(); },
				}));
		} else {
			_name.setText(st::semiboldTextStyle, name);
		}
		setNaturalWidth([&] {
			if (_name.isEmpty() || _style->colorPatternIndex(_index)) {
				return st::settingsColorSampleSize;
			}
			const auto padding = st::settingsColorSamplePadding;
			return std::max(
				padding.left() + _name.maxWidth() + padding.right(),
				padding.top() + st::semiboldFont->height + padding.bottom());
		}());
		update();
	}, lifetime());
}

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::ChatStyle> style,
	uint8 colorIndex,
	bool selected)
: AbstractButton(parent)
, _style(style)
, _index(colorIndex)
, _selected(selected)
, _simple(true) {
	setNaturalWidth(st::settingsColorSampleSize);
}

void ColorSample::setSelected(bool selected) {
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	_selectAnimation.start(
		[=] { update(); },
		_selected ? 0. : 1.,
		_selected ? 1. : 0.,
		kSelectAnimationDuration);
}

void ColorSample::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = _style->coloredValues(false, _index);
	if (!_simple && !colors.outlines[1].alpha()) {
		const auto radius = height() / 2;
		p.setPen(Qt::NoPen);
		if (const auto raw = _collectible.get()) {
			const auto withBg = [&](const QColor &color) {
				return Ui::CountContrast(st::windowBg->c, color);
			};
			const auto dark = (withBg({ 0, 0, 0 })
				< withBg({ 255, 255, 255 }));
			const auto name = (dark && raw->darkAccentColor.alpha() > 0)
				? raw->darkAccentColor
				: raw->accentColor;
			auto bg = name;
			bg.setAlpha(0.12 * 255);
			p.setBrush(bg);
		} else {
			p.setBrush(colors.bg);
		}
		p.drawRoundedRect(rect(), radius, radius);

		const auto padding = st::settingsColorSamplePadding;
		p.setPen(colors.name);
		p.setBrush(Qt::NoBrush);
		p.setFont(st::semiboldFont);
		_name.drawLeftElided(
			p,
			padding.left(),
			padding.top(),
			width() - padding.left() - padding.right(),
			width(),
			1,
			style::al_top);
	} else {
		const auto size = float64(width());
		const auto half = size / 2.;
		const auto full = QRectF(-half, -half, size, size);
		p.translate(size / 2., size / 2.);
		p.setPen(Qt::NoPen);
		if (colors.outlines[1].alpha()) {
			p.rotate(-45.);
			p.setClipRect(-size, 0, 3 * size, size);
			p.setBrush(colors.outlines[1]);
			p.drawEllipse(full);
			p.setClipRect(-size, -size, 3 * size, size);
		}
		p.setBrush(colors.outlines[0]);
		p.drawEllipse(full);
		p.setClipping(false);
		if (colors.outlines[2].alpha()) {
			const auto multiplier = size / st::settingsColorSampleSize;
			const auto center = st::settingsColorSampleCenter * multiplier;
			const auto radius = st::settingsColorSampleCenterRadius
				* multiplier;
			p.setBrush(colors.outlines[2]);
			p.drawRoundedRect(
				QRectF(-center / 2., -center / 2., center, center),
				radius,
				radius);
		}
		const auto selected = _selectAnimation.value(_selected ? 1. : 0.);
		if (selected > 0) {
			const auto line = st::settingsColorRadioStroke * 1.;
			const auto thickness = selected * line;
			auto pen = st::boxBg->p;
			pen.setWidthF(thickness);
			p.setBrush(Qt::NoBrush);
			p.setPen(pen);
			const auto skip = 1.5 * line;
			p.drawEllipse(full.marginsRemoved({ skip, skip, skip, skip }));
		}
	}
}

uint8 ColorSample::index() const {
	return _index;
}

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
	std::move(colorIndexValue) | rpl::start_with_next([=](uint8 index) {
		if (index != kUnsetColorIndex) {
			_fake->changeColorIndex(index);
			update();
		}
	}, lifetime());
	std::move(backgroundEmojiId) | rpl::start_with_next([=](DocumentId id) {
		_fake->changeBackgroundEmojiId(id);
		update();
	}, lifetime());
	std::move(colorCollectible) | rpl::start_with_next([=](
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
	) | rpl::start_with_next([=](not_null<const Element*> view) {
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
	}) | rpl::start_with_next([=](int width) {
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
			Ui::Text::WithEntities));
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
};
void Set(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SetValues values) {
	const auto wasIndex = peer->colorIndex();
	const auto wasEmojiId = peer->backgroundEmojiId();
	const auto &wasColorCollectible = peer->colorCollectible();

	const auto setLocal = [=](
			uint8 index,
			DocumentId emojiId,
			std::optional<Ui::ColorCollectible> colorCollectible) {
		using UpdateFlag = Data::PeerUpdate::Flag;
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
		peer->session().changes().peerUpdated(
			peer,
			UpdateFlag::Color | UpdateFlag::BackgroundEmoji);
	};
	setLocal(
		values.colorIndex,
		values.backgroundEmojiId,
		values.colorCollectible);

	const auto done = [=] {
		show->showToast(peer->isSelf()
			? tr::lng_settings_color_changed(tr::now)
			: tr::lng_settings_color_changed_channel(tr::now));
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
			MTP_flags(Flag::f_color),
			(values.colorCollectible
				? MTP_inputPeerColorCollectible(
					MTP_long(values.colorCollectible->collectibleId))
				: MTP_peerColor(
					MTP_flags(ColorFlag()
						| ColorFlag::f_color
						| ColorFlag::f_background_emoji_id),
					MTP_int(values.colorIndex),
					MTP_long(values.backgroundEmojiId)))));
	} else if (const auto channel = peer->asChannel()) {
		if (peer->isBroadcast()) {
			using Flag = MTPchannels_UpdateColor::Flag;
			send(MTPchannels_UpdateColor(
				MTP_flags(Flag::f_color | Flag::f_background_emoji_id),
				channel->inputChannel,
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

bool ShowPremiumToast(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	if (!peer->isSelf() || show->session().premium()) {
		return false;
	}
	Settings::ShowPremiumPromoToast(
		show,
		tr::lng_settings_color_subscribe(
			tr::now,
			lt_link,
			Ui::Text::Link(
				Ui::Text::Bold(
					tr::lng_send_as_premium_required_link(tr::now))),
			Ui::Text::WithEntities),
		u"name_color"_q);
	return true;
}

void Apply(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SetValues values,
		Fn<void()> close,
		Fn<void()> cancel) {
	if (peer->colorIndex() == values.colorIndex
		&& peer->backgroundEmojiId() == values.backgroundEmojiId
		&& (!peer->colorCollectible() == !values.colorCollectible)
		&& (!peer->colorCollectible()
			|| (*peer->colorCollectible() == *values.colorCollectible))
		&& !values.statusChanged) {
		close();
	} else if (peer->isSelf()) {
		Set(show, peer, values);
		close();
	} else {
		CheckBoostLevel(show, peer, [=](int level) {
			const auto peerColors = &peer->session().api().peerColors();
			const auto colorRequired = peer->isMegagroup()
				? peerColors->requiredGroupLevelFor(
					peer->id,
					values.colorIndex)
				: peerColors->requiredChannelLevelFor(
					peer->id,
					values.colorIndex);
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
				Set(show, peer, values);
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

class ColorSelector final : public Ui::RpWidget {
public:
	ColorSelector(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::ChatStyle> style,
		rpl::producer<std::vector<uint8>> indices,
		rpl::producer<uint8> index,
		Fn<void(uint8)> callback);

private:
	void fillFrom(std::vector<uint8> indices);

	int resizeGetHeight(int newWidth) override;

	const std::shared_ptr<Ui::ChatStyle> _style;
	std::vector<std::unique_ptr<ColorSample>> _samples;
	const Fn<void(uint8)> _callback;
	rpl::variable<uint8> _index;

};

ColorSelector::ColorSelector(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::ChatStyle> style,
	rpl::producer<std::vector<uint8>> indices,
	rpl::producer<uint8> index,
	Fn<void(uint8)> callback)
: RpWidget(box)
, _style(style)
, _callback(std::move(callback))
, _index(std::move(index)) {
	std::move(
		indices
	) | rpl::start_with_next([=](std::vector<uint8> indices) {
		fillFrom(std::move(indices));
	}, lifetime());
}

void ColorSelector::fillFrom(std::vector<uint8> indices) {
	auto samples = std::vector<std::unique_ptr<ColorSample>>();
	const auto initial = _index.current();
	const auto add = [&](uint8 index) {
		auto i = ranges::find(_samples, index, &ColorSample::index);
		if (i != end(_samples)) {
			samples.push_back(std::move(*i));
			_samples.erase(i);
		} else {
			samples.push_back(std::make_unique<ColorSample>(
				this,
				_style,
				index,
				index == initial));
			samples.back()->show();
			samples.back()->setClickedCallback([=] {
				_callback(index);
			});
		}
	};
	for (const auto index : indices) {
		add(index);
	}
	if (initial != kUnsetColorIndex && !ranges::contains(indices, initial)) {
		add(initial);
	}
	_samples = std::move(samples);
	if (width() > 0) {
		resizeToWidth(width());
	}

	_index.value(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](uint8 was, uint8 now) {
		const auto i = ranges::find(_samples, was, &ColorSample::index);
		if (i != end(_samples)) {
			i->get()->setSelected(false);
		}
		const auto j = ranges::find(_samples, now, &ColorSample::index);
		if (j != end(_samples)) {
			j->get()->setSelected(true);
		}
	}, lifetime());
}

int ColorSelector::resizeGetHeight(int newWidth) {
	if (newWidth <= 0) {
		return 0;
	}
	const auto count = int(_samples.size());
	const auto columns = Ui::kSimpleColorIndexCount;
	const auto skip = st::settingsColorRadioSkip;
	const auto size = (newWidth - skip * (columns - 1)) / float64(columns);
	const auto isize = int(base::SafeRound(size));
	auto top = 0;
	auto left = 0.;
	for (auto i = 0; i != count; ++i) {
		_samples[i]->resize(isize, isize);
		_samples[i]->move(int(base::SafeRound(left)), top);
		left += size + skip;
		if (!((i + 1) % columns)) {
			top += isize + skip;
			left = 0.;
		}
	}
	return (top - skip) + ((count % columns) ? (isize + skip) : 0);
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
		Fn<void(DocumentId)> emojiIdChosen) {
	const auto button = ButtonStyleWithRightEmoji(
		parent,
		tr::lng_settings_color_emoji_off(tr::now));
	auto result = Settings::CreateButtonWithIcon(
		parent,
		tr::lng_settings_color_emoji(),
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
	) | rpl::start_with_next([=](EmojiStatusPanel::CustomChosen chosen) {
		emojiIdChosen(chosen.id.documentId);
	}, raw->lifetime());

	std::move(colorIndexValue) | rpl::start_with_next([=](uint8 index) {
		state->index = index;
		if (state->emoji) {
			right->update();
		}
	}, right->lifetime());

	const auto session = &show->session();
	const auto added = st::normalFont->spacew;
	std::move(emojiIdValue) | rpl::start_with_next([=](DocumentId emojiId) {
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
	) | rpl::start_with_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::start_with_next([=] {
		if (state->panel.paintBadgeFrame(right)) {
			return;
		}
		auto p = QPainter(right);
		const auto height = right->height();
		if (state->emoji && state->index != kUnsetColorIndex) {
			const auto colors = style->coloredValues(false, state->index);
			state->emoji->paint(p, {
				.textColor = colors.name,
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
		AddLevelBadge(
			Data::LevelLimits(&channel->session()).channelBgIconLevelMin(),
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
	) | rpl::start_with_next([=](EmojiStatusPanel::CustomChosen chosen) {
		statusIdChosen({ chosen.id }, chosen.until);
	}, raw->lifetime());

	const auto session = &show->session();
	std::move(statusIdValue) | rpl::start_with_next([=](EmojiStatusId id) {
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
	) | rpl::start_with_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::filter([=] {
		return state->icon != nullptr;
	}) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=](DocumentData *icon) {
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

void AddColorGiftTabs(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		Fn<void(uint64 giftId)> chosen) {
	using namespace Info::PeerGifts;

	struct State {
		rpl::variable<std::vector<Data::StarGift>> list;
		Ui::SubTabs *tabs = nullptr;
	};
	const auto state = container->lifetime().make_state<State>();

	GiftsStars(
		session,
		session->user()
	) | rpl::start_with_next([=](const std::vector<GiftTypeStars> &list) {
		auto filtered = std::vector<Data::StarGift>();
		for (const auto &gift : list) {
			if (gift.info.peerColorAvailable && gift.resale) {
				filtered.push_back(gift.info);
			}
		}
		state->list = std::move(filtered);
	}, container->lifetime());

	state->list.value(
	) | rpl::start_with_next([=](const std::vector<Data::StarGift> &list) {
		auto tabs = std::vector<Ui::SubTabs::Tab>();
		tabs.push_back({
			.id = u"my"_q,
			.text = tr::lng_gift_stars_tabs_my(tr::now, Ui::Text::WithEntities),
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
			) | rpl::start_with_next([=](const QString &id) {
				state->tabs->setActiveTab(id);
				chosen(id.toULongLong());
			}, state->tabs->lifetime());
		} else {
			state->tabs->setTabs(std::move(tabs), context);
		}
		container->resizeToWidth(container->width());
	}, container->lifetime());
}

void AddGiftSelector(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		rpl::producer<uint64> showingGiftIdValue,
		Fn<void(std::shared_ptr<Data::UniqueGift> selected)> chosen,
		rpl::producer<std::optional<Ui::ColorCollectible>> selected) {
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
		rpl::variable<std::optional<Ui::ColorCollectible>> selected;
		int perRow = 1;

		Fn<void()> loadMore;
		Fn<void()> resize;
		Fn<void()> rebuild;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->delegate.emplace(session, GiftButtonMode::Full);
	state->showingGiftId = std::move(showingGiftIdValue);
	state->selected = std::move(selected);
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
			) | rpl::start_with_next([=](Data::ResaleGiftsDescriptor slice) {
				auto &entry = state->lists[shownGiftId];
				entry.loading.destroy();
				entry.offset = slice.offset;
				entry.loaded = entry.offset.isEmpty();
				if (state->showingGiftId.current() != shownGiftId) {
					return;
				}

				auto &list = state->current->list;
				for (const auto &gift : slice.list) {
					if (gift.unique && gift.unique->peerColor) {
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
			) | rpl::start_with_next([=](Data::MyGiftsDescriptor slice) {
				auto &entry = state->lists[shownGiftId];
				entry.loading.destroy();
				entry.offset = slice.offset;
				entry.loaded = entry.offset.isEmpty();
				if (state->showingGiftId.current() != shownGiftId) {
					return;
				}

				auto &list = state->current->list;
				for (const auto &gift : slice.list) {
					if (gift.info.unique && gift.info.unique->peerColor) {
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
		const auto current = state->selected.current();
		const auto selectedId = current ? current->collectibleId : 0;
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
			raw->toggleSelected(
				gift.info.unique->id == selectedId,
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

		state->selected.value(
		) | rpl::combine_previous() | rpl::start_with_next([=](
				const std::optional<Ui::ColorCollectible> &was,
				const std::optional<Ui::ColorCollectible> &now) {
			const auto wasId = was ? was->collectibleId : 0;
			const auto nowId = now ? now->collectibleId : 0;
			const auto find = [&](uint64 id) -> GiftButton* {
				if (!id) {
					return nullptr;
				}
				for (auto i = 0, count = int(state->current->list.size())
					; i != count
					; ++i) {
					const auto &gift = state->current->list[i];
					if (gift.info.unique->id == id) {
						return state->buttons[i].get();
					}
				}
				return nullptr;
			};
			if (const auto button = find(wasId)) {
				button->toggleSelected(false, GiftSelectionMode::Inset);
			}
			if (const auto button = find(nowId)) {
				button->toggleSelected(true, GiftSelectionMode::Inset);
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

		const auto rows = (count + state->perRow - 1) / state->perRow;
		const auto height = padding.top()
			+ (rows * state->delegate->buttonSize().height())
			+ ((rows - 1) * st::giftBoxGiftSkip.y())
			+ padding.bottom();
		raw->resize(raw->width(), height);

		state->rebuild();
	};

	state->showingGiftId.value(
	) | rpl::start_with_next([=](uint64 showingId) {
		state->current = &state->lists[showingId];
		state->buttons.clear();
		state->delegate.emplace(session, showingId
			? GiftButtonMode::Full
			: GiftButtonMode::Minimal);
		state->resize();
	}, raw->lifetime());

	state->visibleRange = raw->visibleRange();
	state->visibleRange.value(
	) | rpl::start_with_next(state->rebuild, raw->lifetime());
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
	) | rpl::start_with_next([=](const QSize &s, const QString &) {
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

void EditPeerColorBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme) {
	const auto group = peer->isMegagroup();
	const auto container = box->verticalLayout();

	box->setTitle(peer->isSelf()
		? tr::lng_settings_color_title()
		: tr::lng_edit_channel_color());
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giftBox);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	struct State {
		rpl::variable<uint8> index;
		rpl::variable<DocumentId> emojiId;
		rpl::variable<EmojiStatusId> statusId;
		rpl::variable<std::optional<Ui::ColorCollectible>> collectible;
		rpl::variable<uint64> showingGiftId;
		std::shared_ptr<Data::UniqueGift> buyCollectible;
		TimeId statusUntil = 0;
		bool statusChanged = false;
		bool changing = false;
		bool applying = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->index = peer->colorCollectible()
		? kUnsetColorIndex
		: peer->colorIndex();
	state->emojiId = peer->backgroundEmojiId();
	state->statusId = peer->emojiStatusId();
	state->collectible = peer->colorCollectible()
		? *peer->colorCollectible()
		: std::optional<Ui::ColorCollectible>();
	if (group) {
		Settings::AddDividerTextWithLottie(box->verticalLayout(), {
			.lottie = u"palette"_q,
			.lottieSize = st::settingsCloudPasswordIconSize,
			.lottieMargins = st::peerAppearanceIconPadding,
			.showFinished = box->showFinishes(),
			.about = tr::lng_boost_group_about(Ui::Text::WithEntities),
			.aboutMargins = st::peerAppearanceCoverLabelMargin,
		});
	} else {
		box->addRow(object_ptr<PreviewWrap>(
			box,
			style,
			theme,
			peer,
			state->index.value(),
			state->emojiId.value(),
			state->collectible.value()
		), style::margins());

		auto indices = peer->session().api().peerColors().suggestedValue();
		const auto margin = st::settingsColorRadioMargin;
		const auto skip = st::settingsColorRadioSkip;
		box->addRow(
			object_ptr<ColorSelector>(
				box,
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
			[=](DocumentId id) { state->emojiId = id; }));

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
	} else if (peer->isSelf()) {
		Ui::AddSkip(container, st::settingsColorSampleSkip);

		const auto session = &peer->session();
		AddColorGiftTabs(container, session, [=](uint64 giftId) {
			state->showingGiftId = giftId;
		});

		auto showingGiftId = state->showingGiftId.value();
		AddGiftSelector(container, session, std::move(showingGiftId), [=](
				std::shared_ptr<Data::UniqueGift> selected) {
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
		}, state->collectible.value());
	}

	const auto button = box->addButton(tr::lng_settings_color_apply(), [=] {
		if (state->applying) {
			return;
		} else if (ShowPremiumToast(show, peer)) {
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
		Apply(show, peer, values, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=] {
			state->applying = false;
		}));
	});
	state->collectible.value(
	) | rpl::start_with_next([=] {
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
					Ui::Text::WithEntities),
				tr::lng_gift_buy_resale_equals(
					lt_cost,
					rpl::single(Ui::Text::IconEmoji(
						&st::starIconEmojiSmall
					).append(Lang::FormatCountDecimal(buy->starsForResale))),
					Ui::Text::WithEntities),
				st::resaleButtonTitle,
				st::resaleButtonSubtitle);
		} else {
			button->setText(tr::lng_gift_buy_resale_button(
				lt_cost,
				rpl::single(Ui::Text::IconEmoji(&st::starIconEmoji).append(
					Lang::FormatCountDecimal(buy->starsForResale))),
				Ui::Text::WithEntities));
		}
	}, button->lifetime());
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
	const auto name = peer->shortName();

	const auto sample = Ui::CreateChild<ColorSample>(
		button.get(),
		&peer->session(),
		style,
		rpl::duplicate(colorIndexValue),
		rpl::duplicate(colorCollectibleValue),
		name);
	sample->show();

	rpl::combine(
		button->widthValue(),
		rpl::duplicate(label),
		rpl::duplicate(colorIndexValue)
	) | rpl::start_with_next([=](
			int width,
			const QString &button,
			int colorIndex) {
		const auto sampleSize = st::settingsColorSampleSize;
		const auto available = width
			- st::settingsButton.padding.left()
			- (st::settingsColorButton.padding.right() - sampleSize)
			- st::settingsButton.style.font->width(button)
			- st::settingsButtonRightSkip;
		if (style->colorPatternIndex(colorIndex)) {
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
	}, sample->lifetime());

	rpl::combine(
		button->sizeValue(),
		sample->sizeValue(),
		std::move(colorIndexValue)
	) | rpl::start_with_next([=](QSize outer, QSize inner, int colorIndex) {
		const auto right = st::settingsColorButton.padding.right()
			- st::settingsColorSampleSkip
			- st::settingsColorSampleSize
			- (style->colorPatternIndex(colorIndex)
				? 0
				: st::settingsColorSamplePadding.right());
		sample->move(
			outer.width() - right - inner.width(),
			(outer.height() - inner.height()) / 2);
	}, sample->lifetime());

	sample->setAttribute(Qt::WA_TransparentForMouseEvents);
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
		peer->input
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
