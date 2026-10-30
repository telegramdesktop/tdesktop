/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/story_audio_box.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_saved_music.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "editor/photo_editor_common.h"
#include "history/history_item.h"
#include "history/view/media/history_view_document.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "settings/settings_common.h"
#include "storage/file_download.h"
#include "storage/storage_shared_media.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace {

constexpr auto kSearchDelay = crl::time(400);

[[nodiscard]] QString TemporaryTrackPath(not_null<DocumentData*> document) {
	const auto suffix = QFileInfo(document->filename()).suffix();
	return QDir::tempPath()
		+ u"/tdesktop_story_music_%1"_q.arg(document->id)
		+ (suffix.isEmpty() ? QString() : ('.' + suffix));
}

[[nodiscard]] bool TrackReady(
		not_null<DocumentData*> document,
		const std::shared_ptr<Data::DocumentMedia> &media) {
	return !document->filepath(true).isEmpty() || media->loaded();
}

void StartLoading(
		not_null<DocumentData*> document,
		Data::FileOrigin origin) {
	const auto toFile = (document->size > Storage::kMaxFileInMemory)
		? TemporaryTrackPath(document)
		: QString();
	document->save(origin, toFile);
}

[[nodiscard]] Editor::StoryAudio MakeAudio(
		not_null<DocumentData*> document,
		const std::shared_ptr<Data::DocumentMedia> &media) {
	const auto song = document->song();
	auto audio = Editor::StoryAudio{
		.document = document,
		.title = song ? song->title : document->filename(),
		.performer = song ? song->performer : QString(),
		.duration = document->duration(),
	};
	audio.path = document->filepath(true);
	if (audio.path.isEmpty()) {
		audio.content = media->bytes();
	}
	return audio;
}

[[nodiscard]] QString TrackTitle(not_null<DocumentData*> document) {
	const auto song = document->song();
	return (song && !song->title.isEmpty())
		? song->title
		: document->filename();
}

[[nodiscard]] QString TrackPerformer(not_null<DocumentData*> document) {
	const auto song = document->song();
	return song ? song->performer : QString();
}

[[nodiscard]] bool Matches(const QString &name, const QString &query) {
	return !name.isEmpty()
		&& (name.startsWith(query, Qt::CaseInsensitive)
			|| name.contains(' ' + query, Qt::CaseInsensitive));
}

class AudioRow final : public Ui::RippleButton {
public:
	AudioRow(
		QWidget *parent,
		not_null<DocumentData*> document,
		Data::FileOrigin origin);

	[[nodiscard]] not_null<DocumentData*> document() const {
		return _document;
	}
	[[nodiscard]] Data::FileOrigin origin() const {
		return _origin;
	}
	[[nodiscard]] std::shared_ptr<Data::DocumentMedia> media() const {
		return _media;
	}
	[[nodiscard]] bool ready() const {
		return TrackReady(_document, _media);
	}
	void setLoading(bool loading);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void paintThumbnail(Painter &p, QRect rect);
	void paintBadge(QPainter &p, QRect thumb);

	const not_null<DocumentData*> _document;
	const Data::FileOrigin _origin;
	const std::shared_ptr<Data::DocumentMedia> _media;
	const QString _title;
	const QString _info;
	bool _loading = false;

};

AudioRow::AudioRow(
	QWidget *parent,
	not_null<DocumentData*> document,
	Data::FileOrigin origin)
: RippleButton(parent, st::defaultRippleAnimation)
, _document(document)
, _origin(origin)
, _media(document->createMediaView())
, _title(TrackTitle(document))
, _info([&] {
	const auto performer = TrackPerformer(document);
	const auto duration = Ui::FormatDurationText(
		document->duration() / 1000);
	return performer.isEmpty()
		? duration
		: (performer + u" • "_q + duration);
}()) {
	resize(1, st::storyAudioRowHeight);
	if (_document->hasThumbnail()) {
		_document->loadThumbnail(_origin);
	}
}

void AudioRow::setLoading(bool loading) {
	_loading = loading;
	update();
}

void AudioRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(p, 0, 0);

	const auto thumb = QRect(
		st::storyAudioRowThumbLeft,
		st::storyAudioRowThumbTop,
		st::storyAudioRowThumbSize,
		st::storyAudioRowThumbSize);
	paintThumbnail(p, thumb);
	paintBadge(p, thumb);

	const auto textLeft = st::storyAudioRowTextLeft;
	const auto textWidth = width() - textLeft - st::storyAudioRowTextRight;
	p.setPen(st::windowFg);
	p.setFont(st::storyAudioRowTitleFont);
	p.drawTextLeft(
		textLeft,
		st::storyAudioRowTitleTop,
		width(),
		st::storyAudioRowTitleFont->elided(_title, textWidth));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::storyAudioRowInfoFont);
	p.drawTextLeft(
		textLeft,
		st::storyAudioRowInfoTop,
		width(),
		st::storyAudioRowInfoFont->elided(_info, textWidth));
	p.fillRect(
		textLeft,
		height() - st::lineWidth,
		width() - textLeft,
		st::lineWidth,
		st::boxDividerBg);
}

