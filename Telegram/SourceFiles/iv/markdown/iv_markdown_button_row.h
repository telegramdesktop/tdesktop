/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"

#include "ui/effects/ripple_animation.h"
#include "ui/style/style_core_types.h"
#include "ui/text/text.h"
#include "ui/click_handler.h"
#include "ui/painter.h"

#include <memory>
#include <vector>

namespace style {
struct Markdown;
struct MarkdownButtonRow;
} // namespace style

namespace Iv::Markdown {

struct LaidOutBlock;
struct MarkdownArticlePaintContext;

struct LaidOutButton {
	Ui::Text::String label;
	QString fullLabel;
	QRect rect;
	QRect labelRect;
	QRect iconRect;
	QRect logicalRect;
	QRect logicalLabelRect;
	QRect logicalIconRect;
	const style::icon *icon = nullptr;
	HistoryMessageMarkupButton::Type type
		= HistoryMessageMarkupButton::Type::Disabled;
	HistoryMessageMarkupButton::Color color
		= HistoryMessageMarkupButton::Color::Normal;
	bool elided = false;
};

struct ButtonRowRuntime {
	explicit ButtonRowRuntime(Fn<void()> repaint);

	Fn<void()> repaint;
	std::vector<HistoryMessageMarkupButton> buttons;
	std::vector<ClickHandlerPtr> handlers;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	QSize rippleSize;
	int rippleIndex = -1;
};

[[nodiscard]] int ButtonRowMinWidth(
	int count,
	const style::MarkdownButtonRow &st);
void LayoutButtonRowButtons(
	std::vector<LaidOutButton> *buttons,
	TableAlignment alignment,
	int width,
	const style::MarkdownButtonRow &st);
[[nodiscard]] int ButtonRowHitIndex(
	const std::vector<LaidOutButton> &buttons,
	QPoint point);
void RefreshButtonRowHandlers(
	const std::shared_ptr<ButtonRowRuntime> &runtime,
	const PreparedButtonRowBlockData &prepared,
	const std::vector<LaidOutButton> &buttons);
void PaintPunchedOutPill(
	QPainter &p,
	QRect rect,
	float64 contentOpacity,
	const Fn<void(QPainter&)> &paintBackground,
	const Fn<void(QPainter&, QColor)> &paintContent);
void PaintButtonRow(
	Painter &p,
	const LaidOutBlock &block,
	const style::Markdown &st,
	const MarkdownArticlePaintContext &context,
	int outerWidth);
void AddPillRipple(
	not_null<std::unique_ptr<Ui::RippleAnimation>*> ripple,
	not_null<QSize*> rippleSize,
	QSize size,
	QPoint point,
	Fn<void()> repaint);
void StopPillRipple(
	const std::unique_ptr<Ui::RippleAnimation> &ripple,
	const Fn<void()> &repaint);
void AddButtonRowRipple(
	const std::shared_ptr<ButtonRowRuntime> &runtime,
	const std::vector<LaidOutButton> &buttons,
	int index,
	QPoint point);
void StopButtonRowRipple(const std::shared_ptr<ButtonRowRuntime> &runtime);

} // namespace Iv::Markdown
