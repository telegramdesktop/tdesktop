/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_text.h"
#include "api/api_bot.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_button_row.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/effects/animation_value.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "ui/dynamic_image.h"
#include "ui/emoji_config.h"
#include "ui/integration.h"
#include "ui/painter.h"

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

constexpr auto kInlineButtonLabelProbeStart = 32;
constexpr auto kInlineButtonLabelSpanCutsMax
	= 2 * kInlineButtonLabelProbeStart;

using ButtonColor = HistoryMessageMarkupButton::Color;

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
	case PreparedLinkKind::ToggleBlockquote:
	case PreparedLinkKind::RichPageButton:
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
	case PreparedLinkKind::ToggleBlockquote:
	case PreparedLinkKind::RichPageButton:
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

class InlineButtonPlainEmoji final : public Ui::Text::CustomEmoji {
public:
	InlineButtonPlainEmoji(EmojiPtr emoji, int size);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const EmojiPtr _emoji = nullptr;
	QImage _frame;
	int _size = 1;

};

class InlineButtonScaledEmoji final : public Ui::Text::CustomEmoji {
public:
	InlineButtonScaledEmoji(
		std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
		int size);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	QImage _frame;
	int _size = 1;

};

class InlineButtonObject final : public Ui::Text::CustomEmoji {
public:
	InlineButtonObject(
		const InlineTextObjectButtonData &data,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const Ui::Text::MarkedContext &context,
		std::shared_ptr<InlineButtonPaintState> paintState,
		int widthCap);

	int width() override;
	QString entityData() override;
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
		const style::TextStyle &textStyle) override;
	QString replacementText() override;
	EntitiesInText replacementEntities() override;
	Ui::Text::CustomEmojiSemantics semantics() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	[[nodiscard]] const style::Markdown &resolvedStyle() const;
	[[nodiscard]] bool labelWouldOverflowIcon(
		const style::Markdown &st) const;
	void paintContent(
		QPainter &p,
		QPoint position,
		QColor color,
		const style::Markdown &st,
		const Context &context) const;

	const QString _entityData;
	const QString _replacementText;
	const EntitiesInText _labelEntities;
	const QByteArray _loadingKey;
	const style::Markdown *_st = nullptr;
	const style::icon *_icon = nullptr;
	const std::shared_ptr<InlineButtonPaintState> _paintState;
	Ui::Text::String _label;
	Ui::Text::CustomEmojiVerticalMetrics _vertical;
	int _width = 1;
	int _height = 1;
	int _labelWidth = 0;
	int _labelLeft = 0;
	int _labelTop = 0;
	int _iconLeft = 0;
	int _lineTopSkip = 0;
	int _lineHeight = 0;
	ButtonColor _color = ButtonColor::Normal;
	bool _disabled = false;
	bool _labelHasParagraphBreak = false;

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

[[nodiscard]] Ui::Text::CustomEmojiVerticalMetrics CenteredVerticalMetrics(
		const style::TextStyle &textStyle,
		int height) {
	const auto top = std::max((TextLineHeight(textStyle) - height) / 2, 0);
	const auto ascent = TextLineAscent(textStyle) - top;
	return {
		.ascent = ascent,
		.descent = height - ascent,
	};
}

[[nodiscard]] int InlineButtonPillHeight(
		const style::TextStyle &textStyle,
		const style::MarkdownInlineButton &st) {
	return std::min(
		textStyle.font->height,
		st.labelStyle.font->height + 2 * st.verticalPadding);
}

[[nodiscard]] int InlineButtonEmojiSize(
		const style::TextStyle &textStyle,
		const style::MarkdownInlineButton &st) {
	return std::max(
		InlineButtonPillHeight(textStyle, st) - 2 * st.verticalPadding,
		1);
}

[[nodiscard]] int InlineButtonLabelWidthCap(
		const style::MarkdownInlineButton &st,
		int buttonWidthCap,
		int trailing) {
	return std::max(buttonWidthCap - st.padding - trailing, 0);
}

[[nodiscard]] int InlineButtonWidthCap(
		const style::MarkdownInlineButton &st,
		const std::shared_ptr<InlineButtonPaintState> &paintState,
		int bandWidthCap) {
	const auto published = (bandWidthCap > 0)
		? bandWidthCap
		: (paintState ? paintState->widthCap : 0);
	return (published > 0) ? std::min(st.maxWidth, published) : st.maxWidth;
}

[[nodiscard]] RichButtonPillColors ResolveInlineButtonColors(
		ButtonColor color,
		const style::Markdown &st) {
	const auto &inlineSt = st.inlineButton;
	const auto tint = [&](const style::color &fg) {
		return RichButtonPillColors{
			.bg = anim::with_alpha(fg->c, inlineSt.tintBgOpacity),
			.ripple = anim::with_alpha(
				fg->c,
				st.buttonRow.tintRippleOpacity),
			.fg = fg->c,
		};
	};
	switch (color) {
	case ButtonColor::Primary:
		return PrimaryPillColors(
			st,
			inlineSt.primaryBg->c,
			st.buttonRow.primaryRipple->c);
	case ButtonColor::Success:
		return tint(inlineSt.successFg);
	case ButtonColor::Danger:
		return tint(inlineSt.dangerFg);
	}
	return tint(inlineSt.defaultFg);
}

