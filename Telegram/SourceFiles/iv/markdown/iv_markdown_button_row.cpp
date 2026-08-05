/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_button_row.h"
#include "api/api_bot.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "lang/lang_keys.h"
#include "ui/effects/animation_value.h"
#include "ui/style/style_core_scale.h"

#include "styles/style_chat.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <algorithm>

namespace Iv::Markdown {
namespace {

using ButtonType = HistoryMessageMarkupButton::Type;
using ButtonColor = HistoryMessageMarkupButton::Color;

[[nodiscard]] const style::icon *ButtonRowIcon(ButtonType type) {
	switch (type) {
	case ButtonType::Url:
	case ButtonType::Auth: return &st::msgBotKbUrlIcon;
	case ButtonType::Buy: return &st::msgBotKbPaymentIcon;
	case ButtonType::SwitchInline:
	case ButtonType::SwitchInlineSame: return &st::msgBotKbSwitchPmIcon;
	case ButtonType::WebView:
	case ButtonType::SimpleWebView: return &st::msgBotKbWebviewIcon;
	case ButtonType::CopyText: return &st::msgBotKbCopyIcon;
	}
	return nullptr;
}

[[nodiscard]] const HistoryMessageMarkupButton *LookupRuntimeButton(
		const std::weak_ptr<ButtonRowRuntime> &weak,
		int index) {
	const auto strong = weak.lock();
	return (strong && (index >= 0) && (index < int(strong->buttons.size())))
		? &strong->buttons[index]
		: nullptr;
}

class RichPageButtonClickHandler final : public ClickHandler {
public:
	RichPageButtonClickHandler(
		std::weak_ptr<ButtonRowRuntime> runtime,
		int index);

	void onClick(ClickContext context) const override;

	QString copyToClipboardText() const override;
	QString copyToClipboardContextItemText() const override;

private:
	[[nodiscard]] const HistoryMessageMarkupButton *lookup() const;

