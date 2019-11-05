/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/match_method.h"
#include "base/flags.h"
#include "base/bytes.h"
#include "base/algorithm.h"
#include "base/assertion.h"
#include "tl/tl_basic_types.h"
#include "tl/tl_boxed.h"

#include <QtCore/QVector>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <gsl/gsl>

using mtpPrime = int32;
using mtpRequestId = int32;
using mtpMsgId = uint64;
using mtpPingId = uint64;

using mtpBuffer = QVector<mtpPrime>;
using mtpTypeId = uint32;

namespace MTP {

// type DcId represents actual data center id, while in most cases
// we use some shifted ids, like DcId() + X * DCShift
using DcId = int32;
using ShiftedDcId = int32;

constexpr auto kDcShift = ShiftedDcId(10000);
constexpr auto kConfigDcShift = 0x01;
constexpr auto kLogoutDcShift = 0x02;
constexpr auto kUpdaterDcShift = 0x03;
constexpr auto kExportDcShift = 0x04;
constexpr auto kExportMediaDcShift = 0x05;
constexpr auto kMaxMediaDcCount = 0x10;
constexpr auto kBaseDownloadDcShift = 0x10;
constexpr auto kBaseUploadDcShift = 0x20;
constexpr auto kDestroyKeyStartDcShift = 0x100;

constexpr DcId BareDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId % kDcShift);
}

constexpr ShiftedDcId ShiftDcId(DcId dcId, int value) {
	return dcId + kDcShift * value;
}

constexpr int GetDcIdShift(ShiftedDcId shiftedDcId) {
	return shiftedDcId / kDcShift;
}

} // namespace MTP

enum {
	// core types
	mtpc_int = tl::id_int,
	mtpc_long = tl::id_long,
	mtpc_int128 = tl::id_int128,
	mtpc_int256 = tl::id_int256,
	mtpc_double = tl::id_double,
	mtpc_string = tl::id_string,
	mtpc_vector = tl::id_vector,
	mtpc_bytes = tl::id_bytes,
	mtpc_flags = tl::id_flags,

	// layers
	mtpc_invokeWithLayer1 = 0x53835315,
	mtpc_invokeWithLayer2 = 0x289dd1f6,
	mtpc_invokeWithLayer3 = 0xb7475268,
	mtpc_invokeWithLayer4 = 0xdea0d430,
	mtpc_invokeWithLayer5 = 0x417a57ae,
	mtpc_invokeWithLayer6 = 0x3a64d54d,
	mtpc_invokeWithLayer7 = 0xa5be56d3,
	mtpc_invokeWithLayer8 = 0xe9abd9fd,
	mtpc_invokeWithLayer9 = 0x76715a63,
	mtpc_invokeWithLayer10 = 0x39620c41,
	mtpc_invokeWithLayer11 = 0xa6b88fdf,
	mtpc_invokeWithLayer12 = 0xdda60d3c,
	mtpc_invokeWithLayer13 = 0x427c8ea2,
	mtpc_invokeWithLayer14 = 0x2b9b08fa,
	mtpc_invokeWithLayer15 = 0xb4418b64,
	mtpc_invokeWithLayer16 = 0xcf5f0987,
	mtpc_invokeWithLayer17 = 0x50858a19,
	mtpc_invokeWithLayer18 = 0x1c900537,

	// manually parsed
	mtpc_rpc_result = 0xf35c6d01,
	mtpc_msg_container = 0x73f1f8dc,
//	mtpc_msg_copy = 0xe06046b2,
	mtpc_gzip_packed = 0x3072cfa1
};
static const mtpTypeId mtpc_core_message = -1; // undefined type, but is used
static const mtpTypeId mtpLayers[] = {
	mtpTypeId(mtpc_invokeWithLayer1),
	mtpTypeId(mtpc_invokeWithLayer2),
	mtpTypeId(mtpc_invokeWithLayer3),
	mtpTypeId(mtpc_invokeWithLayer4),
	mtpTypeId(mtpc_invokeWithLayer5),
	mtpTypeId(mtpc_invokeWithLayer6),
	mtpTypeId(mtpc_invokeWithLayer7),
	mtpTypeId(mtpc_invokeWithLayer8),
	mtpTypeId(mtpc_invokeWithLayer9),
	mtpTypeId(mtpc_invokeWithLayer10),
	mtpTypeId(mtpc_invokeWithLayer11),
	mtpTypeId(mtpc_invokeWithLayer12),
	mtpTypeId(mtpc_invokeWithLayer13),
	mtpTypeId(mtpc_invokeWithLayer14),
	mtpTypeId(mtpc_invokeWithLayer15),
	mtpTypeId(mtpc_invokeWithLayer16),
	mtpTypeId(mtpc_invokeWithLayer17),
	mtpTypeId(mtpc_invokeWithLayer18),
};
static const uint32 mtpLayerMaxSingle = sizeof(mtpLayers) / sizeof(mtpLayers[0]);

