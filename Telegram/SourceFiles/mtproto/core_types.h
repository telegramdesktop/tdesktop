/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "core/basic_types.h"

namespace MTP {

// type DcId represents actual data center id, while in most cases
// we use some shifted ids, like DcId() + X * DCShift
typedef int32 DcId;
typedef int32 ShiftedDcId;

}

typedef int32 mtpPrime;
typedef int32 mtpRequestId;
typedef uint64 mtpMsgId;
typedef uint64 mtpPingId;

typedef QVector<mtpPrime> mtpBuffer;
typedef uint32 mtpTypeId;

class mtpRequestData;
class mtpRequest : public QSharedPointer<mtpRequestData> {
public:

	mtpRequest() {
	}
    explicit mtpRequest(mtpRequestData *ptr) : QSharedPointer<mtpRequestData>(ptr) {
	}

	uint32 innerLength() const;
	void write(mtpBuffer &to) const;

	typedef void ResponseType; // don't know real response type =(

};

class mtpRequestData : public mtpBuffer {
public:
	// in toSend: = 0 - must send in container, > 0 - can send without container
	// in haveSent: = 0 - container with msgIds, > 0 - when was sent
	TimeMs msDate;

	mtpRequestId requestId;
	mtpRequest after;
	bool needsLayer;

	mtpRequestData(bool/* sure*/) : msDate(0), requestId(0), needsLayer(false) {
	}

	static mtpRequest prepare(uint32 requestSize, uint32 maxSize = 0) {
		if (!maxSize) maxSize = requestSize;
		mtpRequest result(new mtpRequestData(true));
		result->reserve(8 + maxSize + _padding(maxSize)); // 2: salt, 2: session_id, 2: msg_id, 1: seq_no, 1: message_length
		result->resize(7);
		result->push_back(requestSize << 2);
		return result;
	}

	static void padding(mtpRequest &request) {
		if (request->size() < 9) return;

		uint32 requestSize = (request.innerLength() >> 2), padding = _padding(requestSize), fullSize = 8 + requestSize + padding; // 2: salt, 2: session_id, 2: msg_id, 1: seq_no, 1: message_length
        if (uint32(request->size()) != fullSize) {
			request->resize(fullSize);
			if (padding) {
                memset_rand(request->data() + (fullSize - padding), padding * sizeof(mtpPrime));
			}
		}
	}

	static uint32 messageSize(const mtpRequest &request) {
		if (request->size() < 9) return 0;
		return 4 + (request.innerLength() >> 2); // 2: msg_id, 1: seq_no, q: message_length
	}

	static bool isSentContainer(const mtpRequest &request); // "request-like" wrap for msgIds vector
	static bool isStateRequest(const mtpRequest &request);
	static bool needAck(const mtpRequest &request);
	static bool needAckByType(mtpTypeId type);

private:

	static uint32 _padding(uint32 requestSize) {
		return ((8 + requestSize) & 0x03) ? (4 - ((8 + requestSize) & 0x03)) : 0;
	}

};

inline uint32 mtpRequest::innerLength() const { // for template MTP requests and MTPBoxed instanciation
    mtpRequestData *value = data();
	if (!value || value->size() < 9) return 0;
	return value->at(7);
}

inline void mtpRequest::write(mtpBuffer &to) const {
    mtpRequestData *value = data();
	if (!value || value->size() < 9) return;
	uint32 was = to.size(), s = innerLength() / sizeof(mtpPrime);
	to.resize(was + s);
    memcpy(to.data() + was, value->constData() + 8, s * sizeof(mtpPrime));
}

class mtpResponse : public mtpBuffer {
public:
	mtpResponse() {
	}
	mtpResponse(const mtpBuffer &v) : mtpBuffer(v) {
	}
	mtpResponse &operator=(const mtpBuffer &v) {
		mtpBuffer::operator=(v);
		return (*this);
	}
	bool needAck() const {
		if (size() < 8) return false;
		uint32 seqNo = *(uint32*)(constData() + 6);
		return (seqNo & 0x01) ? true : false;
	}
};

