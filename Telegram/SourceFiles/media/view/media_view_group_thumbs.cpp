/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_group_thumbs.h"

#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_media.h"
#include "auth_session.h"
#include "styles/style_mediaview.h"

namespace Media {
namespace View {
namespace {

constexpr auto kThumbDuration = TimeMs(150);

int Round(float64 value) {
	return int(std::round(value));
}

using Context = GroupThumbs::Context;
using Key = GroupThumbs::Key;

Context ComputeContext(const SharedMediaWithLastSlice &slice, int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = base::get_if<not_null<PhotoData*>>(&value)) {
		if (const auto peer = (*photo)->peer) {
			return peer->id;
		}
		return base::none;
	} else if (const auto msgId = base::get_if<FullMsgId>(&value)) {
		if (const auto item = App::histItemById(*msgId)) {
			if (!item->toHistoryMessage()) {
				return item->history()->peer->id;
			} else if (const auto groupId = item->groupId()) {
				return groupId;
			}
		}
		return base::none;
	}
	Unexpected("Variant in ComputeContext(SharedMediaWithLastSlice::Value)");
}

Context ComputeContext(const UserPhotosSlice &slice, int index) {
	return peerFromUser(slice.key().userId);
}

Key ComputeKey(const SharedMediaWithLastSlice &slice, int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = base::get_if<not_null<PhotoData*>>(&value)) {
		return (*photo)->id;
	} else if (const auto msgId = base::get_if<FullMsgId>(&value)) {
		return *msgId;
	}
	Unexpected("Variant in ComputeContext(SharedMediaWithLastSlice::Value)");
}

Key ComputeKey(const UserPhotosSlice &slice, int index) {
	return slice[index];
}

int ComputeThumbsLimit(int availableWidth) {
	const auto singleWidth = st::mediaviewGroupWidth
		+ 2 * st::mediaviewGroupSkip;
	const auto currentWidth = st::mediaviewGroupWidthMax
		+ 2 * st::mediaviewGroupSkipCurrent;
	const auto skipForAnimation = 2 * singleWidth;
	const auto leftWidth = availableWidth
		- currentWidth
		- skipForAnimation;
	return std::max(leftWidth / (2 * singleWidth), 1);
}

} // namespace

class GroupThumbs::Thumb {
public:
	enum class State {
		Unknown,
		Current,
		Alive,
		Dying,
	};

	Thumb(Key key, ImagePtr image, Fn<void()> handler);

	int leftToUpdate() const;
	int rightToUpdate() const;

	void animateToLeft(not_null<Thumb*> next);
	void animateToRight(not_null<Thumb*> prev);

	void setState(State state);
	State state() const;
	bool removed() const;

	void paint(Painter &p, int x, int y, int outerWidth, float64 progress);
	ClickHandlerPtr getState(QPoint point) const;

private:
	QSize wantedPixSize() const;
	void validateImage();
	int currentLeft() const;
	int currentWidth() const;
	int finalLeft() const;
	int finalWidth() const;
	void animateTo(int left, int width);

	ClickHandlerPtr _link;
	const Key _key;
	ImagePtr _image;
	State _state = State::Alive;
	QPixmap _full;
	int _fullWidth = 0;
	bool _hiding = false;

	anim::value _left = { 0. };
	anim::value _width = { 0. };
	anim::value _opacity = { 0., 1. };

};

GroupThumbs::Thumb::Thumb(
	Key key,
	ImagePtr image,
	Fn<void()> handler)
: _key(key)
, _image(image) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	_fullWidth = std::min(
		wantedPixSize().width(),
		st::mediaviewGroupWidthMax);
	validateImage();
}

QSize GroupThumbs::Thumb::wantedPixSize() const {
	const auto originalWidth = std::max(_image->width(), 1);
	const auto originalHeight = std::max(_image->height(), 1);
	const auto pixHeight = st::mediaviewGroupHeight;
	const auto pixWidth = originalWidth * pixHeight / originalHeight;
	return { pixWidth, pixHeight };
}