namespace MTP {
namespace details {

struct SecureRequestCreateTag {
};

} // namespace details

class SecureRequestData;
class SecureRequest {
public:
	SecureRequest() = default;

	static constexpr auto kSaltInts = 2;
	static constexpr auto kSessionIdInts = 2;
	static constexpr auto kMessageIdInts = 2;
	static constexpr auto kSeqNoPosition = kSaltInts
		+ kSessionIdInts
		+ kMessageIdInts;
	static constexpr auto kSeqNoInts = 1;
	static constexpr auto kMessageLengthPosition = kSeqNoPosition
		+ kSeqNoInts;
	static constexpr auto kMessageLengthInts = 1;
	static constexpr auto kMessageBodyPosition = kMessageLengthPosition
		+ kMessageLengthInts;

	static SecureRequest Prepare(uint32 size, uint32 reserveSize = 0);

	template <
		typename Request,
		typename = std::enable_if_t<tl::is_boxed_v<Request>>>
	static SecureRequest Serialize(const Request &request);

	// For template MTP requests and MTPBoxed instanciation.
	template <typename Accumulator>
	void write(Accumulator &to) const {
		if (const auto size = sizeInBytes()) {
			tl::Writer<Accumulator>::PutBytes(to, dataInBytes(), size);
		}
	}

	SecureRequestData *operator->() const;
	SecureRequestData &operator*() const;
	explicit operator bool() const;

	void addPadding(bool extended);
	uint32 messageSize() const;

	// "request-like" wrap for msgIds vector
	bool isSentContainer() const;
	bool isStateRequest() const;
	bool needAck() const;

	using ResponseType = void; // don't know real response type =(

private:
	explicit SecureRequest(const details::SecureRequestCreateTag &);

	[[nodiscard]] size_t sizeInBytes() const;
	[[nodiscard]] const void *dataInBytes() const;

	std::shared_ptr<SecureRequestData> _data;

};

class SecureRequestData : public mtpBuffer {
public:
	explicit SecureRequestData(const details::SecureRequestCreateTag &) {
	}

	// in toSend: = 0 - must send in container, > 0 - can send without container
	// in haveSent: = 0 - container with msgIds, > 0 - when was sent
	int64 msDate = 0;

	mtpRequestId requestId = 0;
	SecureRequest after;
	bool needsLayer = false;

};

template <typename Request, typename>
SecureRequest SecureRequest::Serialize(const Request &request) {
	const auto requestSize = tl::count_length(request) >> 2;
	auto serialized = Prepare(requestSize);
	request.template write<mtpBuffer>(*serialized);
	return serialized;
}

} // namespace MTP

using MTPint = tl::int_type;

inline MTPint MTP_int(int32 v) {
	return tl::make_int(v);
}

template <typename Flags>
using MTPflags = tl::flags_type<Flags>;

template <typename T>
inline MTPflags<base::flags<T>> MTP_flags(base::flags<T> v) {
	return tl::make_flags(v);
}

template <typename T, typename = std::enable_if_t<!std::is_same<T, int>::value>>
inline MTPflags<base::flags<T>> MTP_flags(T v) {
	return tl::make_flags(v);
}

inline tl::details::zero_flags_helper MTP_flags(void(tl::details::zero_flags_helper::*)()) {
	return tl::details::zero_flags_helper();
}

using MTPlong = tl::long_type;

inline MTPlong MTP_long(uint64 v) {
	return tl::make_long(v);
}

using MTPint128 = tl::int128_type;

inline MTPint128 MTP_int128(uint64 l, uint64 h) {
	return tl::make_int128(l, h);
}

using MTPint256 = tl::int256_type;

inline MTPint256 MTP_int256(const MTPint128 &l, const MTPint128 &h) {
	return tl::make_int256(l, h);
}

using MTPdouble = tl::double_type;

inline MTPdouble MTP_double(float64 v) {
	return tl::make_double(v);
}

using MTPstring = tl::string_type;
using MTPbytes = tl::bytes_type;

inline MTPstring MTP_string(const std::string &v) {
	return tl::make_string(v);
}
inline MTPstring MTP_string(const QString &v) {
	return tl::make_string(v);
}
inline MTPstring MTP_string(const char *v) {
	return tl::make_string(v);
}
inline MTPstring MTP_string() {
	return tl::make_string();
}
MTPstring MTP_string(const QByteArray &v) = delete;

inline MTPbytes MTP_bytes(const QByteArray &v) {
	return tl::make_bytes(v);
}
inline MTPbytes MTP_bytes(QByteArray &&v) {
	return tl::make_bytes(std::move(v));
}
inline MTPbytes MTP_bytes() {
	return tl::make_bytes();
}
inline MTPbytes MTP_bytes(bytes::const_span buffer) {
	return tl::make_bytes(buffer);
}
inline MTPbytes MTP_bytes(const bytes::vector &buffer) {
	return tl::make_bytes(buffer);
}

