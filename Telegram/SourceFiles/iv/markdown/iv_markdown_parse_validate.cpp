/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_parse_validate.h"
#include "iv/markdown/iv_markdown_math.h"

#include <array>
#include <cstddef>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] unsigned char ByteAt(const QByteArray &source, int index) {
	return static_cast<unsigned char>(source.at(index));
}

template <std::size_t Size>
[[nodiscard]] bool HasPrefix(
		const QByteArray &source,
		const std::array<unsigned char, Size> &prefix) {
	if (source.size() < static_cast<int>(Size)) {
		return false;
	}
	for (auto i = std::size_t(0); i != Size; ++i) {
		if (ByteAt(source, static_cast<int>(i)) != prefix[i]) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool HasUtf8Bom(const QByteArray &source) {
	constexpr auto kUtf8Bom = std::array<unsigned char, 3>{
		0xEF,
		0xBB,
		0xBF,
	};
	return HasPrefix(source, kUtf8Bom);
}

[[nodiscard]] bool HasUnsupportedUnicodeBom(const QByteArray &source) {
	constexpr auto kUtf32Boms = std::array{
		std::array<unsigned char, 4>{ 0x00, 0x00, 0xFE, 0xFF },
		std::array<unsigned char, 4>{ 0xFF, 0xFE, 0x00, 0x00 },
	};
	for (const auto &bom : kUtf32Boms) {
		if (HasPrefix(source, bom)) {
			return true;
		}
	}
	constexpr auto kUtf16Boms = std::array{
		std::array<unsigned char, 2>{ 0xFE, 0xFF },
		std::array<unsigned char, 2>{ 0xFF, 0xFE },
	};
	for (const auto &bom : kUtf16Boms) {
		if (HasPrefix(source, bom)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QByteArray StripUtf8Bom(QByteArray source) {
	if (HasUtf8Bom(source)) {
		source.remove(0, 3);
	}
	return source;
}

[[nodiscard]] bool IsAllowedControl(unsigned char byte) {
	return byte == '\t' || byte == '\n' || byte == '\r';
}

[[nodiscard]] bool LooksBinary(const QByteArray &source) {
	const auto size = static_cast<int>(source.size());
	if (size == 0) {
		return false;
	}
	auto controlBytes = 0;
	for (auto i = 0; i != size; ++i) {
		const auto byte = ByteAt(source, i);
		if (byte == 0) {
			return true;
		} else if ((byte < 0x20 || byte == 0x7F)
			&& !IsAllowedControl(byte)) {
			++controlBytes;
		}
	}
	return (controlBytes * 10) > size;
}

[[nodiscard]] bool IsUtf8Continuation(unsigned char byte) {
	return (byte & 0xC0) == 0x80;
}

[[nodiscard]] bool IsValidCodepoint(int codepoint, int minimum) {
	return codepoint >= minimum
		&& codepoint <= 0x10FFFF
		&& (codepoint < 0xD800 || codepoint > 0xDFFF);
}

[[nodiscard]] bool IsValidUtf8(const QByteArray &source) {
	const auto size = static_cast<int>(source.size());
	for (auto i = 0; i != size; ++i) {
		const auto byte = ByteAt(source, i);
		if (byte <= 0x7F) {
			continue;
		}
		auto extraBytes = 0;
		auto codepoint = 0;
		auto minimum = 0;
		if (byte >= 0xC2 && byte <= 0xDF) {
			extraBytes = 1;
			codepoint = byte & 0x1F;
			minimum = 0x80;
		} else if (byte >= 0xE0 && byte <= 0xEF) {
			extraBytes = 2;
			codepoint = byte & 0x0F;
			minimum = 0x800;
		} else if (byte >= 0xF0 && byte <= 0xF4) {
			extraBytes = 3;
			codepoint = byte & 0x07;
			minimum = 0x10000;
		} else {
			return false;
		}
		if (i + extraBytes >= size) {
			return false;
		}
		for (auto j = 1; j <= extraBytes; ++j) {
			const auto continuation = ByteAt(source, i + j);
			if (!IsUtf8Continuation(continuation)) {
				return false;
			}
			codepoint = (codepoint << 6) | (continuation & 0x3F);
		}
		if (!IsValidCodepoint(codepoint, minimum)) {
			return false;
		}
		i += extraBytes;
	}
	return true;
}

} // namespace

ParseResult Failure(QString sourceName, QString error) {
	auto result = ParseResult();
	result.document = EmptyDocument(std::move(sourceName));
	result.error = std::move(error);
	result.ok = false;
	return result;
}

MarkdownSourceValidationResult ValidationFailure(
		QString sourceName,
		QString error) {
	auto result = MarkdownSourceValidationResult();
	result.source.sourceName = std::move(sourceName);
	result.error = std::move(error);
	result.ok = false;
	return result;
}

MarkdownSourceValidationResult ValidateMarkdownSourceForIv(
		const QByteArray &source,
		ParseOptions options) {
	const auto &limits = ParseLimitsForIv();
	if (source.size() > limits.maxSourceBytes) {
		return ValidationFailure(
			std::move(options.sourceName),
			u"source-too-large"_q);
	}
	if (HasUnsupportedUnicodeBom(source)) {
		return ValidationFailure(
			std::move(options.sourceName),
			u"source-unsupported-bom"_q);
	}
	auto normalized = StripUtf8Bom(source);
	if (LooksBinary(normalized)) {
		return ValidationFailure(
			std::move(options.sourceName),
			u"source-binary"_q);
	}
	if (!IsValidUtf8(normalized)) {
		return ValidationFailure(
			std::move(options.sourceName),
			u"source-invalid-utf8"_q);
	}
	auto result = MarkdownSourceValidationResult();
	result.source.normalized = std::move(normalized);
	result.source.decoded = QString::fromUtf8(
		result.source.normalized.constData(),
		result.source.normalized.size());
	result.source.lineStarts = BuildLineStarts(result.source.normalized);
	result.source.sourceName = std::move(options.sourceName);
	return result;
}

} // namespace Iv::Markdown
