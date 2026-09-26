/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_text.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_emoji_document.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>

namespace Editor {
namespace {

constexpr auto kPaddingFactor = 0.4;
constexpr auto kMaxWidthFactor = 0.8;
constexpr auto kMinContentWidth = 20;
constexpr auto kBrightnessFramedThreshold = 0.721;
constexpr auto kBrightnessSemiTransparentThreshold = 0.25;
constexpr auto kSemiTransparentAlpha = 0x99;
constexpr auto kCornerRadiusFactor = 1. / 3.;
constexpr auto kLinePadHFactor = 1. / 3.;
constexpr auto kMergeRadiusFactor = 1.5;

constexpr auto kNoWrapWidth = 1e9;

struct LayoutMetrics {
	int contentWidth = 0;
	int contentHeight = 0;
	int padding = 0;
	int textMaxWidth = 0;
};

constexpr auto kCondensedStretch = 78;

QFont TextFont(float64 fontSize, TextTypeface typeface) {
	auto font = QFont();
	font.setPixelSize(std::max(int(fontSize), 1));
	font.setWeight(QFont::DemiBold);
	switch (typeface) {
	case TextTypeface::Default:
		break;
	case TextTypeface::Italic:
		font.setItalic(true);
		break;
	case TextTypeface::Serif:
		font.setStyleHint(QFont::Serif);
		font.setFamily(u"serif"_q);
		break;
	case TextTypeface::Condensed:
		font.setStretch(kCondensedStretch);
		break;
	case TextTypeface::Monospace:
		font.setStyleHint(QFont::Monospace);
		font.setFamily(u"monospace"_q);
		break;
	}
	return font;
}

[[nodiscard]] float64 AlignOffset(
		int contentWidth,
		float64 lineWidth,
		TextAlignment alignment) {
	switch (alignment) {
	case TextAlignment::Center:
		return (contentWidth - lineWidth) / 2.;
	case TextAlignment::Left:
		return 0.;
	case TextAlignment::Right:
		return contentWidth - lineWidth;
	}
	Unexpected("Alignment in AlignOffset.");
}

float64 ComputeBrightness(const QColor &color) {
	return (color.red() * 0.2126
		+ color.green() * 0.7152
		+ color.blue() * 0.0722) / 255.;
}

struct PreparedLayout {
	std::unique_ptr<QTextLayout> layout;
	LayoutMetrics metrics;
};

[[nodiscard]] PreparedLayout PrepareLayout(
		const QString &processedText,
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style,
		TextTypeface typeface,
		const QVector<QTextLayout::FormatRange> &formats = {}) {
	const auto spec = ComputeTextLayoutSpec(
		fontSize,
		imageSize,
		style,
		typeface);
	const auto textMaxWidth = spec.maxTextWidth;

	auto option = QTextOption();
	option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

	auto layout = std::make_unique<QTextLayout>(processedText, spec.font);
	layout->setTextOption(option);
	if (!formats.isEmpty()) {
		layout->setFormats(formats);
	}
	layout->beginLayout();

	auto totalHeight = 0.;
	auto maxWidth = 0.;
	while (true) {
		auto line = layout->createLine();
		if (!line.isValid()) {
			break;
		}
		line.setLineWidth(kNoWrapWidth);
		line.setPosition(QPointF(0, totalHeight));
		totalHeight += line.height();
		maxWidth = std::max(maxWidth, float64(line.naturalTextWidth()));
	}
	layout->endLayout();

	return {
		.layout = std::move(layout),
		.metrics = {
			.contentWidth = std::max(
				int(std::ceil(maxWidth)),
				kMinContentWidth),
			.contentHeight = int(std::ceil(totalHeight)),
			.padding = spec.padding,
			.textMaxWidth = textMaxWidth,
		},
	};
}

struct LineRect {
	float64 left = 0;
	float64 top = 0;
	float64 right = 0;
	float64 bottom = 0;
	[[nodiscard]] float64 width() const { return right - left; }
};

[[nodiscard]] TextAlignment NextAlignment(TextAlignment alignment) {
	switch (alignment) {
	case TextAlignment::Left: return TextAlignment::Center;
	case TextAlignment::Center: return TextAlignment::Right;
	case TextAlignment::Right: return TextAlignment::Left;
	}
	Unexpected("Alignment in NextAlignment.");
}

[[nodiscard]] int AlignRestFrame(TextAlignment alignment) {
	switch (alignment) {
	case TextAlignment::Left: return 20;
	case TextAlignment::Center: return 0;
	case TextAlignment::Right: return 40;
	}
	Unexpected("Alignment in AlignRestFrame.");
}

struct AlignFrames {
	int from = 0;
	int to = 0;
};

[[nodiscard]] AlignFrames AlignTransitionFrames(
		TextAlignment from,
		TextAlignment to) {
	if (from == TextAlignment::Left) {
		return (to == TextAlignment::Center)
			? AlignFrames{ 20, 0 }
			: AlignFrames{ 20, 40 };
	} else if (from == TextAlignment::Center) {
		return (to == TextAlignment::Left)
			? AlignFrames{ 0, 20 }
			: AlignFrames{ 60, 40 };
	}
	return (to == TextAlignment::Left)
		? AlignFrames{ 40, 20 }
		: AlignFrames{ 40, 60 };
}

class AlignAction final : public Ui::Menu::Action {
public:
	AlignAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		TextAlignment alignment);