void AudioRow::paintThumbnail(Painter &p, QRect rect) {
	auto hq = PainterHighQualityEnabler(p);
	const auto covered = _document->isSongWithCover()
		&& HistoryView::DrawThumbnailAsSongCover(
			p,
			st::songCoverOverlayFg,
			_media,
			rect);
	if (!covered) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgFileInBg);
		p.drawEllipse(rect);
	}
	const auto &icon = covered
		? st::historyFileThumbPlay
		: st::historyFileInPlay;
	icon.paintInCenter(p, rect);
}

void AudioRow::paintBadge(QPainter &p, QRect thumb) {
	if (ready()) {
		return;
	}
	const auto size = st::historyAudioDownloadSize;
	const auto shift = st::historyAudioDownloadShift;
	const auto badge = QRect(
		thumb.x() + shift,
		thumb.y() + shift,
		size,
		size);
	auto hq = PainterHighQualityEnabler(p);
	auto pen = st::boxBg->p;
	pen.setWidth(st::lineWidth);
	p.setPen(pen);
	p.setBrush(st::msgFileInBg);
	p.drawEllipse(badge);
	if (_loading) {
		const auto progress = std::clamp(_document->progress(), 0., 1.);
		auto ring = QPen(st::historyFileInIconFg->c);
		ring.setWidthF(st::storyAudioRowRing);
		ring.setCapStyle(Qt::RoundCap);
		p.setPen(ring);
		p.setBrush(Qt::NoBrush);
		const auto inset = st::storyAudioRowRingInset;
		p.drawArc(
			QRectF(badge).adjusted(inset, inset, -inset, -inset),
			90 * 16,
			-int(std::round(progress * 360 * 16)));
	} else {
		st::historyAudioInDownload.paintInCenter(p, badge);
	}
}

class SearchRow final : public Ui::RpWidget {
public:
	explicit SearchRow(QWidget *parent);

	[[nodiscard]] rpl::producer<QString> queryChanges() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const base::unique_qptr<Ui::InputField> _field;

};

SearchRow::SearchRow(QWidget *parent)
: RpWidget(parent)
, _field(base::make_unique_q<Ui::InputField>(
	this,
	st::storyAudioSearchField,
	tr::lng_dlg_filter())) {
}

rpl::producer<QString> SearchRow::queryChanges() const {
	return _field->changes() | rpl::map([=] {
		return _field->getLastText().trimmed();
	});
}

int SearchRow::resizeGetHeight(int newWidth) {
	const auto height = st::storyAudioSearchHeight;
	const auto left = st::storyAudioSearchFieldLeft;
	_field->resize(
		std::max(newWidth - left - st::storyAudioSearchFieldRight, 1),
		_field->height());
	_field->moveToLeft(left, (height - _field->height()) / 2);
	return height;
}

void SearchRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect(), radius, radius);
	const auto &icon = st::storyAudioSearchIcon;
	icon.paint(
		p,
		st::storyAudioSearchIconLeft,
		(height() - icon.height()) / 2,
		width());
}

struct SharedMusic {
	std::vector<not_null<HistoryItem*>> items;
	Data::MessagePosition offsetPosition;
	QString query;
	mtpRequestId requestId = 0;
	int32 offsetRate = 0;
	int fullCount = 0;
	bool loading = false;
	bool loaded = false;
	bool hasMore = false;
};

struct State {
	QString query;
	SharedMusic shared;
	std::vector<AudioRow*> rows;
	DocumentData *downloading = nullptr;
	std::shared_ptr<Data::DocumentMedia> downloadingMedia;
	Ui::VerticalLayout *list = nullptr;
	Ui::VerticalLayout *files = nullptr;
	base::Timer searchTimer;
	Fn<void()> rebuild;
	Fn<void(bool more)> loadShared;
	Fn<void(not_null<AudioRow*> row)> choose;
};

