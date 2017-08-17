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
