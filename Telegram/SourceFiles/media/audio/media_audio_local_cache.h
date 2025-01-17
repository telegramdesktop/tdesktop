/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Audio {

class LocalCache final {
public:
    LocalCache() = default;
    ~LocalCache();

    [[nodiscard]] QString path(
        DocumentId id,
        Fn<QByteArray()> resolveBytes,
        Fn<QByteArray()> fallbackBytes);

private:
    base::flat_map<DocumentId, QString> _cache;

};

} // namespace Media::Audio
