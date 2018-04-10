/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_abstract_structure.h"

namespace Data {
namespace {

using DataStructures = OrderedSet<AbstractStructure**>;
NeverFreedPointer<DataStructures> structures;

} // namespace

namespace internal {

void registerAbstractStructure(AbstractStructure **p) {
	structures.createIfNull();
	structures->insert(p);
}

} // namespace internal

void clearGlobalStructures() {
	if (!structures) return;
	for (auto &p : *structures) {
		delete (*p);
		*p = nullptr;
	}
	structures.clear();
}

} // namespace Data
