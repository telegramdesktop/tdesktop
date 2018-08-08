/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/runtime_composer.h"

struct RuntimeComposerMetadatasMap {
	std::map<uint64, std::unique_ptr<RuntimeComposerMetadata>> data;
	QMutex mutex;
};

const RuntimeComposerMetadata *GetRuntimeComposerMetadata(uint64 mask) {
	static RuntimeComposerMetadatasMap RuntimeComposerMetadatas;

	QMutexLocker lock(&RuntimeComposerMetadatas.mutex);
	auto i = RuntimeComposerMetadatas.data.find(mask);
	if (i == end(RuntimeComposerMetadatas.data)) {
		i = RuntimeComposerMetadatas.data.emplace(
			mask,
			std::make_unique<RuntimeComposerMetadata>(mask)).first;
	}
	return i->second.get();
}

const RuntimeComposerMetadata *RuntimeComposerBase::ZeroRuntimeComposerMetadata = GetRuntimeComposerMetadata(0);

RuntimeComponentWrapStruct RuntimeComponentWraps[64];

QAtomicInt RuntimeComponentIndexLast;
