/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/gram_boc.h"

#include <QtCore/QByteArray>

#include <utility>
#include <vector>

namespace Gram::Tests {
namespace {

// Messages the pinned wallet engine really produced: an external rotation,
// the same for a wallet that is not deployed yet, so that the message also
// carries a state init, and the internal one a gas-paying relayer delivers.
// The offset is where the request signature starts in the decoded bytes.
const auto kExternal = u"te6ccgEBAwEA2wABRYgAZ4xQk4U3opHAuJR05zBkP9ox03AUWUCLZzGptmq5eCIMAQHgb3k9gvcbRTBHo0i1Tf6H7+uEc/sWCnBvso+EvWulcg3Sln/+t8K1TbV3ZY0GMfu9SzGLZbD2mX2i7watvgPTB/u6mch//3/9cQAAAAECAwTCrGFc4SyQlfUmZksQauBUlmtXR6JkHwvHYR18E+fdLQIAgMaXjUHo2b8/kzmLv2VlyZq9CkIyoIsmPjPooy9ulySPlJzma5y5fVXX2hgk+42rJJWOyIv9orOu0TaS7uFk8Ac="_q;
constexpr auto kExternalSignatureAt = 51;
const auto kStateInit = u"te6ccgECBQEAASMAA0eIAGeMUJOFN6KRwLiUdOcwZD/aMdNwFFlAi2cxqbZquXgiEbABAgMAMP8AIJgh10mDCLnyQN+Ahfgz0O0eIO1T2QBSAAAAAAB//3/9sVgYwu51mjANtmKYj5sLSE5BNQcQddZ7R4cEAwsqFRQB4IJOhqLIPT6ZAVSqau5TiNrwViO18a5BfcbDaD7kwgRBs9qp20QMmdor2Eq49U+WG51zil92UuLZeqEeur18hwj7upnIf/9//XEAAAAAAAAADZsMkWTuztpaoK5jxR0a4idQWpWkORvJ/mlZTNwu4yoEAIBibQcxgA3vUtUu9Z0gckIggtUepLQeyuRyq8CwTKsH+YviQW5yhCnSUky2x7ssTWQ8FztE7UtBqc6G6SSvQJMH"_q;
constexpr auto kStateInitSignatureAt = 124;
const auto kInternal = u"te6ccgEBAwEA6AABYEIAGeMUJOFN6KRwLiUdOcwZD/aMdNwFFlAi2cxqbZquXgiAAAAAAAAAAAAAAAAAAQEB4GjrM3YMvhbU9Hg0G1vQpJVrg5iC886zd6ZNwSAmZRCh2fClR/+rNimR/GY/gmH80A5VJ+8DDRO9q+CrtyIitA77upnHf/9//XEAAAABAgMEk6mZyJrraLyLwzObhObSS7c/VCofehNrPY77xbJQoDUCAIBWCljSrxoHWqV8hxqGgA1c/Ns5vGKNvWAKMFPRbs91WEA37cX7cP9JJc8cEsjbVg5Ue4we4AJjn4VCaU01HWQE"_q;
constexpr auto kInternalSignatureAt = 64;

// The demo dApp's real default payload ("Hello!") and stateInit.
const auto kComment = u"te6cckEBAQEADAAAFAAAAABIZWxsbyGVgYQo"_q;
constexpr auto kCommentOpcodeAt = 13;
constexpr auto kCommentTextAt = 17;
constexpr auto kCommentTextSize = 6;
const auto kDeploy = u"te6cckEBBAEAOgACATQCAQAAART/APSkE/S88sgLAwBI0wHQ0wMBcbCRW+D6QDBwgBDIywVYzxYh+gLLagHPFsmAQPsAlxCarA=="_q;
const auto kHashed = u"te6cckEBAwEARQARGAAAAABIZWxsbyEhIQIAZAAAAAAAAAAAAAAAAAAAAAAAAAARIjNEUVVFUllJRCEBACCAgYKDhIWGh4iJiouMjY6PAADqDvOl"_q;

constexpr auto kSignatureBytes = 64;

[[nodiscard]] QByteArray Decode(const QString &base64) {
	return QByteArray::fromBase64(base64.toLatin1());
}

[[nodiscard]] QString Encode(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toBase64());
}

[[nodiscard]] quint32 Crc32c(const QByteArray &data, int size) {
	auto result = quint32(0xFFFFFFFFU);
	for (auto i = 0; i != size; ++i) {
		result ^= uchar(data[i]);
		for (auto bit = 0; bit != 8; ++bit) {
			result = (result & 1)
				? ((result >> 1) ^ 0x82F63B78U)
				: (result >> 1);
		}
	}
	return ~result;
}

[[nodiscard]] QString CheckBroken(
		const QString &original,
		int signatureAt) {
	const auto broken = BreakRotationSignature(original);
	if (broken.isEmpty()) {
		return u"refused a real rotation message"_q;
	}
	const auto was = Decode(original);
	const auto now = Decode(broken);
	if (now.size() != was.size()) {
		return u"length changed: %1 became %2"_q
			.arg(was.size())
			.arg(now.size());
	}
	auto changed = 0;
	for (auto i = 0, count = int(was.size()); i != count; ++i) {
		if (was[i] == now[i]) {
			continue;
		} else if (i < signatureAt || i >= signatureAt + kSignatureBytes) {
			return u"byte %1 changed outside the signature"_q.arg(i);
		}
		++changed;
	}
	if (changed < kSignatureBytes / 2) {
		return u"only %1 signature bytes changed"_q.arg(changed);
	}
	return QString();
}

[[nodiscard]] QString CheckRefused(
		const QString &name,
		const QString &boc) {
	return BreakRotationSignature(boc).isEmpty()
		? QString()
		: (u"accepted "_q + name);
}

[[nodiscard]] QString CheckComment(
		const QString &boc,
		const QString &expected) {
	const auto comment = TextCommentFromBoc(boc);
	return !comment
		? u"read no comment"_q
		: (*comment != expected)
		? (u"read \""_q + *comment + u"\" instead"_q)
		: QString();
}

[[nodiscard]] QString CheckNotComment(
		const QString &name,
		const QString &boc) {
	return TextCommentFromBoc(boc)
		? (u"read a comment from "_q + name)
		: QString();
}

[[nodiscard]] QString CommentWithByte(int at, uchar value) {
	auto bytes = Decode(kComment);
	bytes[at] = char(value);
	return Encode(bytes);
}

[[nodiscard]] QString SnakeComment() {
	auto bytes = QByteArray::fromHex("b5ee9c72" "01" "01" "02" "01" "00" "0f");
	bytes.append(QByteArray::fromHex("00"));
	bytes.append(QByteArray::fromHex("010e" "00000000" "616263" "01"));
	bytes.append(QByteArray::fromHex("0006" "646566"));
	return Encode(bytes);
}

} // namespace

