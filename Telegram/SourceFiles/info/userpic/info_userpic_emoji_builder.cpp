/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder.h"

#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_layer.h"
#include "info/userpic/info_userpic_emoji_builder_widget.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info_userpic_builder.h"

namespace UserpicBuilder {

void ShowLayer(
		not_null<Window::SessionController*> controller,
		StartData data,
		Fn<void(UserpicBuilder::Result)> &&doneCallback) {
	auto layer = std::make_unique<LayerWidget>();
	const auto layerRaw = layer.get();
	{
		struct State {
			rpl::event_stream<> clicks;
		};
		const auto state = layer->lifetime().make_state<State>();

		const auto content = CreateUserpicBuilder(
			layerRaw,
			controller,
			data,
			BothWayCommunication<UserpicBuilder::Result>{
				.triggers = state->clicks.events(),
				.result = [=, done = std::move(doneCallback)](Result r) {
					done(std::move(r));
					layerRaw->closeLayer();
				},
			});
		const auto save = Ui::CreateChild<Ui::RoundButton>(
			content.get(),
			tr::lng_connection_save(),
			st::userpicBuilderEmojiButton);
		save->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		content->sizeValue(
		) | rpl::start_with_next([=] {
			const auto &p = st::userpicBuilderEmojiSavePosiiton;
			save->moveToRight(p.x(), p.y());
		}, save->lifetime());

		save->clicks() | rpl::to_empty | rpl::start_to_stream(
			state->clicks,
			save->lifetime());

		const auto back = Ui::CreateChild<Ui::IconButton>(
			content.get(),
			st::userpicBuilderEmojiBackButton);
		back->setClickedCallback([=] {
			layerRaw->closeLayer();
		});
		content->sizeValue(
		) | rpl::start_with_next([=] {
			const auto &p = st::userpicBuilderEmojiBackPosiiton;
			back->moveToLeft(p.x(), p.y());
		}, back->lifetime());

		layer->setContent(content);
	}

	controller->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

} // namespace UserpicBuilder

