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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "shortcuts.h"

#include "mainwindow.h"
#include "passcodewidget.h"
#include "mainwidget.h"
#include "playerwidget.h"

namespace ShortcutCommands {

typedef void(*Handler)();

void lock_telegram() {
	if (auto w = App::wnd()) {
		if (App::passcoded()) {
			w->passcodeWidget()->onSubmit();
		} else if (cHasPasscode()) {
			w->setupPasscode(true);
		}
	}
}

void minimize_telegram() {
	if (auto w = App::wnd()) {
		if (cWorkMode() == dbiwmTrayOnly) {
			w->minimizeToTray();
		} else {
			w->setWindowState(Qt::WindowMinimized);
		}
	}
}

void close_telegram() {
	if (!Ui::hideWindowNoQuit()) {
		if (auto w = App::wnd()) {
			w->close();
		}
	}
}

void quit_telegram() {
	App::quit();
}

//void start_stop_recording() {

//}

//void cancel_recording() {

//}

void media_play() {
	if (MainWidget *m = App::main()) {
		m->player()->playPressed();
	}
}

void media_pause() {
	if (MainWidget *m = App::main()) {
		m->player()->pausePressed();
	}
}

void media_playpause() {
	if (MainWidget *m = App::main()) {
		m->player()->playPausePressed();
	}
}

void media_stop() {
	if (MainWidget *m = App::main()) {
		m->player()->stopPressed();
	}
}

void media_previous() {
	if (MainWidget *m = App::main()) {
		m->player()->prevPressed();
	}
}

void media_next() {
	if (MainWidget *m = App::main()) {
		m->player()->nextPressed();
	}
}

void search() {
	if (MainWidget *m = App::main()) {
		m->cmd_search();
	}
}

void previous_chat() {
	if (MainWidget *m = App::main()) {
		m->cmd_previous_chat();
	}
}

void next_chat() {
	if (MainWidget *m = App::main()) {
		m->cmd_next_chat();
	}
}

// other commands here

} // namespace ShortcutCommands

inline bool qMapLessThanKey(const ShortcutCommands::Handler &a, const ShortcutCommands::Handler &b) {
	return a < b;
}

