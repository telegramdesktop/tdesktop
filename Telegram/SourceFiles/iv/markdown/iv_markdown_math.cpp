/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_math.h"

#include <QtCore/QString>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace Iv::Markdown {
namespace {

[[nodiscard]] bool SetError(QString *error, QString value) {
	if (error) {
		*error = std::move(value);
	}
	return false;
}

[[nodiscard]] bool IsMasked(const std::vector<bool> &mask, int offset) {
	return (offset >= 0)
		&& (offset < static_cast<int>(mask.size()))
		&& mask[offset];
}

[[nodiscard]] bool IsAsciiLetter(char ch) {
	return ((ch >= 'a') && (ch <= 'z'))
		|| ((ch >= 'A') && (ch <= 'Z'));
}

void OffsetToPosition(
		const std::vector<int> &lineStarts,
		int offset,
		int *line,
		int *column) {
	const auto clampedOffset = std::max(offset, 0);
	auto resultLine = 1;
	auto resultColumn = clampedOffset + 1;
	if (!lineStarts.empty()) {
		auto i = std::upper_bound(
			lineStarts.begin(),
			lineStarts.end(),
			clampedOffset);
		if (i != lineStarts.begin()) {
			--i;
			resultLine = static_cast<int>(i - lineStarts.begin()) + 1;
			resultColumn = clampedOffset - *i + 1;
		}
	}
	if (line) {
		*line = resultLine;
	}
	if (column) {
		*column = resultColumn;
	}
}

[[nodiscard]] QString FindParentBlock(
		const std::vector<MathScanBlock> &blocks,
		int line,
		int column) {
	auto best = u"document"_q;
	auto bestSpan = std::numeric_limits<int>::max();
	for (const auto &block : blocks) {
		const auto &range = block.range;
		if (!range.available) {
			continue;
		} else if (line < range.startLine || line > range.endLine) {
			continue;
		} else if (line == range.startLine && column < range.startColumn) {
			continue;
		} else if (line == range.endLine && column > range.endColumn) {
			continue;
		}
		const auto span = (range.endLine - range.startLine) * 1000
			+ (range.endColumn - range.startColumn);
		if (span < bestSpan) {
			bestSpan = span;
			best = block.nodeKind;
		}
	}
	return best;
}

[[nodiscard]] bool LooksLikeCurrency(const QByteArray &content) {
	if (content.isEmpty()) {
		return true;
	}
	auto hasDigit = false;
	const auto size = static_cast<int>(content.size());
	for (auto i = 0; i != size; ++i) {
		const auto ch = content.at(i);
		if (ch >= '0' && ch <= '9') {
			hasDigit = true;
		} else if (ch != '.' && ch != ',' && ch != ' ') {
			return false;
		}
	}
	return hasDigit;
}

[[nodiscard]] bool HasStrongMathSignal(const QByteArray &content) {
	const auto size = static_cast<int>(content.size());
	const auto hasVisibleNeighbors = [&](int position) {
		auto left = position - 1;
		while (left >= 0) {
			const auto ch = content.at(left);
			if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
				break;
			}
			--left;
		}
		auto right = position + 1;
		while (right < size) {
			const auto ch = content.at(right);
			if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
				break;
			}
			++right;
		}
		return (left >= 0) && (right < size);
	};
	for (auto i = 0; i != size; ++i) {
		const auto ch = content.at(i);
		if (ch == '\\' && (i + 1) < size) {
			const auto next = content.at(i + 1);
			if (IsAsciiLetter(next)
				|| next == ','
				|| next == ':'
				|| next == ';'
				|| next == '!') {
				return true;
			}
		} else if (ch == '^' || ch == '_') {
			if (hasVisibleNeighbors(i)) {
				return true;
			}
		} else if (ch == '+'
			|| ch == '='
			|| ch == '<'
			|| ch == '>') {
			if (hasVisibleNeighbors(i)) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool LooksLikeProse(const QByteArray &content) {
	auto alphaWords = 0;
	auto longestRun = 0;
	const auto size = static_cast<int>(content.size());
	auto i = 0;
	while (i < size) {
		while (i < size && !IsAsciiLetter(content.at(i))) {
			++i;
		}
		const auto start = i;
		while (i < size && IsAsciiLetter(content.at(i))) {
			++i;
		}
		const auto length = i - start;
		if (length >= 2 && (start == 0 || content.at(start - 1) != '\\')) {
			++alphaWords;
			longestRun = std::max(longestRun, length);
		}
	}
	return alphaWords >= 2 || longestRun >= 5;
}

[[nodiscard]] QByteArray StripBlockquoteMarkers(QByteArray content) {
	auto cleaned = QByteArray();
	cleaned.reserve(content.size());
	const auto size = static_cast<int>(content.size());
	auto position = 0;
	while (position < size) {
		auto endOfLine = position;
		while (endOfLine < size && content.at(endOfLine) != '\n') {
			++endOfLine;
		}
		auto start = position;
		if (position > 0) {
			while (start < endOfLine) {
				while (start < endOfLine
					&& (content.at(start) == ' ' || content.at(start) == '\t')) {
					++start;
				}
				if (start < endOfLine && content.at(start) == '>') {
					++start;
					continue;
				}
				break;
			}
			if (start < endOfLine && content.at(start) == ' ') {
				++start;
			}
		}
		cleaned.append(content.constData() + start, endOfLine - start);
		if (endOfLine < size) {
			cleaned.append('\n');
		}
		position = endOfLine + 1;
	}
	return cleaned;
}

[[nodiscard]] SourceRange RangeForOffsets(
		const std::vector<int> &lineStarts,
		int sourceSize,
		int startOffset,
		int endOffset) {
	auto startLine = 0;
	auto startColumn = 0;
	auto endLine = 0;
	auto endColumn = 0;
	OffsetToPosition(lineStarts, startOffset, &startLine, &startColumn);
	OffsetToPosition(
		lineStarts,
		std::max(startOffset, endOffset - 1),
		&endLine,
		&endColumn);
	return RangeFromLineColumns(
		lineStarts,
		sourceSize,
		startLine,
		startColumn,
		endLine,
		endColumn);
}

[[nodiscard]] bool ExceedsFormulaBytes(
		const QByteArray &content,
		int maxFormulaBytes) {
	return (maxFormulaBytes >= 0)
		&& (content.size() > maxFormulaBytes);
}

[[nodiscard]] bool ExceedsFormulaCount(
		const std::vector<MathFormula> &formulas,
		int maxFormulaCount) {
	return (maxFormulaCount >= 0)
		&& (static_cast<int>(formulas.size()) >= maxFormulaCount);
}

} // namespace

std::vector<int> BuildLineStarts(const QByteArray &source) {
	auto result = std::vector<int>();
	result.push_back(0);
	const auto size = static_cast<int>(source.size());
	for (auto i = 0; i != size; ++i) {
		if (source.at(i) == '\n') {
			result.push_back(i + 1);
		}
	}
	return result;
}

SourceRange RangeFromLineColumns(
		const std::vector<int> &lineStarts,
		int sourceSize,
		int startLine,
		int startColumn,
		int endLine,
		int endColumn) {
	auto result = SourceRange();
	result.startLine = startLine;
	result.startColumn = startColumn;
	result.endLine = endLine;
	result.endColumn = endColumn;
	const auto linesCount = static_cast<int>(lineStarts.size());
	if (startLine <= 0
		|| endLine <= 0
		|| startLine > linesCount
		|| endLine > linesCount) {
		return result;
	}
	const auto maxOffset = std::max(sourceSize, 0);
	const auto startOffset = lineStarts[startLine - 1]
		+ std::max(0, startColumn - 1);
	const auto endOffset = lineStarts[endLine - 1]
		+ std::max(0, endColumn);
	result.available = true;
	result.startOffset = std::clamp(startOffset, 0, maxOffset);
	result.endOffset = std::clamp(endOffset, 0, maxOffset);
	return result;
}

bool ExtractMathRegions(
		const QByteArray &source,
		const std::vector<bool> &mask,
		const std::vector<int> &lineStarts,
		const std::vector<MathScanBlock> &blocks,
		int maxFormulaBytes,
		int maxFormulaCount,
		std::vector<MathFormula> *out,
		QString *error) {
	if (!out) {
		return SetError(error, u"invalid-output"_q);
	}
	auto formulas = std::vector<MathFormula>();
	const auto size = static_cast<int>(source.size());
	auto i = 0;
	while (i < size) {
		if (IsMasked(mask, i) || source.at(i) != '$') {
			++i;
			continue;
		}
		if (i > 0 && source.at(i - 1) == '\\') {
			++i;
			continue;
		}
		const auto display = (i + 1 < size)
			&& (source.at(i + 1) == '$')
			&& !IsMasked(mask, i + 1);
		const auto delimiterSize = display ? 2 : 1;
		const auto contentStart = i + delimiterSize;
		auto j = contentStart;
		auto closing = -1;
		while (j < size) {
			if (IsMasked(mask, j)) {
				++j;
				continue;
			}
			if (source.at(j) == '\\') {
				j += 2;
				continue;
			}
			if (!display && source.at(j) == '\n') {
				break;
			}
			if (!display && source.at(j) == '$') {
				closing = j;
				break;
			}
			if (display
				&& source.at(j) == '$'
				&& (j + 1 < size)
				&& source.at(j + 1) == '$'
				&& !IsMasked(mask, j + 1)) {
				closing = j;
				break;
			}
			++j;
		}
		if (closing < 0) {
			++i;
			continue;
		}
		auto content = source.mid(contentStart, closing - contentStart);
		if (!display && LooksLikeCurrency(content)) {
			++i;
			continue;
		}
		if (!display
			&& !HasStrongMathSignal(content)
			&& LooksLikeProse(content)) {
			++i;
			continue;
		}
		if (display) {
			content = StripBlockquoteMarkers(std::move(content));
		}
		if (ExceedsFormulaBytes(content, maxFormulaBytes)) {
			return SetError(error, u"formula-too-large"_q);
		}
		if (ExceedsFormulaCount(formulas, maxFormulaCount)) {
			return SetError(error, u"too-many-formulas"_q);
		}
		auto startLine = 0;
		auto startColumn = 0;
		OffsetToPosition(lineStarts, i, &startLine, &startColumn);
		auto formula = MathFormula();
		formula.index = static_cast<int>(formulas.size()) + 1;
		formula.kind = display ? MathKind::Display : MathKind::Inline;
		formula.tex = QString::fromUtf8(content.constData(), content.size());
		formula.range = RangeForOffsets(
			lineStarts,
			size,
			i,
			closing + delimiterSize);
		formula.parentNodeKind = FindParentBlock(blocks, startLine, startColumn);
		formulas.push_back(std::move(formula));
		i = closing + delimiterSize;
	}
	*out = std::move(formulas);
	if (error) {
		error->clear();
	}
	return true;
}

} // namespace Iv::Markdown
