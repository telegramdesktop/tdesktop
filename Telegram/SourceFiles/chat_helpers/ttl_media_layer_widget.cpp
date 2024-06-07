/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/ttl_media_layer_widget.h"

#include "base/event_filter.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "editor/editor_layer_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_document.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "window/section_widget.h" // Window::ChatThemeValueFromPeer.
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"

namespace ChatHelpers {
namespace {

class PreviewDelegate final : public HistoryView::DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		rpl::producer<bool> chatWideValue,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	HistoryView::Context elementContext() override;
	bool elementIsChatWide() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	rpl::variable<bool> _chatWide;

};

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	rpl::producer<bool> chatWideValue,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(HistoryView::MakePathShiftGradient(st, update))
, _chatWide(std::move(chatWideValue)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

not_null<Ui::PathShiftGradient*> PreviewDelegate::elementPathShiftGradient() {
	return _pathGradient.get();
}

HistoryView::Context PreviewDelegate::elementContext() {
	return HistoryView::Context::TTLViewer;
}

bool PreviewDelegate::elementIsChatWide() {
	return _chatWide.current();
}

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		rpl::producer<QRect> viewportValue,
		rpl::producer<bool> chatWideValue,
		rpl::producer<std::shared_ptr<Ui::ChatTheme>> theme);
	~PreviewWrap();

	[[nodiscard]] rpl::producer<> closeRequests() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void createView();
	[[nodiscard]] bool goodItem() const;
	void clear();

	const not_null<HistoryItem*> _item;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	rpl::variable<QRect> _globalViewport;
	rpl::variable<bool> _chatWide;
	std::shared_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<HistoryView::Element> _element;
	QRect _viewport;
	QRect _elementGeometry;
	rpl::variable<QRect> _elementInner;
	rpl::lifetime _elementLifetime;

	QImage _lastFrameCache;

	rpl::event_stream<> _closeRequests;

};

PreviewWrap::PreviewWrap(
	not_null<Ui::RpWidget*> parent,
	not_null<HistoryItem*> item,
	rpl::producer<QRect> viewportValue,
	rpl::producer<bool> chatWideValue,
	rpl::producer<std::shared_ptr<Ui::ChatTheme>> theme)
: RpWidget(parent)
, _item(item)
, _style(std::make_unique<Ui::ChatStyle>(
	item->history()->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	std::move(chatWideValue),
	[=] { update(_elementGeometry); }))
, _globalViewport(std::move(viewportValue)) {
	const auto closeCallback = [=] { _closeRequests.fire({}); };
	HistoryView::TTLVoiceStops(
		item->fullId()
	) | rpl::start_with_next([=] {
		_lastFrameCache = Ui::GrabWidgetToImage(this, _elementGeometry);
		closeCallback();
	}, lifetime());

	const auto isRound = _item
		&& _item->media()
		&& _item->media()->document()
		&& _item->media()->document()->isVideoMessage();

	std::move(
		theme
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> theme) {
		_theme = std::move(theme);
		_style->apply(_theme.get());
	}, lifetime());

	const auto session = &_item->history()->session();
	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const HistoryView::Element*> view) {
		if (view == _element.get()) {
			update(_elementGeometry);
		}
	}, lifetime());
	session->data().itemViewRefreshRequest(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		if (item == _item) {
			if (goodItem()) {
				createView();
				update();
			} else {
				clear();
				_closeRequests.fire({});
			}
		}
	}, lifetime());
	session->data().itemDataChanges(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		if (item == _item) {
			_element->itemDataChanged();
		}
	}, lifetime());
	session->data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (item == _item) {
			_closeRequests.fire({});
		}
	}, lifetime());

	{
		const auto close = Ui::CreateChild<Ui::RoundButton>(
			this,
			item->out()
				? tr::lng_close()
				: tr::lng_ttl_voice_close_in(),
			st::ttlMediaButton);
		close->setFullRadius(true);
		close->setClickedCallback(closeCallback);
		close->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

		rpl::combine(
			sizeValue(),
			_elementInner.value()
		) | rpl::start_with_next([=](QSize size, QRect inner) {
			close->moveToLeft(
				inner.x() + (inner.width() - close->width()) / 2,
				(size.height()
					- close->height()
					- st::ttlMediaButtonBottomSkip));
		}, close->lifetime());
	}

	QWidget::setAttribute(Qt::WA_OpaquePaintEvent, false);
	createView();

	{
		auto text = item->out()
			? (isRound
				? tr::lng_ttl_round_tooltip_out
				: tr::lng_ttl_voice_tooltip_out)(
					lt_user,
					rpl::single(
						item->history()->peer->shortName()
					) | Ui::Text::ToRichLangValue(),
					Ui::Text::RichLangValue)
			: (isRound
				? tr::lng_ttl_round_tooltip_in
				: tr::lng_ttl_voice_tooltip_in)(Ui::Text::RichLangValue);
		const auto tooltip = Ui::CreateChild<Ui::ImportantTooltip>(
			this,
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				this,
				Ui::MakeNiceTooltipLabel(
					parent,
					std::move(text),
					st::dialogsStoriesTooltipMaxWidth,
					st::ttlMediaImportantTooltipLabel),
				st::defaultImportantTooltip.padding),
			st::dialogsStoriesTooltip);
		tooltip->toggleFast(true);
		_elementInner.value(
		) | rpl::filter([](const QRect &inner) {
			return !inner.isEmpty();
		}) | rpl::start_with_next([=](const QRect &inner) {
			tooltip->pointAt(inner, RectPart::Top, [=](QSize size) {
				return QPoint{
					inner.x() + (inner.width() - size.width()) / 2,
					(inner.y()
						- st::normalFont->height
						- size.height()
						- st::defaultImportantTooltip.padding.top()),
				};
			});
		}, tooltip->lifetime());
	}
}

