/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/basic_click_handlers.h"
#include "ui/dynamic_image.h"
#include "ui/integration.h"

#include "styles/palette.h"
#include "styles/style_iv.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace Iv::Markdown {
namespace {

constexpr auto kIvMarkedTextOptions = TextParseOptions{
	TextParseMultiline,
	0,
	0,
	Qt::LayoutDirectionAuto,
};

constexpr auto kIvMarkedTextOptionsRtl = TextParseOptions{
	TextParseMultiline,
	0,
	0,
	Qt::RightToLeft,
};

struct PreparedLinkExternalData {
	ClickHandler::TextEntity entity;
	QString copyText;
	QString copyLabel;
};

[[nodiscard]] QString OpenableTargetForLink(const PreparedLink &link) {
	return link.fragment.isEmpty()
		? link.target
		: (link.target + u"#"_q + link.fragment);
}

[[nodiscard]] std::optional<PreparedLinkExternalData> ExternalDataForLink(
		const PreparedLink &link) {
	if (link.kind != PreparedLinkKind::External
		&& link.kind != PreparedLinkKind::InstantViewPage) {
		return std::nullopt;
	}
	const auto target = OpenableTargetForLink(link);
	auto type = link.entityType;
	if (type != EntityType::Url
		&& type != EntityType::CustomUrl
		&& type != EntityType::Email) {
		if (target.isEmpty()) {
			return std::nullopt;
		}
		type = UrlClickHandler::IsEmail(target)
			? EntityType::Email
			: EntityType::CustomUrl;
	}
	return PreparedLinkExternalData{
		.entity = { type, target },
		.copyText = link.fragment.isEmpty() && !link.copyText.isEmpty()
			? link.copyText
			: target,
		.copyLabel = (type == EntityType::Email)
			? Ui::Integration::Instance().phraseContextCopyEmail()
			: Ui::Integration::Instance().phraseContextCopyLink(),
	};
}

[[nodiscard]] ClickHandler::TextEntity TextEntityForLink(
		const PreparedLink &link) {
	if (const auto external = ExternalDataForLink(link)) {
		return external->entity;
	}
	return {};
}

[[nodiscard]] QString CopyTextForLink(const PreparedLink &link) {
	if (const auto external = ExternalDataForLink(link)) {
		return external->copyText;
	}
	switch (link.kind) {
	case PreparedLinkKind::Anchor:
	case PreparedLinkKind::FootnoteBacklink:
		return link.target.isEmpty() ? QString() : (u"#"_q + link.target);
	case PreparedLinkKind::Footnote:
		return !link.copyText.isEmpty() ? link.copyText : link.target;
	case PreparedLinkKind::LocalFile:
		return link.fragment.isEmpty()
			? link.target
			: (link.target + u"#"_q + link.fragment);
	case PreparedLinkKind::External:
	case PreparedLinkKind::InstantViewPage:
		return link.target;
	case PreparedLinkKind::RejectedRelative:
	case PreparedLinkKind::ToggleDetails:
		return QString();
	}
	return QString();
}

[[nodiscard]] QString CopyLabelForLink(const PreparedLink &link) {
	if (const auto external = ExternalDataForLink(link)) {
		return external->copyLabel;
	}
	switch (link.kind) {
	case PreparedLinkKind::RejectedRelative:
	case PreparedLinkKind::ToggleDetails:
		return QString();
	case PreparedLinkKind::External:
	case PreparedLinkKind::InstantViewPage:
	case PreparedLinkKind::Anchor:
	case PreparedLinkKind::Footnote:
	case PreparedLinkKind::FootnoteBacklink:
	case PreparedLinkKind::LocalFile:
		return tr::lng_context_copy_link(tr::now);
	}
	return QString();
}

class PreparedLinkClickHandler final : public ClickHandler {
public:
	explicit PreparedLinkClickHandler(PreparedLink link);

	void onClick(ClickContext) const override;

	[[nodiscard]] const PreparedLink &link() const;

	QString url() const override;

	QString copyToClipboardText() const override;

	QString copyToClipboardContextItemText() const override;

	TextEntity getTextEntity() const override;

	QString tooltip() const override;

private:
	PreparedLink _link;

};

PreparedLinkClickHandler::PreparedLinkClickHandler(PreparedLink link)
: _link(std::move(link)) {
}

void PreparedLinkClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton
		&& context.button != Qt::MiddleButton) {
		return;
	}
	const auto data = ExternalEntityLinkData(_link);
	if (!data) {
		return;
	}
	if (const auto handler = Ui::Integration::Instance().createLinkHandler(
			*data,
			Ui::Text::MarkedContext())) {
		handler->onClick(std::move(context));
	}
}