	void cycle(TextAlignment alignment);

private:
	void paintEvent(QPaintEvent *e) override;

	const std::unique_ptr<Lottie::Icon> _icon;
	TextAlignment _alignment;

};

AlignAction::AlignAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	TextAlignment alignment)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _icon(Lottie::MakeIcon({
	.path = u":/animations/photo_editor_text_align.tgs"_q,
	.color = &st::mediaviewMenuFg,
	.sizeOverride = QSize(
		st::photoEditorAlignIconSize,
		st::photoEditorAlignIconSize),
	.frame = AlignRestFrame(alignment),
}))
, _alignment(alignment) {
}

void AlignAction::cycle(TextAlignment alignment) {
	if (_alignment == alignment) {
		return;
	}
	const auto frames = AlignTransitionFrames(_alignment, alignment);
	_alignment = alignment;
	_icon->animate([=] { update(); }, frames.from, frames.to);
}

void AlignAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto selected = isSelected();
	paintBackground(p, selected);
	paintRipple(p, 0, 0);
	p.setPen(selected ? st().itemFgOver : st().itemFg);
	paintText(p);
	const auto position = st().itemIconPosition;
	_icon->paint(p, position.x(), position.y());
}

QPainterPath BuildConnectedBackground(
		const QTextLayout &layout,
		int contentWidth,
		int padding,
		float64 fontSize,
		TextAlignment alignment) {
	auto lines = std::vector<TextBackgroundLine>();
	lines.reserve(layout.lineCount());
	for (auto i = 0; i < layout.lineCount(); ++i) {
		const auto line = layout.lineAt(i);
		const auto width = float64(line.naturalTextWidth());
		const auto left = padding
			+ AlignOffset(contentWidth, width, alignment);
		lines.push_back({
			.left = left,
			.top = padding + float64(line.y()),
			.right = left + width,
			.bottom = padding + float64(line.y() + line.height()),
		});
	}
	return BuildTextBackgroundPath(std::move(lines), fontSize);
}

