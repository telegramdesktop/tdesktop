/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/ringtones_box.h"

#include "api/api_ringtones.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_thread.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "platform/platform_notifications_manager.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_menu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kDefaultValue = -1;
constexpr auto kNoSoundValue = -2;
constexpr auto kNoDetachTimeout = crl::time(250);

class AudioCreator final {
public:
	AudioCreator();
	AudioCreator(AudioCreator &&other);
	~AudioCreator();

private:
	rpl::lifetime _lifetime;
	bool _attached = false;

};

AudioCreator::AudioCreator()
: _attached(true) {
	crl::async([] {
		QMutexLocker lock(Media::Player::internal::audioPlayerMutex());
		Media::Audio::AttachToDevice();
	});
	base::timer_each(
		kNoDetachTimeout
	) | rpl::start_with_next([=] {
		Media::Audio::StopDetachIfNotUsedSafe();
	}, _lifetime);
}

AudioCreator::AudioCreator(AudioCreator &&other)
: _lifetime(base::take(other._lifetime))
, _attached(base::take(other._attached)) {
}

AudioCreator::~AudioCreator() {
	if (_attached) {
		Media::Audio::ScheduleDetachIfNotUsedSafe();
	}
}

} // namespace

QString ExtractRingtoneName(not_null<DocumentData*> document) {
	if (document->isNull()) {
		return QString();
	}
	const auto name = document->filename();
	if (!name.isEmpty()) {
		const auto extension = Data::FileExtension(name);
		if (extension.isEmpty()) {
			return name;
		} else if (name.size() > extension.size() + 1) {
			return name.mid(0, name.size() - extension.size() - 1);
		}
	}
	const auto date = langDateTime(
		base::unixtime::parse(document->date));
	const auto base = document->isVoiceMessage()
		? (tr::lng_in_dlg_audio(tr::now) + ' ')
		: document->isAudioFile()
		? (tr::lng_in_dlg_audio_file(tr::now) + ' ')
		: QString();
	return base + date;
}

void RingtonesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Data::NotifySound selected,
		Fn<void(Data::NotifySound)> save) {
	box->setTitle(tr::lng_ringtones_box_title());

	const auto container = box->verticalLayout();

	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());

	struct State {
		AudioCreator creator;
		std::shared_ptr<Ui::RadiobuttonGroup> group;
		std::vector<std::shared_ptr<Data::DocumentMedia>> medias;
		Data::NotifySound chosen;
		base::unique_qptr<Ui::PopupMenu> menu;
		QPointer<Ui::Radiobutton> defaultButton;
		QPointer<Ui::Radiobutton> chosenButton;
		std::vector<QPointer<Ui::Radiobutton>> buttons;
	};
	const auto state = container->lifetime().make_state<State>(State{
		.group = std::make_shared<Ui::RadiobuttonGroup>(),
		.chosen = selected,
	});

	const auto addToGroup = [=](
			not_null<Ui::VerticalLayout*> verticalLayout,
			int value,
			const QString &text,
			bool chosen) {
		if (chosen) {
			state->group->setValue(value);
		}
		const auto button = verticalLayout->add(
			object_ptr<Ui::Radiobutton>(
				verticalLayout,
				state->group,
				value,
				text,
				st::defaultCheckbox),
			padding);
		if (chosen) {
			state->chosenButton = button;
		}
		if (value == kDefaultValue) {
			state->defaultButton = button;
			button->setClickedCallback([=] {
				Core::App().notifications().playSound(session, 0);
			});
		}
		if (value < 0) {
			return;
		}
		while (state->buttons.size() <= value) {
			state->buttons.push_back(nullptr);
		}
		button->setClickedCallback([=] {
			const auto media = state->medias[value].get();
			if (media->loaded()) {
				Core::App().notifications().playSound(
					session,
					media->owner()->id);
			}
		});
		base::install_event_filter(button, [=](not_null<QEvent*> e) {
			if (e->type() != QEvent::ContextMenu || state->menu) {
				return base::EventFilterResult::Continue;
			}
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				button,
				st::popupMenuWithIcons);
			auto callback = [=] {
				const auto id = state->medias[value]->owner()->id;
				session->api().ringtones().remove(id);
			};
			state->menu->addAction(
				tr::lng_box_delete(tr::now),
				std::move(callback),
				&st::menuIconDelete);
			state->menu->popup(QCursor::pos());
			return base::EventFilterResult::Cancel;
		});
	};

	session->api().ringtones().uploadFails(
	) | rpl::start_with_next([=](const QString &error) {
		if ((error == u"RINGTONE_DURATION_TOO_LONG"_q)) {
			box->getDelegate()->show(Ui::MakeInformBox(
				tr::lng_ringtones_error_max_duration(
					tr::now,
					lt_duration,
					Ui::FormatMuteFor(
						session->api().ringtones().maxDuration()))));
		} else if ((error == u"RINGTONE_SIZE_TOO_BIG"_q)) {
			box->getDelegate()->show(Ui::MakeInformBox(
				tr::lng_ringtones_error_max_size(
					tr::now,
					lt_size,
					Ui::FormatSizeText(
						session->api().ringtones().maxSize()))));
		} else if (error == u"RINGTONE_MIME_INVALID"_q) {
			box->getDelegate()->show(
				Ui::MakeInformBox(tr::lng_edit_media_invalid_file()));
		}
	}, box->lifetime());

	Ui::AddSubsectionTitle(
		container,
		tr::lng_ringtones_box_cloud_subtitle());

	const auto noSound = selected.none;
	addToGroup(
		container,
		kDefaultValue,
		tr::lng_ringtones_box_default(tr::now),
		false);
	addToGroup(
		container,
		kNoSoundValue,
		tr::lng_ringtones_box_no_sound(tr::now),
		noSound);

	const auto custom = container->add(
		object_ptr<Ui::VerticalLayout>(container));

	const auto rebuild = [=] {
		const auto old = base::take(state->medias);
		auto value = 0;
		while (custom->count()) {
			delete custom->widgetAt(0);
		}

		for (const auto &id : session->api().ringtones().list()) {
			const auto chosen = (state->chosen.id && state->chosen.id == id);
			const auto document = session->data().document(id);
			const auto text = ExtractRingtoneName(document);
			addToGroup(custom, value++, text, chosen);
			state->medias.push_back(document->createMediaView());
			document->owner().notifySettings().cacheSound(document);
		}

		custom->resizeToWidth(container->width());
		if (!state->chosenButton) {
			state->group->setValue(kDefaultValue);
			state->defaultButton->finishAnimating();
		}
	};

	session->api().ringtones().listUpdates(
	) | rpl::start_with_next(rebuild, container->lifetime());

	session->api().ringtones().uploadDones(
	) | rpl::start_with_next([=](DocumentId id) {
		state->chosen = Data::NotifySound{ .id = id };
		rebuild();
	}, container->lifetime());

	session->api().ringtones().requestList();
	rebuild();

	const auto upload = box->addRow(
		Settings::CreateButtonWithIcon(
			container,
			tr::lng_ringtones_box_upload_button(),
			st::ringtonesBoxButton,
			{
				&st::settingsIconAdd,
				Settings::IconType::Round,
				&st::windowBgActive
			}),
		style::margins());
	upload->addClickHandler([=] {
		const auto delay = st::ringtonesBoxButton.ripple.hideDuration;
		base::call_delayed(delay, crl::guard(box, [=] {
			const auto callback = [=](const FileDialog::OpenResult &result) {
				auto mime = QString();
				auto name = QString();
				auto content = result.remoteContent;
				if (!result.paths.isEmpty()) {
					auto info = QFileInfo(result.paths.front());
					mime = Core::MimeTypeForFile(info).name();
					name = info.fileName();
					auto f = QFile(result.paths.front());
					if (f.open(QIODevice::ReadOnly)) {
						content = f.readAll();
						f.close();
					}
				} else {
					mime = Core::MimeTypeForData(content).name();
					name = "audio";
				}
				const auto &ringtones = session->api().ringtones();
				if (int(content.size()) > ringtones.maxSize()) {
					box->getDelegate()->show(Ui::MakeInformBox(
						tr::lng_ringtones_error_max_size(
							tr::now,
							lt_size,
							Ui::FormatSizeText(ringtones.maxSize()))));
					return;
				}

				session->api().ringtones().upload(name, mime, content);
			};
			FileDialog::GetOpenPath(
				box.get(),
				tr::lng_ringtones_box_upload_choose(tr::now),
				"Audio files (*.mp3)",
				crl::guard(box, callback));
		}));
	});

	box->addSkip(st::ringtonesBoxSkip);
	Ui::AddDividerText(container, tr::lng_ringtones_box_about());

	box->addSkip(st::ringtonesBoxSkip);

	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_settings_save(), [=] {
		const auto value = state->group->value();
		auto sound = (value == kDefaultValue)
			? Data::NotifySound()
			: (value == kNoSoundValue)
			? Data::NotifySound{ .none = true }
			: Data::NotifySound{ .id = state->medias[value]->owner()->id };
		save(sound);
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ThreadRingtonesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Data::Thread*> thread) {
	const auto now = thread->owner().notifySettings().sound(thread);
	RingtonesBox(box, &thread->session(), now, [=](Data::NotifySound sound) {
		thread->owner().notifySettings().update(thread, {}, {}, sound);
	});
}
