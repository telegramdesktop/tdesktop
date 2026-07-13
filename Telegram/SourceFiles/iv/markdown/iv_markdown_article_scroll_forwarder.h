/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/ui_utility.h"

#include <optional>
#include <QtCore/QPoint>

class QEvent;
class QMouseEvent;
class QTouchEvent;
class QWheelEvent;
class QWidget;

namespace Iv::Markdown {

class MarkdownArticle;

class MarkdownArticleScrollForwarder final {
public:
	[[nodiscard]] static bool IsTouchEvent(QEvent *e);

	void handleWheel(
		MarkdownArticle *article,
		QWheelEvent *e,
		QPoint articleTopLeft);
	[[nodiscard]] bool handleMousePress(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft);
	[[nodiscard]] bool handleMouseMove(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft);
	[[nodiscard]] bool handleMouseRelease(
		MarkdownArticle *article,
		QMouseEvent *e,
		QPoint articleTopLeft);
	[[nodiscard]] bool handleTouchHook(
		MarkdownArticle *article,
		QWidget *widget,
		QEvent *e,
		QPoint articleTopLeft);
	void reset(MarkdownArticle *article);

private:
	[[nodiscard]] bool handleTouch(
		MarkdownArticle *article,
		QWidget *widget,
		QTouchEvent *e,
		QPoint articleTopLeft);

	Ui::ScrollDirectionLock _scrollDirectionLock;
	std::optional<QPoint> _pendingTouchPoint;
	bool _mouseDrag = false;
	bool _touchScroll = false;

};

} // namespace Iv::Markdown
