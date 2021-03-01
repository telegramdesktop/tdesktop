/*
This file is part of Telegram Desktop x64,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#include "core/enhanced_settings.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "base/parse_helper.h"
#include "facades.h"
#include "ui/widgets/input_fields.h"
#include "lang/lang_cloud_manager.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>

namespace EnhancedSettings {
	namespace {

		constexpr auto kWriteJsonTimeout = crl::time(5000);

		QString DefaultFilePath() {
			return cWorkingDir() + qsl("tdata/enhanced-settings-default.json");
		}

		QString CustomFilePath() {
			return cWorkingDir() + qsl("tdata/enhanced-settings-custom.json");
		}

		bool DefaultFileIsValid() {
			QFile file(DefaultFilePath());
			if (!file.open(QIODevice::ReadOnly)) {
				return false;
			}
			auto error = QJsonParseError{0, QJsonParseError::NoError};
			const auto document = QJsonDocument::fromJson(
					base::parse::stripComments(file.readAll()),
					&error);
			file.close();

			if (error.error != QJsonParseError::NoError || !document.isObject()) {
				return false;
			}
			const auto settings = document.object();

			return true;
		}

		void WriteDefaultCustomFile() {
			const auto path = CustomFilePath();
			auto input = QFile(":/misc/default_enhanced-settings-custom.json");
			auto output = QFile(path);
			if (input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly)) {
				output.write(input.readAll());
			}
		}

		bool ReadOption(QJsonObject obj, QString key, std::function<void(QJsonValue)> callback) {
			const auto it = obj.constFind(key);
			if (it == obj.constEnd()) {
				return false;
			}
			callback(*it);
			return true;
		}

		bool ReadObjectOption(QJsonObject obj, QString key, std::function<void(QJsonObject)> callback) {
			auto readResult = false;
			auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
				if (v.isObject()) {
					callback(v.toObject());
					readResult = true;
				}
			});
			return (readValueResult && readResult);
		}

		bool ReadArrayOption(QJsonObject obj, QString key, std::function<void(QJsonArray)> callback) {
			auto readResult = false;
			auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
				if (v.isArray()) {
					callback(v.toArray());
					readResult = true;
				}
			});
			return (readValueResult && readResult);
		}

		bool ReadStringOption(QJsonObject obj, QString key, std::function<void(QString)> callback) {
			auto readResult = false;
			auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
				if (v.isString()) {
					callback(v.toString());
					readResult = true;
				}
			});
			return (readValueResult && readResult);
		}

		bool ReadIntOption(QJsonObject obj, QString key, std::function<void(int)> callback) {
			auto readResult = false;
			auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
				if (v.isDouble()) {
					callback(v.toInt());
					readResult = true;
				}
			});
			return (readValueResult && readResult);
		}

		bool ReadBoolOption(QJsonObject obj, QString key, std::function<void(bool)> callback) {
			auto readResult = false;
			auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
				if (v.isBool()) {
					callback(v.toBool());
					readResult = true;
				}
			});
			return (readValueResult && readResult);
		}

		std::unique_ptr<Manager> Data;

	} // namespace

	Manager::Manager() {
		_jsonWriteTimer.setSingleShot(true);
		connect(&_jsonWriteTimer, SIGNAL(timeout()), this, SLOT(writeTimeout()));
	}

	void Manager::fill() {
		if (!DefaultFileIsValid()) {
			writeDefaultFile();
		}
		if (!readCustomFile()) {
			WriteDefaultCustomFile();
		}
	}

	void Manager::write(bool force) {
		if (force && _jsonWriteTimer.isActive()) {
			_jsonWriteTimer.stop();
			writeTimeout();
		} else if (!force && !_jsonWriteTimer.isActive()) {
			_jsonWriteTimer.start(kWriteJsonTimeout);
		}
	}

	bool Manager::readCustomFile() {
		QFile file(CustomFilePath());
		if (!file.exists()) {
			cSetEnhancedFirstRun(true);
			return false;
		}
		cSetEnhancedFirstRun(false);
		if (!file.open(QIODevice::ReadOnly)) {
			return true;
		}
		auto error = QJsonParseError{0, QJsonParseError::NoError};
		const auto document = QJsonDocument::fromJson(
				base::parse::stripComments(file.readAll()),
				&error);
		file.close();

		if (error.error != QJsonParseError::NoError) {
			return true;
		} else if (!document.isObject()) {
			return true;
		}
		const auto settings = document.object();

		if (settings.isEmpty()) {
			return true;
		}


		ReadOption(settings, "net_speed_boost", [&](auto v) {
			if (v.isString()) {

				const auto option = v.toString();
				if (option == "high") {
					SetNetworkBoost(3);
				} else if (option == "medium") {
					SetNetworkBoost(2);
				} else if (option == "low") {
					SetNetworkBoost(1);
				} else {
					SetNetworkBoost(0);
				}

			} else if (v.isNull()) {
				SetNetworkBoost(0);
			} else if (v.isDouble()) {
				SetNetworkBoost(v.toInt());
			}
		});

		ReadBoolOption(settings, "show_messages_id", [&](auto v) {
			cSetShowMessagesID(v);
		});

		ReadBoolOption(settings, "show_repeater_option", [&](auto v) {
			cSetShowRepeaterOption(v);
		});

		ReadBoolOption(settings, "show_emoji_button_as_text", [&](auto v) {
			cSetShowEmojiButtonAsText(v);
		});

		ReadOption(settings, "always_delete_for", [&](auto v) {
			if (v.isNull()) {
				SetAlwaysDelete(0);
			} else if (v.isDouble()) {
				SetAlwaysDelete(v.toInt());
			}
		});

		ReadBoolOption(settings, "show_phone_number", [&](auto v) {
			cSetShowPhoneNumber(v);
		});

		ReadBoolOption(settings, "repeater_reply_to_orig_msg", [&](auto v) {
			cSetRepeaterReplyToOrigMsg(v);
		});

		ReadBoolOption(settings, "disable_cloud_draft_sync", [&](auto v) {
			cSetDisableCloudDraftSync(v);
		});

		ReadBoolOption(settings, "show_scheduled_button", [&](auto v) {
			cSetShowScheduledButton(v);
		});

		ReadBoolOption(settings, "radio_mode", [&](auto v) {
			cSetRadioMode(v);
		});

		ReadStringOption(settings, "radio_controller", [&](auto v) {
			cSetRadioController(v);
		});

		return true;
	}

	void Manager::writeDefaultFile() {
		auto file = QFile(DefaultFilePath());
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		const char *defaultHeader = R"HEADER(
// This is a list of default options for Telegram Desktop x64
// Please don't modify it, its content is not used in any way
// You can place your own options in the 'enhanced-settings-custom.json' file
)HEADER";
		file.write(defaultHeader);

		auto settings = QJsonObject();
		settings.insert(qsl("net_speed_boost"), 0);
		settings.insert(qsl("show_messages_id"), false);
		settings.insert(qsl("show_repeater_option"), false);
		settings.insert(qsl("show_emoji_button_as_text"), false);
		settings.insert(qsl("always_delete_for"), 0);
		settings.insert(qsl("show_phone_number"), true);
		settings.insert(qsl("repeater_reply_to_orig_msg"), false);
		settings.insert(qsl("disable_cloud_draft_sync"), false);
		settings.insert(qsl("show_scheduled_button"), false);
		settings.insert(qsl("radio_mode"), false);
		settings.insert(qsl("radio_controller"), "");

		auto document = QJsonDocument();
		document.setObject(settings);
		file.write(document.toJson(QJsonDocument::Indented));
	}

	void Manager::writeCurrentSettings() {
		auto file = QFile(CustomFilePath());
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		if (_jsonWriteTimer.isActive()) {
			writing();
		}
		const char *customHeader = R"HEADER(
// This file was automatically generated from current settings
// It's better to edit it with app closed, so there will be no rewrites
// You should restart app to see changes
)HEADER";
		file.write(customHeader);

		auto settings = QJsonObject();
		settings.insert(qsl("net_speed_boost"), cNetSpeedBoost());
		settings.insert(qsl("show_messages_id"), cShowMessagesID());
		settings.insert(qsl("show_repeater_option"), cShowRepeaterOption());
		settings.insert(qsl("show_emoji_button_as_text"), cShowEmojiButtonAsText());
		settings.insert(qsl("always_delete_for"), cAlwaysDeleteFor());
		settings.insert(qsl("show_phone_number"), cShowPhoneNumber());
		settings.insert(qsl("repeater_reply_to_orig_msg"), cRepeaterReplyToOrigMsg());
		settings.insert(qsl("disable_cloud_draft_sync"), cDisableCloudDraftSync());
		settings.insert(qsl("show_scheduled_button"), cShowScheduledButton());
		settings.insert(qsl("radio_mode"), cRadioMode());
		settings.insert(qsl("radio_controller"), cRadioController());

		auto document = QJsonDocument();
		document.setObject(settings);
		file.write(document.toJson(QJsonDocument::Indented));
	}

	void Manager::writeTimeout() {
		writeCurrentSettings();
	}

	void Manager::writing() {
		_jsonWriteTimer.stop();
	}

	void Start() {
		if (Data) return;

		Data = std::make_unique<Manager>();
		Data->fill();
	}

	void Write() {
		if (!Data) return;

		Data->write();
	}

	void Finish() {
		if (!Data) return;

		Data->write(true);
	}

} // namespace EnhancedSettings