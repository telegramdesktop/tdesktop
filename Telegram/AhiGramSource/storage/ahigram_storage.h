/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Storage for AhiGram settings
*/

#pragma once

#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <functional>

namespace AhiGram {
struct SettingsData {
    bool offLogsInServer = true;

    QVariantMap toMap() const;

    static SettingsData fromMap(const QVariantMap &map);
};

namespace Storage{
class Settings{
public:
    static Settings& Instance();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    SettingsData data() const { return _data; }

    void update(const std::function<void(SettingsData&)>& modifier);

    const SettingsData& get() const { return _data; }

    void save();
    void load();
    void reset();

    void setAutoSave(bool enabled);
private:
    Settings();
    ~Settings();
    
    void loadFromDisk();
    void saveToDisk();
    QString settingsPath() const;
    
    SettingsData _data;
    bool _dirty = false;
    bool _autosave = true;
};

} // namespace Settings
} // namespace AhiGram