[[nodiscard]] QPainterPath BuildGroupBackgroundPath(
		std::vector<TextBackgroundLine> lines,
		float64 fontSize) {
	const auto linePadH = fontSize * kLinePadHFactor;
	const auto cornerRadius = fontSize * kCornerRadiusFactor;
	const auto mergeRadius = cornerRadius * kMergeRadiusFactor;

	auto rects = std::vector<LineRect>();
	rects.reserve(lines.size());
	for (const auto &line : lines) {
		rects.push_back({
			.left = line.left - linePadH,
			.top = line.top,
			.right = line.right + linePadH,
			.bottom = line.bottom,
		});
	}

	if (rects.empty()) {
		return {};
	}
	if (rects.size() == 1) {
		auto path = QPainterPath();
		const auto &r = rects[0];
		path.addRoundedRect(
			QRectF(r.left, r.top, r.width(), r.bottom - r.top),
			cornerRadius,
			cornerRadius);
		return path;
	}

	for (auto i = 1; i < int(rects.size()); ++i) {
		rects[i - 1].bottom = rects[i].top;
	}

	for (auto i = 1; i < int(rects.size()); ++i) {
		auto traceback = false;
		if (std::abs(rects[i - 1].left - rects[i].left) < mergeRadius) {
			const auto v = std::min(rects[i - 1].left, rects[i].left);
			rects[i - 1].left = rects[i].left = v;
			traceback = true;
		}
		if (std::abs(rects[i - 1].right - rects[i].right) < mergeRadius) {
			const auto v = std::max(rects[i - 1].right, rects[i].right);
			rects[i - 1].right = rects[i].right = v;
			traceback = true;
		}
		if (traceback) {
			for (auto j = i; j >= 1; --j) {
				if (std::abs(rects[j - 1].left - rects[j].left)
					< mergeRadius) {
					const auto v = std::min(
						rects[j - 1].left,
						rects[j].left);
					rects[j - 1].left = rects[j].left = v;
				}
				if (std::abs(rects[j - 1].right - rects[j].right)
					< mergeRadius) {
					const auto v = std::max(
						rects[j - 1].right,
						rects[j].right);
					rects[j - 1].right = rects[j].right = v;
				}
			}
		}
	}

	struct V { float64 x, y; };
	auto verts = std::vector<V>();

	verts.push_back({ rects[0].left, rects[0].top });
	verts.push_back({ rects[0].right, rects[0].top });

	for (auto i = 1; i < int(rects.size()); ++i) {
		if (std::abs(rects[i].right - rects[i - 1].right) > 0.5) {
			verts.push_back({ rects[i - 1].right, rects[i].top });
			verts.push_back({ rects[i].right, rects[i].top });
		}
	}

	const auto last = int(rects.size()) - 1;
	verts.push_back({ rects[last].right, rects[last].bottom });
	verts.push_back({ rects[last].left, rects[last].bottom });

	for (auto i = last - 1; i >= 0; --i) {
		if (std::abs(rects[i].left - rects[i + 1].left) > 0.5) {
			verts.push_back({ rects[i + 1].left, rects[i + 1].top });
			verts.push_back({ rects[i].left, rects[i + 1].top });
		}
	}

	auto path = QPainterPath();
	const auto n = int(verts.size());
	for (auto i = 0; i < n; ++i) {
		const auto &prev = verts[(i + n - 1) % n];
		const auto &curr = verts[i];
		const auto &next = verts[(i + 1) % n];

		const auto dx1 = curr.x - prev.x;
		const auto dy1 = curr.y - prev.y;
		const auto len1 = std::sqrt(dx1 * dx1 + dy1 * dy1);

		const auto dx2 = next.x - curr.x;
		const auto dy2 = next.y - curr.y;
		const auto len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);

		if (len1 < 0.1 || len2 < 0.1) {
			if (i == 0) {
				path.moveTo(curr.x, curr.y);
			} else {
				path.lineTo(curr.x, curr.y);
			}
			continue;
		}

		const auto r = std::min({
			cornerRadius,
			len1 / 2.,
			len2 / 2.,
		});
		const auto bx = curr.x - dx1 / len1 * r;
		const auto by = curr.y - dy1 / len1 * r;
		const auto ax = curr.x + dx2 / len2 * r;
		const auto ay = curr.y + dy2 / len2 * r;

		if (i == 0) {
			path.moveTo(bx, by);
		} else {
			path.lineTo(bx, by);
		}
		path.quadTo(curr.x, curr.y, ax, ay);
	}
	path.closeSubpath();
	return path;
}

} // namespace

QPainterPath BuildTextBackgroundPath(
		std::vector<TextBackgroundLine> lines,
		float64 fontSize) {
	auto result = QPainterPath();
	auto group = std::vector<TextBackgroundLine>();
	const auto flush = [&] {
		if (!group.empty()) {
			result.addPath(BuildGroupBackgroundPath(
				base::take(group),
				fontSize));
		}
	};
	for (const auto &line : lines) {
		if ((line.right - line.left) < 1.) {
			flush();
		} else {
			group.push_back(line);
		}
	}
	flush();
	return result;
}

