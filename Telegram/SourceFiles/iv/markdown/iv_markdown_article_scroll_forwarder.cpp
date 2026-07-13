/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_scroll_forwarder.h"

#include "base/qt/qt_common_adapters.h"
#include "base/algorithm.h"
#include "iv/markdown/iv_markdown_article.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QTouchEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <cmath>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QPoint LocalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

} // namespace

bool MarkdownArticleScrollForwarder::IsTouchEvent(QEvent *e) {
	const auto type = e->type();
	return (type == QEvent::TouchBegin)
		|| (type == QEvent::TouchUpdate)
		|| (type == QEvent::TouchEnd)
		|| (type == QEvent::TouchCancel);
}

void MarkdownArticleScrollForwarder::handleWheel(
		MarkdownArticle *article,
		QWheelEvent *e,
		QPoint articleTopLeft) {
	if (!article) {
		(void)_scrollDirectionLock.update(e->phase(), {});
		e->ignore();
		return;
	}
	const auto delta = Ui::ScrollDeltaF(e);
	const auto locked = _scrollDirectionLock.update(e->phase(), delta);
	const auto horizontal = locked
		? (*locked == Qt::Horizontal)
		: (std::abs(delta.x()) > std::abs(delta.y()));
	const auto local = LocalPosition(e) - articleTopLeft;
	if (!article->horizontalScrollHit(local).scrollable) {
		e->ignore();
		return;
	}
	if (horizontal) {
		(void)article->consumeHorizontalScroll(
			local,
			int(std::round(delta.x())));
		e->accept();
	} else {
		e->ignore();
	}
}

bool MarkdownArticleScrollForwarder::handleMousePress(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft) {
	if (!article || e->button() != Qt::LeftButton) {
		return false;
	}
	if (!article->beginHorizontalScroll(e->pos() - articleTopLeft, false)) {
		return false;
	}
	_mouseDrag = true;
	e->accept();
	return true;
}

bool MarkdownArticleScrollForwarder::handleMouseMove(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft) {
	if (!_mouseDrag) {
		return false;
	}
	if (article) {
		(void)article->updateHorizontalScroll(e->pos() - articleTopLeft);
	}
	e->accept();
	return true;
}

bool MarkdownArticleScrollForwarder::handleMouseRelease(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft) {
	if (!_mouseDrag || e->button() != Qt::LeftButton) {
		return false;
	}
	_mouseDrag = false;
	if (article) {
		(void)article->updateHorizontalScroll(e->pos() - articleTopLeft);
		article->endHorizontalScroll();
	}
	e->accept();
	return true;
}

bool MarkdownArticleScrollForwarder::handleTouchHook(
		MarkdownArticle *article,
		QWidget *widget,
		QEvent *e,
		QPoint articleTopLeft) {
	if (!IsTouchEvent(e)) {
		return false;
	}
	const auto touch = static_cast<QTouchEvent*>(e);
	if (touch->device()->type() != base::TouchDevice::TouchScreen) {
		return false;
	}
	return handleTouch(article, widget, touch, articleTopLeft);
}

bool MarkdownArticleScrollForwarder::handleTouch(
		MarkdownArticle *article,
		QWidget *widget,
		QTouchEvent *e,
		QPoint articleTopLeft) {
	const auto wasActive = _touchScroll;
	if (e->type() == QEvent::TouchCancel) {
		_pendingTouchPoint = std::nullopt;
		if (!base::take(_touchScroll)) {
			return false;
		}
		if (article) {
			article->endHorizontalScroll();
		}
		e->accept();
		return true;
	}
	if (!article || e->touchPoints().isEmpty()) {
		return wasActive;
	}
	const auto local = widget->mapFromGlobal(
		e->touchPoints().cbegin()->screenPos().toPoint()) - articleTopLeft;
	switch (e->type()) {
	case QEvent::TouchBegin: {
		_pendingTouchPoint = std::nullopt;
		const auto hit = article->horizontalScrollHit(local);
		_touchScroll = hit.overScrollbar
			&& article->beginHorizontalScroll(local, false);
		if (!_touchScroll && hit.overViewport) {
			_pendingTouchPoint = local;
		}
		if (_touchScroll) {
			e->accept();
		}
	} break;
	case QEvent::TouchUpdate:
		if (_touchScroll) {
			(void)article->updateHorizontalScroll(local);
			e->accept();
		} else if (_pendingTouchPoint) {
			const auto delta = local - *_pendingTouchPoint;
			if (delta.manhattanLength() < QApplication::startDragDistance()) {
				break;
			}
			const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
			if (!horizontal) {
				_pendingTouchPoint = std::nullopt;
				break;
			}
			_touchScroll = article->beginHorizontalScroll(
				*base::take(_pendingTouchPoint),
				true);
			if (_touchScroll) {
				(void)article->updateHorizontalScroll(local);
				e->accept();
			}
		}
		break;
	case QEvent::TouchEnd:
		_pendingTouchPoint = std::nullopt;
		if (base::take(_touchScroll)) {
			article->endHorizontalScroll();
			e->accept();
		}
		break;
	default:
		break;
	}
	return wasActive || _touchScroll;
}

void MarkdownArticleScrollForwarder::reset(MarkdownArticle *article) {
	const auto mouse = base::take(_mouseDrag);
	const auto touch = base::take(_touchScroll);
	if (article && (mouse || touch)) {
		article->endHorizontalScroll();
	}
	_pendingTouchPoint = std::nullopt;
	_scrollDirectionLock.reset();
}

} // namespace Iv::Markdown