const PreparedLink &PreparedLinkClickHandler::link() const {
	return _link;
}

QString PreparedLinkClickHandler::url() const {
	return _link.target;
}

QString PreparedLinkClickHandler::copyToClipboardText() const {
	return CopyTextForLink(_link);
}

QString PreparedLinkClickHandler::copyToClipboardContextItemText() const {
	return copyToClipboardText().isEmpty()
		? QString()
		: CopyLabelForLink(_link);
}

ClickHandler::TextEntity PreparedLinkClickHandler::getTextEntity() const {
	return TextEntityForLink(_link);
}

QString PreparedLinkClickHandler::tooltip() const {
	return TooltipForPreparedLink(_link);
}

[[nodiscard]] int FormulaTextSize(const style::TextStyle &textStyle) {
	return std::max(textStyle.font->height, 1);
}

[[nodiscard]] int ScaleFormulaCap(int cap, int textSize, int baseTextSize) {
	if (cap <= 0) {
		return 0;
	}
	const auto numerator = int64(cap) * std::max(textSize, 1);
	const auto denominator = std::max(baseTextSize, 1);
	return std::max(int((numerator + denominator - 1) / denominator), 1);
}

[[nodiscard]] PreparedFormulaMeasurementSignature FormulaRenderSignature(
		const PreparedFormulaSlot &slot,
		const style::Markdown &st) {
	const auto &displayMath = st.displayMath;
	return {
		.trimmedTex = slot.trimmedTex.trimmed(),
		.kind = slot.kind,
		.textSize = slot.textSize ? slot.textSize : displayMath.textSize,
		.renderWidthCap = slot.renderWidthCap
			? slot.renderWidthCap
			: displayMath.maxRenderWidth,
		.renderHeightCap = slot.renderHeightCap
			? slot.renderHeightCap
			: displayMath.maxRenderHeight,
	};
}

[[nodiscard]] PreparedFormulaMeasurementSignature InlineFormulaSignature(
		QString trimmedTex,
		const style::TextStyle &textStyle,
		const style::Markdown &st) {
	const auto &displayMath = st.displayMath;
	const auto textSize = FormulaTextSize(textStyle);
	return {
		.trimmedTex = std::move(trimmedTex).trimmed(),
		.kind = MathKind::Inline,
		.textSize = textSize,
		.renderWidthCap = ScaleFormulaCap(
			displayMath.maxRenderWidth,
			textSize,
			displayMath.textSize),
		.renderHeightCap = ScaleFormulaCap(
			displayMath.maxRenderHeight,
			textSize,
			displayMath.textSize),
	};
}

template <typename Formula>
void NormalizeInlineFormulaRasterMetrics(Formula *formula);

[[nodiscard]] RenderedFormula MeasuredFallback(const MeasuredFormula &measured) {
	auto result = RenderedFormula();
	result.logicalSize = measured.logicalSize;
	result.logicalDepth = measured.logicalDepth;
	result.exact = measured.exact;
	result.fallbackText = measured.fallbackText;
	result.error = measured.error;
	result.success = false;
	result.overflow = measured.overflow;
	result.tooLarge = measured.tooLarge;
	NormalizeInlineFormulaRasterMetrics(&result);
	return result;
}

[[nodiscard]] int RenderFormulaDevicePixelRatio(const RenderedFormula &formula) {
	const auto ratio = formula.image.devicePixelRatio();
	return (ratio > 0.) ? int(std::round(ratio)) : 0;
}

[[nodiscard]] int RoundedInlineFormulaMetric(int scaledValue) {
	return (scaledValue > 0)
		? ((scaledValue + kFormulaExactMetricScale - 1)
			/ kFormulaExactMetricScale)
		: 0;
}

[[nodiscard]] int FlooredInlineFormulaMetric(int scaledValue) {
	return (scaledValue >= 0)
		? (scaledValue / kFormulaExactMetricScale)
		: -((-scaledValue + kFormulaExactMetricScale - 1)
			/ kFormulaExactMetricScale);
}

[[nodiscard]] qreal LogicalInlineFormulaMetric(int scaledValue) {
	return qreal(scaledValue) / qreal(kFormulaExactMetricScale);
}

struct InlineFormulaColorizedKey {
	QRgb color = 0;
	int devicePixelRatio = 0;

	friend bool operator<(
		InlineFormulaColorizedKey a,
		InlineFormulaColorizedKey b);
};

bool operator<(
		InlineFormulaColorizedKey a,
		InlineFormulaColorizedKey b) {
	if (a.color != b.color) {
		return a.color < b.color;
	}
	return a.devicePixelRatio < b.devicePixelRatio;
}

