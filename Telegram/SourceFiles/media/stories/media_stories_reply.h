/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "history/history_item_helpers.h"

class History;
enum class SendMediaType;

namespace Api {
struct MessageToSend;
struct SendAction;
struct SendOptions;
} // namespace Api

namespace Data {
struct ReactionId;
} // namespace Data

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace HistoryView::Controls {
struct VoiceToSend;
} // namespace HistoryView::Controls

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Main {
class Session;
} // namespace Main

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Ui {
struct PreparedList;
struct PreparedBundle;
class SendFilesWay;
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Controller;

struct ReplyAreaData {
	PeerData *peer = nullptr;
	StoryId id = 0;

	friend inline auto operator<=>(ReplyAreaData, ReplyAreaData) = default;
	friend inline bool operator==(ReplyAreaData, ReplyAreaData) = default;
};

class ReplyArea final : public base::has_weak_ptr {
public:
	explicit ReplyArea(not_null<Controller*> controller);
	~ReplyArea();

	void show(
		ReplyAreaData data,
		rpl::producer<Data::ReactionId> likedValue);
	bool sendReaction(const Data::ReactionId &id);

	[[nodiscard]] bool focused() const;
	[[nodiscard]] rpl::producer<bool> focusedValue() const;
	[[nodiscard]] rpl::producer<bool> activeValue() const;
	[[nodiscard]] rpl::producer<bool> hasSendTextValue() const;

	[[nodiscard]] bool ignoreWindowMove(QPoint position) const;
	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	[[nodiscard]] not_null<Ui::RpWidget*> likeAnimationTarget() const;

private:
	class Cant;

	using VoiceToSend = HistoryView::Controls::VoiceToSend;

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<History*> history() const;

	bool send(
		Api::MessageToSend message,
		bool skipToast = false);

	[[nodiscard]] bool checkSendPayment(
		int messagesCount,
		Api::SendOptions options,
		Fn<void(int)> withPaymentApproved);

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Ui::PreparedList &list) const;
	bool showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const;
	void sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter);
	void sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options);
	void finishSending(bool skipToast = false);

	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId);
	void sendExistingPhoto(not_null<PhotoData*> photo);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot);
	void sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId);

	void initGeometry();
	void initActions();

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void send(Api::SendOptions options);
	void sendVoice(const VoiceToSend &data);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);

	[[nodiscard]] Fn<SendMenu::Details()> sendMenuDetails() const;

	void showPremiumToast(not_null<DocumentData*> emoji);
	[[nodiscard]] bool showSlowmodeError();

	const not_null<Controller*> _controller;
	rpl::variable<bool> _isComment;
	rpl::variable<int> _starsForMessage;

	const std::unique_ptr<HistoryView::ComposeControls> _controls;
	std::unique_ptr<Cant> _cant;

	ReplyAreaData _data;
	base::has_weak_ptr _shownPeerGuard;
	bool _chooseAttachRequest = false;
	rpl::variable<bool> _choosingAttach;

	SendPaymentHelper _sendPayment;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