typedef QMap<mtpRequestId, mtpRequest> mtpPreRequestMap;
typedef QMap<mtpMsgId, mtpRequest> mtpRequestMap;
typedef QMap<mtpMsgId, bool> mtpMsgIdsSet;

class mtpRequestIdsMap : public QMap<mtpMsgId, mtpRequestId> {
public:
	typedef QMap<mtpMsgId, mtpRequestId> ParentType;

	mtpMsgId min() const {
		return size() ? cbegin().key() : 0;
	}

	mtpMsgId max() const {
		ParentType::const_iterator e(cend());
		return size() ? (--e).key() : 0;
	}
};

typedef QMap<mtpRequestId, mtpResponse> mtpResponseMap;

class mtpErrorUnexpected : public Exception {
public:
	mtpErrorUnexpected(mtpTypeId typeId, const QString &type) : Exception(QString("MTP Unexpected type id #%1 read in %2").arg(uint32(typeId), 0, 16).arg(type), false) { // maybe api changed?..
	}
};

class mtpErrorInsufficient : public Exception {
public:
	mtpErrorInsufficient() : Exception("MTP Insufficient bytes in input buffer") {
	}
};

class mtpErrorBadTypeId : public Exception {
public:
	mtpErrorBadTypeId(mtpTypeId typeId, const QString &type) : Exception(QString("MTP Bad type id %1 passed to constructor of %2").arg(typeId).arg(type)) {
	}
};

class mtpData {
public:
	mtpData() : cnt(1) {
	}
    mtpData(const mtpData &) : cnt(1) {
	}

	mtpData *incr() {
		++cnt;
		return this;
	}
	bool decr() {
		return !--cnt;
	}
	bool needSplit() {
		return (cnt > 1);
	}

	virtual mtpData *clone() = 0;
	virtual ~mtpData() {
	}

private:
	uint32 cnt;
};

template <typename T>
class mtpDataImpl : public mtpData {
public:
	virtual mtpData *clone() {
		return new T(*(T*)this);
	}
};

class mtpDataOwner {
public:
	mtpDataOwner(const mtpDataOwner &v) : data(v.data ? v.data->incr() : 0) {
	}
	mtpDataOwner &operator=(const mtpDataOwner &v) {
		setData(v.data ? v.data->incr() : v.data);
		return *this;
	}
	~mtpDataOwner() {
		if (data && data->decr()) delete data;
	}

protected:
	explicit mtpDataOwner(mtpData *_data) : data(_data) {
	}
	void split() {
		if (data && data->needSplit()) {
			mtpData *clone = data->clone();
			if (data->decr()) delete data;
			data = clone;
		}
	}
	void setData(mtpData *_data) {
		if (data != _data) {
			if (data && data->decr()) delete data;
			data = _data;
		}
	}
	mtpData *data;
};

enum {
	// core types
	mtpc_int = 0xa8509bda,
	mtpc_long = 0x22076cba,
	mtpc_int128 = 0x4bb5362b,
	mtpc_int256 = 0x929c32f,
	mtpc_double = 0x2210c154,
	mtpc_string = 0xb5286e24,

	mtpc_vector = 0x1cb5c415,

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
static const mtpTypeId mtpc_bytes = mtpc_string;
static const mtpTypeId mtpc_flags = mtpc_int;
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

template <typename bareT>
class MTPBoxed : public bareT {
public:
	MTPBoxed() {
	}
	MTPBoxed(const bareT &v) : bareT(v) {
	}
	MTPBoxed(const MTPBoxed<bareT> &v) : bareT(v) {
	}
	MTPBoxed(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) {
		read(from, end, cons);
	}

	MTPBoxed<bareT> &operator=(const bareT &v) {
		*((bareT*)this) = v;
		return *this;
	}
	MTPBoxed<bareT> &operator=(const MTPBoxed<bareT> &v) {
		*((bareT*)this) = v;
		return *this;
	}