	std::weak_ptr<ButtonRowRuntime> _runtime;
	int _index = 0;

};

RichPageButtonClickHandler::RichPageButtonClickHandler(
	std::weak_ptr<ButtonRowRuntime> runtime,
	int index)
: _runtime(std::move(runtime))
, _index(index) {
}

void RichPageButtonClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto button = lookup();
	if (!button) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	Api::ActivateRichPageBotButton(my, *button);
}

QString RichPageButtonClickHandler::copyToClipboardText() const {
	const auto button = lookup();
	if (!button) {
		return QString();
	}
	switch (button->type) {
	case ButtonType::Url:
	case ButtonType::Auth:
	case ButtonType::CopyText: return QString::fromUtf8(button->data);
	}
	return QString();
}

QString RichPageButtonClickHandler::copyToClipboardContextItemText() const {
	const auto button = lookup();
	if (!button) {
		return QString();
	}
	switch (button->type) {
	case ButtonType::Url:
	case ButtonType::Auth: return tr::lng_context_copy_link(tr::now);
	case ButtonType::CopyText: return tr::lng_context_copy_text(tr::now);
	}
	return QString();
}

const HistoryMessageMarkupButton *RichPageButtonClickHandler::lookup() const {
	return LookupRuntimeButton(_runtime, _index);
}

[[nodiscard]] int NaturalButtonWidth(
		const LaidOutButton &button,
		const style::MarkdownButtonRow &st) {
	const auto icon = ButtonRowIcon(button.type);
	const auto extra = icon ? st.iconExtra : 0;
	return std::max(
		st.height,
		button.label.maxWidth() + 2 * st.padding + extra);
}

// Cell boundaries are floored cumulative prefixes computed in int64, never
// an accumulated float64. With non-negative integer weights the truncating
// division is exactly the floor, so the last boundary lands on `free` with
// no rounding slack, every cell differs from its exact share by less than
// one pixel, and the errors cancel instead of drifting along the row.
// A cell that lands under `floorWidth` is then pinned at the floor, removed
// from the pool, and the width left over is redistributed over the remaining
// weights the same way, until no new cell is pinned; that keeps both the
// exact total and the ratios between the unpinned cells, which an
// independent per-cell clamp would break. The floor is dropped entirely when
// the row is narrower than `count * floorWidth + (count - 1) * spacing`,
// where it is unsatisfiable; otherwise at least one cell always stays
// unpinned, so the pinned cells never eat more than the row.
void DistributeButtonCells(
		std::vector<int> *lefts,
		std::vector<int> *widths,
		const std::vector<int> &weights,
		int available,
		int spacing,
		int floorWidth) {
	const auto count = int(weights.size());
	const auto free = std::max(available - (count - 1) * spacing, 0);
	if (int64(count) * floorWidth > free) {
		floorWidth = 0;
	}
	auto pinned = std::vector<bool>(count, false);
	auto pinnedCount = 0;
	while (true) {
		auto total = int64();
		for (auto i = 0; i != count; ++i) {
			if (!pinned[i]) {
				total += weights[i];
			}
		}
		const auto share = int64(free) - int64(pinnedCount) * floorWidth;
		auto prefix = int64();
		auto bound = int64();
		for (auto i = 0; i != count; ++i) {
			if (pinned[i]) {
				(*widths)[i] = floorWidth;
			} else if (total > 0) {
				prefix += weights[i];
				const auto next = (share * prefix) / total;
				(*widths)[i] = int(next - bound);
				bound = next;
			} else {
				(*widths)[i] = 0;
			}
		}
		auto added = 0;
		for (auto i = 0; i != count; ++i) {
			if (!pinned[i] && ((*widths)[i] < floorWidth)) {
				pinned[i] = true;
				++added;
			}
		}
		if (!added) {
			break;
		}
		pinnedCount += added;
	}
	auto left = 0;
	for (auto i = 0; i != count; ++i) {
		(*lefts)[i] = left;
		left += (*widths)[i] + spacing;
	}
}

void ApplyButtonFallbackLadder(
		LaidOutButton *button,
		int left,
		int width,
		const style::MarkdownButtonRow &st) {
	const auto natural = button->label.maxWidth();
	const auto icon = ButtonRowIcon(button->type);
	const auto extra = icon ? st.iconExtra : 0;
	const auto clearance = (width - natural) / 2;
	const auto iconRoom = st.iconPosition.x() + (icon ? icon->width() : 0);
	const auto withIcon = icon
		&& ((width >= natural + 2 * st.padding + extra)
			|| ((width >= natural + 2 * st.padding)
				&& (clearance >= iconRoom)));
	const auto available = std::max(width - 2 * st.labelMinPadding, 0);
	const auto labelWidth = std::min(natural, available);
	const auto labelHeight = st.labelStyle.font->height;
	button->rect = QRect(left, 0, width, st.height);
	button->elided = (labelWidth < natural);
	button->labelRect = QRect(
		left + (width - labelWidth) / 2,
		(st.height - labelHeight) / 2,
		labelWidth,
		labelHeight);
	button->icon = withIcon ? icon : nullptr;
	button->iconRect = withIcon
		? QRect(
			button->rect.right() + 1 - st.iconPosition.x() - icon->width(),
			button->rect.top() + st.iconPosition.y(),
			icon->width(),
			icon->height())
		: QRect();
}

[[nodiscard]] RichButtonPillColors ResolveButtonColors(
		ButtonColor color,
		const style::Markdown &markdownSt) {
	const auto &st = markdownSt.buttonRow;
	switch (color) {
	case ButtonColor::Primary:
		return (st.primaryBg->c == markdownSt.textColor->c)
			? BubbleGradientPillColors(markdownSt, st.tintBgOpacity, true)
			: RichButtonPillColors{
				.bg = st.primaryBg->c,
				.ripple = st.primaryRipple->c,
				.fg = markdownSt.textColor->c,
			};
	case ButtonColor::Success:
		return {
			.bg = anim::with_alpha(st.successFg->c, st.tintBgOpacity),
			.ripple = anim::with_alpha(st.successFg->c, st.tintRippleOpacity),
			.fg = st.successFg->c,
		};
	case ButtonColor::Danger:
		return {
			.bg = anim::with_alpha(st.dangerFg->c, st.tintBgOpacity),
			.ripple = anim::with_alpha(st.dangerFg->c, st.tintRippleOpacity),
			.fg = st.dangerFg->c,
		};
	}
	return {
		.bg = anim::with_alpha(st.defaultBg->c, st.defaultBgOpacity),
		.ripple = anim::with_alpha(
			st.defaultRipple->c,
			st.defaultRippleOpacity),
		.fg = st.defaultFg->c,
	};
}

void PaintButtonLabel(
		QPainter &p,
		const LaidOutButton &button,
		const MarkdownArticlePaintContext &context,
		const style::TextPalette *palette) {
	if (button.labelRect.isEmpty() || button.label.isEmpty()) {
		return;
	}
	const auto available = std::max(button.labelRect.width(), 1);
	button.label.draw(p, {
		.position = button.labelRect.topLeft(),
		.availableWidth = available,
		.geometry = Ui::Text::SimpleGeometry(available, 1, 0, false),
		.clip = context.clip,
		.palette = palette,
		.now = context.now,
		.elisionLines = 1,
	});
}

void PaintButtonContent(
		QPainter &p,
		const LaidOutButton &button,
		QColor fg,
		const MarkdownArticlePaintContext &context,
		int outerWidth,
		const style::TextPalette *palette) {
	if (button.icon) {
		button.icon->paint(p, button.iconRect.topLeft(), outerWidth, fg);
	}
	p.setPen(fg);
	PaintButtonLabel(p, button, context, palette);
}

void PaintButtonPill(
		QPainter &p,
		const LaidOutButton &button,
		const RichButtonPillColors &colors,
		Ui::RippleAnimation *ripple,
		const style::MarkdownButtonRow &st,
		int outerWidth,
		bool eraseRipple) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st.height / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(colors.bg);
	p.drawRoundedRect(button.rect, radius, radius);
	if (ripple) {
		const auto mode = p.compositionMode();
		if (eraseRipple) {
			p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		}
		ripple->paint(
			p,
			button.rect.x(),
			button.rect.y(),
			outerWidth,
			&colors.ripple);
		p.setCompositionMode(mode);
	}
}

