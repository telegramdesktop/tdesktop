/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/shortcuts.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "media/player/media_player_instance.h"
#include "platform/platform_specific.h"
#include "base/parse_helper.h"

namespace Shortcuts {
namespace {

constexpr auto kCountLimit = 256; // How many shortcuts can be in json file.

rpl::event_stream<not_null<Request*>> RequestsStream;

const auto AutoRepeatCommands = base::flat_set<Command>{
	Command::MediaPrevious,
	Command::MediaNext,
	Command::ChatPrevious,
	Command::ChatNext,
};

const auto MediaCommands = base::flat_set<Command>{
	Command::MediaPlay,
	Command::MediaPause,
	Command::MediaPlayPause,
	Command::MediaStop,
	Command::MediaPrevious,
	Command::MediaNext,
};

const auto CommandByName = base::flat_map<QString, Command>{
	{ qsl("close_telegram")   , Command::Close },
	{ qsl("lock_telegram")    , Command::Lock },
	{ qsl("minimize_telegram"), Command::Minimize },
	{ qsl("quit_telegram")    , Command::Quit },

	{ qsl("media_play")       , Command::MediaPlay },
	{ qsl("media_pause")      , Command::MediaPause },
	{ qsl("media_playpause")  , Command::MediaPlayPause },
	{ qsl("media_stop")       , Command::MediaStop },
	{ qsl("media_previous")   , Command::MediaPrevious },
	{ qsl("media_next")       , Command::MediaNext },

	{ qsl("search")           , Command::Search },

	{ qsl("previous_chat")    , Command::ChatPrevious },
	{ qsl("next_chat")        , Command::ChatNext },
	{ qsl("first_chat")       , Command::ChatFirst },
	{ qsl("last_chat")        , Command::ChatLast },
};

const auto CommandNames = base::flat_map<Command, QString>{
	{ Command::Close         , qsl("close_telegram") },
	{ Command::Lock          , qsl("lock_telegram") },
	{ Command::Minimize      , qsl("minimize_telegram") },
	{ Command::Quit          , qsl("quit_telegram") },

	{ Command::MediaPlay     , qsl("media_play") },
	{ Command::MediaPause    , qsl("media_pause") },
	{ Command::MediaPlayPause, qsl("media_playpause") },
	{ Command::MediaStop     , qsl("media_stop") },
	{ Command::MediaPrevious , qsl("media_previous") },
	{ Command::MediaNext     , qsl("media_next") },

	{ Command::Search        , qsl("search") },

	{ Command::ChatPrevious  , qsl("previous_chat") },
	{ Command::ChatNext      , qsl("next_chat") },
	{ Command::ChatFirst     , qsl("first_chat") },
	{ Command::ChatLast      , qsl("last_chat") },
};

class Manager {
public:
	void fill();
	void clear();

	std::optional<Command> lookup(int shortcutId) const;
	void toggleMedia(bool toggled);

	const QStringList &errors() const;

private:
	void fillDefaults();
	void writeDefaultFile();
	bool readCustomFile();

	void set(const QString &keys, Command command);
	void remove(const QString &keys);
	void unregister(base::unique_qptr<QShortcut> shortcut);

	QStringList _errors;

	base::flat_map<QKeySequence, base::unique_qptr<QShortcut>> _shortcuts;
	base::flat_map<int, Command> _commandByShortcutId;