[[nodiscard]] std::optional<InlineTextObjectButtonData> InlineButtonDataFor(
		QStringView data) {
	const auto parsed = ParseInlineTextObjectEntity(data);
	if (!parsed || parsed->kind != InlineTextObjectKind::Button) {
		return std::nullopt;
	}
	const auto button = std::get_if<InlineTextObjectButtonData>(&parsed->data);
	return button
		? std::make_optional(*button)
		: std::nullopt;
}

[[nodiscard]] auto ActionableInlineButtonDataFor(QStringView data)
-> std::optional<InlineTextObjectButtonData> {
	auto result = InlineButtonDataFor(data);
	if (result
		&& (result->type == HistoryMessageMarkupButton::Type::Disabled)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] bool InlineButtonActionable(QStringView data) {
	return ActionableInlineButtonDataFor(data).has_value();
}

[[nodiscard]] HistoryMessageMarkupButton InlineButtonRecord(
		const InlineTextObjectButtonData &button) {
	auto result = HistoryMessageMarkupButton(
		button.type,
		button.label.text,
		HistoryMessageMarkupButton::Visual{ .color = button.color },
		button.data,
		QString(),
		button.buttonId);
	result.peerTypes = button.peerTypes;
	return result;
}

void ActivateInlineButton(QStringView data, ClickContext context) {
	const auto button = ActionableInlineButtonDataFor(data);
	if (!button) {
		return;
	}
	const auto record = InlineButtonRecord(*button);
	const auto my = context.other.value<ClickHandlerContext>();
	Api::ActivateRichPageBotButton(my, record);
}

class InlineButtonClickHandler final : public ClickHandler {
public:
	explicit InlineButtonClickHandler(QString data);

	void onClick(ClickContext context) const override;

	QString tooltip() const override;

private:
	const QString _data;

};

InlineButtonClickHandler::InlineButtonClickHandler(QString data)
: _data(std::move(data)) {
}

void InlineButtonClickHandler::onClick(ClickContext context) const {
	ActivateInlineButton(_data, std::move(context));
}

QString InlineButtonClickHandler::tooltip() const {
	return InlineButtonTooltip(_data);
}

[[nodiscard]] QString InlineButtonPlainEmojiPrefix() {
	return u"iv-markdown:inline-button-emoji:"_q;
}

// MarkInlineButtonPlainEmoji marks the plain emoji of a label, and
// InlineButtonLabelEmojiCrosses answers whether a planned removal can change
// what it marks; the guard is sound only while it visits exactly the positions
// the marking walk lands on, so the two share one traversal rather than two
// look-alike loops that can drift. Both details below are load-bearing: `ch`
// moves past the whole match before the callback runs, so a callback that
// declines the match still leaves the walk past it, and a miss advances by one
// code unit rather than by one code point.
template <typename Callback>
void EnumerateInlineButtonLabelEmoji(
		const QString &text,
		Callback &&callback) {
	const auto start = text.constData();
	const auto finish = start + text.size();
	auto ch = start;
	while (ch != finish) {
		auto length = 0;
		const auto emoji = Ui::Emoji::Find(ch, finish, &length);
		if (!emoji) {
			++ch;
			continue;
		}
		const auto from = int(ch - start);
		ch += length;
		if (!callback(from, length, emoji)) {
			return;
		}
	}
}

[[nodiscard]] TextWithEntities MarkInlineButtonPlainEmoji(
		TextWithEntities label) {
	const auto till = [](const EntityInText &entity) {
		return entity.offset() + entity.length();
	};
	auto i = label.entities.begin();
	EnumerateInlineButtonLabelEmoji(label.text, [&](
			int from,
			int length,
			EmojiPtr emoji) {
		while (i != label.entities.end() && till(*i) <= from) {
			++i;
		}
		if (i != label.entities.end() && i->offset() < from + length) {
			return true;
		}
		i = label.entities.insert(i, EntityInText(
			EntityType::CustomEmoji,
			from,
			length,
			InlineButtonPlainEmojiPrefix() + emoji->text()));
		return true;
	});
	return label;
}

struct InlineButtonLabelCodePoint {
	uint ucs4 = 0;
	int length = 1;
};

// InlineButtonLabelStrongCut and InlineButtonLabelSpanKeep have to agree on
// which character is the first strong one: the scan keeps a span's prefix
// through its own first strong character precisely so that StringDirection
// and the strong cut still find that same character over the shortened label.
// Two look-alike decode loops can drift apart; one shared pair cannot.
[[nodiscard]] InlineButtonLabelCodePoint InlineButtonLabelCodePointAt(
		QStringView text,
		int i) {
	auto result = InlineButtonLabelCodePoint{
		.ucs4 = uint(text.at(i).unicode()),
	};
	if (QChar::isHighSurrogate(result.ucs4) && (i + 1 < int(text.size()))) {
		const auto low = text.at(i + 1).unicode();
		if (QChar::isLowSurrogate(low)) {
			result.ucs4 = QChar::surrogateToUcs4(result.ucs4, low);
			result.length = 2;
		}
	}
	return result;
}

[[nodiscard]] Qt::LayoutDirection InlineButtonLabelStrongDirection(
		uint ucs4) {
	const auto direction = QChar::direction(ucs4);
	return (direction == QChar::DirL)
		? Qt::LeftToRight
		: (direction == QChar::DirR || direction == QChar::DirAL)
		? Qt::RightToLeft
		: Qt::LayoutDirectionAuto;
}

[[nodiscard]] bool InlineButtonLabelCharKept(
		QChar ch,
		InlineButtonLabelCodePoint point) {
	return !Ui::Text::IsBad(ch)
		&& !Ui::Text::IsDiacritic(ch)
		&& ((point.length == 2)
			? (point.ucs4 < 0xE0000)
			: !QChar::isSurrogate(point.ucs4));
}

[[nodiscard]] int InlineButtonLabelStrongCut(
		const TextWithEntities &label,
		int cut) {
	// A Ui::Text::String takes its first paragraph's direction from the first
	// character of strong direction in that paragraph and falls back to the
	// interface direction when there is none, and a line whose paragraph
	// direction opposes the alignment is drawn shifted by the width it did
	// not use. A prefix that dropped the label's only strong character would
	// therefore draw the same glyphs at a different x — but only when that
	// character resolves to the direction the fallback would not have picked.
	const auto &text = label.text;
	const auto size = int(text.size());
	auto i = 0;
	while (i < size) {
		const auto ch = text.at(i);
		if (ch.unicode() == QChar::LineFeed) {
			return cut;
		}
		const auto point = InlineButtonLabelCodePointAt(text, i);
		const auto strong = InlineButtonLabelStrongDirection(point.ucs4);
		if (strong != Qt::LayoutDirectionAuto) {
			return (strong == style::LayoutDirection())
				? cut
				: std::max(cut, i + point.length);
		}
		i += point.length;
	}
	return cut;
}

[[nodiscard]] int InlineButtonLabelCut(
		const TextWithEntities &label,
		int position) {
	const auto size = int(label.text.size());
	if (position >= size) {
		return size;
	}
	auto result = InlineButtonLabelStrongCut(label, position);
	// Ui::Text::Mid truncates an entity that straddles the range end instead
	// of dropping it, so a CustomEmoji entity that starts inside the drawn
	// region and ends past the cut would be rewritten and would change
	// visible glyphs; the cut moves past its end instead. The list is not
	// ordered by offset and this one forward pass does not need it to be.
	// What it needs is that no two entities partially overlap, and the
	// producers nest them: AddEntity in iv_rich_page.cpp pushes a container
	// after the entities it encloses, so an entry with an earlier offset
	// than one before it is one that contains it and reaches at least as
	// far. Moving the cut can therefore never expose an entity the pass
	// already walked past.
	for (const auto &entity : label.entities) {
		const auto till = entity.offset() + entity.length();
		if ((entity.offset() < result) && (till > result)) {
			result = till;
		}
	}
	return std::min(result, size);
}

[[nodiscard]] Ui::Text::MarkedContext InlineButtonLabelContext(
		const Ui::Text::MarkedContext &context,
		int emojiSize) {
	auto result = context;
	result.customEmojiFactory = [
		parent = context.customEmojiFactory,
		emojiSize
	](
			QStringView data,
			const Ui::Text::MarkedContext &context
	) -> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto prefix = InlineButtonPlainEmojiPrefix();
		if (data.startsWith(prefix)) {
			const auto text = data.mid(prefix.size());
			const auto emoji = Ui::Emoji::Find(text);
			return emoji
				? std::make_unique<InlineButtonPlainEmoji>(emoji, emojiSize)
				: nullptr;
		}
		return Ui::Text::MakeWrappedEmoji<InlineButtonScaledEmoji>(
			parent ? parent(data, context) : nullptr,
			emojiSize);
	};
	return result;
}