QColor TextBackgroundColor(const QColor &color, TextStyle style) {
	switch (style) {
	case TextStyle::Framed:
		return color;
	case TextStyle::SemiTransparent:
		return (ComputeBrightness(color)
				>= kBrightnessSemiTransparentThreshold)
			? QColor(0, 0, 0, kSemiTransparentAlpha)
			: QColor(255, 255, 255, kSemiTransparentAlpha);
	case TextStyle::Plain:
		return QColor(Qt::transparent);
	case TextStyle::Opaque:
		return (ComputeBrightness(color)
				>= kBrightnessSemiTransparentThreshold)
			? QColor(0, 0, 0)
			: QColor(255, 255, 255);
	}
	Unexpected("Text style in TextBackgroundColor.");
}

int TextBackgroundPadding(float64 fontSize, TextStyle style) {
	const auto hasBackground = (style == TextStyle::Framed)
		|| (style == TextStyle::SemiTransparent)
		|| (style == TextStyle::Opaque);
	return hasBackground ? int(fontSize * kPaddingFactor) : 0;
}

TextLayoutSpec ComputeTextLayoutSpec(
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style,
		TextTypeface typeface) {
	const auto padding = TextBackgroundPadding(fontSize, style);
	const auto shortSide = std::min(imageSize.width(), imageSize.height());
	return {
		.font = TextFont(fontSize, typeface),
		.padding = padding,
		.maxTextWidth = std::max(
			int(shortSide * kMaxWidthFactor) - 2 * padding,
			kMinContentWidth),
	};
}

QColor EffectiveTextColor(const QColor &color, TextStyle style) {
	if (style != TextStyle::Framed) {
		return color;
	}
	return (ComputeBrightness(color) >= kBrightnessFramedThreshold)
		? QColor(0, 0, 0)
		: QColor(255, 255, 255);
}

ItemText::ItemText(
	const QString &text,
	const QColor &color,
	float64 fontSize,
	TextStyle style,
	TextTypeface typeface,
	TextAlignment alignment,
	const QSize &imageSize,
	ItemBase::Data data)
: ItemBase(std::move(data))
, _text(text)
, _color(color)
, _fontSize(fontSize)
, _textStyle(style)
, _typeface(typeface)
, _alignment(alignment)
, _imageSize(imageSize) {
	renderContent();
}

