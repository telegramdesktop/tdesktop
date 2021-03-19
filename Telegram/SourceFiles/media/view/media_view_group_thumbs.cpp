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
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_file_origin.h"
#include "history/history.h"
#include "history/view/media/history_view_media.h"
#include "ui/image/image.h"
#include "main/main_session.h"
#include "core/crash_reports.h"
#include "app.h"
#include "styles/style_media_view.h"

namespace Media {
namespace View {
namespace {

constexpr auto kThumbDuration = crl::time(150);

int Round(float64 value) {
	return int(std::round(value));
}

using Context = GroupThumbs::Context;
using Key = GroupThumbs::Key;

[[nodiscard]] QString DebugSerializeMsgId(FullMsgId itemId) {
	return QString("msg%1_%2").arg(itemId.channel).arg(itemId.msg);
}

[[nodiscard]] QString DebugSerializePeer(PeerId peerId) {
	return peerIsUser(peerId)
		? QString("user%1").arg(peerToUser(peerId))
		: peerIsChat(peerId)
		? QString("chat%1").arg(peerToChat(peerId))
		: QString("channel%1").arg(peerToChannel(peerId));
}

[[nodiscard]] QString DebugSerializeKey(const Key &key) {
	return v::match(key, [&](PhotoId photoId) {
		return QString("photo%1").arg(photoId);
	}, [](FullMsgId itemId) {
		return DebugSerializeMsgId(itemId);
	}, [&](GroupThumbs::CollageKey key) {
		return QString("collage%1").arg(key.index);
	});
}

[[nodiscard]] QString DebugSerializeContext(const Context &context) {
	return v::match(context, [](PeerId peerId) {
		return DebugSerializePeer(peerId);
	}, [](MessageGroupId groupId) {
		return QString("group_%1_%2"
		).arg(DebugSerializePeer(groupId.peer)
		).arg(groupId.value);
	}, [](FullMsgId item) {
		return DebugSerializeMsgId(item);
	}, [](v::null_t) -> QString {
		return "null";
	});
}

Data::FileOrigin ComputeFileOrigin(const Key &key, const Context &context) {
	return v::match(key, [&](PhotoId photoId) {
		return v::match(context, [&](PeerId peerId) {
			return peerIsUser(peerId)
				? Data::FileOriginUserPhoto(peerToUser(peerId), photoId)
				: Data::FileOrigin(Data::FileOriginPeerPhoto(peerId));
		}, [](auto&&) {
			return Data::FileOrigin();
		});
	}, [](FullMsgId itemId) {
		return Data::FileOrigin(itemId);
	}, [&](GroupThumbs::CollageKey) {
		return v::match(context, [](const GroupThumbs::CollageSlice &slice) {
			return Data::FileOrigin(slice.context);
		}, [](auto&&) {
			return Data::FileOrigin();
		});
	});
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const SharedMediaWithLastSlice &slice,
		int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		if (const auto peer = (*photo)->peer) {
			return peer->id;
		}
		return v::null;
	} else if (const auto msgId = std::get_if<FullMsgId>(&value)) {
		if (const auto item = session->data().message(*msgId)) {
			if (!item->toHistoryMessage()) {
				return item->history()->peer->id;
			} else if (const auto groupId = item->groupId()) {
				return groupId;
			}
		}
		return v::null;
	}
	Unexpected("Variant in ComputeContext(SharedMediaWithLastSlice::Value)");
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const UserPhotosSlice &slice,
		int index) {
	return peerFromUser(slice.key().userId);
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const GroupThumbs::CollageSlice &slice,
		int index) {
	return slice.context;
}

Key ComputeKey(const SharedMediaWithLastSlice &slice, int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		return (*photo)->id;
	} else if (const auto msgId = std::get_if<FullMsgId>(&value)) {
		return *msgId;
	}
	Unexpected("Variant in ComputeKey(SharedMediaWithLastSlice::Value)");
}

Key ComputeKey(const UserPhotosSlice &slice, int index) {
	return slice[index];
}

Key ComputeKey(const GroupThumbs::CollageSlice &slice, int index) {
	return GroupThumbs::CollageKey{ index };
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

	Thumb(Key key, Fn<void()> handler);
	Thumb(
		Key key,
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> handler);
	Thumb(
		Key key,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		Fn<void()> handler);

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
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	Image *_image = nullptr;
	Data::FileOrigin _origin;
	State _state = State::Alive;
	QPixmap _full;
	int _fullWidth = 0;
	bool _hiding = false;

	anim::value _left = { 0. };
	anim::value _width = { 0. };
	anim::value _opacity = { 0., 1. };

};

GroupThumbs::Thumb::Thumb(Key key, Fn<void()> handler)
: _key(key) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	validateImage();
}

