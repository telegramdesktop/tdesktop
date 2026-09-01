/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/effects/glare.h"
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
	std::vector<QByteArray> loadingKeys;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	QSize rippleSize;
	int rippleIndex = -1;
};

struct RichButtonPillColors {
	QColor bg;
	QColor ripple;
	QColor fg;
	bool punchOut = false;
};

struct RichButtonLoadingState {
	Ui::GlareEffect glare;
	base::Timer timer;
	Fn<void()> repaint;
	crl::time lastPaintedAt = 0;
	crl::time lastCoveringPassAt = 0;
};

struct RichButtonLoading {
	RichButtonLoadingState *state = nullptr;
	::Data::Session *owner = nullptr;
	FullMsgId itemId;
};

struct RichButtonLoadingCoverage {
	int top = 0;
	int bottom = 0;
};

[[nodiscard]] RichButtonPillColors BubbleGradientPillColors(
	const style::Markdown &st,
	float64 tintBgOpacity,
	bool primary);

[[nodiscard]] RichButtonPillColors PrimaryPillColors(
	const style::Markdown &st,
	QColor bg,
	QColor ripple);

[[nodiscard]] const style::icon *RichButtonIcon(
	HistoryMessageMarkupButton::Type type);

[[nodiscard]] int ButtonRowMinWidth(
	int count,
	const style::MarkdownButtonRow &st);
[[nodiscard]] int ButtonRowControlReserve();
[[nodiscard]] QRect ButtonRowControlRect(QRect outer);
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
[[nodiscard]] bool RichButtonLoadingPassCovered(
	RichButtonLoadingCoverage coverage,
	QRect laidOut,
	QRect clip);
[[nodiscard]] QByteArray RichButtonLoadingKey(
	const HistoryMessageMarkupButton &button);
[[nodiscard]] RichButtonLoadingState *RichButtonLoadingActive(
	const RichButtonLoading &loading,
	const QByteArray &key);
void PaintRichButtonLoading(
	QPainter &p,
	not_null<RichButtonLoadingState*> state,
	const RichButtonPillColors &colors,
	QRect rect,
	int radius);
void PaintButtonRow(
	Painter &p,
	const LaidOutBlock &block,
	const style::Markdown &st,
	const MarkdownArticlePaintContext &context,
	int outerWidth);
void PaintRichButtonPreview(
	Painter &p,
	const LaidOutButton &button,
	const style::Markdown &st,
	QRect clip,
	crl::time now,
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
