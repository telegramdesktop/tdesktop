/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_color_box.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/boosts/info_boosts_widget.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/boxes/boost_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace {

using namespace Settings;

constexpr auto kFakeChannelId = ChannelId(0xFFFFFFF000ULL);
constexpr auto kFakeWebPageId = WebPageId(0xFFFFFFFF00000000ULL);
constexpr auto kSelectAnimationDuration = crl::time(150);

class ColorSample final : public Ui::AbstractButton {
public:
	ColorSample(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::ChatStyle> style,
		rpl::producer<uint8> colorIndex,
		const QString &name);
	ColorSample(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::ChatStyle> style,
		uint8 colorIndex,
		bool selected);

	[[nodiscard]] uint8 index() const;
	int naturalWidth() const override;

	void setSelected(bool selected);

private:
	void paintEvent(QPaintEvent *e) override;

	std::shared_ptr<Ui::ChatStyle> _style;
	Ui::Text::String _name;
	uint8 _index = 0;
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
		rpl::producer<DocumentId> backgroundEmojiId);
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

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::ChatStyle> style,
	rpl::producer<uint8> colorIndex,
	const QString &name)
: AbstractButton(parent)
, _style(style)
, _name(st::semiboldTextStyle, name) {
	std::move(
		colorIndex
	) | rpl::start_with_next([=](uint8 index) {
		_index = index;
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
		p.setBrush(colors.bg);
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

int ColorSample::naturalWidth() const {
	if (_name.isEmpty() || _style->colorPatternIndex(_index)) {
		return st::settingsColorSampleSize;
	}
	const auto padding = st::settingsColorSamplePadding;
	return std::max(
		padding.left() + _name.maxWidth() + padding.right(),
		padding.top() + st::semiboldFont->height + padding.bottom());
}

PreviewWrap::PreviewWrap(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::ChatStyle> style,
	std::shared_ptr<Ui::ChatTheme> theme,
	not_null<PeerData*> peer,
	rpl::producer<uint8> colorIndexValue,
	rpl::producer<DocumentId> backgroundEmojiId)
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
	0, // duration
	QString(), // author
	false, // hasLargeMedia
	0)) // pendingTill
, _theme(theme)
, _style(style)
, _delegate(std::make_unique<PreviewDelegate>(box, _style.get(), [=] {
	update();
}))
, _replyToItem(_history->addNewLocalMessage(
	_history->nextNonHistoryEntryId(),
	(MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId
		| MessageFlag::Post),
	UserId(), // via
	FullReplyTo(),
	base::unixtime::now(), // date
	_fake->id,
	QString(), // postAuthor
	TextWithEntities{ _peer->isSelf()
		? tr::lng_settings_color_reply(tr::now)
		: tr::lng_settings_color_reply_channel(tr::now),
	},
	MTP_messageMediaEmpty(),
	HistoryMessageMarkupData(),
	uint64(0)))
, _replyItem(_history->addNewLocalMessage(
	_history->nextNonHistoryEntryId(),
	(MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId
		| MessageFlag::HasReplyInfo
		| MessageFlag::Post),
	UserId(), // via
	FullReplyTo{ .messageId = _replyToItem->fullId() },
	base::unixtime::now(), // date
	_fake->id,
	QString(), // postAuthor
	TextWithEntities{ _peer->isSelf()
		? tr::lng_settings_color_text(tr::now)
		: tr::lng_settings_color_text_channel(tr::now),
	},
	MTP_messageMediaWebPage(
		MTP_flags(0),
		MTP_webPagePending(
			MTP_flags(0),
			MTP_long(_webpage->id),
			MTPstring(),
			MTP_int(0))),
	HistoryMessageMarkupData(),
	uint64(0)))
, _element(_replyItem->createView(_delegate.get()))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	_fake->setName(peer->name(), QString());
	std::move(colorIndexValue) | rpl::start_with_next([=](uint8 index) {
		_fake->changeColorIndex(index);
		update();
	}, lifetime());
	std::move(backgroundEmojiId) | rpl::start_with_next([=](DocumentId id) {
		_fake->changeBackgroundEmojiId(id);
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

void Set(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		uint8 colorIndex,
		DocumentId backgroundEmojiId) {
	const auto wasIndex = peer->colorIndex();
	const auto wasEmojiId = peer->backgroundEmojiId();

	const auto setLocal = [=](uint8 index, DocumentId emojiId) {
		using UpdateFlag = Data::PeerUpdate::Flag;
		peer->changeColorIndex(index);
		peer->changeBackgroundEmojiId(emojiId);
		peer->session().changes().peerUpdated(
			peer,
			UpdateFlag::Color | UpdateFlag::BackgroundEmoji);
	};
	setLocal(colorIndex, backgroundEmojiId);

	const auto done = [=] {
		show->showToast(peer->isSelf()
			? tr::lng_settings_color_changed(tr::now)
			: tr::lng_settings_color_changed_channel(tr::now));
	};
	const auto fail = [=](const MTP::Error &error) {
		setLocal(wasIndex, wasEmojiId);
		show->showToast(error.type());
	};
	const auto send = [&](auto &&request) {
		peer->session().api().request(
			std::move(request)
		).done(done).fail(fail).send();
	};
	if (peer->isSelf()) {
		send(MTPaccount_UpdateColor(
			MTP_flags(
				MTPaccount_UpdateColor::Flag::f_background_emoji_id),
			MTP_int(colorIndex),
			MTP_long(backgroundEmojiId)));
	} else if (const auto channel = peer->asChannel()) {
		send(MTPchannels_UpdateColor(
			MTP_flags(
				MTPchannels_UpdateColor::Flag::f_background_emoji_id),
			channel->inputChannel,
			MTP_int(colorIndex),
			MTP_long(backgroundEmojiId)));
	} else {
		Unexpected("Invalid peer type in Set(colorIndex).");
	}
}

void Apply(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		uint8 colorIndex,
		DocumentId backgroundEmojiId,
		Fn<void()> close,
		Fn<void()> cancel) {
	const auto session = &peer->session();
	if (peer->colorIndex() == colorIndex
		&& peer->backgroundEmojiId() == backgroundEmojiId) {
		close();
	} else if (peer->isSelf() && !session->premium()) {
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
		cancel();
	} else if (peer->isSelf()) {
		Set(show, peer, colorIndex, backgroundEmojiId);
		close();
	} else {
		session->api().request(MTPpremium_GetBoostsStatus(
			peer->input
		)).done([=](const MTPpremium_BoostsStatus &result) {
			const auto &data = result.data();
			const auto required = session->account().appConfig().get<int>(
				"channel_color_level_min",
				5);
			if (data.vlevel().v >= required) {
				Set(show, peer, colorIndex, backgroundEmojiId);
				close();
				return;
			}
			const auto next = data.vnext_level_boosts().value_or_empty();
			const auto openStatistics = [=] {
				if (const auto controller = show->resolveWindow(
						ChatHelpers::WindowUsage::PremiumPromo)) {
					controller->showSection(Info::Boosts::Make(peer));
				}
			};
			show->show(Box(Ui::AskBoostBox, Ui::AskBoostBoxData{
				.link = qs(data.vboost_url()),
				.boost = {
					.level = data.vlevel().v,
					.boosts = data.vboosts().v,
					.thisLevelBoosts = data.vcurrent_level_boosts().v,
					.nextLevelBoosts = next,
				},
				.requiredLevel = required,
			}, openStatistics, nullptr));
			cancel();
		}).fail([=](const MTP::Error &error) {
			show->showToast(error.type());
			cancel();
		}).send();
	}
}

class ColorSelector final : public Ui::RpWidget {
public:
	ColorSelector(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::ChatStyle> style,
		rpl::producer<std::vector<uint8>> indices,
		uint8 index,
		Fn<void(uint8)> callback);

private:
	void fillFrom(std::vector<uint8> indices);

	int resizeGetHeight(int newWidth) override;

	const std::shared_ptr<Ui::ChatStyle> _style;
	std::vector<std::unique_ptr<ColorSample>> _samples;
	const Fn<void(uint8)> _callback;
	uint8 _index = 0;

};

ColorSelector::ColorSelector(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::ChatStyle> style,
	rpl::producer<std::vector<uint8>> indices,
	uint8 index,
	Fn<void(uint8)> callback)
: RpWidget(box)
, _style(style)
, _callback(std::move(callback))
, _index(index) {
	std::move(
		indices
	) | rpl::start_with_next([=](std::vector<uint8> indices) {
		fillFrom(std::move(indices));
	}, lifetime());
}

void ColorSelector::fillFrom(std::vector<uint8> indices) {
	auto samples = std::vector<std::unique_ptr<ColorSample>>();
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
				index == _index));
			samples.back()->show();
			samples.back()->setClickedCallback([=] {
				if (_index != index) {
					_callback(index);

					ranges::find(
						_samples,
						_index,
						&ColorSample::index
					)->get()->setSelected(false);
					_index = index;
					ranges::find(
						_samples,
						_index,
						&ColorSample::index
					)->get()->setSelected(true);
				}
			});
		}
	};
	for (const auto index : indices) {
		add(index);
	}
	if (!ranges::contains(indices, _index)) {
		add(_index);
	}
	_samples = std::move(samples);
	if (width() > 0) {
		resizeToWidth(width());
	}
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

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateEmojiIconButton(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Ui::ChatStyle> style,
		rpl::producer<uint8> colorIndexValue,
		rpl::producer<DocumentId> emojiIdValue,
		Fn<void(DocumentId)> emojiIdChosen) {
	const auto &basicSt = st::settingsButtonNoIcon;
	const auto ratio = style::DevicePixelRatio();
	const auto added = st::normalFont->spacew;
	const auto emojiSize = Data::FrameSizeFromTag({}) / ratio;
	const auto noneWidth = added
		+ st::normalFont->width(tr::lng_settings_color_emoji_off(tr::now));
	const auto emojiWidth = added + emojiSize;
	const auto rightPadding = std::max(noneWidth, emojiWidth)
		+ basicSt.padding.right();
	const auto st = parent->lifetime().make_state<style::SettingsButton>(
		basicSt);
	st->padding.setRight(rightPadding);
	auto result = CreateButton(
		parent,
		tr::lng_settings_color_emoji(),
		*st,
		{});
	const auto raw = result.data();

	const auto right = Ui::CreateChild<Ui::RpWidget>(raw);
	right->show();

	struct State {
		Info::Profile::EmojiStatusPanel panel;
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
		DocumentId emojiId = 0;
		uint8 index = 0;
	};
	const auto state = right->lifetime().make_state<State>();
	state->panel.backgroundEmojiChosen(
	) | rpl::start_with_next(emojiIdChosen, raw->lifetime());

	std::move(colorIndexValue) | rpl::start_with_next([=](uint8 index) {
		state->index = index;
		if (state->emoji) {
			right->update();
		}
	}, right->lifetime());

	const auto session = &show->session();
	std::move(emojiIdValue) | rpl::start_with_next([=](DocumentId emojiId) {
		state->emojiId = emojiId;
		state->emoji = emojiId
			? session->data().customEmojiManager().create(
				emojiId,
				[=] { right->update(); })
			: nullptr;
		right->resize(
			(emojiId ? emojiWidth : noneWidth) + added,
			right->height());
		right->update();
	}, right->lifetime());

	rpl::combine(
		raw->sizeValue(),
		right->widthValue()
	) | rpl::start_with_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::start_with_next([=] {
		if (state->panel.paintBadgeFrame(right)) {
			return;
		}
		auto p = QPainter(right);
		const auto height = right->height();
		if (state->emoji) {
			const auto colors = style->coloredValues(false, state->index);
			state->emoji->paint(p, {
				.textColor = colors.name,
				.position = QPoint(added, (height - emojiSize) / 2),
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
		const auto controller = show->resolveWindow(
			ChatHelpers::WindowUsage::PremiumPromo);
		if (controller) {
			state->panel.show({
				.controller = controller,
				.button = right,
				.currentBackgroundEmojiId = state->emojiId,
				.customTextColor = customTextColor,
				.backgroundEmojiMode = true,
			});
		}
	});

	return result;
}

} // namespace

void EditPeerColorBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatStyle> style,
		std::shared_ptr<Ui::ChatTheme> theme) {
	box->setTitle(tr::lng_settings_color_title());
	box->setWidth(st::boxWideWidth);

	struct State {
		rpl::variable<uint8> index;
		rpl::variable<DocumentId> emojiId;
		bool changing = false;
		bool applying = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->index = peer->colorIndex();
	state->emojiId = peer->backgroundEmojiId();

	box->addRow(object_ptr<PreviewWrap>(
		box,
		style,
		theme,
		peer,
		state->index.value(),
		state->emojiId.value()
	), {});

	const auto appConfig = &peer->session().account().appConfig();
	auto indices = rpl::single(
		rpl::empty
	) | rpl::then(
		appConfig->refreshed()
	) | rpl::map([=] {
		const auto list = appConfig->get<std::vector<int>>(
			"peer_colors_available",
			{ 0, 1, 2, 3, 4, 5, 6 });
		return list | ranges::views::transform([](int i) {
			return uint8(i);
		}) | ranges::to_vector;
	});
	const auto margin = st::settingsColorRadioMargin;
	const auto skip = st::settingsColorRadioSkip;
	box->addRow(
		object_ptr<ColorSelector>(
			box,
			style,
			std::move(indices),
			state->index.current(),
			[=](uint8 index) { state->index = index; }),
		{ margin, skip, margin, skip });

	const auto container = box->verticalLayout();
	AddDividerText(container, peer->isSelf()
		? tr::lng_settings_color_about()
		: tr::lng_settings_color_about_channel());

	AddSkip(container, st::settingsColorSampleSkip);

	container->add(CreateEmojiIconButton(
		container,
		show,
		style,
		state->index.value(),
		state->emojiId.value(),
		[=](DocumentId id) { state->emojiId = id; }));

	AddSkip(container, st::settingsColorSampleSkip);
	AddDividerText(container, peer->isSelf()
		? tr::lng_settings_color_emoji_about()
		: tr::lng_settings_color_emoji_about_channel());

	box->addButton(tr::lng_settings_apply(), [=] {
		if (state->applying) {
			return;
		}
		state->applying = true;
		const auto index = state->index.current();
		const auto emojiId = state->emojiId.current();
		Apply(show, peer, index, emojiId, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=] {
			state->applying = false;
		}));
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void AddPeerColorButton(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	const auto button = AddButton(
		container,
		(peer->isSelf()
			? tr::lng_settings_theme_name_color()
			: tr::lng_edit_channel_color()),
		st::settingsColorButton,
		{ &st::menuIconChangeColors });

	auto colorIndexValue = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Color
	) | rpl::map([=] {
		return peer->colorIndex();
	});
	const auto name = peer->shortName();

	const auto style = std::make_shared<Ui::ChatStyle>(
		peer->session().colorIndicesValue());
	const auto theme = std::shared_ptr<Ui::ChatTheme>(
		Window::Theme::DefaultChatThemeOn(button->lifetime()));
	style->apply(theme.get());

	const auto sample = Ui::CreateChild<ColorSample>(
		button.get(),
		style,
		rpl::duplicate(colorIndexValue),
		name);
	sample->show();

	rpl::combine(
		button->widthValue(),
		tr::lng_settings_theme_name_color(),
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

	button->setClickedCallback([=] {
		show->show(Box(EditPeerColorBox, show, peer, style, theme));
	});
}
