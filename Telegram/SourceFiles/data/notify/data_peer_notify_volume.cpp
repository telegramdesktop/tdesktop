/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/notify/data_peer_notify_volume.h"

#include "data/data_peer.h"
#include "data/data_thread.h"
#include "data/notify/data_peer_notify_settings.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "platform/platform_notifications_manager.h"
#include "settings/settings_common.h"
#include "ui/vertical_list.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

namespace Data {

Data::VolumeController DefaultRingtonesVolumeController(
		not_null<Main::Session*> session,
		Data::DefaultNotify defaultNotify) {
	return Data::VolumeController(
		[=]() -> ushort {
			const auto volume = session->settings().ringtoneVolume(
				defaultNotify);
			return volume ? volume : 100;
		}, [=](ushort volume) {
			session->settings().setRingtoneVolume(defaultNotify, volume);
			session->saveSettingsDelayed();
		});
}

Data::VolumeController ThreadRingtonesVolumeController(
		not_null<Data::Thread*> thread) {
	return Data::VolumeController(
		[=]() -> ushort {
			const auto volume = thread->session().settings().ringtoneVolume(
				thread->peer()->id,
				thread->topicRootId(),
				thread->monoforumPeerId());
			return volume ? volume : 100;
		}, [=](ushort volume) {
			thread->session().settings().setRingtoneVolume(
				thread->peer()->id,
				thread->topicRootId(),
				thread->monoforumPeerId(),
				volume);
			thread->session().saveSettingsDelayed();
		});
}

} // namespace Data

namespace Ui {

void AddRingtonesVolumeSlider(
		not_null<Ui::VerticalLayout*> container,
		bool toggled,
		rpl::producer<bool> toggleOn,
		rpl::producer<QString> subtitle,
		Data::VolumeController volumeController) {
	if (!Platform::Notifications::VolumeSupported()) {
		return;
	}
	Expects(volumeController.volume && volumeController.saveVolume);

	const auto volumeWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	volumeWrap->toggle(toggled, anim::type::instant);
	volumeWrap->finishAnimating();
	volumeWrap->toggleOn(std::move(toggleOn), anim::type::normal);

	Ui::AddSubsectionTitle(volumeWrap->entity(), std::move(subtitle));
	auto sliderWithLabel = Settings::MakeSliderWithLabel(
		volumeWrap->entity(),
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("100%"),
		true);
	const auto slider = sliderWithLabel.slider;
	const auto label = sliderWithLabel.label;

	volumeWrap->entity()->add(
		std::move(sliderWithLabel.widget),
		st::settingsBigScalePadding);

	const auto updateLabel = [=](int volume) {
		label->setText(QString::number(volume) + '%');
	};

	slider->setPseudoDiscrete(
		100,
		[=](int index) { return index + 1; },
		int(volumeController.volume()),
		updateLabel,
		[saveVolume = volumeController.saveVolume](int volume) {
			saveVolume(volume);
		});
	updateLabel(volumeController.volume());
}

} // namespace Ui
