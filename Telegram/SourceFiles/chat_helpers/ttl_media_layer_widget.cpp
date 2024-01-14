/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/ttl_media_layer_widget.h"

#include "base/event_filter.h"
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
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	HistoryView::Context elementContext() override;
	bool elementIsChatWide() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

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

not_null<Ui::PathShiftGradient*> PreviewDelegate::elementPathShiftGradient() {
	return _pathGradient.get();
}

HistoryView::Context PreviewDelegate::elementContext() {
	return HistoryView::Context::TTLViewer;
}

bool PreviewDelegate::elementIsChatWide() {
	return true;
}

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		rpl::producer<std::shared_ptr<Ui::ChatTheme>> theme);
	~PreviewWrap();

	[[nodiscard]] rpl::producer<> closeRequests() const;

private:
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] QRect elementRect() const;

	const not_null<HistoryItem*> _item;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	std::shared_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<HistoryView::Element> _element;
	rpl::lifetime _elementLifetime;

	struct {
		QImage frame;
		bool use = false;
	} _last;

	rpl::event_stream<> _closeRequests;

};

PreviewWrap::PreviewWrap(
	not_null<Ui::RpWidget*> parent,
	not_null<HistoryItem*> item,
	rpl::producer<std::shared_ptr<Ui::ChatTheme>> theme)
: RpWidget(parent)
, _item(item)
, _style(std::make_unique<Ui::ChatStyle>(
	item->history()->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(elementRect()); })) {

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
			update(elementRect());
		}
	}, lifetime());

	const auto closeCallback = [=] { _closeRequests.fire({}); };

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

		sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			close->moveToLeft(
				(s.width() - close->width()) / 2,
				s.height() - close->height() - st::ttlMediaButtonBottomSkip);
		}, close->lifetime());
	}

	QWidget::setAttribute(Qt::WA_OpaquePaintEvent, false);
	_element = _item->createView(_delegate.get());

	{
		_element->initDimensions();
		widthValue(
		) | rpl::filter([=](int width) {
			return width > st::msgMinWidth;
		}) | rpl::start_with_next([=](int width) {
			_element->resizeGetHeight(width);
		}, _elementLifetime);
	}

	{
		auto text = item->out()
			? tr::lng_ttl_voice_tooltip_out(
				lt_user,
				rpl::single(
					item->history()->peer->name()
				) | rpl::map(Ui::Text::RichLangValue),
				Ui::Text::RichLangValue)
			: tr::lng_ttl_voice_tooltip_in(Ui::Text::RichLangValue);
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
		sizeValue(
		) | rpl::filter(
			[](const QSize &s) { return !s.isNull(); }
		) | rpl::take(1) | rpl::start_with_next([=](const QSize &s) {
			if (s.isEmpty()) {
				return;
			}
			auto area = elementRect();
			area.setWidth(_element->media()
				? _element->media()->width()
				: _element->width());
			tooltip->pointAt(area, RectPart::Top, [=](QSize size) {
				return QPoint{
					(area.width() - size.width()) / 2,
					(s.height() - size.height() * 2 - _element->height()) / 2
						- st::defaultImportantTooltip.padding.top(),
				};
			});
		}, tooltip->lifetime());
	}

	HistoryView::TTLVoiceStops(
		item->fullId()
	) | rpl::start_with_next([=] {
		_last.use = true;
		closeCallback();
	}, lifetime());
}

QRect PreviewWrap::elementRect() const {
	return QRect(
		(width() - _element->width()) / 2,
		(height() - _element->height()) / 2,
		_element->width(),
		_element->height());
}

rpl::producer<> PreviewWrap::closeRequests() const {
	return _closeRequests.events();
}

PreviewWrap::~PreviewWrap() {
	_elementLifetime.destroy();
	_element = nullptr;
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	if (!_element) {
		return;
	}

	auto p = QPainter(this);
	const auto r = rect();

	if (!_last.use) {
		const auto size = _element->currentSize();
		auto result = QImage(
			size * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.fill(Qt::transparent);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		{
			auto q = Painter(&result);
			auto context = _theme->preparePaintContext(
				_style.get(),
				Rect(size),
				Rect(size),
				!window()->isActiveWindow());
			context.outbg = _element->hasOutLayout();
			_element->draw(q, context);
		}
		_last.frame = std::move(result);
	}
	p.translate(
		(r.width() - _element->width()) / 2,
		(r.height() - _element->height()) / 2);
	p.drawImage(0, 0, _last.frame);
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