GroupThumbs::Thumb::Thumb(
	Key key,
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	Fn<void()> handler)
: _key(key)
, _photoMedia(photo->createMediaView())
, _origin(origin) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	_photoMedia->wanted(Data::PhotoSize::Thumbnail, origin);
	validateImage();
}

GroupThumbs::Thumb::Thumb(
	Key key,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	Fn<void()> handler)
: _key(key)
, _documentMedia(document->createMediaView())
, _origin(origin) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	_documentMedia->thumbnailWanted(origin);
	validateImage();
}

QSize GroupThumbs::Thumb::wantedPixSize() const {
	const auto originalWidth = _image ? std::max(_image->width(), 1) : 1;
	const auto originalHeight = _image ? std::max(_image->height(), 1) : 1;
	const auto pixHeight = st::mediaviewGroupHeight;
	const auto pixWidth = originalWidth * pixHeight / originalHeight;
	return { pixWidth, pixHeight };
}

void GroupThumbs::Thumb::validateImage() {
	if (!_image) {
		if (_photoMedia) {
			_image = _photoMedia->image(Data::PhotoSize::Thumbnail);
		} else if (_documentMedia) {
			_image = _documentMedia->thumbnail();
		}
	}
	if (!_full.isNull() || !_image) {
		return;
	}

	const auto pixSize = wantedPixSize();
	if (pixSize.width() > st::mediaviewGroupWidthMax) {
		const auto originalWidth = _image->width();
		const auto originalHeight = _image->height();
		const auto takeWidth = originalWidth * st::mediaviewGroupWidthMax
			/ pixSize.width();
		auto original = _image->original();
		original.setDevicePixelRatio(cRetinaFactor());
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
	_fullWidth = std::min(
		wantedPixSize().width(),
		st::mediaviewGroupWidthMax);
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

int GroupThumbs::CollageSlice::size() const {
	return data->items.size();
}

GroupThumbs::GroupThumbs(not_null<Main::Session*> session, Context context)
: _session(session)
, _context(context) {
}

void GroupThumbs::updateContext(Context context) {
	if (_context != context) {
		clear();
		_context = context;
	}
}

template <typename Slice>
void GroupThumbs::RefreshFromSlice(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const Slice &slice,
		int index,
		int availableWidth) {
	const auto context = ComputeContext(session, slice, index);
	if (instance) {
		instance->updateContext(context);
	}
	if (v::is_null(context)) {
		if (instance) {
			instance->resizeToWidth(availableWidth);
		}
		return;
	}
	const auto limit = ComputeThumbsLimit(availableWidth);
	const auto from = [&] {
		const auto edge = std::max(index - limit, 0);
		for (auto result = index; result != edge; --result) {
			if (ComputeContext(session, slice, result - 1) != context) {
				return result;
			}
		}
		return edge;
	}();
	const auto till = [&] {
		const auto edge = std::min(index + limit + 1, slice.size());
		for (auto result = index + 1; result != edge; ++result) {
			if (ComputeContext(session, slice, result) != context) {
				return result;
			}
		}
		return edge;
	}();
	if (from + 1 < till) {
		if (!instance) {
			instance = std::make_unique<GroupThumbs>(session, context);
		}
		instance->fillItems(slice, from, index, till);
		instance->resizeToWidth(availableWidth);
	} else if (instance) {
		instance->clear();
		instance->resizeToWidth(availableWidth);
	}
}

template <typename Slice>
void ValidateSlice(
		const Slice &slice,
		const Context &context,
		int from,
		int index,
		int till) {
	auto keys = base::flat_set<Key>();
	for (auto i = from; i != till; ++i) {
		const auto key = ComputeKey(slice, i);
		if (keys.contains(key)) {
			// All items should be unique!
			auto strings = QStringList();
			strings.reserve(till - from);
			for (auto i = from; i != till; ++i) {
				strings.push_back(DebugSerializeKey(ComputeKey(slice, i)));
			}
			CrashReports::SetAnnotation(
				"keys",
				QString("%1:%2-(%3)-%4:"
				).arg(DebugSerializeContext(context)
				).arg(from
				).arg(index
				).arg(till) + strings.join(","));
			if (Logs::DebugEnabled()) {
				Unexpected("Bad slice in GroupThumbs.");
			}
			break;
		} else {
			keys.emplace(key);
		}
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

	//ValidateSlice(slice, _context, from, index, till);

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
	//Expects(_cache.size() >= _items.size());

	if (_cache.size() >= _items.size()) {
		_dying.reserve(_cache.size() - _items.size());
	}
	animatePreviouslyAlive(old);
	markRestAsDying();
}

void GroupThumbs::markRestAsDying() {
	//Expects(_cache.size() >= _items.size());

	if (_cache.size() >= _items.size()) {
		_dying.reserve(_cache.size() - _items.size());
	}
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

auto GroupThumbs::createThumb(Key key)
-> std::unique_ptr<Thumb> {
	if (const auto photoId = std::get_if<PhotoId>(&key)) {
		const auto photo = _session->data().photo(*photoId);
		return createThumb(key, photo);
	} else if (const auto msgId = std::get_if<FullMsgId>(&key)) {
		if (const auto item = _session->data().message(*msgId)) {
			if (const auto media = item->media()) {
				if (const auto photo = media->photo()) {
					return createThumb(key, photo);
				} else if (const auto document = media->document()) {
					return createThumb(key, document);
				}
			}
		}
		return createThumb(key, nullptr);
	} else if (const auto collageKey = std::get_if<CollageKey>(&key)) {
		if (const auto itemId = std::get_if<FullMsgId>(&_context)) {
			if (const auto item = _session->data().message(*itemId)) {
				if (const auto media = item->media()) {
					if (const auto page = media->webpage()) {
						return createThumb(
							key,
							page->collage,
							collageKey->index);
					}
				}
			}
		}
		return createThumb(key, nullptr);
	}
	Unexpected("Value of Key in GroupThumbs::createThumb()");
}

auto GroupThumbs::createThumb(
	Key key,
	const WebPageCollage &collage,
	int index)
-> std::unique_ptr<Thumb> {
	if (index < 0 || index >= collage.items.size()) {
		return createThumb(key, nullptr);
	}
	const auto &item = collage.items[index];
	if (const auto photo = std::get_if<PhotoData*>(&item)) {
		return createThumb(key, (*photo));
	} else if (const auto document = std::get_if<DocumentData*>(&item)) {
		return createThumb(key, (*document));
	}
	return createThumb(key, nullptr);
}

auto GroupThumbs::createThumb(Key key, std::nullptr_t)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::createThumb(Key key, not_null<PhotoData*> photo)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, photo, origin, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::createThumb(Key key, not_null<DocumentData*> document)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, document, origin, [=] {
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
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const SharedMediaWithLastSlice &slice,
		int index,
		int availableWidth) {
	RefreshFromSlice(session, instance, slice, index, availableWidth);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const UserPhotosSlice &slice,
		int index,
		int availableWidth) {
	RefreshFromSlice(session, instance, slice, index, availableWidth);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const CollageSlice &slice,
		int index,
		int availableWidth) {
	RefreshFromSlice(session, instance, slice, index, availableWidth);
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
	_animation.stop();
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
		_animation.start([=] { update(); }, 0., 1., kThumbDuration);
	}
}

void GroupThumbs::update() {
	if (_cache.empty()) {
		return;
	}
	_updateRequests.fire_copy(_updatedRect);
}

void GroupThumbs::paint(Painter &p, int x, int y, int outerWidth) {
	const auto progress = _waitingForAnimationStart
		? 0.
		: _animation.value(1.);
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