	uint32 innerLength() const {
		return sizeof(mtpTypeId) + bareT::innerLength();
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		cons = (mtpTypeId)*(from++);
		bareT::read(from, end, cons);
	}
	void write(mtpBuffer &to) const {
        to.push_back(bareT::type());
		bareT::write(to);
	}
};
template <typename T>
class MTPBoxed<MTPBoxed<T> > {
	typename T::CantMakeBoxedBoxedType v;
};

class MTPint {
public:
	int32 v;

	MTPint() {
	}
	MTPint(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return sizeof(int32);
	}
	mtpTypeId type() const {
		return mtpc_int;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_int) throw mtpErrorUnexpected(cons, "MTPint");
		v = (int32)*(from++);
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)v);
	}

private:
	explicit MTPint(int32 val) : v(val) {
	}

	friend MTPint MTP_int(int32 v);
};
inline MTPint MTP_int(int32 v) {
	return MTPint(v);
}
typedef MTPBoxed<MTPint> MTPInt;

template <typename Flags>
class MTPflags {
public:
	Flags v;
	static_assert(sizeof(Flags) == sizeof(int32), "MTPflags are allowed only wrapping int32 flag types!");

	MTPflags() {
	}
	MTPflags(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_flags) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return sizeof(Flags);
	}
	mtpTypeId type() const {
		return mtpc_flags;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_flags) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_flags) throw mtpErrorUnexpected(cons, "MTPflags");
		v = static_cast<Flags>(*(from++));
	}
	void write(mtpBuffer &to) const {
		to.push_back(static_cast<mtpPrime>(v));
	}

private:
	explicit MTPflags(Flags val) : v(val) {
	}

	template <typename T>
	friend MTPflags<T> MTP_flags(T v);
};

template <typename T>
inline MTPflags<T> MTP_flags(T v) {
	return MTPflags<T>(v);
}

template <typename Flags>
using MTPFlags = MTPBoxed<MTPflags<Flags>>;

inline bool operator==(const MTPint &a, const MTPint &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPint &a, const MTPint &b) {
	return a.v != b.v;
}

class MTPlong {
public:
	uint64 v;

	MTPlong() {
	}
	MTPlong(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_long) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return sizeof(uint64);
	}
	mtpTypeId type() const {
		return mtpc_long;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_long) {
		if (from + 2 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_long) throw mtpErrorUnexpected(cons, "MTPlong");
		v = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		from += 2;
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)(v & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(v >> 32));
	}

private:
	explicit MTPlong(uint64 val) : v(val) {
	}

	friend MTPlong MTP_long(uint64 v);
};
inline MTPlong MTP_long(uint64 v) {
	return MTPlong(v);
}
typedef MTPBoxed<MTPlong> MTPLong;

inline bool operator==(const MTPlong &a, const MTPlong &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPlong &a, const MTPlong &b) {
	return a.v != b.v;
}

class MTPint128 {
public:
	uint64 l;
	uint64 h;

	MTPint128() {
	}
	MTPint128(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int128) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return sizeof(uint64) + sizeof(uint64);
	}
	mtpTypeId type() const {
		return mtpc_int128;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int128) {
		if (from + 4 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_int128) throw mtpErrorUnexpected(cons, "MTPint128");
		l = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		h = (uint64)(((uint32*)from)[2]) | ((uint64)(((uint32*)from)[3]) << 32);
		from += 4;
	}
	void write(mtpBuffer &to) const {
		to.push_back((mtpPrime)(l & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(l >> 32));
		to.push_back((mtpPrime)(h & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(h >> 32));
	}

private:
	explicit MTPint128(uint64 low, uint64 high) : l(low), h(high) {
	}

	friend MTPint128 MTP_int128(uint64 l, uint64 h);
};
inline MTPint128 MTP_int128(uint64 l, uint64 h) {
	return MTPint128(l, h);
}
typedef MTPBoxed<MTPint128> MTPInt128;

inline bool operator==(const MTPint128 &a, const MTPint128 &b) {
	return a.l == b.l && a.h == b.h;
}
inline bool operator!=(const MTPint128 &a, const MTPint128 &b) {
	return a.l != b.l || a.h != b.h;
}

class MTPint256 {
public:
	MTPint128 l;
	MTPint128 h;

	MTPint256() {
	}
	MTPint256(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int256) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return l.innerLength() + h.innerLength();
	}
	mtpTypeId type() const {
		return mtpc_int256;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_int256) {
		if (cons != mtpc_int256) throw mtpErrorUnexpected(cons, "MTPint256");
		l.read(from, end);
		h.read(from, end);
	}
	void write(mtpBuffer &to) const {
		l.write(to);
		h.write(to);
	}

private:
	explicit MTPint256(MTPint128 low, MTPint128 high) : l(low), h(high) {
	}

	friend MTPint256 MTP_int256(const MTPint128 &l, const MTPint128 &h);
};
inline MTPint256 MTP_int256(const MTPint128 &l, const MTPint128 &h) {
	return MTPint256(l, h);
}
typedef MTPBoxed<MTPint256> MTPInt256;

