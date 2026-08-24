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
#include "ui/color_contrast.h"

#include "styles/style_chat.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <algorithm>

namespace Iv::Markdown {
namespace {

using ButtonType = HistoryMessageMarkupButton::Type;
using ButtonColor = HistoryMessageMarkupButton::Color;

constexpr auto kMinPrimaryLabelContrast = 1.5;
constexpr auto kLoadingGlareOpacity = 0.2;
constexpr auto kLoadingDisabledOutlineOpacity = 0.6;
constexpr auto kLoadingHighlightOpacity = 1.;
constexpr auto kLoadingGlareTimeout = crl::time(0);
constexpr auto kLoadingGlareDuration = crl::time(1100);
constexpr auto kLoadingIdleTimeout = crl::time(500);
constexpr auto kLoadingDisabledPollInterval = crl::time(300);

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
	const auto icon = RichButtonIcon(button.type);
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
	const auto icon = RichButtonIcon(button->type);
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
		return PrimaryPillColors(
			markdownSt,
			st.primaryBg->c,
			st.primaryRipple->c);
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
		QRect clip,
		crl::time now,
		const style::TextPalette *palette) {
	if (button.labelRect.isEmpty() || button.label.isEmpty()) {
		return;
	}
	const auto available = std::max(button.labelRect.width(), 1);
	button.label.draw(p, {
		.position = button.labelRect.topLeft(),
		.availableWidth = available,
		.geometry = Ui::Text::SimpleGeometry(available, 1, 0, false),
		.clip = clip,
		.palette = palette,
		.now = now,
		.elisionLines = 1,
	});
}

void PaintButtonContent(
		QPainter &p,
		const LaidOutButton &button,
		QColor fg,
		QRect clip,
		crl::time now,
		int outerWidth,
		const style::TextPalette *palette) {
	if (button.icon) {
		button.icon->paint(p, button.iconRect.topLeft(), outerWidth, fg);
	}
	p.setPen(fg);
	PaintButtonLabel(p, button, clip, now, palette);
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
		QRect clip,
		crl::time now,
		int outerWidth,
		bool disabled,
		RichButtonLoadingState *loading) {
	PaintButtonPill(p, button, colors, ripple, st, outerWidth, false);
	if (loading) {
		PaintRichButtonLoading(p, loading, colors, button.rect, st.height / 2);
	}
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
		clip,
		now,
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
		QRect clip,
		crl::time now,
		int outerWidth,
		bool disabled,
		RichButtonLoadingState *loading) {
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
				colors.punchOut);
			if (loading) {
				PaintRichButtonLoading(
					q,
					loading,
					colors,
					button.rect,
					st.height / 2);
			}
		},
		[&](QPainter &q, QColor fg) {
			PaintButtonContent(
				q,
				button,
				fg,
				clip,
				now,
				outerWidth,
				palette);
		});
}

void PaintOneButton(
		Painter &p,
		const LaidOutButton &button,
		const RichButtonPillColors &colors,
		Ui::RippleAnimation *ripple,
		const style::MarkdownButtonRow &st,
		QRect clip,
		crl::time now,
		int outerWidth,
		bool disabled,
		RichButtonLoadingState *loading) {
	if (colors.punchOut) {
		PaintPrimaryButton(
			p,
			button,
			colors,
			ripple,
			st,
			clip,
			now,
			outerWidth,
			disabled,
			loading);
	} else {
		PaintPlainButton(
			p,
			button,
			colors,
			ripple,
			st,
			clip,
			now,
			outerWidth,
			disabled,
			loading);
	}
}

void PaintRichButtonLoadingOutline(
		QPainter &p,
		QRect rect,
		int radius,
		const QBrush &brush) {
	const auto pen = st::lineWidth;
	const auto half = pen / 2.;
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(brush, pen));
	p.drawRoundedRect(
		QRectF(rect).adjusted(half, half, -half, -half),
		radius - half,
		radius - half);
}

