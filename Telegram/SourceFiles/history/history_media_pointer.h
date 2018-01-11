/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryMedia;

// HistoryMedia has a special owning smart pointer
// which regs/unregs this media to the holding HistoryItem
class HistoryMediaPtr {
public:
	HistoryMediaPtr();
	HistoryMediaPtr(const HistoryMediaPtr &other) = delete;
	HistoryMediaPtr &operator=(const HistoryMediaPtr &other) = delete;
	HistoryMediaPtr(std::unique_ptr<HistoryMedia> other);
	HistoryMediaPtr &operator=(std::unique_ptr<HistoryMedia> other);

	HistoryMedia *get() const {
		return _pointer.get();
	}
	void reset(std::unique_ptr<HistoryMedia> pointer = nullptr);
	bool isNull() const {
		return !_pointer;
	}

	HistoryMedia *operator->() const {
		return get();
	}
	HistoryMedia &operator*() const {
		Expects(!isNull());
		return *get();
	}
	explicit operator bool() const {
		return !isNull();
	}
	~HistoryMediaPtr();

private:
	std::unique_ptr<HistoryMedia> _pointer;

};
