/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/transfer_gift_box.h"

#include "apiwrap.h"
#include "api/api_credits.h"
#include "api/api_cloud_password.h"
#include "base/unixtime.h"
#include "boxes/passcode_box.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "boxes/filters/edit_filter_chats_list.h" // CreatePe...tionSubtitle.
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/star_gift_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h" // peerListSingleRow.
#include "styles/style_dialogs.h" // recentPeersSpecialName.
#include "styles/style_layers.h" // boxLabel.

namespace {

struct ExportOption {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<bool(int, int, int)> overrideKey;
	Fn<void()> activate;
};

class Controller final : public ContactsBoxController {
public:
	Controller(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		Fn<void(not_null<PeerData*>, Fn<void()>)> choose);

	void init(not_null<PeerListBox*> box);

	void noSearchSubmit();

private:
	void prepareViewHook() override;

	bool overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;
	void rowClicked(not_null<PeerListRow*> row) override;

	const not_null<Window::SessionController*> _window;
	const std::shared_ptr<Data::UniqueGift> _gift;
	const Data::SavedStarGiftId _giftId;
	const Fn<void(not_null<PeerData*>, Fn<void()>)> _choose;
	ExportOption _exportOption;
	QPointer<PeerListBox> _box;

};

void ConfirmExportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Data::UniqueGift> gift,
		Fn<void(Fn<void()> close)> confirmed) {
	box->setTitle(tr::lng_gift_transfer_confirm_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_gift_transfer_confirm_text(
			lt_name,
			rpl::single(Ui::Text::Bold(UniqueGiftName(*gift))),
			Ui::Text::WithEntities),
		st::boxLabel));
	box->addButton(tr::lng_gift_transfer_confirm_button(), [=] {
		confirmed([weak = Ui::MakeWeak(box)] {
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		});
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ExportOnBlockchain(
		not_null<Window::SessionController*> window,
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId giftId,
		Fn<void()> boxShown,
		Fn<void()> wentToUrl) {
	struct State {
		bool loading = false;
		rpl::lifetime lifetime;
	};
	const auto state = std::make_shared<State>();
	const auto session = &window->session();
	const auto show = window->uiShow();
	session->api().cloudPassword().reload();
	session->api().request(
		MTPpayments_GetStarGiftWithdrawalUrl(
			Api::InputSavedStarGiftId(giftId),
			MTP_inputCheckPasswordEmpty())
	).fail([=](const MTP::Error &error) {
		auto box = PrePasswordErrorBox(
			error.type(),
			session,
			TextWithEntities{
				tr::lng_gift_transfer_password_about(tr::now),
			});
		if (box) {
			show->show(std::move(box));
			boxShown();
			return;
		}
		state->lifetime = session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Core::CloudPasswordState &pass) {
			auto fields = PasscodeBox::CloudFields::From(pass);
			fields.customTitle = tr::lng_gift_transfer_password_title();
			fields.customDescription
				= tr::lng_gift_transfer_password_description(tr::now);
			fields.customSubmitButton = tr::lng_passcode_submit();
			fields.customCheckCallback = crl::guard(parent, [=](
					const Core::CloudPasswordResult &result,
					QPointer<PasscodeBox> box) {
				using ExportUrl = MTPpayments_StarGiftWithdrawalUrl;
				session->api().request(
					MTPpayments_GetStarGiftWithdrawalUrl(
						Api::InputSavedStarGiftId(giftId),
						result.result)
				).done([=](const ExportUrl &result) {
					UrlClickHandler::Open(qs(result.data().vurl()));
					wentToUrl();
					if (box) {
						box->closeBox();
					}
				}).fail([=](const MTP::Error &error) {
					const auto message = error.type();
					if (box && !box->handleCustomCheckError(message)) {
						show->showToast(message);
					}
				}).send();
			});
			show->show(Box<PasscodeBox>(session, fields));
			boxShown();
		});
	}).send();
}