[[nodiscard]] bool RichButtonLoadingIdle(
		not_null<RichButtonLoadingState*> state,
		crl::time now) {
	return (state->lastCoveringPassAt > state->lastPaintedAt)
		|| (now > state->lastPaintedAt + kLoadingIdleTimeout);
}

void RichButtonLoadingTick(not_null<RichButtonLoadingState*> state) {
	if (RichButtonLoadingIdle(state, crl::now())) {
		state->glare.animation.stop();
		state->timer.cancel();
	} else if (state->repaint) {
		state->repaint();
	}
}

} // namespace

ButtonRowRuntime::ButtonRowRuntime(Fn<void()> repaint)
: repaint(std::move(repaint)) {
}

const style::icon *RichButtonIcon(ButtonType type) {
	using TypeIcon = HistoryMessageMarkupButton::TypeIcon;
	switch (HistoryMessageMarkupButton::IconOfType(type)) {
	case TypeIcon::Url: return &st::msgBotKbUrlIcon;
	case TypeIcon::Payment: return &st::msgBotKbPaymentIcon;
	case TypeIcon::SwitchPm: return &st::msgBotKbSwitchPmIcon;
	case TypeIcon::Webview: return &st::msgBotKbWebviewIcon;
	case TypeIcon::Copy: return &st::msgBotKbCopyIcon;
	case TypeIcon::None: return nullptr;
	}
	Unexpected("TypeIcon in Iv::Markdown::RichButtonIcon.");
}

int ButtonRowMinWidth(int count, const style::MarkdownButtonRow &st) {
	return (count > 0)
		? (count * st.height + (count - 1) * st.spacing)
		: 0;
}

int ButtonRowControlReserve() {
	return st::ivEditorButtonRowControlSkip
		+ st::ivEditorButtonRowControlSize;
}

