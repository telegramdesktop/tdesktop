/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace qthelp {

class RegularExpressionMatch {
public:
	RegularExpressionMatch(const QRegularExpressionMatch &other) = delete;
	RegularExpressionMatch(const RegularExpressionMatch &other) = delete;
	RegularExpressionMatch(QRegularExpressionMatch &&match) : data_(std::move(match)) {
	}
	RegularExpressionMatch(RegularExpressionMatch &&other) : data_(std::move(other.data_)) {
	}
	RegularExpressionMatch &operator=(const QRegularExpressionMatch &match) = delete;
	RegularExpressionMatch &operator=(const RegularExpressionMatch &other) = delete;
	RegularExpressionMatch &operator=(QRegularExpressionMatch &&match) {
		data_ = std::move(match);
		return *this;
	}
	RegularExpressionMatch &operator=(RegularExpressionMatch &&other) {
		data_ = std::move(other.data_);
		return *this;
	}
	QRegularExpressionMatch *operator->() {
		return &data_;
	}
	const QRegularExpressionMatch *operator->() const {
		return &data_;
	}
	bool valid() const {
		return data_.hasMatch();
	}
	explicit operator bool() const {
		return valid();
	}

private:
	QRegularExpressionMatch data_;

};

enum class RegExOption {
	None = QRegularExpression::NoPatternOption,
	CaseInsensitive = QRegularExpression::CaseInsensitiveOption,
	DotMatchesEverything = QRegularExpression::DotMatchesEverythingOption,
	Multiline = QRegularExpression::MultilineOption,
	ExtendedSyntax = QRegularExpression::ExtendedPatternSyntaxOption,
	InvertedGreediness = QRegularExpression::InvertedGreedinessOption,
	DontCapture = QRegularExpression::DontCaptureOption,
	UseUnicodeProperties = QRegularExpression::UseUnicodePropertiesOption,
#ifndef OS_MAC_OLD
	OptimizeOnFirstUsage = QRegularExpression::OptimizeOnFirstUsageOption,
	DontAutomaticallyOptimize = QRegularExpression::DontAutomaticallyOptimizeOption,
#endif // OS_MAC_OLD
};
using RegExOptions = base::flags<RegExOption>;
inline constexpr auto is_flag_type(RegExOption) { return true; };

inline RegularExpressionMatch regex_match(const QString &string, const QString &subject, RegExOptions options = 0) {
	auto qtOptions = QRegularExpression::PatternOptions(static_cast<int>(options));
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subject));
}

inline RegularExpressionMatch regex_match(const QString &string, const QStringRef &subjectRef, RegExOptions options = 0) {
	auto qtOptions = QRegularExpression::PatternOptions(static_cast<int>(options));
#ifndef OS_MAC_OLD
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subjectRef));
#else // OS_MAC_OLD
	return RegularExpressionMatch(QRegularExpression(string, qtOptions).match(subjectRef.toString()));
#endif // OS_MAC_OLD
}

} // namespace qthelp
