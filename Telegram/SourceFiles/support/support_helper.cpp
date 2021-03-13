/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_helper.h"

#include "dialogs/dialogs_key.h"
#include "data/data_drafts.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "api/api_text_entities.h"
#include "history/history.h"
#include "boxes/abstract_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/input_fields.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/format_values.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "facades.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Main {
class Session;
} // namespace Main

namespace Support {
namespace {

constexpr auto kOccupyFor = TimeId(60);
constexpr auto kReoccupyEach = 30 * crl::time(1000);
constexpr auto kMaxSupportInfoLength = MaxMessageSize * 4;

class EditInfoBox : public Ui::BoxContent {
public:
	EditInfoBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		const TextWithTags &text,
		Fn<void(TextWithTags, Fn<void(bool success)>)> submit);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	const not_null<Window::SessionController*> _controller;
	object_ptr<Ui::InputField> _field = { nullptr };
	Fn<void(TextWithTags, Fn<void(bool success)>)> _submit;

};

EditInfoBox::EditInfoBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const TextWithTags &text,
	Fn<void(TextWithTags, Fn<void(bool success)>)> submit)
: _controller(controller)
, _field(
	this,
	st::supportInfoField,
	Ui::InputField::Mode::MultiLine,
	rpl::single(qsl("Support information")), // #TODO hard_lang
	text)
, _submit(std::move(submit)) {
	_field->setMaxLength(kMaxSupportInfoLength);
	_field->setSubmitSettings(
		Core::App().settings().sendSubmitWay());
	_field->setInstantReplaces(Ui::InstantReplaces::Default());
	_field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	_field->setMarkdownReplacesEnabled(rpl::single(true));
	_field->setEditLinkCallback(DefaultEditLinkCallback(controller, _field));
}

void EditInfoBox::prepare() {
	setTitle(rpl::single(qsl("Edit support information"))); // #TODO hard_lang

	const auto save = [=] {
		const auto done = crl::guard(this, [=](bool success) {
			if (success) {
				closeBox();
			} else {
				_field->showError();
			}
		});
		_submit(_field->getTextWithAppliedMarkdown(), done);
	};
	addButton(tr::lng_settings_save(), save);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	connect(_field, &Ui::InputField::submitted, save);
	connect(_field, &Ui::InputField::cancelled, [=] { closeBox(); });
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_field,
		&_controller->session());

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);

	widthValue(
	) | rpl::start_with_next([=](int width) {
		_field->resizeToWidth(
			width - st::boxPadding.left() - st::boxPadding.right());
		_field->moveToLeft(st::boxPadding.left(), st::boxPadding.bottom());
	}, _field->lifetime());

	_field->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			st::boxPadding.bottom() + height + st::boxPadding.bottom());
	}, _field->lifetime());
}

void EditInfoBox::setInnerFocus() {
	_field->setFocusFast();
}

uint32 OccupationTag() {
	return uint32(Core::Sandbox::Instance().installationTag() & 0xFFFFFFFF);
}

QString NormalizeName(QString name) {
	return name.replace(':', '_').replace(';', '_');
}

Data::Draft OccupiedDraft(const QString &normalizedName) {
	const auto now = base::unixtime::now(), till = now + kOccupyFor;
	return {
		TextWithTags{ "t:"
			+ QString::number(till)
			+ ";u:"
			+ QString::number(OccupationTag())
			+ ";n:"
			+ normalizedName },
		MsgId(0),
		MessageCursor(),
		Data::PreviewState::Allowed
	};
}

[[nodiscard]] bool TrackHistoryOccupation(History *history) {
	if (!history) {
		return false;
	} else if (const auto user = history->peer->asUser()) {
		return !user->isBot();
	}
	return false;
}

uint32 ParseOccupationTag(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = uint32();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= base::unixtime::now()) {
				valid = true;
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			result = part.mid(2).toUInt();
		}
	}
	return valid ? result : 0;
}

QString ParseOccupationName(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return QString();
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return QString();
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = QString();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= base::unixtime::now()) {
				valid = true;
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("n:"))) {
#ifndef OS_MAC_OLD
			result = part.mid(2).toString();
#else // OS_MAC_OLD
			result = part.mid(2);
#endif // OS_MAC_OLD
		}
	}
	return valid ? result : QString();
}

TimeId OccupiedBySomeoneTill(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = TimeId();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= base::unixtime::now()) {
				result = part.mid(2).toInt();
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			if (part.mid(2).toUInt() != OccupationTag()) {
				valid = true;
			} else {
				return 0;
			}
		}
	}
	return valid ? result : 0;
}

} // namespace

Helper::Helper(not_null<Main::Session*> session)
: _session(session)
, _api(&_session->mtp())
, _templates(_session)
, _reoccupyTimer([=] { reoccupy(); })
, _checkOccupiedTimer([=] { checkOccupiedChats(); }) {
	_api.request(MTPhelp_GetSupportName(
	)).done([=](const MTPhelp_SupportName &result) {
		result.match([&](const MTPDhelp_supportName &data) {
			setSupportName(qs(data.vname()));
		});
	}).fail([=](const MTP::Error &error) {
		setSupportName(
			qsl("[rand^")
			+ QString::number(Core::Sandbox::Instance().installationTag())
			+ ']');
	}).send();
}

