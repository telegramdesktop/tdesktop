/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_math_box.h"

#include "base/random.h"
#include "base/weak_qptr.h"
#include "iv/markdown/iv_markdown_microtex.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"

#include "styles/palette.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"

#include <array>

namespace Iv::Editor {
namespace {

const auto kFormulaSamples = std::array{
	u"e^{i\\pi}=-1"_q,
	u"x^n+y^n=z^n"_q,
	u"\\sin^2\\alpha+\\cos^2\\alpha=1"_q,
	u"x_{1,2}=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"_q,
};

class MathPreview final : public Ui::RpWidget {
public:
	MathPreview(QWidget *parent);

	void setSource(QString source);
	void setColor(const style::color &color);
	void setCardWidth(int cardWidth);

	rpl::producer<int> desiredHeightValue() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void rerender();
	void updateDesiredHeight();
	void relayout();

	QString _source;
	style::color _color = st::ivFormulaPreviewFg;
	QImage _image;
	QSize _logicalSize;
	int _cardWidth = 0;
	rpl::variable<int> _desiredHeight = 0;

};

MathPreview::MathPreview(QWidget *parent) : RpWidget(parent) {
	_desiredHeight = st::ivFormulaPreviewMinHeight;
}

void MathPreview::setSource(QString source) {
	source = source.trimmed();
	source.replace('\r', ' ');
	source.replace('\n', ' ');
	if (source == _source) {
		return;
	}
	_source = source;
	rerender();
}

void MathPreview::setColor(const style::color &color) {
	if (_color == color) {
		return;
	}
	_color = color;
	rerender();
}

void MathPreview::setCardWidth(int cardWidth) {
	if (_cardWidth == cardWidth) {
		return;
	}
	_cardWidth = cardWidth;
	relayout();
}

rpl::producer<int> MathPreview::desiredHeightValue() const {
	return _desiredHeight.value();
}

void MathPreview::rerender() {
	if (_source.isEmpty()) {
		_image = QImage();
		_logicalSize = QSize();
		updateDesiredHeight();
		relayout();
		update();
		return;
	}
	const auto ratio = std::max(style::DevicePixelRatio(), 1);
	const auto &math = st::defaultMarkdownDisplayMath;
	auto rendered = Markdown::RenderWithMicrotex({
		.trimmedTex = _source,
		.kind = Markdown::MathKind::Display,
		.textSize = math.textSize,
		.renderWidthCap = math.maxRenderWidth,
		.renderHeightCap = math.maxRenderHeight,
		.devicePixelRatio = ratio,
	});
	if (!rendered.measured.success || rendered.image.isNull()) {
		_image = QImage();
		_logicalSize = QSize();
		updateDesiredHeight();
		relayout();
		update();
		return;
	}
	const auto &white = rendered.image;
	auto colorized = QImage(
		white.size(),
		QImage::Format_ARGB32_Premultiplied);
	style::colorizeImage(
		white,
		_color->c,
		&colorized,
		QRect(),
		QPoint(),
		true);
	colorized.setDevicePixelRatio(white.devicePixelRatio());
	_image = std::move(colorized);
	_logicalSize = rendered.measured.logicalSize;
	updateDesiredHeight();
	relayout();
	update();
}

void MathPreview::updateDesiredHeight() {
	const auto padded = _logicalSize.height()
		+ st::ivFormulaPreviewPadding.top()
		+ st::ivFormulaPreviewPadding.bottom();
	_desiredHeight = std::max(st::ivFormulaPreviewMinHeight, padded);
}

void MathPreview::relayout() {
	const auto &padding = st::ivFormulaPreviewPadding;
	const auto width = std::max(
		_cardWidth,
		_logicalSize.width() + padding.left() + padding.right());
	resize(width, _desiredHeight.current());
}

void MathPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::ivFormulaPreviewBg);
	const auto radius = st::ivFormulaPreviewRadius;
	p.drawRoundedRect(rect(), radius, radius);
	if (_image.isNull()) {
		return;
	}
	const auto x = (width() - _logicalSize.width()) / 2;
	const auto y = (height() - _logicalSize.height()) / 2;
	p.drawImage(QPoint(x, y), _image);
}

} // namespace

