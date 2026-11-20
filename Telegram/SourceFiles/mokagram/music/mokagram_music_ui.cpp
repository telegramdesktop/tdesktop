// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 7b8dec14
#include "mokagram/music/mokagram_music_ui.h"
#include "mokagram/ui/mokagram_box_parts.h"
#include "mokagram/lang/mokagram_lang.h"
#include "ui/widgets/rp_window.h"
#include "ui/boxes/confirm_box.h"
#include "mokagram/misc/mokagram_misc.h"

#include "mokagram/music/mokagram_soundcloud.h"
#include "mokagram/music/mokagram_soundcloud_login.h"
#include "mokagram/music/mokagram_music_canvas.h"
#include "mokagram/music/mokagram_music_library.h"
#include "mokagram/music/mokagram_lyrics.h"
#include "mokagram/music/mokagram_motion.h"
#include "mokagram/perf/mokagram_perf.h"
#include "media/clip/media_clip_reader.h"
#include "styles/style_mokagram_motion.h"
#include "mokagram/settings/mokagram_settings.h"
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTimer>

#include "mokagram/settings/mokagram_settings_box.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "base/options.h"
#include "mainwindow.h"
#include "base/random.h"
#include <set>
#include "core/application.h"
#include "core/core_settings.h"
#include "window/window_columns.h"
#include "window/window_controller.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "data/data_media_types.h"
#include "data/data_saved_music.h"
#include "data/data_search_controller.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "mokagram/plugins/mokagram_plugin_install.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "boxes/peer_list_controllers.h"
#include "styles/style_menu_icons.h"
#include "storage/localimageloader.h"
#include "storage/serialize_document.h"
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/version.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lottie/lottie_single_player.h"
#include "styles/style_chat_helpers.h"

#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/effects/animations.h"
#include <QtWidgets/QGraphicsEffect>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <array>
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/power_saving.h"
#include "ui/text/format_song_document_name.h"
#include "ui/vertical_list.h"
#include "ui/resize_area.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/scroll_area.h"
#include "media/player/media_player_volume_controller.h"
#include "window/window_session_controller.h"
#include "mainwidget.h"
#include "window/window_peer_menu.h"
#include "boxes/peer_list_controllers.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

#include <map>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QLinearGradient>
#include <QtGui/QTextLayout>
#include <QtGui/QGuiApplication>
#include "ui/chat/message_bubble.h"
#include "mokagram/music/mokagram_island_glyphs.h"
#include <QtSvg/QSvgRenderer>
#include <QtCore/QPointer>
#include <array>
#include <cmath>

namespace Mokagram::Music {
namespace {

class MusicSourceController final
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	MusicSourceController(
		not_null<Main::Session*> session,
		Fn<void(not_null<Data::Thread*>)> chosen)
	: ChatsListBoxController(session)
	, _session(session)
	, _chosen(std::move(chosen))
	, _key(u"music.pinned_sources.%1"_q.arg(session->uniqueId())) {
		Settings::Register({
			.key = _key,
			.kind = Settings::Kind::String,
			.defaultValue = QString(),
			.hidden = true,
		});
		_pins = Settings::String(_key).split(u',', Qt::SkipEmptyParts);
	}

	Main::Session &session() const override { return *_session; }

	void rowClicked(not_null<PeerListRow*> row) override {
		_chosen(_session->data().history(row->peer()));
	}

	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
			QWidget *parent,
			not_null<PeerListRow*> row) override {
		auto menu = base::make_unique_q<Ui::PopupMenu>(parent, st::popupMenuWithIcons);
		const auto id = QString::number(row->peer()->id.value);
		const auto pinned = _pins.contains(id);
		menu->addAction(QString::fromUtf8(Lang::Tr(pinned
			? "Открепить источник" : "Закрепить источник")), [=] {
			if (pinned) {
				_pins.removeAll(id);
			} else {
				_pins.prepend(id);
				if (!delegate()->peerListFindRow(row->id())) {
					delegate()->peerListPrependRow(createRow(
						_session->data().history(row->peer())));
				}
			}
			Settings::SetString(_key, _pins.join(u','));
			sortPins();
		});
		return menu;
	}

protected:
	void prepareViewHook() override {
		delegate()->peerListSetTitle(rpl::single(
			QString::fromUtf8(Lang::Tr("Источник музыки"))));
		for (const auto &id : _pins) {
			if (const auto peer = _session->data().peerLoaded(PeerId(id.toULongLong()))) {
				if (auto row = createRow(_session->data().history(peer))) {
					delegate()->peerListAppendRow(std::move(row));
				}
			}
		}
	}

	std::unique_ptr<Row> createRow(not_null<History*> history) override {
		const auto peer = history->peer;
		if (peer->isRepliesChat() || peer->isVerifyCodes()) return nullptr;
		crl::on_main(this, [=] { sortPins(); });
		return std::make_unique<Row>(history);
	}

private:
	void sortPins() {
		delegate()->peerListSortRows([=](const PeerListRow &a, const PeerListRow &b) {
			const auto rank = [&](const PeerListRow &row) {
				const auto i = _pins.indexOf(QString::number(row.peer()->id.value));
				return i < 0 ? _pins.size() : i;
			};
			return rank(a) < rank(b);
		});
		delegate()->peerListRefreshRows();
	}

	const not_null<Main::Session*> _session;
	Fn<void(not_null<Data::Thread*>)> _chosen;
	const QString _key;
	QStringList _pins;
};

void ShowMusicSourceBox(
		not_null<Window::SessionController*> controller,
		Fn<bool(not_null<Data::Thread*>)> chosen) {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	*weak = controller->show(Box<PeerListBox>(
		std::make_unique<MusicSourceController>(&controller->session(),
			[=](not_null<Data::Thread*> thread) {
				if (chosen(thread) && *weak) (*weak)->closeBox();
			}),
		[](not_null<PeerListBox*> box) {
			box->addButton(rpl::single(QString::fromUtf8(Lang::Tr("Отмена"))),
				[=] { box->closeBox(); });
		}));
}

constexpr auto kRowHeight = 76;
constexpr auto kSwitchDuration = crl::time(220);
constexpr auto kCoverFadeDuration = crl::time(320);
constexpr auto kTypingDelay = crl::time(400);
constexpr auto kArtSize = 56;

struct Radio {
	Track seed;
	QString sourceId;
	std::set<QString> seen;
	std::set<QString> cursors;
	QString next;
	bool exhausted = false;
	bool loading = false;
};

struct Queue {
	std::shared_ptr<Radio> radio;
	std::shared_ptr<Radio> pendingRadio;
	base::flat_map<QString, FullMsgId> messages;
	base::weak_ptr<Main::Session> session;
	std::vector<Track> tracks;       // очередь воспроизведения
	std::vector<Track> results;      // то, что сейчас показано в списке
	std::vector<Track> searchResults;
	std::vector<Track> forYou;
	QString forYouNote;
	int tab = 0;                     // 0 поиск, 1 лайкнутые, 2 для вас
	Track current;                   // играющий трек (с разобранными ссылками)
	bool recorded = true;            // прослушивание уже записано в историю
	QString lastQuery;
	bool custom = false;             // очередь собрана вручную
	int index = -1;
	QString currentId;
	int pendingToken = 0;
	rpl::lifetime lifetime;
	rpl::event_stream<> changes;
	base::weak_ptr<Window::SessionController> controller;
	bool subscribed = false;
	bool telegramSearch = false;
	PeerId selectedChat;
	// Умный поток: очередь сама пополняется, пока играет.
	bool stream = false;
	bool refilling = false;
	bool waiting = false;            // очередь кончилась, ждём пополнения
	int streamEpoch = 0;
	std::set<QString> autoIds;       // добавлено подборщиком, не вручную
};

Queue &QueueState() {
	static auto queue = Queue();
	return queue;
}

[[nodiscard]] bool IsTelegramTrack(const Track &track) {
	return track.id.startsWith(u"telegram:"_q);
}

[[nodiscard]] HistoryItem *TrackMessage(const Track &track) {
	const auto &queue = QueueState();
	const auto session = queue.session.get();
	const auto i = queue.messages.find(track.id);
	return (session && i != queue.messages.end())
		? session->data().message(i->second)
		: nullptr;
}

[[nodiscard]] bool CanForwardTrack(const Track &track) {
	if (!IsTelegramTrack(track)) {
		return true;
	}
	const auto item = TrackMessage(track);
	return item && item->allowsForward();
}

void BindController(not_null<Window::SessionController*> controller) {
	auto &queue = QueueState();
	if (queue.session.get() != &controller->session()) {
		++queue.pendingToken;
		queue.radio.reset();
		queue.pendingRadio.reset();
		queue.messages.clear();
		queue.selectedChat = {};
		queue.tracks.clear();
		queue.results.clear();
		queue.searchResults.clear();
		queue.current = {};
		queue.currentId.clear();
		queue.lastQuery.clear();
		queue.index = -1;
		queue.custom = false;
		queue.recorded = true;
		queue.stream = false;
		queue.refilling = false;
		queue.waiting = false;
		++queue.streamEpoch;
		queue.autoIds.clear();
		queue.session = base::make_weak(&controller->session());
	}
	queue.controller = base::make_weak(controller);
}

[[nodiscard]] Track TrackFromMessage(not_null<HistoryItem*> item) {
	const auto document = item->media() ? item->media()->document() : nullptr;
	if (!document || !document->isSong()) {
		return {};
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	auto track = Track();
	const auto id = item->fullId();
	track.id = u"telegram:%1:%2"_q.arg(id.peer.value).arg(id.msg.bare);
	track.title = name.title;
	track.artist = name.performer;
	track.durationMs = document->duration();
	QueueState().messages.emplace(track.id, id);
	return track;
}

class ChatMusicSearch final {
public:
	ChatMusicSearch(
		not_null<Main::Session*> session,
		Fn<void(std::vector<Track>, QString)> done,
		PeerId peerId = {});
	~ChatMusicSearch();
	void search(const QString &query);
	void searchMore();
	void cancel();
	[[nodiscard]] bool hasMore() const;

private:
	base::weak_ptr<Main::Session> _session;
	Fn<void(std::vector<Track>, QString)> _done;
	PeerId _peerId;
	QString _query;
	std::vector<Track> _tracks;
	Data::MessagePosition _offset;
	mtpRequestId _requestId = 0;
	int _rate = 0;
	int _loaded = 0;
	int _generation = 0;
	bool _more = false;

};

ChatMusicSearch::ChatMusicSearch(
	not_null<Main::Session*> session,
		Fn<void(std::vector<Track>, QString)> done,
		PeerId peerId)
: _session(base::make_weak(session))
, _done(std::move(done))
, _peerId(peerId) {
}

ChatMusicSearch::~ChatMusicSearch() {
	cancel();
}

void ChatMusicSearch::cancel() {
	++_generation;
	if (const auto session = _session.get()) {
		session->api().request(base::take(_requestId)).cancel();
	}
	_more = false;
}

void ChatMusicSearch::search(const QString &query) {
	cancel();
	_query = query;
	_tracks.clear();
	_offset = {};
	_rate = 0;
	_loaded = 0;
	_more = true;
	searchMore();
}

bool ChatMusicSearch::hasMore() const {
	return _more && !_requestId;
}

void ChatMusicSearch::searchMore() {
	const auto session = _session.get();
	if (!session || _requestId || !_more) {
		return;
	}
	const auto generation = _generation;
	const auto received = [=](const MTPmessages_Messages &result) {
		if (generation != _generation
			|| QueueState().session.get() != session) {
			return;
		}
		_requestId = 0;
		if (result.type() == mtpc_messages_messagesNotModified) {
			_more = false;
			_done(_tracks, QString());
			return;
		}
		const auto parsed = Api::ParseGlobalMediaResult(session, result, false);
		_loaded += int(parsed.messageIds.size());
		_more = !parsed.messageIds.empty()
			&& _loaded < parsed.fullCount
			&& (parsed.offsetPosition != _offset || parsed.offsetRate != _rate);
		_offset = parsed.offsetPosition;
		_rate = parsed.offsetRate;
		for (const auto &position : parsed.messageIds) {
			if (const auto item = session->data().message(position.fullId)) {
				auto track = TrackFromMessage(item);
				if (!track.id.isEmpty()
					&& !ranges::contains(_tracks, track.id, &Track::id)) {
					_tracks.push_back(std::move(track));
				}
			}
		}
		_done(_tracks, QString());
	};
	const auto failed = [=](const MTP::Error &error) {
		if (generation != _generation
			|| QueueState().session.get() != session) {
			return;
		}
		_requestId = 0;
		_done(_tracks, MTP::IgnoreError(error) ? QString() : error.type());
	};
	if (_peerId) {
		_requestId = session->api().request(MTPmessages_Search(
			MTP_flags(0),
			session->data().peer(_peerId)->input(),
			MTP_string(_query),
			MTP_inputPeerEmpty(),
			MTP_inputPeerEmpty(),
			MTP_vector<MTPReaction>(),
			MTP_int(0),
			MTP_inputMessagesFilterMusic(),
			MTP_int(0),
			MTP_int(0),
			MTP_int(_offset.fullId.msg),
			MTP_int(0),
			MTP_int(50),
			MTP_int(0),
			MTP_int(0),
			MTP_long(0)
		)).done(received).fail(failed).send();
	} else {
		_requestId = session->api().request(MTPmessages_SearchGlobal(
			MTP_flags(0),
			MTP_int(0),
			MTPInputChannel(),
			MTP_string(_query),
			MTP_inputMessagesFilterMusic(),
			MTP_int(0),
			MTP_int(0),
			MTP_int(_rate),
			(_offset.fullId.peer
				? session->data().peer(_offset.fullId.peer)->input()
				: MTP_inputPeerEmpty()),
			MTP_int(_offset.fullId.msg),
			MTP_int(50)
		)).done(received).fail(failed).send();
	}
}

[[nodiscard]] Track PlayingTrack() {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto document = current.audio();
	if (!document) {
		return {};
	}
	const auto &track = QueueState().current;
	if (OwnsTrack(current)
		&& !track.id.isEmpty()
		&& document->id == DocumentIdForTrack(track.id)) {
		return track;
	}
	if (const auto item = document->session().data().message(current.contextId())) {
		return TrackFromMessage(item);
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	auto result = Track();
	result.id = u"telegram:%1"_q.arg(document->id);
	result.title = name.title;
	result.artist = name.performer;
	result.durationMs = document->duration();
	return result;
}

[[nodiscard]] QString FormatDuration(int ms) {
	const auto seconds = ms / 1000;
	return u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

[[nodiscard]] QString ArtUrl(const QString &url, const QString &size) {
	auto result = url;
	result.replace(u"-large."_q, u"-"_q + size + u"."_q);
	return result;
}

// Mokagram: один поиск SoundCloud-двойника локальной песни, без точного совпадения пустой Track.
void ResolveLocalSoundCloud(const Track &track, Fn<void(Track)> done) {
	using Key = std::pair<QString, QString>;
	static auto found = std::map<Key, Track>();
	static auto pending = std::map<Key, std::vector<Fn<void(Track)>>>();
	const auto key = Key(track.title.trimmed(), track.artist.trimmed());
	if (key.first.isEmpty()) {
		done(Track());
		return;
	}
	if (const auto i = found.find(key); i != found.end()) {
		done(i->second);
		return;
	}
	const auto active = pending.contains(key);
	pending[key].push_back(std::move(done));
	if (active) {
		return;
	}
	Search(key.first, 0, [=](SearchResult result) {
		LOG(("[moka-dbg] sc search '%1' error '%2' n %3").arg(key.first).arg(result.error).arg(result.tracks.size()));
		for (auto i = 0; i < std::min(8, int(result.tracks.size())); ++i) LOG(("[moka-dbg]   cand '%1' / '%2' id %3").arg(result.tracks[i].title).arg(result.tracks[i].artist).arg(result.tracks[i].id));
		auto matches = std::vector<Track>();
		for (const auto &candidate : result.tracks) {
			if (candidate.title.trimmed() == key.first
				&& candidate.id.toULongLong()) {
				matches.push_back(candidate);
			}
		}
		if (matches.size() > 1) {
			std::erase_if(matches, [&](const Track &candidate) {
				return key.second.isEmpty()
					|| candidate.artist.trimmed() != key.second;
			});
		}
		const auto match = result.error.isEmpty() && matches.size() == 1
			? matches.front()
			: Track();
		if (result.error.isEmpty()) {
			if (found.size() >= 256) found.erase(found.begin());
			found[key] = match;
		}
		auto callbacks = std::move(pending[key]);
		pending.erase(key);
		for (auto &callback : callbacks) {
			callback(match);
		}
	});
}

void LoadLocalArtwork(const Track &track, Fn<void(QImage)> done) {
	ResolveLocalSoundCloud(track, [done = std::move(done)](Track match) mutable {
		LoadImage(
			match.id.isEmpty()
				? QString()
				: ArtUrl(match.artworkUrl, u"t500x500"_q),
			std::move(done));
	});
}

// Комментарии: у SoundCloud-трека свой id, у локальной песни id двойника.
void ResolveCommentsTrack(const Track &track, Fn<void(QString)> done) {
	LOG(("[moka-dbg] comments track id '%1' tg %2 title '%3' artist '%4'").arg(track.id).arg(IsTelegramTrack(track) ? 1 : 0).arg(track.title).arg(track.artist));
	if (track.id.toULongLong()
		&& QString::number(track.id.toULongLong()) == track.id) {
		done(track.id);
	} else if (!IsTelegramTrack(track)) {
		done(QString());
	} else {
		ResolveLocalSoundCloud(track, [=](Track match) {
			LOG(("[moka-dbg] comments match '%1' artist '%2'").arg(match.id).arg(match.artist));
			done(match.id);
		});
	}
}

[[nodiscard]] not_null<DocumentData*> MakeDocument(
		not_null<Main::Session*> session,
		const Track &track,
		const QString &path) {
	const auto size = QFileInfo(path).size();
	auto attributes = QVector<MTPDocumentAttribute>();
	attributes.push_back(MTP_documentAttributeAudio(
		MTP_flags(MTPDdocumentAttributeAudio::Flag::f_title
			| MTPDdocumentAttributeAudio::Flag::f_performer),
		MTP_int(track.durationMs / 1000),
		MTP_string(track.title),
		MTP_string(track.artist),
		MTPstring()));
	attributes.push_back(MTP_documentAttributeFilename(
		MTP_string(track.title + '.' + track.extension)));
	// Тот же маппинг, что и в DocumentIdForTrack: слишком длинный id не переполнит префикс.
	const auto id = DocumentId(DocumentIdForTrack(track.id));
	const auto document = session->data().document(
		id,
		0,
		QByteArray(),
		base::unixtime::now(),
		attributes,
		track.extension == u"mp3"_q ? u"audio/mpeg"_q : u"audio/mp4"_q,
		InlineImageLocation(),
		ImageWithLocation(),
		ImageWithLocation(),
		false,
		0,
		size);
	document->setLocation(Core::FileLocation(path));
	return document;
}

void PlayIndex(int index);
void RefillStream();
void Subscribe();
void PrefetchUpcoming();

void ResolveThen(const Track &track, Fn<void(Track, QString)> done) {
	if (!track.transcodingUrl.isEmpty()) {
		done(track, QString());
		return;
	}
	Resolve(track, std::move(done));
}

// Записывает в историю то, что успели послушать, до перехода на другой трек.
// Возвращает true, если трек пропущен в первые секунды.
bool RecordCurrentListen(bool finished) {
	auto &queue = QueueState();
	if (queue.recorded || queue.current.id.isEmpty()) {
		return false;
	}
	queue.recorded = true;
	auto listened = queue.current.durationMs;
	if (!finished) {
		const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
		const auto frequency = std::max<int64>(state.frequency, 1);
		listened = int(state.position * 1000 / frequency);
	}
	RecordListen(queue.current, listened, finished);
	return !finished && listened < 30 * 1000;
}

bool StreamEnabled() {
	return Mokagram::Settings::Bool(u"music.smart_stream"_q);
}

// Убирает из хвоста очереди то, что добавил подборщик (после позиции from).
void DropUpcoming(Fn<bool(const Track &)> predicate, int from, bool onlyAuto) {
	auto &queue = QueueState();
	for (auto i = int(queue.tracks.size()) - 1; i > from; --i) {
		const auto &t = queue.tracks[i];
		if ((!onlyAuto || queue.autoIds.contains(t.id)) && predicate(t)) {
			queue.autoIds.erase(t.id);
			queue.tracks.erase(queue.tracks.begin() + i);
		}
	}
	queue.changes.fire({});
	PrefetchUpcoming();
}

void RememberRadioTrack(Radio &radio, const Track &track) {
	for (const auto &key : TrackIdentityKeys(track)) {
		radio.seen.insert(key);
	}
}

[[nodiscard]] bool RadioAccepts(const Radio &radio, const Track &track) {
	if (!track.id.toULongLong() || IsArtistBlocked(track)) {
		return false;
	}
	for (const auto &key : TrackIdentityKeys(track)) {
		if (radio.seen.contains(key)) {
			return false;
		}
	}
	return true;
}

void FetchRadioBatch(
		std::shared_ptr<Radio> radio,
		Fn<bool()> valid,
		Fn<void(std::vector<Track>, QString)> done,
		int pagesLeft = 4) {
	const auto cursor = radio->next;
	LoadRelatedPage(radio->sourceId, cursor, [=](RelatedPage page) {
		if (!valid()) {
			return;
		}
		if (!page.error.isEmpty()) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr(
				"Не удалось загрузить радио. Попробуйте ещё раз.")));
			return;
		}
		radio->cursors.insert(cursor);
		radio->next = page.next;
		radio->exhausted = page.next.isEmpty()
			|| radio->cursors.contains(page.next);
		auto tracks = std::vector<Track>();
		for (auto &track : page.tracks) {
			if (RadioAccepts(*radio, track)) {
				RememberRadioTrack(*radio, track);
				tracks.push_back(std::move(track));
			}
		}
		if (tracks.empty() && !radio->exhausted && pagesLeft > 1) {
			FetchRadioBatch(radio, valid, done, pagesLeft - 1);
		} else {
			done(std::move(tracks), QString());
		}
	});
}

void RefillRadio() {
	auto &queue = QueueState();
	const auto radio = queue.radio;
	if (!radio || radio->loading) {
		return;
	}
	if (radio->exhausted) {
		if (!base::take(queue.waiting)) {
			return;
		}
		if (const auto controller = queue.controller.get()) {
			controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
				"Радио закончилось. Запустите радио от другого трека.")));
		}
		return;
	}
	for (const auto &track : queue.tracks) {
		RememberRadioTrack(*radio, track);
	}
	radio->loading = true;
	const auto session = queue.session;
	const auto valid = [=] {
		return session && QueueState().session.get() == session.get()
			&& QueueState().radio == radio;
	};
	FetchRadioBatch(radio, valid, [=](std::vector<Track> tracks, QString error) {
		auto &queue = QueueState();
		radio->loading = false;
		auto queued = Radio();
		for (const auto &track : queue.tracks) {
			RememberRadioTrack(queued, track);
		}
		for (auto &track : tracks) {
			if (RadioAccepts(queued, track)) {
				RememberRadioTrack(queued, track);
				queue.tracks.push_back(std::move(track));
			}
		}
		const auto waiting = base::take(queue.waiting);
		queue.changes.fire({});
		PrefetchUpcoming();
		if (waiting && queue.index + 1 < int(queue.tracks.size())) {
			PlayIndex(queue.index + 1);
		} else if (waiting || !error.isEmpty()) {
			if (const auto controller = queue.controller.get()) {
				controller->showToast(error.isEmpty()
					? QString::fromUtf8(Mokagram::Lang::Tr(
						"Нет новых подходящих треков для радио."))
					: error);
			}
		}
	});
}

void CommitRadio(
		std::shared_ptr<Radio> radio,
		std::vector<Track> tracks,
		const Track &seed,
		const QString &path) {
	auto &queue = QueueState();
	const auto controller = queue.controller.get();
	queue.pendingRadio.reset();
	if (!controller) {
		return;
	}
	if (IsArtistBlocked(seed)) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
			"Радио недоступно: автор скрыт")));
		return;
	}
	tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track &track) {
		return IsArtistBlocked(track);
	}), tracks.end());
	if (tracks.empty()) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
			"Нет новых подходящих треков для радио.")));
		return;
	}
	const auto keepPlaying = PlayingTrack().id == seed.id;
	if (!keepPlaying) {
		RecordCurrentListen(false);
	}
	++queue.pendingToken;
	++queue.streamEpoch;
	queue.pendingRadio.reset();
	queue.radio = radio;
	queue.stream = false;
	queue.refilling = false;
	queue.waiting = false;
	queue.custom = false;
	queue.autoIds.clear();
	queue.tracks = { seed };
	queue.tracks.insert(queue.tracks.end(), tracks.begin(), tracks.end());
	queue.index = 0;
	queue.currentId = seed.id;
	if (!keepPlaying && IsTelegramTrack(seed)) {
		PlayIndex(0);
	} else if (!keepPlaying) {
		queue.current = seed;
		queue.recorded = false;
		const auto document = MakeDocument(&controller->session(), seed, path);
		LoadImage(ArtUrl(seed.artworkUrl, u"t500x500"_q), [id = document->id](QImage image) {
			SetCover(id, std::move(image));
			QueueState().changes.fire({});
		});
		Media::Player::instance()->play(
			AudioMsgId(document, FullMsgId()),
			std::nullopt);
		RecordRecentTrack(seed);
	}
	Subscribe();
	queue.changes.fire({});
	PrefetchUpcoming();
	controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
		"Радио по треку: ")) + seed.title);
	if (queue.tracks.size() <= 4 && !radio->exhausted) {
		RefillRadio();
	}
}

void FailRadio() {
	QueueState().pendingRadio.reset();
	if (const auto controller = QueueState().controller.get()) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
			"Не удалось загрузить радио. Попробуйте ещё раз.")));
	}
}

void PrepareRadioSeed(
		std::shared_ptr<Radio> radio,
		std::vector<Track> tracks,
		Fn<bool()> valid) {
	const auto seed = radio->seed;
	if (IsTelegramTrack(seed)
		&& PlayingTrack().id != seed.id
		&& !TrackMessage(seed)) {
		FailRadio();
		return;
	}
	if (PlayingTrack().id == seed.id || IsTelegramTrack(seed)) {
		CommitRadio(radio, tracks, seed, {});
		return;
	}
	Resolve(seed, [=](Track resolved, QString error) {
		if (!valid()) {
			return;
		}
		if (!error.isEmpty()) {
			FailRadio();
			return;
		}
		Download(resolved, [=](const QString &path, const QString &error) {
			if (!valid()) {
				return;
			}
			if (!error.isEmpty()) {
				FailRadio();
				return;
			}
			CommitRadio(radio, tracks, resolved, path);
		});
	});
}

void StartRadio(
		not_null<Window::SessionController*> controller,
		Track seed) {
	BindController(controller);
	if ((!IsTelegramTrack(seed) && !seed.id.toULongLong())
		|| IsArtistBlocked(seed)) {
		return;
	}
	auto &queue = QueueState();
	const auto radio = std::make_shared<Radio>();
	radio->seed = seed;
	radio->sourceId = IsTelegramTrack(seed) ? QString() : seed.id;
	RememberRadioTrack(*radio, seed);
	queue.pendingRadio = radio;
	const auto session = queue.session;
	const auto token = queue.pendingToken;
	const auto playback = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto valid = [=] {
		return session && QueueState().session.get() == session.get()
			&& QueueState().pendingRadio == radio
			&& QueueState().pendingToken == token
			&& Media::Player::instance()->current(AudioMsgId::Type::Song) == playback;
	};
	controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
		"Загружаю радио по треку: ")) + seed.title);
	const auto fetch = [=] {
		FetchRadioBatch(radio, valid, [=](std::vector<Track> tracks, QString error) {
			const auto controller = QueueState().controller.get();
			if (!controller) {
				return;
			}
			if (!error.isEmpty() || tracks.empty()) {
				QueueState().pendingRadio.reset();
				controller->showToast(error.isEmpty()
					? QString::fromUtf8(Mokagram::Lang::Tr(
						"Нет новых подходящих треков для радио."))
					: error);
				return;
			}
			PrepareRadioSeed(radio, std::move(tracks), valid);
		});
	};
	if (!IsTelegramTrack(seed)) {
		fetch();
		return;
	}
	const auto metadata = MotionProtocol::Metadata(seed.title, seed.artist, false);
	Search(metadata.artist + u" "_q + metadata.title, 0, [=](SearchResult result) {
		if (!valid()) {
			return;
		}
		for (const auto &candidate : result.tracks) {
			const auto match = MotionProtocol::Metadata(
				candidate.title, candidate.artist, true);
			const auto hasDuration = seed.durationMs > 0 && candidate.durationMs > 0;
			if (candidate.id.toULongLong() && !IsArtistBlocked(candidate)
				&& MotionProtocol::MatchesTrack(
					metadata.title, metadata.artist, hasDuration ? seed.durationMs : 1,
					match.title, match.artist, hasDuration ? candidate.durationMs : 1)) {
				radio->sourceId = candidate.id;
				RememberRadioTrack(*radio, candidate);
				fetch();
				return;
			}
		}
		QueueState().pendingRadio.reset();
		if (const auto controller = QueueState().controller.get()) {
			controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr(
				result.error.isEmpty()
					? Mokagram::Lang::Tr("Не найден подходящий трек для радио.")
					: Mokagram::Lang::Tr("Не удалось загрузить радио. Попробуйте ещё раз."))));
		}
	});
}

void AddRadioAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const Track &track) {
	const auto supported = IsTelegramTrack(track)
		? (!track.title.trimmed().isEmpty() && !track.artist.trimmed().isEmpty())
		: (track.id.toULongLong() != 0);
	const auto text = !supported
		? Mokagram::Lang::Tr("Радио недоступно для треков Telegram")
		: IsArtistBlocked(track)
		? Mokagram::Lang::Tr("Радио недоступно: автор скрыт")
		: Mokagram::Lang::Tr("Радио по треку");
	const auto weak = base::make_weak(controller);
	menu->addAction(QString::fromUtf8(text), [=] {
		if (const auto controller = weak.get()) {
			StartRadio(controller, track);
		}
	}, &st::menuIconSoundOn)->setEnabled(supported && !IsArtistBlocked(track));
}

// Дозагрузка очереди с учётом того, что играет сейчас и что недавно было.
void RefillStream() {
	auto &queue = QueueState();
	if (queue.radio) {
		RefillRadio();
		return;
	}
	if (!queue.stream || queue.refilling || !StreamEnabled()) {
		return;
	}
	queue.refilling = true;
	const auto epoch = queue.streamEpoch;
	auto context = StreamContext();
	context.current = queue.current;
	const auto from = std::max(0, queue.index - 4);
	for (auto i = from; i < int(queue.tracks.size()); ++i) {
		if (i != queue.index) {
			context.avoid.push_back(queue.tracks[i]);
		}
	}
	BuildStream(std::move(context), 12, [=](std::vector<Track> tracks, QString note) {
		auto &queue = QueueState();
		if (epoch != queue.streamEpoch) {
			return;
		}
		queue.refilling = false;
		for (auto &t : tracks) {
			queue.autoIds.insert(t.id);
			queue.tracks.push_back(std::move(t));
		}
		queue.changes.fire({});
		PrefetchUpcoming();
		if (queue.waiting) {
			queue.waiting = false;
			if (queue.index + 1 < int(queue.tracks.size())) {
				PlayIndex(queue.index + 1);
			} else if (const auto controller = queue.controller.get(); controller && !note.isEmpty()) {
				controller->showToast(note);
			}
		}
	});
}

// Три следующих трека готовим заранее: переход на них будет без загрузки.
// Telegram-треки стримит сам плеер, их не трогаем.
void PrefetchUpcoming() {
	const auto &queue = QueueState();
	for (auto i = queue.index + 1, left = 3
		; left > 0 && i < int(queue.tracks.size())
		; ++i) {
		const auto &track = queue.tracks[i];
		if (IsTelegramTrack(track) || !track.id.toULongLong()) {
			continue;
		}
		--left;
		ResolveThen(track, [](Track next, QString error) {
			if (error.isEmpty()) {
				Download(next, [](const QString &, const QString &) {});
			}
		});
	}
}

void Subscribe() {
	auto &queue = QueueState();
	if (queue.subscribed) {
		return;
	}
	queue.subscribed = true;
	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		auto &queue = QueueState();
		if (queue.currentId.isEmpty() || !UsesQueue(state.id)) {
			return;
		}
		if (Media::Player::IsStoppedAtEnd(state.state)) {
			RecordCurrentListen(true);
			const auto count = int(queue.tracks.size());
			const auto &settings = Core::App().settings();
			if (settings.playerRepeatMode() == Media::RepeatMode::One) {
				PlayIndex(queue.index);
			} else if (base::options::value<bool>(
					Media::Player::kOptionDisableAutoplayNext)
				|| !Mokagram::Settings::Bool(u"player.autoplay_next"_q)) {
				return;
			} else if (queue.radio || (queue.stream && StreamEnabled())) {
				if (queue.index + 1 < count) {
					PlayIndex(queue.index + 1);
				} else {
					queue.waiting = true;
					RefillStream();
				}
			} else if (settings.playerOrderMode() == Media::OrderMode::Shuffle
				&& count > 1) {
				auto next = queue.index;
				while (next == queue.index) {
					next = base::RandomIndex(count);
				}
				PlayIndex(next);
			} else if (queue.index + 1 < count) {
				PlayIndex(queue.index + 1);
			} else if (settings.playerRepeatMode() == Media::RepeatMode::All
				&& count > 0) {
				PlayIndex(0);
			}
		}
	}, queue.lifetime);
}

void PlayIndex(int index) {
	auto &queue = QueueState();
	const auto controller = queue.controller.get();
	if (!controller || index < 0 || index >= int(queue.tracks.size())) {
		return;
	}
	Subscribe();
	const auto previousArtist = queue.current.artistId;
	const auto skippedEarly = RecordCurrentListen(false);
	if (skippedEarly && queue.stream && !previousArtist.isEmpty()) {
		// Пропуск в первые секунды: убираем автора из предстоящих авто-треков.
		DropUpcoming([=](const Track &t) {
			return t.artistId == previousArtist;
		}, std::max(queue.index, index), true);
	}
	const auto track = queue.tracks[index];
	const auto token = ++queue.pendingToken;
	queue.index = index;
	queue.currentId = track.id;
	queue.changes.fire({});
	PrefetchUpcoming();
	if (IsTelegramTrack(track)) {
		const auto item = TrackMessage(track);
		const auto document = item && item->media()
			? item->media()->document()
			: nullptr;
		if (!document) {
			controller->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Трек больше недоступен")));
			return;
		}
		queue.current = track;
		queue.recorded = true;
		Media::Player::instance()->play(
			AudioMsgId(document, item->fullId()),
			std::nullopt);
		return;
	}
	controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Загружаю: ")) + track.title);
	ResolveThen(track, [=](Track resolved, QString error) {
		auto &queue = QueueState();
		const auto controller = queue.controller.get();
		if (!controller || token != queue.pendingToken
			|| (queue.radio && IsArtistBlocked(track))) {
			return;
		}
		if (!error.isEmpty()) {
			controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить: ")) + error);
			return;
		}
		if (index < int(queue.tracks.size()) && queue.tracks[index].id == resolved.id) {
			queue.tracks[index] = resolved;
		}
		Download(resolved, [=](const QString &path, const QString &error) {
			auto &queue = QueueState();
			const auto controller = queue.controller.get();
			if (!controller || token != queue.pendingToken
				|| (queue.radio && IsArtistBlocked(resolved))) {
				return;
			}
			if (!error.isEmpty()) {
				controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить: ")) + error);
				return;
			}
			const auto document = MakeDocument(&controller->session(), resolved, path);
			LoadImage(ArtUrl(resolved.artworkUrl, u"t500x500"_q), [id = document->id](QImage image) {
				SetCover(id, std::move(image));
				QueueState().changes.fire({});
			});
			queue.current = resolved;
			queue.recorded = false;
			Media::Player::instance()->play(
				AudioMsgId(document, FullMsgId()),
				std::nullopt);
			RecordRecentTrack(resolved);
			if ((queue.radio || queue.stream)
				&& int(queue.tracks.size()) - index - 1 <= 3) {
				RefillStream();
			}
			PrefetchUpcoming();
		});
	});
}


[[nodiscard]] QString SafeName(const Track &track) {
	auto name = track.artist + u" - "_q + track.title;
	name.replace(QRegularExpression(u"[\\\\/:*?\"<>|]"_q), u"_"_q);
	return name.left(120).trimmed();
}

// Копия трека с понятным именем: оно уйдёт в Telegram как название файла.
[[nodiscard]] QString NamedCopy(const Track &track, const QString &path) {
	const auto dir = CacheDir() + u"/send"_q;
	QDir().mkpath(dir);
	const auto named = dir + '/' + SafeName(track) + '.' + track.extension;
	QFile::remove(named);
	QFile::copy(path, named);
	return named;
}

void EnsureFile(
		const Track &track,
		Fn<void(const QString &, const Track &)> done) {
	const auto controller = QueueState().controller.get();
	ResolveThen(track, [=](Track resolved, QString error) {
		if (!error.isEmpty()) {
			if (const auto controller = QueueState().controller.get()) {
				controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить: ")) + error);
			}
			return;
		}
		Download(resolved, [=](const QString &path, const QString &error) {
			if (!error.isEmpty()) {
				if (const auto controller = QueueState().controller.get()) {
					controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить: ")) + error);
				}
				return;
			}
			done(path, resolved);
		});
	});
	if (controller) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Загружаю: ")) + track.title);
	}
}

void UploadTrack(
		not_null<Data::Thread*> thread,
		const Track &track,
		const QString &path,
		const QImage &cover) {
	const auto session = &thread->owningHistory()->session();
	auto list = Storage::PrepareMediaList(
		QStringList{ NamedCopy(track, path) },
		st::sendMediaPreviewSize,
		session->user()->isPremium());
	if (list.error != Ui::PreparedList::Error::None || list.files.empty()) {
		return;
	}
	auto &file = list.files.front();
	auto &information = *file.information;
	auto song = std::get_if<Ui::PreparedFileInformation::Song>(
		&information.media);
	if (!song) {
		information.media = Ui::PreparedFileInformation::Song();
		song = std::get_if<Ui::PreparedFileInformation::Song>(
			&information.media);
	}
	if (song->duration <= 0) {
		song->duration = track.durationMs;
	}
	if (!track.title.isEmpty()) {
		song->title = track.title;
	}
	if (!track.artist.isEmpty()) {
		song->performer = track.artist;
	}
	if (!cover.isNull()) {
		song->cover = cover;
	}
	file.type = Ui::PreparedFile::Type::Music;
	auto action = Api::SendAction(thread);
	action.clearDraft = false;
	session->api().sendFiles(
		std::move(list),
		SendMediaType::File,
		nullptr,
		std::move(action));
}

void SendTrack(
		not_null<Data::Thread*> thread,
		const Track &track,
		const QString &path) {
	const auto cover = CoverForDocument(DocumentIdForTrack(track.id));
	if (!cover.isNull() || track.artworkUrl.isEmpty()) {
		UploadTrack(thread, track, path, cover);
		return;
	}
	const auto weak = base::make_weak(thread);
	LoadImage(ArtUrl(track.artworkUrl, u"t500x500"_q), [=](QImage image) {
		if (const auto thread = weak.get()) {
			UploadTrack(thread, track, path, image);
		}
	});
}

void SaveToDisk(const Track &track) {
	EnsureFile(track, [=](const QString &path, const Track &track) {
		auto dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
		if (dir.isEmpty()) {
			dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
		}
		QDir().mkpath(dir);
		const auto target = dir + '/' + SafeName(track) + '.' + track.extension;
		QFile::remove(target);
		const auto controller = QueueState().controller.get();
		if (QFile::copy(path, target)) {
			if (controller) {
				controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Сохранено: ")) + target, 4000);
			}
		} else if (controller) {
			controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось сохранить файл")));
		}
	});
}

void ForwardTrack(
		not_null<Window::SessionController*> controller,
		const Track &track) {
	if (IsTelegramTrack(track)) {
		const auto item = TrackMessage(track);
		if (item && item->allowsForward()) {
			Window::ShowForwardMessagesBox(
				controller,
				MessageIdsList{ item->fullId() });
		}
		return;
	}
	Window::ShowChooseRecipientBox(
		controller,
		[=](not_null<Data::Thread*> thread) {
			const auto weak = base::make_weak(thread);
			EnsureFile(track, [=](const QString &path, const Track &track) {
				if (const auto thread = weak.get()) {
					SendTrack(thread, track, path);
				}
			});
			return true;
		},
		rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Переслать трек"))));
}

// Профильная музыка Telegram хранится в документах: заливаем файл в «Избранное»
// и, когда он загрузился, сохраняем в профиль.
void AddToProfile(
		not_null<Window::SessionController*> controller,
		const Track &track) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(session);
	EnsureFile(track, [=](const QString &path, const Track &track) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto history = strong->data().history(strong->user());
		SendTrack(history, track, path);
		const auto name = SafeName(track) + '.' + track.extension;
		const auto timer = new QTimer();
		const auto tries = std::make_shared<int>(0);
		timer->setInterval(1500);
		QObject::connect(timer, &QTimer::timeout, [=] {
			const auto session = weak.get();
			const auto finish = [&] {
				timer->stop();
				timer->deleteLater();
			};
			if (!session || ++*tries > 80) {
				finish();
				return;
			}
			const auto history = session->data().history(session->user());
			for (const auto &block : history->blocks) {
				for (const auto &view : block->messages) {
					const auto item = view->data();
					const auto document = item->media()
						? item->media()->document()
						: nullptr;
					if (!document
						|| item->isSending()
						|| item->hasFailed()
						|| document->filename() != name) {
						continue;
					}
					session->data().savedMusic().save(
						document,
						Data::FileOrigin(item->fullId()));
					if (const auto controller = QueueState().controller.get()) {
						controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Добавлено в профиль: ")) + track.title);
					}
					finish();
					return;
				}
			}
		});
		timer->start();
	});
}


// Клик по треку из результатов поиска.
[[nodiscard]] std::vector<Track> &ContextTracks() {
	static auto result = std::vector<Track>();
	return result;
}

void PlayFromResults(int resultIndex);

// Трек из списка чата, откуда играет Telegram: запускаем его уже внутри очереди плеера.
void PlayContext(int index) {
	auto &queue = QueueState();
	if (index < 0 || index >= int(ContextTracks().size())) {
		return;
	}
	queue.results = ContextTracks();
	PlayFromResults(index);
}

void PlayFromResults(int resultIndex) {
	auto &queue = QueueState();
	if (resultIndex < 0 || resultIndex >= int(queue.results.size())) {
		return;
	}
	const auto track = queue.results[resultIndex];
	if (!queue.custom) {
		queue.radio.reset();
		queue.pendingRadio.reset();
		queue.tracks = queue.results;
		queue.stream = false;
		++queue.streamEpoch;
		queue.refilling = false;
		PlayIndex(resultIndex);
		return;
	}
	for (auto i = 0; i != int(queue.tracks.size()); ++i) {
		if (queue.tracks[i].id == track.id) {
			PlayIndex(i);
			return;
		}
	}
	const auto at = std::min<int>(queue.index + 1, queue.tracks.size());
	queue.tracks.insert(queue.tracks.begin() + at, track);
	PlayIndex(at);
}

void EnqueueNext(const Track &track) {
	auto &queue = QueueState();
	queue.custom = true;
	const auto at = std::min<int>(queue.index + 1, queue.tracks.size());
	queue.tracks.insert(queue.tracks.begin() + at, track);
	queue.changes.fire({});
}

void EnqueueLast(const Track &track) {
	auto &queue = QueueState();
	queue.custom = true;
	queue.tracks.push_back(track);
	queue.changes.fire({});
}

void QueueMove(int index, int delta) {
	auto &queue = QueueState();
	const auto target = index + delta;
	if (index < 0 || index >= int(queue.tracks.size())
		|| target < 0 || target >= int(queue.tracks.size())) {
		return;
	}
	std::swap(queue.tracks[index], queue.tracks[target]);
	if (queue.index == index) {
		queue.index = target;
	} else if (queue.index == target) {
		queue.index = index;
	}
	queue.custom = true;
	queue.changes.fire({});
}

void QueueRemove(int index) {
	auto &queue = QueueState();
	if (index < 0 || index >= int(queue.tracks.size())) {
		return;
	}
	queue.tracks.erase(queue.tracks.begin() + index);
	if (queue.index > index) {
		--queue.index;
	} else if (queue.index == index) {
		queue.index = -1;
	}
	queue.custom = true;
	queue.changes.fire({});
}

// Сердце из Ionicons (MIT): ion:heart и ion:heart-outline, viewBox 512x512.
void DrawHeart(QPainter &p, const QRectF &r, bool filled, const QColor &color) {
	static auto cache = std::map<std::pair<bool, QRgb>, std::unique_ptr<QSvgRenderer>>();
	auto &renderer = cache[{ filled, color.rgba() }];
	if (!renderer) {
		const auto paint = color.name(QColor::HexRgb).toLatin1();
		const auto opacity = QByteArray::number(color.alphaF(), 'f', 3);
		const auto shape = filled
			? QByteArray("<path fill=\"") + paint + "\" fill-opacity=\"" + opacity
				+ "\" d=\"M256 448a32 32 0 0 1-18-5.57c-78.59-53.35-112.62-89.93-131.39-112.8c-40-48.75-59.15-98.8-58.61-153C48.63 114.52 98.46 64 159.08 64c44.08 0 74.61 24.83 92.39 45.51a6 6 0 0 0 9.06 0C278.31 88.81 308.84 64 352.92 64c60.62 0 110.45 50.52 111.08 112.64c.54 54.21-18.63 104.26-58.61 153c-18.77 22.87-52.8 59.45-131.39 112.8a32 32 0 0 1-18 5.56\"/>"
			: QByteArray("<path fill=\"none\" stroke=\"") + paint + "\" stroke-opacity=\"" + opacity
				+ "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"32\" d=\"M352.92 80C288 80 256 144 256 144s-32-64-96.92-64c-52.76 0-94.54 44.14-95.08 96.81c-1.1 109.33 86.73 187.08 183 252.42a16 16 0 0 0 18 0c96.26-65.34 184.09-143.09 183-252.42c-.54-52.67-42.32-96.81-95.08-96.81\"/>";
		renderer = std::make_unique<QSvgRenderer>(
			QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">")
			+ shape + "</svg>");
	}
	const auto side = std::min(r.width(), r.height());
	renderer->render(&p, QRectF(
		r.center().x() - side / 2,
		r.center().y() - side / 2,
		side,
		side));
}

[[nodiscard]] const QColor &HeartColor() {
	static const auto color = QColor(255, 77, 109);
	return color;
}

constexpr auto kLikeDuration = crl::time(650);

[[nodiscard]] bool LikeAnimationsEnabled() {
	return !PowerSaving::On(PowerSaving::kAnimations);
}

// Масштаб сердца по ходу анимации лайка: затухающая пружина к 1.
[[nodiscard]] float64 LikeScale(float64 progress, bool liked) {
	const auto from = liked ? 0.4 : 0.75;
	return 1.
		- (1. - from) * std::exp(-7. * progress) * std::cos(14. * progress);
}

// Вспышка вокруг сердца при лайке: расходящееся кольцо и искры.
void DrawLikeBurst(
		QPainter &p,
		QPointF center,
		float64 radius,
		float64 progress,
		const QColor &color) {
	const auto easeOut = [](float64 t) {
		return 1. - std::pow(1. - t, 3);
	};
	p.save();
	if (progress < 0.45) {
		const auto t = progress / 0.45;
		auto ring = color;
		ring.setAlphaF(0.9 * (1. - t));
		const auto width = std::max(radius * 0.3 * (1. - t), 0.5);
		const auto r = radius * (0.35 + 0.6 * easeOut(t)) - width / 2.;
		p.setPen(QPen(ring, width));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(center, r, r);
	}
	const auto t = std::clamp((progress - 0.12) / 0.6, 0., 1.);
	if (t > 0. && t < 1.) {
		static const auto spark = QColor(255, 179, 71);
		const auto distance = radius * (0.55 + 0.35 * easeOut(t));
		const auto size = radius * 0.12 * (1. - t);
		constexpr auto kCount = 7;
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != kCount; ++i) {
			const auto angle = -M_PI / 2. + i * 2. * M_PI / kCount;
			const auto big = center
				+ QPointF(std::cos(angle), std::sin(angle)) * distance;
			const auto side = angle + 0.35;
			const auto small = center
				+ QPointF(std::cos(side), std::sin(side)) * distance * 0.8;
			p.setBrush(color);
			p.drawEllipse(big, size, size);
			p.setBrush(spark);
			p.drawEllipse(small, size * 0.7, size * 0.7);
		}
	}
	p.restore();
}

class TabPill final : public Ui::AbstractButton {
public:
	TabPill(QWidget *parent, const QString &text)
	: AbstractButton(parent)
	, _text(text) {
		resize(width(), 40);
		setPointerCursor(true);
		paintRequest(
		) | rpl::on_next([=](QRect) {
			// Плашка и дорожка рисуются родителем и едут между вкладками,
			// здесь только подпись.
			auto p = QPainter(this);
			const auto progress = _fade.value(_active ? 1. : 0.);
			p.setFont(st::semiboldFont);
			p.setPen(anim::color(
				isOver() ? st::windowFg->c : st::windowSubTextFg->c,
				st::windowFg->c,
				progress));
			p.drawText(rect(), Qt::AlignCenter, _text);
		}, lifetime());
	}
	void setActive(bool active) {
		if (_active != active) {
			_active = active;
			_fade.start(
				[=] { update(); },
				_active ? 0. : 1.,
				_active ? 1. : 0.,
				kSwitchDuration,
				anim::easeOutCubic);
		}
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		update();
	}

private:
	QString _text;
	Ui::Animations::Simple _fade;
	bool _active = false;

};

class SourceSwitch final : public Ui::AbstractButton {
public:
	SourceSwitch(QWidget *parent, QString first, QString second)
	: AbstractButton(parent)
	, _labels{ std::move(first), std::move(second) } {
	resize(width(), 40);
	setPointerCursor(true);
	setClickedCallback([=] {
		const auto chats = mapFromGlobal(QCursor::pos()).x() >= width() / 2;
		if (_chats != chats && _changed) {
			_changed(chats);
		}
	});
	paintRequest(
	) | rpl::on_next([=](QRect) {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		const auto margin = 4;
		const auto half = (width() - 2 * margin) / 2;
		const auto progress = _animation.value(_chats ? 1. : 0.);
		// Плашка едет к выбранной половине и слегка вытягивается на ходу.
		const auto stretch = int(base::SafeRound(
			half * 0.18 * (1. - std::abs(2. * progress - 1.))));
		const auto left = margin + int(base::SafeRound(half * progress));
		const auto selected = QRect(
			left - stretch / 2,
			margin,
			half + stretch,
			height() - 2 * margin);
		p.setBrush(st::windowActiveTextFg->c);
		p.drawRoundedRect(selected, selected.height() / 2., selected.height() / 2.);
		p.setFont(st::semiboldFont);
		for (auto i = 0; i != 2; ++i) {
			const auto active = (i == 1) ? progress : (1. - progress);
			p.setPen(anim::color(st::windowSubTextFg, st::windowFg, active));
			p.drawText(QRect(i * width() / 2, 0, width() / 2, height()), Qt::AlignCenter, _labels[i]);
		}
	}, lifetime());
	}
	void setChats(bool chats) {
		if (_chats != chats) {
			_chats = chats;
			_animation.start(
				[=] { update(); },
				_chats ? 0. : 1.,
				_chats ? 1. : 0.,
				kSwitchDuration,
				anim::easeOutCubic);
		}
	}
	void setChangedCallback(Fn<void(bool)> callback) {
		_changed = std::move(callback);
	}
	void finishAnimating() {
		_animation.stop();
		update();
	}

private:
	const std::array<QString, 2> _labels;
	Ui::Animations::Simple _animation;
	Fn<void(bool)> _changed;
	bool _chats = false;

};

class PicksBar final : public Ui::AbstractButton {
public:
	PicksBar(QWidget *parent, Fn<void()> play, Fn<void()> refresh)
	: AbstractButton(parent) {
		resize(width(), 36);
		setPointerCursor(true);
		setClickedCallback([=] {
			if (refreshRect().contains(mapFromGlobal(QCursor::pos()))) {
				refresh();
			} else {
				play();
			}
		});
		paintRequest(
		) | rpl::on_next([=](QRect) {
			auto p = QPainter(this);
			// Только текст, без значков и плашек.
			const auto onRefresh = refreshRect().contains(
				mapFromGlobal(QCursor::pos()));
			p.setFont(st::semiboldFont);
			p.setPen(onRefresh || !isOver()
				? st::windowActiveTextFg
				: st::windowFg);
			p.drawText(
				QRect(8, 0, width() - 8 - 110, height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				QString::fromUtf8(Mokagram::Lang::Tr("Слушать подборку")));
			p.setFont(st::normalFont);
			p.setPen(onRefresh ? st::windowFg : st::windowSubTextFg);
			p.drawText(
				refreshRect(),
				Qt::AlignVCenter | Qt::AlignRight,
				QString::fromUtf8(Mokagram::Lang::Tr("Обновить")));
		}, lifetime());
	}

private:
	[[nodiscard]] QRect refreshRect() const {
		return QRect(width() - 110, 0, 110 - 8, height());
	}

};

// Карточка SoundCloud: вход и обновление лайков или плейлистов.
class SoundCloudAccountBar final : public Ui::AbstractButton {
public:
	SoundCloudAccountBar(
			QWidget *parent,
			Fn<void()> login,
			Fn<void()> logout,
			bool playlists = false)
	: AbstractButton(parent)
	, _login(std::move(login))
	, _logout(std::move(logout))
	, _playlists(playlists) {
		// Высоту задаёт раскладка: resize() в конструкторе дал бы нулевую
		// высоту и кнопка не ловила бы клики.
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		_refreshButton = Ui::CreateChild<Ui::RoundButton>(
			this,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Обновить"))),
			actionStyle());
		_disconnectButton = Ui::CreateChild<Ui::RoundButton>(
			this,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отключить"))),
			actionStyle());
		_disconnectButton->setClickedCallback(_logout);
		_refreshButton->setClickedCallback([=] {
			if (_syncing || !Mokagram::Music::Account::IsLoggedIn()) {
				return;
			}
			_syncing = true;
			_syncNote = QString::fromUtf8(Mokagram::Lang::Tr("Обновляем…"));
			_refreshButton->setDisabled(true);
			refresh();
			const auto finish = crl::guard(this, [=](int, int, QString error) {
					_syncing = false;
					_syncNote = std::move(error);
					_refreshButton->setDisabled(false);
					refresh();
				});
			if (_playlists) {
				Mokagram::Music::Account::SyncPlaylists(finish);
			} else {
				Mokagram::Music::Account::SyncLikes(finish);
			}
		});
		refresh();
		setClickedCallback([=] {
			if (!Mokagram::Music::Account::IsLoggedIn()) {
				_login();
			}
		});
		Mokagram::Music::Account::Changes() | rpl::on_next([=] {
			refresh();
		}, lifetime());
		if (_playlists) {
			PlaylistsChanged() | rpl::on_next([=] {
				refresh();
			}, lifetime());
		} else {
			LikesChanged() | rpl::on_next([=] {
				refresh();
			}, lifetime());
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto buttonWidth = (newWidth - s(36)) / 2;
		_refreshButton->setFullWidth(buttonWidth);
		_disconnectButton->setFullWidth(buttonWidth);
		_refreshButton->move(s(14), s(68));
		_disconnectButton->move(newWidth - s(14) - buttonWidth, s(68));
		return s(_logged ? 122 : 72);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto r = rect();
		p.setPen(Qt::NoPen);
		p.setBrush(_logged || !isOver() ? st::windowBg->c : st::windowBgOver->c);
		p.drawRoundedRect(r, s(22), s(22));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(r.adjusted(s(2), s(2), -s(2), -s(2)), s(20), s(20));
		}
		const auto badge = QRect(
			s(14),
			s(14),
			s(44),
			s(44));
		if (_avatar.isNull()) {
			Mokagram::Music::PaintSoundCloudBadge(p, badge);
		} else {
			// Аватарка профиля кругом, фирменный значок маленьким в углу.
			p.save();
			auto clip = QPainterPath();
			clip.addEllipse(badge);
			p.setClipPath(clip);
			p.drawImage(badge, _avatar);
			p.restore();
			const auto mark = s(20);
			const auto markRect = QRect(
				badge.right() - mark + s(4),
				badge.bottom() - mark + s(4),
				mark,
				mark);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg->c);
			p.drawEllipse(markRect.adjusted(-s(2), -s(2), s(2), s(2)));
			Mokagram::Music::PaintSoundCloudBadge(p, markRect);
		}

		auto titleFont = st::semiboldFont->f;
		titleFont.setPixelSize(s(15));
		p.setFont(titleFont);
		p.setPen(st::windowFg);
		const auto textLeft = badge.right() + s(14);
		const auto textRight = width() - s(16);
		p.drawText(
			QRect(textLeft, s(15), textRight - textLeft, s(20)),
			Qt::AlignVCenter | Qt::AlignLeft,
			_logged
				? QString::fromUtf8(Mokagram::Lang::Tr("SoundCloud"))
				: QString::fromUtf8(Mokagram::Lang::Tr("Подключить SoundCloud")));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto noteFont = QFontMetrics(st::normalFont->f);
		p.drawText(
			QRect(textLeft, s(37), textRight - textLeft, s(18)),
			Qt::AlignVCenter | Qt::AlignLeft,
			noteFont.elidedText(
				_note,
				Qt::ElideRight,
				textRight - textLeft));
	}

private:
	static const style::RoundButton &actionStyle() {
		static const auto result = [] {
			auto result = Mokagram::BoxParts::PillLight();
			result.textBg = st::windowBgOver;
			result.textBgOver = st::windowBgRipple;
			result.textFg = st::windowFg;
			result.textFgOver = st::windowFg;
			return result;
		}();
		return result;
	}

	void refresh() {
		_logged = Mokagram::Music::Account::IsLoggedIn();
		const auto info = Mokagram::Music::Account::User();
		const auto playlists = Playlists();
		const auto count = _playlists
			? int(std::count_if(playlists.begin(), playlists.end(), [](const Playlist &p) {
				return p.remote;
			}))
			: int(LikedTracks().size());
		if (!_logged) {
			_note = QString::fromUtf8(Mokagram::Lang::Tr(
				_playlists
					? "Плейлисты из аккаунта SoundCloud"
					: "Лайки и рекомендации из аккаунта"));
		} else {
			const auto who = info.username.isEmpty()
				? QString::fromUtf8(Mokagram::Lang::Tr("Аккаунт подключён"))
				: (u"@"_q + info.username);
			const auto countText = _playlists
				? QString::fromUtf8(Mokagram::Lang::Tr("плейлистов: %1"))
				: QString::fromUtf8(Mokagram::Lang::Tr("лайков: %1"));
			_note = (count > 0)
				? (who + u" · "_q + countText.arg(count))
				: who;
		}
		if (_logged
			&& info.avatarUrl.contains(u"sndcdn.com"_q)
			&& info.avatarUrl != _avatarUrl) {
			_avatarUrl = info.avatarUrl;
			LoadImage(ArtUrl(_avatarUrl, u"t200x200"_q), crl::guard(this, [=](QImage image) {
				_avatar = std::move(image);
				update();
			}));
		} else if (!_logged) {
			_avatarUrl = QString();
			_avatar = QImage();
		}
		if (!_syncNote.isEmpty() && _logged) {
			_note = _syncNote;
		}
		_refreshButton->setVisible(_logged);
		_disconnectButton->setVisible(_logged);
		resizeToWidth(width());
		update();
	}

	Ui::RoundButton *_refreshButton = nullptr;
	Ui::RoundButton *_disconnectButton = nullptr;
	QString _syncNote;
	bool _syncing = false;
	Fn<void()> _login;
	Fn<void()> _logout;
	bool _playlists = false;
	QString _note;
	QImage _avatar;
	QString _avatarUrl;
	bool _logged = false;

};

void PlayAll(const std::vector<Track> &tracks) {
	auto &queue = QueueState();
	if (tracks.empty()) {
		return;
	}
	queue.custom = false;
	queue.radio.reset();
	queue.pendingRadio.reset();
	queue.tracks = tracks;
	queue.stream = StreamEnabled();
	queue.refilling = false;
	queue.waiting = false;
	++queue.streamEpoch;
	queue.autoIds.clear();
	if (queue.stream) {
		for (const auto &t : tracks) {
			queue.autoIds.insert(t.id);
		}
	}
	PlayIndex(0);
}

class TrackLikeButton final : public Ui::AbstractButton {
public:
	TrackLikeButton(QWidget *parent, Track track)
	: AbstractButton(parent)
	, _track(std::move(track)) {
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		const auto refresh = [=] {
			const auto text = IsLiked(_track.id)
				? "Убрать из избранного"
				: "В избранное";
			setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr(text)) + u": "_q + _track.title);
			update();
		};
		refresh();
		LikesChanged() | rpl::on_next(refresh, lifetime());
		setClickedCallback([=] { SetLiked(_track, !IsLiked(_track.id)); });
		paintRequest() | rpl::on_next([=](QRect) {
			auto p = QPainter(this);
			auto hq = PainterHighQualityEnabler(p);
			const auto liked = IsLiked(_track.id);
			DrawHeart(p, QRectF(rect()).adjusted(5, 6, -5, -5), liked,
				liked ? HeartColor() : QColor(st::windowSubTextFg->c));
			if (hasFocus()) {
				p.setPen(QPen(st::windowActiveTextFg->c, 2));
				p.setBrush(Qt::NoBrush);
				p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 5, 5);
			}
		}, lifetime());
	}

private:
	Track _track;

};

// Плавное наведение с разворотом от текущего кадра.
struct HoverFade {
	Ui::Animations::Simple animation;
	bool on = false;

	void set(bool now, Fn<void()> repaint) {
		if (on == now) {
			return;
		}
		const auto from = value();
		on = now;
		if (!enabled()) {
			animation.stop();
			repaint();
			return;
		}
		animation.start(
			std::move(repaint),
			from,
			now ? 1. : 0.,
			now ? 160 : 120,
			anim::easeOutCubic);
	}
	[[nodiscard]] double value() const {
		const auto target = on ? 1. : 0.;
		return enabled() ? animation.value(target) : target;
	}

private:
	[[nodiscard]] static bool enabled() {
		return !anim::Disabled() && !PowerSaving::On(PowerSaving::kAnimations);
	}
};

[[nodiscard]] QRect HomeActionRect(const QRect &art) {
	const auto s = [](int v) { return style::ConvertScale(v); };
	const auto size = std::min(s(38), std::max(art.width() - s(16), 0));
	return QRect(art.right() - s(8) - size + 1,
		art.bottom() - s(8) - size + 1, size, size);
}

struct HomeTrackPlaybackState {
	bool current = false;
	bool playing = false;
};

[[nodiscard]] HomeTrackPlaybackState HomeTrackPlayback(const QString &id) {
	if (PlayingTrack().id != id) {
		return {};
	}
	const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
	return {
		!Media::Player::IsStoppedOrStopping(state.state),
		state.state == Media::Player::State::Playing
			|| state.state == Media::Player::State::Starting
			|| state.state == Media::Player::State::Resuming,
	};
}

void PaintHomeCardAction(
		QPainter &p,
		const QRect &art,
		bool album,
		double shown = 1.,
		bool playing = false) {
	const auto s = [](int v) { return style::ConvertScale(v); };
	const auto button = HomeActionRect(art);
	if (button.width() < s(24)) {
		return;
	}
	shown = std::max(shown, playing ? 1. : 0.);
	if (shown <= 0.) {
		return;
	}
	p.save();
	p.setOpacity(p.opacity() * shown);
	p.translate(0., s(6) * (1. - shown));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 120));
	p.drawEllipse(button);
	const auto center = button.center();
	const auto glyph = QColor(255, 255, 255, 235);
	if (album) {
		p.setPen(QPen(glyph, s(2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPolyline(QPolygon({ center + QPoint(-s(3), -s(6)),
			center + QPoint(s(3), 0), center + QPoint(-s(3), s(6)) }));
	} else if (playing) {
		p.setBrush(glyph);
		p.drawRoundedRect(QRect(
			center.x() - s(4), center.y() - s(6), s(3), s(12)), s(1), s(1));
		p.drawRoundedRect(QRect(
			center.x() + s(1), center.y() - s(6), s(3), s(12)), s(1), s(1));
	} else {
		p.setBrush(glyph);
		p.drawPolygon(QPolygon({ center + QPoint(-s(4), -s(7)),
			center + QPoint(s(7), 0), center + QPoint(-s(4), s(7)) }));
	}
	p.restore();
}

class TrackMenuButton final : public QToolButton {
public:
	explicit TrackMenuButton(QWidget *parent) : QToolButton(parent) {
		setCursor(Qt::PointingHandCursor);
		setAutoRaise(true);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 7, 7);
		}
		p.setPen(Qt::NoPen);
		p.setBrush((underMouse() || hasFocus()) ? st::windowFg->c : st::windowSubTextFg->c);
		const auto center = QPointF(width() / 2., height() / 2.);
		for (auto i = -1; i <= 1; ++i) {
			p.drawEllipse(center + QPointF(i * 4.5, 0), 1.5, 1.5);
		}
	}
};

[[nodiscard]] QString PlaylistText(const char *text) {
	return QString::fromUtf8(Mokagram::Lang::Tr(text));
}

void PlaylistNameBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &initial,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Mokagram::Plugins::RoundedKeepButtons(box, st::defaultBox);
		box->setTitle(rpl::single(title));
		const auto input = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(PlaylistText("Название плейлиста"))));
		input->setText(initial);
		box->setFocusCallback([=] { input->setFocusFast(); });
		const auto submit = [=] {
			const auto name = input->getLastText().trimmed();
			if (name.isEmpty()) {
				input->showError();
				return;
			}
			done(name);
			box->closeBox();
		};
		input->submits() | rpl::on_next(submit, input->lifetime());
		box->addButton(rpl::single(PlaylistText("Сохранить")), submit);
		box->addButton(rpl::single(PlaylistText("Отмена")), [=] { box->closeBox(); });
	}));
}

void AddPlaylistDestinationMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		Fn<void(QString)> add) {
	auto submenu = std::make_unique<Ui::PopupMenu>(menu.get(), st::popupMenuWithIcons);
	submenu->addAction(PlaylistText("Создать плейлист…"), [=] {
		PlaylistNameBox(controller, PlaylistText("Создать плейлист"), {}, [=](QString name) {
			const auto id = CreatePlaylist(name);
			if (!id.isEmpty()) add(id);
		});
	}, &st::menuIconAdd);
	const auto playlists = Playlists();
	if (!playlists.empty()) submenu->addSeparator();
	for (const auto &playlist : playlists) {
		const auto id = playlist.id;
		submenu->addAction(playlist.name, [=] { add(id); }, nullptr);
	}
	menu->addAction(PlaylistText("Добавить в плейлист"), std::move(submenu), &st::menuIconAdd);
}

void AddToPlaylistMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const Track &track) {
	if (track.id.isEmpty() || IsTelegramTrack(track)) return;
	AddPlaylistDestinationMenu(menu, controller, [=](QString id) { AddPlaylistTrack(id, track); });
}

void AddAlbumToPlaylistMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const Album &album) {
	if (album.id.isEmpty()) return;
	AddPlaylistDestinationMenu(menu, controller, [=](QString id) {
		AlbumTracks(album.id, crl::guard(controller, [=](std::vector<Track> tracks, QString error) {
			if (!error.isEmpty()) {
				controller->showToast(error);
				return;
			}
			std::erase_if(tracks, [](const Track &track) { return track.unavailable || track.id.isEmpty() || IsTelegramTrack(track); });
			if (tracks.empty()) {
				controller->showToast(PlaylistText("В альбоме нет доступных треков"));
				return;
			}
			for (auto &track : tracks) {
				if (track.artworkUrl.isEmpty()) track.artworkUrl = album.artworkUrl;
			}
			AddPlaylistTracks(id, tracks);
			controller->showToast(PlaylistText("Альбом добавлен в плейлист"));
		}));
	});
}

void InstallMusicContextMenu(
		not_null<Ui::RpWidget*> widget,
		Fn<void(not_null<Ui::PopupMenu*>)> populate) {
	const auto menu = widget->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	widget->events() | rpl::filter([](not_null<QEvent*> event) {
		if (event->type() == QEvent::ContextMenu) return true;
		if (event->type() != QEvent::KeyPress) return false;
		const auto key = static_cast<QKeyEvent*>(event.get());
		return key->key() == Qt::Key_Menu
			|| (key->key() == Qt::Key_F10 && key->modifiers() == Qt::ShiftModifier);
	}) | rpl::on_next([=](not_null<QEvent*> event) {
		event->accept();
		*menu = base::make_unique_q<Ui::PopupMenu>(widget, st::popupMenuWithIcons);
		populate(menu->get());
		const auto position = event->type() == QEvent::ContextMenu
			&& static_cast<QContextMenuEvent*>(event.get())->reason() != QContextMenuEvent::Keyboard
			? static_cast<QContextMenuEvent*>(event.get())->globalPos()
			: widget->mapToGlobal(widget->rect().center());
		(*menu)->popup(position);
	}, widget->lifetime());
}

void InstallAlbumContextMenu(
		not_null<Ui::RpWidget*> widget,
		not_null<Window::SessionController*> controller,
		Album album) {
	InstallMusicContextMenu(widget, [=](not_null<Ui::PopupMenu*> menu) {
		AddAlbumToPlaylistMenu(menu, controller, album);
	});
}

enum class RowMode { Results, Queue, Context, Home, Album };

class Row final : public Ui::AbstractButton {
public:
	void onStateChanged(State, StateChangeSource) override {
		if (_mode == RowMode::Home) {
			_hoverFade.set(isOver() || isDown(), [=] { update(); });
		}
	}

	Row(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Track &track,
		int index,
		RowMode mode = RowMode::Results,
		std::shared_ptr<std::vector<Track>> results = nullptr,
		int resultIndex = -1,
		Fn<void(Track)> open = nullptr,
		QString playlistId = {},
		bool homePlaybackControls = true)
	: AbstractButton(parent)
	, _track(track)
	, _mode(mode)
	, _results(std::move(results))
	, _homePlaybackControls(homePlaybackControls)
	, _playlistId(std::move(playlistId)) {
		resize(width(), kRowHeight);
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		if (mode == RowMode::Results) {
			_menuButton = new TrackMenuButton(this);
			_menuButton->setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Действия с треком")));
			_menuButton->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Действия с треком")));
			connect(_menuButton, &QToolButton::clicked, this, [=] {
				auto event = QContextMenuEvent(QContextMenuEvent::Mouse, rect().center(), _menuButton->mapToGlobal(_menuButton->rect().bottomLeft()));
				showMenu(controller, resultIndex >= 0 ? resultIndex : index, &event);
			});
		}
		const auto unavailable = _mode == RowMode::Album && _track.unavailable;
		const auto unavailableText = QString::fromUtf8(Mokagram::Lang::Tr("Недоступен для воспроизведения"));
		setAccessibleName(_track.title + u" - "_q + _track.artist
			+ (unavailable ? u" · "_q + unavailableText : QString()));
		if (unavailable) {
			setDisabled(true);
			setToolTip(unavailableText);
		} else if (!IsTelegramTrack(_track)) {
			_likeButton = Ui::CreateChild<TrackLikeButton>(this, _track);
		}
		if (_mode != RowMode::Home) {
			_equalizerTimer.setInterval(33);
			connect(&_equalizerTimer, &QTimer::timeout, this, [=] {
				_equalizerPhase += _equalizerClock.restart() / 1000.;
				update();
			});
			Media::Player::instance()->updatedNotifier(
			) | rpl::on_next([=] { updateAnimation(); }, lifetime());
			PowerSaving::Changes(
			) | rpl::on_next([=] { updateAnimation(); }, lifetime());
			connect(qApp, &QGuiApplication::applicationStateChanged, this, [=] {
				updateAnimation();
			});
		}
		{
			if (const auto item = TrackMessage(track)) {
				const auto document = item->media()->document();
				_media = document->createMediaView();
				_media->thumbnailWanted(item->fullId());
				LoadLocalArtwork(track, crl::guard(this, [=](QImage image) {
					if (!image.isNull()) {
						_media = nullptr;
						_art = std::move(image);
						update();
					}
				}));
				document->session().downloaderTaskFinished(
				) | rpl::on_next([=] { update(); }, lifetime());
			} else if (!IsTelegramTrack(track)) {
				LoadImage(ArtUrl(track.artworkUrl, u"t200x200"_q), crl::guard(this, [=](QImage image) {
					_art = std::move(image);
					update();
				}));
			}
		}
		QueueState().changes.events(
		) | rpl::on_next([=] {
			updateAnimation();
			update();
		}, lifetime());
		LikesChanged(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
		const auto playIndex = (resultIndex >= 0) ? resultIndex : index;
		setClickedCallback([=] {
			if (_mode == RowMode::Home && open) {
				const auto s = [](int v) { return style::ConvertScale(v); };
				const auto inset = s(12);
				const auto side = std::max(width() - 2 * inset, 0);
				if (!HomeActionRect(QRect(inset, inset, side, side)).contains(
						mapFromGlobal(QCursor::pos()))) {
					open(_track);
					return;
				}
				if (_homePlaybackControls
					&& HomeTrackPlayback(_track.id).current) {
					Media::Player::instance()->playPause(AudioMsgId::Type::Song);
					return;
				}
			}
			if (_mode == RowMode::Queue) {
				PlayIndex(index);
			} else if (_mode == RowMode::Context) {
				PlayContext(index);
			} else {
				if (_results) {
					QueueState().results = *_results;
				}
				if (_mode == RowMode::Home) {
					QueueState().custom = false;
				}
				PlayFromResults(playIndex);
			}
		});
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
		events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::ContextMenu;
		}) | rpl::on_next([=](not_null<QEvent*> e) {
			e->accept();
			if (!_track.unavailable) {
				showMenu(controller, playIndex, static_cast<QContextMenuEvent*>(e.get()));
			}
		}, lifetime());
	}

	void setTextLinks(Fn<void(Track)> title, Fn<void(Track)> artist) {
		const auto make = [&](Fn<void(Track)> action) {
			const auto button = Ui::CreateChild<Ui::AbstractButton>(this);
			button->setPointerCursor(true);
			button->setFocusPolicy(Qt::TabFocus);
			button->setClickedCallback([=] { action(_track); });
			button->paintRequest() | rpl::on_next([=](QRect) {
				if (button->isOver() || button->hasFocus()) {
					auto p = QPainter(button);
					p.setPen(st::windowActiveTextFg);
					p.drawLine(0, button->height() - style::ConvertScale(2),
						button->width(), button->height() - style::ConvertScale(2));
				}
			}, button->lifetime());
			return button;
		};
		_titleLink = make(std::move(title));
		_artistLink = make(std::move(artist));
		_titleLink->setAccessibleName(_track.title);
		_artistLink->setAccessibleName(_track.artist);
		layoutTextLinks();
	}

	// Строка лежит на цветном фоне плеера: подсветка белым с прозрачностью, а не серым.
	void setOnGlass(bool glass) {
		_glass = glass;
		update();
	}

protected:
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override {
		AbstractButton::resizeEvent(e);
		layoutTextLinks();
		if (_menuButton) _menuButton->setGeometry(width() - 48, (height() - 32) / 2, 32, 32);
		if (_likeButton) {
			_likeButton->setGeometry(heartRect());
		}
	}

private:
	void layoutTextLinks() {
		if (!_titleLink || !_artistLink) return;
		const auto left = 20 + kArtSize - 1 + 16;
		const auto right = width() - (_menuButton ? 72 : 24);
		const auto available = std::max((IsTelegramTrack(_track)
			? right - st::normalFont->width(FormatDuration(_track.durationMs)) - 14
			: heartRect().left() - 12) - left, 0);
		_titleLink->setGeometry(left, height() / 2 - 24,
			std::min(st::semiboldFont->width(_track.title), available), 22);
		_artistLink->setGeometry(left, height() / 2 + 2,
			std::min(st::normalFont->width(_track.artist), available), 20);
		_titleLink->setVisible(!_track.title.isEmpty());
		_artistLink->setVisible(!_track.artist.trimmed().isEmpty());
	}
	Ui::AbstractButton *_titleLink = nullptr;
	Ui::AbstractButton *_artistLink = nullptr;
	void updateAnimation();
	QToolButton *_menuButton = nullptr;

	[[nodiscard]] QRect heartRect() const {
		if (_mode == RowMode::Home) {
			return QRect(width() - 36, height() - 36, 28, 28);
		}
		const auto durationWidth = st::normalFont->width(FormatDuration(_track.durationMs));
		return QRect(
			width() - (_menuButton ? 72 : 24) - durationWidth - 14 - 28,
			(height() - 28) / 2,
			28,
			28);
	}

	void showMenu(
			not_null<Window::SessionController*> controller,
			int index,
			QContextMenuEvent *event) {
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		const auto track = _track;
		AddRadioAction(_menu.get(), controller, track);
		const auto add = [&](const QString &text, Fn<void()> callback, const style::icon *icon) {
			_menu->addAction(text, std::move(callback), icon);
		};
		add(QString::fromUtf8(Mokagram::Lang::Tr("Воспроизвести")), [=] {
			if (_mode == RowMode::Queue) {
				PlayIndex(index);
			} else if (_mode == RowMode::Context) {
				PlayContext(index);
			} else {
				if (_results) {
					QueueState().results = *_results;
				}
				PlayFromResults(index);
			}
		}, &st::menuIconSoundOn);
		add(IsLiked(track.id)
			? QString::fromUtf8(Mokagram::Lang::Tr("Убрать из избранного"))
			: QString::fromUtf8(Mokagram::Lang::Tr("В избранное")), [=] {
			SetLiked(track, !IsLiked(track.id));
		}, &st::menuIconFave);
		AddToPlaylistMenu(_menu.get(), controller, track);
		if (!_playlistId.isEmpty()) {
			add(PlaylistText("Убрать из плейлиста"), [=] {
				RemovePlaylistTrack(_playlistId, track.id);
			}, &st::menuIconDelete);
		}
		if (_mode == RowMode::Results || _mode == RowMode::Home || _mode == RowMode::Album) {
			add(QString::fromUtf8(Mokagram::Lang::Tr("Играть следующим")), [=] { EnqueueNext(track); controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Добавлено в очередь"))); }, &st::menuIconAbove);
			add(QString::fromUtf8(Mokagram::Lang::Tr("В конец очереди")), [=] { EnqueueLast(track); controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Добавлено в очередь"))); }, &st::menuIconBelow);
		} else {
			add(QString::fromUtf8(Mokagram::Lang::Tr("Выше")), [=] { QueueMove(index, -1); }, &st::menuIconAbove);
			add(QString::fromUtf8(Mokagram::Lang::Tr("Ниже")), [=] { QueueMove(index, 1); }, &st::menuIconBelow);
			add(QString::fromUtf8(Mokagram::Lang::Tr("Убрать из очереди")), [=] { QueueRemove(index); }, &st::menuIconDelete);
		}
		if (CanForwardTrack(track)) {
			add(QString::fromUtf8(Mokagram::Lang::Tr("Переслать")), [=] {
				ForwardTrack(controller, track);
			}, &st::menuIconForward);
		}
		if (IsTelegramTrack(track)) {
			add(QString::fromUtf8(Mokagram::Lang::Tr("Показать в чате")), [=] {
				if (const auto item = TrackMessage(track)) {
					controller->showPeerHistory(item->history(), {}, item->id);
				}
			}, &st::menuIconChats);
		} else {
			add(QString::fromUtf8(Mokagram::Lang::Tr("Скачать")), [=] { SaveToDisk(track); }, &st::menuIconDownload);
			add(QString::fromUtf8(Mokagram::Lang::Tr("Добавить в профиль")), [=] { AddToProfile(controller, track); }, &st::menuIconProfile);
			add(QString::fromUtf8(Mokagram::Lang::Tr("Копировать ссылку")), [=] {
				QGuiApplication::clipboard()->setText(track.permalink);
				controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Ссылка скопирована")));
			}, &st::menuIconLink);
		}
		_menu->popup(event->reason() == QContextMenuEvent::Keyboard
			? mapToGlobal(rect().center())
			: event->globalPos());
	}

	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto selected = (QueueState().currentId == _track.id);
		if (_mode == RowMode::Home) {
			paintHome(
				p,
				selected,
				_homePlaybackControls
					? HomeTrackPlayback(_track.id)
					: HomeTrackPlaybackState());
			return;
		}
		const auto playing = selected;
		const auto over = isOver() || isDown();
		if (playing || over) {
			p.setPen(Qt::NoPen);
			p.setBrush(_glass
				? QColor(255, 255, 255, playing ? 34 : 20)
				: st::windowBgOver->c);
			p.drawRoundedRect(rect().marginsRemoved({ 8, 4, 8, 4 }), 12, 12);
		}
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(8, 4, -8, -4), 8, 8);
		}
		const auto art = QRect(20, (height() - kArtSize) / 2, kArtSize, kArtSize);
		if (_media) {
			if (const auto image = _media->thumbnail()) {
				_art = image->original();
			} else if (const auto image = _media->thumbnailInline()) {
				_art = image->original();
			}
		}
		auto path = QPainterPath();
		path.addRoundedRect(art, 8, 8);
		if (!_art.isNull()) {
			p.save();
			p.setClipPath(path);
			p.drawImage(art, _art);
			if (playing) {
				p.fillRect(art, QColor(0, 0, 0, 120));
			}
			p.restore();
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawPath(path);
		}
		if (playing) {
			// Три столбика «играет».
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::white);
			const auto base = art.center().y() + 9;
			const auto left = art.center().x() - 10;
			for (auto i = 0; i != 3; ++i) {
				const auto wave = std::sin(_equalizerPhase * (5. + i) + i * 2.1);
				const auto barHeight = 6. + 7. * (wave + 1.);
				p.drawRoundedRect(
					QRectF(left + i * 8, base - barHeight, 4, barHeight),
					2,
					2);
			}
		}
		const auto left = art.right() + 16;
		const auto right = width() - (_menuButton ? 72 : 24);
		const auto duration = FormatDuration(_track.durationMs);
		const auto durationWidth = st::normalFont->width(duration);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(right - durationWidth, 0, durationWidth, height()),
			Qt::AlignVCenter | Qt::AlignRight,
			duration);
		const auto heart = heartRect();
		const auto textWidth = (IsTelegramTrack(_track)
			? right - durationWidth - 14
			: heart.left() - 12) - left;
		p.setFont(st::semiboldFont);
		p.setPen(playing ? st::windowActiveTextFg : st::windowFg);
		p.drawText(
			QRect(left, height() / 2 - 24, textWidth, 22),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::semiboldFont->elided(_track.title, textWidth));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto item = TrackMessage(_track);
		const auto subtitle = _track.unavailable
			? _track.artist + u" · "_q + QString::fromUtf8(Mokagram::Lang::Tr("Недоступен для воспроизведения"))
			: item
			? _track.artist + u" · "_q + item->history()->peer->name()
			: _track.artist;
		p.drawText(
			QRect(left, height() / 2 + 2, textWidth, 20),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(subtitle, textWidth));
	}

	void paintHome(
			QPainter &p,
			bool selected,
			HomeTrackPlaybackState playback) {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto inset = s(12);
		const auto artSize = std::max(width() - 2 * inset, 0);
		const auto art = QRect(inset, inset, artSize, artSize);
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(
			QColor(0x24, 0x24, 0x24),
			QColor(0x30, 0x30, 0x30),
			(selected || playback.current) ? 1. : _hoverFade.value()));
		p.drawRoundedRect(rect(), s(8), s(8));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(1), s(1), -s(1), -s(1)), s(8), s(8));
			p.setPen(Qt::NoPen);
		}
		if (_media) {
			if (const auto image = _media->thumbnail()) {
				_art = image->original();
			} else if (const auto image = _media->thumbnailInline()) {
				_art = image->original();
			}
		}
		auto path = QPainterPath();
		path.addRoundedRect(art, s(6), s(6));
		p.save();
		p.setClipPath(path);
		if (_art.isNull()) {
			p.fillRect(art, st::windowBgOver->c);
		} else {
			p.drawImage(art, _art);
		}
		p.restore();
		const auto actionShown = (hasFocus() || playback.current)
			? 1.
			: _hoverFade.value();
		PaintHomeCardAction(
			p,
			art,
			false,
			actionShown,
			_homePlaybackControls && playback.playing);
		const auto textWidth = std::max(width() - 2 * inset - s(28), 0);
		p.setFont(st::semiboldFont);
		p.setPen((selected || playback.current)
			? st::windowActiveTextFg
			: st::windowFg);
		p.drawText(QRect(inset, art.bottom() + s(8), width() - 2 * inset, s(22)),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::semiboldFont->elided(_track.title, width() - 2 * inset));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(inset, art.bottom() + s(30), textWidth, s(22)),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(_track.artist, textWidth));

	}

	QTimer _equalizerTimer;
	QElapsedTimer _equalizerClock;
	double _equalizerPhase = 0.;
	Track _track;
	RowMode _mode;
	std::shared_ptr<std::vector<Track>> _results;
	bool _homePlaybackControls = true;
	TrackLikeButton *_likeButton = nullptr;
	bool _glass = false;
	QImage _art;
	std::shared_ptr<Data::DocumentMedia> _media;
	base::unique_qptr<Ui::PopupMenu> _menu;
	QString _playlistId;
	Ui::Animations::Simple _likeAnimation;

	HoverFade _hoverFade;

};

class HomeSectionHeader final : public Ui::AbstractButton {
public:
	HomeSectionHeader(QWidget *parent, QString title, bool back, Fn<void()> action, bool create = false)
	: AbstractButton(parent)
	, _title(std::move(title))
	, _back(back)
	, _create(create) {
		resize(width(), style::ConvertScale(56));
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(_create ? _title : _back
			? QString::fromUtf8(Mokagram::Lang::Tr("Назад")) + u": "_q + _title
			: QString::fromUtf8(Mokagram::Lang::Tr("Показать все")) + u": "_q + _title);
		setClickedCallback(std::move(action));
		paintRequest() | rpl::on_next([=](QRect) {
			auto p = QPainter(this);
			const auto s = [](int v) { return style::ConvertScale(v); };
			auto font = st::semiboldFont->f;
			font.setPixelSize(s(20));
			p.setFont(font);
			if (hasFocus()) {
				p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
				p.setBrush(Qt::NoBrush);
				p.drawRoundedRect(rect().adjusted(s(2), s(2), -s(2), -s(2)), s(6), s(6));
			}
			p.setPen(st::windowFg);
			const auto title = _create ? u"+  "_q + _title : _back ? u"‹  "_q + _title : _title;
			p.save();
			if (_back) {
				const auto scale = 1. - 0.03 * _press.value(_pressed ? 1. : 0.);
				p.translate(s(12), height() / 2.);
				p.scale(scale, scale);
				p.translate(-s(12), -height() / 2.);
			}
			p.drawText(QRect(s(12), 0, std::max(width() - s(140), 0), height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				QFontMetrics(font).elidedText(title, Qt::ElideRight, std::max(width() - s(140), 0)));
			p.restore();
			if (!_back) {
				p.setFont(st::normalFont);
				p.setPen(isOver() ? st::windowFg : st::windowSubTextFg);
				p.drawText(QRect(width() - s(128), 0, s(116), height()),
					Qt::AlignVCenter | Qt::AlignRight,
					QString::fromUtf8(Mokagram::Lang::Tr("Показать все")));
			}
		}, lifetime());
	}

protected:
	void mousePressEvent(QMouseEvent *event) override {
		_pointerPress = true;
		AbstractButton::mousePressEvent(event);
	}
	void mouseReleaseEvent(QMouseEvent *event) override {
		_pointerPress = false;
		AbstractButton::mouseReleaseEvent(event);
	}
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		if (!_back || !((was ^ state()) & StateFlag::Down)) {
			return;
		}
		const auto pressed = isDown() && _pointerPress;
		if (pressed == _pressed) {
			return;
		}
		const auto from = _press.value(_pressed ? 1. : 0.);
		_pressed = pressed;
		if (anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations)) {
			_press.stop();
			update();
		} else {
			_press.start([=] { update(); }, from, pressed ? 1. : 0., 140, anim::easeOutQuint);
		}
	}

private:
	QString _title;
	bool _back = false;
	bool _create = false;
	bool _pointerPress = false;
	bool _pressed = false;
	Ui::Animations::Simple _press;

};

class PlaylistRow final : public Ui::AbstractButton {
public:
	void onStateChanged(State was, StateChangeSource source) override {
		const auto active = isOver() || isDown();
		if (active == _hoverTarget) return;
		const auto from = _hover.value(_hoverTarget ? 1. : 0.);
		_hoverTarget = active;
		if (anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations)) {
			_hover.stop();
			update();
		} else {
			_hover.start([=] { update(); }, from, active ? 1. : 0., 160, anim::easeOutCubic);
		}
	}
	PlaylistRow(QWidget *parent, not_null<Window::SessionController*> controller, Playlist playlist, Fn<void(QString)> open)
	: AbstractButton(parent), _controller(controller), _playlist(std::move(playlist)) {
		const auto s = [](int v) { return style::ConvertScale(v); };
		resize(width(), s(92));
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(_playlist.name);
		setClickedCallback([=] { open(_playlist.id); });
		_menuButton = new TrackMenuButton(this);
		_menuButton->setAccessibleName(PlaylistText("Действия с плейлистом"));
		_menuButton->setToolTip(PlaylistText("Действия с плейлистом"));
		_menuButton->setVisible(!_playlist.remote);
		connect(_menuButton, &QToolButton::clicked, this, [=] {
			showMenu(_menuButton->mapToGlobal(_menuButton->rect().bottomLeft()));
		});
		for (const auto &track : _playlist.tracks) {
			if (track.artworkUrl.isEmpty()) continue;
			const auto url = ArtUrl(track.artworkUrl, u"t200x200"_q);
			if (std::find(_urls.begin(), _urls.end(), url) != _urls.end()) continue;
			const auto index = int(_urls.size());
			_urls.push_back(url);
			LoadImage(url, crl::guard(this, [=](QImage image) {
				_art[index] = std::move(image);
				if (!_art[index].isNull() && isVisible() && !anim::Disabled()
					&& !PowerSaving::On(PowerSaving::kAnimations)) {
					_artFade[index].start([=] { update(); }, 0., 1., 220, anim::easeOutCubic);
				}
				update();
			}));
			if (_urls.size() == _art.size()) break;
		}
		paintRequest() | rpl::on_next([=](QRect) { paint(); }, lifetime());
	}
protected:
	void resizeEvent(QResizeEvent *event) override {
		AbstractButton::resizeEvent(event);
		const auto s = [](int v) { return style::ConvertScale(v); };
		_menuButton->setGeometry(width() - s(54), (height() - s(32)) / 2, s(32), s(32));
	}
	void contextMenuEvent(QContextMenuEvent *event) override {
		if (_playlist.remote) {
			event->accept();
			return;
		}
		showMenu(event->reason() == QContextMenuEvent::Keyboard
			? mapToGlobal(rect().center()) : event->globalPos());
		event->accept();
	}
	void keyPressEvent(QKeyEvent *event) override {
		if (_playlist.remote) {
			AbstractButton::keyPressEvent(event);
			return;
		}
		if (event->key() == Qt::Key_Menu
			|| (event->key() == Qt::Key_F10 && event->modifiers() == Qt::ShiftModifier)) {
			showMenu(mapToGlobal(rect().center()));
			event->accept();
		} else {
			AbstractButton::keyPressEvent(event);
		}
	}
private:
	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto card = rect().adjusted(s(8), s(4), -s(8), -s(4));
		const auto hover = (anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations))
			? (isOver() || isDown() ? 1. : 0.)
			: _hover.value(isOver() || isDown() ? 1. : 0.);
		if (hover > 0.) {
			p.save();
			p.setOpacity(hover);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(card, s(12), s(12));
			p.restore();
		}
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(card.adjusted(s(1), s(1), -s(1), -s(1)), s(12), s(12));
		}
		const auto side = s(68);
		const auto cover = QRect(s(16), (height() - side) / 2, side, side);
		auto path = QPainterPath();
		path.addRoundedRect(cover, s(9), s(9));
		p.save();
		p.setClipPath(path);
		p.fillRect(cover, QColor(49, 57, 70));
		if (_urls.empty()) {
			p.setPen(QColor(181, 199, 221));
			auto font = st::semiboldFont->f;
			font.setPixelSize(s(30));
			p.setFont(font);
			p.drawText(cover, Qt::AlignCenter, QString::fromUtf8("♫"));
		} else {
			const auto count = (_urls.size() == 1) ? 1 : 4;
			for (auto i = 0; i != count; ++i) {
				const auto &image = _art[i % _urls.size()];
				const auto x = count == 1 ? 0 : i % 2;
				const auto y = count == 1 ? 0 : i / 2;
				const auto cell = count == 1 ? cover : QRect(
					cover.x() + x * (side / 2), cover.y() + y * (side / 2),
					x ? side - side / 2 : side / 2, y ? side - side / 2 : side / 2);
				p.fillRect(cell, (i % 2) ? QColor(57, 65, 79) : QColor(68, 78, 94));
				if (!image.isNull()) {
					p.save();
					p.setOpacity((anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations))
						? 1. : _artFade[i % _urls.size()].value(1.));
					const auto dimension = std::min(image.width(), image.height());
					const auto source = QRect((image.width() - dimension) / 2,
						(image.height() - dimension) / 2, dimension, dimension);
					p.drawImage(cell, image, source);
					p.restore();
				}
			}
		}
		p.restore();
		const auto left = cover.right() + s(18);
		const auto textWidth = std::max(width() - left - s(70), 0);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(left, height() / 2 - s(21), textWidth, s(23)), Qt::AlignVCenter,
			p.fontMetrics().elidedText(_playlist.name, Qt::ElideRight, textWidth));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(left, height() / 2 + s(3), textWidth, s(20)), Qt::AlignVCenter,
			PlaylistText("Треков: %1").arg(int(_playlist.tracks.size())));
	}
	void showMenu(QPoint position) {
		_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
		const auto id = _playlist.id;
		const auto controller = _controller;
		const auto name = _playlist.name;
		_menu->addAction(PlaylistText("Переименовать"), [=] {
			PlaylistNameBox(controller, PlaylistText("Переименовать плейлист"), name, [=](QString value) {
				RenamePlaylist(id, value);
			});
		}, &st::menuIconEdit);
		_menu->addAction(PlaylistText("Удалить плейлист"), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = TextWithEntities{ PlaylistText("Удалить плейлист «%1»?").arg(name) },
				.confirmed = [=](Fn<void()> close) { close(); DeletePlaylist(id); },
				.confirmText = PlaylistText("Удалить"),
				.cancelText = PlaylistText("Отмена"),
				.title = rpl::single(PlaylistText("Удалить плейлист")),
			}));
		}, &st::menuIconDelete);
		_menu->popup(position);
	}
	const not_null<Window::SessionController*> _controller;
	Playlist _playlist;
	std::vector<QString> _urls;
	std::array<QImage, 4> _art;
	std::array<Ui::Animations::Simple, 4> _artFade;
	Ui::Animations::Simple _hover;
	bool _hoverTarget = false;
	QToolButton *_menuButton = nullptr;
	base::unique_qptr<Ui::PopupMenu> _menu;
};

void Row::updateAnimation() {
	if (_mode == RowMode::Home) {
		return;
	}
	const auto eligible = isVisible()
		&& QGuiApplication::applicationState() == Qt::ApplicationActive
		&& QueueState().currentId == _track.id
		&& !PowerSaving::On(PowerSaving::kAnimations);
	if (!eligible) {
		_equalizerTimer.stop();
		return;
	}
	const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
	const auto animate = UsesQueue(state.id)
		&& (state.state == Media::Player::State::Playing
			|| state.state == Media::Player::State::Starting
			|| state.state == Media::Player::State::Resuming);
	if (animate && !_equalizerTimer.isActive()) {
		_equalizerClock.start();
		_equalizerTimer.start();
	} else if (!animate) {
		_equalizerTimer.stop();
	}
}

void Row::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Menu
		|| (e->key() == Qt::Key_F10 && e->modifiers() == Qt::ShiftModifier)) {
		auto event = QContextMenuEvent(
			QContextMenuEvent::Keyboard,
			rect().center(),
			mapToGlobal(rect().center()));
		QCoreApplication::sendEvent(this, &event);
		e->accept();
	} else {
		AbstractButton::keyPressEvent(e);
	}
}

void Row::showEvent(QShowEvent *e) {
	AbstractButton::showEvent(e);
	updateAnimation();
}

void Row::hideEvent(QHideEvent *e) {
	_equalizerTimer.stop();
	AbstractButton::hideEvent(e);
}

// Альбом в результатах поиска: обложка, название, исполнитель и число треков.
class AlbumRow final : public Ui::AbstractButton {
public:
	AlbumRow(QWidget *parent, not_null<Window::SessionController*> controller, const Album &album, Fn<void()> open)
	: AbstractButton(parent)
	, _album(album) {
		resize(width(), kRowHeight);
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Альбом"))
			+ u": "_q + _album.title + u" - "_q + _album.artist);
		setClickedCallback(std::move(open));
		InstallAlbumContextMenu(this, controller, album);
		LoadImage(ArtUrl(album.artworkUrl, u"t200x200"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			update();
		}));
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
	}

private:
	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		if (isOver() || isDown()) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect().marginsRemoved({ 8, 4, 8, 4 }), 12, 12);
		}
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(8, 4, -8, -4), 8, 8);
		}
		const auto art = QRect(20, (height() - kArtSize) / 2, kArtSize, kArtSize);
		auto path = QPainterPath();
		path.addRoundedRect(art, 10, 10);
		if (_art.isNull()) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawPath(path);
		} else {
			p.save();
			p.setClipPath(path);
			p.drawImage(art, _art);
			p.restore();
		}
		const auto left = art.right() + 16;
		const auto right = width() - 24;
		// Стрелка «открыть».
		p.setPen(QPen(st::windowSubTextFg, 2., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		const auto cy = height() / 2.;
		p.drawPolyline(QPolygonF({
			QPointF(right - 5, cy - 6),
			QPointF(right + 1, cy),
			QPointF(right - 5, cy + 6) }));
		const auto textWidth = right - 20 - left;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(left, height() / 2 - 24, textWidth, 22),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::semiboldFont->elided(_album.title, textWidth));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto subtitle = _album.artist
			+ u" · "_q
			+ QString::fromUtf8(Mokagram::Lang::Tr("треков: %1")).arg(_album.trackCount);
		p.drawText(
			QRect(left, height() / 2 + 2, textWidth, 20),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(subtitle, textWidth));
	}

	Album _album;
	QImage _art;

};

class HomeAlbumCard final : public Ui::AbstractButton {
public:
	void onStateChanged(State was, StateChangeSource source) override {
		_hoverFade.set(isOver() || isDown(), [=] { update(); });
	}

	HomeAlbumCard(QWidget *parent, not_null<Window::SessionController*> controller, Album album, Fn<void()> open)
	: AbstractButton(parent)
	, _album(std::move(album)) {
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(releaseKindText() + u" · "_q + _album.title
			+ u" · "_q + _album.artist);
		setClickedCallback(std::move(open));
		InstallAlbumContextMenu(this, controller, _album);
		LoadImage(ArtUrl(_album.artworkUrl, u"t200x200"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			update();
		}));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto inset = s(12);
		const auto side = std::max(width() - 2 * inset, 0);
		const auto art = QRect(inset, inset, side, side);
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(QColor(0x24, 0x24, 0x24), QColor(0x30, 0x30, 0x30), _hoverFade.value()));
		p.drawRoundedRect(rect(), s(8), s(8));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(1), s(1), -s(1), -s(1)), s(8), s(8));
		}
		auto path = QPainterPath();
		path.addRoundedRect(art, s(6), s(6));
		p.save();
		p.setClipPath(path);
		if (_art.isNull()) {
			p.fillRect(art, st::windowBgOver->c);
		} else {
			p.drawImage(art, _art);
		}
		p.restore();
		PaintHomeCardAction(p, art, true, hasFocus() ? 1. : _hoverFade.value());
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(inset, art.bottom() + s(8), side, s(22)),
			Qt::AlignVCenter | Qt::AlignLeft, st::semiboldFont->elided(_album.title, side));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto details = _album.releaseYear > 0
			? QString::number(_album.releaseYear) + u" · "_q + releaseKindText()
			: releaseKindText() + u" · "_q + _album.artist;
		p.drawText(QRect(inset, art.bottom() + s(30), side, s(22)),
			Qt::AlignVCenter | Qt::AlignLeft, st::normalFont->elided(details, side));
	}

private:
	[[nodiscard]] QString releaseKindText() const {
		switch (_album.releaseKind) {
		case ReleaseKind::Single: return PlaylistText("Сингл");
		case ReleaseKind::Ep: return PlaylistText("EP");
		case ReleaseKind::Compilation: return PlaylistText("Сборник");
		case ReleaseKind::Unknown:
		case ReleaseKind::Album: return PlaylistText("Альбом");
		}
		Unexpected("Release kind.");
	}

	Album _album;
	QImage _art;

	HoverFade _hoverFade;

};

class HomeRecentTile final : public Ui::AbstractButton {
public:
	void onStateChanged(State was, StateChangeSource source) override {
		_hoverFade.set(isOver() || isDown(), [=] { update(); });
	}

	HomeRecentTile(QWidget *parent, not_null<Window::SessionController*> controller, RecentItem item, Fn<void()> activate, Fn<void()> play)
	: AbstractButton(parent)
	, _item(std::move(item)) {
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		const auto title = _item.isAlbum ? _item.album.title : _item.track.title;
		const auto artist = _item.isAlbum ? _item.album.artist : _item.track.artist;
		setAccessibleName((_item.isAlbum
			? QString::fromUtf8(Mokagram::Lang::Tr("Альбом")) + u": "_q
			: QString()) + title + u" - "_q + artist);
		setClickedCallback([=] {
			const auto s = [](int v) { return style::ConvertScale(v); };
			const auto button = QRect(width() - s(56), (height() - s(44)) / 2, s(44), s(44));
			if (button.contains(mapFromGlobal(QCursor::pos()))) {
				if (!_item.isAlbum
					&& HomeTrackPlayback(_item.track.id).current) {
					Media::Player::instance()->playPause(AudioMsgId::Type::Song);
				} else {
					play();
				}
			} else {
				activate();
			}
		});
		const auto refreshPlayback = [=] {
			const auto playback = _item.isAlbum
				? HomeTrackPlaybackState()
				: HomeTrackPlayback(_item.track.id);
			if (_current != playback.current || _playing != playback.playing) {
				_current = playback.current;
				_playing = playback.playing;
				update();
			}
		};
		Media::Player::instance()->updatedNotifier(
		) | rpl::on_next(refreshPlayback, lifetime());
		QueueState().changes.events(
		) | rpl::on_next(refreshPlayback, lifetime());
		refreshPlayback();
		const auto recentItem = _item;
		InstallMusicContextMenu(this, [=](not_null<Ui::PopupMenu*> menu) {
			menu->addAction(PlaylistText("Воспроизвести"), play, &st::menuIconSoundOn);
			if (recentItem.isAlbum) {
				menu->addAction(PlaylistText("Открыть альбом"), activate, &st::menuIconSearch);
				AddAlbumToPlaylistMenu(menu, controller, recentItem.album);
				return;
			}
			const auto track = recentItem.track;
			AddRadioAction(menu, controller, track);
			menu->addAction(IsLiked(track.id) ? PlaylistText("Убрать из избранного") : PlaylistText("В избранное"), [=] {
				SetLiked(track, !IsLiked(track.id));
			}, &st::menuIconFave);
			AddToPlaylistMenu(menu, controller, track);
			menu->addAction(PlaylistText("Играть следующим"), [=] {
				EnqueueNext(track); controller->showToast(PlaylistText("Добавлено в очередь"));
			}, &st::menuIconAbove);
			menu->addAction(PlaylistText("В конец очереди"), [=] {
				EnqueueLast(track); controller->showToast(PlaylistText("Добавлено в очередь"));
			}, &st::menuIconBelow);
			if (CanForwardTrack(track)) menu->addAction(PlaylistText("Переслать"), [=] { ForwardTrack(controller, track); }, &st::menuIconForward);
			menu->addAction(PlaylistText("Скачать"), [=] { SaveToDisk(track); }, &st::menuIconDownload);
			menu->addAction(PlaylistText("Добавить в профиль"), [=] { AddToProfile(controller, track); }, &st::menuIconProfile);
			menu->addAction(PlaylistText("Копировать ссылку"), [=] {
				QGuiApplication::clipboard()->setText(track.permalink);
				controller->showToast(PlaylistText("Ссылка скопирована"));
			}, &st::menuIconLink);
		});
		const auto url = _item.isAlbum ? _item.album.artworkUrl : _item.track.artworkUrl;
		LoadImage(ArtUrl(url, u"t200x200"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			update();
		}));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(QColor(0x29, 0x29, 0x29), QColor(0x35, 0x35, 0x35), _hoverFade.value()));
		p.drawRoundedRect(rect(), s(6), s(6));
		const auto art = QRect(0, 0, std::min(height(), width()), height());
		p.save();
		auto path = QPainterPath();
		path.addRoundedRect(rect(), s(6), s(6));
		p.setClipPath(path);
		if (_art.isNull()) {
			p.fillRect(art, st::windowBgOver->c);
		} else {
			p.drawImage(art, _art);
		}
		p.restore();
		const auto left = art.right() + s(13);
		const auto available = std::max(width() - left - s(64), 0);
		{
			const auto art = QRect(width() - s(64), (height() - s(60)) / 2, s(60), s(60));
			const auto actionShown = (hasFocus() || _current)
				? 1.
				: _hoverFade.value();
			PaintHomeCardAction(
				p,
				art,
				false,
				actionShown,
				_playing);
		}
		const auto title = _item.isAlbum ? _item.album.title : _item.track.title;
		p.setFont(st::semiboldFont);
		p.setPen(_current ? st::windowActiveTextFg : st::windowFg);
		p.drawText(QRect(left, 0, available, height()), Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(title, available));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(1), s(1), -s(1), -s(1)), s(6), s(6));
		}
	}

private:
	RecentItem _item;
	QImage _art;
	bool _current = false;
	bool _playing = false;

	HoverFade _hoverFade;

};

class HomeArtistCard final : public Ui::AbstractButton {
public:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		_hoverFade.set(isOver() || isDown(), [=] { update(); });
	}

	HomeArtistCard(QWidget *parent, Track track, Fn<void(Track)> open)
	: AbstractButton(parent), _track(std::move(track)) {
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Исполнитель")) + u": "_q + _track.artist);
		setClickedCallback([=] { open(_track); });
		const auto url = _track.artistArtworkUrl.isEmpty() ? _track.artworkUrl : _track.artistArtworkUrl;
		LoadImage(ArtUrl(url, u"t200x200"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			_scaledArt = QImage();
			update();
		}));
	}
protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(QColor(0x24, 0x24, 0x24), QColor(0x30, 0x30, 0x30), _hoverFade.value()));
		p.drawRoundedRect(rect(), s(8), s(8));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(1), s(1), -s(1), -s(1)), s(8), s(8));
		}
		const auto inset = s(12);
		const auto art = QRect(inset, inset, width() - 2 * inset, width() - 2 * inset);
		p.save();
		auto clip = QPainterPath();
		clip.addEllipse(art);
		p.setClipPath(clip);
		if (_art.isNull()) {
			p.fillRect(art, st::windowBgOver->c);
		} else {
			if (_scaledArt.isNull() || _scaledArtSize != art.size()) {
				_scaledArt = _art.scaled(art.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				_scaledArtSize = art.size();
			}
			p.drawImage(art, _scaledArt, QRect((_scaledArt.width() - art.width()) / 2, (_scaledArt.height() - art.height()) / 2, art.width(), art.height()));
		}
		p.restore();
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(inset, art.bottom() + s(8), width() - 2 * inset, s(22)), Qt::AlignVCenter, st::semiboldFont->elided(_track.artist, width() - 2 * inset));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(inset, art.bottom() + s(30), width() - 2 * inset, s(22)), Qt::AlignVCenter, QString::fromUtf8(Mokagram::Lang::Tr("Исполнитель")));
	}
private:
	Track _track;
	QImage _art;
	QImage _scaledArt;
	QSize _scaledArtSize;
	HoverFade _hoverFade;
};

[[nodiscard]] std::vector<Track> HomeRecommendationArtists(const std::vector<Track> &tracks) {
	auto result = std::vector<Track>();
	auto names = std::set<QString>();
	for (const auto &track : tracks) {
		const auto name = track.artist.trimmed();
		if (!name.isEmpty() && !track.artistId.isEmpty() && track.artistId != u"0"_q
			&& names.insert(name.toCaseFolded()).second) {
			result.push_back(track);
		}
	}
	return result;
}

[[nodiscard]] std::vector<Track> HomeSimilarArtists(
		const Track &artist,
		const std::vector<Track> &tracks) {
	auto result = HomeRecommendationArtists(tracks);
	const auto artistName = artist.artist.trimmed();
	std::erase_if(result, [&](const Track &candidate) {
		return (!artist.artistId.isEmpty() && candidate.artistId == artist.artistId)
			|| (!artistName.isEmpty()
				&& candidate.artist.trimmed().compare(artistName, Qt::CaseInsensitive) == 0);
	});
	return result;
}

class HomeRecentGrid final : public Ui::RpWidget {
public:
	HomeRecentGrid(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const std::vector<RecentItem> &recent,
		Fn<void(Album)> open,
		Fn<void(Track)> openTrack)
	: RpWidget(parent) {
		const auto tracks = std::make_shared<std::vector<Track>>();
		for (const auto &item : recent) {
			if (!item.isAlbum) tracks->push_back(item.track);
		}
		auto trackIndex = 0;
		const auto count = std::min(int(recent.size()), 8);
		for (auto i = 0; i != count; ++i) {
			const auto item = recent[i];
			if (item.isAlbum) {
				_items.push_back(Ui::CreateChild<HomeRecentTile>(this, controller, item,
					[=] { open(item.album); }, [=] {
					AlbumTracks(item.album.id, crl::guard(controller, [=](std::vector<Track> tracks, QString error) {
						if (!error.isEmpty()) {
							controller->showToast(error);
							return;
						}
						tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
							[](const Track &track) { return track.unavailable; }), tracks.end());
						PlayAll(tracks);
					}));
				}));
			} else {
				const auto index = trackIndex++;
				_items.push_back(Ui::CreateChild<HomeRecentTile>(this, controller, item,
					[=] { openTrack(item.track); }, [=] {
					QueueState().results = *tracks;
					QueueState().custom = false;
					PlayFromResults(index);
				}));
			}
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto gap = s(12);
		const auto available = std::max(newWidth - 2 * gap, 1);
		const auto columns = std::clamp((available + gap) / (s(240) + gap), 1, 4);
		const auto itemWidth = (available - (columns - 1) * gap) / columns;
		for (auto i = 0; i != int(_items.size()); ++i) {
			_items[i]->setGeometry(gap + (i % columns) * (itemWidth + gap),
				(i / columns) * (s(64) + gap), itemWidth, s(64));
		}
		return ((_items.size() + columns - 1) / columns) * (s(64) + gap) + s(16);
	}

private:
	std::vector<HomeRecentTile*> _items;

};

// Затухание по краю карусели: карточки уходят в фон, а не обрываются.
class HomeEdgeFade final : public Ui::RpWidget {
public:
	// Край, у которого фон гуще: слева, справа или сверху.
	enum class Edge { Left, Right, Top, Bottom };

	HomeEdgeFade(QWidget *parent, Edge edge)
	: RpWidget(parent)
	, _edge(edge) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	// Цвет фона под краем, если он не просто #1c1c1c (например, тонировка альбома).
	void setColor(Fn<QColor()> color) {
		_color = std::move(color);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto from = _color ? _color() : QColor(0x1c, 0x1c, 0x1c);
		auto to = from;
		to.setAlpha(0);
		const auto vertical = (_edge == Edge::Top || _edge == Edge::Bottom);
		const auto reversed = (_edge == Edge::Right || _edge == Edge::Bottom);
		auto gradient = QLinearGradient(
			0, 0, vertical ? 0 : width(), vertical ? height() : 0);
		gradient.setColorAt(0., reversed ? to : from);
		gradient.setColorAt(1., reversed ? from : to);
		p.fillRect(rect(), gradient);
	}

private:
	const Edge _edge = Edge::Left;
	Fn<QColor()> _color;

};

class HomeCarouselArrow final : public Ui::AbstractButton {
public:
	HomeCarouselArrow(QWidget *parent, bool forward, Fn<void()> action)
	: AbstractButton(parent)
	, _forward(forward) {
		setPointerCursor(true);
		setFocusPolicy(Qt::NoFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr(
			forward
				? Mokagram::Lang::Tr("Вперёд")
				: Mokagram::Lang::Tr("Назад"))));
		setClickedCallback(std::move(action));
	}

	// Появляется и пропадает плавно, а не щёлкает.
	void toggle(bool shown) {
		if (_shown == shown) {
			return;
		}
		_shown = shown;
		if (shown) {
			show();
		}
		_fade.start([=] {
			update();
			if (!_shown && !_fade.animating()) {
				hide();
			}
		}, shown ? 0. : 1., shown ? 1. : 0., 180, anim::easeOutCubic);
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		const auto now = isOver();
		if (now != bool(was & StateFlag::Over)) {
			_hover.start([=] { update(); },
				now ? 0. : 1., now ? 1. : 0., 140, anim::easeOutCubic);
		}
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto visible = _fade.value(_shown ? 1. : 0.);
		const auto over = _hover.value(isOver() ? 1. : 0.);
		p.setOpacity(visible);
		const auto scale = 0.85 + 0.15 * visible;
		p.translate(rect().center());
		p.scale(scale, scale);
		p.translate(-rect().center());
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(QColor(0, 0, 0, 150), QColor(0, 0, 0, 200), over));
		p.drawEllipse(rect());
		p.setPen(QPen(QColor(255, 255, 255, 220), s(2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		const auto center = rect().center();
		const auto sign = _forward ? 1 : -1;
		p.drawPolyline(QPolygon({ center + QPoint(-sign * s(4), -s(8)),
			center + QPoint(sign * s(4), 0), center + QPoint(-sign * s(4), s(8)) }));
	}

private:
	bool _forward = false;
	bool _shown = false;
	Ui::Animations::Simple _fade;
	Ui::Animations::Simple _hover;

};

class HomeMixedGrid final : public Ui::RpWidget {
public:
	HomeMixedGrid(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const std::vector<Track> &tracks,
		const std::vector<Album> &albums,
		Fn<void(Album)> open,
		Fn<void(Track)> openTrack,
		bool expanded,
		std::shared_ptr<int> offset,
		std::vector<Track> artists = {},
		Fn<void(Track)> openArtist = nullptr,
		bool homePlaybackControls = true)
	: RpWidget(parent)
	, _expanded(expanded)
	, _homePlaybackControls(homePlaybackControls)
	, _offset(std::move(offset)) {
		if (!_expanded) {
			_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea);
			_content = _scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll));
			_scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> event) {
				auto delta = event->pixelDelta().x();
				if (!delta) delta = event->angleDelta().x() / 2;
				if (!delta && event->modifiers().testFlag(Qt::ShiftModifier)) {
					delta = event->angleDelta().y() / 2;
				}
				if (!delta) return false;
				_scroll->scrollToX(_scroll->scrollLeft() - delta);
				return true;
			});
			_scroll->scrolls() | rpl::on_next([=] {
				if (!_restoring && _offset) *_offset = _scroll->scrollLeft();
				updateArrows();
			}, lifetime());
			const auto slide = [=](int delta) {
				const auto from = _scroll->scrollLeft();
				const auto to = std::clamp(from + delta, 0, _scroll->scrollLeftMax());
				_slide.start([=](float64 value) {
					_scroll->scrollToX(int(base::SafeRound(value)));
				}, from, to, 320, anim::easeOutCubic);
			};
			_previous = Ui::CreateChild<HomeCarouselArrow>(this, false, [=] {
				slide(-_scroll->width() * 3 / 4);
			});
			_next = Ui::CreateChild<HomeCarouselArrow>(this, true, [=] {
				slide(_scroll->width() * 3 / 4);
			});
			_fadeLeft = Ui::CreateChild<HomeEdgeFade>(this, HomeEdgeFade::Edge::Left);
			_fadeRight = Ui::CreateChild<HomeEdgeFade>(this, HomeEdgeFade::Edge::Right);
		} else {
			_content = this;
		}
		const auto results = std::make_shared<std::vector<Track>>(tracks);
		auto trackIndex = 0;
		auto albumIndex = 0;
		auto artistIndex = 0;
		const auto limit = _expanded ? tracks.size() + albums.size() + artists.size() : size_t(12);
		const auto artistKey = (artists.empty() ? 0ULL : artists.front().artistId.toULongLong())
			+ (tracks.empty() ? 0ULL : tracks.front().id.toULongLong());
		const auto artistSlot = 1 + int(artistKey % 4);
		while (_items.size() < limit
			&& (trackIndex < int(tracks.size()) || albumIndex < int(albums.size()) || artistIndex < int(artists.size()))) {
			if (artistIndex < int(artists.size()) && openArtist
				&& ((_items.size() % 4) == artistSlot || (trackIndex >= int(tracks.size()) && albumIndex >= int(albums.size())))) {
				_items.push_back(Ui::CreateChild<HomeArtistCard>(_content, artists[artistIndex++], openArtist));
			} else if (albumIndex < int(albums.size())
				&& ((_items.size() % 3) == 2 || (trackIndex >= int(tracks.size()) && artistIndex >= int(artists.size())))) {
				const auto album = albums[albumIndex++];
				_items.push_back(Ui::CreateChild<HomeAlbumCard>(
					_content, controller, album, [=] { open(album); }));
			} else if (trackIndex < int(tracks.size())) {
				const auto index = trackIndex++;
				_items.push_back(Ui::CreateChild<Row>(
					_content, controller, tracks[index], index, RowMode::Home,
					results, -1, openTrack, QString(), _homePlaybackControls));
			} else if (albumIndex < int(albums.size())) {
				const auto album = albums[albumIndex++];
				_items.push_back(Ui::CreateChild<HomeAlbumCard>(_content, controller, album, [=] { open(album); }));
			} else {
				break;
			}
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto gap = s(12);
		const auto available = std::max(newWidth - 2 * gap, 1);
		if (_expanded) {
			const auto columns = std::clamp((available + gap) / (s(180) + gap), 1, 8);
			const auto cardWidth = (available - (columns - 1) * gap) / columns;
			const auto cardHeight = cardWidth + s(56);
			for (auto i = 0; i != int(_items.size()); ++i) {
				_items[i]->setGeometry(gap + (i % columns) * (cardWidth + gap),
					(i / columns) * (cardHeight + gap), cardWidth, cardHeight);
			}
			return ((_items.size() + columns - 1) / columns) * (cardHeight + gap);
		}
		const auto cardWidth = std::min(s(200), available);
		const auto cardHeight = cardWidth + s(56);
		_scroll->setGeometry(0, 0, newWidth, cardHeight + gap);
		_content->resize(gap + int(_items.size()) * (cardWidth + gap), cardHeight + gap);
		for (auto i = 0; i != int(_items.size()); ++i) {
			_items[i]->setGeometry(gap + i * (cardWidth + gap), 0, cardWidth, cardHeight);
		}
		const auto arrow = s(48);
		const auto fade = s(72);
		_fadeLeft->setGeometry(0, 0, fade, cardHeight + gap);
		_fadeRight->setGeometry(std::max(newWidth - fade, 0), 0, fade, cardHeight + gap);
		_fadeLeft->raise();
		_fadeRight->raise();
		_previous->setGeometry(gap, (cardWidth - arrow) / 2, arrow, arrow);
		_next->setGeometry(std::max(newWidth - gap - arrow, 0), (cardWidth - arrow) / 2, arrow, arrow);
		_previous->raise();
		_next->raise();
		if (!_restoreQueued) {
			_restoreQueued = true;
			crl::on_main(this, [=] {
				if (_offset) _scroll->scrollToX(*_offset);
				_restoring = false;
				updateArrows();
			});
		}
		updateArrows();
		return cardHeight + gap;
	}

private:
	void updateArrows() {
		if (!_scroll || !_previous || !_next) return;
		const auto canBack = _scroll->scrollLeft() > 0;
		const auto canForward = _scroll->scrollLeft() < _scroll->scrollLeftMax();
		_previous->toggle(canBack);
		_next->toggle(canForward);
		_fadeLeft->setVisible(canBack);
		_fadeRight->setVisible(canForward);
	}

	bool _expanded = false;
	bool _homePlaybackControls = true;
	bool _restoring = true;
	bool _restoreQueued = false;
	std::shared_ptr<int> _offset;
	Ui::ScrollArea *_scroll = nullptr;
	Ui::RpWidget *_content = nullptr;
	HomeCarouselArrow *_previous = nullptr;
	HomeCarouselArrow *_next = nullptr;
	HomeEdgeFade *_fadeLeft = nullptr;
	HomeEdgeFade *_fadeRight = nullptr;
	Ui::Animations::Simple _slide;
	std::vector<Ui::RpWidget*> _items;

};

// Название открытого альбома и возврат к результатам.
class SearchAlbumBar final : public Ui::AbstractButton {
public:
	SearchAlbumBar(QWidget *parent, const QString &title, Fn<void()> back)
	: AbstractButton(parent)
	, _title(title) {
		resize(width(), 40);
		setPointerCursor(true);
		setClickedCallback(std::move(back));
		paintRequest(
		) | rpl::on_next([=](QRect) {
			auto p = QPainter(this);
			auto hq = PainterHighQualityEnabler(p);
			const auto onBack = isOver();
			p.setPen(QPen(
				onBack ? st::windowFg : st::windowSubTextFg,
				2.,
				Qt::SolidLine,
				Qt::RoundCap,
				Qt::RoundJoin));
			const auto cy = height() / 2.;
			p.drawPolyline(QPolygonF({
				QPointF(16, cy - 6),
				QPointF(10, cy),
				QPointF(16, cy + 6) }));
			const auto available = std::max(width() - 38, 0);
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFg);
			p.drawText(
				QRect(26, 0, available, height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				st::semiboldFont->elided(_title, available));
		}, lifetime());
	}

private:
	QString _title;

};

class AlbumBar final : public Ui::AbstractButton {
public:
	AlbumBar(QWidget *parent, const QString &title, Fn<void()> back, Fn<void()> play)
	: AbstractButton(parent), _title(title) {
		resize(width(), 40);
		setPointerCursor(true);
		setMouseTracking(true);
		setClickedCallback([=] {
			if (playRect().contains(mapFromGlobal(QCursor::pos()))) play();
			else back();
		});
		paintRequest() | rpl::on_next([=](QRect) {
			auto p = QPainter(this);
			auto hq = PainterHighQualityEnabler(p);
			const auto onPlay = playRect().contains(mapFromGlobal(QCursor::pos()));
			p.setPen(QPen(isOver() && !onPlay ? st::windowFg : st::windowSubTextFg,
				2., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			const auto cy = height() / 2.;
			p.drawPolyline(QPolygonF({ QPointF(16, cy - 6), QPointF(10, cy), QPointF(16, cy + 6) }));
			const auto available = playRect().left() - 38;
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFg);
			p.drawText(QRect(26, 0, available, height()), Qt::AlignVCenter | Qt::AlignLeft,
				st::semiboldFont->elided(_title, available));
			p.setPen(onPlay ? st::windowFg : st::windowActiveTextFg);
			p.drawText(playRect(), Qt::AlignVCenter | Qt::AlignRight,
				QString::fromUtf8(Mokagram::Lang::Tr("Слушать альбом")));
		}, lifetime());
	}
protected:
	void mouseMoveEvent(QMouseEvent *event) override {
		AbstractButton::mouseMoveEvent(event);
		update();
	}
private:
	[[nodiscard]] QRect playRect() const {
		const auto w = st::semiboldFont->width(QString::fromUtf8(Mokagram::Lang::Tr("Слушать альбом")));
		return QRect(width() - 8 - w, 0, w, height());
	}
	QString _title;
};

[[nodiscard]] QColor ComputeAccent(const QImage &cover);

class HomeAlbumHeader final : public Ui::RpWidget {
public:
	HomeAlbumHeader(
		QWidget *parent,
		Album album,
		int count,
		bool isTrack,
		Fn<void(QImage, QRect)> showCover,
		bool isArtist = false,
		QString bannerUrl = QString(),
		Fn<void()> playArtist = nullptr,
		Fn<void()> openArtist = nullptr,
		Fn<void()> bannerReady = nullptr)
	: RpWidget(parent)
	, _album(std::move(album))
	, _count(count)
	, _isTrack(isTrack)
	, _isArtist(isArtist)
	, _bannerUrl(std::move(bannerUrl))
	, _showCover(std::move(showCover))
	, _openArtist(std::move(openArtist)) {
		if (_isArtist) {
			_playArtist = Ui::CreateChild<Ui::RoundButton>(
				this,
				rpl::single(PlaylistText("Воспроизвести")),
				BoxParts::PillActive());
			_playArtist->setFocusPolicy(Qt::TabFocus);
			_playArtist->setClickedCallback(std::move(playArtist));
			_playArtistOpacity = new QGraphicsOpacityEffect(_playArtist);
			_playArtistOpacity->setOpacity(1.);
			_playArtist->setGraphicsEffect(_playArtistOpacity);
		}
		setMouseTracking(true);
		setToolTip(_album.title + u" - "_q + _album.artist);
		_artistButton = Ui::CreateChild<Ui::AbstractButton>(this);
		_artistButton->setFocusPolicy(Qt::TabFocus);
		_artistButton->setAccessibleName(_album.artist);
		_artistButton->setClickedCallback([=] {
			if (_openArtist) _openArtist();
		});
		_artistButton->paintOn([=](QPainter &p) {
			if (_artistButton->isOver() || _artistButton->hasFocus()) {
				const auto underline = style::ConvertScale(2);
				p.setPen(st::windowActiveTextFg);
				p.drawLine(0, _artistButton->height() - underline,
					_artistButton->width(), _artistButton->height() - underline);
			}
		});
		LoadImage(ArtUrl(_album.artworkUrl, u"t500x500"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			_scaledArt = QImage();
			update();
		}));
		if (_isArtist && !_bannerUrl.isEmpty()) {
			LoadImage(_bannerUrl, crl::guard(this, [=](QImage image) {
				if (image.isNull()) return;
				_banner = std::move(image);
				_scaledBanner = QImage();
				resizeToWidth(width());
				update();
				if (bannerReady) bannerReady();
			}));
		}
	}

	void setArtistPlayable(bool playable) {
		if (_playArtist) _playArtist->setDisabled(!playable);
	}

	void setArtistIdentityOpacity(double opacity) {
		if (!_playArtist || _artistIdentityOpacity == opacity) return;

		_artistIdentityOpacity = opacity;
		_playArtistOpacity->setOpacity(opacity);
		_playArtist->setVisible(opacity > 0.);
		_playArtist->setAttribute(Qt::WA_TransparentForMouseEvents, opacity < 0.5);
		_playArtist->setFocusPolicy(opacity < 0.5 ? Qt::NoFocus : Qt::TabFocus);
		update();
	}

	[[nodiscard]] bool hasArtistBanner() const {
		return !_banner.isNull();
	}

	[[nodiscard]] int artistBannerHeight() const {
		const auto narrow = width() < style::ConvertScale(520);
		return std::max(height() - style::ConvertScale(narrow ? 160 : 136), 0);
	}

	void setBannerTopInset(int inset) {
		if (!_isArtist || _bannerTopInset == inset) return;
		_bannerTopInset = inset;
		update();
	}

	void paintArtistBanner(QPainter &p, QPoint origin) {
		if (_banner.isNull()) return;

		const auto bounds = QRect(
			origin.x(), origin.y() - _bannerTopInset,
			width(), artistBannerHeight() + _bannerTopInset);
		if (bounds.isEmpty()) return;
		if (_scaledBanner.isNull() || _scaledBannerSize != bounds.size()) {
			_scaledBanner = _banner.scaled(
				bounds.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			_scaledBannerSize = bounds.size();
		}
		p.drawImage(bounds, _scaledBanner, QRect(
			(_scaledBanner.width() - bounds.width()) / 2,
			(_scaledBanner.height() - bounds.height()) / 2,
			bounds.width(), bounds.height()));
	}

protected:
	void resizeEvent(QResizeEvent *e) override {
		RpWidget::resizeEvent(e);
		updateArtistButtonGeometry();
		if (_playArtist) {
			const auto inset = style::ConvertScale(30);
			const auto available = std::max(width() - 2 * inset, 0);
			_playArtist->setFullWidth(std::min(available, style::ConvertScale(144)));
			const auto narrow = width() < style::ConvertScale(520);
			_playArtist->moveToLeft(
				narrow ? inset : width() - inset - _playArtist->width(),
				height() - style::ConvertScale(narrow ? 22 : 44) - _playArtist->height());
		}
	}

	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		if (_isArtist) {
			const auto identity = s(newWidth < s(520) ? 152 : 128);
			const auto banner = hasArtistBanner()
				? std::clamp(newWidth / 4, s(120), s(244))
				: 0;
			return banner + identity + s(8);
		}
		if (newWidth < s(480)) {
			return std::min(std::max(newWidth - s(48), 0), s(224)) + s(160);
		}
		return s(288);
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		setCursor(coverRect().contains(e->pos()) && !_art.isNull()
			&& _artistIdentityOpacity >= 0.5
			? style::cur_pointer
			: style::cur_default);
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && coverRect().contains(e->pos())
			&& !_art.isNull() && _artistIdentityOpacity >= 0.5) {
			_showCover(_art, QRect(mapToGlobal(coverRect().topLeft()), coverRect().size()));
		}
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto narrow = width() < s(480);
		const auto margin = s(24);
		const auto art = coverRect();
		if (_isArtist) {
			paintArtist(p, art);
			return;
		}
		auto clip = QPainterPath();
		clip.addRoundedRect(art, s(6), s(6));
		p.save();
		p.setClipPath(clip);
		if (_art.isNull()) {
			p.fillRect(art, QColor(0x38, 0x38, 0x38));
		} else {
			if (_scaledArt.isNull() || _scaledArtSize != art.size()) {
				_scaledArt = _art.scaled(art.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				_scaledArtSize = art.size();
			}
			const auto source = QRect((_scaledArt.width() - art.width()) / 2,
				(_scaledArt.height() - art.height()) / 2, art.width(), art.height());
			p.drawImage(art, _scaledArt, source);
		}
		p.restore();
		const auto left = narrow ? margin : art.right() + s(25);
		const auto top = narrow ? art.bottom() + s(12) : s(76);
		const auto available = std::max(width() - left - margin, 0);
		auto label = st::normalFont->f;
		label.setPixelSize(s(14));
		p.setFont(label);
		p.setPen(QColor(255, 255, 255, 190));
		p.drawText(QRect(left, top, available, s(24)), Qt::AlignLeft | Qt::AlignVCenter,
			QString::fromUtf8(Mokagram::Lang::Tr(_isTrack ? "Трек" : "Альбом")));
		auto title = st::semiboldFont->f;
		title.setPixelSize(s(narrow ? 28 : 44));
		p.setFont(title);
		p.setPen(Qt::white);
		const auto titleHeight = narrow ? s(56) : s(72);
		p.drawText(QRect(left, top + s(28), available, titleHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(title).elidedText(_album.title, Qt::ElideRight, available));
		p.setFont(label);
		p.setPen(QColor(255, 255, 255, 210));
		const auto details = _album.artist + (_count > 0
			? u" · "_q + QString::fromUtf8(Mokagram::Lang::Tr("треков: %1")).arg(_count)
			: QString());
		p.drawText(QRect(left, top + s(28) + titleHeight, available, s(24)),
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(label).elidedText(details, Qt::ElideRight, available));
	}

private:
	void paintArtist(QPainter &p, const QRect &avatar) {
		const auto s = [](int value) { return style::ConvertScale(value); };
		if (rect().isEmpty()) return;

		const auto identityTop = artistBannerHeight();
		if (hasArtistBanner()) {
			p.fillRect(
				QRect(0, identityTop, width(), height() - identityTop - s(8)),
				st::windowBg);
		}
		const auto narrow = width() < s(520);
		const auto banner = QRect(0, 0, width(), identityTop);
		if (!banner.isEmpty()) {
			p.save();
			p.setClipRect(banner, Qt::IntersectClip);
			p.fillRect(banner, st::windowBgOver);
			paintArtistBanner(p, QPoint());
			p.restore();
		}

		if (_artistIdentityOpacity <= 0.) return;
		p.save();
		p.setOpacity(_artistIdentityOpacity);
		auto avatarClip = QPainterPath();
		avatarClip.addEllipse(avatar);
		p.save();
		p.setClipPath(avatarClip);
		p.fillRect(avatar, st::windowBgOver);
		if (!_art.isNull()) {
			if (_scaledArt.isNull() || _scaledArtSize != avatar.size()) {
				_scaledArt = _art.scaled(
					avatar.size(),
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation);
				_scaledArtSize = avatar.size();
			}
			p.drawImage(avatar, _scaledArt, QRect(
				(_scaledArt.width() - avatar.width()) / 2,
				(_scaledArt.height() - avatar.height()) / 2,
				avatar.width(),
				avatar.height()));
		}
		p.restore();

		const auto left = avatar.right() + s(17);
		const auto right = narrow ? rect().right() - s(30) : _playArtist->x() - s(16);
		const auto available = std::max(right - left + 1, 0);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(left, identityTop + s(24), available, s(20)),
			Qt::AlignVCenter | Qt::AlignLeft, PlaylistText("Исполнитель"));
		auto title = st::semiboldFont->f;
		title.setPixelSize(s(narrow ? 26 : 40));
		p.setFont(title);
		p.setPen(st::windowFg);
		p.drawText(QRect(left, identityTop + s(46), available, s(narrow ? 36 : 52)),
			Qt::AlignVCenter | Qt::AlignLeft,
			QFontMetrics(title).elidedText(_album.title, Qt::ElideRight, available));
		p.restore();
	}

	void updateArtistButtonGeometry() {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto narrow = width() < s(480);
		const auto margin = s(24);
		const auto art = coverRect();
		const auto left = narrow ? margin : art.right() + s(25);
		const auto top = narrow ? art.bottom() + s(12) : s(76);
		const auto titleHeight = s(narrow ? 56 : 72);
		const auto available = std::max(width() - left - margin, 0);
		auto font = st::normalFont->f;
		font.setPixelSize(s(14));
		const auto artistWidth = std::min(
			QFontMetrics(font).horizontalAdvance(_album.artist),
			available);
		_artistButton->setGeometry(
			left,
			top + s(28) + titleHeight,
			artistWidth,
			s(24));
		const auto hasArtist = !_isArtist
			&& bool(_openArtist)
			&& !_album.artist.trimmed().isEmpty()
			&& artistWidth > 0;
		_artistButton->setVisible(hasArtist);
		_artistButton->setDisabled(!hasArtist);
		_artistButton->setPointerCursor(hasArtist);
	}

	[[nodiscard]] QRect coverRect() const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		if (_isArtist) {
			const auto narrow = width() < s(520);
			const auto side = s(narrow ? 64 : 88);
			return QRect(s(30), height() - s(narrow ? 140 : 116), side, side);
		}
		const auto side = (width() < s(480))
			? std::min(std::max(width() - s(48), 0), s(224))
			: s(224);
		return QRect(s(24), s(32), side, side);
	}

	Album _album;
	int _count = 0;
	bool _isTrack = false;
	bool _isArtist = false;
	QString _bannerUrl;
	int _bannerTopInset = 0;
	QImage _art;
	QImage _scaledArt;
	QSize _scaledArtSize;
	QImage _banner;
	QImage _scaledBanner;
	QSize _scaledBannerSize;
	Fn<void(QImage, QRect)> _showCover;
	Fn<void()> _openArtist;
	Ui::RoundButton *_playArtist = nullptr;
	QGraphicsOpacityEffect *_playArtistOpacity = nullptr;
	double _artistIdentityOpacity = 1.;
	Ui::AbstractButton *_artistButton = nullptr;

};

class HomeArtistLikesCard final : public Ui::AbstractButton {
public:
	HomeArtistLikesCard(
		QWidget *parent,
		QString artworkUrl,
		QString counts,
		bool expanded,
		Fn<void()> toggle)
	: AbstractButton(parent)
	, _counts(std::move(counts))
	, _action(PlaylistText(expanded ? "Свернуть" : "Показать все")) {
		resize(width(), style::ConvertScale(84));
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(PlaylistText("Вам нравится") + u" · "_q + _counts
			+ u" · "_q + _action);
		setClickedCallback(std::move(toggle));
		LoadImage(ArtUrl(artworkUrl, u"t200x200"_q), crl::guard(this, [=](QImage image) {
			_art = std::move(image);
			update();
		}));
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		update();
	}

	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int value) { return style::ConvertScale(value); };
		BoxParts::PaintCard(p, rect());
		if (isOver() || isDown()) {
			p.setBrush(isDown() ? st::windowBgRipple : st::windowBgOver);
			p.drawRoundedRect(rect(), s(22), s(22));
		}
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(1), s(1), -s(1), -s(1)), s(22), s(22));
		}
		const auto artwork = QRect(s(14), s(14), s(56), s(56));
		auto clip = QPainterPath();
		clip.addRoundedRect(artwork, s(14), s(14));
		p.save();
		p.setClipPath(clip);
		p.fillRect(artwork, st::windowBgOver);
		if (!_art.isNull()) p.drawImage(artwork, _art);
		p.restore();

		const auto left = artwork.right() + s(15);
		const auto actionWidth = st::semiboldFont->width(_action);
		const auto showAction = width() >= left + actionWidth + s(152);
		const auto right = width() - s(16) - (showAction ? actionWidth + s(16) : 0);
		const auto available = std::max(right - left, 0);
		const auto title = PlaylistText("Вам нравится");
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(left, s(16), available, s(24)),
			Qt::AlignVCenter | Qt::AlignLeft, st::semiboldFont->elided(title, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(left, s(43), available, s(22)),
			Qt::AlignVCenter | Qt::AlignLeft, st::normalFont->elided(_counts, available));
		if (showAction) {
			p.setFont(st::semiboldFont);
			p.setPen(st::windowActiveTextFg);
			p.drawText(QRect(right + s(16), 0, actionWidth, height()),
				Qt::AlignVCenter | Qt::AlignLeft, _action);
		}
	}

private:
	QString _counts;
	QString _action;
	QImage _art;
};

enum class DiscographyFilter {
	All,
	Albums,
	SinglesAndEps,
};

class HomeDiscographyBar final : public Ui::RpWidget {
public:
	HomeDiscographyBar(
		QWidget *parent,
		DiscographyFilter selected,
		bool expanded,
		Fn<void(DiscographyFilter)> filter,
		Fn<void()> toggle)
	: RpWidget(parent) {
		_title = Ui::CreateChild<Ui::FlatLabel>(
			this, PlaylistText("Дискография"), st::boxTitle);
		if (toggle) {
			_toggle = Ui::CreateChild<Ui::RoundButton>(
				this,
				rpl::single(PlaylistText(expanded ? "Свернуть" : "Показать все")),
				BoxParts::PillLight());
			_toggle->setFocusPolicy(Qt::TabFocus);
			_toggle->setClickedCallback(std::move(toggle));
		}
		const auto labels = std::array{
			"Все релизы",
			"Альбомы",
			"Синглы и EP",
		};
		for (auto i = 0; i != int(labels.size()); ++i) {
			const auto category = DiscographyFilter(i);
			const auto button = _filters[i] = Ui::CreateChild<Ui::RoundButton>(
				this,
				rpl::single(PlaylistText(labels[i])),
				selected == category ? BoxParts::PillActive() : BoxParts::PillLight());
			button->setFocusPolicy(Qt::TabFocus);
			button->setClickedCallback([=] { filter(category); });
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int value) { return style::ConvertScale(value); };
		const auto rowHeight = s(44);
		const auto toggleWidth = _toggle ? std::min(s(130), newWidth / 2) : 0;
		_title->resizeToWidth(std::max(newWidth - toggleWidth - s(8), 0));
		_title->moveToLeft(0, (rowHeight - _title->height()) / 2, newWidth);
		if (_toggle) {
			_toggle->setFullWidth(toggleWidth);
			_toggle->moveToRight(0, (rowHeight - _toggle->height()) / 2, newWidth);
		}
		const auto gap = s(6);
		const auto count = int(_filters.size());
		const auto cell = std::max(std::min(s(144), (newWidth - (count - 1) * gap) / count), 0);
		for (auto i = 0; i != count; ++i) {
			_filters[i]->setFullWidth(cell);
			_filters[i]->moveToLeft(i * (cell + gap), rowHeight + s(6), newWidth);
		}
		return 2 * rowHeight + s(6);
	}

private:
	Ui::FlatLabel *_title = nullptr;
	Ui::RoundButton *_toggle = nullptr;
	std::array<Ui::RoundButton*, 3> _filters = {};
};

class HomeCoverPreview final : public Ui::RpWidget {
public:
	HomeCoverPreview(QWidget *parent, QImage image, QRect source)
	: RpWidget(parent)
	, _image(std::move(image))
	, _source(source) {
		setGeometry(parent->rect());
		setFocusPolicy(Qt::TabFocus);
		parent->installEventFilter(this);
		show();
		raise();
		setFocus();
		if (!PowerSaving::On(PowerSaving::kAnimations)) {
			_animation.start([=] { update(); }, 0., 1., 220, anim::easeOutCubic);
		}
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		if (watched == parentWidget() && event->type() == QEvent::Resize) {
			setGeometry(parentWidget()->rect());
		}
		return RpWidget::eventFilter(watched, event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		if (event->button() == Qt::LeftButton && !targetRect().contains(event->pos())) {
			closePreview();
		}
	}

	void keyPressEvent(QKeyEvent *event) override {
		if (event->key() == Qt::Key_Escape) {
			closePreview();
		} else {
			RpWidget::keyPressEvent(event);
		}
	}

	void paintEvent(QPaintEvent *event) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = _animation.value(_closing ? 0. : 1.);
		p.fillRect(rect(), QColor(0, 0, 0, int(210 * progress)));
		const auto target = targetRect();
		const auto start = QRectF(_source);
		const auto current = QRectF(
			start.x() + (target.x() - start.x()) * progress,
			start.y() + (target.y() - start.y()) * progress,
			start.width() + (target.width() - start.width()) * progress,
			start.height() + (target.height() - start.height()) * progress);
		p.setOpacity(progress);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(current, _image);
		p.setFont(st::semiboldFont);
		p.setPen(Qt::white);
		p.drawText(QRectF(target.left(), target.bottom() + style::ConvertScale(12),
			target.width(), style::ConvertScale(32)), Qt::AlignCenter,
			QString::fromUtf8(Mokagram::Lang::Tr("Закрыть")));
	}

private:
	[[nodiscard]] QRectF targetRect() const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto side = std::max(std::min({ width() - s(48), height() - s(100), s(600) }), 0);
		return QRectF((width() - side) / 2., (height() - side - s(40)) / 2., side, side);
	}

	void closePreview() {
		if (_closing) return;
		_closing = true;
		if (PowerSaving::On(PowerSaving::kAnimations)) {
			deleteLater();
			return;
		}
		const auto current = _animation.value(1.);
		_animation.start([=] { update(); }, current, 0., 180, anim::easeOutCubic);
		QTimer::singleShot(180, this, [=] { deleteLater(); });
	}

	QImage _image;
	QRect _source;
	Ui::Animations::Simple _animation;
	bool _closing = false;

};

// ---- полноценный плеер ----

class HomeRetryView final : public Ui::AbstractButton {
public:
	HomeRetryView(QWidget *parent, QString message, Fn<void()> retry)
	: AbstractButton(parent)
	, _message(std::move(message)) {
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Попробовать снова")));
		setClickedCallback(std::move(retry));
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto text = QFontMetrics(st::normalFont->f).boundingRect(
			QRect(0, 0, std::max(newWidth - s(24), 1), s(1000)),
			Qt::TextWordWrap, _message);
		return text.height() + s(64);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto s = [](int v) { return style::ConvertScale(v); };
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(s(12), 0, std::max(width() - s(24), 0), height() - s(40)),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, _message);
		p.setPen(st::windowActiveTextFg);
		p.drawText(QRect(s(12), height() - s(40), std::max(width() - s(24), 0), s(32)),
			Qt::AlignLeft | Qt::AlignVCenter,
			QString::fromUtf8(Mokagram::Lang::Tr("Попробовать снова")));
		if (hasFocus()) {
			p.setPen(QPen(st::windowActiveTextFg->c, s(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(rect().adjusted(s(2), s(2), -s(2), -s(2)), s(6), s(6));
		}
	}

private:
	QString _message;

};

class HomeLoadingView final : public Ui::RpWidget {
public:
	HomeLoadingView(QWidget *parent, QString text)
	: RpWidget(parent)
	, _text(std::move(text)) {
		if (!PowerSaving::On(PowerSaving::kAnimations)) {
			_started = crl::now();
			_animation.init([=](crl::time) {
				update();
				return true;
			});
			_animation.start();
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto gap = s(12);
		const auto available = std::max(newWidth - 2 * gap, 1);
		const auto columns = std::clamp((available + gap) / (s(180) + gap), 1, 6);
		const auto cardWidth = (available - (columns - 1) * gap) / columns;
		return s(56) + ((6 + columns - 1) / columns) * (cardWidth + s(56) + gap);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto gap = s(12);
		const auto available = std::max(width() - 2 * gap, 1);
		const auto columns = std::clamp((available + gap) / (s(180) + gap), 1, 6);
		const auto cardWidth = (available - (columns - 1) * gap) / columns;
		const auto cardHeight = cardWidth + s(56);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(QRect(gap, 0, available, s(24)),
			Qt::AlignVCenter | Qt::AlignLeft, _text);
		const auto bar = QRectF(gap, s(32), std::min(available, s(240)), s(3));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0x36, 0x36, 0x36));
		p.drawRoundedRect(bar, s(2), s(2));
		// Сегмент въезжает слева и уезжает вправо за край полоски, поэтому на
		// стыке цикла нет скачка в начало.
		const auto segment = bar.width() * 0.3;
		const auto phase = _started ? std::fmod((crl::now() - _started) / 1400., 1.) : 0.;
		const auto x = bar.left() - segment + (bar.width() + segment) * phase;
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(bar, s(2), s(2));
		p.setClipPath(clip);
		p.setBrush(st::windowActiveTextFg);
		p.drawRoundedRect(QRectF(x, bar.top(), segment, bar.height()), s(2), s(2));
		p.restore();
		for (auto i = 0; i != 6; ++i) {
			const auto left = gap + (i % columns) * (cardWidth + gap);
			const auto top = s(56) + (i / columns) * (cardHeight + gap);
			p.setBrush(QColor(0x24, 0x24, 0x24));
			p.drawRoundedRect(QRect(left, top, cardWidth, cardHeight), s(8), s(8));
			p.setBrush(QColor(0x32, 0x32, 0x32));
			const auto art = std::max(cardWidth - 2 * gap, 0);
			p.drawRoundedRect(QRect(left + gap, top + gap, art, art), s(6), s(6));
			p.drawRoundedRect(QRect(left + gap, top + cardWidth, art * 3 / 4, s(8)), s(4), s(4));
			p.drawRoundedRect(QRect(left + gap, top + cardWidth + s(20), art / 2, s(8)), s(4), s(4));
		}
	}

private:
	QString _text;
	Ui::Animations::Basic _animation;
	crl::time _started = 0;

};

// Загрузка списка: бегущая полоска, подпись и мерцающие заглушки строк.
class LoadingView final : public Ui::RpWidget {
public:
	LoadingView(QWidget *parent, const QString &text)
	: RpWidget(parent)
	, _text(text) {
		resize(width(), kHeaderHeight + kRows * kRowStep + 8);
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
		if (!PowerSaving::On(PowerSaving::kAnimations)) {
			_started = crl::now();
			_animation.init([=](crl::time) {
				update();
				return true;
			});
			_animation.start();
		}
	}

private:
	static constexpr auto kHeaderHeight = 44;
	static constexpr auto kRows = 4;
	static constexpr auto kRowStep = 60;

	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto time = _started ? (crl::now() - _started) : crl::time(0);
		const auto skeleton = [&] {
			auto color = st::windowSubTextFg->c;
			color.setAlphaF(0.16);
			return color;
		}();

		// Подпись слегка «дышит».
		auto textColor = st::windowSubTextFg->c;
		textColor.setAlphaF(_started
			? 0.6 + 0.4 * (0.5 + 0.5 * std::sin(time / 260.))
			: 1.);
		p.setPen(textColor);
		p.setFont(st::normalFont);
		p.drawText(
			QRect(24, 0, width() - 48, 24),
			Qt::AlignLeft | Qt::AlignVCenter,
			_text);

		// Неопределённая полоска: сегмент бежит слева направо.
		const auto barRect = QRectF(24, 28, width() - 48, 4);
		p.setPen(Qt::NoPen);
		p.setBrush(skeleton);
		p.drawRoundedRect(barRect, 2, 2);
		const auto phase = std::fmod(time / 1100., 1.);
		const auto eased = 1. - std::pow(1. - phase, 2.);
		const auto segment = barRect.width() * 0.32;
		const auto x = _started
			? barRect.left() - segment + (barRect.width() + segment) * eased
			: barRect.left();
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(barRect, 2, 2);
		p.setClipPath(clip);
		p.setBrush(st::windowActiveTextFg->c);
		p.drawRoundedRect(QRectF(x, barRect.top(), segment, barRect.height()), 2, 2);
		p.restore();

		// Заглушки строк с бегущим бликом.
		const auto sweep = _started
			? std::fmod(time / 1400., 1.) * (width() + 160) - 80
			: -1000.;
		for (auto i = 0; i != kRows; ++i) {
			const auto top = kHeaderHeight + i * kRowStep;
			auto shape = QPainterPath();
			shape.addRoundedRect(QRectF(16, top + 6, 44, 44), 8, 8);
			shape.addRoundedRect(QRectF(72, top + 12, width() * 0.45 - i * 14, 10), 5, 5);
			shape.addRoundedRect(QRectF(72, top + 32, width() * 0.28, 9), 4.5, 4.5);
			p.setPen(Qt::NoPen);
			p.setBrush(skeleton);
			p.drawPath(shape);
			p.save();
			p.setClipPath(shape);
			auto glow = QLinearGradient(sweep - 70, 0, sweep + 70, 0);
			auto edge = st::windowFg->c;
			edge.setAlphaF(0.);
			auto mid = st::windowFg->c;
			mid.setAlphaF(0.10);
			glow.setColorAt(0., edge);
			glow.setColorAt(0.5, mid);
			glow.setColorAt(1., edge);
			p.fillRect(QRectF(0, top, width(), kRowStep), glow);
			p.restore();
		}
	}

	const QString _text;
	Ui::Animations::Basic _animation;
	crl::time _started = 0;

};

constexpr auto kHintStickerKey = "music.hint_sticker";

// Свой стикер вместо стандартного: документ целиком, с версией формата,
// чтобы после перезапуска не зависеть от загруженных наборов.
[[nodiscard]] DocumentData *LoadHintSticker(not_null<Main::Session*> session) {
	const auto encoded = Mokagram::Settings::String(
		QString::fromLatin1(kHintStickerKey));
	if (encoded.isEmpty()) {
		return nullptr;
	}
	auto bytes = QByteArray::fromBase64(encoded.toLatin1());
	auto stream = QDataStream(&bytes, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	stream >> version;
	const auto document = Serialize::Document::readFromStream(
		session,
		version,
		stream);
	return (stream.status() == QDataStream::Ok
		&& document
		&& document->sticker())
		? document
		: nullptr;
}

void SaveHintSticker(DocumentData *document) {
	const auto key = QString::fromLatin1(kHintStickerKey);
	if (!document) {
		Mokagram::Settings::Reset(key);
		return;
	}
	auto bytes = QByteArray();
	{
		auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(AppVersion);
		Serialize::Document::writeToStream(stream, document);
	}
	Mokagram::Settings::SetString(key, QString::fromLatin1(bytes.toBase64()));
}

// Пустой поиск: анимированный стикер посередине. Правой кнопкой его
// можно сменить на любой свой стикер.
class StickerHint final : public Ui::RpWidget {
public:
	StickerHint(
		QWidget *parent,
		not_null<Main::Session*> session,
		Fn<void(QPoint)> showMenu)
	: RpWidget(parent)
	, _session(session)
	, _showMenu(std::move(showMenu)) {
		_sheet = QPixmap(u":/gui/art/mokagram_music_hint.png"_q);
		_sheet.setDevicePixelRatio(2.);
		resize(width(), kSide + 56);
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
		Mokagram::Settings::StringValue(
			QString::fromLatin1(kHintStickerKey)
		) | rpl::on_next([=](const QString &) {
			loadSticker();
		}, lifetime());
		if (!PowerSaving::On(PowerSaving::kAnimations)) {
			_started = crl::now();
			_animation.init([=](crl::time) {
				update();
				return true;
			});
			_animation.start();
		}
	}

protected:
	void contextMenuEvent(QContextMenuEvent *e) override {
		if (_showMenu) {
			_showMenu(e->globalPos());
		}
	}

private:
	void loadSticker() {
		_playerLifetime.destroy();
		_player = nullptr;
		_custom = LoadHintSticker(_session);
		update();
		if (!_custom) {
			return;
		}
		const auto document = not_null<DocumentData*>(_custom);
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();
		rpl::single() | rpl::then(
			_session->downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::on_next([=] {
			using namespace HistoryView;
			const auto info = document->sticker();
			const auto box = QSize(kSide, kSide);
			if (info->isLottie()) {
				_player = std::make_shared<LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::ChatIntroHelloSticker,
						box,
						Lottie::Quality::High));
			} else if (info->isWebm()) {
				_player = std::make_shared<WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					box);
			} else {
				_player = std::make_shared<StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					box);
			}
			_player->setRepaintCallback([=] { update(); });
			update();
		}, _playerLifetime);
	}

	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto target = QRect((width() - kSide) / 2, 28, kSide, kSide);
		if (_custom) {
			// Пока свой стикер грузится, место остаётся пустым,
			// чтобы стандартный не мелькал перед ним.
			if (!_player || !_player->ready()) {
				return;
			}
			const auto paused = !_started || !window()->isActiveWindow();
			const auto frame = _player->frame(
				QSize(kSide, kSide),
				QColor(0, 0, 0, 0),
				false,
				crl::now(),
				paused).image;
			const auto size = HistoryView::DownscaledSize(
				frame.size() / style::DevicePixelRatio(),
				QSize(kSide, kSide));
			p.drawImage(
				QRect(
					target.x() + (kSide - size.width()) / 2,
					target.y() + (kSide - size.height()) / 2,
					size.width(),
					size.height()),
				frame);
			_player->markFrameShown();
			return;
		}
		if (_sheet.isNull()) {
			return;
		}
		const auto frame = _started
			? int(((crl::now() - _started) / kFrameDuration) % kFrames)
			: 0;
		// Кадр в листе задан в пикселях устройства, а целевой квадрат —
		// в логических, иначе при DPR 2 картинка ужимается и уезжает влево.
		p.drawPixmap(
			target,
			_sheet,
			QRect(
				(frame % kColumns) * kSource,
				(frame / kColumns) * kSource,
				kSource,
				kSource));
	}

	static constexpr auto kSource = 192;
	static constexpr auto kSide = kSource / 2;
	static constexpr auto kColumns = 9;
	static constexpr auto kFrames = 49;
	static constexpr auto kFrameDuration = crl::time(1000 / 15);

	const not_null<Main::Session*> _session;
	const Fn<void(QPoint)> _showMenu;
	QPixmap _sheet;
	Ui::Animations::Basic _animation;
	crl::time _started = 0;
	DocumentData *_custom = nullptr;
	std::shared_ptr<HistoryView::StickerPlayer> _player;
	rpl::lifetime _playerLifetime;

};

[[nodiscard]] QString FormatSeekTime(qint64 seconds) {
	const auto time = std::max<qint64>(seconds, 0);
	return u"%1:%2"_q.arg(time / 60).arg(time % 60, 2, 10, QChar('0'));
}

class SeekBar final : public Ui::RpWidget {
public:
	explicit SeekBar(QWidget *parent, Fn<QColor()> background = nullptr)
	: RpWidget(parent)
	, _background(std::move(background)) {
		resize(width(), 28);
		setCursor(style::cur_pointer);
		Mokagram::Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (_expressive && key == u"player.expressive_seek"_q) {
				updateWaveAnimation();
				update();
			}
		}, lifetime());
		paintRequest(
		) | rpl::on_next([=](QRect area) {
			auto p = QPainter(this);
			if (testAttribute(Qt::WA_OpaquePaintEvent)) {
				p.fillRect(area, _background());
			}
			auto hq = PainterHighQualityEnabler(p);
			if (_thin) {
				if (showsWave()) {
					paintWave(p);
					return;
				}
				// Тонкая полоса: толще на наведении и при перетаскивании.
				const auto grow = _grow.value((_over || _dragging) ? 1. : 0.);
				const auto h = 4. + 2. * grow;
				const auto line = QRectF(0, height() / 2. - h / 2., width(), h);
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(255, 255, 255, 46));
				p.drawRoundedRect(line, h / 2., h / 2.);
				const auto progress = _dragging ? _drag : _progress;
				auto fill = line;
				fill.setWidth(line.width() * progress);
				if (progress > 0.) {
					p.setBrush(_fill);
					p.drawRoundedRect(fill, h / 2., h / 2.);
				}
				if (grow > 0.) {
					const auto radius = std::min(5. * grow, line.width() / 2.);
					const auto x = std::clamp(line.width() * progress, radius, line.width() - radius);
					p.setBrush(Qt::white);
					p.drawEllipse(QPointF(x, line.center().y()), radius, radius);
				}
				return;
			}
			const auto line = QRectF(8, height() / 2. - 2, width() - 16, 4);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(line, 3, 3);
			const auto progress = _dragging ? _drag : _progress;
			auto fill = line;
			fill.setWidth(line.width() * progress);
			p.setBrush(st::windowFg);
			p.drawRoundedRect(fill, 2, 2);
			const auto knob = QPointF(fill.right(), line.center().y());
			p.setBrush(Qt::white);
			p.drawEllipse(knob, _dragging ? 8. : 6., _dragging ? 8. : 6.);
		}, lifetime());
	}

	void enableTimePreview() {
		_timePreview = Ui::CreateChild<Ui::RpWidget>(parentWidget());
		_timePreview->setAttribute(Qt::WA_TransparentForMouseEvents);
		_timePreview->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			const auto radius = _timePreview->height() / 2.;
			p.drawRoundedRect(_timePreview->rect(), radius, radius);
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFg);
			p.drawText(_timePreview->rect(), Qt::AlignCenter, _previewTime);
		});
		_timePreview->hide();
	}

	void setDuration(qint64 duration) {
		if (_duration == duration) return;
		_duration = duration;
		refreshTimePreview(mapFromGlobal(QCursor::pos()).x());
	}

	void setOpaqueBackground(bool opaque) {
		if (testAttribute(Qt::WA_OpaquePaintEvent) != opaque) {
			setAttribute(Qt::WA_OpaquePaintEvent, opaque);
			update();
		}
	}

	void setFillColor(QColor color) {
		_fill = color;
		update();
	}
	void setExpressive(bool expressive, bool playing) {
		if (_expressive == expressive && _wavePlaying == playing) {
			return;
		}
		_expressive = expressive;
		_wavePlaying = playing;
		updateWaveAnimation();
		update();
	}
	void setThin(bool thin) {
		_thin = thin;
		setMouseTracking(true);
		updateWaveAnimation();
		update();
	}
	void setProgress(float64 value) {
		value = std::clamp(value, 0., 1.);
		if (_progress == value) {
			return;
		}
		_progress = value;
		if (!_dragging) {
			update();
		}
	}
	[[nodiscard]] bool waveAnimating() const {
		return _waveAnimating.current();
	}
	[[nodiscard]] rpl::producer<bool> waveAnimationChanges() const {
		return _waveAnimating.changes();
	}
	void advanceWave(float64 elapsed) {
		const auto wanted = _wavePlaying ? 1. : 0.;
		_wavePhase += elapsed * 2. * wanted;
		_waveAmp += (wanted - _waveAmp) * (1. - std::exp(-elapsed * 8.));
		if (!_wavePlaying && _waveAmp <= 0.01) {
			_waveAmp = 0.;
			_waveAnimating = false;
		}
		update();
	}
	[[nodiscard]] rpl::producer<float64> sought() const {
		return _sought.events();
	}
protected:
	void showEvent(QShowEvent *e) override {
		RpWidget::showEvent(e);
		updateWaveAnimation();
	}
	void hideEvent(QHideEvent *e) override {
		_waveAnimating = false;
		if (_timePreview) _timePreview->hide();
		RpWidget::hideEvent(e);
	}
	void enterEventHook(QEnterEvent *e) override {
		setOver(true);
		refreshTimePreview(e->position().toPoint().x());
	}
	void leaveEventHook(QEvent *e) override {
		setOver(false);
		refreshTimePreview(mapFromGlobal(QCursor::pos()).x());
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			e->ignore();
			return;
		}
		_dragging = true;
		startGrow();
		moveTo(e->pos().x());
		refreshTimePreview(e->pos().x());
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (_dragging) {
			moveTo(e->pos().x());
		}
		refreshTimePreview(e->pos().x());
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (_dragging && e->button() == Qt::LeftButton) {
			_dragging = false;
			_over = rect().contains(e->pos());
			startGrow();
			moveTo(e->pos().x());
			_progress = _drag;
			refreshTimePreview(e->pos().x());
			_sought.fire_copy(_drag);
		}
	}

private:
	[[nodiscard]] bool showsWave() const {
		return _thin && _expressive && !_over && !_dragging
			&& Mokagram::Settings::Bool(u"player.expressive_seek"_q);
	}
	void updateWaveAnimation() {
		_waveAnimating = isVisible() && showsWave()
			&& (_wavePlaying || _waveAmp > 0.01);
	}
	void paintWave(QPainter &p) {
		const auto y = height() / 2.;
		const auto end = width() * _progress;
		const auto phase = _wavePhase;
		const auto amplitude = st::motionWaveAmplitude * _waveAmp;
		p.setPen(QPen(QColor(255, 255, 255, 65), st::motionWaveStroke,
			Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(std::min(end + st::motionWaveGap, double(width())), y),
			QPointF(width(), y));
		auto path = QPainterPath();
		for (auto x = 0.; x <= end; x += std::max(st::motionWaveSample, 1)) {
			const auto envelope = std::clamp((end - x) / st::motionWaveLength, 0., 1.);
			const auto point = QPointF(x, y + amplitude * envelope
				* std::sin(x * 2. * M_PI / st::motionWaveLength - phase));
			if (x == 0.) path.moveTo(point); else path.lineTo(point);
		}
		p.setPen(QPen(_fill, st::motionWaveStroke, Qt::SolidLine, Qt::RoundCap));
		p.drawPath(path);
		p.setPen(Qt::NoPen);
		p.setBrush(_fill);
		const auto handle = st::motionWaveHandle;
		p.drawRoundedRect(QRectF(end - handle / 2.,
			y - st::motionWaveHandleHeight / 2., handle, st::motionWaveHandleHeight),
			handle / 2., handle / 2.);
	}

	void setOver(bool over) {
		if (_over != over) {
			_over = over;
			startGrow();
		}
	}
	void startGrow() {
		updateWaveAnimation();
		const auto to = (_over || _dragging) ? 1. : 0.;
		_grow.start([=] { update(); }, 1. - to, to, 180, anim::easeOutCubic);
	}
	[[nodiscard]] float64 progressAt(int x) const {
		const auto pad = _thin ? 0 : 8;
		return std::clamp((x - pad) / float64(std::max(width() - 2 * pad, 1)), 0., 1.);
	}
	void moveTo(int x) {
		_drag = progressAt(x);
		update();
	}
	void refreshTimePreview(int x) {
		if (!_timePreview) return;
		if (!isVisible() || (!_over && !_dragging) || _duration <= 0) {
			_timePreview->hide();
			return;
		}

		const auto s = [](int value) { return style::ConvertScale(value); };
		const auto progress = progressAt(x);
		const auto time = FormatSeekTime(qint64(_duration * progress / 1000.));
		const auto width = st::semiboldFont->width(time) + s(16);
		const auto left = this->x() + std::clamp(x, 0, this->width()) - width / 2;
		_timePreview->setGeometry(
			std::clamp(left, 0, std::max(parentWidget()->width() - width, 0)),
			y() - s(21), width, s(22));
		if (time != _previewTime) {
			_previewTime = time;
			_timePreview->update();
		}
		_timePreview->raise();
		_timePreview->show();
	}

	Fn<QColor()> _background;
	Ui::RpWidget *_timePreview = nullptr;
	QString _previewTime;
	qint64 _duration = 0;
	Ui::Animations::Simple _grow;
	rpl::variable<bool> _waveAnimating = false;
	bool _expressive = false;
	bool _wavePlaying = false;
	float64 _wavePhase = 0.;
	float64 _waveAmp = 0.;
	bool _thin = false;
	bool _over = false;
	QColor _fill = Qt::white;
	float64 _progress = 0.;
	float64 _drag = 0.;
	bool _dragging = false;
	rpl::event_stream<float64> _sought;

};

// Значки островка (SVG из mokagram_island_glyphs.h) в квадрат size по центру c.
void PaintIslandGlyph(
		QPainter &p,
		const char *svg,
		QPointF c,
		int size,
		QColor color = QColor(Qt::white)) {
	using Key = std::tuple<const void*, int, QRgb>;
	static auto cache = base::flat_map<Key, QImage>();
	const auto ratio = style::DevicePixelRatio();
	const auto key = Key(svg, size, color.rgba());
	auto i = cache.find(key);
	if (i == end(cache)) {
		auto image = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		auto q = QPainter(&image);
		auto renderer = QSvgRenderer(QByteArray(svg));
		renderer.render(&q, QRectF(0, 0, size, size));
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(QRectF(0, 0, size, size), color);
		q.end();
		i = cache.emplace(key, std::move(image)).first;
	}
	p.drawImage(QPointF(c.x() - size / 2., c.y() - size / 2.), i->second);
}

class VolumeSlider final : public Ui::ContinuousSlider {
public:
	explicit VolumeSlider(QWidget *parent) : ContinuousSlider(parent) {
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Громкость")));
	}

	void setHovered(bool hovered) {
		_hovered = hovered;
		refreshHighlight();
	}

	void setAccent(QColor accent) {
		_accent = accent;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto seek = QRectF(myrtlrect(getSeekRect()));
		const auto thickness = style::ConvertScale(4);
		const auto y = seek.center().y();
		const auto from = seek.left();
		const auto to = seek.right();
		const auto value = getCurrentValue();
		const auto x = from + (to - from) * value;
		const auto highlight = _highlight.value();
		p.setPen(QPen(QColor(255, 255, 255, 65), thickness,
			Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(from, y), QPointF(to, y));
		if (value > 0.) {
			const auto color = anim::color(QColor(Qt::white),
				_accent.isValid() ? _accent : st::windowActiveTextFg->c,
				highlight);
			p.setPen(QPen(color, thickness, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(from, y), QPointF(x, y));
		}
		if (highlight > 0.) {
			p.setOpacity(highlight);
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::white);
			const auto radius = style::ConvertScale(6);
			p.drawEllipse(QPointF(x, y), radius, radius);
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			e->ignore();
			return;
		}
		ContinuousSlider::mousePressEvent(e);
		refreshHighlight();
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			e->ignore();
			return;
		}
		ContinuousSlider::mouseMoveEvent(e);
		ContinuousSlider::mouseReleaseEvent(e);
		refreshHighlight();
	}

	void focusInEvent(QFocusEvent *e) override {
		ContinuousSlider::focusInEvent(e);
		refreshHighlight();
	}

	void focusOutEvent(QFocusEvent *e) override {
		ContinuousSlider::focusOutEvent(e);
		refreshHighlight();
	}

private:
	// The parent tracks hover across the speaker and the slider together.
	void enterEventHook(QEnterEvent *) override {
	}
	void leaveEventHook(QEvent *) override {
	}
	float64 getOverDuration() const override {
		return 160.;
	}
	QSize getSeekDecreaseSize() const override {
		return QSize(2 * style::ConvertScale(6), 0);
	}
	void refreshHighlight() {
		_highlight.set(_hovered || isChanging() || hasFocus(), [=] { update(); });
	}

	QColor _accent;
	HoverFade _highlight;
	bool _hovered = false;
};

class VolumeControl final : public Ui::RpWidget {
public:
	explicit VolumeControl(QWidget *parent)
	: RpWidget(parent)
	, _mute(Ui::CreateChild<Ui::AbstractButton>(this))
	, _slider(Ui::CreateChild<VolumeSlider>(this)) {
		resize(width(), style::ConvertScale(28));
		_mute->setPointerCursor(true);
		_mute->setFocusPolicy(Qt::TabFocus);
		_mute->paintRequest() | rpl::on_next([=](QRect) {
			auto p = QPainter(_mute);
			auto hq = PainterHighQualityEnabler(p);
			const auto bounds = QRectF(_mute->rect());
			if (_mute->hasFocus()) {
				const auto inset = style::ConvertScale(2);
				p.setBrush(Qt::NoBrush);
				p.setPen(QPen(st::windowActiveTextFg->c, inset));
				p.drawEllipse(bounds.adjusted(inset, inset, -inset, -inset));
			}
			p.setOpacity(_mute->isOver() || _mute->hasFocus() ? 1. : 0.6);
			using namespace Mokagram::Music::IslandGlyphs;
			PaintIslandGlyph(p, _muted ? kGlyphSpeaker : kGlyphOutput,
				bounds.center(), style::ConvertScale(20));
		}, _mute->lifetime());

		const auto setVolume = [](float64 value) {
			auto &app = Core::App();
			if (app.settings().songVolume() != value) {
				Media::Player::mixer()->setSongVolume(value);
				app.settings().setSongVolume(value);
				app.saveSettingsDelayed();
			}
		};
		_slider->setChangeProgressCallback(setVolume);
		_slider->setChangeFinishedCallback([](float64 value) {
			if (value > 0.) {
				Core::App().settings().setRememberedSongVolume(value);
			}
		});
		_mute->setClickedCallback([=] {
			auto &settings = Core::App().settings();
			const auto volume = settings.songVolume();
			if (volume > 0.) {
				settings.setRememberedSongVolume(volume);
				setVolume(0.);
			} else {
				setVolume(settings.rememberedSongVolume());
			}
		});
		const auto refresh = [=](float64 volume) {
			const auto value = std::clamp(volume, 0., 1.);
			_slider->setValue(value);
			if (value > 0. && !_slider->isChanging()) {
				Core::App().settings().setRememberedSongVolume(value);
			}
			const auto muted = (value == 0.);
			if (_muted != muted || _mute->accessibleName().isEmpty()) {
				_muted = muted;
				const auto text = QString::fromUtf8(Mokagram::Lang::Tr(
					muted ? "Включить звук" : "Выключить звук"));
				_mute->setAccessibleName(text);
				_mute->setToolTip(text);
				_mute->update();
			}
		};
		const auto &settings = Core::App().settings();
		refresh(settings.songVolume());
		settings.songVolumeChanges() | rpl::on_next(refresh, lifetime());
	}

	void setAccent(QColor accent) {
		_slider->setAccent(accent);
	}

protected:
	void enterEventHook(QEnterEvent *e) override {
		RpWidget::enterEventHook(e);
		_slider->setHovered(true);
	}
	void leaveEventHook(QEvent *e) override {
		RpWidget::leaveEventHook(e);
		_slider->setHovered(false);
	}
	void hideEvent(QHideEvent *e) override {
		_slider->setHovered(false);
		RpWidget::hideEvent(e);
	}
	void resizeEvent(QResizeEvent *) override {
		const auto iconWidth = style::ConvertScale(28);
		_mute->setGeometry(0, 0, iconWidth, height());
		_slider->setGeometry(iconWidth, 0, std::max(width() - iconWidth, 0), height());
	}

private:
	const not_null<Ui::AbstractButton*> _mute;
	const not_null<VolumeSlider*> _slider;
	bool _muted = false;
};

enum class Glyph { Previous, Next, Play, Add, Shuffle, Repeat, More, Block, Collapse, Queue, Filter };

class RoundGlyph final : public Ui::AbstractButton {
public:
	RoundGlyph(QWidget *parent, Glyph glyph, int size)
	: AbstractButton(parent)
	, _glyph(glyph) {
		resize(size, size);
		setPointerCursor(true);
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
	}

	void setAccent(QColor accent) {
		_accent = accent.isValid() ? accent : QColor(Qt::white);
		update();
	}

	void setPlaying(bool playing) {
		if (_playing != playing) {
			_playing = playing;
			update();
		}
	}
	void setLiked(bool liked) {
		if (_liked != liked) {
			_liked = liked;
			if (_overlay && liked) {
				_pop.start([=] { update(); }, 0., 1., 360, anim::easeOutCubic);
			}
			update();
		}
	}
	void animateLike(bool liked) {
		setLiked(liked);
		if (LikeAnimationsEnabled()) {
			_likeAnimation.stop();
			_likeAnimation.start([=] { update(); }, 0., 1., kLikeDuration);
		}
	}
	// Кнопки капсулы плеера: значки островка, без кругов-подложек.
	void setIsland(bool island) {
		_island = island;
		update();
	}
	// Кнопки полноэкранного плеера: белые значки, тёмный полупрозрачный круг.
	void setOverlay(bool overlay) {
		_overlay = overlay;
		update();
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		if ((_overlay || _island) && ((was & StateFlag::Over) != (state() & StateFlag::Over))) {
			const auto over = isOver();
			_hover.start([=] { update(); }, over ? 0. : 1., over ? 1. : 0., 180, anim::easeOutCubic);
		}
		if ((_overlay || _island) && ((was & StateFlag::Down) != (state() & StateFlag::Down))) {
			const auto down = isDown();
			_press.start([=] { update(); }, down ? 0. : 1., down ? 1. : 0., 120, anim::easeOutCubic);
		}
	}

private:
	void paintOverlay(QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(rect());
		auto c = r.center();
		const auto over = _hover.value(isOver() ? 1. : 0.);
		const auto down = _press.value(isDown() ? 1. : 0.);
		const auto play = (_glyph == Glyph::Play);
		const auto dark = (_glyph == Glyph::Collapse);
		p.setPen(Qt::NoPen);
		// Play: тёмный полупрозрачный круг с белым значком, как кнопки в шапке.
		p.setBrush(play
			? QColor(255, 255, 255, int(36 + 20 * over))
			: dark
			? QColor(0, 0, 0, int(80 + 50 * over))
			: QColor(255, 255, 255, int(40 * over)));
		p.translate(c);
		const auto squeeze = 1. - 0.08 * down;
		p.scale(squeeze, squeeze);
		p.translate(-c);
		p.drawEllipse(r);
		const auto pen = QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		const auto u = r.width() / 48.;
		if (_glyph == Glyph::Collapse) {
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawPolyline(QPolygonF({
				c + QPointF(-7, -3) * u * 1.2,
				c + QPointF(0, 4) * u * 1.2,
				c + QPointF(7, -3) * u * 1.2 }));
			return;
		}
		if (_glyph == Glyph::Filter) {
			p.setPen(pen);
			for (auto i = 0; i != 3; ++i) {
				const auto x = c.x() + (i - 1) * 7 * u;
				const auto y = c.y() + (i - 1) * 3 * u;
				p.drawLine(QPointF(x, c.y() - 9 * u), QPointF(x, y - 2 * u));
				p.drawLine(QPointF(x, y + 2 * u), QPointF(x, c.y() + 9 * u));
				p.drawEllipse(QPointF(x, y), 2 * u, 2 * u);
			}
			return;
		}
		if (_glyph == Glyph::Queue) {
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			for (auto i = 0; i != 3; ++i) {
				const auto y = c.y() + (i - 1) * 6 * u * 1.2;
				p.drawLine(QPointF(c.x() - 3 * u * 1.2, y), QPointF(c.x() + 8 * u * 1.2, y));
				p.drawPoint(QPointF(c.x() - 8 * u * 1.2, y));
			}
			return;
		}
		if (_glyph == Glyph::Add) {
			const auto pop = 1. + 0.3 * std::sin(_pop.value(1.) * M_PI) + 0.06 * over;
			p.translate(c);
			const auto base = (width() >= 44) ? 1.05 : 0.8;
			p.scale(pop * base, pop * base);
			auto heart = QPainterPath();
			heart.moveTo(QPointF(0, 8.5));
			heart.cubicTo(QPointF(-14, -1), QPointF(-8.5, -11.5), QPointF(0, -4));
			heart.cubicTo(QPointF(8.5, -11.5), QPointF(14, -1), QPointF(0, 8.5));
			heart.closeSubpath();
			if (_liked) {
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(0xFF, 0x3B, 0x5C));
			} else {
				p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				p.setBrush(Qt::NoBrush);
			}
			p.drawPath(heart);
			return;
		}
		if (_glyph == Glyph::Shuffle || _glyph == Glyph::Repeat) {
			const auto &settings = Core::App().settings();
			const auto shuffle = (_glyph == Glyph::Shuffle);
			const auto mode = settings.playerRepeatMode();
			const auto active = shuffle
				? settings.playerOrderMode() == Media::OrderMode::Shuffle
				: mode != Media::RepeatMode::None;
			const auto &icon = shuffle
				? st::mediaPlayerOrderIconShuffle
				: mode == Media::RepeatMode::One
				? st::mediaPlayerRepeatOneIcon
				: st::mediaPlayerRepeatDisabledIcon;
			// Включённый режим: яркий значок и точка под ним, выключенный приглушён.
			if (active) {
				p.setBrush(_accent);
				p.drawEllipse(QPointF(c.x(), c.y() + 17. * u), 2. * u, 2. * u);
			}
			const auto scale = 28. / std::max(icon.width(), icon.height());
			p.translate(
				c.x() - icon.width() * scale / 2.,
				c.y() - icon.height() * scale / 2.);
			p.scale(scale, scale);
			icon.paint(
				p,
				QPoint(),
				icon.width(),
				active ? _accent : QColor(255, 255, 255, 150));
			return;
		}
		p.setBrush(Qt::white);
		p.setPen(Qt::NoPen);
		if (_glyph == Glyph::Play) {
			if (_playing) {
				p.drawRoundedRect(QRectF(c.x() - 11 * u, c.y() - 12 * u, 8 * u, 24 * u), 2.5 * u, 2.5 * u);
				p.drawRoundedRect(QRectF(c.x() + 3 * u, c.y() - 12 * u, 8 * u, 24 * u), 2.5 * u, 2.5 * u);
			} else {
				auto path = QPainterPath();
				path.moveTo(c.x() - 7 * u, c.y() - 12 * u);
				path.lineTo(c.x() + 13 * u, c.y());
				path.lineTo(c.x() - 7 * u, c.y() + 12 * u);
				path.closeSubpath();
				p.drawPath(path);
			}
			return;
		}
		const auto dir = (_glyph == Glyph::Next) ? 1. : -1.;
		auto path = QPainterPath();
		path.moveTo(c.x() + dir * (-9) * u, c.y() - 11 * u);
		path.lineTo(c.x() + dir * 7 * u, c.y());
		path.lineTo(c.x() + dir * (-9) * u, c.y() + 11 * u);
		path.closeSubpath();
		p.drawPath(path);
		p.drawRoundedRect(QRectF(c.x() + dir * 9 * u - 2 * u, c.y() - 11 * u, 4 * u, 22 * u), 1.5 * u, 1.5 * u);
	}

	void paintIsland(QPainter &p) {
		using namespace Mokagram::Music::IslandGlyphs;
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto c = QRectF(rect()).center();
		const auto over = _hover.value(isOver() ? 1. : 0.);
		const auto down = _press.value(isDown() ? 1. : 0.);
		auto scale = 1. - 0.12 * down;
		auto color = QColor(Qt::white);
		auto svg = (const char*)nullptr;
		auto size = s(22);
		auto opacity = 0.85 + 0.15 * over;
		switch (_glyph) {
		case Glyph::Previous: svg = kGlyphBack; size = s(34); break;
		case Glyph::Next: svg = kGlyphForward; size = s(34); break;
		case Glyph::Play:
			svg = _playing ? kGlyphPause : kGlyphPlay;
			size = s(30);
			break;
		case Glyph::Queue:
			svg = kGlyphQueue;
			opacity = 0.6 + 0.25 * over;
			break;
		case Glyph::Add: {
			svg = _liked ? kGlyphHeartFill : kGlyphHeart;
			color = _liked ? QColor(0xFF, 0x3B, 0x5C) : QColor(Qt::white);
			opacity = _liked ? 1. : (0.6 + 0.25 * over);
			const auto pop = _pop.value(1.);
			scale *= 1. + 0.3 * std::sin(pop * M_PI);
		} break;
		case Glyph::Shuffle:
		case Glyph::Repeat: {
			const auto &settings = Core::App().settings();
			const auto shuffle = (_glyph == Glyph::Shuffle);
			const auto mode = settings.playerRepeatMode();
			const auto active = shuffle
				? (settings.playerOrderMode() == Media::OrderMode::Shuffle)
				: (mode != Media::RepeatMode::None);
			svg = shuffle
				? kGlyphShuffle
				: (mode == Media::RepeatMode::One)
				? kGlyphRepeatOne
				: kGlyphRepeat;
			if (active) {
				// Включённый режим: светлый круг, как в островке.
				const auto r = s(15);
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(255, 255, 255, 46));
				p.drawEllipse(c, r, r);
			}
			opacity = active ? 1. : (0.6 + 0.25 * over);
		} break;
		default: break;
		}
		if (!svg) {
			return;
		}
		p.setOpacity(opacity);
		p.translate(c);
		p.scale(scale, scale);
		PaintIslandGlyph(p, svg, QPointF(), size, color);
	}

	void paint() {
		auto p = QPainter(this);
		if (_island) {
			paintIsland(p);
			return;
		}
		if (_overlay) {
			paintOverlay(p);
			return;
		}
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(rect());
		const auto c = r.center();
		p.setPen(Qt::NoPen);
		if (_glyph == Glyph::Add) {
			// Лайк: контурное сердце, после добавления в профиль закрашено.
			auto heart = QPainterPath();
			heart.moveTo(c + QPointF(0, 8.5));
			heart.cubicTo(
				c + QPointF(-14, -1),
				c + QPointF(-8.5, -11.5),
				c + QPointF(0, -4));
			heart.cubicTo(
				c + QPointF(8.5, -11.5),
				c + QPointF(14, -1),
				c + QPointF(0, 8.5));
			heart.closeSubpath();
			const auto progress = _likeAnimation.value(1.);
			if (progress < 1.) {
				if (_liked) {
					DrawLikeBurst(p, c, 18., progress, QColor(0xFF, 0x3B, 0x5C));
				}
				const auto scale = LikeScale(progress, _liked);
				p.translate(c);
				p.scale(scale, scale);
				p.translate(-c);
			}
			if (_liked) {
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(0xFF, 0x3B, 0x5C));
			} else {
				p.setPen(QPen(
					QColor(st::windowFg->c),
					2.,
					Qt::SolidLine,
					Qt::RoundCap,
					Qt::RoundJoin));
				p.setBrush(Qt::NoBrush);
			}
			p.drawPath(heart);
			return;
		}
		if (_glyph == Glyph::More || _glyph == Glyph::Block) {
			const auto pen = QPen(
				isOver() ? QColor(st::windowActiveTextFg->c) : QColor(st::windowFg->c),
				2.,
				Qt::SolidLine,
				Qt::RoundCap,
				Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			if (_glyph == Glyph::More) {
				p.drawLine(c + QPointF(-8, 0), c + QPointF(8, 0));
				p.drawLine(c + QPointF(0, -8), c + QPointF(0, 8));
			} else {
				p.drawEllipse(c, 9., 9.);
				p.drawLine(c + QPointF(-6.4, 6.4), c + QPointF(6.4, -6.4));
			}
			return;
		}
		if (_glyph == Glyph::Shuffle || _glyph == Glyph::Repeat) {
			const auto &settings = Core::App().settings();
			const auto shuffle = (_glyph == Glyph::Shuffle);
			const auto mode = settings.playerRepeatMode();
			const auto active = shuffle
				? settings.playerOrderMode() == Media::OrderMode::Shuffle
				: mode != Media::RepeatMode::None;
			const auto &icon = shuffle
				? st::mediaPlayerOrderIconShuffle
				: mode == Media::RepeatMode::One
				? st::mediaPlayerRepeatOneIcon
				: st::mediaPlayerRepeatDisabledIcon;
			const auto scale = 22. / std::max(icon.width(), icon.height());
			p.translate(
				c.x() - icon.width() * scale / 2.,
				c.y() - icon.height() * scale / 2.);
			p.scale(scale, scale);
			icon.paint(p, QPoint(), icon.width(), active
				? QColor(st::windowActiveTextFg->c)
				: QColor(st::windowFg->c));
			return;
		}
		if (_glyph == Glyph::Play) {
			auto fill = QColor(st::windowFg->c);
			fill.setAlpha(isOver() ? 56 : 38);
			p.setBrush(fill);
			p.drawEllipse(r.adjusted(2, 2, -2, -2));
			p.setBrush(st::windowFg);
			const auto u = r.width() / 100.;
			if (_playing) {
				p.drawRoundedRect(QRectF(c.x() - 14 * u, c.y() - 17 * u, 10 * u, 34 * u), 3 * u, 3 * u);
				p.drawRoundedRect(QRectF(c.x() + 4 * u, c.y() - 17 * u, 10 * u, 34 * u), 3 * u, 3 * u);
			} else {
				auto path = QPainterPath();
				path.moveTo(c.x() - 11 * u, c.y() - 19 * u);
				path.lineTo(c.x() + 19 * u, c.y());
				path.lineTo(c.x() - 11 * u, c.y() + 19 * u);
				path.closeSubpath();
				p.drawPath(path);
			}
			return;
		}
		if (isOver()) {
			p.setBrush(st::windowBgOver);
			p.drawEllipse(r);
		}
		p.setBrush(st::windowFg);
		const auto u = r.width() / 48.;
		const auto dir = (_glyph == Glyph::Next) ? 1. : -1.;
		auto tri = [&](double x) {
			auto path = QPainterPath();
			path.moveTo(c.x() + dir * (x - 8) * u, c.y() - 11 * u);
			path.lineTo(c.x() + dir * (x + 8) * u, c.y());
			path.lineTo(c.x() + dir * (x - 8) * u, c.y() + 11 * u);
			path.closeSubpath();
			p.drawPath(path);
		};
		tri(-2);
		p.drawRoundedRect(QRectF(c.x() + dir * 9 * u - 2 * u, c.y() - 11 * u, 4 * u, 22 * u), 1.5 * u, 1.5 * u);
	}

	Glyph _glyph;
	QColor _accent = Qt::white;
	bool _island = false;
	bool _playing = false;
	bool _liked = false;
	Ui::Animations::Simple _likeAnimation;
	bool _overlay = false;
	Ui::Animations::Simple _hover;
	Ui::Animations::Simple _press;
	Ui::Animations::Simple _pop;

};

} // namespace

namespace {

[[nodiscard]] QColor ComputeAccent(const QImage &cover) {
	if (cover.isNull()) {
		return QColor();
	}
	const auto small = cover.scaled(
		24,
		24,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
	constexpr auto kBins = 12;
	double weight[kBins] = {};
	double sumR[kBins] = {}, sumG[kBins] = {}, sumB[kBins] = {};
	for (auto y = 0; y != small.height(); ++y) {
		for (auto x = 0; x != small.width(); ++x) {
			const auto c = QColor(small.pixel(x, y));
			const auto s = c.hsvSaturationF();
			const auto v = c.valueF();
			if (v < 0.12) {
				continue;
			}
			const auto bin = std::clamp(int(std::max(c.hsvHueF(), 0.f) * kBins), 0, kBins - 1);
			const auto w = (0.05 + s * s) * (0.3 + v);
			weight[bin] += w;
			sumR[bin] += w * c.redF();
			sumG[bin] += w * c.greenF();
			sumB[bin] += w * c.blueF();
		}
	}
	auto totalS = 0., totalV = 0., total = 0.;
	for (auto y = 0; y != small.height(); ++y) {
		for (auto x = 0; x != small.width(); ++x) {
			const auto c = QColor(small.pixel(x, y));
			totalS += c.hsvSaturationF();
			totalV += c.valueF();
			total += 1.;
		}
	}
	const auto avgS = totalS / total;
	const auto avgV = totalV / total;
	// Почти чёрно-белая обложка: нейтральный серый, без случайного оттенка.
	if (avgS < 0.16) {
		return QColor::fromHsvF(0., 0., std::clamp(avgV, 0.3, 0.5));
	}
	auto best = 0;
	for (auto i = 1; i != kBins; ++i) {
		if (weight[i] > weight[best]) {
			best = i;
		}
	}
	if (weight[best] <= 0.) {
		return QColor();
	}
	auto result = QColor::fromRgbF(
		sumR[best] / weight[best],
		sumG[best] / weight[best],
		sumB[best] / weight[best]);
	auto h = 0.f, s = 0.f, v = 0.f;
	result.getHsvF(&h, &s, &v);
	return QColor::fromHsvF(
		h < 0 ? 0.f : h,
		std::clamp(s, 0.25f, 0.8f),
		std::clamp(v, 0.4f, 0.7f));
}

[[nodiscard]] QImage CoverBackgroundForPlayback() {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto cover = CoverForPlayback(current);
	if (cover.isNull()) {
		return {};
	}
	static auto cachedKey = qint64();
	static auto cached = QImage();
	if (cachedKey != cover.cacheKey() || cached.isNull()) {
		cachedKey = cover.cacheKey();
		cached = Images::BlurLargeImage(
			cover.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
				.convertToFormat(QImage::Format_ARGB32_Premultiplied),
			10);
		auto p = QPainter(&cached);
		p.fillRect(cached.rect(), QColor(0x14, 0x14, 0x15, 180));
	}
	return cached;
}

class CoverBackground final {
public:
	void refresh(Fn<void()> repaint, bool visible);
	void paint(QPainter &p, QRect area, QRect canvas) const;

private:
	[[nodiscard]] QImage frame() const;

	QImage _from;
	QImage _to;
	Ui::Animations::Simple _animation;

};

// Карточка комментария поверх плеера, в палитре и формах mokaGram.
class CommentHoverCard final : public Ui::RpWidget {
public:
	explicit CommentHoverCard(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFocusPolicy(Qt::NoFocus);
		hide();
	}
	void setComment(const TrackComment &comment, const QImage &avatar, int availableWidth) {
		const auto s = [](int v) { return style::ConvertScale(v); };
		_author = comment.author;
		const auto seconds = std::max<qint64>(0, comment.timestampMs / 1000);
		_time = u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
		_avatar = avatar;
		const auto cardWidth = std::min(s(360), std::max(0, availableWidth));
		_body.clearLayout();
		_body.setText(comment.body);
		_body.setFont(st::normalFont->f);
		auto option = QTextOption();
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		_body.setTextOption(option);
		_body.beginLayout();
		auto y = 0.;
		while (true) {
			auto line = _body.createLine();
			if (!line.isValid()) break;
			line.setLineWidth(std::max(1, cardWidth - s(32)));
			line.setPosition(QPointF(0., y));
			y += line.height();
		}
		_body.endLayout();
		resize(cardWidth, std::min(s(72) + int(std::ceil(y)),
			std::max(0, parentWidget()->height() - s(28))));
		setAccessibleName(_author + u"\n"_q + comment.body);
		update();
	}
protected:
	void paintEvent(QPaintEvent *) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		BoxParts::PaintCard(p, rect());
		const auto avatarRect = QRect(s(16), s(16), s(28), s(28));
		p.save();
		auto clip = QPainterPath();
		clip.addEllipse(avatarRect);
		p.setClipPath(clip);
		if (!_avatar.isNull()) {
			p.drawImage(avatarRect, _avatar);
		} else {
			p.fillRect(avatarRect, st::windowBgActive);
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(avatarRect, Qt::AlignCenter, _author.left(1).toUpper());
		}
		p.restore();
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto timeWidth = st::normalFont->width(_time);
		p.drawText(QRect(width() - s(16) - timeWidth, s(16), timeWidth, s(28)), Qt::AlignVCenter, _time);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		const auto authorWidth = std::max(0, width() - s(76) - timeWidth);
		p.drawText(QRect(s(52), s(16), authorWidth, s(28)), Qt::AlignVCenter,
			st::semiboldFont->elided(_author, authorWidth));
		p.setClipRect(QRect(s(16), s(56), width() - s(32), std::max(0, height() - s(72))));
		_body.draw(&p, QPointF(s(16), s(56)));
	}
private:
	QString _author, _time;
	QImage _avatar;
	QTextLayout _body;
};

// Комментарии живут рядом с текстом в обоих вариантах плеера.
class TimedComments final : public Ui::AbstractButton {
public:
	explicit TimedComments(QWidget *parent) : AbstractButton(parent) {
		resize(style::ConvertScale(32), style::ConvertScale(32));
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Комментарии SoundCloud")));
		setClickedCallback([=] {
			if (_active.empty()) return;
			if (_slots.empty()) return;
			_seekRequests.fire_copy(_comments[hoveredComment()].timestampMs);
		});
		Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (key != u"lyrics.comments"_q) return;
			++_generation;
			_loading = false;
			_comments.clear();
			_active.clear();
			_avatars.clear();
			_pendingAvatars.clear();
			_next.clear();
			_note.clear();
			if (Settings::Bool(key) && validTrack()) load({});
			refresh();
		}, lifetime());
		Settings::RowIconsUpdated() | rpl::on_next([=] { update(); }, lifetime());
	}

	void setTrack(const QString &id) {
		if (id == _trackId) return;
		_trackId = id;
		++_generation;
		_loading = false;
		_comments.clear();
		_active.clear();
		_avatars.clear();
		_pendingAvatars.clear();
		_next.clear();
		_note.clear();
		if (Settings::Bool(u"lyrics.comments"_q) && validTrack()) load({});
		refresh();
	}
	void setTimelineDuration(qint64 duration) {
		if (_timelineDuration == duration) return;
		_timelineDuration = duration;
		refresh();
	}
	void setPosition(qint64 position) {
		if (_position == position) return;
		_position = position;
		refresh();
	}
	[[nodiscard]] int avatarCount() const {
		return int(_active.size());
	}
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}
	[[nodiscard]] bool validTrack() const {
		return _trackId.toULongLong() && QString::number(_trackId.toULongLong()) == _trackId;
	}
	[[nodiscard]] rpl::producer<qint64> seekRequests() const {
		return _seekRequests.events();
	}

protected:
	void leaveEventHook(QEvent *event) override {
		if (_hoverCard) _hoverCard->hide();
		AbstractButton::leaveEventHook(event);
	}
	void hideEvent(QHideEvent *event) override {
		if (_hoverCard) _hoverCard->hide();
		AbstractButton::hideEvent(event);
	}
	void resizeEvent(QResizeEvent *event) override {
		layoutSlots();
		AbstractButton::resizeEvent(event);
	}
	void mouseMoveEvent(QMouseEvent *event) override {
		AbstractButton::mouseMoveEvent(event);
		for (auto i = 0; i < int(_slots.size()); ++i) {
			if (_slots[i].rect.contains(event->pos()) && _hoverIndex != i) {
				_hoverIndex = i;
				_hoverComment = 0;
				break;
			}
		}
		updateHoverCard();
	}
	void wheelEvent(QWheelEvent *event) override {
		if (_slots.empty()) return;
		const auto slot = std::clamp(_hoverIndex, 0, int(_slots.size()) - 1);
		const auto count = int(_slots[slot].comments.size());
		_hoverComment = (_hoverComment + (event->angleDelta().y() < 0 ? 1 : count - 1)) % count;
		updateHoverCard();
		update();
		event->accept();
	}
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		for (auto i = 0; i < int(_slots.size()); ++i) {
			const auto &slot = _slots[i];
			const auto selected = i == _hoverIndex
				? std::clamp(_hoverComment, 0, int(slot.comments.size()) - 1) : 0;
			const auto &comment = _comments[slot.comments[selected]];
			p.save();
			auto clip = QPainterPath();
			clip.addEllipse(slot.rect);
			p.setClipPath(clip);
			const auto image = _avatars.find(comment.avatarUrl);
			if (image != _avatars.end() && !image->second.isNull()) {
				p.drawImage(slot.rect, image->second);
			} else {
				p.fillRect(slot.rect, st::windowBgActive);
				p.setPen(st::windowFg);
				p.setFont(st::semiboldFont);
				p.drawText(slot.rect, Qt::AlignCenter, comment.author.left(1).toUpper());
			}
			p.restore();
			if (slot.comments.size() > 1) {
				const auto badgeSize = style::ConvertScale(16);
				const auto badge = QRect(slot.rect.right() - badgeSize + 1,
					slot.rect.bottom() - badgeSize + 1, badgeSize, badgeSize);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBg);
				p.drawEllipse(badge);
				p.setPen(st::windowFg);
				auto font = st::normalFont->f;
				font.setPixelSize(style::ConvertScale(10));
				p.setFont(font);
				p.drawText(badge, Qt::AlignCenter, slot.comments.size() > 99
					? u"99+"_q : QString::number(slot.comments.size()));
			}
		}
	}

private:
	struct Slot {
		QRect rect;
		std::vector<int> comments;
	};
	int hoveredComment() const {
		const auto &slot = _slots[std::clamp(_hoverIndex, 0, int(_slots.size()) - 1)];
		return slot.comments[std::clamp(_hoverComment, 0, int(slot.comments.size()) - 1)];
	}
	void ensureAvatar(int index) {
		const auto url = _comments[index].avatarUrl;
		if (url.isEmpty() || _avatars.contains(url) || !_pendingAvatars.insert(url).second) return;
		const auto generation = _generation;
		LoadImage(url, crl::guard(this, [=](QImage image) {
			if (generation != _generation) return;
			_pendingAvatars.erase(url);
			_avatars[url] = std::move(image);
			updateHoverCard();
			update();
		}));
	}
	void layoutSlots() {
		_slots.clear();
		const auto step = style::ConvertScale(32);
		const auto side = std::min(style::ConvertScale(28), height());
		const auto capacity = std::max(1, width() / step);
		auto groups = std::map<int, std::vector<int>>();
		for (auto i = 0; i < int(_active.size()); ++i) {
			const auto index = _active[i];
			const auto bin = _timelineDuration > 0
				? std::clamp(int(double(_comments[index].timestampMs) / _timelineDuration * capacity), 0, capacity - 1)
				: std::min(i, capacity - 1);
			groups[bin].push_back(index);
		}
		auto mask = QRegion();
		for (auto &[bin, comments] : groups) {
			const auto x = _timelineDuration > 0
				? int(double(_comments[comments.front()].timestampMs) / _timelineDuration * std::max(0, width() - side))
				: bin * step + (step - side) / 2;
			auto rect = QRect(std::clamp(x, 0, std::max(0, width() - side)),
				(height() - side) / 2, side, side);
			if (!_slots.empty() && _slots.back().rect.right() + style::ConvertScale(4) >= rect.left()) {
				auto &previous = _slots.back().comments;
				previous.insert(previous.end(), comments.begin(), comments.end());
				continue;
			}
			mask += QRegion(rect);
			_slots.push_back({ rect, std::move(comments) });
		}
		setMask(mask);
		for (const auto &slot : _slots) ensureAvatar(slot.comments.front());
	}
	void updateHoverCard() {
		if (_slots.empty() || !isOver() || isHidden()) {
			if (_hoverCard) _hoverCard->hide();
			return;
		}
		const auto slot = std::clamp(_hoverIndex, 0, int(_slots.size()) - 1);
		auto comment = _comments[hoveredComment()];
		const auto count = int(_slots[slot].comments.size());
		if (count > 1) comment.author += u" · %1/%2"_q
			.arg(std::clamp(_hoverComment, 0, count - 1) + 1).arg(count);
		ensureAvatar(hoveredComment());
		if (!_hoverCard) _hoverCard = base::make_unique_q<CommentHoverCard>(window());
		const auto image = _avatars.find(comment.avatarUrl);
		const auto root = window();
		const auto margin = style::ConvertScale(14);
		_hoverCard->setComment(comment, image == _avatars.end() ? QImage() : image->second,
			root->width() - 2 * margin);
		const auto anchor = mapTo(root, _slots[slot].rect.topLeft());
		const auto gap = style::ConvertScale(8);
		const auto top = anchor.y() - gap - _hoverCard->height();
		_hoverCard->move(
			std::clamp(anchor.x(), margin, std::max(margin, root->width() - margin - _hoverCard->width())),
			std::clamp(top >= margin ? top : anchor.y() + height() + gap,
				margin, std::max(margin, root->height() - margin - _hoverCard->height())));
		_hoverCard->show();
		_hoverCard->raise();
	}
	void refresh() {
		const auto wasActive = _active;
		_shownTimestamp = -1;
		_active.clear();
		for (auto index = 0; index < int(_comments.size()); ++index) {
			const auto &comment = _comments[index];
			if (comment.timestampMs < 0 || (_timelineDuration <= 0
				&& (comment.timestampMs > _position || _position - comment.timestampMs >= 7000))) continue;
			_shownTimestamp = std::max(_shownTimestamp, comment.timestampMs);
			_active.push_back(index);
		}
		std::stable_sort(_active.begin(), _active.end(), [&](int left, int right) {
			return _comments[left].timestampMs > _comments[right].timestampMs;
		});
		layoutSlots();
		updateHoverCard();
		update();
		if (wasActive != _active) _changes.fire({});
		// Следующая страница нужна, когда воспроизведение дошло до её таймкодов.
		if (!_loading && _note.isEmpty() && !_next.isEmpty()
			&& (_timelineDuration > 0 || _comments.empty() || _comments.back().timestampMs <= _position + 7000)) {
			load(_next);
		}
	}
	void load(const QString &cursor) {
		LOG(("[moka-dbg] comments load() track '%1' setting %2 valid %3").arg(_trackId).arg(Settings::Bool(u"lyrics.comments"_q) ? 1 : 0).arg(validTrack() ? 1 : 0));
		if (!Settings::Bool(u"lyrics.comments"_q) || !validTrack()) return;
		_loading = true;
		_note.clear();
		refresh();
		const auto generation = _generation;
		LoadTrackComments(_trackId, cursor, crl::guard(this, [=](TrackCommentsPage page) {
			if (generation != _generation) return;
			LOG(("[moka-dbg] comments load %1: %2 items, error '%3'").arg(_trackId).arg(page.comments.size()).arg(page.error));
			_loading = false;
			_note = std::move(page.error);
			if (_note.isEmpty()) {
				if (cursor.isEmpty()) _comments.clear();
				for (auto &comment : page.comments) {
					const auto exists = std::any_of(_comments.begin(), _comments.end(),
						[&](const TrackComment &c) { return c.id == comment.id; });
					if (!exists) _comments.push_back(std::move(comment));
				}
				_next = std::move(page.next);
			}
			refresh();
		}));
	}
	base::unique_qptr<CommentHoverCard> _hoverCard;
	QString _trackId, _next, _note;
	std::vector<TrackComment> _comments;
	std::vector<int> _active;
	std::vector<Slot> _slots;
	std::map<QString, QImage> _avatars;
	std::set<QString> _pendingAvatars;
	int _hoverIndex = 0;
	int _hoverComment = 0;
	qint64 _timelineDuration = 0;
	rpl::event_stream<> _changes;
	rpl::event_stream<qint64> _seekRequests;
	qint64 _shownTimestamp = -1;
	qint64 _position = 0;
	int _generation = 0;
	bool _loading = false;
};

class LyricsView final : public Ui::RpWidget {
public:
	explicit LyricsView(QWidget *parent)
	: RpWidget(parent)
	, _comments(Ui::CreateChild<TimedComments>(this)) {
		_comments->changes() | rpl::on_next([=] { layoutComments(); }, lifetime());
		_comments->seekRequests() | rpl::on_next([=](qint64 ms) {
			_lineClicked.fire_copy(ms);
		}, lifetime());
		Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (key == u"lyrics.comments"_q) {
				_heights.clear();
				_layouts.clear();
				layoutComments();
				update();
			}
		}, lifetime());
		resize(width(), 280);
		setMouseTracking(true);
		_clock.start();
		_timer.setInterval(16);
		_timer.setTimerType(Qt::PreciseTimer);
		connect(&_timer, &QTimer::timeout, this, [=] { tick(); });
		connect(qApp, &QGuiApplication::applicationStateChanged, this, [=] {
			updateTimer();
		}, Qt::QueuedConnection);
		PowerSaving::Changes() | rpl::on_next([=] {
			updateTimer();
		}, lifetime());
	}

	// Полноэкранный плеер: текст слева, крупнее, без своей подложки.
	void setBig(bool big) {
		_big = big;
		_coverTint = false;
		_heights.clear();
		_layouts.clear();
		updateTimer();
		update();
	}
	// Полноэкранный плеер рисует сюда свой фон: тогда при перерисовке текста родитель не трогается.
	void setBackground(Fn<void(QPainter&, QRect)> paint) {
		_background = std::move(paint);
		setAttribute(Qt::WA_OpaquePaintEvent, _background != nullptr);
	}
	void setCoverTint(bool on) {
		_coverTint = on;
		updateTimer();
		update();
	}
	void setLoading() {
		reset();
		layoutComments();
		_status = QString::fromUtf8(Mokagram::Lang::Tr("Ищу текст…"));
		updateTimer();
		update();
	}
	void setLyrics(Lyrics lyrics) {
		reset();
		_lyrics = std::move(lyrics);
		_status = _lyrics.found
			? QString()
			: _lyrics.unavailable
			? QString::fromUtf8(Mokagram::Lang::Tr("Сервис текста временно недоступен"))
			: QString::fromUtf8(Mokagram::Lang::Tr("Текст этой песни не найден"));
		layoutComments();
		_weights.assign(_lyrics.lines.size(), 0.f);
		updateTimer();
		tick();
		update();
	}
	void updateCover() {
		if (!_big) {
			_backdrop.refresh([=] { update(); }, isVisible());
		}
	}
	void setCommentsTrack(const QString &id) {
		_comments->setTrack(id);
		layoutComments();
	}
	void followPlayback() {
		_manualUntil = 0;
	}
	void setPosition(qint64 ms, bool playing) {
		const auto jumped = _big && std::abs(ms - livePosition()) > 1000;
		if (jumped) {
			followPlayback();
			_scrollReady = false;
		}
		_comments->setPosition(ms);
		updateCover();
		_position = ms;
		_positionAt = _clock.elapsed();
		_playing = playing;
		updateTimer();
		if (jumped || !_timer.isActive()) {
			tick();
		}
	}

	// Нажали на строку синхронного текста: время строки в мс (как в Spotify, перемотка к этой строке).
	[[nodiscard]] rpl::producer<qint64> lineClicked() const {
		return _lineClicked.events();
	}

protected:
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override {
		if (!_lyrics.found || _lyrics.lines.empty()) {
			e->ignore();
			return;
		}
		const auto delta = e->pixelDelta().isNull()
			? -e->angleDelta().y() / 120. * 48.
			: -double(e->pixelDelta().y());
		if (_lyrics.synced) {
			const auto last = int(_lyrics.lines.size()) - 1;
			const auto topMargin = double(style::ConvertScale(_big ? 32 : 16));
			const auto minScroll = anchorY() - topMargin;
			const auto maxScroll = std::max(centerOf(last), minScroll);
			_scroll = std::clamp(_scroll + delta, minScroll, maxScroll);
			_manualUntil = _clock.elapsed() + 3000;
		} else {
			auto total = 24.;
			for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
				total += heightOf(i);
			}
			_plainScroll = std::clamp(_plainScroll + delta, 0., std::max(total - lyricsHeight(), 0.));
		}
		e->accept();
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		setCursor((lineAt(e->pos().y()) >= 0) ? style::cur_pointer : style::cur_default);
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		if (const auto index = lineAt(e->pos().y()); index >= 0) {
			_lineClicked.fire_copy(_lyrics.lines[index].timeMs);
		}
	}

private:
	// Номер строки под точкой по вертикали (-1 нет), считается так же, как рисуется.
	[[nodiscard]] int lineAt(int y) {
		if (y >= lyricsHeight() || !_lyrics.found || !_lyrics.synced || _lyrics.lines.empty()) {
			return -1;
		}
		auto top = anchorY() - _scroll;
		for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
			const auto lineHeight = heightOf(i);
			if (y >= top && y < top + lineHeight) {
				return i;
			}
			top += lineHeight;
			if (top > height()) {
				break;
			}
		}
		return -1;
	}
	rpl::event_stream<qint64> _lineClicked;
	void updateTimer();
	void reset() {
		_lyrics = {};
		_current = -1;
		_scroll = 0.;
		_plainScroll = 0.;
		_manualUntil = 0;
		_scrollReady = false;
		_heights.clear();
		_layouts.clear();
		_weights.clear();
		_fill = 0.;
	}

	// Позиция трека между обновлениями плеера: плавнее подсветка строк.
	[[nodiscard]] qint64 livePosition() const {
		return _position + (_playing
			? std::min<qint64>(_clock.elapsed() - _positionAt, 600)
			: 0);
	}

	void tick() {
		if (!isVisible()) {
			_last = _clock.elapsed();
			return;
		}
		layoutComments();
		const auto now = _clock.elapsed();
		const auto dt = std::clamp((now - _last) / 1000., 0., 0.1);
		_last = now;
		auto changed = false;
		if (_lyrics.found && _lyrics.synced && !_lyrics.lines.empty()) {
			auto index = -1;
			const auto at = livePosition() + 120;
			for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
				if (_lyrics.lines[i].timeMs <= at) {
					index = i;
				} else {
					break;
				}
			}
			changed |= (_current != index);
			_current = index;
			auto target = centerOf(std::max(index, 0));
			const auto topMargin = double(style::ConvertScale(_big ? 32 : 16));
			target = std::max(target, anchorY() - topMargin);
			const auto snap = !_scrollReady;
			if (snap) {
				_scroll = target;
				_scrollReady = true;
			}
			// Пружина без рывков: чем дальше строка, тем быстрее догоняем.
			// Пока пользователь листает текст сам (и 3 с после), автопрокрутка к строке не работает.
			if (now >= _manualUntil) {
				changed |= (std::abs(target - _scroll) > 0.1);
				_scroll += (target - _scroll)
					* (_timer.isActive() ? (1. - std::exp(-dt * 6.5)) : 1.);
			}
			const auto fade = _timer.isActive() && !snap
				? float(1. - std::exp(-dt * 9.))
				: 1.f;
			for (auto i = 0; i != int(_weights.size()); ++i) {
				const auto goal = (i == _current) ? 1.f : 0.f;
				changed |= (std::abs(goal - _weights[i]) > 0.001f);
				_weights[i] += (goal - _weights[i]) * fade;
			}
			const auto fill = (_current >= 0)
				? lineProgress(_current, at)
				: 0.;
			changed |= (std::abs(fill - _fill) > 0.0005);
			_fill = fill;
		}
		if (changed) {
			update();
		}
	}

	// Доля спетого в строке (0..1). Пословных меток у lrc нет, поэтому темп
	// угадываем: ~12 знаков/с, как поют обычно. Пауза в конце строки
	// заливку не растягивает: строка заливается раньше, чем начнётся следующая.
	[[nodiscard]] double lineProgress(int index, qint64 at) const {
		const auto &lines = _lyrics.lines;
		const auto start = lines[index].timeMs;
		auto span = qint64(std::max(int(lines[index].text.size()), 1)) * 85;
		if (index + 1 < int(lines.size())) {
			span = std::min(span, lines[index + 1].timeMs - start - 150);
		}
		span = std::max<qint64>(span, 250);
		return std::clamp((at - start) / double(span), 0., 1.);
	}

	[[nodiscard]] double anchorY() const {
		return lyricsHeight() * (_big ? 0.4 : 0.5);
	}
	[[nodiscard]] int textWidth() const {
		const auto gutter = Settings::Bool(u"lyrics.comments"_q) && _comments->validTrack()
			? style::ConvertScale(108) : 0;
		return std::max(1, width() - (_big ? 0 : style::ConvertScale(48)) - gutter);
	}
	[[nodiscard]] QFont lineFont() const {
		auto font = st::semiboldFont->f;
		font.setPixelSize(style::ConvertScale(_big ? 36 : 24));
		if (_big) {
			font.setWeight(QFont::DemiBold);
		}
		return font;
	}
	[[nodiscard]] const QTextLayout &layoutOf(int index) {
		if (_layouts.size() != _lyrics.lines.size()) {
			_layouts.clear();
			_layouts.resize(_lyrics.lines.size());
		}
		auto &layout = _layouts[index];
		if (!layout) {
			const auto &text = _lyrics.lines[index].text;
			layout = std::make_unique<QTextLayout>(
				text.isEmpty() ? u" "_q : text,
				lineFont());
			auto option = QTextOption(_big ? Qt::AlignLeft : Qt::AlignHCenter);
			option.setWrapMode(QTextOption::WordWrap);
			layout->setTextOption(option);
			layout->beginLayout();
			auto y = 0.;
			for (auto line = layout->createLine(); line.isValid(); line = layout->createLine()) {
				line.setLineWidth(textWidth());
				line.setPosition(QPointF(0., y));
				y += line.height();
			}
			layout->endLayout();
		}
		return *layout;
	}
	[[nodiscard]] int heightOf(int index) {
		if (_heights.size() != _lyrics.lines.size()) {
			_heights.assign(_lyrics.lines.size(), -1);
		}
		if (_heights[index] < 0) {
			_heights[index] = int(std::ceil(layoutOf(index).boundingRect().height()))
				+ style::ConvertScale(_big ? 26 : 24);
		}
		return _heights[index];
	}
	// Строка поверх фона; fill >= 0 заливает спетую часть белым слева направо с мягким краем.
	void drawLayout(
			QPainter &p,
			const QTextLayout &layout,
			QPointF origin,
			double fill,
			double sung,
			double unsung) const {
		const auto white = [](double alpha) {
			auto color = QColor(255, 255, 255);
			color.setAlphaF(std::clamp(alpha, 0., 1.));
			return color;
		};
		const auto position = fill * layout.text().size();
		const auto feather = style::ConvertScale(_big ? 16 : 10);
		for (auto i = 0; i != layout.lineCount(); ++i) {
			const auto line = layout.lineAt(i);
			const auto from = line.textStart();
			const auto till = from + line.textLength();
			if (fill < 0. || position >= till) {
				p.setPen(white(sung));
			} else if (position <= from) {
				p.setPen(white(unsung));
			} else {
				const auto index = int(position);
				const auto left = line.cursorToX(index);
				const auto right = line.cursorToX(std::min(index + 1, till));
				const auto x = origin.x() + left + (right - left) * (position - index);
				auto gradient = QLinearGradient(x - feather, 0., x + feather, 0.);
				gradient.setColorAt(0., white(sung));
				gradient.setColorAt(1., white(unsung));
				p.setPen(QPen(QBrush(gradient), 1.));
			}
			line.draw(&p, origin);
		}
	}
	// Центр строки index в координатах содержимого.
	[[nodiscard]] double centerOf(int index) {
		auto y = 0.;
		for (auto i = 0; i != index; ++i) {
			y += heightOf(i);
		}
		return y + heightOf(index) / 2.;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto rounded = QPainterPath();
		if (_big) {
			if (_background) {
				_background(p, e->rect());
			}
		} else {
			// Скругление нужно только подложке и затуханию, текст рисуется без маски.
			rounded.addRoundedRect(QRectF(rect()), 12, 12);
			p.save();
			p.setClipPath(rounded);
			if (_coverTint) {
				_backdrop.paint(p, rect(), rect());
			} else {
				p.fillRect(rect(), st::windowBgOver);
			}
			p.restore();
		}
		p.setClipRect(QRect(0, 0, width(), lyricsHeight()));
		if (!_lyrics.found) {
			p.setPen(_big ? QColor(255, 255, 255, 140) : st::windowSubTextFg->c);
			p.setFont(st::normalFont);
			p.drawText(QRect(0, 0, width(), lyricsHeight()), Qt::AlignCenter, _status);
			return;
		}
		const auto count = int(_lyrics.lines.size());
		auto top = _lyrics.synced ? (anchorY() - _scroll) : (12. - _plainScroll);
		p.setFont(lineFont());
		for (auto i = 0; i != count; ++i) {
			const auto lineHeight = heightOf(i);
			if (top + lineHeight >= -20 && top <= height() + 20) {
				const auto w = (_lyrics.synced && i < int(_weights.size()))
					? double(_weights[i])
					: 0.;
				// Дальние строки тусклее, уже спетые ещё тусклее, текущая белая.
				const auto distance = _lyrics.synced
					? std::abs(i - std::max(_current, 0))
					: 1;
				const auto past = _lyrics.synced && i < _current;
				const auto rest = !_lyrics.synced
					? 0.84
					: std::max(0.22, (past ? 0.34 : 0.44) - 0.04 * distance);
				const auto cy = top + lineHeight / 2.;
				auto edge = 1.;
				if (_big) {
					// Края текста растворяются в фоне плавно.
					const auto smooth = [](double t) {
						t = std::clamp(t, 0., 1.);
						return t * t * (3. - 2. * t);
					};
					edge = smooth(cy / (height() * 0.2))
						* smooth((height() - cy) / (height() * 0.28));
				}
				// Без масштабирования: текст с трансформацией не берётся из кэша глифов и тормозит.
				const auto origin = _big
					? QPointF(0., top + style::ConvertScale(13))
					: QPointF(style::ConvertScale(24), top + style::ConvertScale(12));
				drawLayout(
					p,
					layoutOf(i),
					origin,
					(_lyrics.synced && i == _current) ? _fill : -1.,
					(rest + (1. - rest) * w) * edge,
					(rest + (0.45 - rest) * w) * edge);
			}
			top += lineHeight;
			if (top > height() + 400) {
				break;
			}
		}
		if (_big) {
			return;
		}
		// Мягкое затухание сверху и снизу.
		p.setClipPath(rounded);
		auto fadeTop = QLinearGradient(0, 0, 0, 56);
		fadeTop.setColorAt(0., QColor(0, 0, 0, 140));
		fadeTop.setColorAt(1., QColor(0, 0, 0, 0));
		p.fillRect(QRect(0, 0, width(), 56), fadeTop);
		auto fadeBottom = QLinearGradient(0, height() - 56, 0, height());
		fadeBottom.setColorAt(0., QColor(0, 0, 0, 0));
		fadeBottom.setColorAt(1., QColor(0, 0, 0, 140));
		p.fillRect(QRect(0, height() - 56, width(), 56), fadeBottom);
	}

	int lyricsHeight() const {
		return height();
	}
	void layoutComments() {
		const auto shown = Settings::Bool(u"lyrics.comments"_q)
			&& _lyrics.found && _comments->validTrack() && _comments->avatarCount() > 0;
		_comments->setVisible(shown);
		if (!shown) return;
		const auto size = style::ConvertScale(32);
		const auto rowWidth = std::min(width(), style::ConvertScale(96));
		auto top = anchorY() - _scroll;
		if (_lyrics.synced && _current >= 0) {
			for (auto i = 0; i < _current; ++i) top += heightOf(i);
		}
		const auto lineHeight = _current >= 0 ? heightOf(_current) : size;
		_comments->setGeometry(std::max(0, width() - rowWidth - style::ConvertScale(8)),
			std::clamp(int(top + lineHeight / 2.) - size / 2, 0, std::max(0, height() - size)), rowWidth, size);
		_comments->raise();
	}
	const not_null<TimedComments*> _comments;
	Lyrics _lyrics;
	QString _status;
	QTimer _timer;
	QElapsedTimer _clock;
	std::vector<int> _heights;
	std::vector<std::unique_ptr<QTextLayout>> _layouts;
	CoverBackground _backdrop;
	Fn<void(QPainter&, QRect)> _background;
	std::vector<float> _weights;
	double _fill = 0.;
	qint64 _position = 0;
	qint64 _positionAt = 0;
	qint64 _last = 0;
	double _scroll = 0.;
	double _plainScroll = 0.;
	qint64 _manualUntil = 0;
	int _current = -1;
	bool _scrollReady = false;
	bool _coverTint = true;
	bool _playing = false;
	bool _big = false;

};

void LyricsView::updateTimer() {
	const auto animate = isVisible()
		&& QGuiApplication::applicationState() == Qt::ApplicationActive
		&& _playing
		&& _lyrics.found
		&& _lyrics.synced
		&& !_lyrics.lines.empty()
		&& !PowerSaving::On(PowerSaving::kAnimations);
	if (animate && !_timer.isActive()) {
		_last = _clock.elapsed();
		_timer.start();
	} else if (!animate) {
		_timer.stop();
	}
}

void LyricsView::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	updateTimer();
}

void LyricsView::hideEvent(QHideEvent *e) {
	_timer.stop();
	RpWidget::hideEvent(e);
}

void LyricsView::resizeEvent(QResizeEvent *e) {
	layoutComments();
	_heights.clear();
	_layouts.clear();
	RpWidget::resizeEvent(e);
}

constexpr auto kLyricsPanelWidth = 340;
constexpr auto kLyricsHeaderHeight = 56;

// Колонка справа от чата: название трека и синхронный текст.
[[nodiscard]] bool &ThirdColumnOpenedForLyrics();

class LyricsPanel final : public Ui::RpWidget {
public:
	LyricsPanel(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor)
	: RpWidget(parent.get())
	, _controller(controller)
	, _anchor(anchor)
	, _view(Ui::CreateChild<LyricsView>(this)) {
		Settings::PreloadRowIcons(&controller->session());
		_view->setCoverTint(Mokagram::Settings::Bool(u"lyrics.plasma"_q));
		_close = Mokagram::Plugins::MakeHeaderButton(this, true, [] {
			Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
		});

		// Левый край тянется, как у третьей колонки: ширина общая с ней.
		_resize = Ui::CreateChild<Ui::ResizeArea>(this);
		_resize->addMoveLeftCallback([=](int globalLeft) {
			if (const auto parent = parentWidget()) {
				controller->columns().setThirdColumnWidth(
					parent->mapToGlobal(QPoint(parent->width(), 0)).x()
						- globalLeft);
			}
		});
		_resize->addMoveFinishedCallback([=] {
			controller->columns().setThirdColumnWidth(std::clamp(
				controller->columns().thirdColumnWidth(),
				st::columnMinimalWidthThird,
				st::columnMaximalWidthThird));
			Core::App().saveSettingsDelayed();
		});

		parent->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::Resize;
		}) | rpl::on_next([=] {
			updateGeometryFromAnchor();
		}, lifetime());
		anchor->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::Resize || e->type() == QEvent::Move;
		}) | rpl::on_next([=] {
			updateGeometryFromAnchor();
		}, lifetime());
		controller->adaptive().changes(
		) | rpl::on_next([=] {
			if (!controller->adaptive().isThreeColumn()) {
				ThirdColumnOpenedForLyrics() = false;
				Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
				return;
			}
			updateGeometryFromAnchor();
		}, lifetime());
		controller->columns().thirdColumnWidthChanges(
		) | rpl::on_next([=](int) {
			updateGeometryFromAnchor();
		}, lifetime());
		anchor->shownValue(
		) | rpl::on_next([=](bool) {
			updateGeometryFromAnchor();
			refresh();
		}, lifetime());
		const auto weak = QPointer<LyricsPanel>(this);
		anchor->lifetime().add([=] {
			if (weak) {
				weak->deleteLater();
			}
		});

		Mokagram::Settings::Changed(
		) | rpl::on_next([=](const QString &key) {
			if (key == u"lyrics.plasma"_q) {
				_view->setCoverTint(Mokagram::Settings::Bool(key));
			} else if (key == u"lyrics.enabled"_q
				&& !Mokagram::Settings::Bool(key)) {
				deleteLater();
			}
		}, lifetime());

		const auto instance = Media::Player::instance();
		instance->updatedNotifier(
		) | rpl::on_next([=] {
			refresh();
		}, lifetime());
		QueueState().changes.events(
		) | rpl::on_next([=] {
			refresh();
		}, lifetime());
		CoversChanged() | rpl::on_next([=] {
			_view->updateCover();
			_view->update();
		}, lifetime());

		// Нажатие на строку перематывает трек на её время.
		_view->lineClicked(
		) | rpl::on_next([=](qint64 ms) {
			const auto state = instance->getState(AudioMsgId::Type::Song);
			const auto lengthMs = state.length * 1000
				/ std::max<int64>(state.frequency, 1);
			if (lengthMs <= 0) {
				return;
			}
			instance->startSeeking(AudioMsgId::Type::Song);
			instance->finishSeeking(
				AudioMsgId::Type::Song,
				std::clamp(float64(ms) / float64(lengthMs), 0., 1.));
		}, lifetime());

		updateGeometryFromAnchor();
		refresh();
		raise();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.fillRect(rect(), st::windowBg);
		p.fillRect(0, 0, st::lineWidth, height(), st::shadowFg);
		const auto left = 24;
		const auto available = width() - left - _close->width() - 16;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(left, 8, available, 22),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(_title, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, 30, available, 18),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(_artist, available));
	}
	void resizeEvent(QResizeEvent *e) override {
		_view->setGeometry(
			0,
			kLyricsHeaderHeight,
			width(),
			std::max(height() - kLyricsHeaderHeight, 0));
		_close->move(
			width() - _close->width() - 8,
			(kLyricsHeaderHeight - _close->height()) / 2);
		_resize->setGeometry(0, 0, st::historyResizeWidth, height());
		_resize->raise();
		RpWidget::resizeEvent(e);
	}

private:
	// Встаём ровно на место третьей колонки: во всю высоту и её ширины, чат
	// при этом сужается штатно. Если колонку показать нельзя, висим под плеером.
	void updateGeometryFromAnchor() {
		const auto parent = parentWidget();
		if (!parent || !_anchor) {
			return;
		}
		const auto layout = _controller->computeColumnLayout();
		const auto docked = _controller->adaptive().isThreeColumn()
			&& layout.thirdWidth > 0;
		_resize->setVisible(docked);
		if (docked) {
			setGeometry(
				parent->width() - layout.thirdWidth,
				0,
				layout.thirdWidth,
				parent->height());
		} else {
			const auto top = _anchor->mapTo(
				parent,
				QPoint(0, _anchor->height())).y();
			const auto width = std::min(kLyricsPanelWidth, parent->width() / 2);
			setGeometry(
				parent->width() - width,
				top,
				width,
				std::max(parent->height() - top, 0));
		}
		raise();
	}
	void refresh() {
		const auto track = PlayingTrack();
		const auto shown = _anchor
			&& _anchor->isVisible()
			&& !track.id.isEmpty();
		setVisible(shown);
		if (!shown) {
			return;
		}
		if (_title != track.title || _artist != track.artist) {
			_title = track.title;
			_artist = track.artist;
			update();
		}
		const auto instance = Media::Player::instance();
		const auto state = instance->getState(AudioMsgId::Type::Song);
		_view->setPosition(
			state.position * 1000 / std::max<int64>(state.frequency, 1),
			state.state == Media::Player::State::Playing
				|| state.state == Media::Player::State::Starting
				|| state.state == Media::Player::State::Resuming);
		if (_lyricsFor == track.id) {
			return;
		}
		_lyricsFor = track.id;
		_view->setCommentsTrack(QString());
		ResolveCommentsTrack(track, crl::guard(this, [=, id = track.id](QString sc) {
			if (_lyricsFor == id) _view->setCommentsTrack(sc);
		}));
		_view->setLoading();
		const auto id = track.id;
		FetchLyrics(
			id,
			track.title,
			track.artist,
			track.durationMs,
			crl::guard(this, [=](Lyrics lyrics) {
				if (_lyricsFor == id) {
					_view->setLyrics(std::move(lyrics));
				}
			}));
	}

	const not_null<Window::SessionController*> _controller;
	const QPointer<Ui::RpWidget> _anchor;
	const not_null<LyricsView*> _view;
	Ui::AbstractButton *_close = nullptr;
	Ui::ResizeArea *_resize = nullptr;
	QString _lyricsFor;
	QString _title;
	QString _artist;

};

[[nodiscard]] QPointer<LyricsPanel> &LyricsPanelInstance() {
	static auto result = QPointer<LyricsPanel>();
	return result;
}

// Третью колонку открыли ради панели текста: при закрытии панели закроем и её.
[[nodiscard]] bool &ThirdColumnOpenedForLyrics() {
	static auto result = false;
	return result;
}

constexpr auto kFullscreenSide = 56;

// Полноэкранный плеер: размытый цветной фон, крупный текст песни, тонкая полоса времени.
// Пункт навигации полноэкранного плеера: слева в столбце или пилюлей в ряду.
class NavItem final : public Ui::AbstractButton {
public:
	NavItem(QWidget *parent, const QString &text, bool centered)
	: AbstractButton(parent)
	, _text(text)
	, _centered(centered) {
		setPointerCursor(true);
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
	}

	void setActive(bool active) {
		if (_active != active) {
			const auto from = _fade.value(_active ? 1. : 0.);
			_active = active;
			if (anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations)) {
				_fade.stop();
				update();
			} else {
				_fade.start(
					[=] { update(); },
					from,
					_active ? 1. : 0.,
					kSwitchDuration,
					anim::easeOutCubic);
			}
		}
	}
	void setActiveColor(QColor color) {
		if (_activeColor != color) {
			_activeColor = color;
			update();
		}
	}
	void setCentered(bool centered) {
		if (_centered != centered) {
			_centered = centered;
			update();
		}
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		update();
	}

private:
	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		auto font = st::semiboldFont->f;
		font.setPixelSize(s(15));
		p.setFont(font);
		const auto inactive = isOver()
			? st::windowFg->c
			: st::windowSubTextFg->c;
		const auto active = _activeColor.isValid()
			? _activeColor
			: st::windowFg->c;
		p.setPen(anim::color(
			inactive,
			active,
			_fade.value(_active ? 1. : 0.)));
		const auto inner = rect().adjusted(s(16), 0, -s(16), 0);
		p.drawText(
			inner,
			(_centered ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter,
			QFontMetrics(font).elidedText(_text, Qt::ElideRight, inner.width()));
	}

	QString _text;
	QColor _activeColor;
	Ui::Animations::Simple _fade;
	bool _centered = false;
	bool _active = false;

};

// Переключатель из двух-трёх сегментов, как в окне музыки: пилюля с ползущим акцентом.
class Segmented final : public Ui::AbstractButton {
public:
	Segmented(QWidget *parent, std::vector<QString> labels, Fn<void(int)> chosen)
	: AbstractButton(parent)
	, _labels(std::move(labels)) {
		setPointerCursor(true);
		setClickedCallback([=] {
			const auto x = mapFromGlobal(QCursor::pos()).x();
			const auto index = std::clamp(
				x * int(_labels.size()) / std::max(width(), 1),
				0,
				int(_labels.size()) - 1);
			if (index != _index) {
				setIndex(index);
				chosen(index);
			}
		});
		paintRequest(
		) | rpl::on_next([=](QRect) {
			paint();
		}, lifetime());
	}

	void setIndex(int index) {
		if (_index != index) {
			_slide.start([=] { update(); }, _index, index, 220, anim::easeOutCubic);
			_index = index;
		}
	}

private:
	void paint() {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		const auto count = int(_labels.size());
		const auto inset = s(3);
		const auto segment = (width() - 2 * inset) / double(count);
		const auto at = _slide.value(_index);
		p.setBrush(st::windowActiveTextFg);
		const auto active = QRectF(
			inset + segment * at,
			inset,
			segment,
			height() - 2 * inset);
		p.drawRoundedRect(active, active.height() / 2., active.height() / 2.);
		auto font = st::semiboldFont->f;
		font.setPixelSize(s(15));
		p.setFont(font);
		for (auto i = 0; i != count; ++i) {
			const auto mix = std::clamp(1. - std::abs(at - i), 0., 1.);
			p.setPen(anim::color(st::windowSubTextFg, QColor(Qt::white), mix));
			p.drawText(
				QRectF(inset + segment * i, 0, segment, height()),
				Qt::AlignCenter,
				_labels[i]);
		}
	}

	std::vector<QString> _labels;
	int _index = 0;
	Ui::Animations::Simple _slide;

};

class SearchSkeleton final : public Ui::RpWidget {
public:
	explicit SearchSkeleton(QWidget *parent) : RpWidget(parent) {
		resize(width(), 5 * kRowHeight);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		if (!PowerSaving::On(PowerSaving::kAnimations)) {
			_started = crl::now();
			_animation.init([=](crl::time) {
				update();
				return !PowerSaving::On(PowerSaving::kAnimations);
			});
			_animation.start();
		}
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto base = st::windowSubTextFg->c;
		base.setAlphaF(0.16);
		const auto moving = _started && !PowerSaving::On(PowerSaving::kAnimations);
		const auto sweep = moving
			? std::fmod((crl::now() - _started) / 1400., 1.) * (width() + 160) - 80
			: -1000.;
		for (auto i = 0; i != 5; ++i) {
			const auto top = i * kRowHeight;
			auto shapes = QPainterPath();
			shapes.addRoundedRect(QRectF(20, top + (kRowHeight - kArtSize) / 2, kArtSize, kArtSize), 8, 8);
			const auto left = 20 + kArtSize + 16;
			const auto available = std::max(width() - left - 32, 0);
			shapes.addRoundedRect(QRectF(left, top + kRowHeight / 2 - 18, std::min(available, 180 - i * 14), 10), 5, 5);
			shapes.addRoundedRect(QRectF(left, top + kRowHeight / 2 + 5, std::min(available, 100), 8), 4, 4);
			p.fillPath(shapes, base);
			if (moving) {
				p.save();
				p.setClipPath(shapes);
				auto glow = QLinearGradient(sweep - 70, 0, sweep + 70, 0);
				auto color = st::windowFg->c;
				color.setAlphaF(0.);
				glow.setColorAt(0., color);
				color.setAlphaF(0.10);
				glow.setColorAt(0.5, color);
				color.setAlphaF(0.);
				glow.setColorAt(1., color);
				p.fillRect(rect(), glow);
				p.restore();
			}
		}
	}

private:
	Ui::Animations::Basic _animation;
	crl::time _started = 0;
};

class SearchIndicator final : public QWidget {
public:
	explicit SearchIndicator(QWidget *parent) : QWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		_timer.setInterval(40);
		connect(&_timer, &QTimer::timeout, this, [=] {
			if (PowerSaving::On(PowerSaving::kAnimations)) {
				_timer.stop();
			}
			update();
		});
		connect(qApp, &QGuiApplication::applicationStateChanged, this, [=] {
			setLoading(_loading);
		});
	}

	void setLoading(bool loading) {
		_loading = loading;
		if (_loading && isVisible()
			&& QGuiApplication::applicationState() == Qt::ApplicationActive
			&& !PowerSaving::On(PowerSaving::kAnimations)) {
			_clock.start();
			_timer.start();
		} else {
			_timer.stop();
		}
		update();
	}

protected:
	void showEvent(QShowEvent *event) override {
		QWidget::showEvent(event);
		setLoading(_loading);
	}
	void hideEvent(QHideEvent *event) override {
		_timer.stop();
		QWidget::hideEvent(event);
	}
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(st::windowSubTextFg->c, style::ConvertScale(2), Qt::SolidLine, Qt::RoundCap));
		const auto unit = width() / 20.;
		p.scale(unit, unit);
		if (_loading) {
			const auto angle = PowerSaving::On(PowerSaving::kAnimations)
				? 0 : int((_clock.elapsed() % 1000) * 360 / 1000);
			p.drawArc(QRectF(3, 3, 14, 14), -angle * 16, 260 * 16);
		} else {
			p.drawEllipse(QRectF(3, 3, 10, 10));
			p.drawLine(QPointF(12, 12), QPointF(17, 17));
		}
	}

private:
	QTimer _timer;
	QElapsedTimer _clock;
	bool _loading = false;
};

constexpr auto kBarHeight = 108;
constexpr auto kBarMaxWidth = 1040;
constexpr auto kBarCover = 72;
constexpr auto kBarRadius = 40;
constexpr auto kHomeTrendingSection = 3;
constexpr auto kSeekFrameInterval = 33;
constexpr auto kBackdropFrameInterval = 100;

// Акцент обложки, поднятый до читаемой светлоты; белый без него.
[[nodiscard]] QColor LiftedAccent(QColor accent) {
	constexpr auto kMinLightness = 0.62;
	if (!accent.isValid()) {
		return QColor(255, 255, 255);
	}
	const auto hsl = accent.toHsl();
	return QColor::fromHslF(
		hsl.hslHueF(),
		hsl.hslSaturationF(),
		std::max(double(hsl.lightnessF()), kMinLightness));
}

class FullscreenPlayer final : public Ui::RpWidget {
public:
	FullscreenPlayer(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		bool openHome)
	: RpWidget(parent.get())
	, _controller(controller)
	, _view(Ui::CreateChild<LyricsView>(this))
	, _tabScroll(Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea))
	, _playbackBar(Ui::CreateChild<Ui::RpWidget>(this))
	, _seek(Ui::CreateChild<SeekBar>(_playbackBar, [=] { return playbackBarColor(); }))
	, _heart(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Add, 44))
	, _collapse(Ui::CreateChild<RoundGlyph>(this, Glyph::Collapse, 44))
	, _filter(Ui::CreateChild<RoundGlyph>(this, Glyph::Filter, 44))
	, _queueToggle(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Queue, 44))
	, _volume(Ui::CreateChild<VolumeControl>(_playbackBar))
	, _tabTransition(Ui::CreateChild<Ui::RpWidget>(this))
	, _tab(openHome ? Tab::Home : Tab::Now)
	, _shuffle(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Shuffle, 44))
	, _previous(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Previous, 52))
	, _play(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Play, 52))
	, _next(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Next, 52))
	, _repeat(Ui::CreateChild<RoundGlyph>(_playbackBar, Glyph::Repeat, 44))
	, _canvas(this, [=] {
		update(computeLayout().cover);
	}) {
		setFocusPolicy(Qt::TabFocus);
		setMouseTracking(true);
		_view->setBig(true);
		_view->setBackground([=](QPainter &p, QRect area) {
			paintBackgroundPart(p, area, _view->pos());
		});
		_view->setAttribute(Qt::WA_OpaquePaintEvent);
		_seek->setThin(true);
		_seek->enableTimePreview();
		_seek->setExpressive(true, false);
		_homeBack = Ui::CreateChild<NavItem>(this, u"‹  "_q + QString::fromUtf8(Mokagram::Lang::Tr("Назад")), false);
		_homeBack->setClickedCallback(crl::guard(this, [=] {
			if (_homeReturnToArtist) {
				openHomeArtist(_homeArtistSeed);
				return;
			}
			_homeAlbumOpen = false;
			_homeArtistOpen = false;
			_nav[1]->setActiveColor(Qt::white);
			++_homeAlbumGeneration;
			relayout();
			rebuildTab();
			_tabScroll->scrollToY(_homeAlbumReturnScroll);
		}));
		_homeBack->hide();
		_homeArtistBar = Ui::CreateChild<Ui::RpWidget>(_tabScroll->viewport());
		_homeArtistBarOpacity = new QGraphicsOpacityEffect(_homeArtistBar);
		_homeArtistBarOpacity->setOpacity(0.);
		_homeArtistBar->setGraphicsEffect(_homeArtistBarOpacity);
		_homeArtistBarPlay = Ui::CreateChild<Ui::RoundButton>(
			_homeArtistBar,
			rpl::single(PlaylistText("Воспроизвести")),
			BoxParts::PillActive());
		_homeArtistBarPlay->setFocusPolicy(Qt::TabFocus);
		_homeArtistBarPlay->setClickedCallback([=] { playHomeArtist(); });
		_homeArtistBar->paintOn([=](QPainter &p) {
			const auto s = [](int value) { return style::ConvertScale(value); };
			auto hq = PainterHighQualityEnabler(p);
			p.fillRect(_homeArtistBar->rect(), st::boxBg);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg);
			const auto inner = _homeArtistBar->rect().adjusted(s(14), 0, -s(14), 0);
			p.drawRoundedRect(inner, s(22), s(22));
			const auto text = QRect(
				inner.left() + s(16), 0,
				std::max(_homeArtistBarPlay->x() - inner.left() - s(28), 0),
				_homeArtistBar->height());
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFg);
			p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
				st::semiboldFont->elided(_homeArtistSeed.artist, text.width()));
		});
		_homeArtistBar->hide();
		AttachCommentsToPlayer(controller, _playbackBar, _seek, false);
		_filter->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Звуковой фильтр")));
		_filter->setClickedCallback([=] {
			_filterMenu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
			for (const auto &entry : Mokagram::Settings::Entries()) {
				if (entry.key != u"player.audio_filter"_q) {
					continue;
				}
				const auto current = Mokagram::Settings::Int(entry.key);
				const auto icons = std::array{
					&st::menuIconSoundOn,
					&st::menuIconBoosts,
					&st::menuIconStartStream,
					&st::menuIconSettings,
					&st::menuIconSoundSelect,
					&st::menuIconPalette,
					&st::menuIconPhone,
				};
				for (auto i = 0; i != entry.choices.size(); ++i) {
					const auto label = entry.choices[i]
						+ (i == current ? u"  ✓"_q : QString());
					_filterMenu->addAction(label, [=] {
						Mokagram::Settings::SetInt(u"player.audio_filter"_q, i);
					}, (i < icons.size()) ? icons[i] : nullptr);
				}
				break;
			}
			_filterMenu->popup(_filter->mapToGlobal(QPoint(0, _filter->height())));
		});
		_queueToggle->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Очередь")));
		_queueToggle->setClickedCallback([=] { setTab(Tab::Queue); });
		const auto s = [](int v) { return style::ConvertScale(v); };
		_shuffle->resize(s(36), s(36));
		_previous->resize(s(40), s(40));
		_play->resize(s(44), s(44));
		_next->resize(s(40), s(40));
		_repeat->resize(s(36), s(36));
		_seek->resize(_seek->width(), s(20));
		_tabContent = _tabScroll->setOwnedWidget(
			object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
				_tabScroll,
				object_ptr<Ui::VerticalLayout>(_tabScroll),
				QMargins()));
		_tabList = _tabContent->entity();
		_playbackBar->setMouseTracking(true);
		_playbackBar->paintOn([=](QPainter &p) {
			if (_tab == Tab::Home) {
				const auto hq = PainterHighQualityEnabler(p);
				p.translate(-_playbackBar->pos());
				const auto layout = computeLayout();
				paintPlaybackBarBackground(p, layout);
				paintPlaybackBarDetails(p, layout);
			}
		});
		_tabTransition->setAttribute(Qt::WA_TransparentForMouseEvents);
		_tabTransition->hide();
		_tabTransition->paintRequest(
		) | rpl::on_next([=] {
			if (_tabTransitionNow.isNull()) {
				return;
			}
			auto p = QPainter(_tabTransition);
			const auto progress = _tabTransitionAnimation.value(1.);
			const auto shift = style::ConvertScale(28) * _tabTransitionDirection;
			p.drawPixmap(0, 0, _tabTransitionNow);
			p.setOpacity(1. - progress);
			p.drawPixmap(
				int(base::SafeRound(-shift * progress)),
				0,
				_tabTransitionWas);
			p.setOpacity(progress);
			p.drawPixmap(
				int(base::SafeRound(shift * (1. - progress))),
				0,
				_tabTransitionNow);
		}, lifetime());
		_homeMoreRetryTimer.setSingleShot(true);
		connect(&_homeMoreRetryTimer, &QTimer::timeout, this, [=] {
			scheduleHomeMoreIfNeeded();
		});
		_tabScroll->scrollTopChanges() | rpl::on_next([=](int top) {
			if (_tab == Tab::Home && _homeAlbumOpen) {
				updateHomeArtistBar();
				update();
			}
			if (!_rebuildingTab) {
				scheduleHomeMoreIfNeeded();
			}
		}, lifetime());
		const auto names = std::array<const char*, 6>{
			"Сейчас играет", "Главная", "Поиск", "Библиотека", "Плейлисты", "Очередь" };
		for (auto i = 0; i != 6; ++i) {
			const auto item = Ui::CreateChild<NavItem>(
				this,
				QString::fromUtf8(Mokagram::Lang::Tr(names[i])),
				false);
			item->setClickedCallback([=] { setTab(Tab(i)); });
			_nav[i] = item;
		}
		_nav[int(_tab)]->setActive(true);
		Account::Changes() | rpl::on_next([=] {
			if (_tab == Tab::Library) {
				syncLibrary();
			}
			++_homeSoundCloudGeneration;
			_homeSoundCloudLoading = false;
			_homeSoundCloudLoaded = false;
			_homeSoundCloudError.clear();
			_homeTrending.clear();
			_homeWeeklyWave = {};
			_homeDailyDrops = {};
			if (_tab == Tab::Home && !_homeAlbumOpen) {
				_homeSection = 0;
				rebuildTab();
			}
		}, lifetime());
		Account::LikeWriteErrors() | rpl::on_next([=](QString error) {
			_controller->showToast(error);
		}, lifetime());
		_searchField = Ui::CreateChild<QLineEdit>(this);
		_searchField->setFrame(false);
		_searchField->installEventFilter(this);
		_searchField->setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Поиск музыки")));
		_searchIndicator = new SearchIndicator(_searchField);
		_searchRetry = new QPushButton(QString::fromUtf8(Mokagram::Lang::Tr("Повторить")), this);
		_searchRetry->setCursor(Qt::PointingHandCursor);
		_searchRetry->setStyleSheet(u"QPushButton { background: %1; color: %2; border: 0; border-radius: 8px; padding: 0 12px; } QPushButton:hover, QPushButton:focus { background: %3; }"_q
			.arg(st::windowBgOver->c.name(), st::windowFg->c.name(), st::windowActiveTextFg->c.name()));
		_searchRetry->hide();
		connect(_searchRetry, &QPushButton::clicked, this, [=] { runSearch(); });
		_searchField->setPlaceholderText(
			QString::fromUtf8(Mokagram::Lang::Tr("Поиск на SoundCloud")));
		_searchField->hide();
		_searchTimer.setSingleShot(true);
		_searchTimer.setInterval(kTypingDelay);
		connect(&_searchTimer, &QTimer::timeout, this, [=] { runSearch(); });
		connect(_searchField, &QLineEdit::textChanged, this, [=] {
			++_searchGen;
			_searchIndicator->setLoading(false);
			_searchPending = !_searchField->text().trimmed().isEmpty();
			_searchFailed = false;
			_openedAlbum = false;
			_searchTracks.clear();
			_albums.clear();
			_searchStatus = _searchField->text().trimmed().isEmpty()
				? QString() : QString::fromUtf8(Mokagram::Lang::Tr("Ищу…"));
			if (_tab == Tab::Search) rebuildTab();
			_searchTimer.start();
		});
		_chooseSearchChat = Ui::CreateChild<Ui::RoundButton>(this,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Выбрать чат"))),
			st::defaultLightButton);
		_chooseSearchChat->hide();
		_chooseSearchChat->setClickedCallback([=] {
			ShowMusicSourceBox(
				_controller,
				crl::guard(this, [=](not_null<Data::Thread*> thread) {
					_searchChat = thread->owningHistory()->peer->id;
					_chooseSearchChat->setText(rpl::single(
						QString::fromUtf8(Mokagram::Lang::Tr("Чат: %1"))
						.arg(thread->owningHistory()->peer->name())));
					runSearch();
					return true;
				}));
		});
		_sourceSwitch = Ui::CreateChild<Segmented>(
			this,
			std::vector<QString>{
				u"SoundCloud"_q,
				QString::fromUtf8(Mokagram::Lang::Tr("Из чатов")) },
			[=](int index) {
				_searchChats = (index == 1);
				_openedAlbum = false;
				_searchField->setPlaceholderText(QString::fromUtf8(Mokagram::Lang::Tr(
					_searchChats ? "Поиск по чатам" : "Поиск на SoundCloud")));
				if (_searchChats) {
					_searchKind = 0;
					_kindSwitch->setIndex(0);
				}
				relayout();
				runSearch();
			});
		_kindSwitch = Ui::CreateChild<Segmented>(
			this,
			std::vector<QString>{
				QString::fromUtf8(Mokagram::Lang::Tr("Треки")),
				QString::fromUtf8(Mokagram::Lang::Tr("Альбомы")) },
			[=](int index) {
				_searchKind = index;
				_openedAlbum = false;
				runSearch();
			});
		_sourceSwitch->hide();
		_kindSwitch->hide();
		QueueState().changes.events(
		) | rpl::on_next([=] {
			if (showsQueue()) {
				rebuildTab();
			}
		}, lifetime());
		for (const auto button : { _heart, _collapse, _filter, _queueToggle, _shuffle, _previous, _play, _next, _repeat }) {
			button->setOverlay(true);
		}
		for (const auto button : { _heart, _queueToggle, _shuffle, _previous, _play, _next, _repeat }) {
			button->setIsland(true);
		}
		_heart->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Нравится")));
		_collapse->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Свернуть")));
		_shuffle->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Перемешать")));
		_repeat->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Повтор")));

		const auto instance = Media::Player::instance();
		_collapse->setClickedCallback([=] { close(); });
		_heart->setClickedCallback([=] {
			const auto track = PlayingTrack();
			if (!track.id.isEmpty()) {
				SetLiked(track, !IsLiked(track.id));
			}
		});
		const auto step = [=](int delta) {
			if (OwnsTrack(instance->current(AudioMsgId::Type::Song))) {
				Step(delta);
			} else if (delta > 0) {
				instance->next(AudioMsgId::Type::Song);
			} else {
				instance->previous(AudioMsgId::Type::Song);
			}
		};
		_previous->setClickedCallback([=] { step(-1); });
		_next->setClickedCallback([=] { step(1); });
		_play->setClickedCallback([=] {
			instance->playPause(AudioMsgId::Type::Song);
		});
		_shuffle->setClickedCallback([=] {
			auto &settings = Core::App().settings();
			settings.setPlayerOrderMode(
				settings.playerOrderMode() == Media::OrderMode::Shuffle
					? Media::OrderMode::Default
					: Media::OrderMode::Shuffle);
			Core::App().saveSettingsDelayed();
		});
		_repeat->setClickedCallback([=] {
			auto &settings = Core::App().settings();
			const auto mode = settings.playerRepeatMode();
			settings.setPlayerRepeatMode(mode == Media::RepeatMode::None
				? Media::RepeatMode::All
				: mode == Media::RepeatMode::All
				? Media::RepeatMode::One
				: Media::RepeatMode::None);
			Core::App().saveSettingsDelayed();
		});
		Core::App().settings().playerOrderModeValue(
		) | rpl::on_next([=] { _shuffle->update(); }, lifetime());
		Core::App().settings().playerRepeatModeValue(
		) | rpl::on_next([=] { _repeat->update(); }, lifetime());
		const auto seek = [=](float64 progress) {
			_view->followPlayback();
			instance->startSeeking(AudioMsgId::Type::Song);
			instance->finishSeeking(AudioMsgId::Type::Song, std::clamp(progress, 0., 1.));
		};
		_seek->sought() | rpl::on_next(seek, lifetime());
		_view->lineClicked() | rpl::on_next([=](qint64 ms) {
			const auto state = instance->getState(AudioMsgId::Type::Song);
			const auto lengthMs = state.length * 1000
				/ std::max<int64>(state.frequency, 1);
			if (lengthMs > 0) {
				seek(float64(ms) / float64(lengthMs));
			}
		}, lifetime());

		parent->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::Resize;
		}) | rpl::on_next([=] {
			setGeometry(parentWidget()->rect());
		}, lifetime());
		instance->updatedNotifier(
		) | rpl::on_next([=] { refresh(); }, lifetime());
		QueueState().changes.events(
		) | rpl::on_next([=] { refresh(); }, lifetime());
		CoversChanged() | rpl::on_next([=] { refresh(); }, lifetime());
		RecentChanged() | rpl::on_next([=] {
			if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
				crl::on_main(this, [=] {
					if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
						rebuildTab();
					}
				});
			}
		}, lifetime());
		PlaylistsChanged() | rpl::on_next([=] {
			if (_tab == Tab::Playlists || (_tab == Tab::Home && _homeArtistOpen)) {
				QTimer::singleShot(0, this, [=] {
					if (_tab == Tab::Playlists || (_tab == Tab::Home && _homeArtistOpen)) {
						rebuildTab();
					}
				});
			}
		}, lifetime());
		LikesChanged() | rpl::on_next([=] {
			refresh();
			if (_tab == Tab::Library || _tab == Tab::Home) {
				rebuildTab();
			}
		}, lifetime());
		MotionChanges() | rpl::on_next([=] {
			_motionFor.clear();
			_motionCover = nullptr;
			_spotifyCanvas = nullptr;
			refresh();
		}, lifetime());
		LyricsAvailabilityChanges() | rpl::on_next([=] { relayout(); }, lifetime());
		Mokagram::Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (key == u"lyrics.comments"_q) relayout();
			if (key == u"player.hide_trending"_q) {
				const auto closeTrending = (_homeSection == kHomeTrendingSection)
					&& Mokagram::Settings::Bool(key);
				if (closeTrending) {
					_homeSection = 0;
					_homeAlbumReturnScroll = _homeFeedScroll;
				}
				if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
					rebuildTab();
					if (closeTrending) {
						_tabScroll->scrollToY(_homeFeedScroll);
					}
				}
				return;
			}
			if (key.startsWith(u"player."_q)) {
				if (key == u"player.motion_covers"_q || key == u"player.spotify_canvas"_q) {
					_motionFor.clear();
					_motionCover = nullptr;
					_spotifyCanvas = nullptr;
				}
				refresh();
				updateAmbientTimer();
				repaintBackdrop();
			}
		}, lifetime());
		connect(&_ambientTimer, &QTimer::timeout, this, [=] {
			const auto now = crl::now();
			const auto elapsed = std::min((now - _ambientAt) / 1000., 0.1);
			_ambientAt = now;
			if (animatesBackdrop()) {
				repaintAnimatedBackdrop(now);
			}
			if (_seek->waveAnimating()) {
				_seek->advanceWave(elapsed);
			}
		});
		_seek->waveAnimationChanges(
		) | rpl::on_next([=] { updateAmbientTimer(); }, lifetime());
		connect(qApp, &QGuiApplication::applicationStateChanged, this, [=] {
			updateAmbientTimer();
		});

		PowerSaving::Changes(
		) | rpl::on_next([=] {
			updateAmbientTimer();
			repaintBackdrop();
		}, lifetime());

		setGeometry(parent->rect());
		refresh();
		rebuildTab();
		show();
		raise();
		setFocus();

		// Появление: плавное проявление всего экрана.
		const auto effect = new QGraphicsOpacityEffect(this);
		effect->setOpacity(0.);
		setGraphicsEffect(effect);
		_fade.start([=] {
			const auto value = _fade.value(_closing ? 0. : 1.);
			if (graphicsEffect() == effect) {
				effect->setOpacity(value);
			}
			if (!_fade.animating()) {
				if (_closing) {
					deleteLater();
				} else {
					setGraphicsEffect(nullptr);
				}
			}
		}, 0., 1., 180, anim::easeOutCubic);
	}

	[[nodiscard]] bool canReuse(
			not_null<Window::SessionController*> controller) const {
		return !_closing && _controller == controller;
	}

	void showHome() {
		if (_tab != Tab::Home) {
			setTab(Tab::Home);
		} else if (_homeSection || _homeAlbumOpen) {
			resetHomeNavigation();
			relayout();
			rebuildTab();
			_tabScroll->scrollToY(0);
		}
	}

	void close() {
		if (_closing) {
			return;
		}
		_closing = true;
		_ambientTimer.stop();
		if (_cancelMotion) _cancelMotion();
		_motionCover = nullptr;
		_spotifyCanvas = nullptr;
		_canvas.refresh({}, {}, {}, false);
		auto effect = new QGraphicsOpacityEffect(this);
		setGraphicsEffect(effect);
		_fade.start([=] {
			effect->setOpacity(_fade.value(0.));
			if (!_fade.animating()) {
				deleteLater();
			}
		}, 1., 0., 180, anim::easeOutCubic);
	}
	~FullscreenPlayer() override {
		if (_cancelMotion) _cancelMotion();
	}

protected:
	void showEvent(QShowEvent *event) override {
		RpWidget::showEvent(event);
		updateAmbientTimer();
	}
	void hideEvent(QHideEvent *event) override {
		_ambientTimer.stop();
		_preparedBackdrop = QImage();
		RpWidget::hideEvent(event);
	}
	bool eventFilter(QObject *watched, QEvent *event) override {
		if (_tab == Tab::Search && event->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event);
			if (watched == _searchField && key->key() == Qt::Key_Escape) {
				if (!_searchField->text().isEmpty()) _searchField->clear();
				else _searchField->clearFocus();
				return true;
			}
			const auto current = std::find(_searchResultWidgets.begin(), _searchResultWidgets.end(), watched);
			if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
				if (watched == _searchField || current != _searchResultWidgets.end()) {
					if (_searchResultWidgets.empty()) return false;
					const auto index = current == _searchResultWidgets.end() ? -1 : int(current - _searchResultWidgets.begin());
					const auto next = key->key() == Qt::Key_Down ? index + 1 : index - 1;
					if (next < 0) _searchField->setFocus();
					else if (next < int(_searchResultWidgets.size())) {
						auto widget = _searchResultWidgets[next];
						widget->setFocus();
						_tabScroll->scrollToY(widget->y(), widget->y() + widget->height());
					}
					return true;
				}
			}
			if (current != _searchResultWidgets.end() && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
				auto press = QKeyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
				auto release = QKeyEvent(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
				QCoreApplication::sendEvent(watched, &press);
				QCoreApplication::sendEvent(watched, &release);
				return true;
			}
		}
		return RpWidget::eventFilter(watched, event);
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (e->key() == Qt::Key_Escape) {
			close();
		} else {
			RpWidget::keyPressEvent(e);
		}
	}
	void mousePressEvent(QMouseEvent *e) override {
		setFocus();
		if (e->button() != Qt::LeftButton) {
			return;
		}
		const auto hover = infoAt(e->pos());
		if (hover == InfoHover::None) {
			return;
		}
		if (hover == InfoHover::BarCover) {
			openNowPlayingFromBar();
			return;
		}
		if (hover == InfoHover::Cover) {
			setHover(InfoHover::None);
			const auto layout = computeLayout();
			if (layout.barCover.isEmpty() || anim::Disabled()
				|| PowerSaving::On(PowerSaving::kAnimations)) {
				setTab(Tab::Home);
			} else if (!_coverClosing.animating()) {
				_coverClosing.start([=] {
					update();
					if (!_coverClosing.animating() && _tab == Tab::Now) {
						setTab(Tab::Home);
						_plainAnim.stop();
						auto effect = new QGraphicsOpacityEffect(_tabScroll);
						effect->setOpacity(0.);
						_tabScroll->setGraphicsEffect(effect);
						_homeEntrance.start([=] {
							effect->setOpacity(_homeEntrance.value(1.));
							if (!_homeEntrance.animating()) {
								_tabScroll->setGraphicsEffect(nullptr);
							}
						}, 0., 1., kSwitchDuration, anim::easeOutCubic);
					}
				}, 0., 1., 200, anim::easeOutQuint);
			}
			return;
		}
		const auto track = PlayingTrack();
		const auto artist = track.artist.trimmed();
		if (hover == InfoHover::Title || hover == InfoHover::BarTitle) {
			setHover(InfoHover::None);
			setTab(Tab::Home);
			openHomeTrack(track);
		} else if (!artist.isEmpty()) {
			setHover(InfoHover::None);
			setTab(Tab::Home);
			openHomeArtist(track);
		}
	}
	void contextMenuEvent(QContextMenuEvent *e) override {
		const auto layout = computeLayout();
		const auto point = e->pos();
		if ((_tab == Tab::Now && layout.cover.contains(point))
			|| layout.barCover.contains(point)
			|| barInfoRect(layout).contains(point)) {
			ShowCurrentTrackMenu(_controller, e->globalPos(), this);
			e->accept();
		} else {
			RpWidget::contextMenuEvent(e);
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		setHover(infoAt(e->pos()));
	}
	void leaveEventHook(QEvent *e) override {
		setHover(InfoHover::None);
	}
	enum class InfoHover { None, Title, Artist, Cover, BarCover, BarTitle, BarArtist };
	[[nodiscard]] QFont titleFont() const {
		auto result = st::semiboldFont->f;
		result.setPixelSize(style::ConvertScale(22));
		result.setWeight(QFont::Bold);
		return result;
	}
	[[nodiscard]] QFont artistFont() const {
		auto result = st::normalFont->f;
		result.setPixelSize(style::ConvertScale(15));
		return result;
	}
	void setHover(InfoHover hover) {
		if (_hover != hover) {
			_hover = hover;
			setCursor((hover == InfoHover::None)
				? style::cur_default
				: style::cur_pointer);
			const auto layout = computeLayout();
			update(layout.info);
			update(layout.bar);
		}
	}
	enum class Tab { Now, Home, Search, Library, Playlists, Queue };
	struct Layout {
		bool verticalNav = false;
		bool showRight = false;
		bool compact = false;
		bool ultra = false;
		bool barShown = true;
		QRect nav;
		QRect center;
		QRect right;
		QRect bar;
		int scrollTop = 0;
		QRect cover;
		QRect info;
		QRect lyrics;
		QRect barCover;
		QRect barInfo;
		QRect seek;
		QRect timeNow;
		QRect timeTotal;
		QRect volume;
		QPoint heart;
		QPoint queue;
		QPoint collapse;
		int controlsLeft = 0;
		int controlsWidth = 0;
		int controlsTop = 0;
	};
	[[nodiscard]] bool showsQueue() const {
		return _tab == Tab::Queue || (_tab == Tab::Now && _lyricsMissing);
	}
	[[nodiscard]] Layout computeLayout() const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		auto result = Layout();
		const auto margin = s(width() < s(560) ? 8 : 16);
		const auto gap = s(12);
		const auto pad = s(16);
		const auto now = (_tab == Tab::Now);
		result.barShown = true;
		// Толстая капсула как у островка: по центру, не на всю ширину.
		const auto barHeight = s(kBarHeight);
		const auto barWidth = std::max(
			std::min(width() - 2 * margin, s(kBarMaxWidth)),
			0);
		result.bar = QRect(
			(width() - barWidth) / 2,
			height() - margin - barHeight,
			barWidth,
			barHeight);

		// Сверху ряд вкладок по центру экрана, справа кнопка «свернуть».
		const auto reserve = (_tab == Tab::Home && _homeAlbumOpen)
			? s(168)
			: margin + _collapse->width() + s(8);
		const auto navWidth = std::clamp(width() - 2 * reserve, 0, s(900));
		result.verticalNav = width() < s(560);
		result.nav = QRect((width() - navWidth) / 2, margin, navWidth, s(result.verticalNav ? 80 : 40));
		result.collapse = QPoint(
			width() - margin - _collapse->width(),
			margin + (s(40) - _collapse->height()) / 2);
		const auto top = result.nav.y() + result.nav.height() + gap;
		const auto areaBottom = result.bar.y() - gap;
		const auto areaHeight = std::max(areaBottom - top, 0);

		if (now) {
			// Обложка слева, текст песни по центру остального экрана.
			result.showRight = width() >= s(800);
			auto lyricsLeft = margin;
			if (result.showRight) {
				const auto colWidth = std::clamp(int(width() * 0.29), s(190), s(460));
				result.right = QRect(margin, top, colWidth, areaHeight);
				const auto side = std::max(std::min({
					colWidth - 2 * pad,
					std::max(areaHeight - s(16), 0) * 4 / 5,
					s(420) }), 0);
				const auto coverHeight = side * 5 / 4;
				const auto y = top + std::max((areaHeight - coverHeight) / 2, 0);
				result.cover = QRect(margin + (colWidth - side) / 2, y, side, coverHeight);
				const auto infoInset = std::min(s(24), side / 4);
				result.info = QRect(
					result.cover.x() + infoInset,
					result.cover.y() + std::max(coverHeight - s(76), 0),
					std::max(side - 2 * infoInset, 0),
					std::min(s(56), coverHeight));
				lyricsLeft = margin + colWidth + s(32);
			}
			result.lyrics = QRect(
				lyricsLeft,
				top,
				std::max(width() - margin - lyricsLeft, 0),
				areaHeight);
			if (showsQueue()) {
				result.center = result.lyrics;
				result.scrollTop = result.center.y();
			}
		} else {
			const auto contentWidth = std::max(width() - ((_tab == Tab::Home && _homeArtistOpen) ? 0 : 2 * margin), 0);
			result.center = QRect((width() - contentWidth) / 2, top, contentWidth, areaHeight);
			result.scrollTop = result.center.y() + ((_tab == Tab::Home || _tab == Tab::Library || _tab == Tab::Queue || _tab == Tab::Playlists)
				? 0
				: (_tab == Tab::Search)
				? ((contentWidth < s(500)) ? s(172) : s(124))
				: s(72));
		}

		// Внизу: постоянная полоса управления.
		const auto &bar = result.bar;
		// Обложка концентрична капсуле: одинаковый отступ сверху, снизу и слева.
		const auto barInset = (bar.height() - s(kBarCover)) / 2;
		result.ultra = bar.width() < s(560);
		result.compact = bar.width() < s(900);
		const auto volumeWidth = s(28) + s(100);
		const auto leftWidth = result.ultra
			? 0
			: result.compact
			? barInset + s(kBarCover) + pad / 2
			: std::clamp(int(bar.width() * 0.3), s(280), s(380));
		const auto rightWidth = pad + _queueToggle->width() + s(2) + _heart->width() + pad
			+ (result.compact ? 0 : volumeWidth + s(12));
		const auto sideWidth = std::max(leftWidth, rightWidth);
		const auto centerWidth = std::max(bar.width() - (result.ultra ? rightWidth + pad : 2 * sideWidth), 0);
		const auto centerLeft = result.ultra
			? bar.x() + pad
			: bar.x() + (bar.width() - centerWidth) / 2;
		result.controlsLeft = centerLeft;
		result.controlsWidth = centerWidth;
		result.controlsTop = bar.y()
			+ (bar.height() - _play->height() - _seek->height()) / 2;
		const auto seekTop = result.controlsTop + _play->height();
		const auto rowWidth = std::max(std::min(
			result.ultra ? bar.width() - 2 * pad : centerWidth - s(16), s(568)), 0);
		const auto rowLeft = result.ultra
			? bar.x() + (bar.width() - rowWidth) / 2
			: centerLeft + (centerWidth - rowWidth) / 2;
		const auto timeWidth = s(44);
		result.timeNow = QRect(rowLeft, seekTop, timeWidth - s(6), _seek->height());
		result.seek = QRect(
			rowLeft + timeWidth,
			seekTop,
			std::max(rowWidth - 2 * timeWidth, 0),
			_seek->height());
		result.timeTotal = QRect(
			rowLeft + rowWidth - timeWidth + s(6),
			seekTop,
			timeWidth - s(6),
			_seek->height());
		result.queue = QPoint(
			bar.x() + bar.width() - barInset - _queueToggle->width(),
			result.ultra ? result.controlsTop + (_play->height() - _queueToggle->height()) / 2
				: bar.y() + (bar.height() - _queueToggle->height()) / 2);
		result.heart = QPoint(
			result.queue.x() - s(2) - _heart->width(),
			result.ultra ? result.controlsTop + (_play->height() - _heart->height()) / 2
				: bar.y() + (bar.height() - _heart->height()) / 2);
		if (!result.compact) {
			result.volume = QRect(
				result.heart.x() - s(12) - volumeWidth,
				bar.y() + (bar.height() - _volume->height()) / 2,
				volumeWidth,
				_volume->height());
		}
		if (!result.ultra) {
			result.barCover = QRect(
				bar.x() + barInset,
				bar.y() + barInset,
				s(kBarCover),
				s(kBarCover));
			result.barInfo = QRect(
				result.barCover.x() + s(kBarCover) + s(14),
				result.barCover.y(),
				std::max(leftWidth - barInset - s(kBarCover) - s(14) - pad / 2, 0),
				s(kBarCover));
		}
		return result;
	}
	[[nodiscard]] int infoAvailable(const Layout &layout) const {
		return layout.info.width();
	}
	[[nodiscard]] QRect barInfoRect(const Layout &layout) const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto shown = _coverClosing.animating()
			? 1.
			: _plainAnim.value(_tab == Tab::Now ? 0. : 1.);
		const auto shift = int((s(kBarCover) + s(14)) * (1. - shown));
		return layout.barInfo.adjusted(-shift, 0, 0, 0);
	}
	// Где под курсором название или исполнитель (по ширине самого текста, не всей строки).
	[[nodiscard]] InfoHover infoAt(QPoint point) const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto layout = computeLayout();
		const auto available = infoAvailable(layout);
		const auto textWidth = [&](const QFont &font, const QString &text) {
			return std::min(QFontMetrics(font).horizontalAdvance(text), available);
		};
		const auto title = QRect(
			layout.info.x(),
			layout.info.y(),
			textWidth(titleFont(), _title),
			s(30));
		const auto artist = QRect(
			layout.info.x(),
			layout.info.y() + s(32),
			textWidth(artistFont(), _artist),
			s(22));
		if (_tab != Tab::Now
			&& !layout.ultra
			&& width() >= s(760)
			&& layout.barCover.contains(point)) {
			return InfoHover::BarCover;
		}
		if (!layout.ultra && !layout.compact && layout.barInfo.width() > 0) {
			const auto info = barInfoRect(layout);
			auto font = st::normalFont->f;
			font.setPixelSize(s(14));
			const auto artistWidth = std::min(
				QFontMetrics(font).horizontalAdvance(_artist),
				info.width());
			const auto artistRect = QRect(
				info.x(), info.y() + s(38), artistWidth, s(20));
			if (artistRect.contains(point) && !_artist.trimmed().isEmpty()) {
				return InfoHover::BarArtist;
			} else if (info.contains(point) && !_title.trimmed().isEmpty()) {
				return InfoHover::BarTitle;
			}
		}
		if (!layout.showRight || _tab != Tab::Now) {
			return InfoHover::None;
		}
		return title.contains(point)
			? InfoHover::Title
			: (artist.contains(point) && !_artist.trimmed().isEmpty())
			? InfoHover::Artist
			: layout.cover.contains(point)
			? InfoHover::Cover
			: InfoHover::None;
	}

	void resizeEvent(QResizeEvent *e) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto layout = computeLayout();
		const auto barOrigin = layout.bar.topLeft();
		_playbackBar->setGeometry(layout.bar);
		_playbackBar->setVisible(layout.barShown);
		_collapse->move(layout.collapse);
		_filter->move(style::ConvertScale(16), layout.collapse.y());
		_homeBack->setGeometry(_filter->x() + _filter->width() + s(8), layout.nav.y(), s(96), layout.nav.height());
		_homeBack->setVisible(_tab == Tab::Home && _homeAlbumOpen);
		_queueToggle->move(layout.queue - barOrigin);
		_heart->move(layout.heart - barOrigin);
		for (const auto button : { _shuffle.get(), _previous.get(), _play.get(), _next.get(), _repeat.get(), _queueToggle.get() }) {
			button->setVisible(layout.barShown);
		}
		_shuffle->setVisible(layout.barShown && !layout.ultra);
		_repeat->setVisible(layout.barShown && !layout.ultra);
		_seek->setVisible(layout.barShown);
		_heart->setVisible(layout.barShown && _heartOwn);
		_volume->setVisible(layout.barShown && !layout.compact);
		if (!layout.compact) {
			_volume->setGeometry(layout.volume.translated(-barOrigin));
		}
		_seek->setGeometry(layout.seek.translated(-barOrigin));
		// The seek bar can restore its own background inside the solid capsule.
		// This keeps its animation from invalidating the scrolling feed below.
		const auto inset = s(1);
		_seek->setOpaqueBackground(_tab == Tab::Home && !layout.seek.isEmpty()
			&& Ui::SquirclePath(QRectF(layout.bar), s(kBarRadius)).contains(
				QRectF(layout.seek).adjusted(-inset, -inset, inset, inset)));
		_view->setVisible(_tab == Tab::Now && !showsQueue());
		_view->setGeometry(layout.lyrics);
		_tabScroll->setVisible(_tab != Tab::Now || showsQueue());
		const auto home = (_tab == Tab::Home);
		const auto scrollBottom = home ? height() : layout.center.bottom() + 1 - s(12);
		// Последнюю карточку можно прокрутить целиком выше плеера.
		_tabContent->setPadding(QMargins(
			0, 0, 0, home ? height() - layout.bar.y() + s(12) : 0));
		_tabScroll->setGeometry(
			layout.center.x(),
			layout.scrollTop,
			layout.center.width(),
			std::max(scrollBottom - layout.scrollTop, 0));
		_tabContent->resizeToWidth(_tabScroll->width());
		if (_homeAlbumHeader) _homeAlbumHeader->setBannerTopInset(_tabScroll->y());
		updateHomeArtistBar();

		// Навигация: ряд пилюль сверху по центру.
		{
			const auto inset = s(4);
			const auto count = int(_nav.size());
			const auto inner = std::max(layout.nav.width() - 2 * inset, 0);
			const auto columns = layout.verticalNav ? 3 : count;
			for (auto i = 0; i != count; ++i) {
				const auto left = layout.nav.x() + inset + inner * (i % columns) / columns;
				const auto right = layout.nav.x() + inset + inner * (i % columns + 1) / columns;
				_nav[i]->setCentered(true);
				_nav[i]->setGeometry(left, layout.nav.y() + s(40) * (i / columns), right - left, s(40));
			}
		}
		{
			const auto inner = std::max(layout.center.width() - s(20), 0);
			const auto left = layout.center.x() + s(10);
			const auto fieldHeight = s(52);
			const auto fieldTop = layout.center.y() + s(8);
			const auto fieldWidth = std::min(inner, s(760));
			_searchField->setGeometry(left + (inner - fieldWidth) / 2, fieldTop, fieldWidth, fieldHeight);
			_searchIndicator->setGeometry(s(18), (fieldHeight - s(20)) / 2, s(20), s(20));
			_searchRetry->setGeometry(left, layout.scrollTop + s(52), s(110), s(32));
			auto font = st::normalFont->f;
			font.setPixelSize(s(15));
			_searchField->setFont(font);
			_searchField->setStyleSheet(u"QLineEdit { background: %3; color: %4; border: 1px solid transparent; border-radius: %1px; padding: 0 %2px 0 %6px; selection-background-color: %5; } QLineEdit:focus { border: 1px solid %5; }"_q
				.arg(fieldHeight / 2)
				.arg(s(20))
				.arg(st::windowBgOver->c.name())
				.arg(st::windowFg->c.name())
				.arg(st::windowActiveTextFg->c.name())
				.arg(s(50)));
			const auto switchTop = fieldTop + fieldHeight + s(12);
			const auto stacked = layout.center.width() < s(500);
			const auto switchWidth = stacked
				? std::min(inner, s(240))
				: std::min(std::max((inner - s(8)) / 2, 0), s(240));
			const auto groupWidth = stacked ? switchWidth : 2 * switchWidth + s(8);
			const auto groupLeft = left + (inner - groupWidth) / 2;
			_sourceSwitch->setGeometry(groupLeft, switchTop, switchWidth, s(40));
			_chooseSearchChat->setGeometry(
				stacked ? groupLeft : groupLeft + switchWidth + s(8),
				stacked ? switchTop + s(48) : switchTop,
				switchWidth,
				s(40));
			_kindSwitch->setGeometry(
				stacked ? groupLeft : groupLeft + switchWidth + s(8),
				stacked ? switchTop + s(48) : switchTop,
				switchWidth,
				s(40));
		}

		const auto row = { _shuffle, _previous, _play, _next, _repeat };
		auto widths = 0;
		auto count = 0;
		for (const auto button : row) {
			if (layout.ultra && (button == _shuffle || button == _repeat)) continue;
			widths += button->width();
			++count;
		}
		const auto gap = std::clamp((layout.controlsWidth - widths) / (count - 1), s(2), s(18));
		const auto top = layout.controlsTop - barOrigin.y();
		auto x = layout.controlsLeft - barOrigin.x()
			+ (layout.controlsWidth - widths - gap * (count - 1)) / 2;
		for (const auto button : row) {
			if (layout.ultra && (button == _shuffle || button == _repeat)) continue;
			button->move(x, top + (_play->height() - button->height()) / 2);
			x += button->width() + gap;
		}
		for (const auto button : { _collapse.get(), _filter.get(), _queueToggle.get(), _heart.get() }) {
			button->raise();
		}
		RpWidget::resizeEvent(e);
		scheduleHomeMoreIfNeeded();
	}

	void relayout() {
		auto event = QResizeEvent(size(), size());
		resizeEvent(&event);
		update();
	}
	void fillAlbums() {
		const auto tr = [](const char *text) {
			return QString::fromUtf8(Mokagram::Lang::Tr(text));
		};
		if (_openedAlbum) {
			_tabList->add(object_ptr<SearchAlbumBar>(
				_tabList,
				_albumTitle,
				crl::guard(this, [=] {
					_openedAlbum = false;
					_albumLoading = false;
					++_searchGen;
					_searchStatus.clear();
					rebuildTab();
				})));
			if (_albumLoading) {
				_tabList->add(object_ptr<SearchSkeleton>(_tabList));
			} else if (_albumTracks.empty()) {
				_tabNote = _searchStatus.isEmpty()
					? tr("В альбоме нет доступных треков") : _searchStatus;
			}
			QueueState().results = _albumTracks;
			for (auto i = 0; i != int(_albumTracks.size()); ++i) {
				_tabList->add(object_ptr<Row>(
					_tabList,
					_controller,
					_albumTracks[i],
					i,
					RowMode::Results));
			}
			return;
		}
		if (_albums.empty()) {
			_tabNote = _searchStatus.isEmpty()
				? tr("Введи название альбома или исполнителя.")
				: _searchStatus;
			return;
		}
		for (const auto &album : _albums) {
			_tabList->add(object_ptr<AlbumRow>(_tabList, _controller, album, crl::guard(this, [=] {
				_openedAlbum = true;
				_albumTitle = album.title;
				_albumTracks.clear();
				_albumLoading = true;
				_searchStatus.clear();
				const auto generation = ++_searchGen;
				AlbumTracks(album.id, crl::guard(this, [=](std::vector<Track> tracks, QString error) {
					if (generation != _searchGen) {
						return;
					}
					_albumLoading = false;
					_albumTracks = std::move(tracks);
					_searchStatus = error;
					if (_tab == Tab::Search) {
						rebuildTab();
					}
				}));
				rebuildTab();
			})));
		}
	}

	void runSearch() {
		_searchTimer.stop();
		const auto query = _searchField->text().trimmed();
		const auto generation = ++_searchGen;
		_searchPending = !query.isEmpty() || (_searchChats && _searchChat);
		_searchIndicator->setLoading(_searchPending);
		_searchFailed = false;
		_searchTracks.clear();
		_albums.clear();
		_chatSearch = nullptr;
		_searchStatus = !_searchPending
			? QString()
			: QString::fromUtf8(Mokagram::Lang::Tr("Ищу…"));
		const auto finish = [=](std::vector<Track> tracks, QString error) {
			if (generation != _searchGen) {
				return;
			}
			_searchPending = false;
			_searchIndicator->setLoading(false);
			_searchTracks = std::move(tracks);
			_searchFailed = !error.isEmpty();
			_searchStatus = !error.isEmpty()
				? error
				: _searchTracks.empty()
				? (query.isEmpty()
					? QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено"))
					: QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено по запросу «%1»")).arg(query))
				: QString();
			if (_tab == Tab::Search) {
				rebuildTab();
			}
		};
		if (_searchPending) {
			if (_searchChats) {
				if (const auto session = QueueState().session.get()) {
					_chatSearch = std::make_unique<ChatMusicSearch>(
						session,
						crl::guard(this, finish),
						_searchChat);
					_chatSearch->search(query);
				} else {
					finish({}, QString::fromUtf8(Mokagram::Lang::Tr("Поиск по чатам недоступен")));
				}
			} else if (_searchKind == 1) {
				SearchAlbums(query, 20, crl::guard(this, [=](std::vector<Album> albums, QString error) {
					if (generation != _searchGen) {
						return;
					}
					_searchPending = false;
					_searchIndicator->setLoading(false);
					_albums = std::move(albums);
					_searchFailed = !error.isEmpty();
					_searchStatus = !error.isEmpty()
						? error
						: _albums.empty()
						? QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено по запросу «%1»")).arg(query)
						: QString();
					if (_tab == Tab::Search) {
						rebuildTab();
					}
				}));
			} else {
				Search(query, 0, crl::guard(this, [=](SearchResult result) {
					finish(std::move(result.tracks), result.error);
				}));
			}
		}
		if (_tab == Tab::Search) {
			rebuildTab();
		}
	}

	// Клик по маленькой обложке внизу: большая вырастает из неё на «Сейчас играет», маленькая гаснет.
	void openNowPlayingFromBar() {
		_morphFrom = computeLayout().barCover;
		setTab(Tab::Now);
		setHover(InfoHover::None);
		_coverMorph.start([=] { update(); }, 0., 1., 360, anim::easeOutCubic);
	}
	void resetHomeNavigation() {
		_homeSection = 0;
		_homeAlbumOpen = false;
		_homeArtistOpen = false;
		_homeReturnToArtist = false;
		_nav[1]->setActiveColor(Qt::white);
		++_homeAlbumGeneration;
	}
	void setTab(Tab tab) {
		if (_tab == tab) {
			if (tab == Tab::Library) {
				syncLibrary();
			}
			return;
		}
		const auto from = _tab;
		const auto animateTransition = _tab != Tab::Now
			&& tab != Tab::Now
			&& !anim::Disabled()
			&& !PowerSaving::On(PowerSaving::kAnimations);
		const auto oldLayout = computeLayout();
		const auto transitionTop = oldLayout.center.isEmpty()
			? oldLayout.lyrics.y()
			: oldLayout.center.y();
		const auto transitionArea = QRect(
			0,
			transitionTop,
			width(),
			std::max(oldLayout.bar.y() - style::ConvertScale(12) - transitionTop, 0));
		auto was = animateTransition
			? Ui::GrabWidget(this, transitionArea)
			: QPixmap();
		_tabTransitionAnimation.stop();
		_tabTransition->hide();
		_tabTransitionWas = QPixmap();
		_tabTransitionNow = QPixmap();
		_homeEntrance.stop();
		_tabScroll->setGraphicsEffect(nullptr);
		_coverClosing.stop();
		_plainAnim.start(
			[=] { update(); },
			_tab == Tab::Now ? 0. : 1.,
			tab == Tab::Now ? 0. : 1.,
			220,
			anim::easeOutCubic);
		const auto navFrom = _navSlide.value(float64(int(_tab)));
		_navSlideFrom = navFrom;
		_tab = tab;
		_backdropDirty = true;
		updateAmbientTimer();
		if (anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations)) {
			_navSlide.stop();
		} else {
			_navSlide.start(
				[=] { update(computeLayout().nav); },
				navFrom,
				float64(int(tab)),
				kSwitchDuration,
				anim::easeOutCubic);
		}
		update(computeLayout().nav);
		resetHomeNavigation();
		for (auto i = 0; i != 6; ++i) {
			_nav[i]->setActive(i == int(tab));
		}
		if (tab == Tab::Library) {
			syncLibrary();
		} else if (tab == Tab::Playlists) {
			syncPlaylists();
		}
		rebuildTab();
		auto event = QResizeEvent(size(), size());
		resizeEvent(&event);
		refresh();
		if (animateTransition && !was.isNull()) {
			_tabTransitionWas = std::move(was);
			_tabTransitionNow = Ui::GrabWidget(this, transitionArea);
			if (!_tabTransitionNow.isNull()) {
				_tabTransitionDirection = (int(tab) > int(from)) ? 1 : -1;
				_tabTransition->setGeometry(transitionArea);
				_tabTransition->show();
				_tabTransition->raise();
				_tabTransitionAnimation.start([=] {
					_tabTransition->update();
					if (!_tabTransitionAnimation.animating()) {
						_tabTransition->hide();
						_tabTransitionWas = QPixmap();
						_tabTransitionNow = QPixmap();
					}
				}, 0., 1., kSwitchDuration, anim::easeOutCubic);
			}
		}
	}
	void syncLibrary() {
		if (!Account::IsLoggedIn()
			|| _librarySyncLoading
			|| (_librarySyncSucceeded
				&& crl::now() - _librarySyncSucceeded < 15 * 1000)) {
			return;
		}
		_librarySyncLoading = true;
		Account::SyncLikes(crl::guard(this, [=](int, int, QString error) {
			_librarySyncLoading = false;
			if (error.isEmpty()) {
				_librarySyncSucceeded = crl::now();
			} else if (_tab == Tab::Library) {
				_controller->showToast(error);
			}
		}));
	}
	void syncPlaylists() {
		if (!Account::IsLoggedIn()
			|| _playlistsSyncLoading
			|| (_playlistsSyncSucceeded
				&& crl::now() - _playlistsSyncSucceeded < 15 * 1000)) {
			return;
		}
		_playlistsSyncLoading = true;
		Account::SyncPlaylists(crl::guard(this, [=](int, int, QString error) {
			_playlistsSyncLoading = false;
			if (error.isEmpty() && Account::IsLoggedIn()) {
				_playlistsSyncSucceeded = crl::now();
			} else if (!Account::IsLoggedIn()) {
				_playlistsSyncSucceeded = 0;
			} else if (_tab == Tab::Playlists) {
				_controller->showToast(error);
			}
			if (_tab == Tab::Playlists) {
				rebuildTab();
			}
		}));
	}
	[[nodiscard]] QString tabTitle() const {
		const auto text = [&]() -> const char* {
			switch (_tab) {
			case Tab::Now: return "Сейчас играет";
			case Tab::Home: return "Главная";
			case Tab::Search: return "Поиск";
			case Tab::Library: return "Любимые";
			case Tab::Playlists: return "Плейлисты";
			case Tab::Queue: return "Очередь";
			}
			return "";
		}();
		return QString::fromUtf8(Mokagram::Lang::Tr(text));
	}
	void loadHomeAlbums(int page) {
		if (page < 0 || page >= int(_homePages.size())) {
			return;
		}
		auto artist = QString();
		for (const auto &track : _homePages[page].tracks) {
			const auto candidate = track.artist.trimmed();
			if (!candidate.isEmpty() && _homeArtists.insert(candidate.toCaseFolded()).second) {
				artist = candidate;
				break;
			}
		}
		if (artist.isEmpty()) {
			return;
		}
		_homePages[page].artist = artist;
		SearchAlbums(artist, 12, crl::guard(this, [=](std::vector<Album> albums, QString error) {
			if (page >= int(_homePages.size())) {
				return;
			}
			for (auto &album : albums) {
				if (!album.id.isEmpty() && _homeAlbumIds.insert(album.id).second) {
					_homePages[page].albums.push_back(std::move(album));
				}
			}
			if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
				rebuildTab();
			}
		}));
	}

	void loadHomeSoundCloudSections() {
		if (_homeSoundCloudLoading || _homeSoundCloudLoaded
			|| _homeSection || _homeAlbumOpen) {
			return;
		}
		_homeSoundCloudLoading = true;
		_homeSoundCloudError.clear();
		const auto generation = ++_homeSoundCloudGeneration;
		const auto pending = std::make_shared<int>(2);
		const auto finished = [=] {
			if (generation != _homeSoundCloudGeneration) {
				return;
			}
			if (--*pending) {
				return;
			}
			_homeSoundCloudLoading = false;
			_homeSoundCloudLoaded = true;
			if (_homeTrending.empty()
				&& _homeWeeklyWave.id.isEmpty()
				&& _homeDailyDrops.id.isEmpty()
				&& _homeSoundCloudError.isEmpty()) {
				_homeSoundCloudError = QString::fromUtf8(Mokagram::Lang::Tr(
					"SoundCloud не вернул подборки для этого аккаунта."));
			}
			if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
				rebuildTab();
			}
		};
		LoadDiscoverPlaylists(crl::guard(this, [=](
				std::vector<Album> albums,
				QString error) {
			if (generation != _homeSoundCloudGeneration) {
				return;
			}
			if (!error.isEmpty()) {
				_homeSoundCloudError = error;
			}
			for (auto &album : albums) {
				const auto title = album.title.simplified().toCaseFolded();
				if (title == u"weekly wave"_q) {
					_homeWeeklyWave = std::move(album);
				} else if (title == u"daily drops"_q) {
					_homeDailyDrops = std::move(album);
				}
			}
			finished();
		}));
		LoadTrending(24, crl::guard(this, [=](
				std::vector<Track> tracks,
				QString error) {
			if (generation != _homeSoundCloudGeneration) {
				return;
			}
			if (!error.isEmpty() && _homeSoundCloudError.isEmpty()) {
				_homeSoundCloudError = error;
			}
			_homeTrending = std::move(tracks);
			finished();
		}));
	}

	void loadHomeAccent(const QString &url, int generation) {
		_homeAccent = QColor();
		_nav[1]->setActiveColor(Qt::white);
		update();
		LoadImage(ArtUrl(url, u"t500x500"_q), crl::guard(this, [=](QImage image) {
			if (generation != _homeAlbumGeneration) {
				return;
			}
			_homeAccent = ComputeAccent(image);
			_nav[1]->setActiveColor(Ui::CountContrast(_homeAccent, Qt::white) >= Ui::CountContrast(_homeAccent, Qt::black)
				? QColor(Qt::white) : QColor(Qt::black));
			update();
		}));
	}

	void loadHomeArtist(
			const QString &name,
			const QString &artistId,
			const QString &trackId,
			const QString &albumId,
			int generation) {
		_homeArtistAlbums.clear();
		_homeArtistTracks.clear();
		if (_homeArtistOpen) {
			_homeArtistTracks.push_back(_homeArtistSeed);
		}
		if (!name.isEmpty()) {
			SearchAlbums(name, 20, crl::guard(this, [=](std::vector<Album> albums, QString error) {
				if (generation != _homeAlbumGeneration) {
					return;
				}
				const auto artist = name.trimmed().toCaseFolded();
				auto found = std::set<QString>();
				for (auto &album : albums) {
					const auto matches = !artistId.isEmpty() && artistId != u"0"_q
						&& !album.artistId.isEmpty() && album.artistId != u"0"_q
						? artistId == album.artistId
						: album.artist.trimmed().toCaseFolded() == artist;
					if (matches && album.id != albumId && found.insert(album.id).second) {
						_homeArtistAlbums.push_back(std::move(album));
					}
				}
				if (_tab == Tab::Home && _homeAlbumOpen) {
					rebuildTab();
				}
			}));
		}
		if (!artistId.isEmpty()) {
			UserTracks(artistId, 12, crl::guard(this, [=](std::vector<Track> tracks) {
				if (generation != _homeAlbumGeneration) {
					return;
				}
				std::erase_if(tracks, [&](const Track &other) {
					return other.id == trackId || (_homeArtistOpen
						&& other.artist.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) != 0);
				});
				if (_homeArtistOpen) {
					if (tracks.empty()) tracks.push_back(_homeArtistSeed);
					std::stable_sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) {
						return a.playbackCount > b.playbackCount;
					});
				}
				_homeArtistTracks = std::move(tracks);
				if (_tab == Tab::Home && _homeAlbumOpen) {
					rebuildTab();
				}
			}));
		}
	}

	// Похожее по звучанию: жанр и слова из названия трека, а не его автор.
	void loadHomeSimilar(const Track &seed, int generation) {
		_homeSimilarTracks.clear();
		_homeSimilarLoading = true;
		auto avoid = std::vector<Track>();
		avoid.push_back(seed);
		for (const auto &track : _homeAlbumTracks) {
			avoid.push_back(track);
		}
		for (const auto &track : _homeArtistTracks) {
			avoid.push_back(track);
		}
		BuildSimilar(seed, avoid, 24, crl::guard(this, [=](std::vector<Track> tracks, QString) {
			if (generation != _homeAlbumGeneration) {
				return;
			}
			_homeSimilarLoading = false;
			_homeSimilarTracks = std::move(tracks);
			if (_tab == Tab::Home && _homeAlbumOpen) rebuildTab();
		}));
	}

	void playHomeArtist() {
		auto results = _homeArtistTracks;
		std::erase_if(results, [](const Track &track) {
			return track.unavailable;
		});
		if (results.empty()) return;

		QueueState().results = std::move(results);
		PlayFromResults(0);
	}

	void updateHomeArtistBar() {
		if (_tab != Tab::Home || !_homeArtistOpen || !_homeAlbumHeader) {
			_homeArtistBar->hide();
			return;
		}

		const auto s = [](int value) { return style::ConvertScale(value); };
		const auto height = s(56);
		const auto remaining = _homeAlbumHeader->height() - _tabScroll->scrollTop();
		const auto ratio = std::clamp((2 * height - remaining) / double(height), 0., 1.);
		const auto opacity = ratio * ratio * (3. - 2. * ratio);
		_homeAlbumHeader->setArtistIdentityOpacity(1. - opacity);
		_homeArtistBar->setVisible(opacity > 0.);
		if (opacity <= 0.) return;

		const auto width = _tabScroll->viewport()->width();
		_homeArtistBar->setGeometry(0, 0, width, height);
		_homeArtistBarOpacity->setOpacity(opacity);
		const auto interactive = opacity >= 0.5;
		_homeArtistBar->setAttribute(Qt::WA_TransparentForMouseEvents, !interactive);
		_homeArtistBarPlay->setFocusPolicy(interactive ? Qt::TabFocus : Qt::NoFocus);
		_homeArtistBarPlay->setFullWidth(std::min(s(144), std::max(width - s(44), 0) / 2));
		_homeArtistBarPlay->moveToRight(
			s(22), (height - _homeArtistBarPlay->height()) / 2);
		_homeArtistBarPlay->setDisabled(std::ranges::all_of(
			_homeArtistTracks, &Track::unavailable));
		_homeArtistBar->raise();
		_homeArtistBar->update();
	}

	void openHomeArtist(Track track) {
		if (!_homeReturnToArtist) {
			_homeArtistFeedScroll = _tabScroll->scrollTop();
			_homeArtistLikesOpen = false;
			_discographyFilter = DiscographyFilter::All;
			_discographyExpanded = false;
			*_discographyOffset = 0;
		}
		_homeReturnToArtist = false;
		_homeAlbumReturnScroll = _homeArtistFeedScroll;
		_homeArtistOpen = true;
		_homeAlbumOpen = true;
		_homeAlbumIsTrack = false;
		_homeArtistSeed = track;
		const auto generation = ++_homeAlbumGeneration;
		_homeArtistBannerUrl.clear();
		const auto art = track.artistArtworkUrl.isEmpty() ? track.artworkUrl : track.artistArtworkUrl;
		_homeAlbum = { {}, track.artist, {}, art, 0 };
		_homeAlbumTracks.clear();
		_homeAlbumNote.clear();
		_homeAlbumLoading = false;
		loadHomeAccent(art, generation);
		if (!track.artistId.isEmpty()) {
			LoadArtistInfo(track.artistId, crl::guard(this, [=](ArtistInfo info) {
				if (generation != _homeAlbumGeneration || !_homeArtistOpen) {
					return;
				}
				if (!info.avatarUrl.isEmpty()) {
					_homeAlbum.artworkUrl = info.avatarUrl;
				}
				_homeArtistBannerUrl = info.bannerUrl;
				if (_tab == Tab::Home) {
					rebuildTab();
				}
			}));
		}
		loadHomeArtist(track.artist, track.artistId, {}, {}, generation);
		loadHomeSimilar(track, generation);
		syncLibrary();
		syncPlaylists();
		relayout();
		rebuildTab();
		_tabScroll->scrollToY(0);
	}

	void openHomeTrack(Track track) {
		_homeArtistOpen = false;
		_homeAlbumReturnScroll = _tabScroll->scrollTop();
		_homeAlbumOpen = true;
		_homeAlbumIsTrack = true;
		const auto generation = ++_homeAlbumGeneration;
		loadHomeAccent(track.artworkUrl, generation);
		loadHomeArtist(track.artist, track.artistId, track.id, {}, generation);
		loadHomeSimilar(track, generation);
		_homeAlbum = { {}, track.title, track.artist, track.artworkUrl, 1 };
		_homeAlbumTracks = { std::move(track) };
		_homeAlbumLoading = false;
		_homeAlbumNote.clear();
		relayout();
		rebuildTab();
		_tabScroll->scrollToY(0);
	}

	void openHomeAlbum(Album album) {
		_homeReturnToArtist = _homeArtistOpen;
		_homeArtistOpen = false;
		_homeAlbumIsTrack = false;
		_homeAlbumReturnScroll = _tabScroll->scrollTop();
		_homeAlbumOpen = true;
		relayout();
		_homeAlbum = album;
		RecordRecentAlbum(album);
		loadHomeAlbum();
		_tabScroll->scrollToY(0);
	}

	void loadHomeAlbum() {
		_homeAlbumTracks.clear();
		_homeAlbumLoading = true;
		_homeAlbumNote = QString::fromUtf8(Mokagram::Lang::Tr("Загружаю альбом…"));
		const auto generation = ++_homeAlbumGeneration;
		loadHomeAccent(_homeAlbum.artworkUrl, generation);
		loadHomeArtist(_homeAlbum.artist, {}, {}, _homeAlbum.id, generation);
		rebuildTab();
		AlbumTracks(_homeAlbum.id, crl::guard(this, [=](std::vector<Track> tracks, QString error) {
			if (generation != _homeAlbumGeneration) {
				return;
			}
			_homeAlbumLoading = false;
			for (auto &track : tracks) {
				if (track.artworkUrl.isEmpty()) track.artworkUrl = _homeAlbum.artworkUrl;
			}
			_homeAlbumTracks = std::move(tracks);
			if (!_homeAlbumTracks.empty() && !_homeAlbumTracks.front().artistId.isEmpty()) {
				const auto artistId = _homeAlbumTracks.front().artistId;
				UserTracks(artistId, 12, crl::guard(this, [=](std::vector<Track> related) {
					if (generation != _homeAlbumGeneration) {
						return;
					}
					std::erase_if(related, [&](const Track &track) {
						return std::any_of(_homeAlbumTracks.begin(), _homeAlbumTracks.end(),
							[&](const Track &albumTrack) { return albumTrack.id == track.id; });
					});
					_homeArtistTracks = std::move(related);
					if (_tab == Tab::Home && _homeAlbumOpen) rebuildTab();
				}));
			}
			_homeAlbumNote = !error.isEmpty() ? error : _homeAlbumTracks.empty()
				? QString::fromUtf8(Mokagram::Lang::Tr("В альбоме нет доступных треков"))
				: QString();
			if (_tab == Tab::Home && _homeAlbumOpen) {
				rebuildTab();
			}
		}));
	}

	void scheduleHomeMoreIfNeeded() {
		if (_homeMoreCheckQueued || _homeMoreLoading || _homeMoreRetryTimer.isActive()
			|| _homePages.empty() || _tab != Tab::Home || _homeSection
			|| _homeAlbumOpen || _tabScroll->height() <= 0
			|| _tabScroll->scrollTopMax() - _tabScroll->scrollTop() > style::ConvertScale(240)) {
			return;
		}
		_homeMoreCheckQueued = true;
		crl::on_main(this, [=] {
			_homeMoreCheckQueued = false;
			if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen
				&& !_homeMoreRetryTimer.isActive() && !_homeMoreLoading
				&& _tabScroll->height() > 0
				&& _tabScroll->scrollTopMax() - _tabScroll->scrollTop() <= style::ConvertScale(240)) {
				loadMoreHome();
			}
		});
	}

	void loadMoreHome() {
		if (_homeMoreLoading || _homePages.empty() || _homeMoreRetryTimer.isActive()) {
			return;
		}
		_homeMoreLoading = true;
		auto context = StreamContext();
		context.avoidPreviousArtists = false;
		for (const auto &page : _homePages) {
			context.avoid.insert(context.avoid.end(), page.tracks.begin(), page.tracks.end());
		}
		if (!context.avoid.empty()) {
			context.current = context.avoid.back();
		}
		BuildStream(std::move(context), 12, crl::guard(this, [=](std::vector<Track> tracks, QString note) {
			_homeMoreLoading = false;
			if (tracks.empty()) {
				_homeMoreFailures = std::min(_homeMoreFailures + 1, 6);
				_homeMoreRetryTimer.start(1000 * (1 << _homeMoreFailures));
			} else {
				_homeMoreFailures = 0;
				_homePages.push_back({ std::move(tracks), {}, {} });
				loadHomeAlbums(int(_homePages.size()) - 1);
			}
			if (_tab == Tab::Home && !_homeSection && !_homeAlbumOpen) {
				rebuildTab();
			}
		}));
	}

	void addHomeDiscography() {
		if (_homeArtistAlbums.empty()) return;

		const auto s = [](int value) { return style::ConvertScale(value); };
		auto releases = _homeArtistAlbums;
		std::erase_if(releases, [&](const Album &release) {
			switch (_discographyFilter) {
			case DiscographyFilter::All: return false;
			case DiscographyFilter::Albums:
				return release.releaseKind != ReleaseKind::Album
					&& release.releaseKind != ReleaseKind::Compilation;
			case DiscographyFilter::SinglesAndEps:
				return release.releaseKind != ReleaseKind::Single
					&& release.releaseKind != ReleaseKind::Ep;
			}
			Unexpected("Discography filter.");
		});
		constexpr auto kPreviewCount = size_t(6);
		const auto generation = _homeAlbumGeneration;
		auto toggle = Fn<void()>();
		if (releases.size() > kPreviewCount) {
			toggle = [=] {
				crl::on_main(this, [=] {
					if (_tab == Tab::Home && generation == _homeAlbumGeneration) {
						_discographyExpanded = !_discographyExpanded;
						rebuildTab();
					}
				});
			};
		}
		_tabList->add(object_ptr<HomeDiscographyBar>(
			_tabList,
			_discographyFilter,
			_discographyExpanded,
			[=](DiscographyFilter filter) {
				if (_discographyFilter == filter) return;
				crl::on_main(this, [=] {
					if (_tab == Tab::Home && generation == _homeAlbumGeneration) {
						_discographyFilter = filter;
						_discographyExpanded = false;
						*_discographyOffset = 0;
						rebuildTab();
					}
				});
			},
			std::move(toggle)), QMargins(s(14), s(20), s(14), s(8)));
		if (releases.empty()) {
			_tabList->add(object_ptr<Ui::FlatLabel>(
				_tabList, PlaylistText("В этой категории нет релизов"), st::boxLabel),
				QMargins(s(14), s(8), s(14), s(8)));
			return;
		}

		if (!_discographyExpanded && releases.size() > kPreviewCount) {
			releases.resize(kPreviewCount);
		}
		_tabList->add(object_ptr<HomeMixedGrid>(
			_tabList, _controller, std::vector<Track>(), releases,
			crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
			nullptr, _discographyExpanded, _discographyOffset));
	}

	void rebuildTab() {
		_rebuildingTab = true;
		const auto rebuildingGuard = gsl::finally([&] { _rebuildingTab = false; });
		const auto keep = (_rebuiltTab == _tab) ? _tabScroll->scrollTop() : 0;
		_rebuiltTab = _tab;
		_searchResultWidgets.clear();
		_tabList->clear();
		_homeBack->setVisible(_tab == Tab::Home && _homeAlbumOpen);
		_tabNote.clear();
		if (_tab != Tab::Playlists) {
			_playlistsSticker = nullptr;
		}
		const auto searching = (_tab == Tab::Search);
		const auto emptySearch = searching && _searchField->text().trimmed().isEmpty()
			&& !(_searchChats && _searchChat);
		if (!emptySearch) {
			_searchEmptySticker = nullptr;
		} else if (!_searchEmptySticker) {
			auto file = QFile(u":/animations/mokagram_search_empty.webm"_q);
			if (file.open(QIODevice::ReadOnly)) {
				_searchEmptySticker = Media::Clip::MakeReader(
					file.readAll(),
					[=](Media::Clip::Notification) { update(); });
			}
		}
		const auto fresh = searching && !_searchField->isVisible();
		_searchField->setVisible(searching);
		_sourceSwitch->setVisible(searching);
		_chooseSearchChat->setVisible(searching && _searchChats);
		_kindSwitch->setVisible(searching && !_searchChats);
		_searchRetry->setVisible(searching && _searchFailed && !_openedAlbum);
		if (fresh) {
			_searchField->setFocus();
		}
		const auto tr = [](const char *text) {
			return QString::fromUtf8(Mokagram::Lang::Tr(text));
		};
		const auto addRows = [&](const std::vector<Track> &tracks, RowMode mode = RowMode::Results, QString playlistId = {}) {
			auto playable = std::make_shared<std::vector<Track>>();
			for (const auto &track : tracks) {
				if (!track.unavailable) {
					playable->push_back(track);
				}
			}
			QueueState().results = *playable;
			auto resultIndex = 0;
			for (auto i = 0; i != int(tracks.size()); ++i) {
				const auto index = tracks[i].unavailable ? -1 : resultIndex++;
				const auto row = _tabList->add(object_ptr<Row>(
					_tabList,
					_controller,
					tracks[i],
					i,
					mode,
					playable,
					index,
					nullptr,
					playlistId));
				row->setTextLinks(
					crl::guard(this, [=](Track track) {
						setTab(Tab::Home);
						openHomeTrack(std::move(track));
					}),
					crl::guard(this, [=](Track track) {
						setTab(Tab::Home);
						openHomeArtist(std::move(track));
					}));
			}
		};
		switch (_tab) {
		case Tab::Now:
			if (showsQueue()) {
				fillQueue();
			}
			break;
		case Tab::Home: {
			const auto &queue = QueueState();
			const auto liked = LikedTracks();
			if (_homeAlbumOpen) {

				const auto albumHeader = _tabList->add(object_ptr<HomeAlbumHeader>(
					_tabList, _homeAlbum, int(_homeAlbumTracks.size()), _homeAlbumIsTrack,
					crl::guard(this, [=](QImage image, QRect source) {
						source.moveTopLeft(mapFromGlobal(source.topLeft()));
						Ui::CreateChild<HomeCoverPreview>(this, std::move(image), source);
					}), _homeArtistOpen, _homeArtistBannerUrl,
					crl::guard(this, [=] { playHomeArtist(); }),
					crl::guard(this, [=] {
						const auto artistTrack = std::find_if(
							_homeAlbumTracks.begin(),
							_homeAlbumTracks.end(),
							[=](const Track &track) {
								return track.artist.trimmed().compare(
									_homeAlbum.artist.trimmed(), Qt::CaseInsensitive) == 0;
							});
						if (artistTrack != _homeAlbumTracks.end()) {
							openHomeArtist(*artistTrack);
						} else if (!_homeAlbumTracks.empty()) {
							openHomeArtist(_homeAlbumTracks.front());
						}
					}), crl::guard(this, [=] {
						updateHomeArtistBar();
						update();
					})), QMargins(0, 0, 0, style::ConvertScale(8)));
				_homeAlbumHeader = albumHeader;
				albumHeader->setBannerTopInset(_tabScroll->y());
				albumHeader->setArtistPlayable(!std::ranges::all_of(
					_homeArtistTracks, &Track::unavailable));
				if (!_homeAlbumIsTrack && !_homeArtistOpen) InstallAlbumContextMenu(albumHeader, _controller, _homeAlbum);

				if (_homeArtistOpen) {
					if (!_homeArtistTracks.empty()) {
						_tabList->add(object_ptr<Ui::FlatLabel>(_tabList, tr("Популярное"), st::boxLabel), QMargins(20, 8, 20, 4));
						addRows(_homeArtistTracks, RowMode::Album);
					}
					auto artistLikes = std::vector<Track>();
					const auto &artistId = _homeArtistSeed.artistId;
					const auto artistName = _homeArtistSeed.artist.trimmed();
					for (const auto &track : liked) {
						const auto matches = !artistId.isEmpty() && artistId != u"0"_q
							&& !track.artistId.isEmpty() && track.artistId != u"0"_q
							? artistId == track.artistId
							: !artistName.isEmpty()
								&& track.artist.trimmed().compare(artistName, Qt::CaseInsensitive) == 0;
						if (matches) artistLikes.push_back(track);
					}
					auto likedReleases = std::vector<Album>();
					const auto playlists = Playlists();
					for (const auto &album : _homeArtistAlbums) {
						if (std::ranges::any_of(playlists, [&](const Playlist &playlist) {
							return playlist.remote && playlist.sourceId == album.id;
						})) {
							likedReleases.push_back(album);
						}
					}
					if (!artistLikes.empty() || !likedReleases.empty()) {
						auto counts = QStringList();
						if (!artistLikes.empty()) {
							counts.push_back(tr("Треков: %1").arg(artistLikes.size()));
						}
						if (!likedReleases.empty()) {
							counts.push_back(tr("Релизов: %1").arg(likedReleases.size()));
						}
						const auto s = [](int value) { return style::ConvertScale(value); };
						const auto generation = _homeAlbumGeneration;
						_tabList->add(object_ptr<HomeArtistLikesCard>(
							_tabList,
							_homeAlbum.artworkUrl,
							counts.join(u" · "_q),
							_homeArtistLikesOpen,
							crl::guard(this, [=] {
								crl::on_main(this, [=] {
									if (_tab == Tab::Home && generation == _homeAlbumGeneration) {
										_homeArtistLikesOpen = !_homeArtistLikesOpen;
										rebuildTab();
									}
								});
							})), QMargins(s(14), s(20), s(14), s(8)));
						if (_homeArtistLikesOpen) {
							if (!artistLikes.empty()) addRows(artistLikes, RowMode::Album);
							if (!likedReleases.empty()) {
								_tabList->add(object_ptr<HomeMixedGrid>(
									_tabList, _controller, std::vector<Track>(), likedReleases,
									crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
									nullptr, true, nullptr));
							}
						}
					}
					addHomeDiscography();
					const auto similarArtists = HomeSimilarArtists(
						_homeArtistSeed,
						_homeSimilarTracks);
					if (!similarArtists.empty() || _homeSimilarLoading) {
						_tabList->add(object_ptr<Ui::FlatLabel>(
							_tabList,
							tr("Похожие исполнители"),
							st::boxLabel), QMargins(
								style::ConvertScale(20),
								style::ConvertScale(28),
								style::ConvertScale(20),
								style::ConvertScale(8)));
						if (!similarArtists.empty()) {
							_tabList->add(object_ptr<HomeMixedGrid>(
								_tabList,
								_controller,
								std::vector<Track>(),
								std::vector<Album>(),
								crl::guard(this, [=](Album album) {
									openHomeAlbum(std::move(album));
								}),
								crl::guard(this, [=](Track track) {
									openHomeTrack(std::move(track));
								}),
								false,
								std::make_shared<int>(0),
								similarArtists,
								crl::guard(this, [=](Track track) {
									openHomeArtist(std::move(track));
								})));
						} else {
							_tabList->add(object_ptr<HomeLoadingView>(
								_tabList,
								tr("Ищу похожих исполнителей…")));
						}
					}
					break;
				}
				if (!_homeAlbumNote.isEmpty()) {
					if (_homeAlbumLoading) {
						_tabList->add(object_ptr<Ui::FlatLabel>(_tabList, _homeAlbumNote, st::boxLabel), QMargins(12, 8, 12, 8));
					} else {
						_tabList->add(object_ptr<HomeRetryView>(_tabList, _homeAlbumNote,
							crl::guard(this, [=] { loadHomeAlbum(); })));
					}
				} else if (!_homeAlbumTracks.empty()) {
					_tabList->add(object_ptr<Ui::FlatLabel>(
						_tabList, tr("Треки"), st::boxLabel), QMargins(20, 8, 20, 4));
					addRows(_homeAlbumTracks, RowMode::Album);
				}
				if (!_homeArtistAlbums.empty() || !_homeArtistTracks.empty()) {
					_tabList->add(object_ptr<Ui::FlatLabel>(
						_tabList,
						tr("Ещё от %1").arg(_homeAlbum.artist),
						st::boxLabel), QMargins(20, 28, 20, 8));
					const auto &tracks = _homeArtistAlbums.empty()
						? _homeArtistTracks
						: std::vector<Track>();
					_tabList->add(object_ptr<HomeMixedGrid>(
						_tabList, _controller, tracks, _homeArtistAlbums,
						crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
						crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); }),
						false, std::make_shared<int>(0)));
				}
				// Не только автор: то же звучание у других исполнителей.
				if (!_homeSimilarTracks.empty() || _homeSimilarLoading) {
					const auto above = !_homeArtistAlbums.empty() || !_homeArtistTracks.empty();
					if (!_homeSimilarTracks.empty()) {
						_tabList->add(object_ptr<Ui::FlatLabel>(
							_tabList,
							tr("Похожее по звучанию"),
							st::boxLabel), QMargins(20, above ? 28 : 20, 20, 8));
						_tabList->add(object_ptr<HomeMixedGrid>(
							_tabList, _controller, _homeSimilarTracks, std::vector<Album>(),
							crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
							crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); }),
							false, std::make_shared<int>(0)));
					} else {
						_tabList->add(object_ptr<HomeLoadingView>(
							_tabList, tr("Ищу похожее по звучанию…")));
					}
				}
				break;
			}
			loadHomeSoundCloudSections();
			if (_homeSection) {
				const auto page = (_homeSection - 100) / 2;
				const auto pageTracks = _homeSection >= 100
					&& !((_homeSection - 100) % 2)
					&& page < int(_homePages.size());
				const auto title = (_homeSection == 2) ? tr("Избранное")
					: (_homeSection == kHomeTrendingSection) ? tr("Trending")
					: (_homeSection == 4) ? tr("Weekly Wave")
					: (_homeSection == 5) ? tr("Daily Drops")
					: tr("Для вас");
				_tabList->add(object_ptr<HomeSectionHeader>(
					_tabList, title, true, crl::guard(this, [=] {
						_homeSection = 0;
						rebuildTab();
						_tabScroll->scrollToY(_homeFeedScroll);
					})));
				const auto emptyTracks = std::vector<Track>();
				const auto &tracks = pageTracks ? _homePages[page].tracks
					: (_homeSection == 2) ? liked
					: (_homeSection == kHomeTrendingSection) ? _homeTrending
					: (_homeSection == 4 || _homeSection == 5) ? emptyTracks
					: queue.forYou;
				auto albums = pageTracks ? _homePages[page].albums : std::vector<Album>();
				if (_homeSection == 4 && !_homeWeeklyWave.id.isEmpty()) {
					albums.push_back(_homeWeeklyWave);
				} else if (_homeSection == 5 && !_homeDailyDrops.id.isEmpty()) {
					albums.push_back(_homeDailyDrops);
				}
				if (tracks.empty() && albums.empty()) {
					_tabList->add(object_ptr<Ui::FlatLabel>(_tabList,
						_homeSection == 2
							? tr("Добавьте треки в избранное с помощью сердечка.")
							: tr("Нет доступной музыки в этой подборке."),
						st::boxLabel));
				} else {
					const auto offset = pageTracks ? _homePages[page].shelfOffset
						: (_homeSection == 2) ? _homeLikedOffset
						: (_homeSection == kHomeTrendingSection) ? _homeTrendingOffset
						: (_homeSection == 4) ? _homeWeeklyWaveOffset
						: (_homeSection == 5) ? _homeDailyDropsOffset
						: std::shared_ptr<int>();
					_tabList->add(object_ptr<HomeMixedGrid>(_tabList, _controller,
						tracks, albums,
						crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
						crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); }),
						true, offset,
						pageTracks ? HomeRecommendationArtists(tracks) : std::vector<Track>(),
						crl::guard(this, [=](Track track) { openHomeArtist(std::move(track)); }),
						!pageTracks || page == 0));
				}
				break;
			}
			const auto section = [&](QString title, int id, const std::vector<Track> &tracks) {
				if (tracks.empty()) return;
				_tabList->add(object_ptr<HomeSectionHeader>(
					_tabList, title, false, crl::guard(this, [=] {
						_homeFeedScroll = _tabScroll->scrollTop();
						_homeSection = id;
						rebuildTab();
						_tabScroll->scrollToY(0);
					})));
				_tabList->add(object_ptr<HomeMixedGrid>(_tabList, _controller, tracks,
					std::vector<Album>(),
					crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
					crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); }),
					false, _homeLikedOffset));
			};
			const auto playlistSection = [&](
					QString title,
					int id,
					const Album &album,
					std::shared_ptr<int> offset) {
				if (album.id.isEmpty()) return;
				_tabList->add(object_ptr<HomeSectionHeader>(
					_tabList, std::move(title), false, crl::guard(this, [=] {
						_homeFeedScroll = _tabScroll->scrollTop();
						_homeSection = id;
						rebuildTab();
						_tabScroll->scrollToY(0);
					})));
				_tabList->add(object_ptr<HomeMixedGrid>(
					_tabList,
					_controller,
					std::vector<Track>(),
					std::vector<Album>{ album },
					crl::guard(this, [=](Album album) {
						openHomeAlbum(std::move(album));
					}),
					crl::guard(this, [=](Track track) {
						openHomeTrack(std::move(track));
					}),
					false,
					std::move(offset)));
			};
			const auto recent = RecentItems();
			if (!recent.empty()) {
				_tabList->add(object_ptr<Ui::FlatLabel>(_tabList, tr("Недавно"), st::boxLabel), QMargins(12, 8, 12, 4));
				_tabList->add(object_ptr<HomeRecentGrid>(_tabList, _controller, recent,
					crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
					crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); })));
			}
			if (_homeSoundCloudLoading) {
				_tabList->add(object_ptr<HomeLoadingView>(
					_tabList, tr("Загружаю подборки SoundCloud…")));
			}
			playlistSection(
				tr("Weekly Wave"),
				4,
				_homeWeeklyWave,
				_homeWeeklyWaveOffset);
			if (!Mokagram::Settings::Bool(u"player.hide_trending"_q)) {
				section(tr("Trending"), kHomeTrendingSection, _homeTrending);
			}
			playlistSection(
				tr("Daily Drops"),
				5,
				_homeDailyDrops,
				_homeDailyDropsOffset);
			if (_homeSoundCloudLoaded
				&& _homeTrending.empty()
				&& _homeWeeklyWave.id.isEmpty()
				&& _homeDailyDrops.id.isEmpty()) {
				_tabList->add(object_ptr<HomeRetryView>(
					_tabList,
					_homeSoundCloudError.isEmpty()
						? tr("Подборки SoundCloud недоступны.")
						: _homeSoundCloudError,
					crl::guard(this, [=] {
						_homeSoundCloudLoaded = false;
						_homeTrending.clear();
						_homeWeeklyWave = {};
						_homeDailyDrops = {};
						loadHomeSoundCloudSections();
					})));
			}
			if (_homePages.empty() && !queue.forYou.empty()) {
				_homePages.push_back({ queue.forYou, {}, {} });
				crl::on_main(this, [=] { loadHomeAlbums(0); });
			}
			for (auto i = 0; i != int(_homePages.size()); ++i) {
				const auto &page = _homePages[i];
				const auto title = i ? tr("Ещё для вас") : tr("Для вас");
				_tabList->add(object_ptr<HomeSectionHeader>(
					_tabList, title, false, crl::guard(this, [=] {
						_homeFeedScroll = _tabScroll->scrollTop();
						_homeSection = 100 + 2 * i;
						rebuildTab();
						_tabScroll->scrollToY(0);
					})));
				_tabList->add(object_ptr<HomeMixedGrid>(_tabList, _controller,
					page.tracks, page.albums,
					crl::guard(this, [=](Album album) { openHomeAlbum(std::move(album)); }),
					crl::guard(this, [=](Track track) { openHomeTrack(std::move(track)); }),
					false, page.shelfOffset, HomeRecommendationArtists(page.tracks),
					crl::guard(this, [=](Track track) { openHomeArtist(std::move(track)); }),
					i == 0));
				if (!i) section(tr("Избранное"), 2, liked);
			}
			if (_homePages.empty() && !_homeLoaded) {
				_tabList->add(object_ptr<HomeLoadingView>(
					_tabList, tr("Подбираю музыку под ваш вкус…")));
				if (!_homeLoading) {
					_homeLoading = true;
					crl::on_main(this, [=] {
						BuildForYou(30, crl::guard(this, [=](std::vector<Track> tracks, QString note) {
							auto &queue = QueueState();
							queue.forYou = std::move(tracks);
							queue.forYouNote = note;
							_homeLoading = false;
							_homeLoaded = true;
							if (_tab == Tab::Home) rebuildTab();
						}));
					});
				}
			} else if (_homePages.empty()) {
				const auto message = queue.forYouNote.isEmpty()
					? tr("Добавьте треки в избранное, чтобы получить рекомендации.")
					: queue.forYouNote;
				_tabList->add(object_ptr<HomeRetryView>(_tabList, message, crl::guard(this, [=] {
					_homeLoaded = false;
					rebuildTab();
				})));
				section(tr("Избранное"), 2, liked);
			}
		} break;
		case Tab::Search:
			if (emptySearch) {
				break;
			} else if (_searchPending && !_openedAlbum) {
				_tabList->add(object_ptr<SearchSkeleton>(_tabList));
			} else if (_searchKind == 1 && !_searchChats) {
				fillAlbums();
			} else if (_searchTracks.empty()) {
				_tabNote = _searchStatus.isEmpty()
					? tr("Введи название или исполнителя.")
					: _searchStatus;
			} else {
				addRows(_searchTracks);
				if (_searchChats && _chatSearch && _chatSearch->hasMore()) {
					const auto more = _tabList->add(object_ptr<Ui::RoundButton>(
						_tabList,
						rpl::single(tr("Показать ещё")),
						st::defaultLightButton));
					more->setClickedCallback([=] {
						if (_chatSearch && _chatSearch->hasMore()) {
							more->setDisabled(true);
							_chatSearch->searchMore();
						}
					});
				}
			}
			break;
		case Tab::Library: {
			// Аккаунт первым: лайки из SoundCloud и свои — один список.
			_tabList->add(object_ptr<SoundCloudAccountBar>(
				_tabList,
				[=] { ShowSoundCloudLogin(nullptr); },
				[=] { Mokagram::Music::Account::Logout(); }),
				QMargins(12, 4, 12, 8));
			const auto liked = LikedTracks();
			if (liked.empty()) {
				_tabNote = tr("Пока пусто. Нажми сердечко у трека, и он появится здесь.");
			} else {
				addRows(liked);
			}
		} break;
		case Tab::Playlists: {
			const auto playlists = Playlists();
			const auto found = std::find_if(playlists.begin(), playlists.end(), [=](const Playlist &p) {
				return p.id == _openedPlaylist;
			});
			if (found != playlists.end()) {
				_tabList->add(object_ptr<HomeSectionHeader>(_tabList, found->name, true, [=] {
					_openedPlaylist.clear();
					rebuildTab();
				}));
				if (found->tracks.empty()) {
					_tabNote = found->remote
						? tr("В этом плейлисте SoundCloud нет доступных треков.")
						: tr("Добавь треки через их контекстное меню.");
				} else {
					addRows(
						found->tracks,
						RowMode::Results,
						found->remote ? QString() : found->id);
				}
			} else {
				_openedPlaylist.clear();
				_tabList->add(object_ptr<HomeSectionHeader>(_tabList, tr("Создать плейлист"), true, [=] {
					PlaylistNameBox(_controller, tr("Создать плейлист"), {}, crl::guard(this, [=](QString name) {
						_openedPlaylist = CreatePlaylist(name);
					}));
				}, true));
				_tabList->add(object_ptr<SoundCloudAccountBar>(
					_tabList,
					[=] { ShowSoundCloudLogin(nullptr); },
					[=] { Mokagram::Music::Account::Logout(); },
					true),
					QMargins(
						style::ConvertScale(12),
						style::ConvertScale(4),
						style::ConvertScale(12),
						style::ConvertScale(8)));
				for (const auto &playlist : playlists) {
					_tabList->add(object_ptr<PlaylistRow>(_tabList, _controller, playlist, [=](QString id) {
						_openedPlaylist = std::move(id);
						rebuildTab();
					}));
				}
				if (playlists.empty()) {
					_tabNote = _playlistsSyncLoading
						? tr("Загружаю плейлисты SoundCloud…")
						: tr("Создай плейлист или добавь трек через контекстное меню.");
				}
			}
		} break;
		case Tab::Queue:
			fillQueue();
			break;
		}
		if (_tab == Tab::Search) {
			for (const auto child : _tabList->children()) {
				if (auto widget = qobject_cast<QWidget*>(child); widget
					&& (dynamic_cast<Row*>(widget) || dynamic_cast<AlbumRow*>(widget)) && widget->isEnabled()) {
					_searchResultWidgets.push_back(widget);
					widget->installEventFilter(this);
				}
			}
		}
		_tabContent->resizeToWidth(_tabScroll->width());
		if (!showsQueue()) {
			_tabScroll->scrollToY(keep);
		}
		updateHomeArtistBar();
		update();
		scheduleHomeMoreIfNeeded();
	}
	void fillQueue() {
		const auto &queue = QueueState();
		const auto playingId = PlayingTrack().id;
		const auto inQueue = ranges::contains(queue.tracks, playingId, &Track::id);
		const auto &tracks = inQueue ? queue.tracks : ContextTracks();
		if (!inQueue) {
			// Играет трек из чата: очередь это музыка того же чата.
			const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
			const auto peer = current.contextId().peer;
			if (peer && peer != _contextPeer) {
				_contextPeer = peer;
				ContextTracks().clear();
				if (const auto session = QueueState().session.get()) {
					_context = std::make_unique<ChatMusicSearch>(
						session,
						crl::guard(this, [=](std::vector<Track> list, QString) {
							ContextTracks() = std::move(list);
							if (showsQueue()) {
								rebuildTab();
							}
						}),
						peer);
					_context->search(QString());
				}
			}
		}
		for (auto i = 0; i != int(tracks.size()); ++i) {
			_tabList->add(object_ptr<Row>(
				_tabList,
				_controller,
				tracks[i],
				i,
				inQueue ? RowMode::Queue : RowMode::Context));
		}
		if (tracks.empty()) {
			_tabNote = QString::fromUtf8(Mokagram::Lang::Tr("Очередь пуста."));
		}
		_tabContent->resizeToWidth(_tabScroll->width());
		auto at = inQueue ? queue.index : -1;
		for (auto i = 0; !inQueue && i != int(tracks.size()); ++i) {
			if (tracks[i].id == playingId) {
				at = i;
				break;
			}
		}
		if (at >= 0) {
			_tabScroll->scrollToY(std::max((at - 1) * style::ConvertScale(kRowHeight), 0));
		}
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto plainBackground = _plainAnim.value(_tab == Tab::Now ? 0. : 1.);
		if (plainBackground < 1.) {
			paintBackgroundPart(p, e->rect());
		} else {
			_preparedBackdrop = QImage();
		}
		// Вне «Сейчас играет» экран в цветах клиента, как остальные окна.
		if (plainBackground > 0.) {
			p.setOpacity(plainBackground);
			p.fillRect(e->rect(), _tab == Tab::Home && _homeArtistOpen
				? st::boxBg->c
				: QColor(0x1c, 0x1c, 0x1c));
			p.setOpacity(1.);
		}
		const auto artistHeader = (_tab == Tab::Home && _homeArtistOpen)
			? _homeAlbumHeader.data()
			: nullptr;
		if (artistHeader && !artistHeader->hasArtistBanner()) {
			const auto offset = _tabScroll->scrollTop();
			const auto end = _tabScroll->y() + artistHeader->height() - offset;
			if (end > 0) {
				const auto tone = _homeAccent.isValid()
					? anim::color(st::windowBg->c, _homeAccent, 0.12)
					: st::windowBg->c;
				auto background = QLinearGradient(0, -offset, 0, end);
				background.setColorAt(0., tone);
				background.setColorAt(1., st::boxBg->c);
				p.fillRect(e->rect().intersected(QRect(0, 0, width(), end)), background);
			}
		} else if (artistHeader) {
			const auto top = _tabScroll->y();
			const auto offset = _tabScroll->scrollTop();
			const auto bannerHeight = artistHeader->artistBannerHeight();
			const auto bannerBottom = top + bannerHeight - offset;
			const auto visible = std::clamp(bannerBottom / double(std::max(top, 1)), 0., 1.);
			const auto panel = QRect(0, 0, width(), top);
			p.save();
			p.setClipRect(panel, Qt::IntersectClip);
			p.setOpacity(visible);
			if (_homeAccent.isValid()) {
				p.fillRect(panel, anim::color(st::boxBg->c, _homeAccent, 0.26));
			}
			artistHeader->paintArtistBanner(p, QPoint(_tabScroll->x(), top - offset));
			if (bannerBottom > 0) {
				p.setOpacity(1.);
				const auto edge = std::min(top, bannerBottom);
				auto shade = QLinearGradient(0, 0, 0, std::max(edge, 1));
				auto topColor = st::boxBg->c;
				topColor.setAlphaF(0.62);
				auto bottomColor = st::boxBg->c;
				bottomColor.setAlphaF(std::clamp(
					(offset + s(24) - bannerHeight) / double(s(24)), 0., 1.));
				shade.setColorAt(0., topColor);
				shade.setColorAt(0.72, topColor);
				shade.setColorAt(1., bottomColor);
				p.fillRect(panel, shade);
			}
			p.restore();
		}
		if (_tab == Tab::Home && _homeAlbumOpen && !_homeArtistOpen) {
			const auto headerWidth = std::max(_tabScroll->width() - s(24), 0);
			const auto headerHeight = (headerWidth < s(480))
				? std::min(std::max(headerWidth - s(48), 0), s(224)) + s(160)
				: s(288);
			const auto top = _tabScroll->y() - _tabScroll->scrollTop();
			const auto bottom = top + s(56) + headerHeight;
			auto gradient = QLinearGradient(0, top, 0, bottom);
			gradient.setColorAt(0., _homeAccent.isValid()
				? _homeAccent.darker(150)
				: QColor(0x32, 0x32, 0x32));
			gradient.setColorAt(1., QColor(0x1c, 0x1c, 0x1c));
			p.fillRect(e->rect(), gradient);
		}
		const auto layout = computeLayout();
		// Одна общая дорожка и движущаяся пилюля вместо отдельных фонов вкладок.
		p.setPen(Qt::NoPen);
		const auto homeColor = (_tab == Tab::Home && _homeAlbumOpen && _homeAccent.isValid())
			? _homeAccent : QColor();
		// На «Сейчас играет» дорожка тоже берёт оттенок обложки.
		const auto trackPlain = _plainAnim.value(_tab == Tab::Now ? 0. : 1.);
		const auto trackTint = homeColor.isValid()
			? homeColor
			: (_accent.isValid() ? _accent : QColor());
		p.setBrush(trackTint.isValid()
			? anim::color(
				anim::color(st::windowBgOver->c, trackTint.darker(220), 0.65),
				st::windowBgOver->c,
				homeColor.isValid() ? 0. : trackPlain)
			: st::windowBgOver->c);
		p.drawRoundedRect(layout.nav, s(20), s(20));
		const auto inset = s(4);
		const auto segment = std::max(layout.nav.width() - 2 * inset, 0) / float64(layout.verticalNav ? 3 : _nav.size());
		const auto at = layout.verticalNav
			? float64(int(_tab) % 3)
			: _navSlide.value(float64(int(_tab)));
		const auto distance = std::abs(float64(int(_tab)) - _navSlideFrom);
		const auto progress = distance > 0.
			? std::clamp(std::abs(at - _navSlideFrom) / distance, 0., 1.)
			: 1.;
		const auto stretch = layout.verticalNav
			? 0.
			: segment * 0.18 * (1. - std::abs(2. * progress - 1.));
		const auto active = QRectF(
			layout.nav.x() + inset + segment * at - stretch / 2.,
			layout.nav.y() + inset + (layout.verticalNav ? s(40) * (int(_tab) / 3) : 0),
			segment + stretch,
			s(40) - 2 * inset);
		const auto coverColor = _accent.isValid()
			? _accent
			: st::windowActiveTextFg->c;
		const auto plain = _plainAnim.value(_tab == Tab::Now ? 0. : 1.);
		p.setBrush(homeColor.isValid() ? homeColor : anim::color(coverColor, st::windowActiveTextFg->c, plain));
		p.drawRoundedRect(active, active.height() / 2., active.height() / 2.);
		// Открытая вкладка лежит на карточке; на «Сейчас играет» обложка и текст прямо на фоне.
		if ((_tab != Tab::Now || showsQueue()) && !(_tab == Tab::Home && _homeAlbumOpen)) {
			if (_tab != Tab::Home && _tab != Tab::Library
				&& _tab != Tab::Playlists && _tab != Tab::Search && !showsQueue()) {
				p.setFont(labelFont(19, true));
				p.setPen(Qt::white);
				p.drawText(
					QRect(layout.center.x() + s(10), layout.center.y() + s(16), layout.center.width() - s(20), s(44)),
					Qt::AlignLeft | Qt::AlignVCenter,
					tabTitle());
			}
			if (!_tabNote.isEmpty()) {
				p.setFont(labelFont(15, false));
				p.setPen(st::windowSubTextFg);
				const auto noteWidth = std::max(layout.center.width() - s(20), 0);
				p.drawText(
					QRect(layout.center.x() + s(10), layout.scrollTop + ((_tab == Tab::Playlists) ? s(64) : (_tab == Tab::Search && _openedAlbum) ? s(40) : 0), noteWidth, s(80)),
					Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
					_tabNote);
			}
			if (_tab == Tab::Search && _searchField->text().trimmed().isEmpty()
				&& !(_searchChats && _searchChat)) {
				const auto area = QRect(
					layout.center.x(), layout.scrollTop,
					layout.center.width(),
					std::max(layout.center.bottom() - layout.scrollTop, 0));
				const auto stickerWidth = std::max(0, std::min({ s(220), area.width(),
					std::max(area.height() - s(72), 0) * 512 / 377 }));
				const auto stickerHeight = stickerWidth * 377 / 512;
				const auto top = area.y() + std::max((area.height() - stickerHeight - s(56)) / 2, 0);
				const auto sticker = QRect(area.center().x() - stickerWidth / 2, top, stickerWidth, stickerHeight);
				if (_searchEmptySticker && _searchEmptySticker->ready() && stickerWidth > 0) {
					const auto request = Media::Clip::FrameRequest{
						.frame = sticker.size(),
						.outer = sticker.size(),
						.factor = style::DevicePixelRatio(),
					};
					if (!_searchEmptySticker->started()) _searchEmptySticker->start(request);
					const auto animate = isVisible() && Mokagram::Perf::UserIsLooking()
						&& !PowerSaving::On(PowerSaving::kAnimations);
					p.drawImage(sticker, _searchEmptySticker->current(request, animate ? crl::now() : 0));
				}
				p.setFont(labelFont(15, false));
				p.setPen(st::windowSubTextFg);
				p.drawText(QRect(area.x() + s(10), sticker.bottom() + s(12),
					std::max(area.width() - s(20), 0), s(44)),
					Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
					QString::fromUtf8(Mokagram::Lang::Tr("Кажется тут ничего нет.. напиши что нибудь в поиск")));
			}

		}

		// Обложка: сжимается на паузе, при смене трека плавно перетекает в новую.
		const auto playing = _playAnim.value(_isPlaying ? 1. : 0.);
		const auto scale = 0.94 + 0.06 * playing;
		const auto fade = _infoFade.value(1.);
		const auto morph = _coverMorph.animating() ? _coverMorph.value(1.) : 1.;
		const auto coverRect = [&] {
			const auto to = QRectF(layout.cover);
			if (morph >= 1. || _morphFrom.isEmpty()) {
				return to;
			}
			const auto from = QRectF(_morphFrom);
			return QRectF(
				from.x() + (to.x() - from.x()) * morph,
				from.y() + (to.y() - from.y()) * morph,
				from.width() + (to.width() - from.width()) * morph,
				from.height() + (to.height() - from.height()) * morph);
		}();
		const auto coverRadius = s(10) + (s(22) - s(10)) * morph;
		const auto drawCover = [&](const QPixmap &pixmap, double opacity, bool current) {
			if (opacity <= 0. || !layout.showRight || _tab != Tab::Now) {
				return;
			}
			p.save();
			p.setOpacity(opacity);
			p.translate(coverRect.center());
			p.scale(scale, scale);
			p.translate(-coverRect.center());
			auto path = QPainterPath();
			path.addRoundedRect(coverRect, coverRadius, coverRadius);
			p.setClipPath(path);
			auto frame = current ? motionFrame(_spotifyCanvas, layout.cover.size()) : QImage();
			if (current) _spotifyPaletteFrame = frame;
			if (!frame.isNull()) {
				p.drawImage(layout.cover, frame);
				p.restore();
				return;
			}
			const auto canvas = current && _canvas.paint(p, layout.cover);
			if (canvas) {
				p.restore();
				return;
			}
			if (current) frame = motionFrame(_motionCover, layout.cover.size());
			if (!frame.isNull()) {
				p.drawImage(layout.cover, frame);
			} else if (pixmap.isNull()) {
				p.fillRect(coverRect, QColor(0x23, 0x23, 0x25));
				st::menuIconSoundOn.paintInCenter(p, layout.cover, QColor(0xa1, 0xa1, 0xa6));
			} else {
				const auto scaled = pixmap.size().scaled(
					layout.cover.size(),
					Qt::KeepAspectRatioByExpanding);
				p.drawPixmap(QRect(
					layout.cover.center() - QPoint(scaled.width(), scaled.height()) / 2,
					scaled), pixmap);
			}
			p.restore();
		};
		p.save();
		const auto closing = _coverClosing.value(0.);
		if (closing > 0. && !layout.cover.isEmpty()) {
			const auto target = QRectF(layout.barCover);
			const auto source = QRectF(layout.cover);
			const auto x = source.x() + (target.x() - source.x()) * closing;
			const auto y = source.y() + (target.y() - source.y()) * closing;
			const auto w = source.width() + (target.width() - source.width()) * closing;
			const auto h = source.height() + (target.height() - source.height()) * closing;
			p.translate(x, y);
			p.scale(w / source.width(), h / source.height());
			p.translate(-source.x(), -source.y());
		}
		drawCover(_coverWas, 1. - fade, false);
		drawCover(_cover, fade, true);
		p.save();
		p.translate(coverRect.center());
		p.scale(scale, scale);
		p.translate(-coverRect.center());
		auto cardPath = QPainterPath();
		cardPath.addRoundedRect(coverRect, coverRadius, coverRadius);
		p.setClipPath(cardPath);
		auto shade = QLinearGradient(coverRect.topLeft(), coverRect.bottomLeft());
		shade.setColorAt(0., QColor(0, 0, 0, 20));
		shade.setColorAt(0.5, QColor(0, 0, 0, 0));
		shade.setColorAt(0.72, QColor(0, 0, 0, 80));
		shade.setColorAt(1., QColor(0, 0, 0, 230));
		p.fillRect(coverRect, shade);
		_canvas.paintLoading(p, QRect(
			layout.cover.x() + s(24),
			layout.cover.y() + s(16),
			std::max(layout.cover.width() - s(48), 0),
			s(4)), _accent);
		p.restore();

		// Название и исполнитель; под курсором подчёркнуты и ведут на их страницы.
		const auto available = infoAvailable(layout);
		const auto drawInfo = [&](
				const QString &title,
				const QString &artist,
				double opacity,
				bool current) {
			if (opacity <= 0. || !layout.showRight || _tab != Tab::Now) {
				return;
			}
			auto font = titleFont();
			font.setUnderline(current && _hover == InfoHover::Title);
			p.setFont(font);
			p.setPen(QColor(255, 255, 255, int(255 * opacity)));
			p.drawText(
				QRect(layout.info.x(), layout.info.y(), available, s(30)),
				Qt::AlignLeft | Qt::AlignVCenter,
				QFontMetrics(font).elidedText(title, Qt::ElideRight, available));
			const auto artistHover = current && _hover == InfoHover::Artist;
			font = artistFont();
			font.setUnderline(artistHover);
			p.setFont(font);
			p.setPen(QColor(255, 255, 255, int((artistHover ? 255 : 210) * opacity)));
			p.drawText(
				QRect(layout.info.x(), layout.info.y() + s(32), available, s(22)),
				Qt::AlignLeft | Qt::AlignVCenter,
				QFontMetrics(font).elidedText(artist, Qt::ElideRight, available));
		};
		drawInfo(_wasTitle, _wasArtist, 1. - fade, false);
		drawInfo(_title, _artist, fade * (1. - closing), true);
		p.restore();
		if (!layout.barShown || _tab == Tab::Home) {
			return;
		}

		paintPlaybackBarBackground(p, layout);
		if (closing > 0. && !layout.cover.isEmpty()) {
			const auto source = QRectF(layout.cover);
			const auto target = QRectF(layout.barCover);
			p.save();
			// Обложка летит в бар уже со скруглением бара, а не квадратом.
			p.setClipPath(Ui::SquirclePath(
				QRectF(layout.barCover),
				s(kBarRadius) - (layout.barCover.y() - layout.bar.y())));
			p.translate(source.x() + (target.x() - source.x()) * closing,
				source.y() + (target.y() - source.y()) * closing);
			p.scale((source.width() + (target.width() - source.width()) * closing) / source.width(),
				(source.height() + (target.height() - source.height()) * closing) / source.height());
			p.translate(-source.x(), -source.y());
			drawCover(_coverWas, 1. - fade, false);
			drawCover(_cover, fade, true);
			p.restore();
		}
		paintPlaybackBarDetails(p, layout);
	}

	[[nodiscard]] static QFont labelFont(int size, bool bold) {
		auto font = (bold ? st::semiboldFont : st::normalFont)->f;
		font.setPixelSize(style::ConvertScale(size));
		return font;
	}

	void paintPlaybackBarBackground(QPainter &p, const Layout &layout) const {
		p.setPen(Qt::NoPen);
		p.fillPath(
			Ui::SquirclePath(QRectF(layout.bar), style::ConvertScale(kBarRadius)),
			playbackBarColor());
	}

	[[nodiscard]] QColor playbackBarColor() const {
		return anim::color(
			anim::color(_barFrom, _barTo, _barAnim.value(1.)),
			QColor(0x2C, 0x34, 0x40),
			_plainAnim.value(_tab == Tab::Now ? 0. : 1.));
	}

	void paintPlaybackBarDetails(QPainter &p, const Layout &layout) const {
		const auto s = [](int v) { return style::ConvertScale(v); };
		// На «Сейчас играет» обложки в капсуле нет: там большая.
		const auto plain = _plainAnim.value(_tab == Tab::Now ? 0. : 1.);
		if (!layout.ultra && plain > 0.) {
			p.save();
			p.setOpacity(plain);
			auto path = QPainterPath();
			path = Ui::SquirclePath(
				QRectF(layout.barCover),
				s(kBarRadius) - (layout.barCover.y() - layout.bar.y()));
			p.setClipPath(path);
			if (_cover.isNull()) {
				p.fillRect(layout.barCover, QColor(0x32, 0x32, 0x32));
			} else {
				p.drawPixmap(layout.barCover, _cover);
			}
			p.restore();
		}
		if (!layout.ultra && !layout.compact && layout.barInfo.width() > 0) {
			// Без маленькой обложки текст сдвигается влево на её место.
			const auto info = barInfoRect(layout);
			const auto infoLeft = info.x();
			const auto infoWidth = info.width();
			auto font = labelFont(16, true);
			font.setUnderline(_hover == InfoHover::BarTitle);
			p.setFont(font);
			p.setPen(Qt::white);
			p.drawText(
				QRect(infoLeft, layout.barInfo.y() + s(13), infoWidth, s(24)),
				Qt::AlignLeft | Qt::AlignVCenter,
				QFontMetrics(font).elidedText(_title, Qt::ElideRight, infoWidth));
			font = labelFont(14, false);
			font.setUnderline(_hover == InfoHover::BarArtist);
			p.setFont(font);
			p.setPen(QColor(255, 255, 255,
				_hover == InfoHover::BarArtist ? 255 : 170));
			p.drawText(
				QRect(infoLeft, layout.barInfo.y() + s(38), infoWidth, s(20)),
				Qt::AlignLeft | Qt::AlignVCenter,
				QFontMetrics(font).elidedText(_artist, Qt::ElideRight, infoWidth));
		}

		// Время по сторонам полосы перемотки.
		p.setFont(labelFont(12, false));
		p.setPen(QColor(255, 255, 255, 140));
		p.drawText(layout.timeNow, Qt::AlignRight | Qt::AlignVCenter, _timeNow);
		p.drawText(layout.timeTotal, Qt::AlignLeft | Qt::AlignVCenter, _timeTotal);
	}

private:
	QImage motionFrame(Media::Clip::ReaderPointer &reader, QSize size) {
		if (!reader || !reader->ready() || size.isEmpty()) return {};
		const auto request = Media::Clip::FrameRequest{
			.frame = QSize(reader->width(), reader->height()).scaled(
				size, Qt::KeepAspectRatioByExpanding),
			.outer = size,
			.factor = style::DevicePixelRatio(),
		};
		if (!reader->started()) reader->start(request);
		const auto animate = _isPlaying && isVisible() && Mokagram::Perf::UserIsLooking()
			&& !PowerSaving::On(PowerSaving::kAnimations);
		return reader->current(request, animate ? crl::now() : 0);
	}

	void setMotion(Media::Clip::ReaderPointer &reader, const QString &path) {
		reader = nullptr;
		if (path.isEmpty()) return;
		const auto target = &reader;
		reader = Media::Clip::MakeReader(path, [=](Media::Clip::Notification) {
			if (*target && (*target)->state() == Media::Clip::State::Error) {
				QFile::remove(path);
				target->setBad();
			}
			if (_tab == Tab::Now) {
				_view->update();
				update(computeLayout().cover);
			}
		});
	}

	QImage canvasBackgroundFrame() const {
		if (_canvasBackground.isNull()) return {};
		auto result = _canvasBackgroundFrom.copy();
		auto painter = QPainter(&result);
		painter.setOpacity(_canvasFade.value(1.));
		painter.drawImage(result.rect(), _canvasBackground);
		return result;
	}

	void setCanvasPalette(QImage background, QColor accent) {
		repaintBackdrop();
		_canvasBackgroundFrom = _canvasBackground.isNull()
			? CoverBackgroundForPlayback() : canvasBackgroundFrame();
		if (_canvasBackgroundFrom.isNull()) {
			_canvasBackgroundFrom = QImage(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
			_canvasBackgroundFrom.fill(QColor(0x14, 0x14, 0x15));
		}
		_canvasBackground = std::move(background);
		_canvasAccentFrom = _accent;
		_canvasAccentTo = accent;
		const auto animate = !anim::Disabled()
			&& !PowerSaving::On(PowerSaving::kAnimations);
		_canvasFade.stop();
		// Ambient colour follows the video slowly, without flashing at scene cuts.
		if (animate) {
			_canvasFade.start([=] {
				repaintAnimatedBackdrop(crl::now(), !_canvasFade.animating());
			},
				0., 1., 2400, anim::easeOutCubic);
		}
		_barFrom = anim::color(_barFrom, _barTo, _barAnim.value(1.));
		_barTo = accent.isValid() ? accent.darker(180) : QColor(0x28, 0x28, 0x28);
		_barAnim.stop();
		if (animate) {
			_barAnim.start([=] { update(computeLayout().bar); },
				0., 1., 2400, anim::easeOutCubic);
		}
	}

	void repaintBackdrop() {
		_backdropDirty = true;
		update();
		_view->update();
	}

	void repaintAnimatedBackdrop(crl::time now, bool finished = false) {
		const auto frame = now / kBackdropFrameInterval;
		if (frame != _animatedBackdropFrame || finished) {
			_animatedBackdropFrame = frame;
			repaintBackdrop();
		}
	}

	void updateBackdropPalette() {
		if (_tab == Tab::Now && crl::now() >= _canvasPaletteNext) {
			_canvasPaletteNext = crl::now() + 3000;
			auto frame = _spotifyPaletteFrame;
			if (frame.isNull()) frame = _canvas.frame();
			if (!frame.isNull()) {
				auto background = Images::BlurLargeImage(
					frame.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
						.convertToFormat(QImage::Format_ARGB32_Premultiplied), 10);
				{
					auto shade = QPainter(&background);
					shade.fillRect(background.rect(), QColor(0x14, 0x14, 0x15, 180));
				}
				setCanvasPalette(std::move(background), ComputeAccent(frame));
			} else if (!_canvasBackground.isNull()) {
				setCanvasPalette(CoverBackgroundForPlayback(), CurrentCoverAccent());
			}
		}
	}

	void paintBackgroundPart(QPainter &p, QRect area, QPoint origin = {}) {
		if (size().isEmpty() || area.isEmpty()) return;
		updateBackdropPalette();
		const auto ratio = devicePixelRatioF();
		const auto pixels = QSize(
			int(std::ceil(width() * ratio)),
			int(std::ceil(height() * ratio)));
		if (_preparedBackdrop.size() != pixels
			|| _preparedBackdrop.devicePixelRatio() != ratio) {
			_preparedBackdrop = QImage(pixels, QImage::Format_RGB32);
			_preparedBackdrop.setDevicePixelRatio(ratio);
			_backdropDirty = true;
		}
		if (_preparedBackdrop.isNull()) {
			p.fillRect(area, QColor(0x14, 0x14, 0x15));
			return;
		}
		if (_backdropDirty) {
			prepareBackdrop();
			_backdropDirty = false;
		}
		p.save();
		p.setClipRect(area, Qt::IntersectClip);
		p.drawImage(-origin, _preparedBackdrop);
		p.restore();
	}

	void prepareBackdrop() {
		if (!_canvasBackground.isNull()) {
			const auto accent = anim::color(
				_canvasAccentFrom, _canvasAccentTo, _canvasFade.value(1.));
			if (_accent != accent) {
				_accent = accent;
				_shuffle->setAccent(_accent);
				_repeat->setAccent(_accent);
			}
		}
		// Keep the blurred backdrop small, but retain colour precision while
		// compositing the gradients so dark transitions do not become steps.
		const auto extent = style::ConvertScale(256);
		const auto rasterSize = size().scaled(
			QSize(extent, extent), Qt::KeepAspectRatio);
		auto background = QImage(rasterSize, QImage::Format_RGBA64_Premultiplied);
		background.fill(QColor(0x14, 0x14, 0x15));
		{
			auto raster = QPainter(&background);
			auto hq = PainterHighQualityEnabler(raster);
			raster.scale(
				rasterSize.width() / double(width()),
				rasterSize.height() / double(height()));
			paintBackdrop(raster);
		}
		// Keep the grain at display size. Translucent black and white approximate
		// SoftLight on the dark backdrop and use the cheaper SourceOver blend.
		static const auto grain = [] {
			const auto extent = style::ConvertScale(128);
			auto image = QImage(extent, extent, QImage::Format_ARGB32_Premultiplied);
			auto random = uint32_t(0x6d2b79f5);
			for (auto y = 0; y != extent; ++y) {
				const auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
				for (auto x = 0; x != extent; ++x) {
					random ^= random << 13;
					random ^= random >> 17;
					random ^= random << 5;
					line[x] = (random & 1) ? qRgba(3, 3, 3, 3) : qRgba(0, 0, 0, 14);
				}
			}
			return QBrush(image);
		}();
		auto p = QPainter(&_preparedBackdrop);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto area = QRectF(QPointF(), _preparedBackdrop.deviceIndependentSize());
		p.drawImage(area, background);
		p.fillRect(area, grain);
	}

	void paintBackdrop(QPainter &p) {
		const auto area = rect();
		if (Mokagram::Settings::Bool(u"player.cover_colors"_q)) {
			if (_canvasBackground.isNull()) {
				_backdrop.paint(p, area, area);
			} else {
				p.drawImage(area, canvasBackgroundFrame());
			}
			p.fillRect(area, QColor(0x10, 0x10, 0x12, 100));
			if (Mokagram::Settings::Bool(u"player.dynamic_background"_q)) {
				const auto phase = crl::now() / 14000.;
				const auto center = QPointF(width() * (0.5 + 0.25 * std::sin(phase)),
					height() * (0.5 + 0.25 * std::cos(phase * 0.7)));
				auto glow = QRadialGradient(center, std::max(width(), height()) * 0.7);
				auto color = _accent;
				color.setAlpha(60);
				glow.setColorAt(0., color);
				color.setAlpha(0);
				glow.setColorAt(1., color);
				p.fillRect(area, glow);
			}
		} else {
			p.fillRect(area, QColor(0x14, 0x14, 0x15));
		}
		const auto layout = computeLayout();
		if (layout.showRight && _tab == Tab::Now && _accent.isValid()) {
			const auto pulse = (_isPlaying
				&& Mokagram::Settings::Bool(u"player.dynamic_background"_q)
				&& !PowerSaving::On(PowerSaving::kAnimations))
				? (0.5 + 0.5 * std::sin(crl::now() / 2200.))
				: 0.;
			const auto cover = QRectF(layout.cover);
			auto glow = QRadialGradient(
				cover.center(),
				std::max(cover.width(), cover.height()) * 0.8);
			auto color = _accent;
			color.setAlpha(int(45 + 15 * pulse));
			glow.setColorAt(0., color);
			color.setAlpha(0);
			glow.setColorAt(1., color);
			p.fillRect(area, glow);
		}
	}

	void refresh() {
		_backdrop.refresh([=] { repaintBackdrop(); }, isVisible());
		const auto track = PlayingTrack();
		if (_canvasPaletteFor != track.id) {
			_canvasPaletteFor = track.id;
			_canvasFade.stop();
			_canvasBackground = QImage();
			_canvasBackgroundFrom = QImage();
			_spotifyPaletteFrame = QImage();
			_canvasPaletteNext = 0;
			repaintBackdrop();
		}
		if (_closing) {
			return;
		}
		if (track.id.isEmpty()) {
			if (_isPlaying) repaintBackdrop();
			_isPlaying = false;
			_seek->setExpressive(true, false);
			_seek->setDuration(0);
			updateAmbientTimer();
			if (_cancelMotion) _cancelMotion();
			_cancelMotion = nullptr;
			_motionFor.clear();
			_motionCover = nullptr;
			_spotifyCanvas = nullptr;
			_canvas.refresh({}, {}, {}, false);
			// Между треками плеер на миг пуст: экран остаётся, закрывает его пользователь.
			return;
		}
		const auto instance = Media::Player::instance();
		const auto current = instance->current(AudioMsgId::Type::Song);
		const auto state = instance->getState(AudioMsgId::Type::Song);
		const auto frequency = std::max<int64>(state.frequency, 1);

		// Обложка могла догрузиться позже трека.
		const auto cover = CoverForPlayback(current);
		const auto coverKey = cover.cacheKey();
		const auto changed = (_title != track.title)
			|| (_artist != track.artist)
			|| (_coverKey != coverKey);
		if (changed) {
			const auto first = _title.isEmpty() && _artist.isEmpty() && _cover.isNull();
			if (!first) {
				_wasTitle = _title;
				_wasArtist = _artist;
				_coverWas = _cover;
				_infoFade.start([=] { update(); }, 0., 1., 320, anim::easeOutCubic);
			}
			_title = track.title;
			_artist = track.artist;
			_coverKey = coverKey;
			const auto size = 512;
			_cover = cover.isNull()
				? QPixmap()
				: QPixmap::fromImage(cover.scaled(
					size,
					size,
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation));
			if (_canvasBackground.isNull()) _accent = CurrentCoverAccent();
			_nav[0]->setActiveColor(Qt::white);
			// Полоса управления плавно перекрашивается под обложку трека.
			{
				const auto now = anim::color(_barFrom, _barTo, _barAnim.value(1.));
				_barFrom = now;
				_barTo = _accent.isValid()
					? _accent.darker(180)
					: QColor(0x28, 0x28, 0x28);
				_barAnim.start([=] { update(); }, 0., 1., 600, anim::easeOutCubic);
			}
			// Перемотка и громкость в оттенке обложки, как у островка.
			_seek->setFillColor(LiftedAccent(_accent));
			_volume->setAccent(_accent.isValid() ? LiftedAccent(_accent) : QColor());
			_shuffle->setAccent(_accent);
			_repeat->setAccent(_accent);
			repaintBackdrop();
		}
		const auto playing = state.state == Media::Player::State::Playing
			|| state.state == Media::Player::State::Starting
			|| state.state == Media::Player::State::Resuming;
		_canvas.refresh(
			Mokagram::Settings::Bool(u"player.public_canvas"_q) && !_spotifyCanvas
				? track.id : QString(),
			track.title, track.artist, playing && _tab == Tab::Now);
		if (_isPlaying != playing) {
			_isPlaying = playing;
			repaintBackdrop();
			updateAmbientTimer();
			_playAnim.start([=] { update(); }, playing ? 0. : 1., playing ? 1. : 0., 180, anim::easeOutCubic);
		}
		_play->setPlaying(playing);
		_seek->setExpressive(true, playing);
		if (_motionFor != track.id) {
			if (_cancelMotion) _cancelMotion();
			_motionFor = track.id;
			_motionCover = nullptr;
			_spotifyCanvas = nullptr;
			const auto id = track.id;
			_cancelMotion = FetchMotion(id, track.title, track.artist, track.durationMs,
				crl::guard(this, [=](MotionFiles files) {
					if (_motionFor != id) return;
					if (Mokagram::Settings::Bool(u"player.motion_covers"_q)) {
						setMotion(_motionCover, files.artwork);
					}
					if (Mokagram::Settings::Bool(u"player.spotify_canvas"_q)) {
						setMotion(_spotifyCanvas, files.canvas);
						if (_spotifyCanvas) _canvas.refresh({}, {}, {}, false);
					}
					update();
				}));
		}
		_heartOwn = !track.id.isEmpty();
		_heart->setVisible(_heartOwn);
		_heart->setLiked(IsLiked(track.id));
		_seek->setProgress(state.length > 0
			? state.position / float64(state.length)
			: 0.);
		_seek->setDuration(state.length * 1000 / frequency);
		const auto now = FormatSeekTime(state.position / frequency);
		const auto total = FormatSeekTime(state.length / frequency);
		const auto nowChanged = (_timeNow != now);
		const auto totalChanged = (_timeTotal != total);
		if (nowChanged || totalChanged) {
			_timeNow = now;
			_timeTotal = total;
			const auto layout = computeLayout();
			auto dirty = QRegion();
			if (nowChanged) dirty += layout.timeNow;
			if (totalChanged) dirty += layout.timeTotal;
			if (_tab == Tab::Home) {
				_playbackBar->update(dirty.translated(-_playbackBar->pos()));
			} else {
				update(dirty);
			}
		}
		_view->setPosition(state.position * 1000 / frequency, playing);
		if (_queueFor != track.id) {
			_queueFor = track.id;
			if (showsQueue()) {
				rebuildTab();
			}
		}
		if (_lyricsFor != track.id) {
			_lyricsFor = track.id;
			if (std::exchange(_lyricsMissing, false) && _tab == Tab::Now) {
				relayout();
				rebuildTab();
			}
			_view->setCommentsTrack(QString());
			ResolveCommentsTrack(track, crl::guard(this, [=, id = track.id](QString sc) {
				if (_lyricsFor == id) _view->setCommentsTrack(sc);
			}));
			_view->setLoading();
			const auto id = track.id;
			FetchLyrics(
				id,
				track.title,
				track.artist,
				track.durationMs,
				crl::guard(this, [=](Lyrics lyrics) {
					if (_lyricsFor == id) {
						_lyricsMissing = !lyrics.found || lyrics.lines.empty();
						_view->setLyrics(std::move(lyrics));
						if (_tab == Tab::Now) {
							relayout();
							rebuildTab();
						}
					}
				}));
		}
	}

	const not_null<Window::SessionController*> _controller;
	const not_null<LyricsView*> _view;
	const not_null<Ui::ScrollArea*> _tabScroll;
	const not_null<Ui::RpWidget*> _playbackBar;
	const not_null<SeekBar*> _seek;
	const not_null<RoundGlyph*> _heart;
	const not_null<RoundGlyph*> _collapse;
	NavItem *_homeBack = nullptr;
	Ui::RpWidget *_homeArtistBar = nullptr;
	QGraphicsOpacityEffect *_homeArtistBarOpacity = nullptr;
	Ui::RoundButton *_homeArtistBarPlay = nullptr;
	const not_null<RoundGlyph*> _filter;
	base::unique_qptr<Ui::PopupMenu> _filterMenu;
	const not_null<RoundGlyph*> _queueToggle;
	const not_null<VolumeControl*> _volume;
	const not_null<Ui::RpWidget*> _tabTransition;
	// Цвет фона страницы на высоте y: плоский или градиент тонировки альбома.
	[[nodiscard]] QColor pageBackgroundAt(int y) const {
		const auto base = QColor(0x1c, 0x1c, 0x1c);
		if (_tab != Tab::Home || !_homeAlbumOpen) {
			return base;
		} else if (_homeArtistOpen) {
			return st::boxBg->c;
		}
		const auto s = [](int v) { return style::ConvertScale(v); };
		const auto headerWidth = std::max(_tabScroll->width() - s(24), 0);
		const auto headerHeight = (headerWidth < s(480))
			? std::min(std::max(headerWidth - s(48), 0), s(224)) + s(160)
			: s(288);
		const auto top = _tabScroll->y() - _tabScroll->scrollTop();
		const auto bottom = top + s(56) + headerHeight;
		const auto tint = _homeAccent.isValid()
			? _homeAccent.darker(150)
			: QColor(0x32, 0x32, 0x32);
		const auto t = std::clamp(
			double(y - top) / std::max(bottom - top, 1),
			0.,
			1.);
		return QColor::fromRgbF(
			tint.redF() + (base.redF() - tint.redF()) * t,
			tint.greenF() + (base.greenF() - tint.greenF()) * t,
			tint.blueF() + (base.blueF() - tint.blueF()) * t);
	}

	Ui::PaddingWrap<Ui::VerticalLayout> *_tabContent = nullptr;
	Ui::VerticalLayout *_tabList = nullptr;
	QPointer<HomeAlbumHeader> _homeAlbumHeader;
	std::array<NavItem*, 6> _nav = {};
	Ui::Animations::Simple _navSlide;
	float64 _navSlideFrom = 0.;
	Ui::Animations::Simple _tabTransitionAnimation;
	QPixmap _tabTransitionWas;
	QPixmap _tabTransitionNow;
	int _tabTransitionDirection = 0;
	Ui::Animations::Simple _homeEntrance;
	QLineEdit *_searchField = nullptr;
	SearchIndicator *_searchIndicator = nullptr;
	QPushButton *_searchRetry = nullptr;
	bool _searchFailed = false;
	bool _searchPending = false;
	std::vector<QWidget*> _searchResultWidgets;
	Ui::RoundButton *_chooseSearchChat = nullptr;
	PeerId _searchChat;
	Segmented *_sourceSwitch = nullptr;
	Segmented *_kindSwitch = nullptr;
	std::vector<Album> _albums;
	std::vector<Track> _albumTracks;
	QString _albumTitle;
	bool _openedAlbum = false;
	bool _albumLoading = false;
	int _searchKind = 0;
	QTimer _searchTimer;
	std::unique_ptr<ChatMusicSearch> _chatSearch;
	std::vector<Track> _searchTracks;
	QString _searchStatus;
	int _searchGen = 0;
	bool _searchChats = false;
	bool _lyricsMissing = false;
	Tab _tab;
	Tab _rebuiltTab = Tab::Queue;
	bool _librarySyncLoading = false;
	crl::time _librarySyncSucceeded = 0;
	bool _playlistsSyncLoading = false;
	crl::time _playlistsSyncSucceeded = 0;
	QString _tabNote;
	QString _openedPlaylist;
	bool _heartOwn = false;
	struct HomePage {
		std::vector<Track> tracks;
		QString artist;
		std::vector<Album> albums;
		std::shared_ptr<int> shelfOffset = std::make_shared<int>(0);
	};
	std::vector<HomePage> _homePages;
	std::vector<Track> _homeTrending;
	Album _homeWeeklyWave;
	Album _homeDailyDrops;
	std::shared_ptr<int> _homeWeeklyWaveOffset = std::make_shared<int>(0);
	std::shared_ptr<int> _homeTrendingOffset = std::make_shared<int>(0);
	std::shared_ptr<int> _homeDailyDropsOffset = std::make_shared<int>(0);
	QString _homeSoundCloudError;
	std::shared_ptr<int> _homeLikedOffset = std::make_shared<int>(0);
	std::set<QString> _homeArtists;
	std::set<QString> _homeAlbumIds;
	std::vector<Track> _homeAlbumTracks;
	std::vector<Track> _homeArtistTracks;
	std::vector<Album> _homeArtistAlbums;
	QString _homeArtistBannerUrl;
	DiscographyFilter _discographyFilter = DiscographyFilter::All;
	std::shared_ptr<int> _discographyOffset = std::make_shared<int>(0);
	bool _discographyExpanded = false;
	// Похожее по звучанию: отдельный ряд от автора играющего трека.
	std::vector<Track> _homeSimilarTracks;
	bool _homeSimilarLoading = false;
	QColor _homeAccent;
	Album _homeAlbum;
	Track _homeArtistSeed;
	QString _homeAlbumNote;
	QTimer _homeMoreRetryTimer;
	int _homeSection = 0;
	int _homeFeedScroll = 0;
	int _homeAlbumReturnScroll = 0;
	int _homeArtistFeedScroll = 0;
	int _homeAlbumGeneration = 0;
	int _homeMoreFailures = 0;
	bool _homeAlbumOpen = false;
	bool _homeArtistOpen = false;
	bool _homeArtistLikesOpen = false;
	bool _homeReturnToArtist = false;
	bool _homeAlbumIsTrack = false;
	bool _homeAlbumLoading = false;
	bool _homeMoreLoading = false;
	bool _homeMoreCheckQueued = false;
	bool _rebuildingTab = false;
	bool _homeLoading = false;
	bool _homeLoaded = false;
	bool _homeSoundCloudLoading = false;
	bool _homeSoundCloudLoaded = false;
	int _homeSoundCloudGeneration = 0;
	std::unique_ptr<ChatMusicSearch> _context;
	PeerId _contextPeer;
	QString _queueFor;
	const not_null<RoundGlyph*> _shuffle;
	const not_null<RoundGlyph*> _previous;
	const not_null<RoundGlyph*> _play;
	const not_null<RoundGlyph*> _next;
	const not_null<RoundGlyph*> _repeat;

	CoverBackground _backdrop;
	CanvasPlayer _canvas;
	QImage _canvasBackground;
	QImage _canvasBackgroundFrom;
	QColor _canvasAccentFrom;
	QColor _canvasAccentTo;
	Ui::Animations::Simple _canvasFade;
	QImage _spotifyPaletteFrame;
	QString _canvasPaletteFor;
	crl::time _canvasPaletteNext = 0;
	QImage _preparedBackdrop;
	bool _backdropDirty = true;
	crl::time _animatedBackdropFrame = -1;
	[[nodiscard]] bool animatesBackdrop() const {
		return _isPlaying && _tab == Tab::Now
			&& Mokagram::Settings::Bool(u"player.dynamic_background"_q);
	}
	void updateAmbientTimer() {
		const auto wave = _seek->waveAnimating();
		const auto needed = !_closing && isVisible()
			&& (animatesBackdrop() || wave)
			&& QGuiApplication::applicationState() == Qt::ApplicationActive
			&& !PowerSaving::On(PowerSaving::kAnimations);
		const auto interval = wave ? kSeekFrameInterval : kBackdropFrameInterval;
		if (needed && (!_ambientTimer.isActive() || _ambientTimer.interval() != interval)) {
			_ambientAt = crl::now();
			_ambientTimer.start(interval);
		} else if (!needed) {
			_ambientTimer.stop();
		}
	}
	QTimer _ambientTimer;
	crl::time _ambientAt = 0;
	Media::Clip::ReaderPointer _motionCover;
	Media::Clip::ReaderPointer _playlistsSticker;
	Media::Clip::ReaderPointer _searchEmptySticker;
	Media::Clip::ReaderPointer _spotifyCanvas;
	QString _motionFor;
	Fn<void()> _cancelMotion;
	QColor _accent;
	Ui::Animations::Simple _fade;
	Ui::Animations::Simple _playAnim;
	Ui::Animations::Simple _coverMorph;
	Ui::Animations::Simple _coverClosing;
	QRect _morphFrom;
	Ui::Animations::Simple _plainAnim;
	Ui::Animations::Simple _barAnim;
	QColor _barFrom = QColor(0x28, 0x28, 0x28);
	QColor _barTo = QColor(0x28, 0x28, 0x28);
	Ui::Animations::Simple _colorAnim;
	Ui::Animations::Simple _infoFade;
	QPixmap _cover;
	QPixmap _coverWas;
	qint64 _coverKey = 0;
	QString _title;
	QString _artist;
	QString _wasTitle;
	QString _wasArtist;
	QString _timeNow = u"0:00"_q;
	QString _timeTotal = u"0:00"_q;
	QString _lyricsFor;
	bool _isPlaying = true;
	bool _closing = false;
	InfoHover _hover = InfoHover::None;

};

void CoverBackground::refresh(Fn<void()> repaint, bool visible) {
	const auto next = CoverBackgroundForPlayback();
	if (_to.cacheKey() == next.cacheKey()) {
		return;
	}
	const auto animate = visible
		&& !_to.isNull()
		&& !anim::Disabled()
		&& QGuiApplication::applicationState() == Qt::ApplicationActive
		&& !PowerSaving::On(PowerSaving::kAnimations);
	_from = animate ? frame() : QImage();
	_to = next;
	_animation.stop();
	if (animate) {
		_animation.start(
			std::move(repaint),
			0.,
			1.,
			kSwitchDuration,
			anim::easeOutCubic);
	} else {
		repaint();
	}
}

QImage CoverBackground::frame() const {
	const auto progress = (anim::Disabled()
		|| PowerSaving::On(PowerSaving::kAnimations))
		? 1.
		: _animation.value(1.);
	if (progress >= 1.) {
		return _to;
	}
	auto result = QImage(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
	result.fill(QColor(0x14, 0x14, 0x15));
	auto p = QPainter(&result);
	if (!_from.isNull()) {
		p.drawImage(result.rect(), _from);
	}
	p.setOpacity(progress);
	if (_to.isNull()) {
		p.fillRect(result.rect(), QColor(0x14, 0x14, 0x15));
	} else {
		p.drawImage(result.rect(), _to);
	}
	return result;
}

void CoverBackground::paint(QPainter &p, QRect area, QRect canvas) const {
	p.fillRect(area, QColor(0x14, 0x14, 0x15));
	const auto background = frame();
	if (background.isNull()) {
		return;
	}
	p.save();
	p.setClipRect(area, Qt::IntersectClip);
	p.drawImage(canvas, background);
	p.restore();
}

[[nodiscard]] QPointer<FullscreenPlayer> &FullscreenInstance() {
	static auto result = QPointer<FullscreenPlayer>();
	return result;
}

void ShowFullscreenPlayerAt(
		not_null<Window::SessionController*> controller,
		bool openHome) {
	BindController(controller);
	auto &instance = FullscreenInstance();
	if (instance && !instance->canReuse(controller)) {
		instance->hide();
		instance->close();
		instance.clear();
	}
	if (!openHome && !instance && PlayingTrack().id.isEmpty()) {
		return;
	}
	// A desktop island can open this screen while the app is inactive or hidden.
	const auto window = controller->widget();
	window->activate();
	if (instance) {
		if (openHome) {
			instance->showHome();
		}
		instance->show();
		instance->raise();
		instance->setFocus();
	} else {
		instance = Ui::CreateChild<FullscreenPlayer>(
			window->bodyWidget(),
			controller,
			openHome);
	}
	window->fixOrder();
}

// Есть ли текст у играющего трека (для состояния кнопки в панели плеера).
struct LyricsAvailability {
	QString id;
	bool found = false;
	bool started = false;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] LyricsAvailability &Availability() {
	static auto result = new LyricsAvailability();
	return *result;
}

void RefreshAvailability() {
	auto &state = Availability();
	const auto track = PlayingTrack();
	if (state.id == track.id) {
		return;
	}
	state.id = track.id;
	state.found = false;
	state.changes.fire({});
	if (track.id.isEmpty()) {
		return;
	}
	const auto id = track.id;
	FetchLyrics(
		id,
		track.title,
		track.artist,
		track.durationMs,
		[=](Lyrics lyrics) {
			auto &state = Availability();
			if (state.id == id && state.found != lyrics.found) {
				state.found = lyrics.found;
				state.changes.fire({});
			}
		});
	// Обложка SC имеет приоритет; прежний поиск остаётся запасным вариантом.
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto document = current.audio();
	if (!document
		|| !IsTelegramTrack(track)
		|| !CoverForDocument(document->id).isNull()) {
		return;
	}
	const auto documentId = document->id;
	LoadLocalArtwork(track, [=](QImage image) {
		if (!image.isNull()) {
			SetCover(documentId, std::move(image));
			return;
		}
		FetchArtwork(
			id,
			track.title,
			track.artist,
			track.durationMs,
			[=](QImage fallback) {
				if (!fallback.isNull()) {
					SetCover(documentId, std::move(fallback));
				}
			});
	});
}

} // namespace

void SyncLyricsPanel(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor) {
	auto &panel = LyricsPanelInstance();
	if (!Mokagram::Settings::Bool(u"lyrics.enabled"_q)) {
		if (panel) {
			panel->deleteLater();
			panel = nullptr;
		}
		if (base::take(ThirdColumnOpenedForLyrics())) {
			controller->closeThirdSection();
		}
		return;
	}
	if (!controller->adaptive().isThreeColumn()) {
		ThirdColumnOpenedForLyrics() = false;
		Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
		return;
	}
	const auto parent = not_null<Ui::RpWidget*>(controller->content());
	if (panel && panel->parentWidget() != parent.get()) {
		panel->deleteLater();
		panel = nullptr;
	}
	if (!panel) {
		panel = Ui::CreateChild<LyricsPanel>(parent.get(), controller, anchor);
	}
}

void AttachCommentsToPlayer(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> anchor,
		bool compactTimeline,
		Fn<bool()> available) {
	Settings::PreloadRowIcons(&controller->session());
	const auto button = Ui::CreateChild<TimedComments>(parent);
	const auto refresh = [=] {
		const auto track = PlayingTrack();
		const auto allowed = !available || available();
		const auto enabled = allowed && Settings::Bool(u"lyrics.comments"_q) && !LyricsAvailable();
		const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
		button->setTimelineDuration(std::max<qint64>(track.durationMs,
			state.length * 1000 / std::max<int64>(state.frequency, 1)));
		button->setTrack(enabled ? track.id : QString());
		const auto shown = enabled && button->validTrack() && button->avatarCount() > 0
			&& !anchor->isHidden();
		button->setVisible(shown);
		const auto size = style::ConvertScale(compactTimeline ? 24 : 28);
		const auto anchorTop = anchor->mapTo(parent, QPoint());
		button->setGeometry(anchorTop.x(),
			std::clamp(anchorTop.y() + (anchor->height() - size) / 2,
				0, std::max(0, parent->height() - size)), anchor->width(), size);
		button->setPosition(state.position * 1000 / std::max<int64>(state.frequency, 1));
		button->raise();
	};
	button->changes() | rpl::on_next(refresh, button->lifetime());
	button->seekRequests() | rpl::on_next([](qint64 ms) {
		const auto instance = Media::Player::instance();
		const auto state = instance->getState(AudioMsgId::Type::Song);
		const auto length = state.length * 1000 / std::max<int64>(state.frequency, 1);
		if (length <= 0) return;
		instance->startSeeking(AudioMsgId::Type::Song);
		instance->finishSeeking(AudioMsgId::Type::Song, std::clamp(double(ms) / length, 0., 1.));
	}, button->lifetime());
	Media::Player::instance()->updatedNotifier() | rpl::on_next(refresh, button->lifetime());
	QueueChanges() | rpl::on_next(refresh, button->lifetime());
	LyricsAvailabilityChanges() | rpl::on_next(refresh, button->lifetime());
	Settings::Changed() | rpl::on_next([=](const QString &key) {
		if (key == u"lyrics.comments"_q) refresh();
	}, button->lifetime());
	anchor->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Move || event->type() == QEvent::Resize
			|| event->type() == QEvent::Show || event->type() == QEvent::Hide) refresh();
	}, button->lifetime());
	parent->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Resize || event->type() == QEvent::Show) refresh();
	}, button->lifetime());
	crl::on_main(button, refresh);
}

bool LyricsAvailable() {
	return Availability().found;
}

rpl::producer<> LyricsAvailabilityChanges() {
	auto &state = Availability();
	if (!state.started) {
		state.started = true;
		Media::Player::instance()->updatedNotifier(
		) | rpl::on_next(RefreshAvailability, state.lifetime);
		QueueState().changes.events(
		) | rpl::on_next(RefreshAvailability, state.lifetime);
		RefreshAvailability();
	}
	return state.changes.events();
}

void ToggleLyricsPanel(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor) {
	// Панель стоит на месте третьей колонки: если её нет, открываем (как кнопка
	// «Информация» в шапке чата) и при закрытии панели закроем обратно.
	if (!Mokagram::Settings::Bool(u"lyrics.enabled"_q)
		&& !controller->adaptive().isThreeColumn()
		&& controller->canShowThirdSection()) {
		Core::App().settings().setThirdSectionInfoEnabled(true);
		Core::App().saveSettingsDelayed();
		controller->resizeForThirdSection();
		controller->updateColumnLayout();
		ThirdColumnOpenedForLyrics() = true;
	}
	Mokagram::Settings::SetBool(
		u"lyrics.enabled"_q,
		!Mokagram::Settings::Bool(u"lyrics.enabled"_q));
	SyncLyricsPanel(controller, anchor);
}

void ShowFullscreenPlayer(not_null<Window::SessionController*> controller) {
	ShowFullscreenPlayerAt(controller, false);
}

void ShowFullscreenPlayerHome(not_null<Window::SessionController*> controller) {
	ShowFullscreenPlayerAt(controller, true);
}

void DebugGrabFullscreen(const QString &path) {
	if (const auto widget = FullscreenInstance().data()) {
		widget->grab().save(path);
	}
}

QImage CoverForPlayback(const AudioMsgId &current) {
	const auto document = current.audio();
	if (!document) {
		return {};
	} else if (OwnsTrack(current)) {
		return CoverForDocument(document->id);
	} else if (auto hq = CoverForDocument(document->id); !hq.isNull()) {
		return hq;
	}
	if (const auto media = document->activeMediaView()) {
		if (const auto image = media->thumbnail()) {
			return image->original();
		} else if (const auto image = media->thumbnailInline()) {
			return image->original();
		}
	}
	return {};
}

QColor CurrentCoverAccent() {
	if (!Mokagram::Settings::Bool(u"player.cover_colors"_q)) {
		return QColor();
	}
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	if (!current.audio()) {
		return QColor();
	}
	static auto cachedKey = qint64();
	static auto cached = QColor();
	const auto cover = CoverForPlayback(current);
	if (cover.isNull()) {
		return QColor();
	}
	if (cachedKey != cover.cacheKey() || !cached.isValid()) {
		cachedKey = cover.cacheKey();
		cached = ComputeAccent(cover);
	}
	return cached;
}

bool OwnsTrack(const AudioMsgId &id) {
	return id.audio()
		&& (id.audio()->id & 0xFFFFFF0000000000ULL) == 0x4D4F4B0000000000ULL;
}

bool UsesQueue(const AudioMsgId &id) {
	const auto &queue = QueueState();
	if (queue.currentId.isEmpty() || !id.audio()
		|| &id.audio()->session() != queue.session.get()) {
		return false;
	}
	if (OwnsTrack(id)) {
		return !queue.currentId.startsWith(u"telegram:"_q)
			&& id.audio()->id == DocumentIdForTrack(queue.currentId);
	}
	const auto i = queue.messages.find(queue.currentId);
	return i != queue.messages.end()
		&& id.contextId() == i->second;
}

bool NextAvailable() {
	const auto &queue = QueueState();
	const auto &settings = Core::App().settings();
	return !queue.tracks.empty()
		&& (queue.index + 1 < int(queue.tracks.size())
			|| (queue.radio && !queue.radio->exhausted)
			|| (!queue.radio && ((queue.stream && StreamEnabled())
				|| settings.playerRepeatMode() == Media::RepeatMode::All
				|| settings.playerOrderMode() == Media::OrderMode::Shuffle)));
}

bool PreviousAvailable() {
	const auto &queue = QueueState();
	return !queue.tracks.empty()
		&& (queue.index > 0
			|| (!queue.radio && Core::App().settings().playerRepeatMode()
				== Media::RepeatMode::All));
}

[[nodiscard]] bool FeedbackAllowed() {
	const auto &queue = QueueState();
	return !queue.current.id.isEmpty()
		&& !IsTelegramTrack(queue.current)
		&& OwnsTrack(Media::Player::instance()->current(AudioMsgId::Type::Song));
}

// «Больше такого»: усиливает вкус и пересобирает предстоящие авто-треки.
void MoreLikeThis() {
	if (!FeedbackAllowed()) {
		return;
	}
	auto &queue = QueueState();
	BoostTrack(queue.current);
	if (queue.stream && StreamEnabled()) {
		DropUpcoming([](const Track &) { return true; }, queue.index + 1, true);
		++queue.streamEpoch;
		queue.refilling = false;
		RefillStream();
	}
	if (const auto controller = queue.controller.get()) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Учту: больше такого")));
	}
}

// «Не нравится автор»: в чёрный список, из очереди убираем, переключаем дальше.
void DislikeArtist() {
	if (!FeedbackAllowed()) {
		return;
	}
	auto &queue = QueueState();
	const auto track = queue.current;
	BlockArtist(track);
	DropUpcoming([=](const Track &t) {
		return IsArtistBlocked(t);
	}, queue.index, false);
	if (const auto controller = queue.controller.get()) {
		controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Автор скрыт: ")) + track.artist);
	}
	if (NextAvailable()) {
		Step(1);
	}
}

void Step(int delta) {
	auto &queue = QueueState();
	const auto count = int(queue.tracks.size());
	if (!count) {
		return;
	}
	auto target = queue.index + delta;
	if (delta > 0 && (queue.radio || (queue.stream && StreamEnabled()))) {
		if (target >= count) {
			queue.waiting = true;
			RefillStream();
			return;
		}
	} else if (delta > 0
		&& Core::App().settings().playerOrderMode()
			== Media::OrderMode::Shuffle
		&& count > 1) {
		target = queue.index;
		while (target == queue.index) {
			target = base::RandomIndex(count);
		}
	} else if (target >= count) {
		target = 0;
	} else if (target < 0) {
		if (queue.radio) {
			return;
		}
		target = count - 1;
	}
	PlayIndex(target);
}

rpl::producer<> QueueChanges() {
	return QueueState().changes.events();
}

bool CurrentTrackInfo(QString &title, QString &artist) {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	if (!OwnsTrack(current)) {
		return false;
	}
	const auto track = PlayingTrack();
	title = track.title;
	artist = track.artist;
	return true;
}

// ---- Слушать вместе: хозяин пишет состояние плеера в комнату бота, гости его читают ----

struct Together {
	QString code;
	bool host = false;
	int lastVer = -1;
	QString lastKey;
	base::Timer timer;
	bool busy = false;
	int listeners = 0;
	int lastEvent = 0;
	bool eventsSeen = false;
	bool eventsBusy = false;
	int tickCount = 0;
	// За каким ключом трека мы идём и под каким id он играет у нас.
	QString followKey;
	QString followId;
	bool resolving = false;
	// Окно комнаты.
	std::shared_ptr<Ui::RpWindow> window;
	Ui::VerticalLayout *log = nullptr;
	Ui::ScrollArea *logScroll = nullptr;
	Ui::FlatLabel *people = nullptr;
	Ui::FlatLabel *title = nullptr;
};

[[nodiscard]] Together &TogetherState();
void TogetherLog(const QString &line);

constexpr auto kTogetherPollMs = 700;
constexpr auto kTogetherDriftMs = 700;

// Сообщает, когда число слушателей изменилось (первый раз молча).
void TogetherNoteListeners(int count) {
	auto &t = TogetherState();
	if (count <= 0 || count == t.listeners) {
		return;
	}
	const auto first = (t.listeners == 0);
	t.listeners = count;
	if (first) {
		return;
	}
	TogetherLog(QString::fromUtf8(Mokagram::Lang::Tr("Слушают вместе: "))
		+ QString::number(count));
}

[[nodiscard]] Together &TogetherState() {
	static auto result = Together();
	return result;
}

[[nodiscard]] bool TogetherActive() {
	return !TogetherState().code.isEmpty();
}

void TogetherStop() {
	auto &t = TogetherState();
	t.timer.cancel();
	t.code.clear();
	t.host = false;
	t.lastVer = -1;
	t.lastKey.clear();
	t.busy = false;
	t.listeners = 0;
	t.lastEvent = 0;
	t.eventsSeen = false;
	t.eventsBusy = false;
	t.tickCount = 0;
	t.followKey.clear();
	t.followId.clear();
	t.resolving = false;
	t.log = nullptr;
	t.logScroll = nullptr;
	t.people = nullptr;
	t.title = nullptr;
	if (const auto window = std::move(t.window)) {
		// Окно удаляем уже вне обработчика его кнопки.
		window->hide();
		crl::on_main([window] {});
	}
}

// Что сейчас играет у нас: ключ трека, позиция мс, играет ли. false: слать нечего.
[[nodiscard]] bool TogetherLocalState(QString &key, qint64 &position, bool &playing) {
	const auto &track = QueueState().current;
	if (track.id.isEmpty()) {
		return false;
	}
	const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
	if (IsTelegramTrack(track)) {
		// Файл из чата у других недоступен: передаём название, исполнителя и длительность.
		const auto meta = track.title.left(40).toUtf8() + '\x1f'
			+ track.artist.left(30).toUtf8() + '\x1f'
			+ QByteArray::number(track.durationMs);
		key = u"m:"_q + QString::fromLatin1(meta.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	} else {
		key = track.id;
	}
	position = state.position * 1000 / std::max<int64>(state.frequency, 1);
	playing = !Media::Player::IsPausedOrPausing(state.state)
		&& !Media::Player::IsStoppedOrStopping(state.state);
	return true;
}

void TogetherHostTick() {
	auto &t = TogetherState();
	QString key;
	qint64 position = 0;
	bool playing = false;
	if (!TogetherActive() || !t.host || t.busy
		|| !TogetherLocalState(key, position, playing)) {
		return;
	}
	t.busy = true;
	const auto code = t.code;
	Mokagram::Bot::Ask(
		u"room:put:%1:%2|%3|%4"_q.arg(code).arg(key).arg(position).arg(playing ? 1 : 0),
		[=](Mokagram::Bot::Results results) {
			TogetherState().busy = false;
			if (!results.empty()) {
				TogetherNoteListeners(results.front().description.toInt());
			}
		},
		[=] { TogetherState().busy = false; });
}

void TogetherGuestApply(const QString &reply, qint64 roundTripMs) {
	auto &t = TogetherState();
	const auto parts = reply.split('|');
	if (parts.size() < 5) {
		return;
	}
	if (parts.size() > 5) {
		TogetherNoteListeners(parts[5].toInt());
	}
	const auto key = parts[1];
	auto position = parts[2].toLongLong();
	const auto playing = (parts[3] == u"1"_q);
	if (playing) {
		position += roundTripMs / 2;
	}
	auto &queue = QueueState();
	if (key.isEmpty() || key.startsWith(u"telegram:"_q)) {
		return;
	}
	const auto instance = Media::Player::instance();
	if (t.followKey != key) {
		// Другой трек: находим и запускаем у себя, подстроимся на следующем шаге.
		if (t.resolving) {
			return;
		}
		t.followKey = key;
		t.followId.clear();
		if (!key.startsWith(u"m:"_q)) {
			// Ключ комнаты превращается в id трека: берём только числа,
			// всё остальное могло прийти от чужого бота.
			const auto trackId = key.toULongLong();
			if (trackId == 0 || QString::number(trackId) != key) {
				return;
			}
			t.followId = key;
			auto track = Track();
			track.id = key;
			PlayAll({ track });
			return;
		}
		const auto meta = QByteArray::fromBase64(
			key.mid(2).toLatin1(),
			QByteArray::Base64UrlEncoding).split('\x1f');
		if (meta.size() < 3) {
			return;
		}
		const auto title = QString::fromUtf8(meta[0]);
		const auto artist = QString::fromUtf8(meta[1]);
		const auto duration = meta[2].toInt();
		t.resolving = true;
		const auto wanted = key;
		Search((artist + u" "_q + title).trimmed(), 0, [=](SearchResult result) {
			auto &t = TogetherState();
			t.resolving = false;
			if (t.followKey != wanted || !TogetherActive()) {
				return;
			}
			auto best = -1;
			auto bestDiff = 0;
			for (auto i = 0; i != int(result.tracks.size()); ++i) {
				const auto diff = std::abs(result.tracks[i].durationMs - duration);
				if (best < 0 || diff < bestDiff) {
					best = i;
					bestDiff = diff;
				}
			}
			if (best < 0) {
				TogetherLog(QString::fromUtf8(Mokagram::Lang::Tr("Не нашёл трек: "))
					+ artist + u" - "_q + title);
				return;
			}
			t.followId = result.tracks[best].id;
			PlayAll({ result.tracks[best] });
		});
		return;
	}
	if (t.followId.isEmpty() || queue.currentId != t.followId) {
		return;
	}
	const auto state = instance->getState(AudioMsgId::Type::Song);
	const auto ours = state.position * 1000 / std::max<int64>(state.frequency, 1);
	const auto weArePlaying = !Media::Player::IsPausedOrPausing(state.state)
		&& !Media::Player::IsStoppedOrStopping(state.state);
	if (playing && !weArePlaying && !Media::Player::IsStoppedOrStopping(state.state)) {
		instance->play(AudioMsgId::Type::Song);
	} else if (!playing && weArePlaying) {
		instance->pause(AudioMsgId::Type::Song);
	}
	// Расхождение больше порога: перематываем к позиции хозяина.
	if (state.length > 0 && std::abs(ours - position) > kTogetherDriftMs) {
		const auto total = state.length * 1000 / std::max<int64>(state.frequency, 1);
		if (total > 0) {
			instance->finishSeeking(
				AudioMsgId::Type::Song,
				std::clamp(position / float64(total), 0., 1.));
		}
	}
}

void TogetherGuestTick() {
	auto &t = TogetherState();
	if (!TogetherActive() || t.host || t.busy) {
		return;
	}
	t.busy = true;
	const auto code = t.code;
	const auto sentAt = crl::now();
	Mokagram::Bot::Ask(
		u"room:get:%1"_q.arg(code),
		[=](Mokagram::Bot::Results results) {
			auto &t = TogetherState();
			t.busy = false;
			if (!TogetherActive() || t.code != code) {
				return;
			}
			if (results.empty() || results.front().title == u"no"_q) {
				if (const auto controller = QueueState().controller.get()) {
					controller->showToast(QString::fromUtf8(
						Mokagram::Lang::Tr("Комната закрыта")));
				}
				TogetherStop();
				return;
			}
			TogetherGuestApply(results.front().title, crl::now() - sentAt);
		},
		[=] { TogetherState().busy = false; });
}

// События комнаты (реакции и сообщения): забираем новые раз в несколько опросов.
void TogetherPollEvents() {
	auto &t = TogetherState();
	if (!TogetherActive() || t.eventsBusy) {
		return;
	}
	t.eventsBusy = true;
	const auto code = t.code;
	Mokagram::Bot::Ask(
		u"room:ev:%1:%2"_q.arg(code).arg(t.lastEvent),
		[=](Mokagram::Bot::Results results) {
			auto &t = TogetherState();
			t.eventsBusy = false;
			if (!TogetherActive() || t.code != code) {
				return;
			}
			const auto show = t.eventsSeen;
			t.eventsSeen = true;
			for (const auto &item : results) {
				const auto parts = item.title.split('|');
				if (parts.size() < 4) {
					continue;
				}
				const auto id = parts[0].toInt();
				if (id <= t.lastEvent) {
					continue;
				}
				t.lastEvent = id;
				if (!show) {
					continue;
				}
				const auto body = parts.mid(3).join('|');
				const auto text = (parts[1] == u"r"_q)
					? parts[2] + u" "_q + body
					: parts[2] + u": "_q + body;
				TogetherLog(text);
			}
		},
		[=] { TogetherState().eventsBusy = false; });
}

void TogetherSay(
		not_null<Window::SessionController*> controller,
		const QString &kind,
		QString body) {
	body = body.simplified().left(120);
	if (!TogetherActive() || body.isEmpty()) {
		return;
	}
	Mokagram::Bot::Ask(
		u"room:say:%1:%2:%3"_q.arg(TogetherState().code).arg(kind).arg(body),
		[=](Mokagram::Bot::Results) {
			// Свои события приходят обратно через опрос, отдельно не показываем.
			TogetherPollEvents();
		},
		[=] {
			controller->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Не удалось отправить")));
		});
}

void TogetherWho(not_null<Window::SessionController*> controller) {
	if (!TogetherActive()) {
		return;
	}
	const auto weak = base::make_weak(controller);
	Mokagram::Bot::Ask(u"room:who:%1"_q.arg(TogetherState().code), [=](Mokagram::Bot::Results results) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto names = QStringList();
		for (const auto &item : results) {
			if (item.title == u"none"_q) {
				continue;
			}
			names.push_back(item.title.startsWith('*')
				? item.title.mid(1)
					+ QString::fromUtf8(Mokagram::Lang::Tr(" (хозяин)"))
				: item.title);
		}
		strong->show(Ui::MakeInformBox(
			names.isEmpty()
				? QString::fromUtf8(Mokagram::Lang::Tr("В комнате пока никого не видно"))
				: names.join('\n')));
	}, [=] {
		if (const auto strong = weak.get()) {
			strong->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Не удалось получить список")));
		}
	});
}

void TogetherChatBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8(
			Mokagram::Lang::Tr("Сообщение в комнату"))));
		const auto input = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Текст")))));
		box->setFocusCallback([=] { input->setFocusFast(); });
		const auto submit = [=] {
			TogetherSay(controller, u"m"_q, input->getLastText());
			box->closeBox();
		};
		input->submits() | rpl::on_next(submit, input->lifetime());
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отправить"))),
			submit);
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отмена"))),
			[=] { box->closeBox(); });
	}));
}

void TogetherLog(const QString &line) {
	auto &t = TogetherState();
	if (t.log && t.logScroll) {
		t.log->add(
			object_ptr<Ui::FlatLabel>(t.log, line, st::boxLabel),
			QMargins(
				style::ConvertScale(12),
				style::ConvertScale(4),
				style::ConvertScale(12),
				style::ConvertScale(4)));
		const auto scroll = t.logScroll;
		Ui::PostponeCall(scroll, [=] {
			scroll->scrollToY(scroll->scrollTopMax());
		});
		return;
	}
	if (const auto controller = QueueState().controller.get()) {
		controller->showToast(line);
	}
}

void TogetherRefreshPeople() {
	auto &t = TogetherState();
	if (!TogetherActive() || !t.people) {
		return;
	}
	Mokagram::Bot::Ask(u"room:who:%1"_q.arg(t.code), [](Mokagram::Bot::Results results) {
		auto &t = TogetherState();
		if (!t.people) {
			return;
		}
		auto names = QStringList();
		for (const auto &item : results) {
			if (item.title == u"none"_q) {
				continue;
			}
			names.push_back(item.title.startsWith('*')
				? item.title.mid(1)
					+ QString::fromUtf8(Mokagram::Lang::Tr(" (хозяин)"))
				: item.title);
		}
		t.people->setText(
			QString::fromUtf8(Mokagram::Lang::Tr("Слушают ("))
			+ QString::number(names.size()) + u"): "_q + names.join(u", "_q));
	});
}

void TogetherOpenWindow(not_null<Window::SessionController*> controller) {
	auto &t = TogetherState();
	if (!TogetherActive()) {
		return;
	}
	if (t.window) {
		t.window->showNormal();
		t.window->raise();
		t.window->activateWindow();
		return;
	}
	const auto s = [](int v) { return style::ConvertScale(v); };
	t.window = std::make_shared<Ui::RpWindow>();
	const auto window = t.window.get();
	window->setTitle(QString::fromUtf8(Mokagram::Lang::Tr("Комната ")) + t.code);
	window->setMinimumSize(QSize(s(340), s(480)));
	window->setGeometry(QRect(120, 120, s(380), s(580)));
	const auto body = window->body();
	body->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(body).fillRect(clip, QColor(0x1c, 0x1c, 0x1c));
	}, body->lifetime());

	const auto head = Ui::CreateChild<Ui::FlatLabel>(
		body,
		QString::fromUtf8(Mokagram::Lang::Tr("Код комнаты: ")) + t.code,
		st::boxLabel);
	t.title = head;
	t.people = Ui::CreateChild<Ui::FlatLabel>(body, QString(), st::boxLabel);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(body, st::defaultScrollArea);
	t.logScroll = scroll;
	t.log = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll));
	const auto input = Ui::CreateChild<Ui::InputField>(
		body,
		st::defaultInputField,
		rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Сообщение"))));
	const auto send = Ui::CreateChild<Ui::RoundButton>(
		body,
		rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отправить"))),
		st::defaultActiveButton);
	const auto copy = Ui::CreateChild<Ui::RoundButton>(
		body,
		rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Скопировать код"))),
		st::defaultLightButton);
	const auto leave = Ui::CreateChild<Ui::RoundButton>(
		body,
		rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Выйти"))),
		st::defaultLightButton);
	auto reactions = std::vector<Ui::RoundButton*>();
	for (const auto emoji : { "🔥", "❤️", "😂", "👏" }) {
		const auto text = QString::fromUtf8(emoji);
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			body,
			rpl::single(text),
			st::defaultLightButton);
		button->setClickedCallback([=] {
			TogetherSay(controller, u"r"_q, text);
		});
		reactions.push_back(button);
	}
	const auto submit = [=] {
		TogetherSay(controller, u"m"_q, input->getLastText());
		input->setText(QString());
	};
	input->submits() | rpl::on_next(submit, input->lifetime());
	send->setClickedCallback(submit);
	copy->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(TogetherState().code);
		TogetherLog(QString::fromUtf8(Mokagram::Lang::Tr("Код скопирован")));
	});
	leave->setClickedCallback([=] {
		TogetherStop();
	});

	body->sizeValue() | rpl::on_next([=](QSize size) {
		const auto pad = s(14);
		const auto width = size.width() - 2 * pad;
		auto y = pad;
		head->resizeToWidth(width);
		head->moveToLeft(pad, y);
		y += head->height() + s(6);
		TogetherState().people->resizeToWidth(width);
		TogetherState().people->moveToLeft(pad, y);
		y += TogetherState().people->height() + s(10);
		const auto bottom = s(44) * 3 + s(24);
		scroll->setGeometry(pad, y, width, std::max(size.height() - y - bottom, s(80)));
		scroll->updateBars();
		auto by = size.height() - pad - s(40);
		// Нижний ряд: копировать код и выйти.
		copy->moveToLeft(pad, by);
		leave->moveToRight(pad, by, size.width());
		by -= s(44);
		// Поле ввода и «Отправить».
		send->moveToRight(pad, by, size.width());
		input->resizeToWidth(width - send->width() - s(8));
		input->moveToLeft(pad, by + s(2));
		by -= s(44);
		// Ряд реакций.
		auto x = pad;
		for (const auto button : reactions) {
			button->moveToLeft(x, by);
			x += button->width() + s(6);
		}
		TogetherState().log->resizeToWidth(width);
	}, body->lifetime());

	window->show();
	TogetherRefreshPeople();
}

void TogetherStart(not_null<Window::SessionController*> controller, QString code, bool host) {
	BindController(controller);
	Subscribe();
	TogetherStop();
	auto &t = TogetherState();
	t.code = code;
	t.host = host;
	t.timer.setCallback([] {
		auto &t = TogetherState();
		if (++t.tickCount % 3 == 0) {
			TogetherPollEvents();
		}
		if (t.tickCount % 9 == 0) {
			TogetherRefreshPeople();
		}
		if (TogetherState().host) {
			TogetherHostTick();
		} else {
			TogetherGuestTick();
		}
	});
	t.timer.callEach(kTogetherPollMs);
	if (host) {
		TogetherHostTick();
	} else {
		TogetherGuestTick();
	}
	TogetherOpenWindow(controller);
}

void TogetherCreate(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	Mokagram::Bot::Ask(u"room:new"_q, [=](Mokagram::Bot::Results results) {
		const auto strong = weak.get();
		if (!strong || results.empty() || results.front().title.size() != 6) {
			if (strong) {
				strong->showToast(QString::fromUtf8(
					Mokagram::Lang::Tr("Не удалось создать комнату")));
			}
			return;
		}
		const auto code = results.front().title;
		TogetherStart(strong, code, true);
		QGuiApplication::clipboard()->setText(code);
		strong->showToast(QString::fromUtf8(
			Mokagram::Lang::Tr("Код комнаты скопирован: ")) + code);
	}, [=] {
		if (const auto strong = weak.get()) {
			strong->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Не удалось создать комнату")));
		}
	});
}

void TogetherJoinBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8(
			Mokagram::Lang::Tr("Войти в комнату"))));
		const auto input = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Код комнаты")))));
		box->setFocusCallback([=] { input->setFocusFast(); });
		const auto submit = [=] {
			const auto code = input->getLastText().trimmed().toUpper();
			if (code.size() != 6) {
				input->showError();
				return;
			}
			TogetherStart(controller, code, false);
			box->closeBox();
		};
		input->submits() | rpl::on_next(submit, input->lifetime());
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Войти"))),
			submit);
		box->addButton(
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Отмена"))),
			[=] { box->closeBox(); });
	}));
}

void AddTogetherActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller) {
	const auto add = [&](const QString &text, Fn<void()> callback, const style::icon *icon) {
		menu->addAction(text, std::move(callback), icon);
	};
	if (TogetherActive()) {
		const auto code = TogetherState().code;
		const auto listeners = TogetherState().listeners;
		add(QString::fromUtf8(Mokagram::Lang::Tr("Окно комнаты")), [=] {
			TogetherOpenWindow(controller);
		}, &st::menuIconChats);
		add(QString::fromUtf8(Mokagram::Lang::Tr("Скопировать код комнаты ")) + code, [=] {
			QGuiApplication::clipboard()->setText(code);
			controller->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Код скопирован")));
		}, &st::menuIconLink);
		for (const auto emoji : { "🔥", "❤️", "😂", "👏" }) {
			const auto text = QString::fromUtf8(emoji);
			add(QString::fromUtf8(Mokagram::Lang::Tr("Реакция ")) + text, [=] {
				TogetherSay(controller, u"r"_q, text);
			}, nullptr);
		}
		add(QString::fromUtf8(Mokagram::Lang::Tr("Написать в комнату")), [=] {
			TogetherChatBox(controller);
		}, &st::menuIconChats);
		add(QString::fromUtf8(Mokagram::Lang::Tr("Кто в комнате")), [=] {
			TogetherWho(controller);
		}, &st::menuIconProfile);
		add(QString::fromUtf8(Mokagram::Lang::Tr("Выйти из комнаты"))
			+ (listeners > 0
				? QString::fromUtf8(Mokagram::Lang::Tr(" (слушают: "))
					+ QString::number(listeners) + u")"_q
				: QString()), [=] {
			TogetherStop();
			controller->showToast(QString::fromUtf8(
				Mokagram::Lang::Tr("Вы вышли из комнаты")));
		}, &st::menuIconDelete);
		return;
	}
	add(QString::fromUtf8(Mokagram::Lang::Tr("Слушать вместе: создать комнату")), [=] {
		TogetherCreate(controller);
	}, &st::menuIconAdd);
	add(QString::fromUtf8(Mokagram::Lang::Tr("Слушать вместе: войти по коду")), [=] {
		TogetherJoinBox(controller);
	}, &st::menuIconProfile);
}

void ShowCurrentTrackMenu(
		not_null<Window::SessionController*> controller,
		QPoint globalPosition,
		QWidget *parent,
		bool islandMenu) {
	BindController(controller);
	const auto track = PlayingTrack();
	QueueState().controller = base::make_weak(controller);
	static auto menu = base::unique_qptr<Ui::PopupMenu>();
	menu = base::make_unique_q<Ui::PopupMenu>(
		parent ? parent : controller->widget().get(),
		st::popupMenuWithIcons);
	if (islandMenu) {
		if (!track.id.isEmpty() && CanForwardTrack(track)) {
			menu->addAction(QString::fromUtf8(Mokagram::Lang::Tr("Переслать")), [=] {
				ForwardTrack(controller, track);
			}, &st::menuIconForward);
		}
		menu->addAction(QString::fromUtf8(Mokagram::Lang::Tr("Закрыть")), [] {
			Media::Player::instance()->stopAndClose();
		}, &st::menuIconCancel);
		menu->popup(globalPosition);
		return;
	}
	if (track.id.isEmpty()) {
		// Ничего не играет: в меню только вход в комнату.
		AddTogetherActions(menu.get(), controller);
		menu->popup(globalPosition);
		return;
	}
	const auto add = [&](const QString &text, Fn<void()> callback, const style::icon *icon) {
		menu->addAction(text, std::move(callback), icon);
	};
	AddRadioAction(menu.get(), controller, track);
	AddToPlaylistMenu(menu.get(), controller, track);
	const auto instance = Media::Player::instance();
	add(QString::fromUtf8(Mokagram::Lang::Tr("Пауза / играть")), [=] {
		instance->playPause(AudioMsgId::Type::Song);
	}, &st::menuIconSoundOn);
	if (UsesQueue(instance->current(AudioMsgId::Type::Song))
		&& PreviousAvailable()) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Предыдущий")), [] { Step(-1); }, &st::menuIconAbove);
	}
	if (UsesQueue(instance->current(AudioMsgId::Type::Song))
		&& NextAvailable()) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Следующий")), [] { Step(1); }, &st::menuIconBelow);
	}
	menu->addSeparator();
	add(IsLiked(track.id)
		? QString::fromUtf8(Mokagram::Lang::Tr("Убрать из избранного"))
		: QString::fromUtf8(Mokagram::Lang::Tr("В избранное")), [=] {
		SetLiked(track, !IsLiked(track.id));
	}, &st::menuIconFave);
	if (!IsTelegramTrack(track)) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Скачать")), [=] { SaveToDisk(track); }, &st::menuIconDownload);
	}
	if (CanForwardTrack(track)) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Переслать")), [=] {
			ForwardTrack(controller, track);
		}, &st::menuIconForward);
	}
	if (TrackMessage(track)) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Показать в чате")), [=] {
			if (const auto message = TrackMessage(track)) {
				controller->showPeerHistory(message->history(), {}, message->id);
			}
		}, &st::menuIconChats);
	} else if (!IsTelegramTrack(track)) {
		add(QString::fromUtf8(Mokagram::Lang::Tr("Добавить в профиль")), [=] { AddToProfile(controller, track); }, &st::menuIconProfile);
		add(QString::fromUtf8(Mokagram::Lang::Tr("Копировать ссылку")), [=] {
			QGuiApplication::clipboard()->setText(track.permalink);
			controller->showToast(QString::fromUtf8(Mokagram::Lang::Tr("Ссылка скопирована")));
		}, &st::menuIconLink);
	}
	menu->addSeparator();
	add(QString::fromUtf8(Mokagram::Lang::Tr("Поиск музыки")), [=] { ShowMusicBox(controller); }, &st::menuIconSearch);
	add(QString::fromUtf8(Mokagram::Lang::Tr("Очередь")), [=] { ShowPlayerBox(controller); }, &st::menuIconChats);
	menu->addSeparator();
	AddTogetherActions(menu.get(), controller);
	menu->popup(globalPosition);
}

void ShowPlayerBox(not_null<Window::SessionController*> controller) {
	BindController(controller);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Mokagram::Plugins::RoundedWindow(box);
		const auto content = box->verticalLayout();
		content->setFocusPolicy(Qt::TabFocus);
		const auto instance = Media::Player::instance();
		content->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			if (e->type() == QEvent::KeyPress) {
				const auto key = static_cast<QKeyEvent*>(e.get());
				return key->key() == Qt::Key_Menu
					|| (key->key() == Qt::Key_F10
						&& key->modifiers() == Qt::ShiftModifier);
			}
			return e->type() == QEvent::ContextMenu;
		}) | rpl::on_next([=](not_null<QEvent*> e) {
			e->accept();
			if (e->type() == QEvent::KeyPress) {
				ShowCurrentTrackMenu(controller,
					content->mapToGlobal(content->rect().center()));
				return;
			}
			const auto event = static_cast<QContextMenuEvent*>(e.get());
			ShowCurrentTrackMenu(controller,
				event->reason() == QContextMenuEvent::Keyboard
					? content->mapToGlobal(content->rect().center())
					: event->globalPos());
		}, box->lifetime());

		// Смена трека: старая обложка с названием плавно растворяется в новой.
		struct InfoFade {
			Ui::Animations::Simple animation;
			QPixmap was;
			QPixmap last;
		};
		const auto fade = box->lifetime().make_state<InfoFade>();

		const auto header = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 16, 16, 0));
		header->resize(header->width(), 40);
		const auto close = Mokagram::Plugins::MakeHeaderButton(
			header,
			true,
			[=] { box->closeBox(); });
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			header,
			QString::fromUtf8(Mokagram::Lang::Tr("Сейчас играет")),
			st::boxTitle);
		const auto searchButton = Mokagram::Plugins::MakeHeaderButton(
			header,
			false,
			[=] {
				box->closeBox();
				ShowMusicBox(controller);
			});
		header->widthValue(
		) | rpl::on_next([=](int width) {
			close->move(0, (header->height() - close->height()) / 2);
			title->move(
				(width - title->width()) / 2,
				(header->height() - title->height()) / 2);
			searchButton->move(
				width - searchButton->width(),
				(header->height() - searchButton->height()) / 2);
		}, header->lifetime());

		// Обложка, название, исполнитель.
		const auto info = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(0, 8, 0, 0));
		const auto coverRect = [=] {
			const auto side = std::min(info->width() - 48, 240);
			return QRect((info->width() - side) / 2, 8, side, side);
		};
		info->widthValue(
		) | rpl::on_next([=](int width) {
			info->resize(width, coverRect().height()
				+ st::mokagramMusicPlayerInfoTextHeight + 12);
		}, info->lifetime());
		const auto paintInfo = [=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			const auto art = coverRect();
			const auto current = instance->current(AudioMsgId::Type::Song);
			const auto cover = CoverForPlayback(current);
			auto path = QPainterPath();
			path.addRoundedRect(art, 8, 8);
			if (!cover.isNull()) {
				p.save();
				p.setClipPath(path);
				const auto crop = std::min(cover.width(), cover.height());
				p.drawImage(art, cover, QRect(
					(cover.width() - crop) / 2,
					(cover.height() - crop) / 2,
					crop,
					crop));
				p.restore();
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawPath(path);
				st::menuIconSoundOn.paintInCenter(p, art);
			}
			const auto track = PlayingTrack();
			auto big = st::semiboldFont->f;
			big.setPixelSize(20);
			p.setFont(big);
			p.setPen(st::windowFg);
			const auto textWidth = info->width() - 48 - 48
				- (OwnsTrack(current) ? 120 : 0);
			const auto metrics = QFontMetrics(big);
			p.drawText(
				QRect(24, art.bottom() + 28, textWidth, 30),
				Qt::AlignLeft | Qt::AlignVCenter,
				metrics.elidedText(track.title, Qt::ElideRight, textWidth));
		};
		info->paintOn([=](QPainter &p) {
			if (fade->animation.animating() && !fade->was.isNull()) {
				p.drawPixmap(0, 0, fade->was);
				p.setOpacity(fade->animation.value(1.));
			}
			paintInfo(p);
		});
		const auto renderInfo = [=] {
			const auto ratio = style::DevicePixelRatio();
			auto result = QPixmap(info->size() * ratio);
			result.setDevicePixelRatio(ratio);
			result.fill(Qt::transparent);
			auto p = QPainter(&result);
			paintInfo(p);
			p.end();
			return result;
		};

		const auto artistButton = Ui::CreateChild<Ui::AbstractButton>(info);
		const auto artistHint = QString::fromUtf8(
			Mokagram::Lang::Tr("Найти исполнителя на SoundCloud"));
		artistButton->setToolTip(artistHint);
		artistButton->setAccessibleName(artistHint);
		artistButton->paintOn([=](QPainter &p) {
			auto font = st::normalFont->f;
			font.setPixelSize(16);
			const auto active = artistButton->isOver() || artistButton->isDown();
			font.setUnderline(active);
			p.setFont(font);
			p.setPen(active
				? st::windowFg
				: st::windowSubTextFg);
			p.drawText(
				artistButton->rect(),
				Qt::AlignLeft | Qt::AlignVCenter,
				QFontMetrics(font).elidedText(
					PlayingTrack().artist,
					Qt::ElideRight,
					artistButton->width()));
		});
		const auto updateArtistButton = [=] {
			const auto current = instance->current(AudioMsgId::Type::Song);
			const auto textWidth = info->width() - 48 - 48
				- (OwnsTrack(current) ? 120 : 0);
			auto font = st::normalFont->f;
			font.setPixelSize(16);
			const auto artistWidth = std::min(
				textWidth,
				QFontMetrics(font).horizontalAdvance(PlayingTrack().artist) + 2);
			artistButton->setGeometry(
				24,
				coverRect().bottom() + 56,
				artistWidth,
				28);
			const auto hasArtist = !PlayingTrack().artist.trimmed().isEmpty();
			artistButton->setDisabled(!hasArtist);
			artistButton->setPointerCursor(hasArtist);
			artistButton->update();
		};
		info->widthValue(
		) | rpl::on_next(updateArtistButton, artistButton->lifetime());
		artistButton->setClickedCallback([=] {
			const auto artist = PlayingTrack().artist.trimmed();
			if (artist.isEmpty()) {
				return;
			}
			QueueState().telegramSearch = false;
			QueueState().tab = 0;
			box->closeBox();
			ShowMusicBox(controller, artist);
		});

		const auto heart = Ui::CreateChild<RoundGlyph>(info, Glyph::Add, 40);
		info->widthValue(
		) | rpl::on_next([=](int width) {
			heart->move(width - 24 - 40 - 40, coverRect().bottom() + 34);
		}, heart->lifetime());
		const auto more = Ui::CreateChild<RoundGlyph>(info, Glyph::More, 40);
		const auto block = Ui::CreateChild<RoundGlyph>(info, Glyph::Block, 40);
		more->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Больше такого")));
		block->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Не нравится автор")));
		info->widthValue(
		) | rpl::on_next([=](int width) {
			more->move(width - 24 - 40 - 40 - 40, coverRect().bottom() + 34);
			block->move(width - 24 - 40 - 40 - 80, coverRect().bottom() + 34);
		}, more->lifetime());
		more->setClickedCallback([=] { MoreLikeThis(); });
		block->setClickedCallback([=] { DislikeArtist(); });

		// Перемотка и время.
		const auto seek = content->add(
			object_ptr<SeekBar>(content),
			QMargins(16, 8, 16, 0));
		const auto times = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(24, 0, 24, 0));
		times->resize(times->width(), 20);
		const auto position = std::make_shared<std::pair<QString, QString>>();
		times->paintOn([=](QPainter &p) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(times->rect(), Qt::AlignLeft | Qt::AlignVCenter, position->first);
			p.drawText(times->rect(), Qt::AlignRight | Qt::AlignVCenter, position->second);
		});

		// Кнопки.
		const auto controls = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 12, 16, 12));
		controls->resize(controls->width(), 80);
		const auto previous = Ui::CreateChild<RoundGlyph>(controls, Glyph::Previous, 44);
		const auto play = Ui::CreateChild<RoundGlyph>(controls, Glyph::Play, 72);
		const auto next = Ui::CreateChild<RoundGlyph>(controls, Glyph::Next, 44);
		const auto shuffle = Ui::CreateChild<RoundGlyph>(controls, Glyph::Shuffle, 44);
		const auto repeat = Ui::CreateChild<RoundGlyph>(controls, Glyph::Repeat, 44);
		previous->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Предыдущий")));
		play->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Пауза / играть")));
		next->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Следующий")));
		shuffle->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Перемешать")));
		repeat->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Повтор")));
		controls->widthValue(
		) | rpl::on_next([=](int width) {
			const auto center = width / 2;
			const auto step = (width - 44) / 4;
			play->move(center - 36, 4);
			previous->move(center - step - 22, 18);
			next->move(center + step - 22, 18);
			shuffle->move(0, 18);
			repeat->move(width - 44, 18);
		}, controls->lifetime());
		shuffle->setClickedCallback([=] {
			auto &settings = Core::App().settings();
			settings.setPlayerOrderMode(
				settings.playerOrderMode() == Media::OrderMode::Shuffle
					? Media::OrderMode::Default
					: Media::OrderMode::Shuffle);
			Core::App().saveSettingsDelayed();
		});
		repeat->setClickedCallback([=] {
			auto &settings = Core::App().settings();
			const auto mode = settings.playerRepeatMode();
			settings.setPlayerRepeatMode(mode == Media::RepeatMode::None
				? Media::RepeatMode::All
				: mode == Media::RepeatMode::All
				? Media::RepeatMode::One
				: Media::RepeatMode::None);
			Core::App().saveSettingsDelayed();
		});
		Core::App().settings().playerOrderModeValue(
		) | rpl::on_next([=] { shuffle->update(); }, shuffle->lifetime());
		Core::App().settings().playerRepeatModeValue(
		) | rpl::on_next([=] { repeat->update(); }, repeat->lifetime());

		const auto share = Ui::CreateChild<Ui::AbstractButton>(info);
		share->resize(40, 40);
		share->setPointerCursor(true);
		const auto shareHint = QString::fromUtf8(Mokagram::Lang::Tr("Переслать"));
		share->setToolTip(shareHint);
		share->setAccessibleName(shareHint);
		share->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			if (share->isOver() || share->isDown()) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawEllipse(share->rect());
			}
			st::menuIconForward.paintInCenter(p, share->rect());
		});
		info->widthValue(
		) | rpl::on_next([=](int width) {
			share->move(width - 24 - share->width(), coverRect().bottom() + 34);
		}, share->lifetime());
		share->setClickedCallback([=] {
			const auto track = PlayingTrack();
			if (!track.id.isEmpty() && CanForwardTrack(track)) {
				ForwardTrack(controller, track);
			}
		});

		const auto step = [=](int delta) {
			if (OwnsTrack(instance->current(AudioMsgId::Type::Song))) {
				Step(delta);
			} else if (delta > 0) {
				instance->next(AudioMsgId::Type::Song);
			} else {
				instance->previous(AudioMsgId::Type::Song);
			}
		};
		heart->setClickedCallback([=] {
			const auto track = PlayingTrack();
			if (!track.id.isEmpty()) {
				SetLiked(track, !IsLiked(track.id));
				heart->animateLike(IsLiked(track.id));
			}
		});
		previous->setClickedCallback([=] { step(-1); });
		next->setClickedCallback([=] { step(1); });
		play->setClickedCallback([=] {
			instance->playPause(AudioMsgId::Type::Song);
		});
		seek->sought(
		) | rpl::on_next([=](float64 progress) {
			instance->startSeeking(AudioMsgId::Type::Song);
			instance->finishSeeking(AudioMsgId::Type::Song, progress);
		}, seek->lifetime());

		struct State {
			AudioMsgId current;
			std::shared_ptr<Data::DocumentMedia> media;
			rpl::lifetime downloadLifetime;
			qint64 coverKey = 0;
		};
		const auto playback = box->lifetime().make_state<State>();
		const auto refresh = [=] {
			const auto current = instance->current(AudioMsgId::Type::Song);
			const auto trackChanged = playback->current != current;
			if (trackChanged) {
				playback->current = current;
				playback->downloadLifetime.destroy();
				playback->media = nullptr;
				const auto document = current.audio();
				if (document && !OwnsTrack(current)) {
					playback->media = document->createMediaView();
					document->session().downloaderTaskFinished(
					) | rpl::on_next([=] {
						info->update();
					}, playback->downloadLifetime);
					playback->media->thumbnailWanted(current.contextId());
				}
			}
			const auto state = instance->getState(AudioMsgId::Type::Song);
			const auto frequency = std::max<int64>(state.frequency, 1);
			const auto length = state.length;
			seek->setProgress(length > 0 ? state.position / float64(length) : 0.);
			const auto fmt = [](int64 seconds) {
				return u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
			};
			const auto text = std::pair{
				fmt(state.position / frequency),
				fmt(length / frequency),
			};
			if (*position != text) {
				*position = text;
				times->update();
			}
			const auto coverKey = CoverForPlayback(current).cacheKey();
			if (trackChanged || playback->coverKey != coverKey) {
				playback->coverKey = coverKey;
				if (!fade->last.isNull()
					&& !PowerSaving::On(PowerSaving::kAnimations)) {
					fade->was = fade->last;
					fade->animation.start(
						[=] { info->update(); },
						0.,
						1.,
						kCoverFadeDuration,
						anim::easeOutCubic);
				}
				fade->last = renderInfo();
				info->update();
			}
			const auto own = OwnsTrack(current);
			updateArtistButton();
			const auto track = PlayingTrack();
			share->setVisible(!track.id.isEmpty());
			share->setDisabled(!CanForwardTrack(track));
			heart->setVisible(!track.id.isEmpty());
			more->setVisible(own);
			block->setVisible(own);
			heart->setLiked(IsLiked(track.id));
			play->setPlaying(state.state == Media::Player::State::Playing
				|| state.state == Media::Player::State::Starting
				|| state.state == Media::Player::State::Resuming);
		};
		instance->updatedNotifier(
		) | rpl::on_next(refresh, box->lifetime());
		QueueState().changes.events(
		) | rpl::on_next(refresh, box->lifetime());
		CoversChanged() | rpl::on_next(refresh, box->lifetime());
		LikesChanged() | rpl::on_next(refresh, box->lifetime());
		refresh();

		// Очередь: живой список, меняется при добавлении, перемещении и смене трека.
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				QString::fromUtf8(Mokagram::Lang::Tr("Очередь")),
				st::defaultSubsectionTitle),
			QMargins(24, 24, 24, 8));
		const auto queueList = content->add(object_ptr<Ui::VerticalLayout>(content));
		const auto empty = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				QString::fromUtf8(Mokagram::Lang::Tr("Очередь пуста. Найдите музыку и выберите «Играть следующим» или «В конец очереди» в меню трека.")),
				st::boxLabel),
			QMargins(24, 4, 24, 8));
		const auto rebuild = [=] {
			queueList->clear();
			const auto &queue = QueueState();
			empty->setVisible(queue.tracks.empty());
			for (auto i = 0; i != int(queue.tracks.size()); ++i) {
				queueList->add(object_ptr<Row>(
					queueList,
					controller,
					queue.tracks[i],
					i,
					RowMode::Queue));
			}
		};
		rebuild();
		QueueState().changes.events(
		) | rpl::on_next(rebuild, box->lifetime());
		Ui::AddSkip(content, 10);
	}));
}

void DebugPlay(int index) {
	PlayFromResults(index);
}

void DebugLike(int index) {
	const auto &queue = QueueState();
	if (index >= 0 && index < int(queue.results.size())) {
		SetLiked(queue.results[index], !IsLiked(queue.results[index].id));
	}
}

void DebugTab(int tab) {
	QueueState().tab = tab;
	if (const auto controller = QueueState().controller.get()) {
		ShowMusicBox(controller);
	}
}

void DebugSaveTrack(int index) {
	const auto &queue = QueueState();
	if (index >= 0 && index < int(queue.tracks.size())) {
		SaveToDisk(queue.tracks[index]);
	}
}

namespace {

void ShowMusicBoxWith(
		not_null<Window::SessionController*> controller,
		const QString &initialQuery,
		bool albumsMode,
		const QString &albumsTrackId) {
	BindController(controller);
	if (albumsMode) {
		// Альбомы есть только у SoundCloud.
		QueueState().telegramSearch = false;
		QueueState().tab = 0;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Mokagram::Plugins::RoundedWindow(box);
		const auto content = box->verticalLayout();

		// Шапка: закрыть слева, название по центру.
		const auto header = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(10, 10, 10, 0));
		header->resize(header->width(), 40);
		const auto close = Mokagram::Plugins::MakeHeaderButton(
			header,
			true,
			[=] { box->closeBox(); });
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			header,
			QString::fromUtf8(Mokagram::Lang::Tr("Музыка")),
			st::boxTitle);
		const auto queueButton = Ui::CreateChild<Ui::RoundButton>(
			header,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Очередь"))),
			st::defaultLightButton);
		queueButton->setClickedCallback([=] {
			box->closeBox();
			ShowPlayerBox(controller);
		});
		header->widthValue(
		) | rpl::on_next([=](int width) {
			close->move(0, (header->height() - close->height()) / 2);
			title->move(
				(width - title->width()) / 2,
				(header->height() - title->height()) / 2);
			queueButton->move(
				width - queueButton->width(),
				(header->height() - queueButton->height()) / 2);
		}, header->lifetime());

		// Поиск: круглая плашка с полем.
		static const auto clear = style::owned_color(QColor(0, 0, 0, 0));
		static const auto fieldStyle = [] {
			auto result = st::defaultInputField;
			result.textBg = clear.color();
			result.textBgActive = clear.color();
			const auto pad = std::max(0, (44 - st::defaultInputField.style.font->height) / 2);
			result.textMargins = style::margins(0, pad, 0, pad);
			result.placeholderFgActive = st::windowSubTextFg;
			result.placeholderScale = 0.;
			result.placeholderShift = 0;
			result.placeholderFont = st::normalFont;
			result.border = 0;
			result.borderActive = 0;
			result.heightMin = 44;
			return result;
		}();
		const auto searchWrap = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 14, 16, 6));
		searchWrap->resize(searchWrap->width(), 44);
		searchWrap->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg);
			p.drawRoundedRect(searchWrap->rect(), 22, 22);
		});
		const auto input = Ui::CreateChild<Ui::InputField>(
			searchWrap,
			fieldStyle,
			rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Поиск на SoundCloud"))));
		searchWrap->widthValue(
		) | rpl::on_next([=](int width) {
			input->setGeometry(18, 0, width - 36, 44);
		}, searchWrap->lifetime());
		const auto sources = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 0, 16, 0));
		sources->resize(sources->width(), 48);
		const auto sourceSwitch = Ui::CreateChild<SourceSwitch>(
			sources,
			u"SoundCloud"_q,
			QString::fromUtf8(Mokagram::Lang::Tr("Из чатов")));
		sources->widthValue(
		) | rpl::on_next([=](int width) {
			sourceSwitch->setGeometry(0, 4, width, 40);
		}, sources->lifetime());
		const auto chooseChat = content->add(
			object_ptr<Ui::RoundButton>(content,
				rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Выбрать чат"))),
				st::defaultLightButton),
			QMargins(16, 0, 16, 4),
			style::al_justify);
		chooseChat->resize(chooseChat->width(), 36);

		// Что искать на SoundCloud: треки или альбомы.
		const auto kinds = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 0, 16, 0));
		kinds->resize(kinds->width(), 44);
		const auto kindSwitch = Ui::CreateChild<SourceSwitch>(
			kinds,
			QString::fromUtf8(Mokagram::Lang::Tr("Треки")),
			QString::fromUtf8(Mokagram::Lang::Tr("Альбомы")));
		kinds->widthValue(
		) | rpl::on_next([=](int width) {
			kindSwitch->setGeometry(0, 4, width, 36);
		}, kinds->lifetime());
		struct AlbumsState {
			bool active = false;
			std::vector<Album> found;
			std::optional<Album> opened;
			std::vector<Track> openedTracks;
			QString lastQuery;
			QString trackId;
			int generation = 0;
		};
		const auto albums = box->lifetime().make_state<AlbumsState>();
		albums->active = albumsMode;
		albums->trackId = albumsTrackId;
		kindSwitch->setChats(albumsMode);
		kindSwitch->finishAnimating();

		// Вкладки: поиск, лайкнутые, подборка.
		const auto tabs = content->add(
			object_ptr<Ui::RpWidget>(content),
			QMargins(16, 4, 16, 6));
		tabs->resize(tabs->width(), 40);
		// Дорожка и плашка как у переключателей выше: плашка едет к выбранной
		// вкладке и слегка вытягивается на ходу.
		struct TabsSlide {
			Ui::Animations::Simple animation;
			int index = 0;
			int from = 0;
			int count = 3;
		};
		const auto slide = box->lifetime().make_state<TabsSlide>();
		tabs->paintRequest(
		) | rpl::on_next([=](QRect) {
			auto p = QPainter(tabs);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(tabs->rect(), tabs->height() / 2., tabs->height() / 2.);
			const auto margin = 4;
			const auto part = (tabs->width() - 2 * margin) / double(slide->count);
			const auto at = slide->animation.value(slide->index);
			const auto distance = std::abs(slide->index - slide->from);
			const auto progress = distance
				? std::clamp(std::abs(at - slide->from) / distance, 0., 1.)
				: 1.;
			const auto moving = part * 0.18 * (1. - std::abs(2. * progress - 1.));
			const auto selected = QRectF(
				margin + part * at - moving / 2,
				margin,
				part + moving,
				tabs->height() - 2 * margin);
			p.setBrush(st::windowActiveTextFg->c);
			p.drawRoundedRect(selected, selected.height() / 2., selected.height() / 2.);
		}, tabs->lifetime());
		const auto tabSearch = Ui::CreateChild<TabPill>(tabs, QString::fromUtf8(Mokagram::Lang::Tr("Поиск")));
		const auto tabLiked = Ui::CreateChild<TabPill>(tabs, QString::fromUtf8(Mokagram::Lang::Tr("Лайкнутые")));
		const auto tabForYou = Ui::CreateChild<TabPill>(tabs, QString::fromUtf8(Mokagram::Lang::Tr("Для вас")));
		tabs->widthValue(
		) | rpl::on_next([=](int width) {
			// Кнопки лежат ровно на сегментах плашки, чтобы подпись была по её центру.
			const auto part = (width - 8) / 3.;
			const auto at = [&](int i) { return 4 + int(base::SafeRound(part * i)); };
			tabSearch->setGeometry(
				QueueState().telegramSearch ? 0 : at(0),
				0,
				QueueState().telegramSearch ? width : at(1) - at(0),
				tabs->height());
			tabLiked->setGeometry(at(1), 0, at(2) - at(1), tabs->height());
			tabForYou->setGeometry(at(2), 0, at(3) - at(2), tabs->height());
		}, tabs->lifetime());

		const auto list = content->add(object_ptr<Ui::VerticalLayout>(content));
		Ui::AddSkip(content, 10);

		// Смена раздела: старое содержимое гаснет и уезжает в сторону,
		// новое проявляется и въезжает с другой стороны.
		struct Transition {
			Ui::Animations::Simple animation;
			QPixmap was;
			QPixmap now;
			int top = 0;
			int direction = 0;
			bool changing = false;
		};
		const auto transition = box->lifetime().make_state<Transition>();
		const auto overlay = Ui::CreateChild<Ui::RpWidget>(content);
		overlay->hide();
		const auto transitionRect = [=] {
			const auto bottom = list->y() + list->height();
			return QRect(
				0,
				transition->top,
				content->width(),
				std::max(bottom - transition->top, 1));
		};
		const auto grabNow = [=] {
			overlay->hide();
			const auto rect = transitionRect();
			transition->now = Ui::GrabWidget(content, rect);
			overlay->setGeometry(rect);
			// Вкладки живут поверх перехода, чтобы плашка ехала по-настоящему.
			overlay->setMask(QRegion(overlay->rect()).subtracted(
				QRegion(tabs->geometry().translated(-rect.topLeft()))));
			overlay->show();
			overlay->raise();
		};
		overlay->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(overlay);
			const auto progress = transition->animation.value(1.);
			const auto shift = 28. * transition->direction;
			p.fillRect(overlay->rect(), st::boxBg);
			p.setOpacity(1. - progress);
			p.drawPixmap(int(base::SafeRound(-shift * progress)), 0, transition->was);
			p.setOpacity(progress);
			p.drawPixmap(int(base::SafeRound(shift * (1. - progress))), 0, transition->now);
		}, overlay->lifetime());
		const auto animateChange = [=](int top, int direction, Fn<void()> change) {
			if (transition->changing) {
				change();
				return;
			}
			overlay->hide();
			transition->top = top;
			auto was = Ui::GrabWidget(content, transitionRect());
			// Старые вкладки в снимке не нужны: они остаются живыми.
			{
				auto p = QPainter(&was);
				p.fillRect(
					tabs->geometry().translated(0, -top),
					st::boxBg);
			}
			transition->changing = true;
			change();
			transition->changing = false;
			transition->was = std::move(was);
			transition->direction = direction;
			grabNow();
			transition->animation.start([=] {
				overlay->update();
				if (!transition->animation.animating()) {
					overlay->hide();
					transition->was = transition->now = QPixmap();
				}
			}, 0., 1., kSwitchDuration, anim::easeOutCubic);
		};
		// Если список обновился прямо во время перехода, подменяем новый кадр.
		const auto refreshTransition = [=] {
			if (!transition->changing && transition->animation.animating()) {
				grabNow();
			}
		};
		const auto statusText = std::make_shared<QString>();
		const auto statusLoading = std::make_shared<bool>(false);
		const auto statusSticker = std::make_shared<bool>(false);
		const auto refreshForYou = std::make_shared<Fn<void()>>();
		struct SearchState {
			std::unique_ptr<ChatMusicSearch> chats;
			std::unique_ptr<ChatMusicSearch> extraChat;
			int generation = 0;
		};
		const auto searchState = box->lifetime().make_state<SearchState>();
		const auto runSearch = std::make_shared<Fn<void()>>();

		// Правая кнопка по стикеру: сменить на свой или вернуть стандартный.
		struct HintStickerState {
			base::unique_qptr<Ui::PopupMenu> menu;
			base::unique_qptr<ChatHelpers::TabbedPanel> panel;
		};
		const auto hintSticker = box->lifetime().make_state<HintStickerState>();
		const auto chooseHintSticker = [=](QPoint global) {
			const auto outer = box->getDelegate()->outerContainer();
			if (!outer) {
				return;
			}
			if (!hintSticker->panel) {
				using Selector = ChatHelpers::TabbedSelector;
				hintSticker->panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
					outer.data(),
					controller,
					object_ptr<Selector>(
						nullptr,
						controller->uiShow(),
						Window::GifPauseReason::Layer,
						Selector::Mode::StickersOnly));
				const auto panel = hintSticker->panel.get();
				panel->setDropDown(false);
				panel->setDesiredHeightValues(
					1.,
					st::emojiPanMinHeight / 2,
					st::emojiPanMinHeight);
				panel->hide();
				panel->selector()->setCurrentPeer(controller->session().user());
				panel->selector()->fileChosen(
				) | rpl::on_next([=](ChatHelpers::FileChosen data) {
					SaveHintSticker(data.document);
					panel->hideAnimated();
				}, panel->lifetime());
			}
			const auto panel = hintSticker->panel.get();
			const auto local = outer->mapFromGlobal(global);
			panel->moveBottomRight(local.y(), local.x() + panel->width() / 2);
			panel->showAnimated();
		};
		const auto showHintMenu = [=](QPoint global) {
			hintSticker->menu = base::make_unique_q<Ui::PopupMenu>(
				box,
				st::popupMenuWithIcons);
			const auto menu = hintSticker->menu.get();
			menu->addAction(
				QString::fromUtf8(Mokagram::Lang::Tr("Сменить стикер")),
				[=] { chooseHintSticker(global); },
				&st::menuIconStickers);
			if (!Mokagram::Settings::String(
					QString::fromLatin1(kHintStickerKey)).isEmpty()) {
				menu->addAction(
					QString::fromUtf8(Mokagram::Lang::Tr("Вернуть стандартный")),
					[] { SaveHintSticker(nullptr); },
					&st::menuIconRestore);
			}
			menu->popup(global);
		};

		// Список: сверху строка действий (для подборки) и пояснение, дальше треки.
		const auto openAlbum = std::make_shared<Fn<void(Album)>>();
		const auto closeAlbum = std::make_shared<Fn<void()>>();
		const Fn<void()> fill = [=] {
			list->clear();
			const auto &queue = QueueState();
			const auto albumsShown = albums->active
				&& queue.tab == 0
				&& !queue.telegramSearch;
			if (albumsShown && albums->opened) {
				list->add(
					object_ptr<AlbumBar>(
						list,
						albums->opened->title,
						[=] { crl::on_main(box, [=] { (*closeAlbum)(); }); },
						[=] { PlayAll(albums->openedTracks); }),
					QMargins(10, 0, 16, 6));
			}
			if (queue.tab == 1 && !queue.telegramSearch) {
				list->add(
					object_ptr<SoundCloudAccountBar>(
						list,
						[=] {
							ShowSoundCloudLogin(nullptr);
						},
						[=] {
							Mokagram::Music::Account::Logout();
						}),
					QMargins(10, 4, 10, 10));
			}
			if (queue.tab == 2 && !queue.forYou.empty()) {
				list->add(
					object_ptr<PicksBar>(
						list,
						[=] { PlayAll(QueueState().forYou); },
						[=] {
							QueueState().forYou.clear();
							QueueState().results.clear();
							(*refreshForYou)();
						}),
					QMargins(16, 4, 16, 10));
			}
			if (*statusSticker) {
				list->add(object_ptr<StickerHint>(
					list,
					&controller->session(),
					showHintMenu));
			} else if (!statusText->isEmpty() && *statusLoading) {
				list->add(object_ptr<LoadingView>(list, *statusText));
			} else if (!statusText->isEmpty()) {
				list->add(
					object_ptr<Ui::FlatLabel>(list, *statusText, st::boxLabel),
					QMargins(24, 8, 24, 8));
			}
			if (albumsShown && !albums->opened) {
				for (const auto &album : albums->found) {
					list->add(object_ptr<AlbumRow>(list, controller, album, [=] {
						crl::on_main(box, [=] { (*openAlbum)(album); });
					}));
				}
			}
			for (auto i = 0; i != int(queue.results.size()); ++i) {
				list->add(object_ptr<Row>(list, controller, queue.results[i], i));
			}
			const auto activeChatSearch = queue.selectedChat
				? searchState->extraChat.get()
				: searchState->chats.get();
			if (queue.tab == 0 && queue.telegramSearch
				&& activeChatSearch && activeChatSearch->hasMore()) {
				const auto more = list->add(
					object_ptr<Ui::RoundButton>(
						list,
						rpl::single(QString::fromUtf8(Mokagram::Lang::Tr("Показать ещё"))),
						st::defaultLightButton),
					QMargins(24, 8, 24, 8));
				more->setClickedCallback([=] {
					const auto active = QueueState().selectedChat
						? searchState->extraChat.get()
						: searchState->chats.get();
					if (active) {
						active->searchMore();
					}
					more->setDisabled(true);
				});
			}
			refreshTransition();
		};
		const auto showStatus = [=](const QString &text, bool loading = false, bool sticker = false) {
			*statusText = text;
			*statusLoading = loading;
			*statusSticker = sticker;
			fill();
		};
		const auto albumsTop = [=] {
			return tabs->y() + tabs->height();
		};
		*openAlbum = [=](Album album) {
			animateChange(albumsTop(), 1, [=] {
				++albums->generation;
				albums->opened = album;
				albums->openedTracks.clear();
				QueueState().results.clear();
				showStatus(QString::fromUtf8(Mokagram::Lang::Tr("Загружаю альбом…")), true);
			});
			const auto generation = albums->generation;
			AlbumTracks(album.id, crl::guard(box, [=](std::vector<Track> tracks, QString error) {
				if (generation != albums->generation) {
					return;
				}
				albums->openedTracks = std::move(tracks);
				auto &queue = QueueState();
				if (!albums->active || queue.tab != 0 || queue.telegramSearch) {
					return;
				}
				queue.results = albums->openedTracks;
				showStatus(!error.isEmpty()
					? error
					: albums->openedTracks.empty()
					? QString::fromUtf8(Mokagram::Lang::Tr("В альбоме нет доступных треков"))
					: QString());
			}));
		};
		*closeAlbum = [=] {
			animateChange(albumsTop(), -1, [=] {
				++albums->generation;
				albums->opened.reset();
				albums->openedTracks.clear();
				QueueState().results.clear();
				showStatus(albums->found.empty()
					? QString::fromUtf8(Mokagram::Lang::Tr("Альбомы не найдены"))
					: QString());
			});
		};
		const auto searchAlbums = [=](const QString &query) {
			const auto generation = ++albums->generation;
			albums->lastQuery = query;
			albums->opened.reset();
			albums->openedTracks.clear();
			albums->found.clear();
			QueueState().results.clear();
			showStatus(QString::fromUtf8(Mokagram::Lang::Tr("Ищу альбомы…")), true);
			const auto finish = [=](
					const std::vector<Album> &first,
					const std::vector<Album> &rest,
					const QString &error) {
				if (generation != albums->generation) {
					return;
				}
				auto seen = base::flat_set<QString>();
				albums->found.clear();
				for (const auto list : { &first, &rest }) {
					for (const auto &album : *list) {
						if (seen.emplace(album.id).second) {
							albums->found.push_back(album);
						}
					}
				}
				if (!albums->active
					|| QueueState().tab != 0
					|| QueueState().telegramSearch) {
					return;
				}
				showStatus(!albums->found.empty()
					? QString()
					: !error.isEmpty()
					? error
					: QString::fromUtf8(Mokagram::Lang::Tr("Альбомы не найдены")));
			};
			const auto byQuery = [=](std::vector<Album> first) {
				if (query.isEmpty()) {
					finish(first, {}, QString());
					return;
				}
				SearchAlbums(query, 20, crl::guard(box, [=](std::vector<Album> rest, QString error) {
					finish(first, rest, error);
				}));
			};
			// Альбомы с играющим треком первыми; только для запроса, с которым окно открыли.
			const auto trackId = std::exchange(albums->trackId, QString());
			if (!trackId.isEmpty()) {
				TrackAlbums(trackId, crl::guard(box, byQuery));
			} else {
				byQuery({});
			}
		};
		searchState->chats = std::make_unique<ChatMusicSearch>(
			&controller->session(),
			[=](std::vector<Track> tracks, QString error) {
				auto &queue = QueueState();
				if (!queue.telegramSearch) {
					return;
				}
				queue.searchResults = std::move(tracks);
				if (queue.tab == 0) {
					queue.results = queue.searchResults;
					showStatus(!error.isEmpty()
						? error
						: queue.results.empty()
						? QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено"))
						: QString());
				}
			});
		const auto buildForYou = [=] {
			showStatus(QString::fromUtf8(Mokagram::Lang::Tr("Подбираю музыку под ваш вкус…")), true);
			BuildForYou(30, crl::guard(box, [=](std::vector<Track> tracks, QString note) {
				auto &queue = QueueState();
				queue.forYou = std::move(tracks);
				queue.forYouNote = note;
				if (queue.tab == 2) {
					queue.results = queue.forYou;
					showStatus(note);
					fill();
				}
			}));
		};
		*refreshForYou = buildForYou;
		const auto setTab = [=](int tab) {
			auto &queue = QueueState();
			if (queue.telegramSearch) {
				tab = 0;
			}
			queue.tab = tab;
			sources->setVisible(tab == 0);
			sources->resize(sources->width(), (tab == 0) ? 48 : 0);
			const auto kindsShown = (tab == 0) && !queue.telegramSearch;
			kinds->setVisible(kindsShown);
			kinds->resize(kinds->width(), kindsShown ? 44 : 0);
			chooseChat->setVisible(tab == 0 && queue.telegramSearch);
			chooseChat->resize(chooseChat->width(), queue.telegramSearch ? 36 : 0);
			tabs->resize(tabs->width(), queue.telegramSearch ? 0 : 40);
			tabLiked->setVisible(!queue.telegramSearch);
			tabForYou->setVisible(!queue.telegramSearch);
			if (slide->index != tab) {
				slide->from = slide->index;
				slide->animation.start(
					[=] { tabs->update(); },
					slide->index,
					tab,
					kSwitchDuration,
					anim::easeOutCubic);
				slide->index = tab;
			}
			tabSearch->setActive(tab == 0);
			tabLiked->setActive(tab == 1);
			tabForYou->setActive(tab == 2);
			if (tab == 0 && kindsShown && albums->active) {
				queue.results = albums->opened
					? albums->openedTracks
					: std::vector<Track>();
				showStatus(
					QString(),
					false,
					!albums->opened && albums->found.empty());
			} else if (tab == 0) {
				queue.results = queue.searchResults;
				showStatus(QString(), false, queue.searchResults.empty());
			} else if (tab == 1) {
				queue.results = LikedTracks();
				showStatus(queue.results.empty()
					? QString::fromUtf8(Mokagram::Lang::Tr("Пока пусто. Нажмите сердечко у трека, и он появится здесь."))
					: QString());
			} else {
				queue.results = queue.forYou;
				if (queue.forYou.empty()) {
					buildForYou();
				} else {
					showStatus(QString());
				}
			}
			fill();
		};
		const auto switchTab = [=](int tab) {
			const auto was = QueueState().tab;
			if (was == tab) {
				return;
			}
			animateChange(
				searchWrap->y() + searchWrap->height(),
				(tab > was) ? 1 : -1,
				[=] { setTab(tab); });
		};
		tabSearch->setClickedCallback([=] { switchTab(0); });
		tabLiked->setClickedCallback([=] { switchTab(1); });
		tabForYou->setClickedCallback([=] { switchTab(2); });
		LikesChanged(
		) | rpl::filter([=] {
			return QueueState().tab == 1;
		}) | rpl::on_next([=] {
			auto &queue = QueueState();
			queue.results = LikedTracks();
			showStatus(queue.results.empty()
				? QString::fromUtf8(Mokagram::Lang::Tr("Пока пусто. Нажмите сердечко у трека, и он появится здесь."))
				: QString());
			fill();
		}, box->lifetime());

		const auto search = [=] {
			const auto query = input->getLastText().trimmed();
			auto &queue = QueueState();
			if (albums->active && !queue.telegramSearch) {
				if (query.isEmpty() && albums->trackId.isEmpty()) {
					return;
				}
				if (queue.tab != 0) {
					switchTab(0);
				}
				searchAlbums(query);
				return;
			}
			if (query.isEmpty() && !queue.telegramSearch) {
				return;
			}
			if (queue.tab != 0) {
				switchTab(0);
			}
			const auto generation = ++searchState->generation;
			searchState->chats->cancel();
			queue.lastQuery = query;
			queue.searchResults.clear();
			queue.results.clear();
			showStatus(QString::fromUtf8(Mokagram::Lang::Tr("Ищу…")), true);
			if (queue.telegramSearch) {
				if (queue.selectedChat) {
					searchState->chats->cancel();
					searchState->extraChat = std::make_unique<ChatMusicSearch>(
						&controller->session(),
						[=](std::vector<Track> tracks, QString error) {
							auto &queue = QueueState();
							if (!queue.telegramSearch || queue.tab != 0) {
								return;
							}
							queue.searchResults = std::move(tracks);
							queue.results = queue.searchResults;
							showStatus(!error.isEmpty()
								? error
								: queue.results.empty()
								? QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено"))
								: QString());
						},
						queue.selectedChat);
					searchState->extraChat->search(query);
				} else {
					searchState->chats->search(query);
				}
				return;
			}
			Search(query, 0, crl::guard(box, [=](SearchResult result) {
				if (generation != searchState->generation) {
					return;
				}
				auto &queue = QueueState();
				queue.searchResults = std::move(result.tracks);
				if (queue.tab == 0) {
					queue.results = queue.searchResults;
					showStatus(!result.error.isEmpty()
						? result.error
						: queue.results.empty()
						? QString::fromUtf8(Mokagram::Lang::Tr("Ничего не найдено"))
						: QString());
				}
			}));
		};
		*runSearch = search;
		chooseChat->setClickedCallback([=] {
			ShowMusicSourceBox(
				controller,
				[=](not_null<Data::Thread*> thread) {
					QueueState().selectedChat = thread->owningHistory()->peer->id;
					const auto peer = controller->session().data().peer(QueueState().selectedChat);
					chooseChat->setText(rpl::single(
						QString::fromUtf8(Mokagram::Lang::Tr("Чат: %1"))
						.arg(peer->name())));
					if (*runSearch) {
						(*runSearch)();
					}
					return true;
				});
		});
		const auto updateSource = [=] {
			const auto chats = QueueState().telegramSearch;
			sourceSwitch->setChats(chats);
			chooseChat->setVisible(chats && QueueState().tab == 0);
			chooseChat->resize(chooseChat->width(), chats ? 36 : 0);
			tabs->resize(tabs->width(), chats ? 0 : 36);
			const auto kindsShown = !chats && QueueState().tab == 0;
			kinds->setVisible(kindsShown);
			kinds->resize(kinds->width(), kindsShown ? 44 : 0);
			if (QueueState().selectedChat) {
				if (const auto peer = controller->session().data().peer(QueueState().selectedChat)) {
					chooseChat->setText(rpl::single(
						QString::fromUtf8(Mokagram::Lang::Tr("Чат: %1"))
						.arg(peer->name())));
				}
			}
			tabLiked->setVisible(!chats);
			tabForYou->setVisible(!chats);
			tabs->update();
			slide->count = chats ? 1 : 3;
			if (chats) {
				tabSearch->setGeometry(0, 0, tabs->width(), 40);
			} else {
				const auto part = (tabs->width() - 8) / 3.;
				const auto at = [&](int i) { return 4 + int(base::SafeRound(part * i)); };
				tabSearch->setGeometry(at(0), 0, at(1) - at(0), 40);
				tabLiked->setGeometry(at(1), 0, at(2) - at(1), 40);
				tabForYou->setGeometry(at(2), 0, at(3) - at(2), 40);
			}
			input->setPlaceholder(rpl::single(input->getLastText().isEmpty()
				? QString::fromUtf8(Mokagram::Lang::Tr(chats
					? "Поиск музыки в чатах и группах"
					: albums->active
					? "Поиск альбомов на SoundCloud"
					: "Поиск на SoundCloud"))
				: QString()));
		};
		const auto setSource = [=](bool chats) {
			if (QueueState().telegramSearch == chats) {
				return;
			}
			++searchState->generation;
			searchState->chats->cancel();
			if (searchState->extraChat) {
				searchState->extraChat->cancel();
			}
			QueueState().telegramSearch = chats;
			QueueState().searchResults.clear();
			QueueState().results.clear();
			updateSource();
			setTab(0);
			search();
		};
		sourceSwitch->setChangedCallback([=](bool chats) {
			// Сам переключатель анимируется отдельно, переход идёт под ним.
			animateChange(
				sources->y() + sources->height(),
				chats ? 1 : -1,
				[=] { setSource(chats); });
		});
		kindSwitch->setChangedCallback([=](bool toAlbums) {
			animateChange(
				kinds->y() + kinds->height(),
				toAlbums ? 1 : -1,
				[=] {
					albums->active = toAlbums;
					kindSwitch->setChats(toAlbums);
					updateSource();
					const auto query = input->getLastText().trimmed();
					const auto last = toAlbums
						? albums->lastQuery
						: QueueState().lastQuery;
					if (!query.isEmpty() && query != last) {
						search();
					} else {
						setTab(0);
					}
				});
		});
		// Поиск по мере набора: запускаем после короткой паузы между буквами.
		const auto typingTimer = box->lifetime().make_state<base::Timer>();
		typingTimer->setCallback([=] {
			const auto query = input->getLastText().trimmed();
			const auto last = (albums->active && !QueueState().telegramSearch)
				? albums->lastQuery
				: QueueState().lastQuery;
			if (query.size() >= 2 && query != last) {
				search();
			}
		});
		input->changes() | rpl::on_next([=] {
			updateSource();
			typingTimer->callOnce(kTypingDelay);
		}, input->lifetime());
		input->submits() | rpl::on_next([=] {
			typingTimer->cancel();
			search();
		}, input->lifetime());
		input->setFocusFast();
		if (!initialQuery.isEmpty() || albumsMode) {
			input->setText(initialQuery);
			setTab(0);
			search();
		} else {
			if (!QueueState().lastQuery.isEmpty()) {
				// Возвращаем последний запрос и его результаты без нового запроса.
				input->setText(QueueState().lastQuery);
			}
			setTab(QueueState().tab);
			if (QueueState().tab == 0 && QueueState().telegramSearch) {
				search();
			}
		}
		updateSource();
		sourceSwitch->finishAnimating();
	}));
}

} // namespace

void ShowMusicBox(
		not_null<Window::SessionController*> controller,
		const QString &initialQuery) {
	ShowMusicBoxWith(controller, initialQuery, false, QString());
}

void ShowAlbumsBox(
		not_null<Window::SessionController*> controller,
		const QString &query,
		const QString &trackId) {
	ShowMusicBoxWith(controller, query, true, trackId);
}


// ---- Мост для панели плеера в баре Omarchy (Quickshell) ----
// Клиент держит локальный сокет $XDG_RUNTIME_DIR/mokagram/ctl.sock. Панель
// подключается, получает построчный JSON (состояние, тексты) и шлёт команды
// строками «play», «seek 12345», «volume 0.5» и т.д. Обложка лежит файлом.
namespace {

struct ShellBridge {
	QLocalServer *server = nullptr;
	std::vector<QLocalSocket*> clients;
	QTimer *timer = nullptr;
	QByteArray lastState;
	QString dir;
	QString coverPath;
	qint64 coverKey = 0;
	int coverSerial = 0;
	QString lyricsId;
	QByteArray lyricsLine;
	QByteArray lastLibrary;
	QString commentsId;
	QByteArray lastCommentsLine;
	qint64 sleepAt = 0;
	int libTick = 0;
	rpl::lifetime lifetime;
};

ShellBridge &Bridge() {
	static auto *bridge = new ShellBridge();
	return *bridge;
}

void BridgeSend(const QByteArray &line) {
	auto &b = Bridge();
	for (const auto client : b.clients) {
		client->write(line);
		client->write("\n");
	}
}

[[nodiscard]] QByteArray BridgeJson(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void BridgeSendLyrics(const QString &id, const Lyrics &lyrics) {
	auto lines = QJsonArray();
	for (const auto &line : lyrics.lines) {
		lines.push_back(QJsonArray{ double(line.timeMs), line.text });
	}
	auto object = QJsonObject();
	object.insert(u"t"_q, u"lyrics"_q);
	object.insert(u"id"_q, id);
	object.insert(u"found"_q, lyrics.found);
	object.insert(u"synced"_q, lyrics.synced);
	object.insert(u"lines"_q, lines);
	auto &b = Bridge();
	b.lyricsLine = BridgeJson(object);
	BridgeSend(b.lyricsLine);
}

void BridgeWriteCover(const AudioMsgId &current, QJsonObject &state) {
	auto &b = Bridge();
	const auto cover = CoverForPlayback(current);
	if (cover.isNull()) {
		state.insert(u"cover"_q, QString());
		return;
	}
	if (cover.cacheKey() != b.coverKey || b.coverPath.isEmpty()) {
		const auto old = b.coverPath;
		b.coverKey = cover.cacheKey();
		b.coverPath = u"%1/cover-%2.jpg"_q.arg(b.dir).arg(++b.coverSerial);
		cover.save(b.coverPath, "JPG", 92);
		if (!old.isEmpty()) {
			QFile::remove(old);
		}
	}
	state.insert(u"cover"_q, b.coverPath);
}

void BridgePush(bool force = false) {
	auto &b = Bridge();
	// При выходе плеер телеги уничтожается раньше, чем гаснет таймер моста.
	if (b.clients.empty() || Core::Quitting()) {
		return;
	}
	const auto instance = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	const auto current = instance->current(type);
	const auto st = instance->getState(type);
	auto state = QJsonObject();
	state.insert(u"t"_q, u"state"_q);
	const auto &settings = Core::App().settings();
	state.insert(u"volume"_q, settings.songVolume());
	state.insert(u"shuffle"_q, settings.playerOrderMode() == Media::OrderMode::Shuffle);
	state.insert(u"repeat"_q, settings.playerRepeatMode() == Media::RepeatMode::One
		? u"one"_q
		: settings.playerRepeatMode() == Media::RepeatMode::All
		? u"all"_q
		: u"none"_q);
	if (!current.audio()) {
		state.insert(u"has"_q, false);
	} else {
		const auto track = PlayingTrack();
		const auto frequency = std::max<int64>(st.frequency, 1);
		const auto playing = (st.state == Media::Player::State::Playing)
			|| (st.state == Media::Player::State::Starting)
			|| (st.state == Media::Player::State::Resuming);
		const auto &queue = QueueState();
		const auto owns = OwnsTrack(current);
		state.insert(u"has"_q, true);
		state.insert(u"id"_q, track.id);
		state.insert(u"title"_q, track.title);
		state.insert(u"artist"_q, track.artist);
		state.insert(u"source"_q, IsTelegramTrack(track)
			? u"telegram"_q
			: u"soundcloud"_q);
		state.insert(u"playing"_q, playing);
		state.insert(u"posMs"_q, double(st.position * 1000 / frequency));
		state.insert(u"lenMs"_q, double(st.length > 0
			? st.length * 1000 / frequency
			: track.durationMs));
		state.insert(u"liked"_q, owns && IsLiked(track.id));
		state.insert(u"canLike"_q, owns);
		state.insert(u"canNext"_q, owns ? NextAvailable() : true);
		state.insert(u"canPrev"_q, owns ? PreviousAvailable() : true);
		state.insert(u"speed"_q, settings.audioPlaybackSpeed(true));
		state.insert(u"speedOk"_q, current.changeablePlaybackSpeed());
		state.insert(u"link"_q, !track.permalink.isEmpty());
		state.insert(u"sleepLeft"_q, b.sleepAt > 0
			? double(std::max<qint64>(0, (b.sleepAt - crl::now()) / 1000))
			: 0.);
		const auto &together = TogetherState();
		state.insert(u"together"_q, !together.code.isEmpty());
		state.insert(u"togetherHost"_q, together.host);
		state.insert(u"listeners"_q, together.listeners);
		const auto accent = CurrentCoverAccent();
		state.insert(u"accent"_q, accent.isValid() ? accent.name() : QString());
		BridgeWriteCover(current, state);
		auto list = QJsonArray();
		if (owns) {
			const auto count = int(queue.tracks.size());
			const auto from = std::max(0, queue.index - 2);
			for (auto i = from; i < std::min(count, from + 40); ++i) {
				auto row = QJsonObject();
				row.insert(u"i"_q, i);
				row.insert(u"title"_q, queue.tracks[i].title);
				row.insert(u"artist"_q, queue.tracks[i].artist);
				row.insert(u"current"_q, i == queue.index);
				list.push_back(row);
			}
		}
		state.insert(u"queue"_q, list);
		if (b.lyricsId != track.id) {
			b.lyricsId = track.id;
			b.lyricsLine.clear();
			b.lastCommentsLine.clear();
			const auto id = track.id;
			if (!IsTelegramTrack(track)) {
				LoadTrackComments(id, QString(), [=](TrackCommentsPage page) {
					if (Bridge().lyricsId != id) {
						return;
					}
					auto list = QJsonArray();
					for (const auto &c : page.comments) {
						if (c.timestampMs < 0 || c.body.trimmed().isEmpty()) {
							continue;
						}
						list.push_back(QJsonArray{ double(c.timestampMs), c.author, c.body });
					}
					auto object = QJsonObject();
					object.insert(u"t"_q, u"comments"_q);
					object.insert(u"id"_q, id);
					object.insert(u"list"_q, list);
					Bridge().lastCommentsLine = BridgeJson(object);
					BridgeSend(Bridge().lastCommentsLine);
				});
			}
			FetchLyrics(
				id,
				track.title,
				track.artist,
				track.durationMs,
				[=](Lyrics lyrics) {
					if (Bridge().lyricsId == id) {
						BridgeSendLyrics(id, lyrics);
					}
				});
		}
	}
	const auto line = BridgeJson(state);
	if (force || line != b.lastState) {
		b.lastState = line;
		BridgeSend(line);
	}
}

[[nodiscard]] QJsonObject BridgeTrackJson(const Track &track) {
	auto row = QJsonObject();
	row.insert(u"title"_q, track.title);
	row.insert(u"artist"_q, track.artist);
	return row;
}

void BridgePushLibrary(bool force = false) {
	auto &b = Bridge();
	if (b.clients.empty()) {
		return;
	}
	auto liked = QJsonArray();
	auto count = 0;
	for (const auto &track : LikedTracks()) {
		if (++count > 150) {
			break;
		}
		liked.push_back(BridgeTrackJson(track));
	}
	auto lists = QJsonArray();
	for (const auto &playlist : Playlists()) {
		auto tracks = QJsonArray();
		auto n = 0;
		for (const auto &track : playlist.tracks) {
			if (++n > 150) {
				break;
			}
			tracks.push_back(BridgeTrackJson(track));
		}
		auto row = QJsonObject();
		row.insert(u"id"_q, playlist.id);
		row.insert(u"name"_q, playlist.name);
		row.insert(u"tracks"_q, tracks);
		lists.push_back(row);
	}
	auto object = QJsonObject();
	object.insert(u"t"_q, u"library"_q);
	object.insert(u"liked"_q, liked);
	object.insert(u"playlists"_q, lists);
	const auto line = BridgeJson(object);
	if (force || line != b.lastLibrary) {
		b.lastLibrary = line;
		BridgeSend(line);
	}
}

void BridgeCommand(const QString &line) {
	const auto parts = line.trimmed().split(QChar(' '), Qt::SkipEmptyParts);
	if (parts.isEmpty()) {
		return;
	}
	const auto cmd = parts[0];
	const auto arg = (parts.size() > 1) ? parts[1] : QString();
	const auto instance = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	const auto current = instance->current(type);
	auto &settings = Core::App().settings();
	if (cmd == u"toggle"_q) {
		instance->playPause(type);
	} else if (cmd == u"play"_q || cmd == u"pause"_q) {
		const auto st = instance->getState(type);
		const auto playing = (st.state == Media::Player::State::Playing)
			|| (st.state == Media::Player::State::Starting)
			|| (st.state == Media::Player::State::Resuming);
		if (playing == (cmd == u"pause"_q)) {
			instance->playPause(type);
		}
	} else if (cmd == u"next"_q || cmd == u"prev"_q) {
		const auto delta = (cmd == u"next"_q) ? 1 : -1;
		if (OwnsTrack(current)) {
			Step(delta);
		} else if (delta > 0) {
			instance->next(type);
		} else {
			instance->previous(type);
		}
	} else if (cmd == u"seek"_q) {
		const auto st = instance->getState(type);
		const auto lengthMs = st.length * 1000 / std::max<int64>(st.frequency, 1);
		if (lengthMs > 0) {
			instance->startSeeking(type);
			instance->finishSeeking(type, std::clamp(arg.toDouble() / double(lengthMs), 0., 1.));
		}
	} else if (cmd == u"volume"_q) {
		const auto value = std::clamp(arg.toDouble(), 0., 1.);
		settings.setSongVolume(value);
		Core::App().saveSettingsDelayed();
		Media::Player::mixer()->setSongVolume(value);
	} else if (cmd == u"shuffle"_q) {
		settings.setPlayerOrderMode(
			settings.playerOrderMode() == Media::OrderMode::Shuffle
				? Media::OrderMode::Default
				: Media::OrderMode::Shuffle);
		Core::App().saveSettingsDelayed();
	} else if (cmd == u"repeat"_q) {
		const auto mode = settings.playerRepeatMode();
		settings.setPlayerRepeatMode(mode == Media::RepeatMode::None
			? Media::RepeatMode::All
			: mode == Media::RepeatMode::All
			? Media::RepeatMode::One
			: Media::RepeatMode::None);
		Core::App().saveSettingsDelayed();
	} else if (cmd == u"like"_q) {
		const auto &queue = QueueState();
		if (OwnsTrack(current) && !queue.current.id.isEmpty()) {
			SetLiked(queue.current, !IsLiked(queue.current.id));
		}
	} else if (cmd == u"playindex"_q) {
		const auto index = arg.toInt();
		if (OwnsTrack(current) && index >= 0 && index < int(QueueState().tracks.size())) {
			PlayIndex(index);
		}
	} else if (cmd == u"speed"_q) {
		settings.setAudioPlaybackSpeed(std::clamp(arg.toDouble(), 0.5, 2.0));
		Core::App().saveSettingsDelayed();
		instance->updatePlaybackSpeed();
	} else if (cmd == u"sleep"_q) {
		const auto minutes = arg.toInt();
		Bridge().sleepAt = (minutes > 0) ? (crl::now() + minutes * 60 * 1000) : 0;
	} else if (cmd == u"playliked"_q) {
		auto &queue = QueueState();
		queue.custom = false;
		queue.results = LikedTracks();
		PlayFromResults(arg.toInt());
	} else if (cmd == u"playlist"_q && parts.size() > 2) {
		for (const auto &playlist : Playlists()) {
			if (playlist.id == arg) {
				auto &queue = QueueState();
				queue.custom = false;
				queue.results = playlist.tracks;
				PlayFromResults(parts[2].toInt());
				break;
			}
		}
	} else if (cmd == u"copylink"_q || cmd == u"openlink"_q) {
		const auto link = PlayingTrack().permalink;
		if (!link.isEmpty()) {
			if (cmd == u"copylink"_q) {
				QGuiApplication::clipboard()->setText(link);
			} else {
				QDesktopServices::openUrl(QUrl(link));
			}
		}
	} else if (cmd == u"forward"_q) {
		if (const auto window = Core::App().activePrimaryWindow()) {
			window->activate();
			if (const auto controller = window->sessionController()) {
				const auto track = PlayingTrack();
				if (!track.id.isEmpty()) {
					ForwardTrack(controller, track);
				}
			}
		}
	} else if (cmd == u"open"_q || cmd == u"fullscreen"_q) {
		if (const auto window = Core::App().activePrimaryWindow()) {
			window->activate();
			if (const auto controller = window->sessionController()) {
				if (cmd == u"fullscreen"_q) {
					ShowFullscreenPlayer(controller);
				} else {
					ShowPlayerBox(controller);
				}
			}
		}
	}
	BridgePush();
}

} // namespace

void StartShellBridge() {
	auto &b = Bridge();
	if (b.server) {
		return;
	}
	const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if (runtime.isEmpty()) {
		return;
	}
	b.dir = runtime + u"/mokagram"_q;
	QDir().mkpath(b.dir);
	QFile::setPermissions(b.dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	const auto path = b.dir + u"/ctl.sock"_q;
	QLocalServer::removeServer(path);
	b.server = new QLocalServer(qApp);
	b.server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!b.server->listen(path)) {
		return;
	}
	QObject::connect(b.server, &QLocalServer::newConnection, [] {
		auto &b = Bridge();
		while (const auto client = b.server->nextPendingConnection()) {
			b.clients.push_back(client);
			const auto buffer = new QByteArray();
			QObject::connect(client, &QLocalSocket::readyRead, client, [=] {
				buffer->append(client->readAll());
				while (true) {
					const auto end = buffer->indexOf('\n');
					if (end < 0) {
						break;
					}
					const auto text = QString::fromUtf8(buffer->left(end));
					buffer->remove(0, end + 1);
					BridgeCommand(text);
				}
			});
			QObject::connect(client, &QLocalSocket::disconnected, client, [=] {
				auto &b = Bridge();
				b.clients.erase(std::remove(b.clients.begin(), b.clients.end(), client), b.clients.end());
				delete buffer;
				client->deleteLater();
			});
			b.lastState.clear();
			crl::on_main([] {
				BridgePush(true);
				BridgePushLibrary(true);
				const auto &b = Bridge();
				if (!b.lastCommentsLine.isEmpty()) {
					BridgeSend(b.lastCommentsLine);
				}
			});
			if (!b.lyricsLine.isEmpty()) {
				client->write(b.lyricsLine + "\n");
			}
		}
	});
	b.timer = new QTimer(qApp);
	b.timer->setInterval(250);
	// Плеер создаётся позже запуска моста: до этого его не трогаем.
	QObject::connect(b.timer, &QTimer::timeout, [] {
		if (Core::Quitting()) {
			return;
		}
		static const auto started = crl::now();
		if (crl::now() - started > 8000) {
			auto &b = Bridge();
			if (b.sleepAt > 0 && crl::now() >= b.sleepAt) {
				b.sleepAt = 0;
				const auto instance = Media::Player::instance();
				const auto st = instance->getState(AudioMsgId::Type::Song);
				if (st.state == Media::Player::State::Playing
					|| st.state == Media::Player::State::Starting
					|| st.state == Media::Player::State::Resuming) {
					instance->playPause(AudioMsgId::Type::Song);
				}
			}
			if (++b.libTick % 8 == 0) {
				BridgePushLibrary();
			}
			BridgePush();
		}
	});
	b.timer->start();

}

Track CurrentQueueTrack() {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto &queue = QueueState();
	return OwnsTrack(current) ? queue.current : Track();
}

} // namespace Mokagram::Music
