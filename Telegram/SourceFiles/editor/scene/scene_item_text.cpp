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
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QtWidgets/QApplication>
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
constexpr auto kLinePadVFactor = 1. / 8.;
constexpr auto kMergeRadiusFactor = 1.5;
constexpr auto kLineShiftFactor = 1. / 7.;
constexpr auto kTextStyleClickDelay = crl::time(120);

struct LayoutMetrics {
	int contentWidth = 0;
	int contentHeight = 0;
	int padding = 0;
	int textMaxWidth = 0;
};

QFont TextFont(float64 fontSize) {
	auto font = QFont();
	font.setPixelSize(std::max(int(fontSize), 1));
	font.setWeight(QFont::DemiBold);
	return font;
}

float64 ComputeBrightness(const QColor &color) {
	return (color.red() * 0.2126
		+ color.green() * 0.7152
		+ color.blue() * 0.0722) / 255.;
}

LayoutMetrics ComputeMetrics(
		const QString &text,
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style) {
	const auto hasBackground = (style == TextStyle::Framed)
		|| (style == TextStyle::SemiTransparent);
	const auto padding = hasBackground ? int(fontSize * kPaddingFactor) : 0;
	const auto shortSide = std::min(imageSize.width(), imageSize.height());
	const auto textMaxWidth = std::max(
		int(shortSide * kMaxWidthFactor) - 2 * padding,
		kMinContentWidth);

	const auto font = TextFont(fontSize);

	auto processedText = text;
	processedText.replace('\n', QChar::LineSeparator);

	auto option = QTextOption();
	option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

	auto layout = QTextLayout(processedText, font);
	layout.setTextOption(option);
	layout.beginLayout();

	auto totalHeight = 0.;
	auto maxWidth = 0.;
	while (true) {
		auto line = layout.createLine();
		if (!line.isValid()) {
			break;
		}
		line.setLineWidth(textMaxWidth);
		line.setPosition(QPointF(0, totalHeight));
		totalHeight += line.height();
		maxWidth = std::max(maxWidth, float64(line.naturalTextWidth()));
	}
	layout.endLayout();

	return {
		.contentWidth = std::max(int(std::ceil(maxWidth)), kMinContentWidth),
		.contentHeight = int(std::ceil(totalHeight)),
		.padding = padding,
		.textMaxWidth = textMaxWidth,
	};
}

struct LineRect {
	float64 left = 0;
	float64 top = 0;
	float64 right = 0;
	float64 bottom = 0;
	[[nodiscard]] float64 width() const { return right - left; }
};