inline bool operator==(const MTPint256 &a, const MTPint256 &b) {
	return a.l == b.l && a.h == b.h;
}
inline bool operator!=(const MTPint256 &a, const MTPint256 &b) {
	return a.l != b.l || a.h != b.h;
}

class MTPdouble {
public:
	float64 v;

	MTPdouble() {
	}
	MTPdouble(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_double) {
		read(from, end, cons);
	}

	uint32 innerLength() const {
		return sizeof(float64);
	}
	mtpTypeId type() const {
		return mtpc_double;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_double) {
		if (from + 2 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_double) throw mtpErrorUnexpected(cons, "MTPdouble");
		*(uint64*)(&v) = (uint64)(((uint32*)from)[0]) | ((uint64)(((uint32*)from)[1]) << 32);
		from += 2;
	}
	void write(mtpBuffer &to) const {
		uint64 iv = *(uint64*)(&v);
		to.push_back((mtpPrime)(iv & 0xFFFFFFFFL));
		to.push_back((mtpPrime)(iv >> 32));
	}

private:
	explicit MTPdouble(float64 val) : v(val) {
	}

	friend MTPdouble MTP_double(float64 v);
};
inline MTPdouble MTP_double(float64 v) {
	return MTPdouble(v);
}
typedef MTPBoxed<MTPdouble> MTPDouble;

inline bool operator==(const MTPdouble &a, const MTPdouble &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPdouble &a, const MTPdouble &b) {
	return a.v != b.v;
}

class MTPDstring : public mtpDataImpl<MTPDstring> {
public:
	MTPDstring() {
	}
	MTPDstring(const std::string &val) : v(val) {
	}
	MTPDstring(const QString &val) : v(val.toUtf8().constData()) {
	}
	MTPDstring(const QByteArray &val) : v(val.constData(), val.size()) {
	}
	MTPDstring(const char *val) : v(val) {
	}

	std::string v;
};

class MTPstring : private mtpDataOwner {
public:
	MTPstring() : mtpDataOwner(new MTPDstring()) {
	}
	MTPstring(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_string) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDstring &_string() {
		t_assert(data != nullptr);
		split();
		return *(MTPDstring*)data;
	}
	const MTPDstring &c_string() const {
		t_assert(data != nullptr);
		return *(const MTPDstring*)data;
	}

