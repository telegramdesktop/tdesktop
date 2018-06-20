/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_stats.h"

namespace Export {
namespace Output {

void Stats::incrementFiles() {
	++_files;
}

void Stats::incrementBytes(int count) {
	_bytes += count;
}

int Stats::filesCount() const {
	return _files;
}

int64 Stats::bytesCount() const {
	return _bytes;
}

} // namespace Output
} // namespace Export
