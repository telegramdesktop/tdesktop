/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

class RuntimeComposer;
typedef void(*RuntimeComponentConstruct)(void *location, RuntimeComposer *composer);
typedef void(*RuntimeComponentDestruct)(void *location);
typedef void(*RuntimeComponentMove)(void *location, void *waslocation);

struct RuntimeComponentWrapStruct {
	// don't init any fields, because it is only created in
	// global scope, so it will be filled by zeros from the start
	RuntimeComponentWrapStruct() = default;
	RuntimeComponentWrapStruct(std::size_t size, std::size_t align, RuntimeComponentConstruct construct, RuntimeComponentDestruct destruct, RuntimeComponentMove move)
		: Size(size)
		, Align(align)
		, Construct(construct)
		, Destruct(destruct)
		, Move(move) {
	}
	std::size_t Size;
	std::size_t Align;
	RuntimeComponentConstruct Construct;
	RuntimeComponentDestruct Destruct;
	RuntimeComponentMove Move;
};

template <int Value, int Denominator>
struct CeilDivideMinimumOne {
	static constexpr int Result = ((Value / Denominator) + ((!Value || (Value % Denominator)) ? 1 : 0));
};

extern RuntimeComponentWrapStruct RuntimeComponentWraps[64];
extern QAtomicInt RuntimeComponentIndexLast;

template <typename Type>
struct RuntimeComponent {
	RuntimeComponent() {
		static_assert(alignof(Type) <= alignof(SmallestSizeType), "Components should align to a pointer!");
	}
	RuntimeComponent(const RuntimeComponent &other) = delete;
	RuntimeComponent &operator=(const RuntimeComponent &other) = delete;
	RuntimeComponent(RuntimeComponent &&other) = delete;
	RuntimeComponent &operator=(RuntimeComponent &&other) = default;

	static int Index() {
		static QAtomicInt _index(0);
		if (int index = _index.loadAcquire()) {
			return index - 1;
		}
		while (true) {
			int last = RuntimeComponentIndexLast.loadAcquire();
			if (RuntimeComponentIndexLast.testAndSetOrdered(last, last + 1)) {
				t_assert(last < 64);
				if (_index.testAndSetOrdered(0, last + 1)) {
					RuntimeComponentWraps[last] = RuntimeComponentWrapStruct(
						CeilDivideMinimumOne<sizeof(Type), sizeof(SmallestSizeType)>::Result * sizeof(SmallestSizeType),
						alignof(Type),
						Type::RuntimeComponentConstruct,
						Type::RuntimeComponentDestruct,
						Type::RuntimeComponentMove);
				}
				break;
			}
		}
		return _index.loadAcquire() - 1;
	}
	static uint64 Bit() {
		return (1ULL << Index());
	}

protected:
	using SmallestSizeType = void*;

	static void RuntimeComponentConstruct(void *location, RuntimeComposer *composer) {
		new (location) Type();
	}
	static void RuntimeComponentDestruct(void *location) {
		((Type*)location)->~Type();
	}
	static void RuntimeComponentMove(void *location, void *waslocation) {
		*(Type*)location = std_::move(*(Type*)waslocation);
	}

};

class RuntimeComposerMetadata {
public:
	RuntimeComposerMetadata(uint64 mask) : size(0), last(64), _mask(mask) {
		for (int i = 0; i < 64; ++i) {
			uint64 m = (1ULL << i);
			if (_mask & m) {
				int s = RuntimeComponentWraps[i].Size;
				if (s) {
					offsets[i] = size;
					size += s;
				} else {
					offsets[i] = -1;
				}
			} else if (_mask < m) {
				last = i;
				for (; i < 64; ++i) {
					offsets[i] = -1;
				}
			} else {
				offsets[i] = -1;
			}
		}
	}

	int size, last;
	int offsets[64];

	bool equals(uint64 mask) const {
		return _mask == mask;
	}
	uint64 maskadd(uint64 mask) const {
		return _mask | mask;
	}
	uint64 maskremove(uint64 mask) const {
		return _mask & (~mask);
	}

private:
	uint64 _mask;

};

const RuntimeComposerMetadata *GetRuntimeComposerMetadata(uint64 mask);

class RuntimeComposer {
public:
	RuntimeComposer(uint64 mask = 0) : _data(zerodata()) {
		if (mask) {
			const RuntimeComposerMetadata *meta = GetRuntimeComposerMetadata(mask);
			int size = sizeof(meta) + meta->size;

			auto data = operator new(size);
			t_assert(data != nullptr);

			_data = data;
			_meta() = meta;
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					try {
						auto constructAt = _dataptrunsafe(offset);
						auto space = RuntimeComponentWraps[i].Size;
						auto alignedAt = std_::align(RuntimeComponentWraps[i].Align, space, constructAt, space);
						t_assert(alignedAt == constructAt);
						RuntimeComponentWraps[i].Construct(constructAt, this);
					} catch (...) {
						while (i > 0) {
							--i;
							offset = meta->offsets[--i];
							if (offset >= 0) {
								RuntimeComponentWraps[i].Destruct(_dataptrunsafe(offset));
							}
						}
						throw;
					}
				}
			}
		}
	}
	RuntimeComposer(const RuntimeComposer &other) = delete;
	RuntimeComposer &operator=(const RuntimeComposer &other) = delete;
	~RuntimeComposer() {
		if (_data != zerodata()) {
			auto meta = _meta();
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					RuntimeComponentWraps[i].Destruct(_dataptrunsafe(offset));
				}
			}
			operator delete(_data);
		}
	}

	template <typename Type>
	bool Has() const {
		return (_meta()->offsets[Type::Index()] >= 0);
	}

	template <typename Type>
	Type *Get() {
		return static_cast<Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}
	template <typename Type>
	const Type *Get() const {
		return static_cast<const Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}

protected:
	void UpdateComponents(uint64 mask = 0) {
		if (!_meta()->equals(mask)) {
			RuntimeComposer tmp(mask);
			tmp.swap(*this);
			if (_data != zerodata() && tmp._data != zerodata()) {
				auto meta = _meta(), wasmeta = tmp._meta();
				for (int i = 0; i < meta->last; ++i) {
					int offset = meta->offsets[i], wasoffset = wasmeta->offsets[i];
					if (offset >= 0 && wasoffset >= 0) {
						RuntimeComponentWraps[i].Move(_dataptrunsafe(offset), tmp._dataptrunsafe(wasoffset));
					}
				}
			}
		}
	}
	void AddComponents(uint64 mask = 0) {
		UpdateComponents(_meta()->maskadd(mask));
	}
	void RemoveComponents(uint64 mask = 0) {
		UpdateComponents(_meta()->maskremove(mask));
	}

private:
	static const RuntimeComposerMetadata *ZeroRuntimeComposerMetadata;
	static void *zerodata() {
		return &ZeroRuntimeComposerMetadata;
	}

	void *_dataptrunsafe(int skip) const {
		return (char*)_data + sizeof(_meta()) + skip;
	}
	void *_dataptr(int skip) const {
		return (skip >= 0) ? _dataptrunsafe(skip) : 0;
	}
	const RuntimeComposerMetadata *&_meta() const {
		return *static_cast<const RuntimeComposerMetadata**>(_data);
	}
	void *_data;

	void swap(RuntimeComposer &other) {
		std::swap(_data, other._data);
	}

};