class InlineFormulaSharedState final {
public:
	InlineFormulaSharedState(
		PreparedFormulaMeasurementSignature signature,
		std::shared_ptr<const MeasuredFormula> measuredData,
		QString displayFallbackText,
		std::shared_ptr<MathRenderer> renderer);

	[[nodiscard]] int width() const;
	[[nodiscard]] bool failed() const;
	[[nodiscard]] std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) const;
	void paint(
		QPainter &p,
		const Ui::Text::CustomEmoji::Context &context,
		const QString &replacementText,
		int fallbackWidth) const;
	void setRenderer(std::shared_ptr<MathRenderer> renderer);
	void invalidatePaletteCache();
	void invalidateRasterCache();

private:
	[[nodiscard]] const MeasuredFormula &measured() const;
	[[nodiscard]] MathRenderer *renderer() const;
	[[nodiscard]] RenderedFormula ensureRendered(int devicePixelRatio) const;
	[[nodiscard]] const QImage *colorizedImage(
		const QColor &color,
		int devicePixelRatio) const;

	PreparedFormulaMeasurementSignature _signature;
	std::shared_ptr<const MeasuredFormula> _measuredData;
	QString _displayFallbackText;
	mutable std::shared_ptr<MathRenderer> _renderer;
	mutable std::map<int, RenderedFormula> _rendered;
	mutable std::map<InlineFormulaColorizedKey, QImage> _colorized;

};

struct InlineFormulaGeometry {
	int width = 1;
	int imageHeight = 0;
	int imageDescent = 0;
	int ascent = 0;
	int descent = 0;
	int paintOffsetYScaled = 0;
};

template <typename Formula>
[[nodiscard]] InlineFormulaGeometry InlineFormulaGeometryFrom(
		const Formula &formula) {
	const auto imageHeight = std::max(formula.logicalSize.height(), 0);
	const auto imageDescent = std::clamp(formula.logicalDepth, 0, imageHeight);
	auto result = InlineFormulaGeometry{
		.width = std::max(formula.logicalSize.width(), 1),
		.imageHeight = imageHeight,
		.imageDescent = imageDescent,
		.ascent = imageHeight - imageDescent,
		.descent = imageDescent,
	};
	const auto &exact = formula.exact;
	const auto exactHeight = exact.scaledSize.height();
	if ((exact.scaledSize.width() <= 0) || (exactHeight <= 0)) {
		return result;
	}
	const auto exactAscent = std::clamp(exact.scaledAscent, 0, exactHeight);
	const auto exactDescent = std::max(exactHeight - exactAscent, 0);
	const auto topInset = std::clamp(exact.scaledInsets.top(), 0, exactAscent);
	const auto bottomInset = std::clamp(
		exact.scaledInsets.bottom(),
		0,
		exactDescent);
	const auto paintTop = topInset - exactAscent;
	const auto paintBottom = std::max(exactDescent - bottomInset, 0);
	const auto imageWidth = RoundedInlineFormulaMetric(
		exact.scaledSize.width());
	const auto exactImageHeight = RoundedInlineFormulaMetric(exactHeight);
	const auto exactImageAscent = std::clamp(
		RoundedInlineFormulaMetric(exactAscent),
		0,
		exactImageHeight);
	const auto top = FlooredInlineFormulaMetric(paintTop);
	const auto bottom = RoundedInlineFormulaMetric(paintBottom);
	result.width = std::max(imageWidth, 1);
	result.imageHeight = exactImageHeight;
	result.imageDescent = std::max(exactImageHeight - exactImageAscent, 0);
	if (top < 0 || bottom > 0) {
		result.ascent = std::max(-top, 0);
		result.descent = std::max(bottom, 0);
		result.paintOffsetYScaled = (-exactAscent) - (top * kFormulaExactMetricScale);
	} else if (exactImageHeight > 0) {
		result.ascent = exactImageHeight - result.imageDescent;
		result.descent = result.imageDescent;
	}
	return result;
}

template <typename Formula>
void NormalizeInlineFormulaRasterMetrics(Formula *formula) {
	if (!formula) {
		return;
	}
	const auto geometry = InlineFormulaGeometryFrom(*formula);
	formula->logicalSize = QSize(geometry.width, geometry.imageHeight);
	formula->logicalDepth = geometry.imageDescent;
}

class InlineFormulaObject final : public Ui::Text::CustomEmoji {
public:
	InlineFormulaObject(
		QString entityData,
		QString replacementText,
		int fallbackWidth,
		std::shared_ptr<InlineFormulaSharedState> state);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) override;
	QString replacementText() override;
	Ui::Text::CustomEmojiSemantics semantics() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	QString _entityData;
	QString _replacementText;
	int _fallbackWidth = 1;
	const std::shared_ptr<InlineFormulaSharedState> _state;

};

