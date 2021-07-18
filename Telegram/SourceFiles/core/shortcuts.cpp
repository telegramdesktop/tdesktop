/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/shortcuts.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "media/player/media_player_instance.h"
#include "base/platform/base_platform_info.h"
#include "platform/platform_specific.h"
#include "base/parse_helper.h"

#include <QtWidgets/QShortcut>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Shortcuts {
namespace {

constexpr auto kCountLimit = 256; // How many shortcuts can be in json file.

rpl::event_stream<not_null<Request*>> RequestsStream;

const auto AutoRepeatCommands = base::flat_set<Command>{
	Command::MediaPrevious,
	Command::MediaNext,
	Command::ChatPrevious,
	Command::ChatNext,
	Command::ChatFirst,
	Command::ChatLast,
};

const auto MediaCommands = base::flat_set<Command>{
	Command::MediaPlay,
	Command::MediaPause,
	Command::MediaPlayPause,
	Command::MediaStop,
	Command::MediaPrevious,
	Command::MediaNext,
};

const auto SupportCommands = base::flat_set<Command>{
	Command::SupportReloadTemplates,
	Command::SupportToggleMuted,
	Command::SupportScrollToCurrent,
	Command::SupportHistoryBack,
	Command::SupportHistoryForward,
};

const auto CommandByName = base::flat_map<QString, Command>{
	{ qsl("close_telegram")    , Command::Close },
	{ qsl("lock_telegram")     , Command::Lock },
	{ qsl("minimize_telegram") , Command::Minimize },
	{ qsl("quit_telegram")     , Command::Quit },

	{ qsl("media_play")        , Command::MediaPlay },
	{ qsl("media_pause")       , Command::MediaPause },
	{ qsl("media_playpause")   , Command::MediaPlayPause },
	{ qsl("media_stop")        , Command::MediaStop },
	{ qsl("media_previous")    , Command::MediaPrevious },
	{ qsl("media_next")        , Command::MediaNext },

	{ qsl("search")            , Command::Search },

	{ qsl("previous_chat")     , Command::ChatPrevious },
	{ qsl("next_chat")         , Command::ChatNext },
	{ qsl("first_chat")        , Command::ChatFirst },
	{ qsl("last_chat")         , Command::ChatLast },
	{ qsl("self_chat")         , Command::ChatSelf },

	{ qsl("previous_folder")   , Command::FolderPrevious },
	{ qsl("next_folder")       , Command::FolderNext },
	{ qsl("all_chats")         , Command::ShowAllChats },

	{ qsl("folder1")           , Command::ShowFolder1 },
	{ qsl("folder2")           , Command::ShowFolder2 },
	{ qsl("folder3")           , Command::ShowFolder3 },
	{ qsl("folder4")           , Command::ShowFolder4 },
	{ qsl("folder5")           , Command::ShowFolder5 },
	{ qsl("folder6")           , Command::ShowFolder6 },
	{ qsl("last_folder")       , Command::ShowFolderLast },

	{ qsl("show_archive")      , Command::ShowArchive },
	{ qsl("show_contacts")     , Command::ShowContacts },

	{ qsl("read_chat")         , Command::ReadChat },

	// Shortcuts that have no default values.
	{ qsl("message")           , Command::JustSendMessage },
	{ qsl("message_silently")  , Command::SendSilentMessage },
	{ qsl("message_scheduled") , Command::ScheduleMessage },
	//
};

const auto CommandNames = base::flat_map<Command, QString>{
	{ Command::Close          , qsl("close_telegram") },
	{ Command::Lock           , qsl("lock_telegram") },
	{ Command::Minimize       , qsl("minimize_telegram") },
	{ Command::Quit           , qsl("quit_telegram") },

	{ Command::MediaPlay      , qsl("media_play") },
	{ Command::MediaPause     , qsl("media_pause") },
	{ Command::MediaPlayPause , qsl("media_playpause") },
	{ Command::MediaStop      , qsl("media_stop") },
	{ Command::MediaPrevious  , qsl("media_previous") },
	{ Command::MediaNext      , qsl("media_next") },

	{ Command::Search         , qsl("search") },

	{ Command::ChatPrevious   , qsl("previous_chat") },
	{ Command::ChatNext       , qsl("next_chat") },
	{ Command::ChatFirst      , qsl("first_chat") },
	{ Command::ChatLast       , qsl("last_chat") },
	{ Command::ChatSelf       , qsl("self_chat") },

	{ Command::FolderPrevious , qsl("previous_folder") },
	{ Command::FolderNext     , qsl("next_folder") },
	{ Command::ShowAllChats   , qsl("all_chats") },

	{ Command::ShowFolder1    , qsl("folder1") },
	{ Command::ShowFolder2    , qsl("folder2") },
	{ Command::ShowFolder3    , qsl("folder3") },
	{ Command::ShowFolder4    , qsl("folder4") },
	{ Command::ShowFolder5    , qsl("folder5") },
	{ Command::ShowFolder6    , qsl("folder6") },
	{ Command::ShowFolderLast , qsl("last_folder") },

	{ Command::ShowArchive    , qsl("show_archive") },
	{ Command::ShowContacts   , qsl("show_contacts") },

	{ Command::ReadChat       , qsl("read_chat") },
};

class Manager {
public:
	void fill();
	void clear();

