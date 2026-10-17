/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "wallet/wallet_session.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class TableLayout;
} // namespace Ui

namespace Wallet {

struct SendCallbacks;

struct WindowSendingRow {
	std::string operationId;
	std::optional<TransferItem> sending;
	std::string predecessor;
	QString servedId;
};

[[nodiscard]] bool ShowsGiftMove(const TransferItem &item);
[[nodiscard]] QString CollectibleRowKindText(const TransferItem &item);

void AddGiftMoveDetailsRows(
	not_null<Ui::TableLayout*> table,
	const TransferItem &item);

void SetWindowSendOperation(
	not_null<Session*> wallet,
	std::string operationId);
[[nodiscard]] const GiftMovePreviewData *CurrentGiftMovePreview(
	not_null<Session*> wallet);
[[nodiscard]] const std::vector<TransferItem> &ListedHistory(
	not_null<Session*> wallet);
[[nodiscard]] WindowSendingRow WindowSendingRowFor(
	not_null<Session*> wallet,
	std::vector<ListedSubmittedTransfer> &listed);

class GiftMovePreview final : public base::has_weak_ptr {
public:
	explicit GiftMovePreview(not_null<Main::Session*> session);

	[[nodiscard]] Fn<SendCallbacks(SendCallbacks)> attempts();

	void show(const TransferWalletIdentity &identity, const SendArgs &args);
	void stop();
	void close();

private:
	struct Work;

	[[nodiscard]] Session *resolveWallet() const;
	[[nodiscard]] std::shared_ptr<Work> fresh(
		std::weak_ptr<bool> guard,
		const TransferWalletIdentity &identity);
	[[nodiscard]] SendCallbacks submitting(SendCallbacks callbacks);
	[[nodiscard]] uint64 resume(const Work &work);

	const base::weak_ptr<Main::Session> _session;
	std::shared_ptr<Work> _lease;
	uint64 _token = 0;
	uint64 _generation = 0;
	bool _closed = false;

};

} // namespace Wallet