inline QString qs(const MTPstring &v) {
	return tl::utf16(v);
}

inline QString qs(const QByteArray &v) {
	return tl::utf16(v);
}

inline QByteArray qba(const MTPstring &v) {
	return tl::utf8(v);
}

template <typename T>
using MTPvector = tl::vector_type<T>;

template <typename T>
inline MTPvector<T> MTP_vector(uint32 count) {
	return tl::make_vector<T>(count);
}
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count, const T &value) {
	return tl::make_vector<T>(count, value);
}
template <typename T>
inline MTPvector<T> MTP_vector(const QVector<T> &v) {
	return tl::make_vector<T>(v);
}
template <typename T>
inline MTPvector<T> MTP_vector(QVector<T> &&v) {
	return tl::make_vector<T>(std::move(v));
}
template <typename T>
inline MTPvector<T> MTP_vector() {
	return tl::make_vector<T>();
}

// Human-readable text serialization

struct MTPStringLogger {
	static constexpr auto kBufferSize = 1024 * 1024; // 1 mb start size

	MTPStringLogger()
	: p(new char[kBufferSize])
	, alloced(kBufferSize) {
	}
	~MTPStringLogger() {
		delete[] p;
	}

	MTPStringLogger &add(const QString &data) {
		auto d = data.toUtf8();
		return add(d.constData(), d.size());
	}

	MTPStringLogger &add(const char *data, int32 len = -1) {
		if (len < 0) len = strlen(data);
		if (!len) return (*this);

		ensureLength(len);
		memcpy(p + size, data, len);
		size += len;
		return (*this);
	}

	MTPStringLogger &addSpaces(int32 level) {
		int32 len = level * 2;
		if (!len) return (*this);

		ensureLength(len);
		for (char *ptr = p + size, *end = ptr + len; ptr != end; ++ptr) {
			*ptr = ' ';
		}
		size += len;
		return (*this);
	}

	MTPStringLogger &error(const char *problem = "could not decode type") {
		return add("[ERROR] (").add(problem).add(")");
	}

	void ensureLength(int32 add) {
		if (size + add <= alloced) return;

		int32 newsize = size + add;
		if (newsize % kBufferSize) {
			newsize += kBufferSize - (newsize % kBufferSize);
		}
		char *b = new char[newsize];
		memcpy(b, p, size);
		alloced = newsize;
		delete[] p;
		p = b;
	}

	char *p = nullptr;
	int size = 0;
	int alloced = 0;

};

[[nodiscard]] bool mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons = 0, uint32 level = 0, mtpPrime vcons = 0);

[[nodiscard]] bool mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons = 0);

inline QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end) {
	MTPStringLogger to;
	[[maybe_unused]] bool result = mtpTextSerializeType(to, from, end, mtpc_core_message);
	return QString::fromUtf8(to.p, to.size);
}

namespace tl {

template <typename Accumulator>
struct Writer;

template <typename Prime>
struct Reader;

template <>
struct Writer<mtpBuffer> {
	static void PutBytes(mtpBuffer &to, const void *bytes, uint32 count) {
		constexpr auto kPrime = sizeof(uint32);
		const auto primes = (count / kPrime) + (count % kPrime ? 1 : 0);
		const auto size = to.size();
		to.resize(size + primes);
		memcpy(to.data() + size, bytes, count);
	}
	static void Put(mtpBuffer &to, uint32 value) {
		to.push_back(mtpPrime(value));
	}
};

template <>
struct Reader<mtpPrime> final {
	[[nodiscard]] static bool HasBytes(
			uint32 count,
			const mtpPrime *from,
			const mtpPrime *end) {
		constexpr auto kPrime = sizeof(uint32);
		const auto primes = (count / kPrime) + (count % kPrime ? 1 : 0);
		return (end - from) >= primes;
	}
	static void GetBytes(
			void *bytes,
			uint32 count,
			const mtpPrime *&from,
			const mtpPrime *end) {
		Expects(HasBytes(count, from, end));

		constexpr auto kPrime = sizeof(uint32);
		const auto primes = (count / kPrime) + (count % kPrime ? 1 : 0);
		memcpy(bytes, from, count);
		from += primes;
	}
	[[nodiscard]] static bool Has(
			uint32 primes,
			const mtpPrime *from,
			const mtpPrime *end) {
		return (end - from) >= primes;
	}
	[[nodiscard]] static uint32 Get(const mtpPrime *&from, const mtpPrime *end) {
		Expects(from < end);

		return uint32(*from++);
	}
};

} // namespace tl