void PaintPlainButton(
		Painter &p,
		const LaidOutButton &button,
		const RichButtonPillColors &colors,
		Ui::RippleAnimation *ripple,
		const style::MarkdownButtonRow &st,
		const MarkdownArticlePaintContext &context,
		int outerWidth,
		bool disabled) {
	PaintButtonPill(p, button, colors, ripple, st, outerWidth, false);
	const auto primary = (button.color == ButtonColor::Primary);
	const auto was = p.opacity();
	if (disabled) {
		p.setOpacity(was * (primary
			? st.disabledPrimaryOpacity
			: st.disabledOpacity));
	}
	PaintButtonContent(
		p,
		button,
		colors.fg,
		context,
		outerWidth,
		&p.textPalette());
	if (disabled) {
		p.setOpacity(was);
	}
}

void PaintPrimaryButton(
		Painter &p,
		const LaidOutButton &button,
		const RichButtonPillColors &colors,
		Ui::RippleAnimation *ripple,
		const style::MarkdownButtonRow &st,
		const MarkdownArticlePaintContext &context,
		int outerWidth,
		bool disabled) {
	const auto palette = &p.textPalette();
	PaintPunchedOutPill(
		p,
		button.rect,
		disabled ? st.disabledPrimaryOpacity : 1.,
		[&](QPainter &q) {
			PaintButtonPill(
				q,
				button,
				colors,
				ripple,
				st,
				outerWidth,
				colors.eraseRipple);
		},
		[&](QPainter &q, QColor fg) {
			PaintButtonContent(q, button, fg, context, outerWidth, palette);
		});
}

} // namespace

ButtonRowRuntime::ButtonRowRuntime(Fn<void()> repaint)
: repaint(std::move(repaint)) {
}

int ButtonRowMinWidth(int count, const style::MarkdownButtonRow &st) {
	return (count > 0)
		? (count * st.height + (count - 1) * st.spacing)
		: 0;
}