QRect ButtonRowControlRect(QRect outer) {
	const auto size = st::ivEditorButtonRowControlSize;
	return QRect(
		outer.x() + outer.width() - size,
		outer.y() + (outer.height() - size) / 2,
		size,
		size);
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
	runtime->loadingKeys.resize(count);
	for (auto i = 0; i != count; ++i) {
		runtime->loadingKeys[i] = RichButtonLoadingKey(runtime->buttons[i]);
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

// A host that knows which part of the article was on screen states it as a
// band in the article's own logical coordinates, derived from knowledge it
// holds independently of the paint clip. The pass covered everything that
// could have painted a loading pill when the clip contains that band across
// the article's full laid-out width. A host that states no band is judged by
// the whole laid-out rect instead, which is what a host that paints the
// whole article it lays out means.
bool RichButtonLoadingPassCovered(
		RichButtonLoadingCoverage coverage,
		QRect laidOut,
		QRect clip) {
	const auto covered = (coverage.bottom > coverage.top)
		? laidOut.intersected(QRect(
			laidOut.x(),
			coverage.top,
			laidOut.width(),
			coverage.bottom - coverage.top))
		: laidOut;
	return !covered.isEmpty() && clip.contains(covered);
}

QByteArray RichButtonLoadingKey(
		const HistoryMessageMarkupButton &button) {
	return HistoryMessageMarkupButton::LoadsOnActivate(button.type)
		? HistoryMessageMarkupButton::RichPageButtonKey(button)
		: QByteArray();
}

RichButtonLoadingState *RichButtonLoadingActive(
		const RichButtonLoading &loading,
		const QByteArray &key) {
	if (!loading.state || !loading.owner || key.isEmpty()) {
		return nullptr;
	}
	const auto record = HistoryMessageMarkupButton::GetRichPageButton(
		loading.owner,
		loading.itemId,
		key);
	return (record && record->requestId) ? loading.state : nullptr;
}

// While the glare runs, the outline's opacity is the travelling gradient's
// own horizontal profile rather than a constant, so the border is bright
// only where the band is. Everything is confined to the pill by
// construction, so no offscreen mask is needed. On a punched-out pill there
// is no resolved foreground to borrow, so the effect erases out of the
// accent fill instead, exactly as its label and its ripple already do.
void PaintRichButtonLoading(
		QPainter &p,
		not_null<RichButtonLoadingState*> state,
		const RichButtonPillColors &colors,
		QRect rect,
		int radius) {
	state->lastPaintedAt = crl::now();
	const auto erase = colors.punchOut;
	const auto color = erase ? QColor(Qt::white) : colors.fg;
	auto hq = PainterHighQualityEnabler(p);
	const auto mode = p.compositionMode();
	const auto guard = gsl::finally([&] {
		p.setCompositionMode(mode);
	});
	if (erase) {
		p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	}
	if (anim::Disabled()) {
		state->glare.animation.stop();
		if (!state->timer.isActive()) {
			const auto raw = state.get();
			state->timer.setCallback([raw] { RichButtonLoadingTick(raw); });
			state->timer.callEach(kLoadingDisabledPollInterval);
		}
		PaintRichButtonLoadingOutline(
			p,
			rect,
			radius,
			anim::with_alpha(color, kLoadingDisabledOutlineOpacity));
		return;
	}
	state->timer.cancel();
	if (!state->glare.animation.animating()) {
		const auto raw = state.get();
		state->glare.glare = {};
		state->glare.validate(
			color,
			[raw] { RichButtonLoadingTick(raw); },
			kLoadingGlareTimeout,
			kLoadingGlareDuration);
		return;
	} else if (!state->glare.glare.birthTime) {
		return;
	}
	const auto progress = std::clamp(
		state->glare.progress(crl::now()),
		0.,
		1.);
	const auto band = rect.width();
	const auto left = rect.x() - band + (band * 2) * progress;
	const auto stops = state->glare.computeGradient(color).stops();
	const auto sweep = [&](float64 opacity) {
		auto result = QLinearGradient(left, 0, left + band, 0);
		auto copy = stops;
		copy[1].second = anim::with_alpha(color, opacity);
		result.setStops(copy);
		return result;
	};
	p.setPen(Qt::NoPen);
	p.setBrush(QBrush(sweep(kLoadingGlareOpacity)));
	p.drawRoundedRect(rect, radius, radius);
	PaintRichButtonLoadingOutline(
		p,
		rect,
		radius,
		QBrush(sweep(kLoadingHighlightOpacity)));
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
		}
		: RichButtonPillColors{
			.bg = anim::with_alpha(foreground, tintBgOpacity),
			.ripple = ripple,
			.fg = foreground,
		};
}

RichButtonPillColors PrimaryPillColors(
		const style::Markdown &st,
		QColor bg,
		QColor ripple) {
	const auto fg = st.buttonRow.primaryFg->c;
	return (Ui::CountContrast(bg, fg) < kMinPrimaryLabelContrast)
		? BubbleGradientPillColors(st, 0., true)
		: RichButtonPillColors{
			.bg = bg,
			.ripple = ripple,
			.fg = fg,
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
		const auto loading = (runtime
			&& (i < int(runtime->loadingKeys.size())))
			? RichButtonLoadingActive(
				context.buttonLoading,
				runtime->loadingKeys[i])
			: nullptr;
		PaintOneButton(
			p,
			button,
			colors,
			ripple,
			style,
			context.clip,
			context.now,
			outerWidth,
			disabled,
			loading);
	}
}

void PaintRichButtonPreview(
		Painter &p,
		const LaidOutButton &button,
		const style::Markdown &st,
		QRect clip,
		crl::time now,
		int outerWidth) {
	const auto colors = ResolveButtonColors(button.color, st);
	PaintOneButton(
		p,
		button,
		colors,
		nullptr,
		st.buttonRow,
		clip,
		now,
		outerWidth,
		false,
		nullptr);
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