class InlineIvImageObject final : public Ui::Text::CustomEmoji {
public:
	InlineIvImageObject(
		QString replacementText,
		int width,
		int height,
		std::shared_ptr<Ui::DynamicImage> image,
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) override;
	QString replacementText() override;
	Ui::Text::CustomEmojiSemantics semantics() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	QString _replacementText;
	int _width = 1;
	int _height = 1;
	const std::shared_ptr<Ui::DynamicImage> _image;
	const Fn<void()> _repaint;
	const Fn<void(QRect)> _repaintRect;
	const std::shared_ptr<QRect> _lastPaintRect = std::make_shared<QRect>();
	bool _subscribed = false;

};

[[nodiscard]] QString InlineFormulaDisplayFallbackText(
		const PreparedFormulaMeasurementSignature &signature,
		const MeasuredFormula &measured) {
	if (!measured.fallbackText.isEmpty()) {
		return measured.fallbackText;
	} else if (!signature.trimmedTex.isEmpty()) {
		return signature.trimmedTex;
	}
	return u"[math]"_q;
}

[[nodiscard]] std::shared_ptr<const MeasuredFormula>
FindInlineFormulaMeasuredData(
		const std::vector<PreparedFormulaSlot> *formulas,
		const PreparedFormulaMeasurementSignature &signature,
		const style::Markdown &st,
		MeasuredFormula *measured) {
	if (!formulas) {
		return nullptr;
	}
	for (const auto &slot : *formulas) {
		if (!slot.present
			|| (FormulaRenderSignature(slot, st) != signature)) {
			continue;
		}
		if (measured) {
			*measured = slot.measured;
		}
		if (slot.measuredData) {
			return slot.measuredData;
		}
		return std::make_shared<MeasuredFormula>(slot.measured);
	}
	return nullptr;
}

} // namespace

ClickHandlerPtr CreatePreparedLinkHandler(PreparedLink link) {
	return std::make_shared<PreparedLinkClickHandler>(std::move(link));
}

std::optional<PreparedLink> ExtractPreparedLink(const ClickHandlerPtr &link) {
	if (const auto prepared = std::dynamic_pointer_cast<PreparedLinkClickHandler>(
			link)) {
		return prepared->link();
	}
	return std::nullopt;
}

void BindLinks(
		Ui::Text::String *leaf,
		const std::vector<PreparedLink> &links) {
	for (const auto &link : links) {
		leaf->setLink(
			link.index,
			CreatePreparedLinkHandler(link));
	}
}

void SetTextLeafSpoilerLinkFilter(
		Ui::Text::String *leaf,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	if (!leaf->hasSpoilers()) {
		return;
	}
	if (spoilerLinkFilter) {
		leaf->setSpoilerLinkFilter(std::move(spoilerLinkFilter));
	} else {
		leaf->setSpoilerLinkFilter([](const ClickContext &context) {
			return context.button == Qt::LeftButton;
		});
	}
}

const PreparedFormulaSlot *PreparedFormulaFor(
		const std::vector<PreparedFormulaSlot> &formulas,
		int formulaIndex) {
	if (formulaIndex < 0 || formulaIndex >= int(formulas.size())) {
		return nullptr;
	} else if (!formulas[formulaIndex].present) {
		return nullptr;
	}
	return &formulas[formulaIndex];
}

PreparedFormulaSlot *PreparedFormulaFor(
		std::vector<PreparedFormulaSlot> *formulas,
		int formulaIndex) {
	if (!formulas || formulaIndex < 0 || formulaIndex >= int(formulas->size())) {
		return nullptr;
	} else if (!(*formulas)[formulaIndex].present) {
		return nullptr;
	}
	return &(*formulas)[formulaIndex];
}

RenderedFormula *FormulaRasterSlot(
		std::vector<RenderedFormula> *rendered,
		int formulaIndex) {
	if (!rendered || formulaIndex < 0) {
		return nullptr;
	}
	if (formulaIndex >= int(rendered->size())) {
		rendered->resize(formulaIndex + 1);
	}
	return &(*rendered)[formulaIndex];
}

