/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_widget.h"

#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/photo_editor_layer_widget.h" // Editor::kProfilePhotoSize.
#include "history/view/media/history_view_sticker_player.h"
#include "info/userpic/info_userpic_bubble_wrap.h"
#include "info/userpic/info_userpic_colors_palette_chooser.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/controls/emoji_button.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info_userpic_builder.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace UserpicBuilder {
namespace {

class PreviewPainter final {
public:
	PreviewPainter(int size);

	[[nodiscard]] not_null<DocumentData*> document() const;

	void setDocument(
		not_null<DocumentData*> document,
		Fn<void()> updateCallback);

	void paintBackground(QPainter &p, const QBrush &brush);
	bool paintForeground(QPainter &p);

private:
	const int _size;
	const int _emojiSize;
	const QRect _frameRect;

	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	bool _paused = false;
	rpl::lifetime _lifetime;

};

PreviewPainter::PreviewPainter(int size)
: _size(size)
, _emojiSize(base::SafeRound(_size / M_SQRT2))
, _frameRect(Rect(Size(_size)) - Margins((_size - _emojiSize) / 2)) {
}

not_null<DocumentData*> PreviewPainter::document() const {
	Expects(_media != nullptr);
	return _media->owner();
}

void PreviewPainter::setDocument(
		not_null<DocumentData*> document,
		Fn<void()> updateCallback) {
	if (_media && (document == _media->owner())) {
		return;
	}
	_lifetime.destroy();

	const auto sticker = document->sticker();
	Assert(sticker != nullptr);
	_media = document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (!_media->loaded()) {
			return;
		}
		_lifetime.destroy();
		const auto emojiSize = Size(_emojiSize);
		if (sticker->isLottie()) {
			_player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					_media.get(),
					//
					ChatHelpers::StickerLottieSize::EmojiInteractionReserved7,
					emojiSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			_player = std::make_unique<HistoryView::WebmPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		} else if (sticker) {
			_player = std::make_unique<HistoryView::StaticStickerPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		}
		if (_player) {
			_player->setRepaintCallback(updateCallback);
		} else {
			updateCallback();
		}
	}, _lifetime);
}

void PreviewPainter::paintBackground(QPainter &p, const QBrush &brush) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawEllipse(0, 0, _size, _size);
}

bool PreviewPainter::paintForeground(QPainter &p) {
	if (_player && _player->ready()) {
		// resolveIsColored();
		const auto frame = _player->frame(
			Size(_emojiSize),
			(/*_isColored
				? st::profileVerifiedCheckBg->c
				: */QColor(0, 0, 0, 0)),
			false,
			crl::now(),
			_paused);

		if (frame.image.width() == frame.image.height()) {
			p.drawImage(_frameRect, frame.image);
		} else {
			auto frameRect = Rect(frame.image.size().scaled(
				_frameRect.size(),
				Qt::KeepAspectRatio));
			frameRect.moveCenter(_frameRect.center());
			p.drawImage(frameRect, frame.image);
		}
		if (!_paused) {
			_player->markFrameShown();
		}
		return true;
	}
	return false;
}

class EmojiUserpic final : public Ui::RpWidget {
public:
	EmojiUserpic(not_null<Ui::RpWidget*> parent, const QSize &size);

	void result(int size, Fn<void(QImage &&image)> done);
	void setGradientStops(QGradientStops stops);
	void setDocument(not_null<DocumentData*> document);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	PreviewPainter _painter;

	QBrush _previousBrush;
	QBrush _brush;
	QGradientStops _stops;

	Ui::Animations::Simple _animation;

};

EmojiUserpic::EmojiUserpic(not_null<Ui::RpWidget*> parent, const QSize &size)
: Ui::RpWidget(parent)
, _painter(size.width()) {
	resize(size);
}

void EmojiUserpic::setDocument(not_null<DocumentData*> document) {
	_painter.setDocument(document, [=] { update(); });
}

void EmojiUserpic::result(int size, Fn<void(QImage &&image)> done) {
	const auto colors = ranges::views::all(
		_stops
	) | ranges::views::transform([](const QGradientStop &stop) {
		return stop.second;
	}) | ranges::to_vector;
	const auto painter = lifetime().make_state<PreviewPainter>(size);
	// Reset to the first frame.
	painter->setDocument(_painter.document(), [=] {
		auto background = Images::GenerateLinearGradient(Size(size), colors);

		auto p = QPainter(&background);
		while (true) {
			if (painter->paintForeground(p)) {
				break;
			}
		}
		done(std::move(background));
	});
}

void EmojiUserpic::setGradientStops(QGradientStops stops) {
	if (_stops == stops) {
		return;
	}
	if (!_stops.empty()) {
		auto gradient = QLinearGradient(0, 0, width() / 2., height());
		gradient.setStops(base::take(_stops));
		_previousBrush = QBrush(std::move(gradient));
	}
	_stops = std::move(stops);
	{
		auto gradient = QLinearGradient(0, 0, width() / 2., height());
		gradient.setStops(_stops);
		_brush = QBrush(std::move(gradient));
	}
	_animation.stop();
	_animation.start([=] { update(); }, 0., 1., st::slideWrapDuration);
}

