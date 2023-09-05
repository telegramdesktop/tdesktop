/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <atomic>

namespace Export {
namespace Output {

class Stats {
public:
	Stats() = default;
	Stats(const Stats &other);

	void incrementFiles();
	void incrementBytes(int count);

	int filesCount() const;
	int64 bytesCount() const;

private:
	std::atomic<int> _files;
	std::atomic<int64> _bytes;

};

} // namespace Output
} // namespace Export