[[nodiscard]] ExportOption MakeExportOption(
		not_null<Window::SessionController*> window,
		not_null<PeerListBox*> box,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId giftId,
		TimeId when) {
	struct State {
		bool exporting = false;
	};
	const auto state = std::make_shared<State>();
	const auto activate = [=] {
		const auto now = base::unixtime::now();
		const auto weak = Ui::MakeWeak(box);
		const auto left = (when > now) ? (when - now) : 0;
		const auto hours = left ? std::max((left + 1800) / 3600, 1) : 0;
		if (!hours) {
			window->show(Box(ConfirmExportBox, gift, [=](Fn<void()> close) {
				if (state->exporting) {
					return;
				}
				state->exporting = true;
				ExportOnBlockchain(window, box, gift, giftId, [=] {
					state->exporting = false;
					close();
				}, [=] {
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
					close();
				});
			}));
			return;
		}
		window->show(Ui::MakeInformBox({
			.text = tr::lng_gift_transfer_unlocks_about(
				lt_when,
				((hours >= 24)
					? tr::lng_gift_transfer_unlocks_when_days(
						lt_count,
						rpl::single((hours / 24) * 1.))
					: tr::lng_gift_transfer_unlocks_when_hours(
						lt_count,
						rpl::single(hours * 1.)))),
			.title = tr::lng_gift_transfer_unlocks_title(),
		}));
	};

	class ExportRow final : public PeerListRow {
	public:
		explicit ExportRow(TimeId when)
		: PeerListRow(Data::FakePeerIdForJustName("ton-export").value) {
			const auto now = base::unixtime::now();
			_available = (when <= now);
			if (const auto left = when - now; left > 0) {
				const auto hours = std::max((left + 1800) / 3600, 1);
				const auto days = hours / 24;
				setCustomStatus(days
					? tr::lng_gift_transfer_unlocks_days(
						tr::now,
						lt_count,
						days)
					: tr::lng_gift_transfer_unlocks_hours(
						tr::now,
						lt_count,
						hours));
			}
		}

		QString generateName() override {
			return tr::lng_gift_transfer_via_blockchain(tr::now);
		}
		QString generateShortName() override {
			return generateName();
		}
		auto generatePaintUserpicCallback(bool forceRound)
		-> PaintRoundImageCallback override {
			return [=](
					Painter &p,
					int x,
					int y,
					int outerWidth,
					int size) mutable {
				Ui::EmptyUserpic::PaintCurrency(p, x, y, outerWidth, size);
			};
		}

		const style::PeerListItem &computeSt(
				const style::PeerListItem &st) const override {
			_st = st;
			_st.namePosition.setY(
				st::recentPeersSpecialName.namePosition.y());
			return _available ? _st : st;
		}

	private:
		mutable style::PeerListItem _st;
		bool _available = false;

	};

	class ExportController final : public PeerListController {
	public:
		ExportController(
			not_null<Main::Session*> session,
			TimeId when,
			Fn<void()> activate)
		: _session(session)
		, _when(when)
		, _activate(std::move(activate)) {
		}

		void prepare() override {
			delegate()->peerListAppendRow(
				std::make_unique<ExportRow>(_when));
			delegate()->peerListRefreshRows();
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_activate();
		}
		Main::Session &session() const override {
			return *_session;
		}

	private:
		const not_null<Main::Session*> _session;
		TimeId _when = 0;
		Fn<void()> _activate;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	Ui::AddSkip(container);

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		ExportController
	>(&window->session(), when, activate);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_contacts_header()));

	const auto overrideKey = [=](int direction, int from, int to) {
		if (!content->isVisible()) {
			return false;
		} else if (direction > 0 && from < 0 && to >= 0) {
			if (content->hasSelection()) {
				const auto was = content->selectedIndex();
				const auto now = content->selectSkip(1).reallyMovedTo;
				if (was != now) {
					return true;
				}
				content->clearSelection();
			} else {
				content->selectSkip(1);
				return true;
			}
		} else if (direction < 0 && to < 0) {
			if (!content->hasSelection()) {
				content->selectLast();
			} else if (from >= 0 || content->hasSelection()) {
				content->selectSkip(-1);
			}
		}
		return false;
	};

	return {
		.content = std::move(result),
		.overrideKey = overrideKey,
		.activate = activate,
	};
}

Controller::Controller(
	not_null<Window::SessionController*> window,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId giftId,
	Fn<void(not_null<PeerData*>, Fn<void()>)> choose)
: ContactsBoxController(&window->session())
, _window(window)
, _gift(std::move(gift))
, _giftId(giftId)
, _choose(std::move(choose)) {
	if (_gift->exportAt) {
		setStyleOverrides(&st::peerListSmallSkips);
	}
}

void Controller::init(not_null<PeerListBox*> box) {
	_box = box;
	if (const auto when = _gift->exportAt) {
		_exportOption = MakeExportOption(_window, box, _gift, _giftId, when);
		delegate()->peerListSetAboveWidget(std::move(_exportOption.content));
		delegate()->peerListRefreshRows();
	}
}

