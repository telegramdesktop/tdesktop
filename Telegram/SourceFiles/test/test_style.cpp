/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_style.h"

#include "base/flat_map.h"
#include "test/test_log.h"

#include <algorithm>

namespace Test {
namespace {

struct Watch {
	crl::time started = 0;
	crl::time lastChange = 0;
	crl::time stableFor = 0;
	crl::time deadline = 0;
	int colorTolerance = 0;
	int samples = 0;
	std::vector<StyleSample> last;
	base::flat_map<QString, std::vector<QString>> sequences;
	bool done = false;
};

[[nodiscard]] base::flat_map<QString, Watch> &Watches() {
	static auto result = base::flat_map<QString, Watch>();
	return result;
}

[[nodiscard]] QString ColorHex(QColor color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	const auto hex = [&](int channel) {
		return u"%1"_q.arg(channel, 2, 16, QChar('0'));
	};
	auto result = u"#%1%2%3"_q.arg(
		hex(color.red()),
		hex(color.green()),
		hex(color.blue()));
	if (color.alpha() != 255) {
		result += hex(color.alpha());
	}
	return result;
}

[[nodiscard]] QString FormatSample(const StyleSample &sample) {
	if (sample.identity) {
		return u"0x%1"_q.arg(quintptr(sample.token), 0, 16);
	}
	return ColorHex(sample.color);
}

[[nodiscard]] int ColorDelta(QColor a, QColor b) {
	if (!a.isValid() || !b.isValid()) {
		return (a.isValid() == b.isValid()) ? 0 : -1;
	}
	return std::max({
		std::abs(a.red() - b.red()),
		std::abs(a.green() - b.green()),
		std::abs(a.blue() - b.blue()),
		std::abs(a.alpha() - b.alpha()) });
}

[[nodiscard]] int SampleDelta(
		const StyleSample &recorded,
		const StyleSample &current) {
	if (recorded.identity || current.identity) {
		return (recorded.identity == current.identity
			&& recorded.token == current.token) ? 0 : 1;
	}
	return ColorDelta(recorded.color, current.color);
}

[[nodiscard]] bool SameSample(
		const StyleSample &a,
		const StyleSample &b,
		int tolerance) {
	if (a.identity || b.identity) {
		return SampleDelta(a, b) == 0;
	}
	const auto delta = SampleDelta(a, b);
	return (delta >= 0) && (delta <= tolerance);
}

[[nodiscard]] const StyleSample *FindLabel(
		const std::vector<StyleSample> &samples,
		const QString &label) {
	for (const auto &sample : samples) {
		if (sample.label == label) {
			return &sample;
		}
	}
	return nullptr;
}

void Remember(
		Watch &watch,
		const QString &label,
		const QString &literal) {
	auto &sequence = watch.sequences[label];
	if (sequence.empty() || sequence.back() != literal) {
		sequence.push_back(literal);
	}
}

[[nodiscard]] QString JoinSequence(const std::vector<QString> &sequence) {
	auto result = QString();
	for (const auto &value : sequence) {
		if (!result.isEmpty()) {
			result += u" -> "_q;
		}
		result += value;
	}
	return result;
}

[[nodiscard]] QString FirstMovingLabel(const Watch &watch) {
	for (const auto &[label, sequence] : watch.sequences) {
		if (sequence.size() > 1) {
			return label;
		}
	}
	if (!watch.sequences.empty()) {
		return watch.sequences.begin()->first;
	}
	return u"none"_q;
}

void QuoteHarnessTolerance() {
	static auto quoted = false;
	if (quoted) {
		return;
	}
	quoted = true;
	Note(u"STYLE_COLOR_TOLERANCE: %1"_q.arg(kStyleColorTolerance));
}

void LogSettle(const QString &name, const Watch &watch, bool settled) {
	LogRaw(u"STYLE_SETTLE: name=%1 settled=%2 windowMs=%3 deadlineMs=%4 "
		"samples=%5 colorTolerance=%6 lastChangeMs=%7"_q
		.arg(name)
		.arg(settled ? 1 : 0)
		.arg(qint64(watch.stableFor))
		.arg(qint64(watch.deadline))
		.arg(watch.samples)
		.arg(watch.colorTolerance)
		.arg(qint64(watch.lastChange
			? (watch.lastChange - watch.started)
			: 0)));
}

} // namespace

bool StyleSettled(
		const QString &name,
		const StyleProbe &probe,
		crl::time stableFor,
		crl::time deadline,
		int colorTolerance) {
	auto &watch = Watches()[name];
	if (watch.done) {
		return true;
	}
	const auto now = crl::now();
	if (!watch.started) {
		watch.started = now;
		watch.lastChange = now;
		watch.stableFor = stableFor;
		watch.deadline = deadline;
		watch.colorTolerance = colorTolerance;
		QuoteHarnessTolerance();
	}
	const auto samples = probe ? probe() : std::vector<StyleSample>();
	++watch.samples;
	auto changed = watch.last.empty() != samples.empty();
	if (samples.size() != watch.last.size()) {
		changed = true;
	}
	for (const auto &sample : samples) {
		Remember(watch, sample.label, FormatSample(sample));
		const auto previous = FindLabel(watch.last, sample.label);
		if (!previous
			|| !SameSample(*previous, sample, watch.colorTolerance)) {
			changed = true;
		}
	}
	for (const auto &previous : watch.last) {
		if (!FindLabel(samples, previous.label)) {
			changed = true;
		}
	}
	if (changed) {
		watch.lastChange = now;
		watch.last = samples;
	}
	if (!watch.last.empty()
		&& (now - watch.lastChange >= watch.stableFor)) {
		watch.done = true;
		LogSettle(name, watch, true);
		Pass(u"style settle: %1"_q.arg(name));
		return true;
	}
	if (now - watch.started >= watch.deadline) {
		watch.done = true;
		const auto label = FirstMovingLabel(watch);
		const auto sequence = watch.sequences.contains(label)
			? JoinSequence(watch.sequences[label])
			: u"empty"_q;
		LogSettle(name, watch, false);
		Fail(
			u"style settle: %1 token=%2 sequence=%3 windowMs=%4"_q.arg(
				name,
				label,
				sequence,
				QString::number(qint64(watch.stableFor))),
			u"colorTolerance=%1"_q.arg(watch.colorTolerance));
		return true;
	}
	return false;
}

void StyleBaseline::add(const QString &label, QColor color) {
	auto sample = StyleSample{
		.label = label,
		.color = color,
	};
	for (auto &existing : _values) {
		if (existing.label == label) {
			existing = std::move(sample);
			return;
		}
	}
	_values.push_back(std::move(sample));
}

void StyleBaseline::addIdentity(const QString &label, quintptr token) {
	auto sample = StyleSample{
		.label = label,
		.token = token,
		.identity = true,
	};
	for (auto &existing : _values) {
		if (existing.label == label) {
			existing = std::move(sample);
			return;
		}
	}
	_values.push_back(std::move(sample));
}

void StyleBaseline::assertHolds(
		const QString &name,
		const StyleProbe &probe,
		int colorTolerance) const {
	QuoteHarnessTolerance();
	const auto current = probe ? probe() : std::vector<StyleSample>();
	for (const auto &recorded : _values) {
		const auto found = FindLabel(current, recorded.label);
		const auto recordedText = FormatSample(recorded);
		const auto currentText = found
			? FormatSample(*found)
			: u"missing"_q;
		const auto delta = found
			? SampleDelta(recorded, *found)
			: -1;
		const auto ok = found
			&& (delta >= 0)
			&& (delta <= (recorded.identity ? 0 : colorTolerance));
		LogRaw(u"STYLE_BASELINE: name=%1 label=%2 recorded=%3 current=%4 "
			"delta=%5 result=%6"_q
			.arg(
				name,
				recorded.label,
				recordedText,
				currentText)
			.arg(delta)
			.arg(ok ? u"PASS"_q : u"FAIL"_q));
		Check(
			ok,
			u"style baseline: %1 label=%2 recorded=%3 current=%4 delta=%5"_q
				.arg(
					name,
					recorded.label,
					recordedText,
					currentText)
				.arg(delta),
			ok ? QString() : u"moved"_q);
	}
}

bool StyleBaseline::empty() const {
	return _values.empty();
}

} // namespace Test

#endif // _DEBUG
