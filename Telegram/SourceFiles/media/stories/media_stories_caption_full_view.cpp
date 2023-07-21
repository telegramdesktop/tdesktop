/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_caption_full_view.h"

#include "core/ui_integration.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "styles/style_media_view.h"

namespace Media::Stories {

CaptionFullView::CaptionFullView(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	const TextWithEntities &text,
	Fn<void()> close)
: RpWidget(parent)
, _scroll(std::make_unique<Ui::ScrollArea>((RpWidget*)this))
, _text(_scroll->setOwnedWidget(
	object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		_scroll.get(),
		object_ptr<Ui::FlatLabel>(_scroll.get(), st::storiesCaptionFull),
		st::mediaviewCaptionPadding))->entity())
, _close(std::move(close))
, _background(st::storiesRadius, st::mediaviewCaptionBg) {
	_text->setMarkedText(text, Core::MarkedTextContext{
		.session = session,
		.customEmojiRepaint = [=] { _text->update(); },
	});

	parent->sizeValue() | rpl::start_with_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}, lifetime());

	show();
	setFocus();
}

CaptionFullView::~CaptionFullView() = default;

void CaptionFullView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_background.paint(p, _scroll->geometry());
	_background.paint(p, _scroll->geometry());
}

void CaptionFullView::resizeEvent(QResizeEvent *e) {
	const auto wanted = _text->naturalWidth();
	const auto padding = st::mediaviewCaptionPadding;
	const auto margin = st::mediaviewCaptionMargin * 2;
	const auto available = (rect() - padding).width()
		- (margin.width() * 2);
	const auto use = std::min(wanted, available);
	_text->resizeToWidth(use);
	const auto fullw = use + padding.left() + padding.right();
	const auto fullh = std::min(
		_text->height() + padding.top() + padding.bottom(),
		height() - (margin.height() * 2));
	const auto left = (width() - fullw) / 2;
	const auto top = (height() - fullh) / 2;
	_scroll->setGeometry(left, top, fullw, fullh);
}

void CaptionFullView::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (const auto onstack = _close) {
			onstack();
		}
	}
}

void CaptionFullView::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (const auto onstack = _close) {
			onstack();
		}
	}
}

} // namespace Media::Stories