rpl::producer<> PreviewWrap::closeRequests() const {
	return _closeRequests.events();
}

bool PreviewWrap::goodItem() const {
	const auto media = _item->media();
	if (!media || !media->ttlSeconds()) {
		return false;
	}
	const auto document = media->document();
	return document
		&& (document->isVoiceMessage() || document->isVideoMessage());
}

void PreviewWrap::createView() {
	clear();
	_element = _item->createView(_delegate.get());
	_element->initDimensions();
	rpl::combine(
		sizeValue(),
		_globalViewport.value()
	) | rpl::start_with_next([=](QSize outer, QRect globalViewport) {
		_viewport = globalViewport.isEmpty()
			? rect()
			: mapFromGlobal(globalViewport);
		if (_viewport.width() < st::msgMinWidth) {
			return;
		}
		_element->resizeGetHeight(_viewport.width());
		_elementGeometry = QRect(
			(_viewport.width() - _element->width()) / 2,
			(_viewport.height() - _element->height()) / 2,
			_element->width(),
			_element->height()
		).translated(_viewport.topLeft());
		_elementInner = _element->innerGeometry().translated(
			_elementGeometry.topLeft());
		update();
	}, _elementLifetime);
}

void PreviewWrap::clear() {
	_elementLifetime.destroy();
	_element = nullptr;
}

PreviewWrap::~PreviewWrap() {
	clear();
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	if (!_element || _elementGeometry.isEmpty()) {
		return;
	}

	auto p = Painter(this);
	p.translate(_elementGeometry.topLeft());
	if (!_lastFrameCache.isNull()) {
		p.drawImage(0, 0, _lastFrameCache);
	} else {
		auto context = _theme->preparePaintContext(
			_style.get(),
			Rect(_element->currentSize()),
			Rect(_element->currentSize()),
			!window()->isActiveWindow());
		context.outbg = _element->hasOutLayout();
		_element->draw(p, context);
	}
}

rpl::producer<QRect> GlobalViewportForWindow(
		not_null<Window::SessionController*> controller) {
	const auto delegate = controller->window().floatPlayerDelegate();
	return rpl::single(rpl::empty) | rpl::then(
		delegate->floatPlayerAreaUpdates()
	) | rpl::map([=] {
		auto section = (Media::Player::FloatSectionDelegate*)nullptr;
		delegate->floatPlayerEnumerateSections([&](
				not_null<Media::Player::FloatSectionDelegate*> check,
				Window::Column column) {
			if ((column == Window::Column::First && !section)
				|| column == Window::Column::Second) {
				section = check;
			}
		});
		if (section) {
			const auto rect = section->floatPlayerAvailableRect();
			if (rect.width() >= st::msgMinWidth) {
				return rect;
			}
		}
		return QRect();
	});
}

} // namespace

void ShowTTLMediaLayerWidget(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto parent = controller->content();
	const auto show = controller->uiShow();
	auto preview = base::make_unique_q<PreviewWrap>(
		parent,
		item,
		GlobalViewportForWindow(controller),
		controller->adaptive().chatWideValue(),
		Window::ChatThemeValueFromPeer(
			controller,
			item->history()->peer));
	preview->closeRequests(
	) | rpl::start_with_next([=] {
		show->hideLayer();
	}, preview->lifetime());
	auto layer = std::make_unique<Editor::LayerWidget>(
		parent,
		std::move(preview));
	layer->lifetime().add([] { ::Media::Player::instance()->stop(); });
	base::install_event_filter(layer.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto k = static_cast<QKeyEvent*>(e.get());
			if (k->key() == Qt::Key_Escape) {
				show->hideLayer();
			}
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
	controller->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

} // namespace ChatHelpers
