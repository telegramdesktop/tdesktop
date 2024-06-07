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

#include <QAction>
#include <QShortcut>
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
	{ u"close_telegram"_q    , Command::Close },
	{ u"lock_telegram"_q     , Command::Lock },
	{ u"minimize_telegram"_q , Command::Minimize },
	{ u"quit_telegram"_q     , Command::Quit },

	{ u"media_play"_q        , Command::MediaPlay },
	{ u"media_pause"_q       , Command::MediaPause },
	{ u"media_playpause"_q   , Command::MediaPlayPause },
	{ u"media_stop"_q        , Command::MediaStop },
	{ u"media_previous"_q    , Command::MediaPrevious },
	{ u"media_next"_q        , Command::MediaNext },

	{ u"search"_q            , Command::Search },

	{ u"previous_chat"_q     , Command::ChatPrevious },
	{ u"next_chat"_q         , Command::ChatNext },
	{ u"first_chat"_q        , Command::ChatFirst },
	{ u"last_chat"_q         , Command::ChatLast },
	{ u"self_chat"_q         , Command::ChatSelf },

	{ u"previous_folder"_q   , Command::FolderPrevious },
	{ u"next_folder"_q       , Command::FolderNext },
	{ u"all_chats"_q         , Command::ShowAllChats },

	{ u"account1"_q          , Command::ShowAccount1 },
	{ u"account2"_q          , Command::ShowAccount2 },
	{ u"account3"_q          , Command::ShowAccount3 },
	{ u"account4"_q          , Command::ShowAccount4 },
	{ u"account5"_q          , Command::ShowAccount5 },
	{ u"account6"_q          , Command::ShowAccount6 },

	{ u"folder1"_q           , Command::ShowFolder1 },
	{ u"folder2"_q           , Command::ShowFolder2 },
	{ u"folder3"_q           , Command::ShowFolder3 },
	{ u"folder4"_q           , Command::ShowFolder4 },
	{ u"folder5"_q           , Command::ShowFolder5 },
	{ u"folder6"_q           , Command::ShowFolder6 },
	{ u"last_folder"_q       , Command::ShowFolderLast },

	{ u"show_archive"_q      , Command::ShowArchive },
	{ u"show_contacts"_q     , Command::ShowContacts },

	{ u"read_chat"_q         , Command::ReadChat },

	// Shortcuts that have no default values.
	{ u"message"_q                       , Command::JustSendMessage },
	{ u"message_silently"_q              , Command::SendSilentMessage },
	{ u"message_scheduled"_q             , Command::ScheduleMessage },
	{ u"media_viewer_video_fullscreen"_q , Command::MediaViewerFullscreen },
	{ u"show_scheduled"_q                , Command::ShowScheduled },
	{ u"archive_chat"_q                  , Command::ArchiveChat },
	//
};

const auto CommandNames = base::flat_map<Command, QString>{
	{ Command::Close          , u"close_telegram"_q },
	{ Command::Lock           , u"lock_telegram"_q },
	{ Command::Minimize       , u"minimize_telegram"_q },
	{ Command::Quit           , u"quit_telegram"_q },

	{ Command::MediaPlay      , u"media_play"_q },
	{ Command::MediaPause     , u"media_pause"_q },
	{ Command::MediaPlayPause , u"media_playpause"_q },
	{ Command::MediaStop      , u"media_stop"_q },
	{ Command::MediaPrevious  , u"media_previous"_q },
	{ Command::MediaNext      , u"media_next"_q },

	{ Command::Search         , u"search"_q },

	{ Command::ChatPrevious   , u"previous_chat"_q },
	{ Command::ChatNext       , u"next_chat"_q },
	{ Command::ChatFirst      , u"first_chat"_q },
	{ Command::ChatLast       , u"last_chat"_q },
	{ Command::ChatSelf       , u"self_chat"_q },

	{ Command::FolderPrevious , u"previous_folder"_q },
	{ Command::FolderNext     , u"next_folder"_q },
	{ Command::ShowAllChats   , u"all_chats"_q },

	{ Command::ShowFolder1    , u"folder1"_q },
	{ Command::ShowFolder2    , u"folder2"_q },
	{ Command::ShowFolder3    , u"folder3"_q },
	{ Command::ShowFolder4    , u"folder4"_q },
	{ Command::ShowFolder5    , u"folder5"_q },
	{ Command::ShowFolder6    , u"folder6"_q },
	{ Command::ShowFolderLast , u"last_folder"_q },

	{ Command::ShowArchive    , u"show_archive"_q },
	{ Command::ShowContacts   , u"show_contacts"_q },

	{ Command::ReadChat       , u"read_chat"_q },
};