void EditMathBox(
		not_null<Ui::GenericBox*> box,
		QString startSource,
		bool editingExisting,
		std::optional<bool> separateLine,
		Fn<void(QString, bool)> callback,
		Fn<void(bool)> setExternalInteractionActive,
		Fn<void()> restoreFocus) {
	Expects(callback != nullptr);
	Expects(setExternalInteractionActive != nullptr);

	setExternalInteractionActive(true);
	box->boxClosing() | rpl::on_next([=] {
		setExternalInteractionActive(false);
		if (restoreFocus) {
			restoreFocus();
		}
	}, box->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_formatting_math_source_label(),
			st::ivFormulaSectionTitle),
		st::ivFormulaPreviewLabelMargin);
	const auto source = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::ivFormulaSourceField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_formatting_math_source_placeholder(),
			startSource),
		st::markdownLinkFieldPadding);
	source->setInputMethodHints(Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	source->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	source->setMinHeight(source->st().heightMin);
	const auto separateLineField = separateLine
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_formatting_math_separate_line(tr::now),
				*separateLine,
				st::defaultBoxCheckbox),
			st::markdownMathCheckboxMargin)
		: nullptr;
	auto checkboxHeight = separateLineField
		? separateLineField->heightValue()
		: rpl::single(0);
	rpl::combine(
		source->topValue(),
		box->getDelegate()->contentHeightMaxValue(),
		std::move(checkboxHeight)
	) | rpl::on_next([=](int top, int contentHeight, int checkboxHeight) {
		const auto checkboxBlock = separateLineField
			? (checkboxHeight
				+ st::markdownMathCheckboxMargin.top()
				+ st::markdownMathCheckboxMargin.bottom())
			: 0;
		source->setMaxHeight(std::max(
			source->st().heightMin,
			std::min(
				st::markdownMathFieldMaxHeight,
				contentHeight
					- top
					- st::markdownLinkFieldPadding.bottom()
					- checkboxBlock)));
	}, source->lifetime());

	const auto submit = [=] {
		auto sourceText = source->getLastText().trimmed();
		sourceText.replace('\r', ' ');
		sourceText.replace('\n', ' ');
		if (sourceText.isEmpty()) {
			source->showError();
			return;
		}
		const auto weak = base::make_weak(box);
		callback(
			sourceText,
			separateLineField && separateLineField->checked());
		if (weak) {
			box->closeBox();
		}
	};
	source->submits(
	) | rpl::on_next(submit, source->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_formatting_math_result_label(),
			st::ivFormulaSectionTitle),
		st::ivFormulaPreviewLabelMargin);
	const auto host = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::ivFormulaPreviewMargin);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		host,
		st::ivFormulaPreviewScroll,
		true);
	const auto preview = scroll->setOwnedWidget(
		object_ptr<MathPreview>(scroll));
	host->sizeValue(
	) | rpl::on_next([=](QSize size) {
		scroll->setGeometry(QRect(QPoint(), size));
	}, host->lifetime());
	const auto cardWidth = st::boxWidth
		- st::ivFormulaPreviewMargin.left()
		- st::ivFormulaPreviewMargin.right();
	preview->setCardWidth(cardWidth);
	preview->desiredHeightValue(
	) | rpl::on_next([=](int desiredHeight) {
		host->resize(cardWidth, desiredHeight);
	}, host->lifetime());
	preview->setSource(startSource);
	const auto applyRandomSample = [=] {
		const auto &sample = kFormulaSamples[
			base::RandomIndex(int(kFormulaSamples.size()))];
		source->setPlaceholder(rpl::single(sample));
		preview->setColor(st::windowSubTextFg);
		preview->setSource(sample);
	};
	const auto wasEmpty = box->lifetime().make_state<bool>(
		startSource.isEmpty());
	if (startSource.isEmpty()) {
		applyRandomSample();
	}
	source->changes(
	) | rpl::on_next([=] {
		const auto text = source->getLastText();
		if (text.isEmpty()) {
			if (!*wasEmpty) {
				*wasEmpty = true;
				applyRandomSample();
			}
		} else {
			*wasEmpty = false;
			preview->setColor(st::ivFormulaPreviewFg);
			preview->setSource(text);
		}
	}, source->lifetime());

	box->setTitle(editingExisting
		? tr::lng_formatting_math_edit_title()
		: tr::lng_formatting_math_create_title());
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	box->verticalLayout()->resizeToWidth(st::boxWidth);
	box->verticalLayout()->moveToLeft(0, 0);
	box->setWidth(st::boxWidth);

	box->setFocusCallback([=] {
		if (!startSource.isEmpty()) {
			source->selectAll();
		}
		source->setFocusFast();
	});
}

} // namespace Iv::Editor
