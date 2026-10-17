/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_chat_show.h"

#include "chat_helpers/compose/compose_show.h"
#include "data/data_file_origin.h"
#include "main/session/session_show.h"
#include "menu/menu_send_details.h"
#include "ui/delayed_activation.h"

#include <rpl/never.h>

namespace Wallet {
namespace {

class ChatShow final : public ChatHelpers::Show {
public:
	ChatShow(std::shared_ptr<Main::SessionShow> show, bool resolveWindows);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

	[[nodiscard]] Main::Session &session() const override;
	[[nodiscard]] Window::SessionController *resolveWindow() const override;
	[[nodiscard]] bool canResolveWindow() const override;

	void activate() override;
	[[nodiscard]] bool paused(
		ChatHelpers::PauseReason reason) const override;
	[[nodiscard]] rpl::producer<> pauseChanged() const override;
	[[nodiscard]] SendMenu::Details sendMenuDetails() const override;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) const override;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) const override;
	void processChosenSticker(
		ChatHelpers::FileChosen &&chosen) const override;

private:
	const std::shared_ptr<Main::SessionShow> _show;
	const bool _resolveWindows = false;

};

ChatShow::ChatShow(
	std::shared_ptr<Main::SessionShow> show,
	bool resolveWindows)
: _show(std::move(show))
, _resolveWindows(resolveWindows) {
}

void ChatShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	_show->showOrHideBoxOrLayer(std::move(layer), options, animated);
}

not_null<QWidget*> ChatShow::toastParent() const {
	return _show->toastParent();
}

bool ChatShow::valid() const {
	return _show->valid();
}

ChatShow::operator bool() const {
	return valid();
}

Main::Session &ChatShow::session() const {
	return _show->session();
}

Window::SessionController *ChatShow::resolveWindow() const {
	return _resolveWindows ? ChatHelpers::Show::resolveWindow() : nullptr;
}

bool ChatShow::canResolveWindow() const {
	return _resolveWindows;
}

void ChatShow::activate() {
	if (_show->valid()) {
		Ui::ActivateWindow(_show->toastParent());
	}
}

bool ChatShow::paused(ChatHelpers::PauseReason) const {
	return !_show->valid();
}

rpl::producer<> ChatShow::pauseChanged() const {
	return rpl::never<>();
}

SendMenu::Details ChatShow::sendMenuDetails() const {
	return { SendMenu::Type::Disabled };
}

bool ChatShow::showMediaPreview(
		Data::FileOrigin,
		not_null<DocumentData*>) const {
	return false;
}

bool ChatShow::showMediaPreview(
		Data::FileOrigin,
		not_null<PhotoData*>) const {
	return false;
}

void ChatShow::processChosenSticker(ChatHelpers::FileChosen &&) const {
}

} // namespace

std::shared_ptr<ChatHelpers::Show> MakeChatShow(
		std::shared_ptr<Main::SessionShow> show,
		bool resolveWindows) {
	return std::make_shared<ChatShow>(std::move(show), resolveWindows);
}

} // namespace Wallet
