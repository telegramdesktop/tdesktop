/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_message_render.h"

#include "data/data_cloud_file.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "editor/editor_message_source.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "window/themes/window_theme.h"
#include "styles/style_chat.h"

namespace Editor {
namespace {

[[nodiscard]] QRect OpaqueBounds(const QImage &image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied);

	const auto width = image.width();
	const auto height = image.height();
	const auto rowHasContent = [&](int y) {
		const auto row = reinterpret_cast<const uint32*>(
			image.constScanLine(y));
		for (auto x = 0; x != width; ++x) {
			if (row[x]) {
				return true;
			}
		}
		return false;
	};
	auto top = 0;
	while (top != height && !rowHasContent(top)) {
		++top;
	}
	if (top == height) {
		return QRect();
	}
	auto bottom = height - 1;
	while (bottom > top && !rowHasContent(bottom)) {
		--bottom;
	}
	auto left = width;
	auto right = -1;
	for (auto y = top; y <= bottom; ++y) {
		const auto row = reinterpret_cast<const uint32*>(
			image.constScanLine(y));
		auto x = 0;
		while (x < left && !row[x]) {
			++x;
		}
		left = std::min(left, x);
		x = width - 1;
		while (x > right && !row[x]) {
			--x;
		}
		right = std::max(right, x);
	}
	return QRect(left, top, right - left + 1, bottom - top + 1);
}

} // namespace

class MessageRenderer::Delegate final
	: public HistoryView::DefaultElementDelegate {
public:
	explicit Delegate(not_null<Ui::PathShiftGradient*> gradient)
	: _gradient(gradient) {
	}

private:
	HistoryView::Context elementContext() override {
		return HistoryView::Context::MediaEditor;
	}
	bool elementAnimationsPaused() override {
		return true;
	}
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override {
		return _gradient;
	}

	const not_null<Ui::PathShiftGradient*> _gradient;

};

MessageRenderer::MessageRenderer(std::shared_ptr<MessageSource> source)
: _source(std::move(source))
, _theme(Window::Theme::DefaultChatThemeOn(_lifetime))
, _style(
	std::make_unique<Ui::ChatStyle>(
		_source->session().colorIndicesValue()))
, _pathGradient(
	HistoryView::MakePathShiftGradient(_style.get(), [=] { repaint(); }))
, _delegate(std::make_unique<Delegate>(_pathGradient.get())) {
	_style->apply(_theme.get());

	const auto session = &_source->session();
	const auto data = &session->data();
	data->viewRepaintRequest(
	) | rpl::filter([=](const Data::RequestViewRepaint &request) {
		return (request.view == _element.get());
	}) | rpl::on_next([=] {
		repaint();
	}, _lifetime);

	data->viewResizeRequest(
	) | rpl::filter([=](not_null<HistoryView::Element*> view) {
		return (view == _element.get());
	}) | rpl::on_next([=] {
		_layoutDirty = true;
		repaint();
	}, _lifetime);

	data->itemViewRefreshRequest(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (item == _source->item());
	}) | rpl::on_next([=] {
		_recreate = true;
		repaint();
	}, _lifetime);

	data->itemDataChanges(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return (item == _source->item());
	}) | rpl::on_next([=] {
		if (_element) {
			_element->itemDataChanged();
			_layoutDirty = true;
			repaint();
		}
	}, _lifetime);

	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		repaint();
	}, _lifetime);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_layoutDirty = true;
		repaint();
	}, _lifetime);

	_source->removed(
	) | rpl::on_next([=] {
		_element = nullptr;
	}, _lifetime);

	createView();
}

MessageRenderer::~MessageRenderer() = default;

void MessageRenderer::setRepaintCallback(Fn<void()> callback) {
	_repaint = std::move(callback);
}

bool MessageRenderer::ready() const {
	return (_element != nullptr);
}

not_null<Ui::ChatTheme*> MessageRenderer::theme() const {
	return _theme.get();
}

void MessageRenderer::createView() {
	const auto item = _source->item();
	_element = item ? item->createView(_delegate.get()) : nullptr;
	_layoutDirty = true;
}

void MessageRenderer::repaint() {
	_base = QImage();
	if (_repaint) {
		_repaint();
	}
}

void MessageRenderer::layout() {
	if (base::take(_recreate)) {
		createView();
	}
	if (!_layoutDirty || !_element) {
		return;
	}
	_layoutDirty = false;
	_element->initDimensions();
	_width = st::msgMargin.left()
		+ st::msgMaxWidth
		+ st::msgMargin.right()
		+ (_element->hasFromPhoto() ? st::msgPhotoSkip : 0);
	_element->resizeGetHeight(_width);
	_base = renderFull(1);
	_bounds = OpaqueBounds(_base).marginsAdded(Margins(1)) & _base.rect();
}

QSize MessageRenderer::size() {
	layout();
	return _bounds.size();
}

QImage MessageRenderer::render(int ratio) {
	Expects(ratio > 0);

	layout();
	if (!_element || _bounds.isEmpty()) {
		return QImage();
	}
	const auto full = (ratio == 1 && !_base.isNull())
		? _base
		: renderFull(ratio);
	const auto bounds = QRect(
		_bounds.topLeft() * ratio,
		_bounds.size() * ratio);
	auto result = full.copy(bounds);
	result.setDevicePixelRatio(ratio);
	return result;
}

QImage MessageRenderer::renderFull(int ratio) {
	const auto full = QSize(_width, _element->height());
	auto result = QImage(
		full * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto p = Painter(&result);
	PainterHighQualityEnabler hq(p);
	const auto rect = Rect(full);
	auto context = _theme->preparePaintContext(
		_style.get(),
		rect,
		rect,
		rect,
		true);
	context.outbg = _element->hasOutLayout();
	_element->draw(p, context);
	paintUserpic(p, full.width(), full.height());
	return result;
}

void MessageRenderer::paintUserpic(Painter &p, int width, int height) {
	if (!_element->displayFromPhoto()) {
		return;
	}
	const auto item = _element->data();
	const auto left = st::historyPhotoLeft;
	const auto top = height - _element->marginBottom() - st::msgPhotoSize;
	const auto size = st::msgPhotoSize;
	if (const auto from = _element->displayFrom()) {
		from->paintUserpicLeft(p, _userpic, left, top, width, size);
	} else if (const auto info = item->displayHiddenSenderInfo()) {
		if (info->customUserpic.empty()) {
			info->emptyUserpic.paintCircle(p, left, top, width, size);
		} else if (!info->paintCustomUserpic(
				p,
				_userpic,
				left,
				top,
				width,
				size)) {
			info->customUserpic.load(&_source->session(), item->fullId());
		}
	}
}

} // namespace Editor