QPainterPath BuildConnectedBackground(
		const QTextLayout &layout,
		int contentWidth,
		int padding,
		float64 fontSize) {
	const auto linePadH = fontSize * kLinePadHFactor;
	const auto linePadV = fontSize * kLinePadVFactor;
	const auto cornerRadius = fontSize * kCornerRadiusFactor;
	const auto mergeRadius = cornerRadius * kMergeRadiusFactor;
	const auto centerX = padding + contentWidth / 2.;

	auto rects = std::vector<LineRect>();
	for (auto i = 0; i < layout.lineCount(); ++i) {
		const auto line = layout.lineAt(i);
		const auto hw = float64(line.naturalTextWidth()) / 2. + linePadH;
		rects.push_back({
			.left = centerX - hw,
			.top = padding + float64(line.y()) - linePadV,
			.right = centerX + hw,
			.bottom = padding + float64(line.y() + line.height()) + linePadV,
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

TextStyle NextTextStyle(TextStyle style) {
	switch (style) {
	case TextStyle::Plain:
		return TextStyle::Framed;
	case TextStyle::Framed:
		return TextStyle::SemiTransparent;
	case TextStyle::SemiTransparent:
		return TextStyle::Plain;
	}
	Unexpected("Text style in NextTextStyle.");
}

} // namespace

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
	const QSize &imageSize,
	ItemBase::Data data)
: ItemBase(std::move(data))
, _text(text)
, _color(color)
, _fontSize(fontSize)
, _textStyle(style)
, _imageSize(imageSize)
, _textStyleClickTimer([=] {
	setTextStyle(NextTextStyle(_textStyle));
	_textStyleClickChanged = true;
}) {
	renderContent();
}

void ItemText::renderContent() {
	if (_text.isEmpty()) {
		_pixmap = QPixmap();
		setAspectRatio(1.);
		return;
	}

	const auto m = ComputeMetrics(_text, _fontSize, _imageSize, _textStyle);
	const auto pixWidth = m.contentWidth + 2 * m.padding;
	const auto pixHeight = m.contentHeight + 2 * m.padding;

	const auto font = TextFont(_fontSize);

	auto processedText = _text;
	processedText.replace('\n', QChar::LineSeparator);

	auto option = QTextOption();
	option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

	auto layout = QTextLayout(processedText, font);
	layout.setTextOption(option);

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
	layout.setFormats(emojiFormats);

	layout.beginLayout();
	auto y = 0.;
	while (true) {
		auto line = layout.createLine();
		if (!line.isValid()) {
			break;
		}
		line.setLineWidth(m.textMaxWidth);
		line.setPosition(QPointF(0, y));
		y += line.height();
	}
	layout.endLayout();

	auto textColor = _color;
	auto bgColor = QColor(Qt::transparent);
	const auto brightness = ComputeBrightness(_color);
	const auto hasBackground =
		(_textStyle == TextStyle::Framed)
		|| (_textStyle == TextStyle::SemiTransparent);

	switch (_textStyle) {
	case TextStyle::Framed:
		bgColor = _color;
		textColor = (brightness >= kBrightnessFramedThreshold)
			? QColor(0, 0, 0)
			: QColor(255, 255, 255);
		break;
	case TextStyle::SemiTransparent:
		bgColor = (brightness >= kBrightnessSemiTransparentThreshold)
			? QColor(0, 0, 0, kSemiTransparentAlpha)
			: QColor(255, 255, 255, kSemiTransparentAlpha);
		break;
	case TextStyle::Plain:
		break;
	}

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
				_fontSize);
			if (_textStyle == TextStyle::SemiTransparent) {
				auto opaque = bgColor;
				opaque.setAlpha(255);
				auto mask = QPixmap(pixmap.size());
				mask.setDevicePixelRatio(dpr);
				mask.fill(Qt::transparent);
				{
					auto mp = QPainter(&mask);
					auto mhq = PainterHighQualityEnabler(mp);
					mp.setPen(Qt::NoPen);
					mp.setBrush(opaque);
					mp.drawPath(bgPath);
				}
				p.setOpacity(bgColor.alphaF());
				p.drawPixmap(0, 0, mask);
				p.setOpacity(1.0);
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(bgColor);
				p.drawPath(bgPath);
			}
		}

		const auto lineShift = _fontSize * kLineShiftFactor;
		const auto lineCount = layout.lineCount();
		p.setPen(textColor);
		for (auto i = 0; i < lineCount; ++i) {
			const auto line = layout.lineAt(i);
			const auto xOffset =
				(m.contentWidth - line.naturalTextWidth()) / 2.;
			const auto yShift = (i < lineCount - 1) ? -lineShift : 0.;
			line.draw(
				&p,
				QPointF(m.padding + xOffset, m.padding + yShift));
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
			const auto xOffset =
				(m.contentWidth - line.naturalTextWidth()) / 2.;
			const auto yShift = (lineIndex < lineCount - 1)
				? -lineShift
				: 0.;
			const auto x = line.cursorToX(ep.start);
			const auto nextX = line.cursorToX(drawEnd);
			const auto glyphWidth = float64(nextX - x);
			const auto drawX = m.padding
				+ xOffset
				+ x
				+ (glyphWidth - emojiSize) / 2.;
			const auto drawY = m.padding
				+ yShift
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
	setAspectRatio(
		(pixHeight + handleMargin) / float64(pixWidth + handleMargin));
}

QSize ItemText::computeContentSize(
		const QString &text,
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style) {
	if (text.isEmpty()) {
		return {};
	}
	auto processedText = text;
	processedText.replace('\n', QChar::LineSeparator);
	const auto m = ComputeMetrics(processedText, fontSize, imageSize, style);
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
		if (flipped()) {
			p->save();
			const auto center = resultRect.center();
			p->translate(center);
			p->scale(-1, 1);
			p->translate(-center);
			p->drawPixmap(resultRect.toRect(), _pixmap);
			p->restore();
		} else {
			p->drawPixmap(resultRect.toRect(), _pixmap);
		}
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
	_text = text;
	renderContent();
	update();
}

const QColor &ItemText::color() const {
	return _color;
}

void ItemText::setColor(const QColor &color) {
	_color = color;
	renderContent();
	update();
}

float64 ItemText::fontSize() const {
	return _fontSize;
}

float64 ItemText::editScale() const {
	const auto natural = computeContentSize(
		_text,
		_fontSize,
		_imageSize,
		_textStyle);
	if (natural.width() <= 0) {
		return 1.;
	}
	return size() / natural.width();
}

TextStyle ItemText::textStyle() const {
	return _textStyle;
}

void ItemText::setTextStyle(TextStyle style) {
	_textStyle = style;
	renderContent();
	update();
}

void ItemText::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	_textStyleClickTimer.cancel();
	_textStyleClickChanged = false;
	_textStyleClickCandidate = (event->button() == Qt::LeftButton)
		&& contentRect().contains(event->pos());
	_textStyleClickDragging = false;
	if (_textStyleClickCandidate) {
		_textStyleClickItemPosition = pos();
		_textStyleClickInitialStyle = _textStyle;
		event->accept();
		return;
	}
	ItemBase::mousePressEvent(event);
}