RenderedFormula EnsureFormulaRendered(
		const PreparedFormulaMeasurementSignature &signature,
		const MeasuredFormula &measured,
		RenderedFormula *rendered,
		MathRenderer *renderer,
		int devicePixelRatio) {
	if (!measured.success) {
		return MeasuredFallback(measured);
	}
	if (rendered
		&& rendered->success
		&& (RenderFormulaDevicePixelRatio(*rendered) == devicePixelRatio)) {
		return *rendered;
	}
	auto ownedRenderer = std::shared_ptr<MathRenderer>();
	if (!renderer) {
		ownedRenderer = std::make_shared<MathRenderer>();
		renderer = ownedRenderer.get();
	}
	auto local = renderer->renderFormula({
		.trimmedTex = signature.trimmedTex,
		.kind = signature.kind,
		.textSize = signature.textSize,
		.renderWidthCap = signature.renderWidthCap,
		.renderHeightCap = signature.renderHeightCap,
		.devicePixelRatio = devicePixelRatio,
	});
	if (local.logicalSize.isEmpty()) {
		local.logicalSize = measured.logicalSize;
		local.logicalDepth = measured.logicalDepth;
		local.exact = measured.exact;
		local.fallbackText = measured.fallbackText;
		local.error = measured.error;
		local.overflow = measured.overflow;
		local.tooLarge = measured.tooLarge;
		NormalizeInlineFormulaRasterMetrics(&local);
	}
	if (rendered) {
		*rendered = std::move(local);
		return rendered->success ? *rendered : MeasuredFallback(measured);
	}
	return local.success ? local : MeasuredFallback(measured);
}

RenderedFormula EnsureFormulaRendered(
		const PreparedFormulaSlot *slot,
		RenderedFormula *rendered,
		MathRenderer *renderer,
		int devicePixelRatio,
		const style::Markdown &st) {
	if (!slot) {
		return RenderedFormula();
	}
	return EnsureFormulaRendered(
		FormulaRenderSignature(*slot, st),
		slot->measured,
		rendered,
		renderer,
		devicePixelRatio);
}

class InlineFormulaObjectCache final {
public:
	InlineFormulaObjectCache() = default;

	void setRenderer(std::shared_ptr<MathRenderer> renderer);
	void clear();
	void invalidatePaletteCache();
	void invalidateRasterCache();
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		const InlineTextObjectFormulaData &data,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const std::vector<PreparedFormulaSlot> *formulas);

private:
	[[nodiscard]] std::shared_ptr<InlineFormulaSharedState> lookupOrCreate(
		const PreparedFormulaMeasurementSignature &signature,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const std::vector<PreparedFormulaSlot> *formulas);

	std::shared_ptr<MathRenderer> _renderer;
	std::map<
		PreparedFormulaMeasurementSignature,
		std::shared_ptr<InlineFormulaSharedState>,
		PreparedFormulaMeasurementSignatureLess> _states;

};

InlineFormulaSharedState::InlineFormulaSharedState(
	PreparedFormulaMeasurementSignature signature,
	std::shared_ptr<const MeasuredFormula> measuredData,
	QString displayFallbackText,
	std::shared_ptr<MathRenderer> renderer)
: _signature(std::move(signature))
, _measuredData(std::move(measuredData))
, _displayFallbackText(std::move(displayFallbackText))
, _renderer(std::move(renderer)) {
}

int InlineFormulaSharedState::width() const {
	const auto geometry = InlineFormulaGeometryFrom(measured());
	return (measured().success && (geometry.width > 0))
		? geometry.width
		: 1;
}

bool InlineFormulaSharedState::failed() const {
	return !measured().success;
}

auto InlineFormulaSharedState::vertical(const style::TextStyle &textStyle) const
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	const auto &formula = measured();
	const auto geometry = InlineFormulaGeometryFrom(formula);
	if (formula.success && (geometry.imageHeight > 0)) {
		return Ui::Text::CustomEmojiVerticalMetrics{
			.ascent = geometry.ascent,
			.descent = geometry.descent,
		};
	}
	const auto ascent = std::max(TextLineAscent(textStyle), 0);
	return Ui::Text::CustomEmojiVerticalMetrics{
		.ascent = ascent,
		.descent = std::max(TextLineHeight(textStyle) - ascent, 0),
	};
}

void InlineFormulaSharedState::paint(
		QPainter &p,
		const Ui::Text::CustomEmoji::Context &context,
		const QString &replacementText,
		int fallbackWidth) const {
	const auto rendered = ensureRendered(std::max(style::DevicePixelRatio(), 1));
	if (rendered.success) {
		const auto geometry = InlineFormulaGeometryFrom(rendered);
		if (const auto image = colorizedImage(
				context.textColor,
				std::max(style::DevicePixelRatio(), 1))) {
			p.drawImage(
				QPointF(context.position)
					+ QPointF(0., LogicalInlineFormulaMetric(
						geometry.paintOffsetYScaled)),
				*image);
		}
		return;
	}
	const auto fallbackText = replacementText.isEmpty()
		? _displayFallbackText
		: replacementText;
	if (fallbackText.isEmpty()) {
		return;
	}
	p.save();
	p.setPen(context.textColor);
	p.drawText(
		QRect(
			context.position.x(),
			context.position.y(),
			std::max(fallbackWidth, 1),
			p.fontMetrics().height()),
		Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
		fallbackText);
	p.restore();
}