void LayoutButtonRowButtons(
		std::vector<LaidOutButton> *buttons,
		TableAlignment alignment,
		int width,
		const style::MarkdownButtonRow &st) {
	Expects(buttons != nullptr);

	const auto count = int(buttons->size());
	if (!count) {
		return;
	}
	auto lefts = std::vector<int>(count, 0);
	auto widths = std::vector<int>(count, 0);
	if (alignment == TableAlignment::None) {
		DistributeButtonCells(
			&lefts,
			&widths,
			std::vector<int>(count, 1),
			width,
			st.spacing,
			st.height);
	} else {
		auto naturals = std::vector<int>(count, 0);
		auto total = 0;
		for (auto i = 0; i != count; ++i) {
			naturals[i] = NaturalButtonWidth((*buttons)[i], st);
			total += naturals[i];
		}
		const auto skips = (count - 1) * st.spacing;
		if (total + skips > width) {
			DistributeButtonCells(
				&lefts,
				&widths,
				naturals,
				width,
				st.spacing,
				st.height);
		} else {
			const auto free = width - total - skips;
			auto left = (alignment == TableAlignment::Left)
				? 0
				: (alignment == TableAlignment::Center)
				? (free / 2)
				: free;
			for (auto i = 0; i != count; ++i) {
				lefts[i] = left;
				widths[i] = naturals[i];
				left += naturals[i] + st.spacing;
			}
		}
	}
	for (auto i = 0; i != count; ++i) {
		ApplyButtonFallbackLadder(
			&(*buttons)[i],
			lefts[i],
			widths[i],
			st);
	}
}