void ItemText::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	if (!_textStyleClickCandidate) {
		ItemBase::mouseMoveEvent(event);
		return;
	}
	const auto delta = event->screenPos()
		- event->buttonDownScreenPos(Qt::LeftButton);
	if (!_textStyleClickDragging
		&& delta.manhattanLength() >= QApplication::startDragDistance()) {
		_textStyleClickDragging = true;
		if (scene()) {
			scene()->clearSelection();
			setSelected(true);
		}
		raiseToTop();
		setCursor(Qt::ClosedHandCursor);
	}
	if (_textStyleClickDragging) {
		const auto sceneDelta = event->scenePos()
			- event->buttonDownScenePos(Qt::LeftButton);
		setPos(_textStyleClickItemPosition + sceneDelta);
	}
	event->accept();
}

void ItemText::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	const auto textStyleClickCandidate = _textStyleClickCandidate;
	const auto textStyleClickDragging = _textStyleClickDragging;
	_textStyleClickCandidate = false;
	_textStyleClickDragging = false;
	if (!textStyleClickCandidate) {
		ItemBase::mouseReleaseEvent(event);
		return;
	}
	if (textStyleClickDragging) {
		unsetCursor();
		event->accept();
		return;
	}
	if (event->button() == Qt::LeftButton) {
		const auto delta = event->screenPos()
			- event->buttonDownScreenPos(Qt::LeftButton);
		if (delta.manhattanLength() >= QApplication::startDragDistance()) {
			event->accept();
			return;
		}
		_textStyleClickTimer.callOnce(kTextStyleClickDelay);
		event->accept();
	}
}

void ItemText::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	_textStyleClickCandidate = false;
	_textStyleClickDragging = false;
	_textStyleClickTimer.cancel();
	if (_textStyleClickChanged) {
		setTextStyle(_textStyleClickInitialStyle);
		_textStyleClickChanged = false;
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
		auto action = _contextMenu->addAction(
			text,
			[=] { setTextStyle(style); },
			icon);
		if (checked) {
			action->setChecked(true);
		}
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

void ItemText::performFlip() {
	update();
}

std::shared_ptr<ItemBase> ItemText::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemText>(
		_text,
		_color,
		_fontSize,
		_textStyle,
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
	};
}

void ItemText::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	const auto &saved = (state == SaveState::Keep) ? _keepedState : _savedState;
	_text = saved.text;
	_color = saved.color;
	_fontSize = saved.fontSize;
	_textStyle = saved.textStyle;
	renderContent();
	ItemBase::restore(state);
}

} // namespace Editor