[[maybe_unused]] constexpr auto kNoValue = {
	Command::JustSendMessage,
	Command::SendSilentMessage,
	Command::ScheduleMessage,
	Command::MediaViewerFullscreen,
	Command::ShowScheduled,
	Command::ArchiveChat,
};

class Manager {
public:
	void fill();
	void clear();

	[[nodiscard]] std::vector<Command> lookup(
		not_null<QObject*> object) const;
	void toggleMedia(bool toggled);
	void toggleSupport(bool toggled);
	void listen(not_null<QWidget*> widget);

	[[nodiscard]] const QStringList &errors() const;

private:
	void fillDefaults();
	void writeDefaultFile();
	bool readCustomFile();

	void set(const QString &keys, Command command, bool replace = false);
	void remove(const QString &keys);
	void unregister(base::unique_qptr<QAction> shortcut);

	QStringList _errors;

	base::flat_map<QKeySequence, base::unique_qptr<QAction>> _shortcuts;
	base::flat_multi_map<not_null<QObject*>, Command> _commandByObject;

	base::flat_set<QAction*> _mediaShortcuts;
	base::flat_set<QAction*> _supportShortcuts;

};

QString DefaultFilePath() {
	return cWorkingDir() + u"tdata/shortcuts-default.json"_q;
}

QString CustomFilePath() {
	return cWorkingDir() + u"tdata/shortcuts-custom.json"_q;
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
	const auto version = versionObject.constFind(u"version"_q);
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
	if (input.open(QIODevice::ReadOnly)
		&& output.open(QIODevice::WriteOnly)) {
#ifdef Q_OS_MAC
		auto text = qs(input.readAll());
		const auto note = R"(
// Note:
// On Apple platforms, reference to "ctrl" corresponds to the Command keys )"
			+ QByteArray()
			+ R"(on the Macintosh keyboard.
// On Apple platforms, reference to "meta" corresponds to the Control keys.

[
)";
		text.replace(u"\n\n["_q, QString(note));
		output.write(text.toUtf8());
#else
		output.write(input.readAll());
#endif // !Q_OS_MAC
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
	_commandByObject.clear();
	_mediaShortcuts.clear();
	_supportShortcuts.clear();
}

const QStringList &Manager::errors() const {
	return _errors;
}

std::vector<Command> Manager::lookup(not_null<QObject*> object) const {
	auto result = std::vector<Command>();
	auto i = _commandByObject.findFirst(object);
	const auto end = _commandByObject.end();
	for (; i != end && (i->first == object); ++i) {
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

void Manager::listen(not_null<QWidget*> widget) {
	for (const auto &[keys, shortcut] : _shortcuts) {
		widget->addAction(shortcut.get());
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
			_errors.push_front((u"While reading file '%1'..."_q
			).arg(file.fileName()));
		}
	});
	if (!file.open(QIODevice::ReadOnly)) {
		_errors.push_back(u"Could not read the file!"_q);
		return true;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError) {
		_errors.push_back((u"Failed to parse! Error: %2"_q
		).arg(error.errorString()));
		return true;
	} else if (!document.isArray()) {
		_errors.push_back(u"Failed to parse! Error: array expected"_q);
		return true;
	}
	const auto shortcuts = document.array();
	auto limit = kCountLimit;
	for (auto i = shortcuts.constBegin(), e = shortcuts.constEnd(); i != e; ++i) {
		if (!(*i).isObject()) {
			_errors.push_back(u"Bad entry! Error: object expected"_q);
			continue;
		}
		const auto entry = (*i).toObject();
		const auto keys = entry.constFind(u"keys"_q);
		const auto command = entry.constFind(u"command"_q);
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
			_errors.push_back(u"Too many entries! Limit is %1"_q.arg(
				kCountLimit));
			break;
		}
	}
	return true;
}