void InlineFormulaSharedState::setRenderer(std::shared_ptr<MathRenderer> renderer) {
	_renderer = std::move(renderer);
	invalidateRasterCache();
}

void InlineFormulaSharedState::invalidatePaletteCache() {
	_colorized.clear();
}

void InlineFormulaSharedState::invalidateRasterCache() {
	_rendered.clear();
	_colorized.clear();
}

const MeasuredFormula &InlineFormulaSharedState::measured() const {
	static const auto kEmpty = MeasuredFormula();
	return _measuredData ? *_measuredData : kEmpty;
}

MathRenderer *InlineFormulaSharedState::renderer() const {
	if (!_renderer) {
		_renderer = std::make_shared<MathRenderer>();
	}
	return _renderer.get();
}

RenderedFormula InlineFormulaSharedState::ensureRendered(
		int devicePixelRatio) const {
	if (!measured().success) {
		return MeasuredFallback(measured());
	}
	if (const auto i = _rendered.find(devicePixelRatio); i != end(_rendered)) {
		return i->second;
	}
	auto rendered = renderer()->renderFormula({
		.trimmedTex = _signature.trimmedTex,
		.kind = _signature.kind,
		.textSize = _signature.textSize,
		.renderWidthCap = _signature.renderWidthCap,
		.renderHeightCap = _signature.renderHeightCap,
		.devicePixelRatio = devicePixelRatio,
	});
	if (rendered.logicalSize.isEmpty()) {
		rendered.logicalSize = measured().logicalSize;
		rendered.logicalDepth = measured().logicalDepth;
		rendered.exact = measured().exact;
		rendered.fallbackText = measured().fallbackText;
		rendered.error = measured().error;
		rendered.overflow = measured().overflow;
		rendered.tooLarge = measured().tooLarge;
		NormalizeInlineFormulaRasterMetrics(&rendered);
	}
	if (!rendered.success) {
		rendered = MeasuredFallback(measured());
	}
	const auto i = _rendered.emplace(
		devicePixelRatio,
		std::move(rendered)).first;
	return i->second;
}

const QImage *InlineFormulaSharedState::colorizedImage(
		const QColor &color,
		int devicePixelRatio) const {
	const auto rendered = ensureRendered(devicePixelRatio);
	if (!rendered.success) {
		return nullptr;
	}
	const auto key = InlineFormulaColorizedKey{
		.color = color.rgba(),
		.devicePixelRatio = devicePixelRatio,
	};
	if (const auto i = _colorized.find(key); i != end(_colorized)) {
		return &i->second;
	}
	auto colorized = QImage(
		rendered.image.size(),
		QImage::Format_ARGB32_Premultiplied);
	style::colorizeImage(
		rendered.image,
		color,
		&colorized,
		QRect(),
		QPoint(),
		true);
	const auto i = _colorized.emplace(
		key,
		std::move(colorized)).first;
	return &i->second;
}

InlineFormulaObject::InlineFormulaObject(
	QString entityData,
	QString replacementText,
	int fallbackWidth,
	std::shared_ptr<InlineFormulaSharedState> state)
: _entityData(std::move(entityData))
, _replacementText(std::move(replacementText))
, _fallbackWidth(std::max(fallbackWidth, 1))
, _state(std::move(state)) {
}

int InlineFormulaObject::width() {
	if (!_state || _state->failed()) {
		return _fallbackWidth;
	}
	return _state->width();
}

QString InlineFormulaObject::entityData() {
	return _entityData;
}

auto InlineFormulaObject::vertical(const style::TextStyle &textStyle)
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	return _state ? _state->vertical(textStyle) : std::nullopt;
}

QString InlineFormulaObject::replacementText() {
	return _replacementText;
}

Ui::Text::CustomEmojiSemantics InlineFormulaObject::semantics() {
	return {
		.isEmoji = false,
		.isRealCustomEmoji = false,
		.exportEntity = false,
		.unloadPersistentAnimation = false,
		.allowCustomEmojiClick = false,
	};
}

void InlineFormulaObject::paint(QPainter &p, const Context &context) {
	if (_state) {
		_state->paint(p, context, _replacementText, _fallbackWidth);
	}
}

void InlineFormulaObject::unload() {
}

bool InlineFormulaObject::ready() {
	return true;
}

bool InlineFormulaObject::readyInDefaultState() {
	return true;
}

void InlineFormulaObjectCache::setRenderer(std::shared_ptr<MathRenderer> renderer) {
	_renderer = std::move(renderer);
	for (const auto &entry : _states) {
		const auto &state = entry.second;
		if (state) {
			state->setRenderer(_renderer);
		}
	}
}