namespace Shortcuts {

// inspired by https://github.com/sindresorhus/strip-json-comments
QByteArray _stripJsonComments(const QByteArray &json) {
	enum InsideComment {
		InsideCommentNone,
		InsideCommentSingleLine,
		InsideCommentMultiLine,
	};
	InsideComment insideComment = InsideCommentNone;
	bool insideString = false;

	QByteArray result;

	const char *b = json.cbegin(), *e = json.cend(), *offset = b;
	for (const char *ch = offset; ch != e; ++ch) {
		char currentChar = *ch;
		char nextChar = (ch + 1 == e) ? 0 : *(ch + 1);

		if (insideComment == InsideCommentNone && currentChar == '"') {
			bool escaped = ((ch > b) && *(ch - 1) == '\\') && ((ch - 1 < b) || *(ch - 2) != '\\');
			if (!escaped) {
				insideString = !insideString;
			}
		}

		if (insideString) {
			continue;
		}

		if (insideComment == InsideCommentNone && currentChar == '/' && nextChar == '/') {
			if (ch > offset) {
				if (result.isEmpty()) result.reserve(json.size() - 2);
				result.append(offset, ch - offset);
				offset = ch;
			}
			insideComment = InsideCommentSingleLine;
			++ch;
		} else if (insideComment == InsideCommentSingleLine && currentChar == '\r' && nextChar == '\n') {
			if (ch > offset) {
				offset = ch;
			}
			++ch;
			insideComment = InsideCommentNone;
		} else if (insideComment == InsideCommentSingleLine && currentChar == '\n') {
			if (ch > offset) {
				offset = ch;
			}
			insideComment = InsideCommentNone;
		} else if (insideComment == InsideCommentNone && currentChar == '/' && nextChar == '*') {
			if (ch > offset) {
				if (result.isEmpty()) result.reserve(json.size() - 2);
				result.append(offset, ch - offset);
				offset = ch;
			}
			insideComment = InsideCommentMultiLine;
			++ch;
		} else if (insideComment == InsideCommentMultiLine && currentChar == '*' && nextChar == '/') {
			if (ch > offset) {
				offset = ch;
			}
			++ch;
			insideComment = InsideCommentNone;
		}
	}

	if (insideComment == InsideCommentNone && e > offset && !result.isEmpty()) {
		result.append(offset, e - offset);
	}
	return result.isEmpty() ? json : result;
}

struct DataStruct;
DataStruct *DataPtr = nullptr;

namespace {

void createCommand(const QString &command, ShortcutCommands::Handler handler);
QKeySequence setShortcut(const QString &keys, const QString &command);
void destroyShortcut(QShortcut *shortcut);

} // namespace

struct DataStruct {
	DataStruct() {
		t_assert(DataPtr == nullptr);
		DataPtr = this;

#define DeclareAlias(keys, command) setShortcut(qsl(keys), qsl(#command))
#define DeclareCommand(keys, command) createCommand(qsl(#command), ShortcutCommands::command); DeclareAlias(keys, command)

		DeclareCommand("ctrl+w", close_telegram);
		DeclareAlias("ctrl+f4", close_telegram);
		DeclareCommand("ctrl+l", lock_telegram);
		DeclareCommand("ctrl+m", minimize_telegram);
		DeclareCommand("ctrl+q", quit_telegram);

		//DeclareCommand("ctrl+r", start_stop_recording);
		//DeclareCommand("ctrl+shift+r", cancel_recording);
		//DeclareCommand("media record", start_stop_recording);

		DeclareCommand("media play", media_play);
		DeclareCommand("media pause", media_pause);
		DeclareCommand("toggle media play/pause", media_playpause);
		DeclareCommand("media stop", media_stop);
		DeclareCommand("media previous", media_previous);
		DeclareCommand("media next", media_next);

		DeclareCommand("ctrl+f", search);
		DeclareAlias("search", search);
		DeclareAlias("find", search);

		DeclareCommand("ctrl+pgdown", next_chat);
		DeclareAlias("alt+down", next_chat);
		DeclareCommand("ctrl+pgup", previous_chat);
		DeclareAlias("alt+up", previous_chat);
		if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
			DeclareAlias("meta+tab", next_chat);
			DeclareAlias("meta+shift+tab", previous_chat);
			DeclareAlias("meta+backtab", previous_chat);
		} else {
			DeclareAlias("ctrl+tab", next_chat);
			DeclareAlias("ctrl+shift+tab", previous_chat);
			DeclareAlias("ctrl+backtab", previous_chat);
		}

		// other commands here

#undef DeclareCommand
#undef DeclareAlias
	}
	QStringList errors;

	QMap<QString, ShortcutCommands::Handler> commands;
	QMap<ShortcutCommands::Handler, QString> commandnames;

	QMap<QKeySequence, QShortcut*> sequences;
	QMap<int, ShortcutCommands::Handler> handlers;

	QSet<QShortcut*> mediaShortcuts;

	QSet<QString> autoRepeatCommands = {
		qsl("media_previous"),
		qsl("media_next"),
		qsl("next_chat"),
		qsl("previous_chat"),
	};

	QSet<QString> mediaCommands = {
		qsl("media_play"),
		qsl("media_pause"),
		qsl("media_playpause"),
		qsl("media_stop"),
		qsl("media_previous"),
		qsl("media_next")
	};
};

