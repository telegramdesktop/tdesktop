/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/passcode_strength_meter.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"

#include "styles/style_passcode_strength_meter.h"

namespace Ui {
namespace {

constexpr auto kSegmentCount = int(PasscodeStrengthBand::Strong) + 1;

[[nodiscard]] rpl::producer<QString> AdviceText(
		PasscodeStrengthAdvice advice) {
	switch (advice) {
	case PasscodeStrengthAdvice::TooShort:
		return tr::lng_passcode_strength_advice_short();
	case PasscodeStrengthAdvice::CommonWord:
		return tr::lng_passcode_strength_advice_word();
	case PasscodeStrengthAdvice::LeetWord:
		return tr::lng_passcode_strength_advice_leet();
	case PasscodeStrengthAdvice::Sequence:
		return tr::lng_passcode_strength_advice_sequence();
	case PasscodeStrengthAdvice::Repeat:
		return tr::lng_passcode_strength_advice_repeat();
	case PasscodeStrengthAdvice::Keyboard:
		return tr::lng_passcode_strength_advice_keyboard();
	case PasscodeStrengthAdvice::DateLike:
		return tr::lng_passcode_strength_advice_date();
	case PasscodeStrengthAdvice::AddVariety:
		return tr::lng_passcode_strength_advice_variety();
	case PasscodeStrengthAdvice::Fine:
		return tr::lng_passcode_strength_advice_fine();
	}
	Unexpected("Advice in AdviceText.");
}

[[nodiscard]] const style::color &BandColor(
		const style::PasscodeStrengthMeter &st,
		PasscodeStrengthBand band) {
	switch (band) {
	case PasscodeStrengthBand::VeryWeak: return st.veryWeakFg;
	case PasscodeStrengthBand::Weak: return st.weakFg;
	case PasscodeStrengthBand::Good: return st.goodFg;
	case PasscodeStrengthBand::Strong: return st.strongFg;
	}
	Unexpected("Band in BandColor.");
}

} // namespace

PasscodeStrengthMeter::PasscodeStrengthMeter(
	QWidget *parent,
	const style::PasscodeStrengthMeter &st)
: RpWidget(parent)
, _st(st)
, _label(CreateChild<FlatLabel>(this, _st.label)) {
	_label->heightValue() | rpl::on_next([=] {
		if (!_inResize) {
			resizeToWidth(width());
		}
	}, lifetime());
	showCandidate(QString());
}

void PasscodeStrengthMeter::showCandidate(const QString &candidate) {
	_empty = candidate.isEmpty();
	_strength = EstimatePasscodeStrength(candidate);
	_adviceLifetime.destroy();
	(_empty
		? tr::lng_passcode_strength_empty()
		: AdviceText(_strength.advice)
	) | rpl::on_next([=](const QString &text) {
		_label->setText(text);
	}, _adviceLifetime);
	update();
}

int PasscodeStrengthMeter::resizeGetHeight(int newWidth) {
	_inResize = true;
	_label->resizeToWidth(newWidth);
	_label->moveToLeft(0, _st.height + _st.labelSkip, newWidth);
	_inResize = false;

	const auto labelHeight = std::max(
		_label->height(),
		_st.label.style.font->height);
	return _st.height + _st.labelSkip + labelHeight;
}

void PasscodeStrengthMeter::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);

	const auto spacing = _st.segmentSpacing;
	const auto available = width() - spacing * (kSegmentCount - 1);
	const auto bandFg = _empty
		? _st.emptyFg
		: BandColor(_st, _strength.band);
	const auto filled = _empty ? 0 : (int(_strength.band) + 1);
	auto left = 0;
	for (auto i = 0; i != kSegmentCount; ++i) {
		const auto segment = (available * (i + 1) / kSegmentCount)
			- (available * i / kSegmentCount);
		p.setBrush((i < filled) ? bandFg : _st.emptyFg);
		p.drawRoundedRect(
			QRect(left, 0, segment, _st.height),
			_st.segmentRadius,
			_st.segmentRadius);
		left += segment + spacing;
	}
}

} // namespace Ui
