/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Core::DeepLinks {

enum class Result {
	Handled,
	NotFound,
	NeedsAuth,
	Unsupported,
};

struct Context {
	Window::SessionController *controller = nullptr;
	QString section;
	QString path;
	QMap<QString, QString> params;
};

using Handler = Fn<Result(const Context&)>;

struct SettingsSection {
	Settings::Type sectionId;
};

struct SettingsControl {
	Settings::Type sectionId;
	QString controlId;
};

struct CodeBlock {
	Handler handler;
};

struct AliasTo {
	QString section;
	QString path;
};

using Action = std::variant<
	SettingsSection,
	SettingsControl,
	CodeBlock,
	AliasTo>;

struct Entry {
	QString path;
	Action action;
	bool requiresAuth = true;
	bool skipActivation = false;
};

} // namespace Core::DeepLinks