	[[nodiscard]] std::vector<Command> lookup(int shortcutId) const;
	void toggleMedia(bool toggled);
	void toggleSupport(bool toggled);

	const QStringList &errors() const;

private:
	void fillDefaults();
	void writeDefaultFile();
	bool readCustomFile();

	void set(const QString &keys, Command command, bool replace = false);
	void remove(const QString &keys);
	void unregister(base::unique_qptr<QShortcut> shortcut);

	QStringList _errors;

	base::flat_map<QKeySequence, base::unique_qptr<QShortcut>> _shortcuts;
	base::flat_multi_map<int, Command> _commandByShortcutId;

	base::flat_set<QShortcut*> _mediaShortcuts;
	base::flat_set<QShortcut*> _supportShortcuts;

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
	_supportShortcuts.clear();
}

const QStringList &Manager::errors() const {
	return _errors;
}

std::vector<Command> Manager::lookup(int shortcutId) const {
	auto result = std::vector<Command>();
	auto i = _commandByShortcutId.findFirst(shortcutId);
	const auto end = _commandByShortcutId.end();
	for (; i != end && (i->first == shortcutId); ++i) {
		result.push_back(i->second);
	}
	return result;
}

void Manager::toggleMedia(bool toggled) {
	for (const auto shortcut : _mediaShortcuts) {
		shortcut->setEnabled(toggled);
	}
}

void Manager::toggleSupport(bool toggled) {
	for (const auto shortcut : _supportShortcuts) {
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
				set((*keys).toString(), i->second, true);
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
	const auto ctrl = Platform::IsMac() ? qsl("meta") : qsl("ctrl");

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

	set(qsl("%1+tab").arg(ctrl), Command::ChatNext);
	set(qsl("%1+shift+tab").arg(ctrl), Command::ChatPrevious);
	set(qsl("%1+backtab").arg(ctrl), Command::ChatPrevious);

	set(qsl("ctrl+alt+home"), Command::ChatFirst);
	set(qsl("ctrl+alt+end"), Command::ChatLast);

	set(qsl("f5"), Command::SupportReloadTemplates);
	set(qsl("ctrl+delete"), Command::SupportToggleMuted);
	set(qsl("ctrl+insert"), Command::SupportScrollToCurrent);
	set(qsl("ctrl+shift+x"), Command::SupportHistoryBack);
	set(qsl("ctrl+shift+c"), Command::SupportHistoryForward);

	set(qsl("ctrl+1"), Command::ChatPinned1);
	set(qsl("ctrl+2"), Command::ChatPinned2);
	set(qsl("ctrl+3"), Command::ChatPinned3);
	set(qsl("ctrl+4"), Command::ChatPinned4);
	set(qsl("ctrl+5"), Command::ChatPinned5);

	auto &&folders = ranges::views::zip(
		kShowFolder,
		ranges::views::ints(1, ranges::unreachable));

	for (const auto [command, index] : folders) {
		set(qsl("%1+%2").arg(ctrl).arg(index), command);
	}

	set(qsl("%1+shift+down").arg(ctrl), Command::FolderNext);
	set(qsl("%1+shift+up").arg(ctrl), Command::FolderPrevious);

	set(qsl("ctrl+0"), Command::ChatSelf);

	set(qsl("ctrl+9"), Command::ShowArchive);
	set(qsl("ctrl+j"), Command::ShowContacts);

	set(qsl("ctrl+r"), Command::ReadChat);
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
		const auto shortcutId = shortcut->id();
		auto i = _commandByShortcutId.findFirst(shortcutId);
		const auto end = _commandByShortcutId.end();
		for (; i != end && i->first == shortcutId; ++i) {
			const auto j = CommandNames.find(i->second);
			if (j != CommandNames.end()) {
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

void Manager::set(const QString &keys, Command command, bool replace) {
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
		Core::App().activeWindow()->widget().get(),
		nullptr,
		nullptr,
		Qt::ApplicationShortcut);
	if (!AutoRepeatCommands.contains(command)) {
		shortcut->setAutoRepeat(false);
	}
	const auto isMediaShortcut = MediaCommands.contains(command);
	const auto isSupportShortcut = SupportCommands.contains(command);
	if (isMediaShortcut || isSupportShortcut) {
		shortcut->setEnabled(false);
	}
	auto id = shortcut->id();
	auto i = _shortcuts.find(result);
	if (i == end(_shortcuts)) {
		i = _shortcuts.emplace(result, std::move(shortcut)).first;
	} else if (replace) {
		unregister(std::exchange(i->second, std::move(shortcut)));
	} else {
		id = i->second->id();
	}
	if (!id) {
		_errors.push_back(qsl("Could not create shortcut '%1'!").arg(keys));
		return;
	}
	_commandByShortcutId.emplace(id, command);
	if (!shortcut && isMediaShortcut) {
		_mediaShortcuts.emplace(i->second.get());
	}
	if (!shortcut && isSupportShortcut) {
		_supportShortcuts.emplace(i->second.get());
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
		_supportShortcuts.erase(shortcut.get());
	}
}

Manager Data;

} // namespace

Request::Request(std::vector<Command> commands)
: _commands(std::move(commands)) {
}

bool Request::check(Command command, int priority) {
	if (ranges::contains(_commands, command)
		&& priority > _handlerPriority) {
		_handlerPriority = priority;
		return true;
	}
	return false;
}

bool Request::handle(FnMut<bool()> handler) {
	_handler = std::move(handler);
	return true;
}

FnMut<bool()> RequestHandler(std::vector<Command> commands) {
	auto request = Request(std::move(commands));
	RequestsStream.fire(&request);
	return std::move(request._handler);
}

FnMut<bool()> RequestHandler(Command command) {
	return RequestHandler(std::vector<Command>{ command });
}

bool Launch(Command command) {
	if (auto handler = RequestHandler(command)) {
		return handler();
	}
	return false;
}

bool Launch(std::vector<Command> commands) {
	if (auto handler = RequestHandler(std::move(commands))) {
		return handler();
	}
	return false;
}

rpl::producer<not_null<Request*>> Requests() {
	return RequestsStream.events();
}

void Start() {
	Data.fill();
}

const QStringList &Errors() {
	return Data.errors();
}

bool HandleEvent(not_null<QShortcutEvent*> event) {
	return Launch(Data.lookup(event->shortcutId()));
}

void ToggleMediaShortcuts(bool toggled) {
	Data.toggleMedia(toggled);
}

void ToggleSupportShortcuts(bool toggled) {
	Data.toggleSupport(toggled);
}

void Finish() {
	Data.clear();
}

} // namespace Shortcuts