void EmojiUserpic::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);

	if (_animation.animating() && (_previousBrush != Qt::NoBrush)) {
		_painter.paintBackground(p, _previousBrush);

		p.setOpacity(_animation.value(1.));
	}

	_painter.paintBackground(p, _brush);

	p.setOpacity(1.);
	_painter.paintForeground(p);
}

class EmojiSelector final : public Ui::RpWidget {
public:
	EmojiSelector(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<not_null<DocumentData*>> chosen() const;

private:
	using Footer = ChatHelpers::TabbedSelector::InnerFooter;
	using List = ChatHelpers::TabbedSelector::Inner;
	using Type = ChatHelpers::SelectorTab;
	void createSelector(Type type);

	struct Selector {
		not_null<List*> list;
		not_null<Footer*> footer;
	};
	[[nodiscard]] Selector createEmojiList() const;
	[[nodiscard]] Selector createStickersList() const;

	const not_null<Window::SessionController*> _controller;
	base::unique_qptr<Ui::RpWidget> _container;
	base::unique_qptr<Ui::ScrollArea> _scroll;

	rpl::event_stream<not_null<DocumentData*>> _chosen;

};

EmojiSelector::EmojiSelector(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	createSelector(Type::Emoji);
}

rpl::producer<not_null<DocumentData*>> EmojiSelector::chosen() const {
	return _chosen.events();
}

EmojiSelector::Selector EmojiSelector::createEmojiList() const {
	const auto session = &_controller->session();
	const auto manager = &session->data().customEmojiManager();
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	auto args = ChatHelpers::EmojiListDescriptor{
		.session = session,
		.mode = ChatHelpers::EmojiListMode::FullReactions,
		.controller = _controller,
		.paused = [=, reason = Window::GifPauseReason::Layer] {
			return _controller->isGifPausedAtLeastFor(reason);
		},
		.customRecentList = session->api().peerPhoto().profileEmojiList(),
		.customRecentFactory = [=](DocumentId id, Fn<void()> repaint) {
			return manager->create(id, std::move(repaint), tag);
		},
		.st = &st::reactPanelEmojiPan,
	};
	const auto list = _scroll->setOwnedWidget(
		object_ptr<ChatHelpers::EmojiListWidget>(_scroll, std::move(args)));
	const auto footer = list->createFooter().data();
	list->refreshEmoji();
	list->customChosen(
	) | rpl::start_with_next([=](const ChatHelpers::FileChosen &chosen) {
		_chosen.fire_copy(chosen.document);
	}, list->lifetime());
	return { list, footer };
}

EmojiSelector::Selector EmojiSelector::createStickersList() const {
	const auto list = _scroll->setOwnedWidget(
		object_ptr<ChatHelpers::StickersListWidget>(
			_scroll,
			_controller,
			Window::GifPauseReason::Layer));
	const auto footer = list->createFooter().data();
	list->refreshRecent();
	list->chosen(
	) | rpl::start_with_next([=](const ChatHelpers::FileChosen &chosen) {
		_chosen.fire_copy(chosen.document);
	}, list->lifetime());
	return { list, footer };
}

void EmojiSelector::createSelector(Type type) {
	Expects((type == Type::Emoji) || (type == Type::Stickers));

	const auto isEmoji = (type == Type::Emoji);
	const auto &stScroll = st::reactPanelScroll;

	_container = base::make_unique_q<Ui::RpWidget>(this);
	const auto container = _container.get();
	container->show();
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		container->setGeometry(Rect(s));
	}, container->lifetime());

	_scroll = base::make_unique_q<Ui::ScrollArea>(container, stScroll);

	const auto selector = isEmoji
		? createEmojiList()
		: createStickersList();
	selector.footer->setParent(container);

	const auto toggleButton = Ui::CreateChild<Ui::AbstractButton>(container);
	const auto &togglePos = st::userpicBuilderEmojiSelectorTogglePosition;
	{
		const auto &pos = togglePos;
		toggleButton->resize(st::menuIconStickers.size()
			// Trying to overlap the settings button under.
			+ QSize(pos.x() * 2, pos.y() * 2));
		toggleButton->show();
		toggleButton->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(toggleButton);
			const auto r = toggleButton->rect()
				- QMargins(pos.x(), pos.y(), pos.x(), pos.y());
			p.fillRect(r, st::boxBg);
			const auto &icon = st::userpicBuilderEmojiToggleStickersIcon;
			if (isEmoji) {
				icon.paintInCenter(p, r);
			} else {
				st::userpicBuilderEmojiToggleEmojiIcon.paintInCenter(p, r);
				const auto line = style::ConvertScaleExact(
					st::historyEmojiCircleLine);
				p.setPen(QPen(
					st::emojiIconFg,
					line,
					Qt::SolidLine,
					Qt::RoundCap));
				p.setBrush(Qt::NoBrush);
				PainterHighQualityEnabler hq(p);
				const auto diff = (icon.width()
					- st::userpicBuilderEmojiToggleEmojiSize) / 2;
				p.drawEllipse(r - Margins(diff));
			}
		}, toggleButton->lifetime());
	}
	toggleButton->show();
	toggleButton->setClickedCallback([=] {
		createSelector(isEmoji ? Type::Stickers : Type::Emoji);
	});

	_scroll->scrollTopChanges(
	) | rpl::start_with_next([=] {
		const auto scrollTop = _scroll->scrollTop();
		const auto scrollBottom = scrollTop + _scroll->height();
		selector.list->setVisibleTopBottom(scrollTop, scrollBottom);
	}, selector.list->lifetime());

	selector.list->scrollToRequests(
	) | rpl::start_with_next([=](int y) {
		_scroll->scrollToY(y);
		// _shadow->update();
	}, selector.list->lifetime());

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container);
	separator->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(separator);
		p.fillRect(r, st::shadowFg);
	}, separator->lifetime());

	selector.footer->show();
	separator->show();
	_scroll->show();

	const auto scrollWidth = stScroll.width;
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto left = st::userpicBuilderEmojiSelectorLeft;
		const auto mostTop = st::userpicBuilderEmojiSelectorLeft;

		toggleButton->move(QPoint(left, mostTop));

		selector.footer->setGeometry(
			(isEmoji ? (rect::right(toggleButton) - togglePos.x()) : left),
			mostTop,
			s.width() - left,
			selector.footer->height());

		separator->setGeometry(
			0,
			rect::bottom(selector.footer),
			s.width(),
			st::lineWidth);

		selector.list->resizeToWidth(s.width() - st::boxRadius * 2);
		_scroll->setGeometry(
			st::boxRadius,
			rect::bottom(separator),
			selector.list->width() + scrollWidth,
			s.height() - rect::bottom(separator));
	}, lifetime());
}

} // namespace

