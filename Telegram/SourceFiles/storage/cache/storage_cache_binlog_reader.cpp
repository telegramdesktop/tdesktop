/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_binlog_reader.h"

namespace Storage {
namespace Cache {
namespace details {

BinlogWrapper::BinlogWrapper(
	File &binlog,
	const Settings &settings,
	int64 till)
: _binlog(binlog)
, _settings(settings)
, _till(till ? till : _binlog.size())
, _data(_settings.readBlockSize)
, _full(_data) {
}

bool BinlogWrapper::finished() const {
	return _finished;
}

bool BinlogWrapper::failed() const {
	return _failed;
}

bool BinlogWrapper::readPart() {
	if (_finished) {
		return false;
	}
	const auto no = [&] {
		finish();
		return false;
	};
	const auto offset = _binlog.offset();
	const auto left = (_till - offset);
	if (!left) {
		return no();
	}

	Assert(_notParsedBytes >= 0 && _notParsedBytes <= _part.size());
	if (_notParsedBytes > 0 && _notParsedBytes < _part.size()) {
		bytes::move(_full, _part.subspan(_part.size() - _notParsedBytes));
	}
	const auto amount = std::min(
		left,
		int64(_full.size() - _notParsedBytes));
	Assert(amount > 0);
	const auto readBytes = _binlog.read(
		_full.subspan(_notParsedBytes, amount));
	if (!readBytes) {
		return no();
	}
	_notParsedBytes += readBytes;
	_part = _full.subspan(0, _notParsedBytes);
	return true;
}

bytes::const_span BinlogWrapper::readRecord(ReadRecordSize readRecordSize) {
	if (_finished) {
		return {};
	}
	const auto size = readRecordSize(*this, _part);
	if (size == kRecordSizeUnknown || size > _part.size()) {
		return {};
	} else if (size == kRecordSizeInvalid) {
		finish();
		_finished = _failed = true;
		return {};
	}
	Assert(size >= 0 && size <= _notParsedBytes);
	const auto result = _part.subspan(0, size);
	_part = _part.subspan(size);
	_notParsedBytes -= size;
	return result;
}

void BinlogWrapper::finish(size_type rollback) {
	Expects(rollback >= 0);

	if (rollback > 0) {
		_failed = true;
		_notParsedBytes += rollback;
	}
	_binlog.seek(_binlog.offset() - _notParsedBytes);
}

} // namespace details
} // namespace Cache
} // namespace Storage