void GroupThumbs::Thumb::validateImage() {
	if (!_full.isNull()) {
		return;
	}
	_image->load();
	if (!_image->loaded()) {
		return;
	}

	const auto pixSize = wantedPixSize();
	if (pixSize.width() > st::mediaviewGroupWidthMax) {
		const auto originalWidth = _image->width();
		const auto originalHeight = _image->height();
		const auto takeWidth = originalWidth * st::mediaviewGroupWidthMax
			/ pixSize.width();
		const auto original = _image->pixNoCache().toImage();
		_full = App::pixmapFromImageInPlace(original.copy(
			(originalWidth - takeWidth) / 2,
			0,
			takeWidth,
			originalHeight
		).scaled(
			st::mediaviewGroupWidthMax * cIntRetinaFactor(),
			pixSize.height() * cIntRetinaFactor(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation));
	} else {
		_full = _image->pixNoCache(
			pixSize.width() * cIntRetinaFactor(),
			pixSize.height() * cIntRetinaFactor(),
			Images::Option::Smooth);
	}
}

int GroupThumbs::Thumb::leftToUpdate() const {
	return Round(std::min(_left.from(), _left.to()));
}

int GroupThumbs::Thumb::rightToUpdate() const {
	return Round(std::max(
		_left.from() + _width.from(),
		_left.to() + _width.to()));
}

int GroupThumbs::Thumb::currentLeft() const {
	return Round(_left.current());
}

int GroupThumbs::Thumb::currentWidth() const {
	return Round(_width.current());
}

int GroupThumbs::Thumb::finalLeft() const {
	return Round(_left.to());
}

int GroupThumbs::Thumb::finalWidth() const {
	return Round(_width.to());
}

void GroupThumbs::Thumb::setState(State state) {
	const auto isNewThumb = (_state == State::Alive);
	_state = state;
	if (_state == State::Current) {
		if (isNewThumb) {
			_opacity = anim::value(1.);
			_left = anim::value(-_fullWidth / 2);
			_width = anim::value(_fullWidth);
		} else {
			_opacity.start(1.);
		}
		_hiding = false;
		animateTo(-_fullWidth / 2, _fullWidth);
	} else if (_state == State::Alive) {
		_opacity.start(0.7);
		_hiding = false;
	} else if (_state == State::Dying) {
		_opacity.start(0.);
		_hiding = true;
		_left.restart();
		_width.restart();
	}
}

void GroupThumbs::Thumb::animateTo(int left, int width) {
	_left.start(left);
	_width.start(width);
}

void GroupThumbs::Thumb::animateToLeft(not_null<Thumb*> next) {
	const auto width = st::mediaviewGroupWidth;
	if (_state == State::Alive) {
		// New item animation, start exactly from the next, move only.
		_left = anim::value(next->currentLeft() - width);
		_width = anim::value(width);
	} else if (_state == State::Unknown) {
		// Existing item animation.
		setState(State::Alive);
	}
	const auto skip1 = st::mediaviewGroupSkip;
	const auto skip2 = (next->state() == State::Current)
		? st::mediaviewGroupSkipCurrent
		: st::mediaviewGroupSkip;
	animateTo(next->finalLeft() - width - skip1 - skip2, width);
}

void GroupThumbs::Thumb::animateToRight(not_null<Thumb*> prev) {
	const auto width = st::mediaviewGroupWidth;
	if (_state == State::Alive) {
		// New item animation, start exactly from the next, move only.
		_left = anim::value(prev->currentLeft() + prev->currentWidth());
		_width = anim::value(width);
	} else if (_state == State::Unknown) {
		// Existing item animation.
		setState(State::Alive);
	}
	const auto skip1 = st::mediaviewGroupSkip;
	const auto skip2 = (prev->state() == State::Current)
		? st::mediaviewGroupSkipCurrent
		: st::mediaviewGroupSkip;
	animateTo(prev->finalLeft() + prev->finalWidth() + skip1 + skip2, width);
}

auto GroupThumbs::Thumb::state() const -> State {
	return _state;
}

bool GroupThumbs::Thumb::removed() const {
	return (_state == State::Dying) && _hiding && !_opacity.current();
}

void GroupThumbs::Thumb::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		float64 progress) {
	validateImage();

	_opacity.update(progress, anim::linear);
	_left.update(progress, anim::linear);
	_width.update(progress, anim::linear);

	const auto left = x + currentLeft();
	const auto width = currentWidth();
	const auto opacity = p.opacity();
	p.setOpacity(_opacity.current() * opacity);
	if (width == _fullWidth) {
		p.drawPixmap(left, y, _full);
	} else {
		const auto takeWidth = width * cIntRetinaFactor();
		const auto from = QRect(
			(_full.width() - takeWidth) / 2,
			0,
			takeWidth,
			_full.height());
		const auto to = QRect(left, y, width, st::mediaviewGroupHeight);
		p.drawPixmap(to, _full, from);
	}
	p.setOpacity(opacity);
}