	uint32 innerLength() const {
		uint32 l = c_string().v.length();
		if (l < 254) {
			l += 1;
		} else {
			l += 4;
		}
		uint32 d = l & 0x03;
		if (d) l += (4 - d);
		return l;
	}
	mtpTypeId type() const {
		return mtpc_string;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_string) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_string) throw mtpErrorUnexpected(cons, "MTPstring");

		uint32 l;
		const uchar *buf = (const uchar*)from;
		if (buf[0] == 254) {
			l = (uint32)buf[1] + ((uint32)buf[2] << 8) + ((uint32)buf[3] << 16);
			buf += 4;
			from += ((l + 4) >> 2) + (((l + 4) & 0x03) ? 1 : 0);
		} else {
			l = (uint32)buf[0];
			++buf;
			from += ((l + 1) >> 2) + (((l + 1) & 0x03) ? 1 : 0);
		}
		if (from > end) throw mtpErrorInsufficient();

		if (!data) setData(new MTPDstring());
		MTPDstring &v(_string());
		v.v.resize(l);
		memcpy(&v.v[0], buf, l);
	}
	void write(mtpBuffer &to) const {
		uint32 l = c_string().v.length(), s = l + ((l < 254) ? 1 : 4), was = to.size();
		if (s & 0x03) {
			s += 4;
		}
		s >>= 2;
		to.resize(was + s);
		char *buf = (char*)&to[was];
		if (l < 254) {
			uchar sl = (uchar)l;
			*(buf++) = *(char*)(&sl);
		} else {
			*(buf++) = (char)254;
			*(buf++) = (char)(l & 0xFF);
			*(buf++) = (char)((l >> 8) & 0xFF);
			*(buf++) = (char)((l >> 16) & 0xFF);
		}
		memcpy(buf, c_string().v.c_str(), l);
	}

private:
	explicit MTPstring(MTPDstring *_data) : mtpDataOwner(_data) {
	}

	friend MTPstring MTP_string(const std::string &v);
	friend MTPstring MTP_string(const QString &v);
	friend MTPstring MTP_string(const char *v);

	friend MTPstring MTP_bytes(const QByteArray &v);
};
inline MTPstring MTP_string(const std::string &v) {
	return MTPstring(new MTPDstring(v));
}
inline MTPstring MTP_string(const QString &v) {
	return MTPstring(new MTPDstring(v));
}
inline MTPstring MTP_string(const char *v) {
	return MTPstring(new MTPDstring(v));
}
MTPstring MTP_string(const QByteArray &v) = delete;
typedef MTPBoxed<MTPstring> MTPString;

using MTPbytes = MTPstring;
using MTPBytes = MTPBoxed<MTPbytes>;

inline MTPbytes MTP_bytes(const QByteArray &v) {
	return MTPbytes(new MTPDstring(v));
}

inline bool operator==(const MTPstring &a, const MTPstring &b) {
	return a.c_string().v == b.c_string().v;
}
inline bool operator!=(const MTPstring &a, const MTPstring &b) {
	return a.c_string().v != b.c_string().v;
}

inline QString qs(const MTPstring &v) {
	auto &d = v.c_string().v;
	return QString::fromUtf8(d.data(), d.length());
}

inline QByteArray qba(const MTPstring &v) {
	auto &d = v.c_string().v;
	return QByteArray(d.data(), d.length());
}

template <typename T>
class MTPDvector : public mtpDataImpl<MTPDvector<T> > {
public:
	MTPDvector() {
	}
	MTPDvector(uint32 count) : v(count) {
	}
	MTPDvector(uint32 count, const T &value) : v(count, value) {
	}
	MTPDvector(const QVector<T> &vec) : v(vec) {
	}

	typedef QVector<T> VType;
	VType v;
};

template <typename T>
class MTPvector : private mtpDataOwner {
public:
	MTPvector() : mtpDataOwner(new MTPDvector<T>()) {
	}
	MTPvector(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_vector) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDvector<T> &_vector() {
		t_assert(data != nullptr);
		split();
		return *(MTPDvector<T>*)data;
	}
	const MTPDvector<T> &c_vector() const {
		t_assert(data != nullptr);
		return *(const MTPDvector<T>*)data;
	}

	uint32 innerLength() const {
		uint32 result(sizeof(uint32));
        for (typename VType::const_iterator i = c_vector().v.cbegin(), e = c_vector().v.cend(); i != e; ++i) {
			result += i->innerLength();
		}
		return result;
	}
	mtpTypeId type() const {
		return mtpc_vector;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_vector) {
		if (from + 1 > end) throw mtpErrorInsufficient();
		if (cons != mtpc_vector) throw mtpErrorUnexpected(cons, "MTPvector");
		uint32 count = (uint32)*(from++);

		if (!data) setData(new MTPDvector<T>());
		MTPDvector<T> &v(_vector());
		v.v.resize(0);
		v.v.reserve(count);
		for (uint32 i = 0; i < count; ++i) {
			v.v.push_back(T(from, end));
		}
	}
	void write(mtpBuffer &to) const {
		to.push_back(c_vector().v.size());
        for (typename VType::const_iterator i = c_vector().v.cbegin(), e = c_vector().v.cend(); i != e; ++i) {
			(*i).write(to);
		}
	}

private:
	explicit MTPvector(MTPDvector<T> *_data) : mtpDataOwner(_data) {
	}