std::unique_ptr<Helper> Helper::Create(not_null<Main::Session*> session) {
	//return std::make_unique<Helper>(session); AssertIsDebug();
	const auto valid = session->user()->phone().startsWith(qstr("424"));
	return valid ? std::make_unique<Helper>(session) : nullptr;
}

void Helper::registerWindow(not_null<Window::SessionController*> controller) {
	controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto history = key.history();
		return TrackHistoryOccupation(history) ? history : nullptr;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](History *history) {
		updateOccupiedHistory(controller, history);
	}, controller->lifetime());
}

void Helper::cloudDraftChanged(not_null<History*> history) {
	chatOccupiedUpdated(history);
	if (history != _occupiedHistory) {
		return;
	}
	occupyIfNotYet();
}

void Helper::chatOccupiedUpdated(not_null<History*> history) {
	if (const auto till = OccupiedBySomeoneTill(history)) {
		_occupiedChats[history] = till + 2;
		history->session().changes().historyUpdated(
			history,
			Data::HistoryUpdate::Flag::ChatOccupied);
		checkOccupiedChats();
	} else if (_occupiedChats.take(history)) {
		history->session().changes().historyUpdated(
			history,
			Data::HistoryUpdate::Flag::ChatOccupied);
	}
}

void Helper::checkOccupiedChats() {
	const auto now = base::unixtime::now();
	while (!_occupiedChats.empty()) {
		const auto nearest = ranges::min_element(
			_occupiedChats,
			std::less<>(),
			[](const auto &pair) { return pair.second; });
		if (nearest->second <= now) {
			const auto history = nearest->first;
			_occupiedChats.erase(nearest);
			history->session().changes().historyUpdated(
				history,
				Data::HistoryUpdate::Flag::ChatOccupied);
		} else {
			_checkOccupiedTimer.callOnce(
				(nearest->second - now) * crl::time(1000));
			return;
		}
	}
	_checkOccupiedTimer.cancel();
}

void Helper::updateOccupiedHistory(
		not_null<Window::SessionController*> controller,
		History *history) {
	if (isOccupiedByMe(_occupiedHistory)) {
		_occupiedHistory->clearCloudDraft();
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
	_occupiedHistory = history;
	occupyInDraft();
}

void Helper::setSupportName(const QString &name) {
	_supportName = name;
	_supportNameNormalized = NormalizeName(name);
	occupyIfNotYet();
}

void Helper::occupyIfNotYet() {
	if (!isOccupiedByMe(_occupiedHistory)) {
		occupyInDraft();
	}
}

void Helper::occupyInDraft() {
	if (_occupiedHistory
		&& !isOccupiedBySomeone(_occupiedHistory)
		&& !_supportName.isEmpty()) {
		const auto draft = OccupiedDraft(_supportNameNormalized);
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
		_reoccupyTimer.callEach(kReoccupyEach);
	}
}

void Helper::reoccupy() {
	if (isOccupiedByMe(_occupiedHistory)) {
		const auto draft = OccupiedDraft(_supportNameNormalized);
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
}

bool Helper::isOccupiedByMe(History *history) const {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag == OccupationTag());
	}
	return false;
}

bool Helper::isOccupiedBySomeone(History *history) const {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag != OccupationTag());
	}
	return false;
}

void Helper::refreshInfo(not_null<UserData*> user) {
	_api.request(MTPhelp_GetUserInfo(
		user->inputUser
	)).done([=](const MTPhelp_UserInfo &result) {
		applyInfo(user, result);
		if (const auto controller = _userInfoEditPending.take(user)) {
			if (const auto strong = controller->get()) {
				showEditInfoBox(strong, user);
			}
		}
	}).send();
}

void Helper::applyInfo(
		not_null<UserData*> user,
		const MTPhelp_UserInfo &result) {
	const auto notify = [&] {
		user->session().changes().peerUpdated(
			user,
			Data::PeerUpdate::Flag::SupportInfo);
	};
	const auto remove = [&] {
		if (_userInformation.take(user)) {
			notify();
		}
	};
	result.match([&](const MTPDhelp_userInfo &data) {
		auto info = UserInfo();
		info.author = qs(data.vauthor());
		info.date = data.vdate().v;
		info.text = TextWithEntities{
			qs(data.vmessage()),
			Api::EntitiesFromMTP(&user->session(), data.ventities().v) };
		if (info.text.empty()) {
			remove();
		} else if (_userInformation[user] != info) {
			_userInformation[user] = info;
			notify();
		}
	}, [&](const MTPDhelp_userInfoEmpty &) {
		remove();
	});
}