[[nodiscard]] Ui::Text::String MakeInlineButtonLabel(
		const TextWithEntities &label,
		const style::TextStyle &labelStyle,
		const Ui::Text::MarkedContext &context) {
	auto result = Ui::Text::String();
	result.setMarkedText(
		labelStyle,
		MarkInlineButtonPlainEmoji(label),
		kIvMarkedTextOptions,
		context);
	return result;
}

[[nodiscard]] int InlineButtonLabelSpanKeep(
		QStringView span,
		bool trimmableEnd) {
	// The text engine draws a CustomEmoji entity as one object of one width
	// whatever it covers, and BidiAlgorithm::infoAt reads every covered
	// character as an object replacement character, so the covered text
	// contributes no glyph, no advance and no direction. It does decide two
	// other things: BlockParser::createBlock emits the block only if it kept
	// at least one of those characters, and StringDirection reads them raw,
	// so the first one of strong direction still resolves the paragraph
	// direction. Keep the span's own text through both, and keep all of it
	// when a character of strong direction is one the parser may skip.
	// A kept prefix may end on a trimmable parsed character only when the
	// caller has proven the parser's trailing trim cannot reach it — a
	// character the parser keeps and does not trim survives after the span
	// in the spliced label, and BlockParser::trimSourceRange moves only the
	// two ends of the string it is handed — which is what `trimmableEnd`
	// asserts. A span with no parsed character at all still yields 0
	// whatever the caller proved: keeping a character the parser skips
	// would invent an object where none exists.
	const auto size = int(span.size());
	auto keepable = 0;
	auto trimmable = 0;
	auto i = 0;
	while (i != size) {
		const auto ch = span.at(i);
		const auto point = InlineButtonLabelCodePointAt(span, i);
		const auto kept = InlineButtonLabelCharKept(ch, point);
		const auto strong = InlineButtonLabelStrongDirection(point.ucs4);
		if (strong != Qt::LayoutDirectionAuto) {
			return kept ? (i + point.length) : 0;
		} else if (!keepable && kept && !Ui::Text::IsTrimmed(ch)) {
			keepable = i + point.length;
		} else if (!trimmable && kept) {
			trimmable = i + point.length;
		}
		i += point.length;
	}
	return keepable
		? keepable
		: trimmableEnd
		? trimmable
		: 0;
}