void Controller::noSearchSubmit() {
	if (const auto onstack = _exportOption.activate) {
		onstack();
	}
}

bool Controller::overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) {
	return _exportOption.overrideKey
		&& _exportOption.overrideKey(direction, fromIndex, toIndex);
}

void Controller::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_gift_transfer_title(
		lt_name,
		rpl::single(UniqueGiftName(*_gift))));
}

std::unique_ptr<PeerListRow> Controller::createRow(
	not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return ContactsBoxController::createRow(user);
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_choose(row->peer(), [parentBox = _box] {
		if (const auto strong = parentBox.data()) {
			strong->closeBox();
		}
	});
}

void TransferGift(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> to,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		Fn<void(Payments::CheckoutResult)> done) {
	Expects(to->isUser());

	const auto session = &window->session();
	const auto weak = base::make_weak(window);
	auto formDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		done(result);
		if (result == Payments::CheckoutResult::Paid) {
			if (const auto strong = weak.get()) {
				strong->session().data().notifyGiftUpdate({
					.id = savedId,
					.action = Data::GiftUpdate::Action::Transfer,
				});
				Ui::ShowGiftTransferredToast(strong, to, *gift);
			}
		}
	};
	if (gift->starsForTransfer <= 0) {
		session->api().request(MTPpayments_TransferStarGift(
			Api::InputSavedStarGiftId(savedId),
			to->input
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			formDone(Payments::CheckoutResult::Paid, &result);
		}).fail([=](const MTP::Error &error) {
			formDone(Payments::CheckoutResult::Failed, nullptr);
			if (const auto strong = weak.get()) {
				strong->showToast(error.type());
			}
		}).send();
		return;
	}
	Ui::RequestStarsFormAndSubmit(
		window,
		MTP_inputInvoiceStarGiftTransfer(
			Api::InputSavedStarGiftId(savedId),
			to->input),
		std::move(formDone));
}

void ShowTransferToBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		Fn<void()> closeParentBox) {
	const auto stars = gift->starsForTransfer;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_gift_transfer_title(
			lt_name,
			rpl::single(UniqueGiftName(*gift))));

		auto transfer = (stars > 0)
			? tr::lng_gift_transfer_button_for(
				lt_price,
				tr::lng_action_gift_for_stars(
					lt_count,
					rpl::single(stars * 1.)))
			: tr::lng_gift_transfer_button();

		struct State {
			bool sent = false;
		};
		const auto state = std::make_shared<State>();
		auto callback = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			const auto weak = Ui::MakeWeak(box);
			const auto done = [=](Payments::CheckoutResult result) {
				if (result == Payments::CheckoutResult::Cancelled) {
					closeParentBox();
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
				} else if (result != Payments::CheckoutResult::Paid) {
					state->sent = false;
				} else {
					if (savedId.isUser()) {
						controller->showPeerHistory(peer);
					}
					closeParentBox();
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
				}
			};
			TransferGift(controller, peer, gift, savedId, done);
		};

		Ui::ConfirmBox(box, {
			.text = (stars > 0)
				? tr::lng_gift_transfer_sure_for(
					lt_name,
					rpl::single(Ui::Text::Bold(UniqueGiftName(*gift))),
					lt_recipient,
					rpl::single(Ui::Text::Bold(peer->shortName())),
					lt_price,
					tr::lng_action_gift_for_stars(
						lt_count,
						rpl::single(stars * 1.),
						Ui::Text::Bold),
					Ui::Text::WithEntities)
				: tr::lng_gift_transfer_sure(
					lt_name,
					rpl::single(Ui::Text::Bold(UniqueGiftName(*gift))),
					lt_recipient,
					rpl::single(Ui::Text::Bold(peer->shortName())),
					Ui::Text::WithEntities),
			.confirmed = std::move(callback),
			.confirmText = std::move(transfer),
		});
	}));
}

} // namespace

void ShowTransferGiftBox(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId) {
	auto controller = std::make_unique<Controller>(
		window,
		gift,
		savedId,
		[=](not_null<PeerData*> peer, Fn<void()> done) {
			ShowTransferToBox(window, peer, gift, savedId, done);
		});
	const auto controllerRaw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		controllerRaw->init(box);

		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

		box->noSearchSubmits() | rpl::start_with_next([=] {
			controllerRaw->noSearchSubmit();
		}, box->lifetime());
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}