	base::flat_set<QShortcut*> _mediaShortcuts;

};

QString DefaultFilePath() {
	return cWorkingDir() + qsl("tdata/shortcuts-default.json");
}

QString CustomFilePath() {
	return cWorkingDir() + qsl("tdata/shortcuts-custom.json");
}

bool DefaultFileIsValid() {
	QFile file(DefaultFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError || !document.isArray()) {
		return false;
	}
	const auto shortcuts = document.array();
	if (shortcuts.isEmpty() || !(*shortcuts.constBegin()).isObject()) {
		return false;
	}
	const auto versionObject = (*shortcuts.constBegin()).toObject();
	const auto version = versionObject.constFind(qsl("version"));
	if (version == versionObject.constEnd()
		|| !(*version).isString()
		|| (*version).toString() != QString::number(AppVersion)) {
		return false;
	}
	return true;
}

void WriteDefaultCustomFile() {
	const auto path = CustomFilePath();
	auto input = QFile(":/misc/default_shortcuts-custom.json");
	auto output = QFile(path);
	if (input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly)) {
		output.write(input.readAll());
	}
}

void Manager::fill() {
	fillDefaults();

	if (!DefaultFileIsValid()) {
		writeDefaultFile();
	}
	if (!readCustomFile()) {
		WriteDefaultCustomFile();
	}
}

void Manager::clear() {
	_errors.clear();
	_shortcuts.clear();
	_commandByShortcutId.clear();
	_mediaShortcuts.clear();
}

const QStringList &Manager::errors() const {
	return _errors;
}

std::optional<Command> Manager::lookup(int shortcutId) const {
	const auto i = _commandByShortcutId.find(shortcutId);
	return (i != end(_commandByShortcutId))
		? base::make_optional(i->second)
		: std::nullopt;
}

void Manager::toggleMedia(bool toggled) {
	for (const auto shortcut : _mediaShortcuts) {
		shortcut->setEnabled(toggled);
	}
}

bool Manager::readCustomFile() {
	// read custom shortcuts from file if it exists or write an empty custom shortcuts file
	QFile file(CustomFilePath());
	if (!file.exists()) {
		return false;
	}
	const auto guard = gsl::finally([&] {
		if (!_errors.isEmpty()) {
			_errors.push_front(qsl("While reading file '%1'..."
			).arg(file.fileName()));
		}
	});
	if (!file.open(QIODevice::ReadOnly)) {
		_errors.push_back(qsl("Could not read the file!"));
		return true;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError) {
		_errors.push_back(qsl("Failed to parse! Error: %2"
		).arg(error.errorString()));
		return true;
	} else if (!document.isArray()) {
		_errors.push_back(qsl("Failed to parse! Error: array expected"));
		return true;
	}
	const auto shortcuts = document.array();
	auto limit = kCountLimit;
	for (auto i = shortcuts.constBegin(), e = shortcuts.constEnd(); i != e; ++i) {
		if (!(*i).isObject()) {
			_errors.push_back(qsl("Bad entry! Error: object expected"));
			continue;
		}
		const auto entry = (*i).toObject();
		const auto keys = entry.constFind(qsl("keys"));
		const auto command = entry.constFind(qsl("command"));
		if (keys == entry.constEnd()
			|| command == entry.constEnd()
			|| !(*keys).isString()
			|| (!(*command).isString() && !(*command).isNull())) {
			_errors.push_back(qsl("Bad entry! "
				"{\"keys\": \"...\", \"command\": [ \"...\" | null ]} "
				"expected."));
		} else if ((*command).isNull()) {
			remove((*keys).toString());
		} else {
			const auto name = (*command).toString();
			const auto i = CommandByName.find(name);
			if (i != end(CommandByName)) {
				set((*keys).toString(), i->second);
			} else {
				LOG(("Shortcut Warning: "
					"could not find shortcut command handler '%1'"
					).arg(name));
			}
		}
		if (!--limit) {
			_errors.push_back(qsl("Too many entries! Limit is %1"
			).arg(kCountLimit));
			break;
		}
	}
	return true;
}

void Manager::fillDefaults() {
	set(qsl("ctrl+w"), Command::Close);
	set(qsl("ctrl+f4"), Command::Close);
	set(qsl("ctrl+l"), Command::Lock);
	set(qsl("ctrl+m"), Command::Minimize);
	set(qsl("ctrl+q"), Command::Quit);

	set(qsl("media play"), Command::MediaPlay);
	set(qsl("media pause"), Command::MediaPause);
	set(qsl("toggle media play/pause"), Command::MediaPlayPause);
	set(qsl("media stop"), Command::MediaStop);
	set(qsl("media previous"), Command::MediaPrevious);
	set(qsl("media next"), Command::MediaNext);

	set(qsl("ctrl+f"), Command::Search);
	set(qsl("search"), Command::Search);
	set(qsl("find"), Command::Search);

	set(qsl("ctrl+pgdown"), Command::ChatNext);
	set(qsl("alt+down"), Command::ChatNext);
	set(qsl("ctrl+pgup"), Command::ChatPrevious);
	set(qsl("alt+up"), Command::ChatPrevious);
	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		set(qsl("meta+tab"), Command::ChatNext);
		set(qsl("meta+shift+tab"), Command::ChatPrevious);
		set(qsl("meta+backtab"), Command::ChatPrevious);
	} else {
		set(qsl("ctrl+tab"), Command::ChatNext);
		set(qsl("ctrl+shift+tab"), Command::ChatPrevious);
		set(qsl("ctrl+backtab"), Command::ChatPrevious);
	}
	set(qsl("ctrl+alt+home"), Command::ChatFirst);
	set(qsl("ctrl+alt+end"), Command::ChatLast);

	set(qsl("f5"), Command::SupportReloadTemplates);
	set(qsl("ctrl+delete"), Command::SupportToggleMuted);
	set(qsl("ctrl+insert"), Command::SupportScrollToCurrent);
}

