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
	std::shared_ptr<const RichPage> page;
	std::vector<const HistoryMessageMarkupButton*> shared;
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
void PaintButtonRow(
	Painter &p,
	const LaidOutBlock &block,
	const style::Markdown &st,
	const MarkdownArticlePaintContext &context,
	int outerWidth);
void AddButtonRowRipple(
	const std::shared_ptr<ButtonRowRuntime> &runtime,
	const std::vector<LaidOutButton> &buttons,
	int index,
	QPoint point,
	const style::MarkdownButtonRow &st);
void StopButtonRowRipple(const std::shared_ptr<ButtonRowRuntime> &runtime);

} // namespace Iv::Markdown
