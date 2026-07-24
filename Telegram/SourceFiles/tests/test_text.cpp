/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_main.h"

#include "base/invoke_queued.h"
#include "base/integration.h"
#include "ui/effects/animations.h"
#include "ui/ui_utility.h"
#include "ui/style/style_core.h"
#include "ui/text/text.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/rp_window.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QThread>
#include <QDir>
#include <QImage>

#include <algorithm>
#include <memory>
#include <span>
#include <utility>

namespace Test {

namespace {

[[nodiscard]] bool HasEntityType(
		const EntitiesInText &entities,
		EntityType type) {
	for (const auto &entity : entities) {
		if (entity.type() == type) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QImage MakeObjectImage(
		QSize size,
		QColor background,
		const QString &label) {
	auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	auto painter = QPainter(&image);
	auto hq = PainterHighQualityEnabler(painter);
	painter.setPen(Qt::NoPen);
	painter.setBrush(background);
	painter.drawRoundedRect(
		QRect(QPoint(), size),
		scale(6),
		scale(6));
	painter.setPen(Qt::black);
	painter.drawText(QRect(QPoint(), size), Qt::AlignCenter, label);
	return image;
}

[[nodiscard]] int RenderTextPadding() {
	return scale(6);
}

[[nodiscard]] QImage RenderTextOffscreen(
		const Ui::Text::String &text,
		int availableWidth,
		std::optional<TextSelection> selection = std::nullopt,
		std::span<Ui::Text::SpecialColor> colors = {},
		std::optional<QPen> pen = std::nullopt) {
	const auto padding = RenderTextPadding();
	const auto image = QImage(
		QSize(
			std::max(text.maxWidth(), availableWidth) + (2 * padding),
			std::max(text.countHeight(availableWidth), text.minHeight())
				+ (2 * padding)),
		QImage::Format_ARGB32_Premultiplied);
	auto result = image;
	result.fill(Qt::transparent);
	auto painter = QPainter(&result);
	if (pen) {
		painter.setPen(*pen);
	}
	text.draw(painter, {
		.position = QPoint(padding, padding),
		.availableWidth = availableWidth,
		.colors = colors,
		.selection = selection.value_or(TextSelection()),
	});
	return result;
}

[[nodiscard]] std::optional<QRect> ChangedBoundsInRect(
		const QImage &first,
		const QImage &second,
		QRect rect) {
	if (first.size() != second.size()) {
		return std::nullopt;
	}
	rect = rect.intersected(QRect(QPoint(), first.size()));
	if (rect.isEmpty()) {
		return std::nullopt;
	}
	auto left = rect.right();
	auto top = rect.bottom();
	auto right = rect.left() - 1;
	auto bottom = rect.top() - 1;
	for (auto y = rect.top(); y <= rect.bottom(); ++y) {
		for (auto x = rect.left(); x <= rect.right(); ++x) {
			if (first.pixel(x, y) == second.pixel(x, y)) {
				continue;
			}
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}
	return (right >= left) && (bottom >= top)
		? std::make_optional(QRect(QPoint(left, top), QPoint(right, bottom)))
		: std::nullopt;
}

[[nodiscard]] std::optional<QRect> SymbolHitBounds(
		const Ui::Text::String &text,
		int availableWidth,
		int offset) {
	if ((availableWidth <= 0) || (offset < 0)) {
		return std::nullopt;
	}
	const auto padding = RenderTextPadding();
	const auto height = std::max(
		text.countHeight(availableWidth),
		text.minHeight());
	auto flags = Ui::Text::StateRequest::Flags();
	flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
	auto request = Ui::Text::StateRequest();
	request.flags = flags;
	auto left = padding + availableWidth;
	auto top = padding + height;
	auto right = -1;
	auto bottom = -1;
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != availableWidth; ++x) {
			const auto hit = text.getState(QPoint(x, y), availableWidth, request);
			if (!hit.uponSymbol || (int(hit.symbol) != offset)) {
				continue;
			}
			left = std::min(left, x + padding);
			top = std::min(top, y + padding);
			right = std::max(right, x + padding);
			bottom = std::max(bottom, y + padding);
		}
	}
	return (right >= left) && (bottom >= top)
		? std::make_optional(QRect(QPoint(left, top), QPoint(right, bottom)))
		: std::nullopt;
}

[[nodiscard]] std::optional<QRect> SymbolRangeHitBounds(
		const Ui::Text::String &text,
		int availableWidth,
		int offset,
		int length) {
	if ((availableWidth <= 0) || (offset < 0) || (length <= 0)) {
		return std::nullopt;
	}
	const auto padding = RenderTextPadding();
	const auto height = std::max(
		text.countHeight(availableWidth),
		text.minHeight());
	auto flags = Ui::Text::StateRequest::Flags();
	flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
	auto request = Ui::Text::StateRequest();
	request.flags = flags;
	auto left = padding + availableWidth;
	auto top = padding + height;
	auto right = -1;
	auto bottom = -1;
	const auto end = offset + length;
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != availableWidth; ++x) {
			const auto hit = text.getState(QPoint(x, y), availableWidth, request);
			if (!hit.uponSymbol) {
				continue;
			}
			const auto symbol = int(hit.symbol);
			if ((symbol < offset) || (symbol >= end)) {
				continue;
			}
			left = std::min(left, x + padding);
			top = std::min(top, y + padding);
			right = std::max(right, x + padding);
			bottom = std::max(bottom, y + padding);
		}
	}
	return (right >= left) && (bottom >= top)
		? std::make_optional(QRect(QPoint(left, top), QPoint(right, bottom)))
		: std::nullopt;
}

[[nodiscard]] bool ImagesEqualInRect(
		const QImage &first,
		const QImage &second,
		QRect rect) {
	return !ChangedBoundsInRect(first, second, rect).has_value();
}

[[nodiscard]] bool HasPixelColorInRect(
		const QImage &image,
		QRect rect,
		QRgb color) {
	rect = rect.intersected(QRect(QPoint(), image.size()));
	if (rect.isEmpty()) {
		return false;
	}
	for (auto y = rect.top(); y <= rect.bottom(); ++y) {
		for (auto x = rect.left(); x <= rect.right(); ++x) {
			if (image.pixel(x, y) == color) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool HasPaintedPixels(const QImage &image) {
	const auto bits = image.constBits();
	if (!bits) {
		return false;
	}
	const auto count = image.sizeInBytes();
	for (auto i = qsizetype(0); i != count; ++i) {
		if (bits[i] != 0) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::optional<QPoint> FirstPaintedPoint(
		const QImage &image,
		QRect rect = QRect()) {
	if (rect.isNull()) {
		rect = QRect(QPoint(), image.size());
	}
	rect = rect.intersected(QRect(QPoint(), image.size()));
	if (rect.isEmpty()) {
		return std::nullopt;
	}
	const auto background = image.pixel(rect.topLeft());
	for (auto y = rect.top(); y <= rect.bottom(); ++y) {
		for (auto x = rect.left(); x <= rect.right(); ++x) {
			if (image.pixel(x, y) != background) {
				return QPoint(x, y);
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] QRect ScaleRect(QRect rect, qreal ratio) {
	return QRect(
		QPoint(
			qRound(rect.x() * ratio),
			qRound(rect.y() * ratio)),
		QSize(
			qRound(rect.width() * ratio),
			qRound(rect.height() * ratio)));
}

class FormulaLikeObject final : public Ui::Text::CustomEmoji {
public:
	FormulaLikeObject(QString entityData, QString replacementText, QImage image);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &) override;
	QString replacementText() override;
	Ui::Text::CustomEmojiSemantics semantics() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	QString _entityData;
	QString _replacementText;
	QImage _image;

};

FormulaLikeObject::FormulaLikeObject(
	QString entityData,
	QString replacementText,
	QImage image)
: _entityData(std::move(entityData))
, _replacementText(std::move(replacementText))
, _image(std::move(image)) {
}

int FormulaLikeObject::width() {
	return _image.width();
}

QString FormulaLikeObject::entityData() {
	return _entityData;
}

std::optional<Ui::Text::CustomEmojiVerticalMetrics>
FormulaLikeObject::vertical(const style::TextStyle &) {
	const auto height = _image.height();
	const auto descent = std::max(height / 5, 1);
	return Ui::Text::CustomEmojiVerticalMetrics{
		.ascent = height - descent,
		.descent = descent,
	};
}

QString FormulaLikeObject::replacementText() {
	return _replacementText;
}

Ui::Text::CustomEmojiSemantics FormulaLikeObject::semantics() {
	return {
		.isEmoji = false,
		.isRealCustomEmoji = false,
		.exportEntity = false,
		.unloadPersistentAnimation = false,
		.allowCustomEmojiClick = false,
	};
}

void FormulaLikeObject::paint(QPainter &p, const Context &context) {
	p.drawImage(context.position, _image);
}

void FormulaLikeObject::unload() {
}

bool FormulaLikeObject::ready() {
	return true;
}

bool FormulaLikeObject::readyInDefaultState() {
	return true;
}

} // namespace

QString name() {
	return u"text"_q;
}

void test(not_null<Ui::RpWindow*> window, not_null<Ui::RpWidget*> body) {
	(void)window;

	const auto formulaEntityData =
		u"iv-markdown:inline-text-object;formula;copy;tex"_q;
	const auto formulaReplacementText = u"$\\frac{a}{b}$"_q;
	const auto controlEntityData = u"test-custom-emoji"_q;
	const auto formulaImage = MakeObjectImage(
		QSize(scale(64), scale(28)),
		QColor(32, 96, 192, 48),
		u"a / b"_q);
	const auto controlImage = MakeObjectImage(
		QSize(scale(24), scale(24)),
		QColor(224, 176, 32, 160),
		u"*"_q);

	auto context = Ui::Text::MarkedContext();
	context.customEmojiFactory = [
		formulaEntityData,
		formulaReplacementText,
		formulaImage,
		controlImage
	](QStringView data, const Ui::Text::MarkedContext &)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		if (data == formulaEntityData) {
			return std::make_unique<FormulaLikeObject>(
				data.toString(),
				formulaReplacementText,
				formulaImage);
		}
		if (data == u"test-custom-emoji"_q) {
			return std::make_unique<Ui::Text::PaletteDependentCustomEmoji>(
				[controlImage] {
					return controlImage;
				},
				data.toString());
		}
		return std::unique_ptr<Ui::Text::CustomEmoji>();
	};

	auto formulaData = TextWithEntities();
	formulaData.append(u"Alpha "_q);
	const auto formulaPosition = formulaData.text.size();
	formulaData.append(QChar::ObjectReplacementCharacter);
	formulaData.entities.push_back(EntityInText(
		EntityType::CustomEmoji,
		formulaPosition,
		1,
		formulaEntityData));
	formulaData.append(u" omega"_q);

	const auto formulaText = new Ui::Text::String(scale(64));
	formulaText->setMarkedText(
		st::defaultTextStyle,
		formulaData,
		kMarkupTextOptions,
		context);

	const auto formulaCollapsedText = Ui::Text::String(
		st::defaultTextStyle,
		u"Alpha   omega"_q,
		kMarkupTextOptions,
		scale(64));
	const auto objectPlaceholderWidth = st::defaultTextStyle.font->width(u" "_q);
	Expects(
		formulaText->maxWidth()
			>= formulaCollapsedText.maxWidth()
				- objectPlaceholderWidth
				+ formulaImage.width());

	const auto expectedFormulaExport = u"Alpha $\\frac{a}{b}$ omega"_q;
	Expects(formulaText->toString() == expectedFormulaExport);
	const auto formulaMime = formulaText->toTextForMimeData();
	Expects(formulaMime.expanded == expectedFormulaExport);
	Expects(formulaMime.rich.text == expectedFormulaExport);
	Expects(!HasEntityType(formulaMime.rich.entities, EntityType::CustomEmoji));
	Expects(formulaMime.tags.size() == 1);
	const auto expectedFormulaTag = TextForMimeDataTag{
		.offset = formulaPosition,
		.length = int(formulaReplacementText.size()),
		.id = Ui::InputField::kTagIvMath,
	};
	Expects(formulaMime.tags.front() == expectedFormulaTag);
	const auto formulaRich = formulaText->toTextWithEntities();
	Expects(formulaRich.text == expectedFormulaExport);
	Expects(!HasEntityType(formulaRich.entities, EntityType::CustomEmoji));
	Expects(
		formulaText->adjustSelection(
			TextSelection(uint16(formulaPosition), uint16(formulaPosition)),
			TextSelectType::Words)
		== TextSelection(
			uint16(formulaPosition),
			uint16(formulaPosition + 1)));
	Expects(
		formulaText->adjustSelection(
			TextSelection(uint16(formulaPosition), uint16(formulaPosition + 1)),
			TextSelectType::Paragraphs)
		== TextSelection(
			uint16(formulaPosition),
			uint16(formulaPosition + 1)));
	Expects(!formulaText->hasCustomEmoji());
	Expects(!formulaText->isOnlyCustomEmoji());
	Expects(!formulaText->isIsolatedEmoji());

	auto controlData = TextWithEntities();
	controlData.append(QChar::ObjectReplacementCharacter);
	controlData.entities.push_back(EntityInText(
		EntityType::CustomEmoji,
		0,
		1,
		controlEntityData));

	const auto controlText = new Ui::Text::String(scale(64));
	controlText->setMarkedText(
		st::defaultTextStyle,
		controlData,
		kMarkupTextOptions,
		context);
	Expects(controlText->hasCustomEmoji());
	Expects(controlText->isOnlyCustomEmoji());
	Expects(controlText->isIsolatedEmoji());
	Expects(
		HasEntityType(
			controlText->toTextWithEntities().entities,
			EntityType::CustomEmoji));
	const auto controlProbeText = u"a / b"_q;
	const auto controlLineSource = u"Alpha a / b omega"_q;
	const auto controlProbePosition = controlLineSource.indexOf(controlProbeText);
	Expects(controlProbePosition >= 0);
	const auto controlLineText = Ui::Text::String(
		st::defaultTextStyle,
		controlLineSource,
		kMarkupTextOptions,
		scale(64));
	const auto selectionWidth = std::max(
		formulaText->maxWidth(),
		controlLineText.maxWidth());
	const auto formulaLinesGeometry = formulaText->countLinesGeometry(
		selectionWidth);
	const auto controlLinesGeometry = controlLineText.countLinesGeometry(
		selectionWidth);
	Expects(formulaLinesGeometry.size() == 1);
	Expects(controlLinesGeometry.size() == 1);
	if ((formulaLinesGeometry.size() == 1)
		&& (controlLinesGeometry.size() == 1)) {
		const auto &formulaLine = formulaLinesGeometry.front();
		const auto &controlLine = controlLinesGeometry.front();
		Expects(formulaLine.baseline > controlLine.baseline);
		Expects(formulaLine.baseline < formulaLine.bottom);
		Expects(controlLine.baseline < controlLine.bottom);
	}
	{
		auto fieldStyle = st::defaultMultiSelectSearchField;
		fieldStyle.textMargins = QMargins(0, 0, 0, 0);
		fieldStyle.placeholderMargins = QMargins(0, 0, 0, 0);
		const auto placeholderText = u"bot query"_q;
		const auto longPlaceholderText = u"bot query placeholder placeholder placeholder placeholder placeholder"_q;
		const auto inlineBotPrefix = u"@bot "_q;
		const auto field = std::make_unique<Ui::InputField>(
			body,
			fieldStyle,
			Ui::InputField::Mode::MultiLine,
			rpl::single(placeholderText),
			QString());
		field->resize(scale(320), fieldStyle.heightMin);
		field->show();
		field->setDocumentMargin(4.);
		field->setAdditionalMargin(style::ConvertScale(4) - 4);
		field->finishAnimating();
		field->setPlaceholderHidden(true);
		field->finishAnimating();
		const auto emptyFieldImage = Ui::GrabWidgetToImage(field.get());
		field->setPlaceholderHidden(false);
		field->finishAnimating();
		const auto placeholderImage = Ui::GrabWidgetToImage(field.get());
		const auto placeholderPoint = FirstPaintedPoint(placeholderImage);
		const auto placeholderBounds = ChangedBoundsInRect(
			emptyFieldImage,
			placeholderImage,
			QRect(QPoint(), placeholderImage.size()));
		Expects(placeholderPoint.has_value());
		Expects(placeholderBounds.has_value());
		field->setText(placeholderText);
		field->finishAnimating();
		const auto liveTextImage = Ui::GrabWidgetToImage(field.get());
		const auto liveTextPoint = FirstPaintedPoint(liveTextImage);
		const auto liveTextBounds = ChangedBoundsInRect(
			emptyFieldImage,
			liveTextImage,
			QRect(QPoint(), liveTextImage.size()));
		Expects(liveTextPoint.has_value());
		Expects(liveTextBounds.has_value());
		if (placeholderPoint && liveTextPoint) {
			Expects(placeholderPoint->y() == liveTextPoint->y());
			Expects(placeholderPoint->x() == liveTextPoint->x());
		}
		const auto expectedTextRect = ScaleRect(
			field->rect().marginsRemoved(field->fullTextMargins()),
			placeholderImage.devicePixelRatio());
		field->clear();
		field->setPlaceholder(rpl::single(longPlaceholderText));
		field->finishAnimating();
		const auto longPlaceholderImage = Ui::GrabWidgetToImage(field.get());
		const auto longPlaceholderBounds = ChangedBoundsInRect(
			emptyFieldImage,
			longPlaceholderImage,
			QRect(QPoint(), longPlaceholderImage.size()));
		Expects(longPlaceholderBounds.has_value());
		if (longPlaceholderBounds) {
			Expects(expectedTextRect.contains(*longPlaceholderBounds));
		}
		field->clear();
		field->setPlaceholder(
			rpl::single(placeholderText),
			inlineBotPrefix.size());
		field->setText(inlineBotPrefix);
		field->finishAnimating();
		const auto inlinePlaceholderImage = Ui::GrabWidgetToImage(field.get());
		const auto inlineSkipWidth = qRound(
			fieldStyle.style.font->width(inlineBotPrefix)
				* inlinePlaceholderImage.devicePixelRatio());
		const auto inlineScanLeft = placeholderPoint
			? std::min(
				placeholderPoint->x() + inlineSkipWidth,
				inlinePlaceholderImage.width() - 1)
			: 0;
		const auto inlinePlaceholderPoint = FirstPaintedPoint(
			inlinePlaceholderImage,
			QRect(
				inlineScanLeft,
				0,
				inlinePlaceholderImage.width() - inlineScanLeft,
				inlinePlaceholderImage.height()));
		Expects(inlinePlaceholderPoint.has_value());
		if (placeholderPoint && inlinePlaceholderPoint) {
			Expects(placeholderPoint->y() == inlinePlaceholderPoint->y());
		}
	}
	const auto formulaSelection = TextSelection(
		0,
		uint16(formulaData.text.size()));
	const auto formulaUnselected = RenderTextOffscreen(
		*formulaText,
		selectionWidth);
	const auto formulaSelected = RenderTextOffscreen(
		*formulaText,
		selectionWidth,
		formulaSelection);
	const auto formulaHitBounds = SymbolHitBounds(
		*formulaText,
		selectionWidth,
		formulaPosition);
	Expects(formulaHitBounds.has_value());
	const auto controlSelection = TextSelection(
		0,
		uint16(controlLineSource.size()));
	const auto controlUnselected = RenderTextOffscreen(
		controlLineText,
		selectionWidth);
	const auto controlSelected = RenderTextOffscreen(
		controlLineText,
		selectionWidth,
		controlSelection);
	const auto controlHitBounds = SymbolRangeHitBounds(
		controlLineText,
		selectionWidth,
		controlProbePosition,
		controlProbeText.size());
	Expects(controlHitBounds.has_value());
	if (formulaHitBounds && controlHitBounds) {
		const auto formulaChangedBounds = ChangedBoundsInRect(
			formulaUnselected,
			formulaSelected,
			QRect(
				formulaHitBounds->x(),
				0,
				formulaHitBounds->width(),
				formulaSelected.height()));
		const auto controlChangedBounds = ChangedBoundsInRect(
			controlUnselected,
			controlSelected,
			QRect(
				controlHitBounds->x(),
				0,
				controlHitBounds->width(),
				controlSelected.height()));
		Expects(formulaChangedBounds.has_value());
		Expects(controlChangedBounds.has_value());
		if (formulaChangedBounds && controlChangedBounds) {
			Expects(
				formulaChangedBounds->top()
					<= controlChangedBounds->top());
			Expects(
				formulaChangedBounds->bottom()
					>= controlChangedBounds->bottom());
			Expects(
				(formulaChangedBounds->top()
					< controlChangedBounds->top())
				|| (formulaChangedBounds->bottom()
					> controlChangedBounds->bottom()));
		}
	}

	auto leadingFormulaData = TextWithEntities();
	leadingFormulaData.append(QChar::ObjectReplacementCharacter);
	leadingFormulaData.entities.push_back(EntityInText(
		EntityType::CustomEmoji,
		0,
		1,
		formulaEntityData));
	const auto leadingFormulaText = Ui::Text::String(
		st::defaultTextStyle,
		leadingFormulaData,
		kMarkupTextOptions,
		scale(32),
		context);
	const auto leadingFormulaRender = RenderTextOffscreen(
		leadingFormulaText,
		std::max(formulaImage.width() / 2, 1));
	Expects(HasPaintedPixels(leadingFormulaRender));

	auto skipOnlyText = Ui::Text::String(
		st::defaultTextStyle,
		u""_q,
		kDefaultTextOptions,
		scale(32));
	const auto skipBlockWidth = scale(36);
	const auto skipBlockHeight = scale(7);
	Expects(skipOnlyText.updateSkipBlock(skipBlockWidth, skipBlockHeight));
	Expects(skipOnlyText.countHeight(skipBlockWidth * 2) == skipBlockHeight);
	Expects(
		skipOnlyText.countDimensions(Ui::Text::SimpleGeometry(
			skipBlockWidth * 2,
			0,
			0,
			false)).height == skipBlockHeight);

	const auto colorizedText = u"Colorized link span"_q;
	const auto colorizedWidth = scale(280);
	const auto plainColorizedText = Ui::Text::String(
		st::defaultTextStyle,
		colorizedText,
		kMarkupTextOptions,
		scale(96));
	const auto emptyColorizedText = Ui::Text::String(
		st::defaultTextStyle,
		Ui::Text::Colorized(colorizedText),
		kMarkupTextOptions,
		scale(96));
	const auto emptyColorizedRender = RenderTextOffscreen(
		emptyColorizedText,
		colorizedWidth);
	const auto colorizedBoundsRect = QRect(
		QPoint(),
		emptyColorizedRender.size());
	const auto linkPenRender = RenderTextOffscreen(
		plainColorizedText,
		colorizedWidth,
		std::nullopt,
		{},
		st::defaultTextPalette.linkFg->p);
	const auto plainColorizedRender = RenderTextOffscreen(
		plainColorizedText,
		colorizedWidth);
	Expects(ImagesEqualInRect(
		emptyColorizedRender,
		linkPenRender,
		colorizedBoundsRect));
	Expects(!ImagesEqualInRect(
		emptyColorizedRender,
		plainColorizedRender,
		colorizedBoundsRect));

	const auto &specialFg = st::defaultTextPalette.linkFg;
	const auto &specialBg = st::defaultTextPalette.markBg;
	auto specialColors = std::vector<Ui::Text::SpecialColor>{
		Ui::Text::SpecialColor{
			&specialFg->p,
			&specialFg->p,
			&specialBg->b,
			&specialBg->b },
	};
	const auto explicitColorizedNoBgText = Ui::Text::String(
		st::defaultTextStyle,
		Ui::Text::Colorized(colorizedText, 1, 0),
		kMarkupTextOptions,
		scale(96));
	const auto explicitColorizedBgText = Ui::Text::String(
		st::defaultTextStyle,
		Ui::Text::Colorized(colorizedText, 1, 1),
		kMarkupTextOptions,
		scale(96));
	const auto explicitColorizedNoBgRender = RenderTextOffscreen(
		explicitColorizedNoBgText,
		colorizedWidth,
		std::nullopt,
		specialColors);
	const auto explicitColorizedBgRender = RenderTextOffscreen(
		explicitColorizedBgText,
		colorizedWidth,
		std::nullopt,
		specialColors);
	const auto specialPenRender = RenderTextOffscreen(
		plainColorizedText,
		colorizedWidth,
		std::nullopt,
		{},
		specialFg->p);
	const auto colorizedSymbolBounds = SymbolRangeHitBounds(
		explicitColorizedBgText,
		colorizedWidth,
		0,
		colorizedText.size());
	Expects(ImagesEqualInRect(
		explicitColorizedNoBgRender,
		specialPenRender,
		colorizedBoundsRect));
	Expects(colorizedSymbolBounds.has_value());
	if (colorizedSymbolBounds) {
		Expects(!ImagesEqualInRect(
			explicitColorizedBgRender,
			explicitColorizedNoBgRender,
			*colorizedSymbolBounds));
		Expects(HasPixelColorInRect(
			explicitColorizedBgRender,
			*colorizedSymbolBounds,
			specialBg->c.rgba()));
		Expects(!HasPixelColorInRect(
			explicitColorizedNoBgRender,
			*colorizedSymbolBounds,
			specialBg->c.rgba()));
	}

	body->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(body);
		p.fillRect(clip, QColor(255, 255, 255));
		const auto left = scale(24);
		const auto top = scale(24);
		const auto width = body->width() - (2 * left);
		formulaText->draw(p, {
			.position = QPoint(left, top),
			.availableWidth = width,
		});
		controlText->draw(p, {
			.position = QPoint(
				left,
				top + formulaText->countHeight(width) + scale(20)),
			.availableWidth = width,
		});
	}, body->lifetime());
}

} // namespace Test
