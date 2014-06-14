/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "types.h"
#include <zlib.h>

//#define DEBUG_MTPPRIME

#ifdef DEBUG_MTPPRIME
class mtpPrime { // for debug visualization, not like int32 :( in default constructor
public:
	explicit mtpPrime() : _v(0) {
	}
	mtpPrime(int32 v) : _v(v) {
	}
	mtpPrime &operator=(int32 v) {
		_v = v;
		return (*this);
	}
	operator int32&() {
		return _v;
	}
	operator const int32 &() const {
		return _v;
	}
private:
	int32 _v;
};
#else
typedef int32 mtpPrime;
#endif

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

	uint32 size() const;
	void write(mtpBuffer &to) const;

	typedef void ResponseType; // don't know real response type =(

};

class mtpRequestData : public mtpBuffer {
public:
	// in toSend: = 0 - must send in container, > 0 - can send without container
	// in haveSent: = 0 - container with msgIds, > 0 - when was sent
	uint64 msDate;
	mtpRequestId requestId;

	mtpRequestData(bool/* sure*/) : msDate(0) {
	}

	static mtpRequest prepare(uint32 requestSize) {
		mtpRequest result(new mtpRequestData(true));
		result->reserve(8 + requestSize + _padding(requestSize)); // 2: salt, 2: session_id, 2: msg_id, 1: seq_no, 1: message_length
		result->resize(7);
		result->push_back(requestSize << 2);
		return result;
	}

	static void padding(mtpRequest &request) {
		if (request->size() < 9) return;

		uint32 requestSize = ((*request)[7] >> 2), padding = _padding(requestSize), fullSize = 8 + requestSize + padding; // 2: salt, 2: session_id, 2: msg_id, 1: seq_no, 1: message_length
        if (uint32(request->size()) != fullSize) {
			request->resize(fullSize);
			if (padding) {
                memset_rand(request->data() + (fullSize - padding), padding * sizeof(mtpPrime));
			}
		}
	}