void ItemText::renderContent() {
	if (_text.isEmpty()) {
		_pixmap = QPixmap();
		setAspectRatio(1.);
		return;
	}

	const auto font = TextFont(_fontSize, _typeface);

	auto processedText = _text;
	processedText.replace('\n', QChar::LineSeparator);

	struct EmojiPos {
		int start = 0;
		int length = 0;
		EmojiPtr emoji = nullptr;
	};
	auto emojiFormats = QVector<QTextLayout::FormatRange>();
	auto emojiPositions = std::vector<EmojiPos>();
	{
		auto pos = 0;
		const auto begin = processedText.constData();
		const auto end = begin + processedText.size();
		while (pos < processedText.size()) {
			auto emojiLen = 0;
			const auto emoji = Ui::Emoji::Find(
				begin + pos,
				end,
				&emojiLen);
			if (emoji && emojiLen > 0) {
				auto fmt = QTextCharFormat();
				fmt.setForeground(QColor(0, 0, 0, 0));
				emojiFormats.append({ pos, emojiLen, fmt });
				emojiPositions.push_back({ pos, emojiLen, emoji });
				pos += emojiLen;
			} else {
				++pos;
			}
		}
	}

	const auto prepared = PrepareLayout(
		processedText,
		_fontSize,
		_imageSize,
		_textStyle,
		_typeface,
		emojiFormats);
	const auto &m = prepared.metrics;
	const auto &layout = *prepared.layout;
	const auto pixWidth = m.contentWidth + 2 * m.padding;
	const auto pixHeight = m.contentHeight + 2 * m.padding;

	const auto textColor = EffectiveTextColor(_color, _textStyle);
	const auto bgColor = TextBackgroundColor(_color, _textStyle);
	const auto hasBackground = (bgColor.alpha() > 0);

	const auto dpr = style::DevicePixelRatio();
	auto pixmap = QPixmap(QSize(pixWidth, pixHeight) * dpr);
	pixmap.setDevicePixelRatio(dpr);
	pixmap.fill(Qt::transparent);

	{
		auto p = QPainter(&pixmap);
		auto hq = PainterHighQualityEnabler(p);

		if (hasBackground) {
			const auto bgPath = BuildConnectedBackground(
				layout,
				m.contentWidth,
				m.padding,
				_fontSize,
				_alignment);
			p.setPen(Qt::NoPen);
			p.setBrush(bgColor);
			p.drawPath(bgPath);
		}

		const auto lineCount = layout.lineCount();
		p.setPen(textColor);
		for (auto i = 0; i < lineCount; ++i) {
			const auto line = layout.lineAt(i);
			const auto xOffset = AlignOffset(
				m.contentWidth,
				line.naturalTextWidth(),
				_alignment);
			line.draw(&p, QPointF(m.padding + xOffset, m.padding));
		}

		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		const auto factor = style::DevicePixelRatio();
		const auto source = Ui::Emoji::GetSizeLarge();
		const auto sourceLogical = source / float64(factor);
		const auto emojiSize = float64(QFontMetrics(font).height());
		const auto emojiScale = emojiSize / sourceLogical;
		for (const auto &ep : emojiPositions) {
			auto lineIndex = -1;
			for (auto i = 0; i < lineCount; ++i) {
				const auto line = layout.lineAt(i);
				const auto lineStart = line.textStart();
				const auto lineEnd = lineStart + line.textLength();
				if (ep.start >= lineStart && ep.start < lineEnd) {
					lineIndex = i;
					break;
				}
			}
			if (lineIndex < 0) {
				continue;
			}
			const auto line = layout.lineAt(lineIndex);
			const auto lineStart = line.textStart();
			const auto lineEnd = lineStart + line.textLength();
			const auto drawEnd = std::min(ep.start + ep.length, lineEnd);
			const auto xOffset = AlignOffset(
				m.contentWidth,
				line.naturalTextWidth(),
				_alignment);
			const auto x = line.cursorToX(ep.start);
			const auto nextX = line.cursorToX(drawEnd);
			const auto glyphWidth = float64(nextX - x);
			const auto drawX = m.padding
				+ xOffset
				+ x
				+ (glyphWidth - emojiSize) / 2.;
			const auto drawY = m.padding
				+ line.y()
				+ (line.height() - emojiSize) / 2.;
			p.save();
			p.translate(drawX, drawY);
			p.scale(emojiScale, emojiScale);
			Ui::Emoji::Draw(p, ep.emoji, source, 0, 0);
			p.restore();
		}
	}

	_pixmap = std::move(pixmap);
	const auto handleMargin = std::max(
		innerRect().width() - contentRect().width(),
		0.);
	applyStretch(
		pixWidth + handleMargin,
		pixHeight + handleMargin);
}

QSize ItemText::computeContentSize(
		const QString &text,
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style,
		TextTypeface typeface) {
	if (text.isEmpty()) {
		return {};
	}
	auto processedText = text;
	processedText.replace('\n', QChar::LineSeparator);
	const auto m = PrepareLayout(
		processedText,
		fontSize,
		imageSize,
		style,
		typeface).metrics;
	return QSize(
		m.contentWidth + 2 * m.padding,
		m.contentHeight + 2 * m.padding);
}

void ItemText::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	if (!_pixmap.isNull()) {
		const auto rect = contentRect();
		const auto pixmapSize = QSizeF(
			_pixmap.size() / style::DevicePixelRatio()
		).scaled(rect.size(), Qt::KeepAspectRatio);
		const auto resultRect = QRectF(
			rect.topLeft(),
			pixmapSize
		).translated(
			(rect.width() - pixmapSize.width()) / 2.,
			(rect.height() - pixmapSize.height()) / 2.);
		p->save();
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		if (flipped()) {
			const auto center = resultRect.center();
			p->translate(center);
			p->scale(-1, 1);
			p->translate(-center);
		}
		p->drawPixmap(resultRect, _pixmap, QRectF(_pixmap.rect()));
		p->restore();
	}
	ItemBase::paint(p, option, w);
}