[[nodiscard]] bool InlineButtonLabelEmojiCrosses(
		const QString &text,
		const std::vector<int> &borders) {
	// MarkInlineButtonPlainEmoji walks the label left to right and asks
	// Ui::Emoji::Find at every position it lands on, so removing text can
	// change what it marks only by carrying that walk over one of the
	// positions the removal joins: a match starting before such a position
	// and ending after it hides the emoji behind it from the walk, or stops
	// hiding one. Running the very same walk here — over the label before the
	// removal against both ends of every planned cut, and over the spliced
	// text against every seam — answers that at every position the real walk
	// can reach, so no match crosses undetected, including one that starts
	// before the span. `borders` is ascending.
	auto border = borders.begin();
	auto crosses = false;
	EnumerateInlineButtonLabelEmoji(text, [&](
			int from,
			int length,
			EmojiPtr) {
		while ((border != borders.end()) && (*border <= from)) {
			++border;
		}
		if ((border != borders.end()) && (*border < from + length)) {
			crosses = true;
			return false;
		}
		return true;
	});
	return crosses;
}

[[nodiscard]] bool InlineButtonLabelSpanCandidate(
		const EntityInText &entity) {
	return (entity.type() == EntityType::CustomEmoji)
		&& (entity.length() > kInlineButtonLabelProbeStart)
		&& !entity.data().isEmpty();
}

[[nodiscard]] std::vector<int> InlineButtonLabelEntityOrder(
		const EntitiesInText &entities) {
	auto result = std::vector<int>(entities.size());
	for (auto i = 0, count = int(result.size()); i != count; ++i) {
		result[i] = i;
	}
	ranges::sort(result, ranges::less(), [&](int i) {
		return entities[i].offset();
	});
	return result;
}

[[nodiscard]] std::vector<bool> InlineButtonLabelIntersected(
		const EntitiesInText &entities,
		const std::vector<int> &order) {
	// A candidate may be shortened only when no other entity overlaps it, and
	// the list is not ordered by offset (see InlineButtonLabelCut), so the
	// test cannot look at neighbours only. Asking it one candidate at a time
	// is quadratic in a label the sender sizes; two sweeps in offset order
	// answer it for the whole list at once. An entity is overlapped when one
	// before it in that order reaches past its start, or one after it starts
	// before its end.
	const auto count = int(order.size());
	auto result = std::vector<bool>(count, false);
	auto reached = std::numeric_limits<int>::min();
	for (auto i = 0; i != count; ++i) {
		const auto &entity = entities[order[i]];
		if (entity.offset() < reached) {
			result[order[i]] = true;
		}
		reached = std::max(reached, entity.offset() + entity.length());
	}
	auto nearest = std::numeric_limits<int>::max();
	for (auto i = count; i != 0;) {
		const auto &entity = entities[order[--i]];
		if (nearest < entity.offset() + entity.length()) {
			result[order[i]] = true;
		}
		nearest = std::min(nearest, entity.offset());
	}
	return result;
}

[[nodiscard]] int InlineButtonLabelLastSolid(
		const TextWithEntities &label,
		const std::vector<int> &order) {
	// A kept trimmable span end is safe only while the parser's trailing
	// trim cannot reach it, and trimSourceRange moves only the two ends of
	// the string it is handed — so the end is safe exactly when a character
	// the parser keeps and does not trim survives after the span in the
	// spliced label. The witness is searched only in text no planned cut
	// can remove: outside every candidate span, because a later candidate's
	// own cut may drop any character inside it. Text between spans and in
	// non-candidate entities survives the splice verbatim, which is what
	// makes the answer valid for the spliced label while it is computed
	// over the original one. `order` is the entity indices in offset order.
	const auto text = QStringView(label.text);
	const auto size = int(text.size());
	auto entity = order.begin();
	const auto end = order.end();
	auto skipTill = 0;
	auto last = -1;
	auto i = 0;
	while (i < size) {
		while ((entity != end)
			&& (label.entities[*entity].offset() <= i)) {
			const auto &excluded = label.entities[*entity];
			if (InlineButtonLabelSpanCandidate(excluded)) {
				skipTill = std::max(
					skipTill,
					excluded.offset() + excluded.length());
			}
			++entity;
		}
		if (i < skipTill) {
			i = skipTill;
			continue;
		}
		const auto ch = text.at(i);
		const auto point = InlineButtonLabelCodePointAt(text, i);
		if (InlineButtonLabelCharKept(ch, point)
			&& !Ui::Text::IsTrimmed(ch)) {
			last = i;
		}
		i += point.length;
	}
	return last;
}

struct InlineButtonLabelSpanCut {
	int index = 0;
	int offset = 0;
	int keep = 0;
	int length = 0;
};

