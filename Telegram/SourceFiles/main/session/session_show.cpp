/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/session/session_show.h"

namespace Main {
namespace {

class SimpleSessionShow final : public SessionShow {
public:
	SimpleSessionShow(
		std::shared_ptr<Ui::Show> show,
		not_null<Session*> session);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	not_null<QWidget*> toastParent() const override;
	bool valid() const override;
	operator bool() const override;

	Session &session() const override;

private:
	const std::shared_ptr<Ui::Show> _show;
	const not_null<Session*> _session;

};

SimpleSessionShow::SimpleSessionShow(
	std::shared_ptr<Ui::Show> show,
	not_null<Session*> session)
: _show(std::move(show))
, _session(session) {
}

void SimpleSessionShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	_show->showOrHideBoxOrLayer(std::move(layer), options, animated);
}

not_null<QWidget*> SimpleSessionShow::toastParent() const {
	return _show->toastParent();
}

bool SimpleSessionShow::valid() const {
	return _show->valid();
}

SimpleSessionShow::operator bool() const {
	return _show->operator bool();
}

Session &SimpleSessionShow::session() const {
	return *_session;
}

} // namespace

std::shared_ptr<SessionShow> MakeSessionShow(
		std::shared_ptr<Ui::Show> show,
		not_null<Session*> session) {
	return std::make_shared<SimpleSessionShow>(std::move(show), session);
}

} // namespace Main
