/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_facade.h"

#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"

namespace Storage {

class Facade::Impl {
public:
	void add(SharedMediaAddNew &&query);
	void add(SharedMediaAddExisting &&query);
	void add(SharedMediaAddSlice &&query);
	void remove(SharedMediaRemoveOne &&query);
	void remove(SharedMediaRemoveAll &&query);
	void invalidate(SharedMediaInvalidateBottom &&query);
	rpl::producer<SharedMediaResult> query(SharedMediaQuery &&query) const;
	SharedMediaResult snapshot(const SharedMediaQuery &query) const;
	bool empty(const SharedMediaKey &key) const;
	rpl::producer<SharedMediaSliceUpdate> sharedMediaSliceUpdated() const;
	rpl::producer<SharedMediaRemoveOne> sharedMediaOneRemoved() const;
	rpl::producer<SharedMediaRemoveAll> sharedMediaAllRemoved() const;
	rpl::producer<SharedMediaInvalidateBottom> sharedMediaBottomInvalidated() const;

	void add(UserPhotosAddNew &&query);
	void add(UserPhotosAddSlice &&query);
	void remove(UserPhotosRemoveOne &&query);
	void remove(UserPhotosRemoveAfter &&query);
	rpl::producer<UserPhotosResult> query(UserPhotosQuery &&query) const;
	rpl::producer<UserPhotosSliceUpdate> userPhotosSliceUpdated() const;

private:
	SharedMedia _sharedMedia;
	UserPhotos _userPhotos;

};

void Facade::Impl::add(SharedMediaAddNew &&query) {
	_sharedMedia.add(std::move(query));
}

void Facade::Impl::add(SharedMediaAddExisting &&query) {
	_sharedMedia.add(std::move(query));
}

void Facade::Impl::add(SharedMediaAddSlice &&query) {
	_sharedMedia.add(std::move(query));
}

void Facade::Impl::remove(SharedMediaRemoveOne &&query) {
	_sharedMedia.remove(std::move(query));
}

void Facade::Impl::remove(SharedMediaRemoveAll &&query) {
	_sharedMedia.remove(std::move(query));
}

void Facade::Impl::invalidate(SharedMediaInvalidateBottom &&query) {
	_sharedMedia.invalidate(std::move(query));
}

rpl::producer<SharedMediaResult> Facade::Impl::query(SharedMediaQuery &&query) const {
	return _sharedMedia.query(std::move(query));
}

SharedMediaResult Facade::Impl::snapshot(const SharedMediaQuery &query) const {
	return _sharedMedia.snapshot(query);
}

bool Facade::Impl::empty(const SharedMediaKey &key) const {
	return _sharedMedia.empty(key);
}

rpl::producer<SharedMediaSliceUpdate> Facade::Impl::sharedMediaSliceUpdated() const {
	return _sharedMedia.sliceUpdated();
}

rpl::producer<SharedMediaRemoveOne> Facade::Impl::sharedMediaOneRemoved() const {
	return _sharedMedia.oneRemoved();
}

rpl::producer<SharedMediaRemoveAll> Facade::Impl::sharedMediaAllRemoved() const {
	return _sharedMedia.allRemoved();
}

rpl::producer<SharedMediaInvalidateBottom> Facade::Impl::sharedMediaBottomInvalidated() const {
	return _sharedMedia.bottomInvalidated();
}

void Facade::Impl::add(UserPhotosAddNew &&query) {
	return _userPhotos.add(std::move(query));
}

void Facade::Impl::add(UserPhotosAddSlice &&query) {
	return _userPhotos.add(std::move(query));
}

void Facade::Impl::remove(UserPhotosRemoveOne &&query) {
	return _userPhotos.remove(std::move(query));
}

void Facade::Impl::remove(UserPhotosRemoveAfter &&query) {
	return _userPhotos.remove(std::move(query));
}

rpl::producer<UserPhotosResult> Facade::Impl::query(UserPhotosQuery &&query) const {
	return _userPhotos.query(std::move(query));
}

rpl::producer<UserPhotosSliceUpdate> Facade::Impl::userPhotosSliceUpdated() const {
	return _userPhotos.sliceUpdated();
}

Facade::Facade() : _impl(std::make_unique<Impl>()) {
}

void Facade::add(SharedMediaAddNew &&query) {
	_impl->add(std::move(query));
}

void Facade::add(SharedMediaAddExisting &&query) {
	_impl->add(std::move(query));
}

void Facade::add(SharedMediaAddSlice &&query) {
	_impl->add(std::move(query));
}

void Facade::remove(SharedMediaRemoveOne &&query) {
	_impl->remove(std::move(query));
}

void Facade::remove(SharedMediaRemoveAll &&query) {
	_impl->remove(std::move(query));
}

void Facade::invalidate(SharedMediaInvalidateBottom &&query) {
	_impl->invalidate(std::move(query));
}

rpl::producer<SharedMediaResult> Facade::query(SharedMediaQuery &&query) const {
	return _impl->query(std::move(query));
}

SharedMediaResult Facade::snapshot(const SharedMediaQuery &query) const {
	return _impl->snapshot(query);
}

bool Facade::empty(const SharedMediaKey &key) const {
	return _impl->empty(key);
}

rpl::producer<SharedMediaSliceUpdate> Facade::sharedMediaSliceUpdated() const {
	return _impl->sharedMediaSliceUpdated();
}

rpl::producer<SharedMediaRemoveOne> Facade::sharedMediaOneRemoved() const {
	return _impl->sharedMediaOneRemoved();
}

rpl::producer<SharedMediaRemoveAll> Facade::sharedMediaAllRemoved() const {
	return _impl->sharedMediaAllRemoved();
}

rpl::producer<SharedMediaInvalidateBottom> Facade::sharedMediaBottomInvalidated() const {
	return _impl->sharedMediaBottomInvalidated();
}

void Facade::add(UserPhotosAddNew &&query) {
	return _impl->add(std::move(query));
}

void Facade::add(UserPhotosAddSlice &&query) {
	return _impl->add(std::move(query));
}

void Facade::remove(UserPhotosRemoveOne &&query) {
	return _impl->remove(std::move(query));
}

void Facade::remove(UserPhotosRemoveAfter &&query) {
	return _impl->remove(std::move(query));
}

rpl::producer<UserPhotosResult> Facade::query(UserPhotosQuery &&query) const {
	return _impl->query(std::move(query));
}

rpl::producer<UserPhotosSliceUpdate> Facade::userPhotosSliceUpdated() const {
	return _impl->userPhotosSliceUpdated();
}

Facade::~Facade() = default;

} // namespace Storage