[[nodiscard]] TextWithEntities ShortenInlineButtonLabelSpans(
		TextWithEntities label,
		const Ui::Text::MarkedContext &context) {
	// This walk runs for every label, before any search, so the candidate
	// count is what keeps an ordinary one out of it. A span of at most
	// kInlineButtonLabelProbeStart characters is never a candidate: it cannot
	// be what leaves the prefix search unbounded, because an accepted prefix
	// carries at most `available / emojiSize` objects, so a label whose every
	// span is that short is bounded by width exactly as in the shipped build.
	// The rewrite is all or nothing over the candidates themselves and not
	// only over how many there are: the whole label is declined as soon as
	// one candidate cannot be brought under kInlineButtonLabelProbeStart
	// characters, and declined again past kInlineButtonLabelSpanCutsMax of
	// them. A span left whole absorbs the search that the shortened ones
	// before it no longer trip — every character removed ahead of it pulls it
	// under a probe that used to land in front of it, and the straddle snap
	// then jumps to its end. So either every CustomEmoji entity carrying data
	// in the label the search is handed covers at most
	// kInlineButtonLabelProbeStart characters, or that label reaches the
	// search exactly as the shipped build hands it and no input is made worse
	// than it is today. The count cap is 2 * kInlineButtonLabelProbeStart
	// because a shortened span draws as one object, so a prefix holding k of
	// them measures at least k * emojiSize, kInlineButtonLabelProbeStart *
	// emojiSize is already more than the pill can ever draw, and probe
	// lengths start at kInlineButtonLabelProbeStart and double while the
	// search accepts the second prefix wider than the cap, so a prefix made
	// of shortened spans is accepted at twice that many of them at the
	// latest. That inequality is checked per scale rather than guaranteed by
	// construction — the cap is a .style pixel while emojiSize follows the
	// label font's height, so they do not scale together: 448 > 408 at 100 %
	// and 640 > 612 at 150 %. Where it does not hold, under a small markdown
	// text style, the only effect is that a label is declined which the
	// search could have bounded, which is the shipped behaviour. A span
	// whose every parsed character is trimmable is admitted with its first
	// parsed character kept only when a witness — a character the parser
	// keeps and does not trim, in text no planned cut can remove — survives
	// after the span in the spliced label (see InlineButtonLabelLastSolid).
	const auto &entities = label.entities;
	const auto candidates = ranges::count_if(
		entities,
		InlineButtonLabelSpanCandidate);
	if (!candidates || (candidates > kInlineButtonLabelSpanCutsMax)) {
		return label;
	}
	const auto order = InlineButtonLabelEntityOrder(entities);
	const auto intersected = InlineButtonLabelIntersected(entities, order);
	const auto lastSolid = InlineButtonLabelLastSolid(label, order);
	auto cuts = std::vector<InlineButtonLabelSpanCut>();
	for (const auto i : order) {
		const auto &entity = entities[i];
		if (!InlineButtonLabelSpanCandidate(entity)) {
			continue;
		} else if (intersected[i]) {
			return label;
		}
		const auto offset = entity.offset();
		const auto length = entity.length();
		const auto keep = InlineButtonLabelSpanKeep(
			QStringView(label.text).mid(offset, length),
			lastSolid >= offset + length);
		if (!keep
			|| (keep > kInlineButtonLabelProbeStart)
			|| !Ui::Text::MakeCustomEmoji(entity.data(), context)) {
			return label;
		}
		cuts.push_back({
			.index = i,
			.offset = offset,
			.keep = keep,
			.length = length,
		});
	}
	auto borders = std::vector<int>();
	borders.reserve(2 * cuts.size());
	for (const auto &cut : cuts) {
		borders.push_back(cut.offset + cut.keep);
		borders.push_back(cut.offset + cut.length);
	}
	if (InlineButtonLabelEmojiCrosses(label.text, borders)) {
		return label;
	}
	auto text = QString();
	auto seams = std::vector<int>();
	text.reserve(label.text.size());
	seams.reserve(cuts.size());
	auto copied = 0;
	for (const auto &cut : cuts) {
		const auto kept = cut.offset + cut.keep;
		text.append(QStringView(label.text).mid(copied, kept - copied));
		seams.push_back(int(text.size()));
		copied = cut.offset + cut.length;
	}
	text.append(QStringView(label.text).mid(copied));
	if (InlineButtonLabelEmojiCrosses(text, seams)) {
		return label;
	}
	auto removed = 0;
	auto cut = cuts.begin();
	for (const auto i : order) {
		auto &entity = label.entities[i];
		while ((cut != cuts.end())
			&& (cut->offset + cut->length <= entity.offset())) {
			removed += cut->length - cut->keep;
			++cut;
		}
		if (removed) {
			entity.shiftRight(-removed);
		}
		if ((cut != cuts.end()) && (cut->index == i)) {
			entity.shrinkFromRight(cut->length - cut->keep);
		}
	}
	label.text = std::move(text);
	return label;
}

