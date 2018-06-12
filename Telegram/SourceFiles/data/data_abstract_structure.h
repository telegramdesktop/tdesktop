/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

// This module suggests a way to hold global data structures, that are
// created on demand and deleted at the end of the app launch.
//
// Usage:
//
// class MyData : public Data::AbstractStruct { .. data .. };
// Data::GlobalStructurePointer<MyData> myData;
// .. somewhere when needed ..
// myData.createIfNull();

class AbstractStructure {
public:
	virtual ~AbstractStructure() = 0;
};
inline AbstractStructure::~AbstractStructure() = default;

namespace internal {

void registerAbstractStructure(AbstractStructure **p);

} // namespace

  // Must be created in global scope!
  // Structure is derived from AbstractStructure.
template <typename Structure>
class GlobalStructurePointer {
public:
	GlobalStructurePointer() = default;
	GlobalStructurePointer(const GlobalStructurePointer<Structure> &other) = delete;
	GlobalStructurePointer &operator=(const GlobalStructurePointer<Structure> &other) = delete;

	void createIfNull() {
		if (!_p) {
			_p = new Structure();
			internal::registerAbstractStructure(&_p);
		}
	}
	Structure *operator->() {
		Assert(_p != nullptr);
		return static_cast<Structure*>(_p);
	}
	const Structure *operator->() const {
		Assert(_p != nullptr);
		return static_cast<const Structure*>(_p);
	}
	explicit operator bool() const {
		return _p != nullptr;
	}

private:
	AbstractStructure *_p;

};

// This method should be called at the end of the app launch.
// It will destroy all data structures created by Data::GlobalStructurePointer.
void clearGlobalStructures();

} // namespace Data