namespace {

void createCommand(const QString &command, ShortcutCommands::Handler handler) {
	t_assert(DataPtr != nullptr);
	t_assert(!command.isEmpty());

	DataPtr->commands.insert(command, handler);
	DataPtr->commandnames.insert(handler, command);
}

QKeySequence setShortcut(const QString &keys, const QString &command) {
	t_assert(DataPtr != nullptr);
	t_assert(!command.isEmpty());
	if (keys.isEmpty()) return QKeySequence();

	QKeySequence seq(keys, QKeySequence::PortableText);
	if (seq.isEmpty()) {
		DataPtr->errors.push_back(qsl("Could not derive key sequence '%1'!").arg(keys));
	} else {
		auto it = DataPtr->commands.constFind(command);
		if (it == DataPtr->commands.cend()) {
			LOG(("Warning: could not find shortcut command handler '%1'").arg(command));
		} else {
			auto shortcut = std_::make_unique<QShortcut>(seq, App::wnd(), nullptr, nullptr, Qt::ApplicationShortcut);
			if (!DataPtr->autoRepeatCommands.contains(command)) {
				shortcut->setAutoRepeat(false);
			}
			auto isMediaShortcut = DataPtr->mediaCommands.contains(command);
			if (isMediaShortcut) {
				shortcut->setEnabled(false);
			}
			int shortcutId = shortcut->id();
			if (!shortcutId) {
				DataPtr->errors.push_back(qsl("Could not create shortcut '%1'!").arg(keys));
			} else {
				auto seqIt = DataPtr->sequences.find(seq);
				if (seqIt == DataPtr->sequences.cend()) {
					seqIt = DataPtr->sequences.insert(seq, shortcut.release());
				} else {
					auto oldShortcut = seqIt.value();
					seqIt.value() = shortcut.release();
					destroyShortcut(oldShortcut);
				}
				DataPtr->handlers.insert(shortcutId, it.value());
				if (isMediaShortcut) {
					DataPtr->mediaShortcuts.insert(seqIt.value());
				}
			}
		}
	}
	return seq;
}

QKeySequence removeShortcut(const QString &keys) {
	t_assert(DataPtr != nullptr);
	if (keys.isEmpty()) return QKeySequence();

	QKeySequence seq(keys, QKeySequence::PortableText);
	if (seq.isEmpty()) {
		DataPtr->errors.push_back(qsl("Could not derive key sequence '%1'!").arg(keys));
	} else {
		auto seqIt = DataPtr->sequences.find(seq);
		if (seqIt != DataPtr->sequences.cend()) {
			auto shortcut = seqIt.value();
			DataPtr->sequences.erase(seqIt);
			destroyShortcut(shortcut);
		}
	}
	return seq;
}

void destroyShortcut(QShortcut *shortcut) {
	t_assert(DataPtr != nullptr);

	DataPtr->handlers.remove(shortcut->id());
	DataPtr->mediaShortcuts.remove(shortcut);
	delete shortcut;
}

} // namespace

