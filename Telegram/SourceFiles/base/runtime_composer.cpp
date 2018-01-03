/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/runtime_composer.h"

struct RuntimeComposerMetadatasMap {
	QMap<uint64, RuntimeComposerMetadata*> data;
	~RuntimeComposerMetadatasMap() {
		for_const (const RuntimeComposerMetadata *p, data) {
			delete p;
		}
	}
};

const RuntimeComposerMetadata *GetRuntimeComposerMetadata(uint64 mask) {
	static RuntimeComposerMetadatasMap RuntimeComposerMetadatas;
	static QMutex RuntimeComposerMetadatasMutex;

	QMutexLocker lock(&RuntimeComposerMetadatasMutex);
	auto i = RuntimeComposerMetadatas.data.constFind(mask);
	if (i == RuntimeComposerMetadatas.data.cend()) {
		RuntimeComposerMetadata *meta = new RuntimeComposerMetadata(mask);
		Assert(meta != nullptr);

		i = RuntimeComposerMetadatas.data.insert(mask, meta);
	}
	return i.value();
}

const RuntimeComposerMetadata *RuntimeComposer::ZeroRuntimeComposerMetadata = GetRuntimeComposerMetadata(0);

RuntimeComponentWrapStruct RuntimeComponentWraps[64];

QAtomicInt RuntimeComponentIndexLast;