[[nodiscard]] Ui::Text::String MakeBoundedInlineButtonLabel(
		const TextWithEntities &label,
		const style::TextStyle &labelStyle,
		const Ui::Text::MarkedContext &context,
		int emojiSize,
		int widthCap) {
	// The renderer lays the whole string out on every paint even though the
	// pill can only ever draw `available` pixels of it, so the label is built
	// from a prefix instead. Probe lengths double until one measures wider
	// than the pill can draw, and the prefix after that one is accepted, so
	// everything the renderer reaches — the line break, the elision cut and
	// the ellipsis — sits inside the half the shorter probe already covered,
	// with more than a pill-width of identical content behind it. A prefix
	// alone cannot bound a custom-emoji span, which draws as one object of
	// one width however many characters it covers: the search has to step
	// past the whole span before its width test can trip, so the span is what
	// makes the accepted prefix long. The spans are therefore shortened
	// first, once, for every label. That walk asks Ui::Text::MakeCustomEmoji
	// at most once per candidate span, and that question registers a session
	// instance and can post messages.getCustomEmojiDocuments, so the walk's
	// candidate cap is what bounds the fetching — and the accepted prefix may
	// still drop a span the walk resolved. The one input this does not
	// render like the shipped build, on either exit, is a span running into
	// the parser's 32 768-character cap: the text behind it that the capped
	// parse never reached becomes visible, and the pill grows to fit it.
	// A probe cut snapped to a shortened span's end can leave the span's
	// kept trimmable character last in the probe's string, where the
	// parser's trailing trim eats it and drops the object from that probe.
	// That only lowers the probe's measured width, so the search runs a
	// probe longer, never shorter, and the accepted prefix stays
	// pixel-identical to the whole-label build: the dropped object lies
	// past everything the renderer reaches, behind the first half that
	// already measured wider than the pill can draw.
	const auto resolved = ResolveRichButtonLabelDates(
		label,
		context.formattedDateFactory);
	const auto nested = InlineButtonLabelContext(context, emojiSize);
	const auto shortened = ShortenInlineButtonLabelSpans(resolved, nested);
	const auto available = std::max(widthCap, 1);
	const auto size = int(shortened.text.size());
	auto exceeded = false;
	auto length = kInlineButtonLabelProbeStart;
	while (true) {
		const auto cut = InlineButtonLabelCut(shortened, length);
		if (cut >= size) {
			return MakeInlineButtonLabel(shortened, labelStyle, nested);
		}
		auto result = MakeInlineButtonLabel(
			Ui::Text::Mid(shortened, 0, cut),
			labelStyle,
			nested);
		if (result.maxWidth() > available) {
			if (exceeded) {
				return result;
			}
			exceeded = true;
		}
		length = 2 * cut;
	}
}

} // namespace

ClickHandlerPtr CreatePreparedLinkHandler(PreparedLink link) {
	// A rich-page button link is deliberately not a PreparedLinkClickHandler:
	// ExtractPreparedLink therefore refuses it, a hit carries only
	// state.link, and both hosts activate that through the generic
	// ActivateClickHandler arm the inline pill already uses. That is why this
	// one kind gets no arm in the three prepared-link routers.
	if (link.kind == PreparedLinkKind::RichPageButton) {
		return std::make_shared<InlineButtonClickHandler>(
			std::move(link.target));
	}
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

InlineButtonPlainEmoji::InlineButtonPlainEmoji(EmojiPtr emoji, int size)
: _emoji(emoji)
, _size(std::max(size, 1)) {
}

int InlineButtonPlainEmoji::width() {
	return _size;
}

QString InlineButtonPlainEmoji::entityData() {
	return InlineButtonPlainEmojiPrefix() + _emoji->text();
}

auto InlineButtonPlainEmoji::vertical(const style::TextStyle &textStyle)
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	return CenteredVerticalMetrics(textStyle, _size);
}