void start() {
	t_assert(Global::started());

	new DataStruct();

	// write default shortcuts to a file if they are not there already
	bool defaultValid = false;
	QFile defaultFile(cWorkingDir() + qsl("tdata/shortcuts-default.json"));
	if (defaultFile.open(QIODevice::ReadOnly)) {
		QJsonParseError error = { 0, QJsonParseError::NoError };
		QJsonDocument doc = QJsonDocument::fromJson(_stripJsonComments(defaultFile.readAll()), &error);
		defaultFile.close();

		if (error.error == QJsonParseError::NoError && doc.isArray()) {
			QJsonArray shortcuts(doc.array());
			if (!shortcuts.isEmpty() && (*shortcuts.constBegin()).isObject()) {
				QJsonObject versionObject((*shortcuts.constBegin()).toObject());
				QJsonObject::const_iterator version = versionObject.constFind(qsl("version"));
				if (version != versionObject.constEnd() && (*version).isString() && (*version).toString() == QString::number(AppVersion)) {
					defaultValid = true;
				}
			}
		}
	}
	if (!defaultValid && defaultFile.open(QIODevice::WriteOnly)) {
		const char *defaultHeader = "\
// This is a list of default shortcuts for Telegram Desktop\n\
// Please don't modify it, its content is not used in any way\n\
// You can place your own shortcuts in the 'shortcuts-custom.json' file\n\n";
		defaultFile.write(defaultHeader);

		QJsonArray shortcuts;

		QJsonObject version;
		version.insert(qsl("version"), QString::number(AppVersion));
		shortcuts.push_back(version);

		for (auto i = DataPtr->sequences.cbegin(), e = DataPtr->sequences.cend(); i != e; ++i) {
			auto h = DataPtr->handlers.constFind(i.value()->id());
			if (h != DataPtr->handlers.cend()) {
				auto n = DataPtr->commandnames.constFind(h.value());
				if (n != DataPtr->commandnames.cend()) {
					QJsonObject entry;
					entry.insert(qsl("keys"), i.key().toString().toLower());
					entry.insert(qsl("command"), n.value());
					shortcuts.append(entry);
				}
			}
		}

		QJsonDocument doc;
		doc.setArray(shortcuts);
		defaultFile.write(doc.toJson(QJsonDocument::Indented));
		defaultFile.close();
	}

	// read custom shortcuts from file if it exists or write an empty custom shortcuts file
	QFile customFile(cWorkingDir() + qsl("tdata/shortcuts-custom.json"));
	if (customFile.exists()) {
		if (customFile.open(QIODevice::ReadOnly)) {
			QJsonParseError error = { 0, QJsonParseError::NoError };
			QJsonDocument doc = QJsonDocument::fromJson(_stripJsonComments(customFile.readAll()), &error);
			customFile.close();

			if (error.error != QJsonParseError::NoError) {
				DataPtr->errors.push_back(qsl("Failed to parse! Error: %2").arg(error.errorString()));
			} else if (!doc.isArray()) {
				DataPtr->errors.push_back(qsl("Failed to parse! Error: array expected"));
			} else {
				QJsonArray shortcuts = doc.array();
				int limit = ShortcutsCountLimit;
				for (QJsonArray::const_iterator i = shortcuts.constBegin(), e = shortcuts.constEnd(); i != e; ++i) {
					if (!(*i).isObject()) {
						DataPtr->errors.push_back(qsl("Bad entry! Error: object expected"));
					} else {
						QKeySequence seq;
						QJsonObject entry((*i).toObject());
						QJsonObject::const_iterator keys = entry.constFind(qsl("keys")), command = entry.constFind(qsl("command"));
						if (keys == entry.constEnd() || command == entry.constEnd() || !(*keys).isString() || (!(*command).isString() && !(*command).isNull())) {
							DataPtr->errors.push_back(qsl("Bad entry! {\"keys\": \"...\", \"command\": [ \"...\" | null ]} expected"));
						} else if ((*command).isNull()) {
							seq = removeShortcut((*keys).toString());
						} else {
							seq = setShortcut((*keys).toString(), (*command).toString());
						}
						if (!--limit) {
							DataPtr->errors.push_back(qsl("Too many entries! Limit is %1").arg(ShortcutsCountLimit));
							break;
						}
					}
				}
			}
		} else {
			DataPtr->errors.push_back(qsl("Could not read the file!"));
		}
		if (!DataPtr->errors.isEmpty()) {
			DataPtr->errors.push_front(qsl("While reading file '%1'...").arg(customFile.fileName()));
		}
	} else if (customFile.open(QIODevice::WriteOnly)) {
		const char *customContent = "\
// This is a list of your own shortcuts for Telegram Desktop\n\
// You can see full list of commands in the 'shortcuts-default.json' file\n\
// Place a null value instead of a command string to switch the shortcut off\n\n\
[\n\
    // {\n\
    //     \"command\": \"close_telegram\",\n\
    //     \"keys\": \"ctrl+f4\"\n\
    // },\n\
    // {\n\
    //     \"command\": \"quit_telegram\",\n\
    //     \"keys\": \"ctrl+q\"\n\
    // }\n\
]\n";
		customFile.write(customContent);
		customFile.close();
	}
}

const QStringList &errors() {
	t_assert(DataPtr != nullptr);
	return DataPtr->errors;
}

bool launch(int shortcutId) {
	t_assert(DataPtr != nullptr);

	auto it = DataPtr->handlers.constFind(shortcutId);
	if (it == DataPtr->handlers.cend()) {
		return false;
	}
	(*it.value())();
	return true;
}

bool launch(const QString &command) {
	t_assert(DataPtr != nullptr);

	auto it = DataPtr->commands.constFind(command);
	if (it == DataPtr->commands.cend()) {
		return false;
	}
	(*it.value())();
	return true;
}

void enableMediaShortcuts() {
	if (!DataPtr) return;
	for_const (auto shortcut, DataPtr->mediaShortcuts) {
		shortcut->setEnabled(true);
	}
}

void disableMediaShortcuts() {
	if (!DataPtr) return;
	for_const (auto shortcut, DataPtr->mediaShortcuts) {
		shortcut->setEnabled(false);
	}
}

void finish() {
	delete DataPtr;
	DataPtr = nullptr;
}

} // namespace Shortcuts