rpl::producer<UserInfo> Helper::infoValue(not_null<UserData*> user) const {
	return user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::SupportInfo
	) | rpl::map([=] {
		return infoCurrent(user);
	});
}

rpl::producer<QString> Helper::infoLabelValue(
		not_null<UserData*> user) const {
	return infoValue(
		user
	) | rpl::map([](const Support::UserInfo &info) {
		const auto time = Ui::FormatDateTime(
			base::unixtime::parse(info.date),
			cTimeFormat());
		return info.author + ", " + time;
	});
}

rpl::producer<TextWithEntities> Helper::infoTextValue(
		not_null<UserData*> user) const {
	return infoValue(
		user
	) | rpl::map([](const Support::UserInfo &info) {
		return info.text;
	});
}

UserInfo Helper::infoCurrent(not_null<UserData*> user) const {
	const auto i = _userInformation.find(user);
	return (i != end(_userInformation)) ? i->second : UserInfo();
}

void Helper::editInfo(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	if (!_userInfoEditPending.contains(user)) {
		_userInfoEditPending.emplace(user, controller.get());
		refreshInfo(user);
	}
}

void Helper::showEditInfoBox(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	const auto info = infoCurrent(user);
	const auto editData = TextWithTags{
		info.text.text,
		TextUtilities::ConvertEntitiesToTextTags(info.text.entities)
	};

	const auto save = [=](TextWithTags result, Fn<void(bool)> done) {
		saveInfo(user, TextWithEntities{
			result.text,
			TextUtilities::ConvertTextTagsToEntities(result.tags)
		}, done);
	};
	Ui::show(
		Box<EditInfoBox>(controller, editData, save),
		Ui::LayerOption::KeepOther);
}

void Helper::saveInfo(
		not_null<UserData*> user,
		TextWithEntities text,
		Fn<void(bool success)> done) {
	const auto i = _userInfoSaving.find(user);
	if (i != end(_userInfoSaving)) {
		if (i->second.data == text) {
			return;
		} else {
			i->second.data = text;
			_api.request(base::take(i->second.requestId)).cancel();
		}
	} else {
		_userInfoSaving.emplace(user, SavingInfo{ text });
	}

	TextUtilities::PrepareForSending(
		text,
		Ui::ItemTextDefaultOptions().flags);
	TextUtilities::Trim(text);

	const auto entities = Api::EntitiesToMTP(
		&user->session(),
		text.entities,
		Api::ConvertOption::SkipLocal);
	_userInfoSaving[user].requestId = _api.request(MTPhelp_EditUserInfo(
		user->inputUser,
		MTP_string(text.text),
		entities
	)).done([=](const MTPhelp_UserInfo &result) {
		applyInfo(user, result);
		done(true);
	}).fail([=](const MTP::Error &error) {
		done(false);
	}).send();
}

Templates &Helper::templates() {
	return _templates;
}

QString ChatOccupiedString(not_null<History*> history) {
	const auto hand = QString::fromUtf8("\xe2\x9c\x8b\xef\xb8\x8f");
	const auto name = ParseOccupationName(history);
	return (name.isEmpty() || name.startsWith(qstr("[rand^")))
		? hand + " chat taken"
		: hand + ' ' + name + " is here";
}

QString InterpretSendPath(
		not_null<Window::SessionController*> window,
		const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return "App Error: Could not open interpret file: " + path;
	}
	const auto content = QString::fromUtf8(f.readAll());
	f.close();
	const auto lines = content.split('\n');
	auto toId = PeerId(0);
	auto filePath = QString();
	auto caption = QString();
	for (const auto &line : lines) {
		if (line.startsWith(qstr("from: "))) {
			if (window->session().userId() != line.midRef(qstr("from: ").size()).toInt()) {
				return "App Error: Wrong current user.";
			}
		} else if (line.startsWith(qstr("channel: "))) {
			const auto channelId = line.midRef(qstr("channel: ").size()).toInt();
			toId = peerFromChannel(channelId);
		} else if (line.startsWith(qstr("file: "))) {
			const auto path = line.mid(qstr("file: ").size());
			if (!QFile(path).exists()) {
				return "App Error: Could not find file with path: " + path;
			}
			filePath = path;
		} else if (line.startsWith(qstr("caption: "))) {
			caption = line.mid(qstr("caption: ").size());
		} else if (!caption.isEmpty()) {
			caption += '\n' + line;
		} else {
			return "App Error: Invalid command: " + line;
		}
	}
	const auto history = window->session().data().historyLoaded(toId);
	if (!history) {
		return "App Error: Could not find channel with id: " + QString::number(peerToChannel(toId));
	}
	Ui::showPeerHistory(history, ShowAtUnreadMsgId);
	history->session().api().sendFiles(
		Storage::PrepareMediaList(QStringList(filePath), st::sendMediaPreviewSize),
		SendMediaType::File,
		{ caption },
		nullptr,
		Api::SendAction(history));
	return QString();
}

} // namespace Support