ClickHandlerPtr GroupThumbs::Thumb::getState(QPoint point) const {
	if (_state != State::Alive) {
		return nullptr;
	}
	const auto left = finalLeft();
	const auto width = finalWidth();
	return QRect(left, 0, width, st::mediaviewGroupHeight).contains(point)
		? _link
		: nullptr;
}

GroupThumbs::GroupThumbs(Context context)
: _context(context) {
}

void GroupThumbs::updateContext(Context context) {
	if (_context != context) {
		clear();
		_context = context;
	}
}

template <typename Slice>
void GroupThumbs::RefreshFromSlice(
		std::unique_ptr<GroupThumbs> &instance,
		const Slice &slice,
		int index,
		int availableWidth) {
	const auto context = ComputeContext(slice, index);
	if (instance) {
		instance->updateContext(context);
	}
	if (!context) {
		if (instance) {
			instance->resizeToWidth(availableWidth);
		}
		return;
	}
	const auto limit = ComputeThumbsLimit(availableWidth);
	const auto from = [&] {
		const auto edge = std::max(index - limit, 0);
		for (auto result = index; result != edge; --result) {
			if (ComputeContext(slice, result - 1) != context) {
				return result;
			}
		}
		return edge;
	}();
	const auto till = [&] {
		const auto edge = std::min(index + limit + 1, slice.size());
		for (auto result = index + 1; result != edge; ++result) {
			if (ComputeContext(slice, result) != context) {
				return result;
			}
		}
		return edge;
	}();
	if (from + 1 < till) {
		if (!instance) {
			instance = std::make_unique<GroupThumbs>(context);
		}
		instance->fillItems(slice, from, index, till);
		instance->resizeToWidth(availableWidth);
	} else if (instance) {
		instance->clear();
		instance->resizeToWidth(availableWidth);
	}
}

template <typename Slice>
void GroupThumbs::fillItems(
		const Slice &slice,
		int from,
		int index,
		int till) {
	Expects(from <= index);
	Expects(index < till);
	Expects(from + 1 < till);


	const auto current = (index - from);
	const auto old = base::take(_items);

	markCacheStale();
	_items.reserve(till - from);
	for (auto i = from; i != till; ++i) {
		_items.push_back(validateCacheEntry(ComputeKey(slice, i)));
	}
	animateAliveItems(current);
	fillDyingItems(old);
	startDelayedAnimation();
}

void GroupThumbs::animateAliveItems(int current) {
	Expects(current >= 0 && current < _items.size());

	_items[current]->setState(Thumb::State::Current);
	for (auto i = current; i != 0;) {
		const auto prev = _items[i];
		const auto item = _items[--i];
		item->animateToLeft(prev);
	}
	for (auto i = current + 1; i != _items.size(); ++i) {
		const auto prev = _items[i - 1];
		const auto item = _items[i];
		item->animateToRight(prev);
	}
}

void GroupThumbs::fillDyingItems(const std::vector<not_null<Thumb*>> &old) {
	_dying.reserve(_cache.size() - _items.size());
	animatePreviouslyAlive(old);
	markRestAsDying();
}

void GroupThumbs::markRestAsDying() {
	_dying.reserve(_cache.size() - _items.size());
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		const auto state = thumb->state();
		if (state == Thumb::State::Unknown) {
			markAsDying(thumb.get());
		}
	}
}

void GroupThumbs::markAsDying(not_null<Thumb*> thumb) {
	thumb->setState(Thumb::State::Dying);
	_dying.push_back(thumb.get());
}

void GroupThumbs::animatePreviouslyAlive(
		const std::vector<not_null<Thumb*>> &old) {
	auto toRight = false;
	for (auto i = 0; i != old.size(); ++i) {
		const auto item = old[i];
		if (item->state() == Thumb::State::Unknown) {
			if (toRight) {
				markAsDying(item);
				item->animateToRight(old[i - 1]);
			}
		} else if (!toRight) {
			for (auto j = i; j != 0;) {
				const auto next = old[j];
				const auto prev = old[--j];
				markAsDying(prev);
				prev->animateToLeft(next);
			}
			toRight = true;
		}
	}
}