int ItemText::type() const {
	return Type;
}

const QString &ItemText::text() const {
	return _text;
}

void ItemText::setText(const QString &text) {
	if (_text == text) {
		return;
	}
	_text = text;
	renderContent();
	update();
}

const QColor &ItemText::color() const {
	return _color;
}

void ItemText::setColor(const QColor &color) {
	if (_color == color) {
		return;
	}
	_color = color;
	renderContent();
	update();
}

float64 ItemText::fontSize() const {
	return _fontSize;
}

void ItemText::setFontSize(float64 fontSize) {
	fontSize = std::max(fontSize, 1.);
	if (_fontSize == fontSize) {
		return;
	}
	_fontSize = fontSize;
	renderContent();
	update();
}

float64 ItemText::editScale() const {
	const auto natural = computeContentSize(
		_text,
		_fontSize,
		_imageSize,
		_textStyle,
		_typeface);
	if (natural.width() <= 0) {
		return 1.;
	}
	const auto handleMargin = std::max(
		innerRect().width() - contentRect().width(),
		0.);
	return std::max(size() - handleMargin, 1.) / natural.width();
}

void ItemText::bakeScale() {
	const auto factor = editScale() * scale();
	if (_text.isEmpty() || (std::abs(factor - 1.) < 0.001)) {
		return;
	}
	_fontSize = std::max(_fontSize * factor, 1.);
	setScale(1.);
	renderContent();
	update();
	notifyPrefsUsed();
}

void ItemText::notifyPrefsUsed() {
	if (const auto s = static_cast<Scene*>(scene())) {
		s->noteTextItemPrefs(this);
	}
}

ItemBase::Placement ItemText::placement() const {
	auto result = ItemBase::placement();
	result.fontSize = _fontSize;
	return result;
}

void ItemText::applyPlacement(const Placement &placement) {
	if ((placement.fontSize > 0.) && (placement.fontSize != _fontSize)) {
		_fontSize = placement.fontSize;
		renderContent();
	}
	ItemBase::applyPlacement(placement);
}

TextStyle ItemText::textStyle() const {
	return _textStyle;
}

void ItemText::setTextStyle(TextStyle style) {
	if (_textStyle == style) {
		return;
	}
	_textStyle = style;
	renderContent();
	update();
}

TextTypeface ItemText::typeface() const {
	return _typeface;
}

void ItemText::setTypeface(TextTypeface typeface) {
	if (_typeface == typeface) {
		return;
	}
	_typeface = typeface;
	renderContent();
	update();
}

TextAlignment ItemText::alignment() const {
	return _alignment;
}

void ItemText::setAlignment(TextAlignment alignment) {
	if (_alignment == alignment) {
		return;
	}
	_alignment = alignment;
	renderContent();
	update();
}

void ItemText::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	const auto resized = isHandling()
		&& (event->button() == Qt::LeftButton);
	ItemBase::mouseReleaseEvent(event);
	if (resized) {
		bakeScale();
	}
}

void ItemText::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	if ((event->button() != Qt::LeftButton)
		|| !contentRect().contains(event->pos())) {
		ItemBase::mouseDoubleClickEvent(event);
		return;
	}
	if (const auto s = static_cast<Scene*>(scene())) {
		s->startTextEditing(this);
	}
}

