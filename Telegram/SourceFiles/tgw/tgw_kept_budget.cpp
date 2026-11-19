/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_kept_budget.h"

#include "tgw/tgw_render_thread.h"

#include <atomic>
#include <mutex>

namespace Tgw::KeptBudget {
namespace {

std::atomic<int64> Physical = 0;
std::atomic<int64> PhysicalPeak = 0;
std::atomic<int64> Queued = 0;
std::atomic<int64> QueuedPeak = 0;
std::mutex Ownership;
Pixels *Deferred = nullptr;
bool Acquiring = false;

void Raise(std::atomic<int64> &peak, int64 value) {
	auto was = peak.load();
	while ((was < value) && !peak.compare_exchange_weak(was, value)) {
	}
}

void Add(std::atomic<int64> &total, std::atomic<int64> &peak, int64 by) {
	Raise(peak, total.fetch_add(by) + by);
}

} // namespace

Backing::Backing(int64 bytes) : _bytes(std::max(bytes, int64())) {
	Add(Physical, PhysicalPeak, _bytes);
}

Backing::Backing(Backing &&other) noexcept
: _bytes(std::exchange(other._bytes, 0)) {
}

Backing &Backing::operator=(Backing &&other) noexcept {
	if (this != &other) {
		Physical.fetch_sub(
			std::exchange(_bytes, std::exchange(other._bytes, 0)));
	}
	return *this;
}

Backing::~Backing() {
	Physical.fetch_sub(_bytes);
}

int64 Backing::bytes() const {
	return _bytes;
}

Acquisition::Acquisition() {
	const auto lock = std::lock_guard(Ownership);
	Acquiring = true;
}

Acquisition::~Acquisition() {
	auto deferred = [] {
		const auto lock = std::lock_guard(Ownership);
		Acquiring = false;
		return std::exchange(Deferred, nullptr);
	}();
	while (deferred) {
		delete std::exchange(deferred, deferred->_deferred);
	}
}

Backing Acquisition::count(const QImage &image) const {
	return Backing(image.isNull() ? int64() : int64(image.sizeInBytes()));
}

Pixels::Pixels(QImage image, Backing backing)
: _backing(std::move(backing))
, _image(std::move(image))
, _queued(_backing.bytes()) {
	Add(Queued, QueuedPeak, _queued);
}

Pixels::~Pixels() {
	dequeue();
}

std::shared_ptr<Pixels> Pixels::Make(QImage image, Backing backing) {
	return std::shared_ptr<Pixels>(
		new Pixels(std::move(image), std::move(backing)),
		&Pixels::Release);
}

void Pixels::Release(Pixels *pixels) {
	if (OnRenderThread()) {
		delete pixels;
		return;
	}
	const auto lock = std::lock_guard(Ownership);
	if (Acquiring) {
		pixels->_deferred = std::exchange(Deferred, pixels);
	} else {
		delete pixels;
	}
}

void Pixels::dequeue() const {
	Queued.fetch_sub(_queued.exchange(0));
}

Totals Read() {
	return {
		.queued = Queued.load(),
		.queuedPeak = QueuedPeak.load(),
		.physical = Physical.load(),
		.physicalPeak = PhysicalPeak.load(),
	};
}

Trim PlanTrim(const std::vector<Entry> &stills) {
	auto result = Trim();
	auto candidates = std::vector<std::size_t>();
	for (auto i = std::size_t(); i != stills.size(); ++i) {
		const auto &entry = stills[i];
		const auto pixels = std::max(entry.pixels, int64());
		if (!entry.unloaded) {
			result.visibleBytes += pixels;
		} else if (pixels > 0) {
			result.unloadedBytes += pixels;
			candidates.push_back(i);
		}
	}
	auto total = result.unloadedBytes;
	if (total <= kPictureLimit) {
		return result;
	}
	ranges::sort(candidates, [&](std::size_t a, std::size_t b) {
		const auto &left = stills[a];
		const auto &right = stills[b];
		return (left.unloaded != right.unloaded)
			? (left.unloaded < right.unloaded)
			: (left.work < right.work);
	});
	for (const auto index : candidates) {
		result.drop.push_back(index);
		total -= stills[index].pixels;
		if (total <= kPictureLimit) {
			break;
		}
	}
	return result;
}

} // namespace Tgw::KeptBudget