void Manager::fillDefaults() {
	const auto ctrl = Platform::IsMac() ? u"meta"_q : u"ctrl"_q;

	set(u"ctrl+w"_q, Command::Close);
	set(u"ctrl+f4"_q, Command::Close);
	set(u"ctrl+l"_q, Command::Lock);
	set(u"ctrl+m"_q, Command::Minimize);
	set(u"ctrl+q"_q, Command::Quit);

	set(u"media play"_q, Command::MediaPlay);
	set(u"media pause"_q, Command::MediaPause);
	set(u"toggle media play/pause"_q, Command::MediaPlayPause);
	set(u"media stop"_q, Command::MediaStop);
	set(u"media previous"_q, Command::MediaPrevious);
	set(u"media next"_q, Command::MediaNext);

	set(u"ctrl+f"_q, Command::Search);
	set(u"search"_q, Command::Search);
	set(u"find"_q, Command::Search);

	set(u"ctrl+pgdown"_q, Command::ChatNext);
	set(u"alt+down"_q, Command::ChatNext);
	set(u"ctrl+pgup"_q, Command::ChatPrevious);
	set(u"alt+up"_q, Command::ChatPrevious);

	set(u"%1+tab"_q.arg(ctrl), Command::ChatNext);
	set(u"%1+shift+tab"_q.arg(ctrl), Command::ChatPrevious);
	set(u"%1+backtab"_q.arg(ctrl), Command::ChatPrevious);

	set(u"ctrl+alt+home"_q, Command::ChatFirst);
	set(u"ctrl+alt+end"_q, Command::ChatLast);

	set(u"f5"_q, Command::SupportReloadTemplates);
	set(u"ctrl+delete"_q, Command::SupportToggleMuted);
	set(u"ctrl+insert"_q, Command::SupportScrollToCurrent);
	set(u"ctrl+shift+x"_q, Command::SupportHistoryBack);
	set(u"ctrl+shift+c"_q, Command::SupportHistoryForward);

	set(u"ctrl+1"_q, Command::ChatPinned1);
	set(u"ctrl+2"_q, Command::ChatPinned2);
	set(u"ctrl+3"_q, Command::ChatPinned3);
	set(u"ctrl+4"_q, Command::ChatPinned4);
	set(u"ctrl+5"_q, Command::ChatPinned5);
	set(u"ctrl+6"_q, Command::ChatPinned6);
	set(u"ctrl+7"_q, Command::ChatPinned7);
	set(u"ctrl+8"_q, Command::ChatPinned8);

	auto &&folders = ranges::views::zip(
		kShowFolder,
		ranges::views::ints(1, ranges::unreachable));

	for (const auto &[command, index] : folders) {
		set(u"%1+%2"_q.arg(ctrl).arg(index), command);
	}

	set(u"%1+shift+down"_q.arg(ctrl), Command::FolderNext);
	set(u"%1+shift+up"_q.arg(ctrl), Command::FolderPrevious);

	set(u"ctrl+0"_q, Command::ChatSelf);

	set(u"ctrl+9"_q, Command::ShowArchive);
	set(u"ctrl+j"_q, Command::ShowContacts);

	set(u"ctrl+r"_q, Command::ReadChat);
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
	version.insert(u"version"_q, QString::number(AppVersion));
	shortcuts.push_back(version);

	for (const auto &[sequence, shortcut] : _shortcuts) {
		const auto object = shortcut.get();
		auto i = _commandByObject.findFirst(object);
		const auto end = _commandByObject.end();
		for (; i != end && i->first == object; ++i) {
			const auto j = CommandNames.find(i->second);
			if (j != CommandNames.end()) {
				QJsonObject entry;
				entry.insert(u"keys"_q, sequence.toString().toLower());
				entry.insert(u"command"_q, j->second);
				shortcuts.append(entry);
			}
		}
	}

	// Commands without a default value.
	for (const auto c : ranges::views::concat(kShowAccount, kNoValue)) {
		for (const auto &[name, command] : CommandByName) {
			if (c == command) {
				auto entry = QJsonObject();
				entry.insert(u"keys"_q, QJsonValue());
				entry.insert(u"command"_q, name);
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
		_errors.push_back(u"Could not derive key sequence '%1'!"_q.arg(keys));
		return;
	}
	auto shortcut = base::make_unique_q<QAction>();
	shortcut->setShortcut(result);
	shortcut->setShortcutContext(Qt::ApplicationShortcut);
	if (!AutoRepeatCommands.contains(command)) {
		shortcut->setAutoRepeat(false);
	}
	const auto isMediaShortcut = MediaCommands.contains(command);
	const auto isSupportShortcut = SupportCommands.contains(command);
	if (isMediaShortcut || isSupportShortcut) {
		shortcut->setEnabled(false);
	}
	auto object = shortcut.get();
	auto i = _shortcuts.find(result);
	if (i == end(_shortcuts)) {
		i = _shortcuts.emplace(result, std::move(shortcut)).first;
	} else if (replace) {
		unregister(std::exchange(i->second, std::move(shortcut)));
	} else {
		object = i->second.get();
	}
	_commandByObject.emplace(object, command);
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
		_errors.push_back(u"Could not derive key sequence '%1'!"_q.arg(keys));
		return;
	}
	const auto i = _shortcuts.find(result);
	if (i != end(_shortcuts)) {
		unregister(std::move(i->second));
		_shortcuts.erase(i);
	}
}

void Manager::unregister(base::unique_qptr<QAction> shortcut) {
	if (shortcut) {
		_commandByObject.erase(shortcut.get());
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

bool HandleEvent(
		not_null<QObject*> object,
		not_null<QShortcutEvent*> event) {
	return Launch(Data.lookup(object));
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

void Listen(not_null<QWidget*> widget) {
	Data.listen(widget);
}

} // namespace Shortcuts