not_null<Ui::VerticalLayout*> CreateUserpicBuilder(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		StartData data,
		BothWayCommunication communication) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	const auto preview = container->add(
		object_ptr<Ui::CenterWrap<EmojiUserpic>>(
			container,
			object_ptr<EmojiUserpic>(
				container,
				Size(st::settingsInfoPhotoSize))),
		st::userpicBuilderEmojiPreviewPadding)->entity();
	if (const auto id = data.documentId) {
		if (const auto document = controller->session().data().document(id)) {
			preview->setDocument(document);
		}
	}

	container->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_userpic_builder_color_subtitle(),
				st::userpicBuilderEmojiSubtitle)),
		st::userpicBuilderEmojiSubtitlePadding);

	const auto paletteBg = Ui::AddBubbleWrap(
		container,
		st::userpicBuilderEmojiBubblePaletteSize,
		[=] { return controller->chatStyle(); });

	const auto palette = Ui::CreateChild<ColorsPalette>(
		paletteBg.get(),
		data.colorIndex);
	palette->stopsValue(
	) | rpl::start_with_next([=](QGradientStops stops) {
		preview->setGradientStops(std::move(stops));
	}, preview->lifetime());
	paletteBg->innerRectValue(
	) | rpl::start_with_next([=](const QRect &r) {
		palette->setGeometry(r
			- st::userpicBuilderEmojiBubblePalettePadding);
	}, palette->lifetime());

	container->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_userpic_builder_emoji_subtitle(),
				st::userpicBuilderEmojiSubtitle)),
		st::userpicBuilderEmojiSubtitlePadding);

	const auto selectorBg = Ui::AddBubbleWrap(
		container,
		QSize(
			st::userpicBuilderEmojiBubblePaletteSize.width(),
			st::userpicBuilderEmojiSelectorMinHeight),
		[=] { return controller->chatStyle(); });
	const auto selector = Ui::CreateChild<EmojiSelector>(
		selectorBg.get(),
		controller);
	selector->chosen(
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		preview->setDocument(document);
	}, preview->lifetime());
	selectorBg->innerRectValue(
	) | rpl::start_with_next([=](const QRect &r) {
		selector->setGeometry(r);
	}, selector->lifetime());

	base::take(
		communication.triggers
	) | rpl::start_with_next([=, done = base::take(communication.result)] {
		preview->result(Editor::kProfilePhotoSize, [=](QImage &&image) {
			done(std::move(image));
		});
	}, preview->lifetime());

	return container;
}

not_null<Ui::RpWidget*> CreateEmojiUserpic(
		not_null<Ui::RpWidget*> parent,
		const QSize &size,
		rpl::producer<not_null<DocumentData*>> document,
		rpl::producer<int> colorIndex) {
	const auto widget = Ui::CreateChild<EmojiUserpic>(parent.get(), size);
	std::move(
		document
	) | rpl::start_with_next([=](not_null<DocumentData*> d) {
		widget->setDocument(d);
	}, widget->lifetime());
	std::move(
		colorIndex
	) | rpl::start_with_next([=](int index) {
		const auto c = Ui::EmptyUserpic::UserpicColor(
			Ui::EmptyUserpic::ColorIndex(index));
		widget->setGradientStops({ { 0, c.color1->c }, { 1, c.color2->c } });
	}, widget->lifetime());
	return widget;
}

} // namespace UserpicBuilder