void ChooseAudioFile(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Editor::StoryAudio)> done) {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty()) {
			return;
		}
		const auto path = result.paths.front();
		const auto info = Media::Player::PrepareForSending(path, {});
		using Song = Ui::PreparedFileInformation::Song;
		const auto song = std::get_if<Song>(&info.media);
		if (!song || song->duration <= 0) {
			show->showToast(tr::lng_story_audio_invalid(tr::now));
			return;
		}
		auto audio = Editor::StoryAudio{
			.path = path,
			.title = (song->title.isEmpty()
				? QFileInfo(path).completeBaseName()
				: song->title),
			.performer = song->performer,
			.duration = song->duration,
		};
		box->closeBox();
		done(std::move(audio));
	};
	FileDialog::GetOpenPath(
		box.get(),
		tr::lng_story_audio_files(tr::now),
		FileDialog::AudioFilesFilter(),
		crl::guard(box, callback));
}

void StoryAudioBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Editor::StoryAudio)> done) {
	box->setTitle(tr::lng_story_audio_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);

	const auto session = &show->session();
	const auto container = box->verticalLayout();
	const auto state = box->lifetime().make_state<State>();

	const auto search = container->add(
		object_ptr<SearchRow>(container),
		st::storyAudioSearchMargins);

	state->files = container->add(object_ptr<Ui::VerticalLayout>(container));
	const auto files = state->files;
	Ui::AddSkip(files);
	files->add(Settings::CreateButtonWithIcon(
		files,
		tr::lng_story_audio_files(),
		st::settingsButtonActive,
		{ &st::menuIconShowInFolder }
	))->setClickedCallback([=] {
		ChooseAudioFile(box, show, done);
	});
	Ui::AddSkip(files);
	Ui::AddDivider(files);

	state->list = container->add(object_ptr<Ui::VerticalLayout>(container));
	const auto list = state->list;
	const auto music = &session->data().savedMusic();
	const auto self = session->userPeerId();

	const auto finish = [=](
			not_null<DocumentData*> document,
			const std::shared_ptr<Data::DocumentMedia> &media) {
		auto audio = MakeAudio(document, media);
		if (audio.empty() || audio.duration <= 0) {
			show->showToast(tr::lng_story_audio_failed(tr::now));
			return;
		}
		box->closeBox();
		done(std::move(audio));
	};
	state->rebuild = [=] {
		while (list->count()) {
			delete list->widgetAt(0);
		}
		state->rows.clear();
		state->files->setVisible(state->query.isEmpty());

		const auto addRow = [&](
				not_null<DocumentData*> document,
				Data::FileOrigin origin) {
			const auto row = list->add(object_ptr<AudioRow>(
				list,
				document,
				origin));
			row->setLoading(state->downloading == document.get());
			row->setClickedCallback([=] {
				state->choose(row);
			});
			state->rows.push_back(row);
			return row;
		};
		const auto matches = [&](not_null<DocumentData*> document) {
			return state->query.isEmpty()
				|| Matches(TrackTitle(document), state->query)
				|| Matches(TrackPerformer(document), state->query);
		};

		auto profile = std::vector<not_null<DocumentData*>>();
		for (const auto item : music->list(self)) {
			const auto document = item->media()
				? item->media()->document()
				: nullptr;
			if (document && matches(document)) {
				profile.push_back(document);
			}
		}
		if (!profile.empty()) {
			Ui::AddSkip(list);
			Ui::AddSubsectionTitle(list, tr::lng_story_audio_profile());
			for (const auto document : profile) {
				addRow(document, Data::FileOrigin());
			}
			Ui::AddSkip(list);
		}

		const auto &shared = state->shared;
		auto sharedCount = 0;
		if (!shared.items.empty()) {
			Ui::AddSkip(list);
			Ui::AddSubsectionTitle(list, tr::lng_story_audio_shared());
			for (const auto item : shared.items) {
				const auto document = item->media()
					? item->media()->document()
					: nullptr;
				if (document) {
					addRow(document, Data::FileOrigin(item->fullId()));
					++sharedCount;
				}
			}
			if (shared.hasMore && !shared.loading) {
				list->add(object_ptr<Ui::SettingsButton>(
					list,
					tr::lng_story_audio_more(),
					st::settingsButtonActive
				))->setClickedCallback([=] {
					state->loadShared(true);
				});
			}
			Ui::AddSkip(list);
		}

		const auto loading = shared.loading
			|| (!music->fullyLoaded(self) && state->query.isEmpty());
		if (profile.empty() && !sharedCount && !loading) {
			Ui::AddSkip(list);
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					tr::lng_story_audio_none(),
					st::boxLabel),
				st::storyAudioEmptyPadding);
			auto info = state->query.isEmpty()
				? tr::lng_story_audio_none_info(Ui::Text::WithEntities)
				: tr::lng_story_audio_none_query(
					lt_query,
					rpl::single(Ui::Text::Bold(state->query)),
					Ui::Text::WithEntities);
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					std::move(info),
					st::boxDividerLabel),
				st::boxRowPadding);
			Ui::AddSkip(list);
		}
		list->resizeToWidth(container->width());
	};
	state->loadShared = [=](bool more) {
		auto &shared = state->shared;
		if (shared.loading) {
			return;
		} else if (!more) {
			if (shared.loaded && shared.query == state->query) {
				return;
			}
			shared.items.clear();
			shared.offsetPosition = Data::MessagePosition();
			shared.offsetRate = 0;
			shared.fullCount = 0;
			shared.hasMore = false;
			shared.loaded = false;
			shared.query = state->query;
		}
		auto request = Api::PrepareGlobalMediaRequest(
			session,
			shared.offsetRate,
			shared.offsetPosition,
			Storage::SharedMediaType::MusicFile,
			shared.query);
		if (!request) {
			return;
		}
		shared.loading = true;
		shared.requestId = session->api().request(
			std::move(*request)
		).done([=](const MTPmessages_Messages &result) {
			auto &shared = state->shared;
			shared.loading = false;
			shared.loaded = true;
			shared.requestId = 0;
			const auto parsed = Api::ParseGlobalMediaResult(
				session,
				result,
				false);
			for (const auto &position : parsed.messageIds) {
				const auto item = session->data().message(position.fullId);
				const auto document = (item && item->media())
					? item->media()->document()
					: nullptr;
				if (document && document->isSong()) {
					shared.items.push_back(item);
				}
			}
			shared.offsetPosition = parsed.offsetPosition;
			shared.offsetRate = parsed.offsetRate;
			shared.fullCount = parsed.fullCount;
			shared.hasMore = !parsed.messageIds.empty()
				&& (int(shared.items.size()) < parsed.fullCount);
			state->rebuild();
		}).fail([=] {
			auto &shared = state->shared;
			shared.loading = false;
			shared.loaded = true;
			shared.requestId = 0;
			state->rebuild();
		}).send();
		state->rebuild();
	};
	state->choose = [=](not_null<AudioRow*> row) {
		if (row->ready()) {
			finish(row->document(), row->media());
			return;
		}
		if (state->downloading && state->downloading != row->document()) {
			state->downloading->cancel();
		}
		state->downloading = row->document();
		state->downloadingMedia = row->media();
		StartLoading(row->document(), row->origin());
		if (!row->document()->loading() && !row->ready()) {
			state->downloading = nullptr;
			state->downloadingMedia = nullptr;
			show->showToast(tr::lng_story_audio_failed(tr::now));
		}
		for (const auto other : state->rows) {
			other->setLoading(other->document() == state->downloading);
		}
	};
	state->rebuild();
	if (!music->fullyLoaded(self)) {
		music->loadMore(self);
	}
	music->changed(
	) | rpl::filter(rpl::mappers::_1 == self) | rpl::on_next([=] {
		state->rebuild();
	}, box->lifetime());
	state->loadShared(false);

	state->searchTimer.setCallback([=] {
		if (state->shared.requestId) {
			session->api().request(state->shared.requestId).cancel();
			state->shared.requestId = 0;
			state->shared.loading = false;
		}
		state->loadShared(false);
	});
	search->queryChanges() | rpl::on_next([=](const QString &query) {
		if (state->query == query) {
			return;
		}
		state->query = query;
		state->rebuild();
		state->searchTimer.callOnce(kSearchDelay);
	}, box->lifetime());

	session->downloaderTaskFinished() | rpl::on_next([=] {
		const auto document = state->downloading;
		if (!document) {
			return;
		}
		const auto media = state->downloadingMedia;
		if (TrackReady(document, media)) {
			state->downloading = nullptr;
			state->downloadingMedia = nullptr;
			finish(document, media);
		} else if (!document->loading()) {
			state->downloading = nullptr;
			state->downloadingMedia = nullptr;
			show->showToast(tr::lng_story_audio_failed(tr::now));
			for (const auto row : state->rows) {
				row->setLoading(false);
			}
		}
	}, box->lifetime());
	session->data().documentLoadProgress(
	) | rpl::on_next([=](not_null<DocumentData*> document) {
		for (const auto row : state->rows) {
			if (row->document() == document) {
				row->update();
			}
		}
	}, box->lifetime());

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ShowStoryAudioBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Editor::StoryAudio)> done) {
	show->showBox(Box(StoryAudioBox, show, std::move(done)));
}