void InlineFormulaObjectCache::clear() {
	_states.clear();
}

void InlineFormulaObjectCache::invalidatePaletteCache() {
	for (const auto &entry : _states) {
		const auto &state = entry.second;
		if (state) {
			state->invalidatePaletteCache();
		}
	}
}

void InlineFormulaObjectCache::invalidateRasterCache() {
	for (const auto &entry : _states) {
		const auto &state = entry.second;
		if (state) {
			state->invalidateRasterCache();
		}
	}
}

InlineIvImageObject::InlineIvImageObject(
	QString replacementText,
	int width,
	int height,
	std::shared_ptr<Ui::DynamicImage> image,
	Fn<void()> repaint,
	Fn<void(QRect)> repaintRect)
: _replacementText(std::move(replacementText))
, _width(std::max(width, 1))
, _height(std::max(height, 1))
, _image(std::move(image))
, _repaint(std::move(repaint))
, _repaintRect(std::move(repaintRect)) {
}

int InlineIvImageObject::width() {
	return _width;
}

QString InlineIvImageObject::entityData() {
	return QString();
}

auto InlineIvImageObject::vertical(const style::TextStyle &textStyle)
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	if (_height > 0) {
		const auto line = TextLineHeight(textStyle);
		const auto above = _height - (_height / 2);
		const auto ascent = above - (line / 2) + TextLineAscent(textStyle);
		return Ui::Text::CustomEmojiVerticalMetrics{
			.ascent = ascent,
			.descent = _height - ascent,
		};
	}
	const auto ascent = std::max(TextLineAscent(textStyle), 0);
	return Ui::Text::CustomEmojiVerticalMetrics{
		.ascent = ascent,
		.descent = std::max(TextLineHeight(textStyle) - ascent, 0),
	};
}

QString InlineIvImageObject::replacementText() {
	return _replacementText;
}

Ui::Text::CustomEmojiSemantics InlineIvImageObject::semantics() {
	return {
		.isEmoji = false,
		.isRealCustomEmoji = false,
		.exportEntity = false,
		.unloadPersistentAnimation = false,
		.allowCustomEmojiClick = false,
	};
}

void InlineIvImageObject::paint(QPainter &p, const Context &context) {
	*_lastPaintRect = QRect(context.position, QSize(_width, _height));
	if (_image) {
		if (!_subscribed) {
			_subscribed = true;
			const auto repaint = _repaint;
			const auto repaintRect = _repaintRect;
			const auto lastPaintRect = _lastPaintRect;
			_image->subscribeToUpdates([=] {
				if (repaintRect && lastPaintRect && !lastPaintRect->isEmpty()) {
					repaintRect(*lastPaintRect);
				} else if (repaint) {
					repaint();
				}
			});
		}
		if (const auto image = _image->image(std::max(_width, _height));
			!image.isNull()) {
			p.drawImage(
				QRect(context.position, QSize(_width, _height)),
				image);
			return;
		}
	}
	if (_replacementText.isEmpty()) {
		return;
	}
	p.save();
	p.setPen(context.textColor);
	p.drawText(
		QRect(context.position, QSize(_width, _height)),
		Qt::AlignCenter | Qt::TextWordWrap,
		_replacementText);
	p.restore();
}

void InlineIvImageObject::unload() {
	if (_subscribed && _image) {
		_subscribed = false;
		_image->subscribeToUpdates(nullptr);
	}
}

bool InlineIvImageObject::ready() {
	return true;
}

bool InlineIvImageObject::readyInDefaultState() {
	return true;
}

std::unique_ptr<Ui::Text::CustomEmoji> InlineFormulaObjectCache::create(
		const InlineTextObjectFormulaData &data,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const std::vector<PreparedFormulaSlot> *formulas) {
	auto replacementText = data.copySource;
	if (replacementText.isEmpty()) {
		replacementText = u"$"_q + data.trimmedTex + u"$"_q;
	}
	auto state = lookupOrCreate(
		InlineFormulaSignature(data.trimmedTex, textStyle, st),
		textStyle,
		st,
		formulas);
	if (!state) {
		return nullptr;
	}
	const auto fallbackWidth = std::max(
		textStyle.font->width(replacementText),
		1);
	const auto entityData = SerializeInlineTextObjectEntity({
		.kind = InlineTextObjectKind::Formula,
		.data = data,
	});
	return std::make_unique<InlineFormulaObject>(
		std::move(entityData),
		std::move(replacementText),
		fallbackWidth,
		std::move(state));
}

auto InlineFormulaObjectCache::lookupOrCreate(
		const PreparedFormulaMeasurementSignature &signature,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const std::vector<PreparedFormulaSlot> *formulas)