void InlineButtonPlainEmoji::paint(QPainter &p, const Context &context) {
	if (_frame.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		const auto large = Ui::Emoji::GetSizeLarge();
		_frame = QImage(
			QSize(large, large),
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
		_frame.fill(Qt::transparent);
		{
			auto q = QPainter(&_frame);
			Ui::Emoji::Draw(q, _emoji, large, 0, 0);
		}
		_frame = _frame.scaled(
			QSize(_size, _size) * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	p.drawImage(context.position, _frame);
}

void InlineButtonPlainEmoji::unload() {
	_frame = QImage();
}

bool InlineButtonPlainEmoji::ready() {
	return true;
}

bool InlineButtonPlainEmoji::readyInDefaultState() {
	return true;
}

InlineButtonScaledEmoji::InlineButtonScaledEmoji(
	std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
	int size)
: _wrapped(std::move(wrapped))
, _size(std::max(size, 1)) {
}

int InlineButtonScaledEmoji::width() {
	return _size;
}

QString InlineButtonScaledEmoji::entityData() {
	return _wrapped->entityData();
}

auto InlineButtonScaledEmoji::vertical(const style::TextStyle &textStyle)
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	return CenteredVerticalMetrics(textStyle, _size);
}

void InlineButtonScaledEmoji::paint(QPainter &p, const Context &context) {
	const auto ratio = style::DevicePixelRatio();

	// A normal-size custom emoji advances by st::emojiSize plus twice
	// st::emojiPadding, but it draws AdjustCustomEmojiSize(st::emojiSize)
	// with its top-left exactly at the position it is handed, so its drawn
	// box is not its advance and a frame sized from the advance would wrap
	// the glyph in a transparent border. This wrapper reports both width()
	// and vertical()->height() as _size, so the renderer paints the wrapped
	// object at the advance box's own top-left instead of at the padded
	// position, and rasterizing exactly the drawn box and stretching it
	// across that whole box is what lets neighbouring pieces tile.
	const auto drawn = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
	const auto full = QSize(drawn, drawn) * ratio;
	if (_frame.size() != full) {
		_frame = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);
	{
		auto q = QPainter(&_frame);
		q.translate(-context.position);
		_wrapped->paint(q, context);
	}
	auto hq = PainterHighQualityEnabler(p);
	p.drawImage(QRect(context.position, QSize(_size, _size)), _frame);
}

void InlineButtonScaledEmoji::unload() {
	_frame = QImage();
	_wrapped->unload();
}

bool InlineButtonScaledEmoji::ready() {
	return _wrapped->ready();
}

bool InlineButtonScaledEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

InlineButtonObject::InlineButtonObject(
	const InlineTextObjectButtonData &data,
	const style::TextStyle &textStyle,
	const style::Markdown &st,
	const Ui::Text::MarkedContext &context,
	std::shared_ptr<InlineButtonPaintState> paintState,
	int widthCap)
: _entityData(SerializeInlineTextObjectEntity({
	.kind = InlineTextObjectKind::Button,
	.data = data,
}))
, _replacementText(data.label.text)
, _labelEntities(Ui::Text::Filtered(
	data.label,
	{ EntityType::CustomEmoji }).entities)
, _loadingKey(RichButtonLoadingKey(InlineButtonRecord(data)))
, _st(&st)
, _icon(RichButtonIcon(data.type))
, _paintState(std::move(paintState))
, _label(MakeBoundedInlineButtonLabel(
	data.label,
	st.inlineButton.labelStyle,
	context,
	InlineButtonEmojiSize(textStyle, st.inlineButton),
	InlineButtonLabelWidthCap(
		st.inlineButton,
		st.inlineButton.maxWidth,
		st.inlineButton.padding)))
, _color(data.color)
, _disabled(data.type == HistoryMessageMarkupButton::Type::Disabled)
, _labelHasParagraphBreak(
	ranges::any_of(data.label.text, Ui::Text::IsNewline)) {
	const auto &inlineSt = st.inlineButton;
	const auto padding = inlineSt.padding;
	_height = InlineButtonPillHeight(textStyle, inlineSt);
	const auto inset = _icon
		? std::max((_height - _icon->height()) / 2, 0)
		: 0;
	const auto trailing = _icon
		? (2 * inset + _icon->width())
		: padding;
	const auto buttonWidthCap = InlineButtonWidthCap(
		inlineSt,
		_paintState,
		widthCap);
	_labelWidth = std::min(
		_label.maxWidth(),
		InlineButtonLabelWidthCap(inlineSt, buttonWidthCap, trailing));
	_width = std::max(_height, padding + _labelWidth + trailing);
	_vertical = CenteredVerticalMetrics(textStyle, _height);
	_lineTopSkip = TextLineAscent(textStyle) - _vertical.ascent;
	_lineHeight = TextLineHeight(textStyle);
	_labelLeft = padding;
	_labelTop = std::clamp(
		_vertical.ascent - inlineSt.labelStyle.font->ascent,
		0,
		std::max(_height - inlineSt.labelStyle.font->height, 0));
	_iconLeft = _icon ? (_width - inset - _icon->width()) : 0;
}

int InlineButtonObject::width() {
	return _width;
}

QString InlineButtonObject::entityData() {
	return _entityData;
}

auto InlineButtonObject::vertical(const style::TextStyle &)
-> std::optional<Ui::Text::CustomEmojiVerticalMetrics> {
	return _vertical;
}

QString InlineButtonObject::replacementText() {
	return _replacementText;
}

EntitiesInText InlineButtonObject::replacementEntities() {
	return _labelEntities;
}

Ui::Text::CustomEmojiSemantics InlineButtonObject::semantics() {
	return {
		.isEmoji = false,
		.isRealCustomEmoji = false,
		.exportEntity = false,
		.unloadPersistentAnimation = true,
		.allowCustomEmojiClick = !_disabled,
	};
}

const style::Markdown &InlineButtonObject::resolvedStyle() const {
	return (_paintState && _paintState->st) ? *_paintState->st : *_st;
}

bool InlineButtonObject::labelWouldOverflowIcon(
		const style::Markdown &st) const {
	return _icon
		&& ((_labelWidth < _label.maxWidth()) || _labelHasParagraphBreak)
		&& (_labelWidth < st.inlineButton.labelStyle.font->elidew);
}

void InlineButtonObject::paintContent(
		QPainter &p,
		QPoint position,
		QColor color,
		const style::Markdown &st,
		const Context &context) const {
	if (_icon) {
		_icon->paintInCenter(
			p,
			QRect(
				position.x() + _iconLeft,
				position.y(),
				_icon->width(),
				_height),
			color);
	}
	if (_label.isEmpty() || labelWouldOverflowIcon(st)) {
		return;
	}
	const auto available = std::max(_labelWidth, 1);
	p.setPen(color);
	_label.draw(p, {
		.position = position + QPoint(_labelLeft, _labelTop),
		.availableWidth = available,
		.geometry = Ui::Text::SimpleGeometry(available, 1, 0, false),
		.palette = &st.textPalette,
		.now = context.now,
		.paused = context.paused,
		.elisionLines = 1,
	});
}

void InlineButtonObject::paint(QPainter &p, const Context &context) {
	const auto &markdownSt = resolvedStyle();
	const auto &st = markdownSt.inlineButton;
	const auto position = context.position;
	const auto rect = QRect(position, QSize(_width, _height));
	const auto radius = _height / 2;
	const auto state = _paintState.get();
	const auto lineRect = QRect(
		rect.x(),
		rect.y() - _lineTopSkip,
		_width,
		_lineHeight);
	if (state
		&& state->pressPending
		&& lineRect.contains(state->pressPoint)) {
		state->pressPending = false;
		if (!_disabled) {
			state->rippleRect = rect;
			AddPillRipple(
				&state->ripple,
				&state->rippleSize,
				rect.size(),
				state->pressPoint - rect.topLeft(),
				state->repaint);
		}
	}
	const auto ripple = (state
		&& state->ripple
		&& (state->rippleRect == rect))
		? state->ripple.get()
		: nullptr;
	const auto loading = state
		? RichButtonLoadingActive(state->buttonLoading, _loadingKey)
		: nullptr;
	const auto fillPill = [&](
			QPainter &q,
			const RichButtonPillColors &colors,
			bool eraseRipple) {
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		q.setBrush(colors.bg);
		q.drawRoundedRect(rect, radius, radius);
		if (ripple) {
			const auto mode = q.compositionMode();
			if (eraseRipple) {
				q.setCompositionMode(
					QPainter::CompositionMode_DestinationOut);
			}
			ripple->paint(
				q,
				rect.x(),
				rect.y(),
				rect.x() * 2 + rect.width(),
				&colors.ripple);
			q.setCompositionMode(mode);
		}
	};
	p.save();
	const auto colors = (state && state->bubbleGradient)
		? BubbleGradientPillColors(
			markdownSt,
			st.tintBgOpacity,
			(_color == ButtonColor::Primary))
		: ResolveInlineButtonColors(_color, markdownSt);
	if (colors.punchOut) {
		PaintPunchedOutPill(
			p,
			rect,
			_disabled ? st.disabledPrimaryOpacity : 1.,
			[&](QPainter &q) {
				fillPill(q, colors, colors.punchOut);
				if (loading) {
					PaintRichButtonLoading(q, loading, colors, rect, radius);
				}
			},
			[&](QPainter &q, QColor fg) {
				paintContent(q, position, fg, markdownSt, context);
			});
	} else {
		const auto primary = (_color == ButtonColor::Primary);
		fillPill(p, colors, false);
		if (loading) {
			PaintRichButtonLoading(p, loading, colors, rect, radius);
		}
		if (_disabled) {
			p.setOpacity(p.opacity() * (primary
				? st.disabledPrimaryOpacity
				: st.disabledOpacity));
		}
		paintContent(p, position, colors.fg, markdownSt, context);
	}
	p.restore();
}

void InlineButtonObject::unload() {
	_label.unloadPersistentAnimation();
}

bool InlineButtonObject::ready() {
	return true;
}

bool InlineButtonObject::readyInDefaultState() {
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

bool TextHasInlineButton(const TextWithEntities &text) {
	return ranges::any_of(text.entities, [](const EntityInText &entity) {
		return (entity.type() == EntityType::CustomEmoji)
			&& InlineButtonDataFor(entity.data()).has_value();
	});
}

QString InlineButtonTooltip(QStringView data) {
	const auto button = ActionableInlineButtonDataFor(data);
	return button
		? RichButtonTooltip(button->type, button->data, QString())
		: QString();
}

void SetTextLeaf(
		Ui::Text::String *leaf,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const TextWithEntities &text,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<InlineButtonPaintState> &inlineButtonPaintState,
		int inlineButtonWidthCap,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		int minResizeWidth,
		bool rtl,
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter,
		bool richButtonLabel) {
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
		inlineButtonPaintState,
		inlineButtonWidthCap,
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
		case InlineTextObjectKind::Button: {
			const auto button = std::get_if<InlineTextObjectButtonData>(
				&parsed->data);
			if (!button) {
				return std::unique_ptr<Ui::Text::CustomEmoji>();
			}
			return std::make_unique<InlineButtonObject>(
				*button,
				*textStyle,
				*st,
				context,
				inlineButtonPaintState,
				inlineButtonWidthCap);
		}
		}
		return std::unique_ptr<Ui::Text::CustomEmoji>();
	};
	const auto resolved = richButtonLabel
		? ResolveRichButtonLabelDates(text, context.formattedDateFactory)
		: TextWithEntities();
	leaf->setMarkedText(
		textStyle,
		richButtonLabel ? resolved : text,
		rtl ? kIvMarkedTextOptionsRtl : kIvMarkedTextOptions,
		context);
	SetTextLeafSpoilerLinkFilter(leaf, std::move(spoilerLinkFilter));
	if (inlineButtonPaintState
		&& !inlineButtonPaintState->editMode
		&& ranges::any_of(text.entities, [](const EntityInText &entity) {
			return (entity.type() == EntityType::CustomEmoji)
				&& InlineButtonActionable(entity.data());
		})) {
		leaf->setCustomEmojiClickHandler([
			state = inlineButtonPaintState
		](QStringView data) {
			if (!InlineButtonActionable(data)) {
				return false;
			}
			state->lookedUpButton = data.toString();
			return true;
		}, ActivateInlineButton);
	}
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeInlineButtonObject(
		QStringView data,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const Ui::Text::MarkedContext &context) {
	const auto button = InlineButtonDataFor(data);
	if (!button) {
		return nullptr;
	}
	return std::make_unique<InlineButtonObject>(
		*button,
		textStyle,
		st,
		context,
		nullptr,
		0);
}

} // namespace Iv::Markdown