void ItemText::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
	if (scene()) {
		scene()->clearSelection();
		setSelected(true);
	}

	_contextMenu = base::make_unique_q<Ui::PopupMenu>(
		nullptr,
		st::mediaviewPopupMenu);
	const auto add = [&](
			const QString &text,
			TextStyle style,
			const style::icon *icon) {
		const auto checked = (_textStyle == style);
		_contextMenu->addAction(
			text,
			[=] {
				setTextStyle(style);
				notifyPrefsUsed();
			},
			checked ? &st::mediaPlayerMenuCheck : icon);
	};
	add(
		tr::lng_photo_editor_text_style_plain(tr::now),
		TextStyle::Plain,
		&st::mediaMenuIconTextStylePlain);
	add(
		tr::lng_photo_editor_text_style_framed(tr::now),
		TextStyle::Framed,
		&st::mediaMenuIconTextStyleFramed);
	add(
		tr::lng_photo_editor_text_style_semi_transparent(tr::now),
		TextStyle::SemiTransparent,
		&st::mediaMenuIconTextStyleSemiTransparent);
	add(
		tr::lng_photo_editor_text_style_opaque(tr::now),
		TextStyle::Opaque,
		&st::mediaMenuIconTextStyleOpaque);

	auto fonts = std::make_unique<Ui::PopupMenu>(
		_contextMenu.get(),
		st::mediaviewPopupMenu);
	const auto addFont = [&](
			const QString &text,
			TextTypeface typeface) {
		const auto checked = (_typeface == typeface);
		fonts->addAction(
			text,
			[=] {
				setTypeface(typeface);
				notifyPrefsUsed();
			},
			checked ? &st::mediaPlayerMenuCheck : nullptr);
	};
	addFont(
		tr::lng_photo_editor_font_default(tr::now),
		TextTypeface::Default);
	addFont(
		tr::lng_photo_editor_font_italic(tr::now),
		TextTypeface::Italic);
	addFont(
		tr::lng_photo_editor_font_serif(tr::now),
		TextTypeface::Serif);
	addFont(
		tr::lng_photo_editor_font_condensed(tr::now),
		TextTypeface::Condensed);
	addFont(
		tr::lng_photo_editor_font_monospace(tr::now),
		TextTypeface::Monospace);
	_contextMenu->addAction(
		tr::lng_photo_editor_font(tr::now),
		std::move(fonts),
		&st::mediaMenuIconFont);

	auto align = base::make_unique_q<AlignAction>(
		_contextMenu->menu(),
		_contextMenu->st().menu,
		new QAction(tr::lng_photo_editor_align(tr::now), _contextMenu.get()),
		_alignment);
	const auto alignRaw = align.get();
	align->setActionTriggered([=] {
		const auto next = NextAlignment(_alignment);
		setAlignment(next);
		notifyPrefsUsed();
		alignRaw->cycle(next);
	});
	align->setPreventClose(true);
	_contextMenu->addAction(std::move(align));

	_contextMenu->addSeparator();

	_contextMenu->addAction(
		tr::lng_photo_editor_menu_duplicate(tr::now),
		[=] { actionDuplicate(); },
		&st::mediaMenuIconCopy);
	_contextMenu->addAction(
		tr::lng_photo_editor_menu_delete(tr::now),
		[=] { actionDelete(); },
		&st::mediaMenuIconDelete);

	_contextMenu->popup(event->screenPos());
}

void ItemText::actionFlip() {
}

void ItemText::performFlip() {
	update();
}

std::shared_ptr<ItemBase> ItemText::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemText>(
		_text,
		_color,
		_fontSize,
		_textStyle,
		_typeface,
		_alignment,
		_imageSize,
		std::move(data));
}

void ItemText::save(SaveState state) {
	ItemBase::save(state);
	auto &saved = (state == SaveState::Keep) ? _keepedState : _savedState;
	saved = {
		.text = _text,
		.color = _color,
		.fontSize = _fontSize,
		.textStyle = _textStyle,
		.typeface = _typeface,
		.alignment = _alignment,
	};
}

void ItemText::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	const auto &saved = (state == SaveState::Keep) ? _keepedState : _savedState;
	const auto changed = (_text != saved.text)
		|| (_color != saved.color)
		|| (_fontSize != saved.fontSize)
		|| (_textStyle != saved.textStyle)
		|| (_typeface != saved.typeface)
		|| (_alignment != saved.alignment);
	_text = saved.text;
	_color = saved.color;
	_fontSize = saved.fontSize;
	_textStyle = saved.textStyle;
	_typeface = saved.typeface;
	_alignment = saved.alignment;
	if (changed) {
		renderContent();
	}
	ItemBase::restore(state);
}

} // namespace Editor