-> std::shared_ptr<InlineFormulaSharedState> {
	if (const auto i = _states.find(signature); i != end(_states)) {
		return i->second;
	}
	auto measured = MeasuredFormula();
	auto measuredData = FindInlineFormulaMeasuredData(
		formulas,
		signature,
		st,
		&measured);
	if (measuredData) {
		measured = *measuredData;
	} else {
		if (!_renderer) {
			_renderer = std::make_shared<MathRenderer>();
		}
		measured = _renderer->measureFormula({
			.trimmedTex = signature.trimmedTex,
			.kind = signature.kind,
			.textSize = signature.textSize,
			.renderWidthCap = signature.renderWidthCap,
			.renderHeightCap = signature.renderHeightCap,
		});
		measuredData = std::make_shared<MeasuredFormula>(measured);
	}
	const auto fallbackText = InlineFormulaDisplayFallbackText(
		signature,
		measured);
	auto state = std::make_shared<InlineFormulaSharedState>(
		signature,
		std::move(measuredData),
		fallbackText,
		_renderer);
	_states.emplace(signature, state);
	return state;
}

std::shared_ptr<InlineFormulaObjectCache> CreateInlineFormulaObjectCache(
		std::shared_ptr<MathRenderer> renderer) {
	auto result = std::make_shared<InlineFormulaObjectCache>();
	result->setRenderer(std::move(renderer));
	return result;
}

void SetInlineFormulaObjectCacheRenderer(
		const std::shared_ptr<InlineFormulaObjectCache> &cache,
		std::shared_ptr<MathRenderer> renderer) {
	if (cache) {
		cache->setRenderer(std::move(renderer));
	}
}

void ClearInlineFormulaObjectCache(
		const std::shared_ptr<InlineFormulaObjectCache> &cache) {
	if (cache) {
		cache->clear();
	}
}

void InvalidateInlineFormulaPaletteCache(
		const std::shared_ptr<InlineFormulaObjectCache> &cache) {
	if (cache) {
		cache->invalidatePaletteCache();
	}
}

void InvalidateInlineFormulaRasterCache(
		const std::shared_ptr<InlineFormulaObjectCache> &cache) {
	if (cache) {
		cache->invalidateRasterCache();
	}
}

void SetTextLeaf(
		Ui::Text::String *leaf,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const TextWithEntities &text,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		int minResizeWidth,
		bool rtl,
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	*leaf = Ui::Text::String(TextMinResizeWidth(minResizeWidth));
	auto context = mediaRuntime
		? mediaRuntime->textContext()
		: Ui::Text::MarkedContext();
	context.repaint = repaint;
	const auto textStylePtr = &textStyle;
	const auto stPtr = &st;
	auto originalCustomEmojiFactory = std::move(context.customEmojiFactory);
	context.customEmojiFactory = [
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		repaintRect = std::move(repaintRect),
		originalCustomEmojiFactory = std::move(originalCustomEmojiFactory),
		textStyle = textStylePtr,
		st = stPtr
	](
			QStringView data,
			const Ui::Text::MarkedContext &context
	) -> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto parsed = ParseInlineTextObjectEntity(data);
		if (!parsed) {
			return originalCustomEmojiFactory
				? originalCustomEmojiFactory(data, context)
				: std::unique_ptr<Ui::Text::CustomEmoji>();
		}
		switch (parsed->kind) {
		case InlineTextObjectKind::Formula: {
			if (!inlineFormulaObjects) {
				return std::unique_ptr<Ui::Text::CustomEmoji>();
			}
			const auto formula = std::get_if<InlineTextObjectFormulaData>(
				&parsed->data);
			return formula
				? inlineFormulaObjects->create(
					*formula,
					*textStyle,
					*st,
					formulas)
				: std::unique_ptr<Ui::Text::CustomEmoji>();
		} break;
		case InlineTextObjectKind::IvImage: {
			const auto image = std::get_if<InlineTextObjectIvImageData>(
				&parsed->data);
			if (!image) {
				return std::unique_ptr<Ui::Text::CustomEmoji>();
			}
			const auto resolved = mediaRuntime
				? mediaRuntime->resolveInlineImage(
					image->documentId,
					QSize(image->width, image->height))
				: nullptr;
			return std::make_unique<InlineIvImageObject>(
				image->replacementText,
				image->width,
				image->height,
				std::move(resolved),
				context.repaint,
				repaintRect);
		}
		}
		return std::unique_ptr<Ui::Text::CustomEmoji>();
	};
	leaf->setMarkedText(
		textStyle,
		text,
		rtl ? kIvMarkedTextOptionsRtl : kIvMarkedTextOptions,
		context);
	SetTextLeafSpoilerLinkFilter(leaf, std::move(spoilerLinkFilter));
}

} // namespace Iv::Markdown