	static uint32 messageSize(const mtpRequest &request) {
		if (request->size() < 9) return 0;
		return 4 + ((*request)[7] >> 2); // 2: msg_id, 1: seq_no, q: message_length
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

inline uint32 mtpRequest::size() const { // for template MTP requests and MTPBoxed instanciation
    mtpRequestData *value = data();
	if (!value || value->size() < 9) return 0;
	return value->at(7);
}

inline void mtpRequest::write(mtpBuffer &to) const {
    mtpRequestData *value = data();
	if (!value || value->size() < 9) return;
	uint32 was = to.size(), s = size() / sizeof(mtpPrime);
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

class mtpMsgIdsSet : public QMap<mtpMsgId, bool> {
public:	
	typedef QMap<mtpMsgId, bool> ParentType;

	bool insert(const mtpMsgId &k, bool v) {
		ParentType::const_iterator i = constFind(k);
		if (i == cend()) {
			if (size() >= MTPIdsBufferSize && k < min()) {
				MTP_LOG(-1, ("No need to handle - %1 < min = %2").arg(k).arg(min()));
				return false;
			} else {
				ParentType::insert(k, v);
				return true;
			}
		} else {
			MTP_LOG(-1, ("No need to handle - %1 already is in map").arg(k));
			return false;
		}
	}

	mtpMsgId min() const {
		return size() ? cbegin().key() : 0;
	}

	mtpMsgId max() const {
		ParentType::const_iterator e(cend());
		return size() ? (--e).key() : 0;
	}
};

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
	mtpErrorUnexpected(mtpTypeId typeId, const QString &type) : Exception(QString("MTP Unexpected type id %1 read in %2").arg(typeId).arg(type), false) { // maybe api changed?..
	}
};

class mtpErrorInsufficient : public Exception {
public:
	mtpErrorInsufficient() : Exception("MTP Insufficient bytes in input buffer") {
	}
};

class mtpErrorUninitialized : public Exception {
public:
	mtpErrorUninitialized() : Exception("MTP Uninitialized variable write attempt") {
	}
};

class mtpErrorBadTypeId : public Exception {
public:
	mtpErrorBadTypeId(mtpTypeId typeId, const QString &type) : Exception(QString("MTP Bad type id %1 passed to constructor of %2").arg(typeId).arg(type)) {
	}
};

class mtpErrorWrongTypeId : public Exception {
public:
	mtpErrorWrongTypeId(mtpTypeId typeId, mtpTypeId required) : Exception(QString("MTP Wrong type id %1 for this data conversion, must be %2").arg(typeId).arg(required)) {
	}
};

class mtpErrorKeyNotReady : public Exception {
public:
	mtpErrorKeyNotReady(const QString &method) : Exception(QString("MTP Auth key is used in %1 without being created").arg(method)) {
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

	mtpc_boolTrue = 0x997275b5,
	mtpc_boolFalse = 0xbc799737,
	mtpc_vector = 0x1cb5c415,
	mtpc_error = 0xc4b9f9bb,
	mtpc_null = 0x56730bcc,

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

	// manually parsed
	mtpc_rpc_result = 0xf35c6d01,
	mtpc_msg_container = 0x73f1f8dc,
//	mtpc_msg_copy = 0xe06046b2,
	mtpc_gzip_packed = 0x3072cfa1
};
static const mtpTypeId mtpc_bytes = mtpc_string;
static const mtpTypeId mtpc_core_message = -1; // undefined type, but is used
static const mtpTypeId mtpLayers[] = {
	mtpc_invokeWithLayer1,
	mtpc_invokeWithLayer2,
	mtpc_invokeWithLayer3,
	mtpc_invokeWithLayer4,
	mtpc_invokeWithLayer5,
	mtpc_invokeWithLayer6,
	mtpc_invokeWithLayer7,
	mtpc_invokeWithLayer8,
	mtpc_invokeWithLayer9,
	mtpc_invokeWithLayer10,
	mtpc_invokeWithLayer11,
	mtpc_invokeWithLayer12,
	mtpc_invokeWithLayer13,
	mtpc_invokeWithLayer14,
}, mtpLayerMax = sizeof(mtpLayers) / sizeof(mtpLayers[0]);

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

	uint32 size() const {
		return sizeof(mtpTypeId) + bareT::size();
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

	uint32 size() const {
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

	uint32 size() const {
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

	uint32 size() const {
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

	uint32 size() const {
		return l.size() + h.size();
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

	uint32 size() const {
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
	MTPDstring(const string &val) : v(val) {
	}
	MTPDstring(const QString &val) : v(val.toUtf8().constData()) {
	}
	MTPDstring(const QByteArray &val) : v(val.constData(), val.size()) {
	}
	MTPDstring(const char *val) : v(val) {
	}

	string v;
};

class MTPstring : private mtpDataOwner {
public:
	MTPstring() : mtpDataOwner(new MTPDstring()) {
	}
	MTPstring(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_string) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDstring &_string() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDstring*)data;
	}
	const MTPDstring &c_string() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDstring*)data;
	}

	uint32 size() const {
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

	friend MTPstring MTP_string(const string &v);
	friend MTPstring MTP_string(const QString &v);
	friend MTPstring MTP_string(const QByteArray &v);
	friend MTPstring MTP_string(const char *v);
};
inline MTPstring MTP_string(const string &v) {
	return MTPstring(new MTPDstring(v));
}
inline MTPstring MTP_string(const QString &v) {
	return MTPstring(new MTPDstring(v));
}
inline MTPstring MTP_string(const QByteArray &v) {
	return MTPstring(new MTPDstring(v));
}
inline MTPstring MTP_string(const char *v) {
	return MTPstring(new MTPDstring(v));
}
typedef MTPBoxed<MTPstring> MTPString;

typedef MTPstring MTPbytes;
typedef MTPString MTPBytes;

inline bool operator==(const MTPstring &a, const MTPstring &b) {
	return a.c_string().v == b.c_string().v;
}
inline bool operator!=(const MTPstring &a, const MTPstring &b) {
	return a.c_string().v != b.c_string().v;
}

inline QString qs(const MTPstring &v) {
	const string &d(v.c_string().v);
	return QString::fromUtf8(d.c_str(), d.length());
}

class MTPbool {
public:
	bool v;

	MTPbool() {
	}
	MTPbool(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return v ? mtpc_boolTrue : mtpc_boolFalse;
	}
	void read(const mtpPrime *& /*from*/, const mtpPrime * /*end*/, mtpTypeId cons) {
		switch (cons) {
			case mtpc_boolFalse: v = false; break;
			case mtpc_boolTrue: v = true; break;
			default: throw mtpErrorUnexpected(cons, "MTPbool");
		}
	}
	void write(mtpBuffer & /*to*/) const {
	}

private:
	explicit MTPbool(bool val) : v(val) {
	}
	
	friend MTPbool MTP_bool(bool v);
};
inline MTPbool MTP_bool(bool v) {
	return MTPbool(v);
}
inline MTPbool MTP_boolFalse() {
	return MTP_bool(false);
}
inline MTPbool MTP_boolTrue() {
	return MTP_bool(true);
}
typedef MTPBoxed<MTPbool> MTPBool;

inline bool operator==(const MTPbool &a, const MTPbool &b) {
	return a.v == b.v;
}
inline bool operator!=(const MTPbool &a, const MTPbool &b) {
	return a.v != b.v;
}

template <typename T>
class MTPDvector : public mtpDataImpl<MTPDvector<T> > {
public:
	MTPDvector() {
	}
	MTPDvector(uint32 count) : v(count) {
	}
	MTPDvector(const QVector<T> &vec) : v(vec) {
	}

	typedef QVector<T> VType;
	VType v;
};



template <typename T>
class MTPvector;
template <typename T>
MTPvector<T> MTP_vector(uint32 count);

template <typename T>
MTPvector<T> MTP_vector(const QVector<T> &v);

template <typename T>
class MTPvector : private mtpDataOwner {
public:
	MTPvector() : mtpDataOwner(new MTPDvector<T>()) {
	}
	MTPvector(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_vector) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDvector<T> &_vector() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDvector<T>*)data;
	}
	const MTPDvector<T> &c_vector() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDvector<T>*)data;
	}

	uint32 size() const {
		uint32 result(sizeof(uint32));
        for (typename VType::const_iterator i = c_vector().v.cbegin(), e = c_vector().v.cend(); i != e; ++i) {
			result += i->size();
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

	friend MTPvector<T> MTP_vector<T>(uint32 count);
	friend MTPvector<T> MTP_vector<T>(const QVector<T> &v);
	typedef typename MTPDvector<T>::VType VType;
};
template <typename T>
inline MTPvector<T> MTP_vector(uint32 count) {
	return MTPvector<T>(new MTPDvector<T>(count));
}
template <typename T>
inline MTPvector<T> MTP_vector(const QVector<T> &v) {
	return MTPvector<T>(new MTPDvector<T>(v));
}
template <typename T>
class MTPVector : public MTPBoxed<MTPvector<T> > {
public:
	MTPVector() {
	}
	MTPVector(uint32 count) : MTPBoxed<MTPvector<T> >(MTP_vector<T>(count)) {
	}
	MTPVector(const MTPvector<T> &v) : MTPBoxed<MTPvector<T> >(v) {
	}
	MTPVector(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTPvector<T> >(from, end, cons) {
	}
};

template <typename T>
inline bool operator==(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v == b.c_vector().v;
}
template <typename T>
inline bool operator!=(const MTPvector<T> &a, const MTPvector<T> &b) {
	return a.c_vector().v != b.c_vector().v;
}

class MTPDerror : public mtpDataImpl<MTPDerror> {
public:
	MTPint vcode;
	MTPstring vtext;

	MTPDerror() {
	}
	MTPDerror(MTPint code, MTPString text) : vcode(code), vtext(text) {
	}
};
class MTPerror : private mtpDataOwner {
public:
	MTPerror() : mtpDataOwner(0) {
	}
	MTPerror(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_error) : mtpDataOwner(0) {
		read(from, end, cons);
	}

	MTPDerror &_error() {
		if (!data) throw mtpErrorUninitialized();
		split();
		return *(MTPDerror*)data;
	}
	const MTPDerror &c_error() const {
		if (!data) throw mtpErrorUninitialized();
		return *(const MTPDerror*)data;
	}

	uint32 size() const {
		return c_error().vcode.size() + c_error().vtext.size();
	}
	mtpTypeId type() const {
		return mtpc_error;
	}
	void read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_error) {
		if (cons != mtpc_error) throw mtpErrorUnexpected(cons, "MTPerror");

		if (!data) setData(new MTPDerror());
		MTPDerror &v(_error());
		v.vcode.read(from, end);
		v.vtext.read(from, end);
	}
	void write(mtpBuffer &to) const {
		c_error().vcode.write(to);
		c_error().vtext.write(to);
	}

private:
	explicit MTPerror(MTPDerror *_data) : mtpDataOwner(_data) {
	}

	friend MTPerror MTP_error(MTPint code, MTPstring text);
};
inline MTPerror MTP_error(MTPint code, MTPstring text) {
	return MTPerror(new MTPDerror(code, text));
}
typedef MTPBoxed<MTPerror> MTPError;

class MTPnull {
public:
	MTPnull() {
	}
	MTPnull(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_null) {
		read(from, end, cons);
	}

	uint32 size() const {
		return 0;
	}
	mtpTypeId type() const {
		return mtpc_null;
	}
	void read(const mtpPrime *& /*from*/, const mtpPrime * /*end*/, mtpTypeId cons = mtpc_null) {
		if (cons != mtpc_null) throw mtpErrorUnexpected(cons, "MTPnull");
	}
	void write(mtpBuffer & /*to*/) const {
	}
};
inline MTPnull MTP_null() {
	return MTPnull();
}
typedef MTPBoxed<MTPnull> MTPNull;

// Human-readable text serialization
#if (defined _DEBUG || defined _WITH_DEBUG)

QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end, mtpPrime cons = 0, uint32 level = 0, mtpPrime vcons = 0);

inline QString mtpTextSerializeCore(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons = 0) {
	QString add = QString(" ").repeated(level * 2);

	switch (mtpTypeId(cons)) {
	case mtpc_int: {
		MTPint value(from, end, cons);
	return QString("%1 [INT]").arg(value.v); }
		
	case mtpc_long: {
		MTPlong value(from, end, cons);
	return QString("%2 [LONG]").arg(value.v); }

	case mtpc_int128: {
		MTPint128 value(from, end, cons);
	return QString("%1 * 2^64 + %2 [INT128]").arg(value.h).arg(value.l); }

	case mtpc_int256: {
		MTPint256 value(from, end, cons);
	return QString("%1 * 2^192 + %2 * 2^128 + %3 * 2^64 + %4 [INT256]").arg(value.h.h).arg(value.h.l).arg(value.l.h).arg(value.l.l); }

	case mtpc_double: {
		MTPdouble value(from, end, cons);
	return QString("%1 [DOUBLE]").arg(value.v); }

	case mtpc_string: {
		MTPstring value(from, end, cons);
		QByteArray strUtf8(value.c_string().v.c_str(), value.c_string().v.length());
		QString str = QString::fromUtf8(strUtf8);
		if (str.toUtf8() == strUtf8) {
			str = QString("\"%1\" [STRING]").arg(str.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n"));
		} else {
			str = mb(strUtf8.constData(), strUtf8.size()).str() + QString(" [%1 BYTES]").arg(strUtf8.size());
		}
	return str; }

	case mtpc_boolTrue:
	case mtpc_boolFalse: {
		MTPbool value(from, end, cons);
	return (value.v ? "[TRUE]" : "[FALSE]"); }

	case mtpc_vector: {
		if (from >= end) {
			throw Exception("from >= end in vector");
		}
		int32 cnt = *(from++);
		QString result;
		if (cnt) {
			result += "\n" + add;
			for (int32 i = 0; i < cnt; ++i) {
				result += "  " + mtpTextSerialize(from, end, vcons, level + 1) + ",\n" + add;
			}
		} else {
			result = " ";
		}
	return QString("[ vector<%1>").arg(vcons) + result + "]"; }
		
	case mtpc_error: {
		QString result;
		result += "\n" + add;
		result += "  code: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		result += "  text: " + mtpTextSerialize(from, end, mtpc_string, level + 1) + ",\n" + add;
	return "{ error" + result + "}"; }

	case mtpc_null: {
		QString result;
		result = " ";
	return "{ null" + result + "}"; }

	case mtpc_rpc_result: {
		QString result;
		result += "\n" + add;
		result += "  req_msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		result += "  result: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
	return "{ rpc_result" + result + "}"; }

	case mtpc_msg_container: {
		QString result;
		result += "\n" + add;
		result += "  messages: " + mtpTextSerialize(from, end, mtpc_vector, level + 1, mtpc_core_message) + ",\n" + add;
	return "{ msg_container" + result + "}"; }

	case mtpc_core_message: {
		QString result;
		result += "\n" + add;
		result += "  msg_id: " + mtpTextSerialize(from, end, mtpc_long, level + 1) + ",\n" + add;
		result += "  seq_no: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		result += "  bytes: " + mtpTextSerialize(from, end, mtpc_int, level + 1) + ",\n" + add;
		result += "  body: " + mtpTextSerialize(from, end, 0, level + 1) + ",\n" + add;
	return "{ core_message" + result + "}"; }

	case mtpc_gzip_packed: {
		MTPstring packed(from, end); // read packed string as serialized mtp string type
		uint32 packedLen = packed.c_string().v.size(), unpackedChunk = packedLen;
		mtpBuffer result; // * 4 because of mtpPrime type
		result.resize(0);

		z_stream stream;
		stream.zalloc = 0;
		stream.zfree = 0;
		stream.opaque = 0;
		stream.avail_in = 0;
		stream.next_in = 0;
		int res = inflateInit2(&stream, 16 + MAX_WBITS);
		if (res != Z_OK) {
			throw Exception(QString("ungzip init, code: %1").arg(res));
		}
		stream.avail_in = packedLen;
		stream.next_in = (Bytef*)&packed._string().v[0];
		stream.avail_out = 0;
		while (!stream.avail_out) {
			result.resize(result.size() + unpackedChunk);
			stream.avail_out = unpackedChunk * sizeof(mtpPrime);
			stream.next_out = (Bytef*)&result[result.size() - unpackedChunk];
			int res = inflate(&stream, Z_NO_FLUSH);
			if (res != Z_OK && res != Z_STREAM_END) {
				inflateEnd(&stream);
				throw Exception(QString("ungzip unpack, code: %1").arg(res));
			}
		}
		if (stream.avail_out & 0x03) {
			uint32 badSize = result.size() * sizeof(mtpPrime) - stream.avail_out;
			throw Exception(QString("ungzip bad length, size: %1").arg(badSize));
		}
		result.resize(result.size() - (stream.avail_out >> 2));
		inflateEnd(&stream);

		if (!result.size()) {
			throw Exception("ungzip void data");
		}
		const mtpPrime *newFrom = result.constData(), *newEnd = result.constData() + result.size();
	return "[GZIPPED] " + mtpTextSerialize(newFrom, newEnd, 0, level); }

	default: {
		for (uint32 i = 1; i < mtpLayerMax; ++i) {
			if (cons == mtpLayers[i]) {
				return QString("[LAYER%1] ").arg(i + 1) + mtpTextSerialize(from, end, 0, level);
			}
		}
	}
	}

	throw Exception(QString("unknown cons %1").arg(cons));
}

#endif