	template <typename U>
	friend MTPvector<U> MTP_vector(uint32 count);
	template <typename U>
	friend MTPvector<U> MTP_vector(uint32 count, const U &value);
	template <typename U>
	friend MTPvector<U> MTP_vector(const QVector<U> &v);
	typedef typename MTPDvector<T>::VType VType;
};
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count) {
	return MTPvector<T>(new MTPDvector<T>(count));
}
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count, const T &value) {
	return MTPvector<T>(new MTPDvector<T>(count, value));
}
template <typename T>
inline MTPvector<T> MTP_vector(const QVector<T> &v) {
	return MTPvector<T>(new MTPDvector<T>(v));
}
template <typename T>
using MTPVector = MTPBoxed<MTPvector<T>>;

template <typename T>
inline bool operator==(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v == b.c_vector().v;
}
template <typename T>
inline bool operator!=(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v != b.c_vector().v;
}

// Human-readable text serialization

template <typename Type>
QString mtpWrapNumber(Type number, int32 base = 10) {
	return QString::number(number, base);
}

struct MTPStringLogger {
	MTPStringLogger() : p(new char[MTPDebugBufferSize]), size(0), alloced(MTPDebugBufferSize) {
	}
	~MTPStringLogger() {
		delete[] p;
	}

	MTPStringLogger &add(const QString &data) {
		QByteArray d = data.toUtf8();
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

	void ensureLength(int32 add) {
		if (size + add <= alloced) return;

		int32 newsize = size + add;
		if (newsize % MTPDebugBufferSize) newsize += MTPDebugBufferSize - (newsize % MTPDebugBufferSize);
		char *b = new char[newsize];
		memcpy(b, p, size);
		alloced = newsize;
		delete[] p;
		p = b;
	}
	char *p;
	int32 size, alloced;
};

void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons = 0, uint32 level = 0, mtpPrime vcons = 0);

void mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons = 0);

inline QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end) {
	MTPStringLogger to;
	try {
		mtpTextSerializeType(to, from, end, mtpc_core_message);
	} catch (Exception &e) {
		to.add("[ERROR] (").add(e.what()).add(")");
	}
	return QString::fromUtf8(to.p, to.size);
}

#include "mtproto/scheme_auto.h"

inline MTPbool MTP_bool(bool v) {
	return v ? MTP_boolTrue() : MTP_boolFalse();
}

inline bool mtpIsTrue(const MTPBool &v) {
	return v.type() == mtpc_boolTrue;
}
inline bool mtpIsFalse(const MTPBool &v) {
	return !mtpIsTrue(v);
}

