/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Storage for AhiGram settings
*/

#include "storage/ahigram_storage.h"
#include "settings.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QFile>
#include <QtCore/QDir>

namespace AhiGram {

QVariantMap SettingsData::toMap() const {
    QVariantMap map;

    map["offLogsInServer"] = offLogsInServer;

    return map;
}

SettingsData SettingsData::fromMap(const QVariantMap &map){
    SettingsData data;

    data.offLogsInServer = map.value("offLogsInServer", data.offLogsInServer).toBool();

    return data;
}

namespace Storage{

namespace {

const auto kSettingsFileName = u"ahigram_settings.json"_q;

QString MakeSettingsPath(){
    const auto basePath = cWorkingDir() + u"tdata/"_q;
    if(!QDir().exists(basePath)){
        QDir().mkpath(basePath);
    }
    return basePath + kSettingsFileName;
}

} // namespace

Settings& Settings::Instance() {
    static Settings instance;
    return instance;
}

Settings::Settings() {
    loadFromDisk();
}

Settings::~Settings(){
    if(_dirty && _autosave){
        saveToDisk();
    }
}

void Settings::update(const std::function<void(SettingsData&)>& modifier){
    modifier(_data);
    _dirty = true;

    if(_autosave){
        saveToDisk();
    }
}

void Settings::loadFromDisk(){
    const auto path = settingsPath();
    auto file = QFile(path);

    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        _data = SettingsData();
        _dirty = false;
        return;
    }

    const auto data = file.readAll();
    file.close();

    auto error = QJsonParseError();
    const auto json = QJsonDocument::fromJson(data, &error);

    if(error.error != QJsonParseError::NoError){
        LOG(("AhiGram: Error parsing settings JSON: %1")
            .arg(error.errorString()));
        _data = SettingsData();
        _dirty = false;
        return;
    }

    if(!json.isObject()){
        LOG(("AhiGram: Settings JSON is not an object."));
        _data = SettingsData();
        _dirty = false;
        return;
    }

    _data = SettingsData::fromMap(json.object().toVariantMap());
    _dirty = false;
}

void Settings::saveToDisk(){
    const auto path = settingsPath();
    auto file = QFile(path);

    if(!file.open(QIODevice::WriteOnly)){
        LOG(("AhiGram: Could not write settings to '%1'.").arg(path));
        return;
    }

    const auto map = _data.toMap();
    const auto json = QJsonDocument::fromVariant(map);
    file.write(json.toJson(QJsonDocument::Indented));
    file.close();

    _dirty = false;
}

QString Settings::settingsPath() const {
    return MakeSettingsPath();
}

void Settings::save(){
    if(_dirty){
        saveToDisk();
    }
}

void Settings::load() {
    loadFromDisk();
}

void Settings::reset(){
    _data = SettingsData();
    _dirty = true;
    if(_autosave){
        saveToDisk();
    }
}

void Settings::setAutoSave(bool enabled){
    _autosave = enabled;
    if(_autosave && _dirty){
        saveToDisk();
    }
}

} // namespace Storage
} // namespace AhiGram