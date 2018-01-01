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