std::vector<Check> BocChecks() {
	return {
		{
			u"boc: an external rotation loses its signature"_q,
			[] { return CheckBroken(kExternal, kExternalSignatureAt); },
		},
		{
			u"boc: a rotation with a state init loses its signature"_q,
			[] { return CheckBroken(kStateInit, kStateInitSignatureAt); },
		},
		{
			u"boc: an internal rotation loses its signature"_q,
			[] { return CheckBroken(kInternal, kInternalSignatureAt); },
		},
		{
			u"boc: every quote gets its own random signature"_q,
			[]() -> QString {
				const auto first = BreakRotationSignature(kExternal);
				const auto second = BreakRotationSignature(kExternal);
				return (!first.isEmpty() && first != second)
					? QString()
					: u"two quotes carried the same signature"_q;
			},
		},
		{
			u"boc: a checksummed message keeps a valid checksum"_q,
			[]() -> QString {
				auto bytes = Decode(kExternal);
				bytes[4] = char(uchar(bytes[4]) | 0x40);
				const auto crc = Crc32c(bytes, int(bytes.size()));
				for (auto i = 0; i != 4; ++i) {
					bytes.append(char(uchar((crc >> (8 * i)) & 0xFF)));
				}
				const auto broken = BreakRotationSignature(Encode(bytes));
				if (broken.isEmpty()) {
					return u"refused a checksummed rotation"_q;
				}
				const auto now = Decode(broken);
				const auto till = int(now.size()) - 4;
				const auto expected = Crc32c(now, till);
				auto found = quint32(0);
				for (auto i = 0; i != 4; ++i) {
					found |= (quint32(uchar(now[till + i])) << (8 * i));
				}
				return (found == expected)
					? QString()
					: u"the checksum was left stale"_q;
			},
		},
		{
			u"boc: anything but a rotation is refused"_q,
			[]() -> QString {
				auto foreign = Decode(kExternal);
				foreign[kExternalSignatureAt + kSignatureBytes] = char(0);
				const auto cases = std::vector<std::pair<QString, QString>>{
					{ u"an empty message"_q, QString() },
					{ u"text that is not base64"_q, u"not base64 at all"_q },
					{
						u"a truncated message"_q,
						Encode(Decode(kExternal).left(80)),
					},
					{
						u"a message with a trailing byte"_q,
						Encode(Decode(kExternal) + " "),
					},
					{ u"a message that is not a rotation"_q, Encode(foreign) },
				};
				for (const auto &[name, boc] : cases) {
					const auto error = CheckRefused(name, boc);
					if (!error.isEmpty()) {
						return error;
					}
				}
				return QString();
			},
		},
		{
			u"boc: the demo payload is the comment Hello!"_q,
			[] { return CheckComment(kComment, u"Hello!"_q); },
		},
		{
			u"boc: a non-zero opcode is not a comment"_q,
			[]() -> QString {
				const auto cases = std::vector<std::pair<QString, QString>>{
					{ u"an empty payload"_q, QString() },
					{ u"text that is not base64"_q, u"not base64 at all"_q },
					{
						u"a truncated payload"_q,
						Encode(Decode(kComment).left(20)),
					},
					{ u"a state init"_q, kDeploy },
					{ u"a cell with stored hashes"_q, kHashed },
					{
						u"a non-zero opcode"_q,
						CommentWithByte(kCommentOpcodeAt, 1),
					},
					{
						u"text that is not UTF-8"_q,
						CommentWithByte(kCommentTextAt, 0xFF),
					},
					{
						u"text cut inside a character"_q,
						CommentWithByte(
							kCommentTextAt + kCommentTextSize - 1,
							0xC3),
					},
				};
				for (const auto &[name, boc] : cases) {
					const auto error = CheckNotComment(name, boc);
					if (!error.isEmpty()) {
						return error;
					}
				}
				return QString();
			},
		},
		{
			u"boc: a comment continues into the referenced cell"_q,
			[] { return CheckComment(SnakeComment(), u"abcdef"_q); },
		},
	};
}

} // namespace Gram::Tests