auto GroupThumbs::createThumb(Key key) -> std::unique_ptr<Thumb> {
	if (const auto photoId = base::get_if<PhotoId>(&key)) {
		const auto photo = Auth().data().photo(*photoId);
		return createThumb(key, photo->date ? photo->thumb : ImagePtr());
	} else if (const auto msgId = base::get_if<FullMsgId>(&key)) {
		if (const auto item = App::histItemById(*msgId)) {
			if (const auto media = item->media()) {
				if (const auto photo = media->photo()) {
					return createThumb(key, photo->thumb);
				} else if (const auto document = media->document()) {
					return createThumb(key, document->thumb);
				}
			}
		}
		return createThumb(key, ImagePtr());
	}
	Unexpected("Value of Key in GroupThumbs::createThumb()");
}

auto GroupThumbs::createThumb(Key key, ImagePtr image)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	return std::make_unique<Thumb>(key, image, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::validateCacheEntry(Key key) -> not_null<Thumb*> {
	const auto i = _cache.find(key);
	return (i != _cache.end())
		? i->second.get()
		: _cache.emplace(key, createThumb(key)).first->second.get();
}

void GroupThumbs::markCacheStale() {
	_dying.clear();
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		thumb->setState(Thumb::State::Unknown);
	}
}

void GroupThumbs::Refresh(
		std::unique_ptr<GroupThumbs> &instance,
		const SharedMediaWithLastSlice &slice,
		int index,
		int availableWidth) {
	RefreshFromSlice(instance, slice, index, availableWidth);
}

void GroupThumbs::Refresh(
		std::unique_ptr<GroupThumbs> &instance,
		const UserPhotosSlice &slice,
		int index,
		int availableWidth) {
	RefreshFromSlice(instance, slice, index, availableWidth);
}

void GroupThumbs::clear() {
	if (_items.empty()) {
		return;
	}
	base::take(_items);
	markCacheStale();
	markRestAsDying();
	startDelayedAnimation();
}

void GroupThumbs::startDelayedAnimation() {
	_animation.finish();
	_waitingForAnimationStart = true;
	countUpdatedRect();
}

void GroupThumbs::resizeToWidth(int newWidth) {
	_width = newWidth;
}

int GroupThumbs::height() const {
	return st::mediaviewGroupPadding.top()
		+ st::mediaviewGroupHeight
		+ st::mediaviewGroupPadding.bottom();
}

bool GroupThumbs::hiding() const {
	return _items.empty();
}

bool GroupThumbs::hidden() const {
	return hiding() && !_waitingForAnimationStart && !_animation.animating();
}

void GroupThumbs::checkForAnimationStart() {
	if (_waitingForAnimationStart) {
		_waitingForAnimationStart = false;
		_animation.start([this] { update(); }, 0., 1., kThumbDuration);
	}
}

void GroupThumbs::update() {
	if (_cache.empty()) {
		return;
	}
	_updateRequests.fire_copy(_updatedRect);
}

void GroupThumbs::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		TimeMs ms) {
	const auto progress = _waitingForAnimationStart
		? 0.
		: _animation.current(ms, 1.);
	x += (_width / 2);
	y += st::mediaviewGroupPadding.top();
	for (auto i = _cache.begin(); i != _cache.end();) {
		const auto &thumb = i->second;
		thumb->paint(p, x, y, outerWidth, progress);
		if (thumb->removed()) {
			_dying.erase(
				ranges::remove(
					_dying,
					thumb.get(),
					[](not_null<Thumb*> thumb) { return thumb.get(); }),
				_dying.end());
			i = _cache.erase(i);
		} else {
			++i;
		}
	}
}

ClickHandlerPtr GroupThumbs::getState(QPoint point) const {
	point -= QPoint((_width / 2), st::mediaviewGroupPadding.top());
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		if (auto link = thumb->getState(point)) {
			return link;
		}
	}
	return nullptr;
}

void GroupThumbs::countUpdatedRect() {
	if (_cache.empty()) {
		return;
	}
	auto min = _width;
	auto max = 0;
	const auto left = [](const auto &cacheItem) {
		const auto &[key, thumb] = cacheItem;
		return thumb->leftToUpdate();
	};
	const auto right = [](const auto &cacheItem) {
		const auto &[key, thumb] = cacheItem;
		return thumb->rightToUpdate();
	};
	accumulate_min(min, left(*ranges::max_element(
		_cache,
		std::greater<>(),
		left)));
	accumulate_max(max, right(*ranges::max_element(
		_cache,
		std::less<>(),
		right)));
	_updatedRect = QRect(
		min,
		st::mediaviewGroupPadding.top(),
		max - min,
		st::mediaviewGroupHeight);
}

GroupThumbs::~GroupThumbs() = default;

} // namespace View
} // namespace Media
