/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw::KeptBudget {

// WHY: a card scrolled back into view shows its kept picture, not a repaint.
inline constexpr auto kPictureLimit = int64(48) * 1024 * 1024;

class Acquisition;

// One distinct backing, counted from its allocation until it is dropped.
class Backing final {
public:
	Backing() = default;
	Backing(Backing &&other) noexcept;
	Backing &operator=(Backing &&other) noexcept;
	~Backing();

	[[nodiscard]] int64 bytes() const;

private:
	friend class Acquisition;

	explicit Backing(int64 bytes);

	int64 _bytes = 0;

};

// Allocations on the render thread, ordered with releases elsewhere.
class Acquisition final {
public:
	Acquisition();
	Acquisition(const Acquisition &) = delete;
	Acquisition &operator=(const Acquisition &) = delete;
	~Acquisition();

	[[nodiscard]] Backing count(const QImage &image) const;

};

// A hosted picture's pixels, queued until the chat takes them.
class Pixels final {
public:
	Pixels(const Pixels &) = delete;
	Pixels &operator=(const Pixels &) = delete;

	[[nodiscard]] static std::shared_ptr<Pixels> Make(
		QImage image,
		Backing backing);

	void dequeue() const;

private:
	friend class Acquisition;

	Pixels(QImage image, Backing backing);
	~Pixels();

	static void Release(Pixels *pixels);

	Backing _backing;
	QImage _image;
	Pixels *_deferred = nullptr;
	mutable std::atomic<int64> _queued = 0;

};

struct Totals {
	int64 queued = 0;
	int64 queuedPeak = 0;
	int64 physical = 0;
	int64 physicalPeak = 0;
};

struct Entry {
	int64 pixels = 0;
	uint64 unloaded = 0;
	uint64 work = 0;
};

struct Trim {
	std::vector<std::size_t> drop;
	int64 unloadedBytes = 0;
	int64 visibleBytes = 0;
};

[[nodiscard]] Totals Read();
[[nodiscard]] Trim PlanTrim(const std::vector<Entry> &stills);

} // namespace Tgw::KeptBudget