// we must validate that MTProto scheme flags don't intersect with client side flags
// and define common bit operators which allow use Type_ClientFlag together with Type::Flag
#define DEFINE_MTP_CLIENT_FLAGS(Type) \
static_assert(static_cast<int32>(Type::Flag::MAX_FIELD) < static_cast<int32>(Type##_ClientFlag::MIN_FIELD), \
	"MTProto flags conflict with client side flags!"); \
inline Type::Flags qFlags(Type##_ClientFlag v) { return Type::Flags(static_cast<int32>(v)); } \
inline Type::Flags operator&(Type::Flags i, Type##_ClientFlag v) { return i & qFlags(v); } \
inline Type::Flags operator&(Type##_ClientFlag i, Type##_ClientFlag v) { return qFlags(i) & v; } \
inline Type::Flags &operator&=(Type::Flags &i, Type##_ClientFlag v) { return i &= qFlags(v); } \
inline Type::Flags operator|(Type::Flags i, Type##_ClientFlag v) { return i | qFlags(v); } \
inline Type::Flags operator|(Type::Flag i, Type##_ClientFlag v) { return i | qFlags(v); } \
inline Type::Flags operator|(Type##_ClientFlag i, Type##_ClientFlag v) { return qFlags(i) | v; } \
inline Type::Flags operator|(Type##_ClientFlag i, Type::Flag v) { return qFlags(i) | v; } \
inline Type::Flags &operator|=(Type::Flags &i, Type##_ClientFlag v) { return i |= qFlags(v); } \
inline Type::Flags operator~(Type##_ClientFlag v) { return ~qFlags(v); }

// we use the same flags field for some additional client side flags
enum class MTPDmessage_ClientFlag : int32 {
	// message has links for "shared links" indexing
	f_has_text_links = (1 << 30),

	// message is a group migrate (group -> supergroup) service message
	f_is_group_migrate = (1 << 29),

	// message needs initDimensions() + resize() + paint()
	f_pending_init_dimensions = (1 << 28),

	// message needs resize() + paint()
	f_pending_resize = (1 << 27),

	// message needs paint()
	f_pending_paint = (1 << 26),

	// message is attached to previous one when displaying the history
	f_attach_to_previous = (1 << 25),

	// message is attached to next one when displaying the history
	f_attach_to_next = (1 << 24),

	// message was sent from inline bot, need to re-set media when sent
	f_from_inline_bot = (1 << 23),

	// message has a switch inline keyboard button, need to return to inline
	f_has_switch_inline_button = (1 << 22),

	// message is generated on the client side and should be unread
	f_clientside_unread = (1 << 21),

	// update this when adding new client side flags
	MIN_FIELD = (1 << 21),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDmessage)

enum class MTPDreplyKeyboardMarkup_ClientFlag : int32 {
	// none (zero) markup
	f_zero = (1 << 30),

	// markup just wants a text reply
	f_force_reply = (1 << 29),

	// markup keyboard is inline
	f_inline = (1 << 28),

	// markup has a switch inline keyboard button
	f_has_switch_inline_button = (1 << 27),

	// update this when adding new client side flags
	MIN_FIELD = (1 << 27),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDreplyKeyboardMarkup)

enum class MTPDstickerSet_ClientFlag : int32 {
	// old value for sticker set is not yet loaded flag
	f_not_loaded__old = (1 << 31),

	// sticker set is not yet loaded
	f_not_loaded = (1 << 30),

	// sticker set is one of featured (should be saved locally)
	f_featured = (1 << 29),

	// sticker set is an unread featured set
	f_unread = (1 << 28),

	// special set like recent or custom stickers
	f_special = (1 << 27),

	// update this when adding new client side flags
	MIN_FIELD = (1 << 27),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDstickerSet)

extern const MTPReplyMarkup MTPnullMarkup;
extern const MTPVector<MTPMessageEntity> MTPnullEntities;
extern const MTPMessageFwdHeader MTPnullFwdHeader;

QString stickerSetTitle(const MTPDstickerSet &s);

inline bool mtpRequestData::isSentContainer(const mtpRequest &request) { // "request-like" wrap for msgIds vector
	if (request->size() < 9) return false;
	return (!request->msDate && !(*request)[6]); // msDate = 0, seqNo = 0
}
inline bool mtpRequestData::isStateRequest(const mtpRequest &request) {
	if (request->size() < 9) return false;
	return (mtpTypeId((*request)[8]) == mtpc_msgs_state_req);
}
inline bool mtpRequestData::needAck(const mtpRequest &request) {
	if (request->size() < 9) return false;
	return mtpRequestData::needAckByType((*request)[8]);
}
inline bool mtpRequestData::needAckByType(mtpTypeId type) {
	switch (type) {
	case mtpc_msg_container:
	case mtpc_msgs_ack:
	case mtpc_http_wait:
	case mtpc_bad_msg_notification:
	case mtpc_msgs_all_info:
	case mtpc_msgs_state_info:
	case mtpc_msg_detailed_info:
	case mtpc_msg_new_detailed_info:
	return false;
	}
	return true;
}