void Manager::writeDefaultFile() {
	auto file = QFile(DefaultFilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	const char *defaultHeader = R"HEADER(
// This is a list of default shortcuts for Telegram Desktop
// Please don't modify it, its content is not used in any way
// You can place your own shortcuts in the 'shortcuts-custom.json' file

)HEADER";
	file.write(defaultHeader);

	auto shortcuts = QJsonArray();
	auto version = QJsonObject();
	version.insert(qsl("version"), QString::number(AppVersion));
	shortcuts.push_back(version);

	for (const auto &[sequence, shortcut] : _shortcuts) {
		const auto i = _commandByShortcutId.find(shortcut->id());
		if (i != end(_commandByShortcutId)) {
			const auto j = CommandNames.find(i->second);
			if (j != end(CommandNames)) {
				QJsonObject entry;
				entry.insert(qsl("keys"), sequence.toString().toLower());
				entry.insert(qsl("command"), j->second);
				shortcuts.append(entry);
			}
		}
	}

	auto document = QJsonDocument();
	document.setArray(shortcuts);
	file.write(document.toJson(QJsonDocument::Indented));
}

void Manager::set(const QString &keys, Command command) {
	if (keys.isEmpty()) {
		return;
	}

	const auto result = QKeySequence(keys, QKeySequence::PortableText);
	if (result.isEmpty()) {
		_errors.push_back(qsl("Could not derive key sequence '%1'!"
		).arg(keys));
		return;
	}
	auto shortcut = base::make_unique_q<QShortcut>(
		result,
		Messenger::Instance().getActiveWindow(),
		nullptr,
		nullptr,
		Qt::ApplicationShortcut);
	if (AutoRepeatCommands.contains(command)) {
		shortcut->setAutoRepeat(false);
	}
	const auto isMediaShortcut = MediaCommands.contains(command);
	if (isMediaShortcut) {
		shortcut->setEnabled(false);
	}
	const auto id = shortcut->id();
	if (!id) {
		_errors.push_back(qsl("Could not create shortcut '%1'!").arg(keys));
		return;
	}
	auto i = _shortcuts.find(result);
	if (i == end(_shortcuts)) {
		i = _shortcuts.emplace(result, std::move(shortcut)).first;
	} else {
		unregister(std::exchange(i->second, std::move(shortcut)));
	}
	_commandByShortcutId.emplace(id, command);
	if (isMediaShortcut) {
		_mediaShortcuts.emplace(i->second.get());
	}
}

void Manager::remove(const QString &keys) {
	if (keys.isEmpty()) {
		return;
	}

	const auto result = QKeySequence(keys, QKeySequence::PortableText);
	if (result.isEmpty()) {
		_errors.push_back(qsl("Could not derive key sequence '%1'!"
		).arg(keys));
		return;
	}
	const auto i = _shortcuts.find(result);
	if (i != end(_shortcuts)) {
		unregister(std::move(i->second));
		_shortcuts.erase(i);
	}
}

void Manager::unregister(base::unique_qptr<QShortcut> shortcut) {
	if (shortcut) {
		_commandByShortcutId.erase(shortcut->id());
		_mediaShortcuts.erase(shortcut.get());
	}
}

Manager Data;

} // namespace

Request::Request(Command command) : _command(command) {
}

bool Request::check(Command command, int priority) {
	if (_command == command && priority > _handlerPriority) {
		_handlerPriority = priority;
		return true;
	}
	return false;
}

bool Request::handle(FnMut<bool()> handler) {
	_handler = std::move(handler);
	return true;
}

FnMut<bool()> RequestHandler(Command command) {
	auto request = Request(command);
	RequestsStream.fire(&request);
	return std::move(request._handler);
}

bool Launch(Command command) {
	if (auto handler = RequestHandler(command)) {
		return handler();
	}
	return false;
}

rpl::producer<not_null<Request*>> Requests() {
	return RequestsStream.events();
}

void Start() {
	Assert(Global::started());

	Data.fill();
}

const QStringList &Errors() {
	return Data.errors();
}

bool HandleEvent(not_null<QShortcutEvent*> event) {
	if (const auto command = Data.lookup(event->shortcutId())) {
		return Launch(*command);
	}
	return false;
}

void EnableMediaShortcuts() {
	Data.toggleMedia(true);
	Platform::SetWatchingMediaKeys(true);
}

void DisableMediaShortcuts() {
	Data.toggleMedia(false);
	Platform::SetWatchingMediaKeys(false);
}

void Finish() {
	Data.clear();
}

} // namespace Shortcuts