int ButtonRowHitIndex(
		const std::vector<LaidOutButton> &buttons,
		QPoint point) {
	const auto count = int(buttons.size());
	for (auto i = 0; i != count; ++i) {
		if (buttons[i].rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

void RefreshButtonRowHandlers(
		const std::shared_ptr<ButtonRowRuntime> &runtime,
		const PreparedButtonRowBlockData &prepared,
		const std::vector<LaidOutButton> &buttons) {
	if (!runtime) {
		return;
	}
	const auto &list = prepared.buttons;
	const auto count = std::min(int(list.size()), int(buttons.size()));
	runtime->buttons.clear();
	runtime->buttons.reserve(count);
	for (auto i = 0; i != count; ++i) {
		runtime->buttons.push_back(list[i].button);
	}
	runtime->handlers.resize(count);
	for (auto i = 0; i != count; ++i) {
		if (buttons[i].type == ButtonType::Disabled) {
			runtime->handlers[i] = nullptr;
		} else if (!runtime->handlers[i]) {
			runtime->handlers[i]
				= std::make_shared<RichPageButtonClickHandler>(runtime, i);
		}
	}
	if (runtime->rippleIndex >= count) {
		runtime->rippleIndex = -1;
		runtime->ripple = nullptr;
	}
}

// A punched-out pill is composed offscreen so that its label glyphs, its
// text-colored custom emoji and any icon can be erased out of the accent
// fill with CompositionMode_DestinationOut. No code here reads the surface
// behind the pill: the holes simply expose whatever the host already
// painted, which is what lets one painter serve an incoming bubble, an
// outgoing bubble and the article surface alike. The content is then drawn
// once more on the real painter with a fully transparent color: plain
// glyphs and text-colored custom emoji contribute nothing there, while
// colorful emoji and non-text-colored custom emoji repaint themselves into
// the holes they punched and so stay colorful above the fill.
void PaintPunchedOutPill(
		QPainter &p,
		QRect rect,
		float64 contentOpacity,
		const Fn<void(QPainter&)> &paintBackground,
		const Fn<void(QPainter&, QColor)> &paintContent) {
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(
		rect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		q.translate(-rect.topLeft());
		paintBackground(q);
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		q.setOpacity(contentOpacity);
		paintContent(q, QColor(Qt::white));
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	}
	p.drawImage(rect.topLeft(), frame);
	const auto was = p.opacity();
	p.setOpacity(was * contentOpacity);
	paintContent(p, QColor(Qt::transparent));
	p.setOpacity(was);
}

RichButtonPillColors BubbleGradientPillColors(
		const style::Markdown &st,
		float64 tintBgOpacity,
		bool primary) {
	const auto foreground = st.textColor->c;
	const auto ripple = anim::with_alpha(
		foreground,
		st.buttonRow.tintRippleOpacity);
	return primary
		? RichButtonPillColors{
			.bg = foreground,
			.ripple = ripple,
			.punchOut = true,
			.eraseRipple = true,
		}
		: RichButtonPillColors{
			.bg = anim::with_alpha(foreground, tintBgOpacity),
			.ripple = ripple,
			.fg = foreground,
		};
}

void PaintButtonRow(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context,
		int outerWidth) {
	const auto &paintSt = context.paintMarkdownStyle(st);
	const auto &style = paintSt.buttonRow;
	const auto &runtime = block.buttonRowRuntime;
	const auto count = int(block.buttons.size());
	for (auto i = 0; i != count; ++i) {
		const auto &button = block.buttons[i];
		if (button.rect.isEmpty()
			|| (!context.clip.isNull()
				&& !button.rect.intersects(context.clip))) {
			continue;
		}
		const auto colors = context.bubbleGradient
			? BubbleGradientPillColors(
				paintSt,
				style.tintBgOpacity,
				(button.color == ButtonColor::Primary))
			: ResolveButtonColors(button.color, paintSt);
		const auto disabled = (button.type == ButtonType::Disabled);
		const auto ripple = (runtime
			&& runtime->ripple
			&& (runtime->rippleIndex == i))
			? runtime->ripple.get()
			: nullptr;
		if (colors.punchOut) {
			PaintPrimaryButton(
				p,
				button,
				colors,
				ripple,
				style,
				context,
				outerWidth,
				disabled);
		} else {
			PaintPlainButton(
				p,
				button,
				colors,
				ripple,
				style,
				context,
				outerWidth,
				disabled);
		}
	}
}

void AddPillRipple(
		not_null<std::unique_ptr<Ui::RippleAnimation>*> ripple,
		not_null<QSize*> rippleSize,
		QSize size,
		QPoint point,
		Fn<void()> repaint) {
	if (size.isEmpty()) {
		return;
	} else if (!*ripple || (*rippleSize != size)) {
		*ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::RoundRectMask(size, size.height() / 2),
			[=] {
				if (repaint) {
					repaint();
				}
			});
		*rippleSize = size;
	}
	point.setX(std::clamp(point.x(), 0, std::max(size.width() - 1, 0)));
	point.setY(std::clamp(point.y(), 0, std::max(size.height() - 1, 0)));
	(*ripple)->add(point);
	if (repaint) {
		repaint();
	}
}

void StopPillRipple(
		const std::unique_ptr<Ui::RippleAnimation> &ripple,
		const Fn<void()> &repaint) {
	if (!ripple) {
		return;
	}
	ripple->lastStop();
	if (repaint) {
		repaint();
	}
}

void AddButtonRowRipple(
		const std::shared_ptr<ButtonRowRuntime> &runtime,
		const std::vector<LaidOutButton> &buttons,
		int index,
		QPoint point) {
	if (!runtime
		|| (index < 0)
		|| (index >= int(buttons.size()))
		|| (index >= int(runtime->handlers.size()))
		|| !runtime->handlers[index]) {
		return;
	}
	const auto size = buttons[index].rect.size();
	if (size.isEmpty()) {
		return;
	} else if (runtime->rippleIndex != index) {
		runtime->ripple = nullptr;
		runtime->rippleIndex = index;
	}
	AddPillRipple(
		&runtime->ripple,
		&runtime->rippleSize,
		size,
		point,
		runtime->repaint);
}

void StopButtonRowRipple(const std::shared_ptr<ButtonRowRuntime> &runtime) {
	if (!runtime) {
		return;
	}
	StopPillRipple(runtime->ripple, runtime->repaint);
}

} // namespace Iv::Markdown
