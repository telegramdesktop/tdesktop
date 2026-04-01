// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/filters/filters_utils.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

#include <QByteArray>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <qjsondocument.h>
#include <QString>
#include <vector>
#include <QtNetwork/QHttpPart>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

constexpr auto BACKUP_VERSION = 2;

void FilterUtils::importFromLink(const QString &link) {
	if (link.isEmpty()) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailFetch(tr::now));
		return;
	}

	const auto request = QNetworkRequest(QUrl(link));
	_reply = _manager->get(request);

	connect(
		_reply,
		&QNetworkReply::finished,
		this,
		[=]
		{
			const auto responseData = _reply->readAll();

			const auto jsonString = QString::fromUtf8(responseData);

			if (jsonString.isNull()) {
				LOG(("FilterUtils: Invalid response."));
				Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));

				_reply->deleteLater();
				return;
			}

			if (!handleResponse(jsonString.toUtf8())) {
				LOG(("FilterUtils: Error handling response."));
			}
			_reply->deleteLater();
		});

	connect(
		_reply,
		&QNetworkReply::errorOccurred,
		this,
		[=](QNetworkReply::NetworkError e)
		{
			gotFailure(e);

			_reply->deleteLater();
		});
}

void FilterUtils::publishFilters() {
	const auto exported = exportFilters();

	auto multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

	QHttpPart contentPart;
	contentPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"content\""));
	contentPart.setBody(exported.toUtf8());

	QHttpPart syntaxPart;
	syntaxPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"syntax\""));
	syntaxPart.setBody("json");

	QHttpPart titlePart;
	titlePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"title\""));
	titlePart.setBody("TeleForge Filters");

	multiPart->append(contentPart);
	multiPart->append(syntaxPart);
	multiPart->append(titlePart);

	QNetworkRequest request(QUrl("https://dpaste.com/api/v2/"));

	_reply = _manager->post(request, multiPart);
	multiPart->setParent(_reply);

	connect(
		_reply,
		&QNetworkReply::finished,
		this,
		[=]
		{
			const auto error = _reply->error();
			const auto location = _reply->header(QNetworkRequest::LocationHeader);

			if (error == QNetworkReply::NoError && location.isValid()) {
				auto url = location.toString();
				url.append(".txt");
				QGuiApplication::clipboard()->setText(url);

				Ui::Toast::Show(tr::lng_stickers_copied(tr::now));
			} else {
				LOG(("Failed to publish filters to dpaste, error: %1").arg(_reply->errorString()));

				Ui::Toast::Show(tr::ayu_FiltersToastFailPublish(tr::now));
			}
			_reply->deleteLater();
		});
}

bool FilterUtils::importFromJson(const QByteArray &json) {
	auto error = QJsonParseError{0, QJsonParseError::NoError};
	const auto document = QJsonDocument::fromJson(json, &error);

	if (error.error != QJsonParseError::NoError) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
		LOG(("FilterUtils: Failed to parse JSON, error: %1"
		).arg(error.errorString()));
		return false;
	}
	if (!document.isObject()) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
		LOG(("FilterUtils: not an object received in JSON"));
		return false;
	}
	const auto changes = prepareChanges(document.object());

	if (changes == ApplyChanges{}) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailNoChanges(tr::now));
		LOG(("FilterUtils: received empty changes"));
		return false;
	}

	const auto any = !changes.newFilters.empty() || !changes.removeFiltersById.empty() || !changes.filtersOverrides.
		empty() || !changes.newExclusions.empty() || !changes.removeExclusions.empty() || !changes.peersToBeResolved.
		empty();

	if (!any) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailNoChanges(tr::now));
		return false;
	}

	applyChanges(changes);

	return true;
}

struct BackupExclusion
{
	ID dialogId;
	std::vector<char> filterId;

	QJsonObject toJson() const {
		QJsonObject json;
		json["dialogId"] = dialogId;

		// make it look like java's UUID
		auto hexId = QString(QByteArray(filterId.data(), filterId.size()).toHex());
		if (hexId.length() == 32) {
			hexId.insert(8, '-');
			hexId.insert(13, '-');
			hexId.insert(18, '-');
			hexId.insert(23, '-');
		}
		json["filterId"] = hexId;
		return json;
	}
};

QString FilterUtils::exportFilters() {
	auto createJsonArray = [&](const auto &container)
	{
		QJsonArray jsonArray;
		for (const auto &item : container) {
			jsonArray.append(item.toJson());
		}
		return jsonArray;
	};

	QJsonArray filtersArray;
	QJsonObject jsonObject;
	jsonObject["version"] = BACKUP_VERSION;
	const auto filters = AyuDatabase::getAllRegexFilters();

	for (const auto &item : filters) {
		QJsonObject filterJson;
		filterJson["caseInsensitive"] = item.caseInsensitive;
		if (item.dialogId.has_value()) {
			filterJson["dialogId"] = item.dialogId.value();
		} else {
			filterJson["dialogId"] = QJsonValue();
		}
		filterJson["enabled"] = item.enabled;
		filterJson["reversed"] = item.reversed;
		filterJson["text"] = QString::fromStdString(item.text);

		// make it look like java's UUID
		auto hexId = QString(QByteArray(item.id.data(), item.id.size()).toHex());
		if (hexId.length() == 32) {
			hexId.insert(8, '-');
			hexId.insert(13, '-');
			hexId.insert(18, '-');
			hexId.insert(23, '-');
		}
		filterJson["id"] = hexId;
		filtersArray.append(filterJson);
	}
	jsonObject["filters"] = filtersArray;

	const auto excl = AyuDatabase::getAllFiltersExclusions();


	std::vector<BackupExclusion> exclusions;
	exclusions.reserve(excl.size());

	for (const auto &item : excl) {
		exclusions.push_back(BackupExclusion{item.dialogId, item.filterId});
	}

	jsonObject["exclusions"] = createJsonArray(exclusions);
	jsonObject["removeFiltersById"] = QJsonValue();
	jsonObject["removeExclusions"] = QJsonValue();

	QJsonObject peers;
	for (const auto &item : filters) {
		const auto &session = currentSession();
		if (!item.dialogId.has_value()) {
			continue;
		}
		if (const auto peer = session->data().peer(peerFromChat(abs(item.dialogId.value())))) {
			if (!peer->username().isEmpty()) {
				QString key = QString::number(item.dialogId.value());
				peers[key] = peer->username();
			}
		}
	}
	jsonObject["peers"] = peers;


	QJsonDocument jsonDoc(jsonObject);
	QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Indented);
	return QString::fromUtf8(jsonData);
}

// for compatibility with Android version

int typeOfMessage(const HistoryItem *item) {
	if (item->isSponsored()) {
		return 0; // TYPE_TEXT
	}

	if (!item->isService()) {
		if (const auto media = item->media()) {
			if (const auto invoice = media->invoice(); invoice && invoice->isPaidMedia) {
				return 29; // TYPE_PAID_MEDIA
			}
			if (media->giveawayStart()) {
				return 26; // TYPE_GIVEAWAY
			}
			if (media->giveawayResults()) {
				return 28; // TYPE_GIVEAWAY_RESULTS
			}
			if (dynamic_cast<Data::MediaDice*>(media)) {
				return 15; // TYPE_ANIMATED_STICKER
			}
			if (media->photo()) {
				return 1; // TYPE_PHOTO
			}
			if (media->location()) {
				return 4; // TYPE_GEO
			}
			if (media->sharedContact()) {
				return 12; // TYPE_CONTACT
			}
			if (media->poll() || media->todolist()) {
				return 17; // TYPE_POLL
			}
			if (media->storyId().valid()) {
				if (media->storyMention()) {
					return 24; // TYPE_STORY_MENTION
				}
				return 23; // TYPE_STORY
			}
			if (const auto document = media->document()) {
				if (document->round()) {
					return 5; // TYPE_ROUND_VIDEO
				}
				if (document->isVideoFile()) {
					return 3; // TYPE_VIDEO
				}
				if (document->isVoiceMessage()) {
					return 2; // TYPE_VOICE
				}
				if (document->isAudioFile()) {
					return 14; // TYPE_MUSIC
				}
				if (document->isAnimation()) {
					return 8; // TYPE_GIF
				}
				if (document->sticker()) {
					if (document->isAnimation()) {
						return 15; // TYPE_ANIMATED_STICKER
					}
					return 13; // TYPE_STICKER
				}
				return 9; // TYPE_FILE
			}
			if (media->game() || media->invoice() || media->webpage()) {
				return 0; // TYPE_TEXT
			}
		} else {
			if (item->isOnlyEmojiAndSpaces()) {
				return 19; // TYPE_EMOJIS
			}
			return 0; // TYPE_TEXT
		}
	} else {
		if (const auto media = item->media()) {
			if (media->call()) {
				return 16; // TYPE_PHONE_CALL
			}
			if (media->photo() && !item->isUserpicSuggestion()) {
				return 11; // TYPE_ACTION_PHOTO
			}
			if (media->photo() && item->isUserpicSuggestion()) {
				return 21; // TYPE_SUGGEST_PHOTO
			}
			if (media->paper()) {
				return 22; // TYPE_ACTION_WALLPAPER
			}
			if (const auto gift = media->gift()) {
				if (gift->type == Data::GiftType::Premium) {
					if (gift->channel) {
						return 25; // TYPE_GIFT_PREMIUM_CHANNEL
					}
					return 18; // TYPE_GIFT_PREMIUM
				}
				if (gift->type == Data::GiftType::Credits
					|| gift->type == Data::GiftType::StarGift
					|| gift->type == Data::GiftType::Ton) {
					return 30; // TYPE_GIFT_STARS
				}
			}
			if (item->Get<HistoryServiceGiveawayResults>()) {
				return 28; // TYPE_GIVEAWAY_RESULTS
			}
		}
		return 10; // TYPE_DATE
	}
	return 0; // TYPE_TEXT
}

QString extractSingle(const not_null<HistoryItem*> item) {
	QString text(item->originalText().text);
	if (!item->originalText().entities.empty()) {
		for (const auto &entity : item->originalText().entities) {
			if (entity.type() == EntityType::Url || entity.type() == EntityType::CustomUrl) {
				text.append("\n");
				text.append(entity.data());
			}
		}
	}

	return text;
}

QString FilterUtils::extractAllText(const not_null<HistoryItem*> item, const Data::Group *group) {
	auto text = QString();
	if (group) {
		for (const auto &groupItem : group->items) {
			const auto res = extractSingle(groupItem).trimmed();
			if (!res.isEmpty()) {
				text.append(res).append("\n");
			}
		}
	} else {
		text = extractSingle(item).trimmed();
	}

	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		if (!markup->data.isNull()) {
			for (const auto &row : markup->data.rows) {
				for (const auto &button : row) {
					text.append("<button>").append(button.text).append(" ").append(qs(button.data)).append("</button>");
					text.append("\n");
				}
			}
		}
	}

	text.append("\n").append("<type>").append(QString::number(typeOfMessage(item))).append("</type>");

	return text;
}

bool FilterUtils::handleResponse(const QByteArray &response) {
	try {
		return importFromJson(response);
	} catch (...) {
		LOG(("FilterUtils: Failed to apply response"));
		return false;
	}
}

void FilterUtils::gotFailure(const QNetworkReply::NetworkError &error) {
	LOG(("FilterUtils: Error %1").arg(error));
	Ui::Toast::Show(tr::ayu_FiltersToastFailFetch(tr::now));
}

ApplyChanges FilterUtils::prepareChanges(const QJsonObject &root) {
	const auto version = root.value("version");

	if (version.toInt() > BACKUP_VERSION) {
		return {};
	}


	const auto existingFilters = AyuDatabase::getAllRegexFilters();
	const auto existingExclusions = AyuDatabase::getAllFiltersExclusions();

	std::vector<RegexFilter> filtersOverrides;
	std::map<std::vector<char>, RegexFilter> newFilters;
	std::vector<RegexFilterGlobalExclusion> newExclusions;
	std::vector<QString> removeFiltersById;
	std::vector<RegexFilterGlobalExclusion> removeExclusions;
	std::map<long long, QString> peersToBeResolved;


	if (const auto &filters = root.value("filters").toArray(); !filters.isEmpty()) {
		for (const auto &filterRef : filters) {
			if (const auto filter = filterRef.toObject(); !filter.isEmpty()) {
				RegexFilter regex;
				regex.caseInsensitive = filter.value("caseInsensitive").toBool();

				const auto dialogIdValue = filter.value("dialogId");
				if (!dialogIdValue.isNull()) {
					regex.dialogId = filter.value("dialogId").toVariant().toLongLong();
				} else {
					regex.dialogId = std::nullopt;
				}
				regex.enabled = filter.value("enabled").toBool();

				auto idString = filter.value("id").toString();
				idString.remove('-');

				auto idBytes = QByteArray::fromHex(idString.toUtf8());
				regex.id = std::vector(idBytes.constData(), idBytes.constData() + idBytes.size());

				regex.reversed = filter.value("reversed").toBool();
				regex.text = filter.value("text").toString().toStdString();


				auto it = std::ranges::find_if(existingFilters,
											   [&regex](const RegexFilter &f)
											   {
												   return f.id == regex.id;
											   });
				if (it != existingFilters.end()) {
					const RegexFilter &existing = *it;
					if (existing != regex) {
						filtersOverrides.push_back(existing);
					}
				} else {
					newFilters[regex.id] = std::move(regex);
				}
			}
		}
	}

	if (const auto exclusions = root.value("exclusions").toArray(); !exclusions.isEmpty()) {
		for (const auto &exclusionRef : exclusions) {
			if (const auto exclusion = exclusionRef.toObject(); !exclusion.isEmpty()) {
				RegexFilterGlobalExclusion regex;

				regex.dialogId = exclusion.value("dialogId").toVariant().toLongLong();

				auto filterIdString = exclusion.value("filterId").toString();
				filterIdString.remove('-');

				auto filterIdBytes = QByteArray::fromHex(filterIdString.toUtf8());
				regex.filterId = std::vector(
					filterIdBytes.constData(),
					filterIdBytes.constData() + filterIdBytes.size()
				);

				auto it = std::ranges::find_if(
					existingExclusions,
					[&regex](const RegexFilterGlobalExclusion &f)
					{
						return f.dialogId == regex.dialogId && f.filterId == regex.filterId;
					});

				if (it == existingExclusions.end()) {
					newExclusions.push_back(std::move(regex));
				}
			}
		}
	}

	if (const auto removeFiltersByIdJson = root.value("removeFiltersById").toArray(); !removeFiltersByIdJson.
		isEmpty()) {
		for (const auto &filterRef : removeFiltersByIdJson) {
			const auto filter = filterRef.toString();

			const auto byteArray = filter.toUtf8();
			const auto filterId = std::vector(byteArray.constData(), byteArray.constData() + byteArray.size());

			const auto exists = std::ranges::any_of(
				existingFilters,
				[&](const RegexFilter &f)
				{
					return f.id == filterId;
				});
			if (exists) {
				removeFiltersById.push_back(filter);
			}
		}
	}

	if (const auto removeExclusionsJson = root.value("removeExclusions").toArray(); !removeExclusionsJson.isEmpty()) {
		for (const auto &exclusionRef : removeExclusionsJson) {
			const auto exclusionObj = exclusionRef.toObject();
			const auto filterIdStr = exclusionObj.value("filterId").toString();
			const qint64 dialogId = exclusionObj.value("dialogId").toVariant().toLongLong();

			const auto byteArray = filterIdStr.toUtf8();
			const auto filterIdVec = std::vector<char>(byteArray.constData(), byteArray.constData() + byteArray.size());

			const bool exists = std::ranges::any_of(
				existingExclusions,
				[&](const RegexFilterGlobalExclusion &x)
				{
					return x.filterId == filterIdVec && x.dialogId == dialogId;
				});

			if (exists) {
				RegexFilterGlobalExclusion regex;
				regex.dialogId = dialogId;
				regex.filterId = filterIdVec;
				removeExclusions.push_back(regex);
			}
		}
	}

	if (const auto peersJson = root.value("peers").toObject(); !peersJson.isEmpty()) {
		for (const auto &dialogIdStr : peersJson.keys()) {
			bool parsed;
			const auto dialogId = dialogIdStr.toLongLong(&parsed);
			if (!parsed) {
				continue;
			}

			// almost everytime fails
			PeerData *peerMaybe = nullptr;
			for (const auto &[index, account] : Core::App().domain().accounts()) {
				if (const auto session = account->maybeSession()) {
					if (const auto peer = session->data().peer(peerFromChat(abs(dialogId)))) {
						peerMaybe = peer;
						break;
					}
				}
			}

			if (!peerMaybe) {
				const auto username = peersJson.value(dialogIdStr).toString();
				peersToBeResolved[dialogId] = username;
			}
		}
	}

	ApplyChanges changes;
	for (auto &filter : newFilters) {
		changes.newFilters.push_back(std::move(filter.second));
	}
	changes.removeFiltersById = std::move(removeFiltersById);
	changes.filtersOverrides = std::move(filtersOverrides);
	changes.newExclusions = std::move(newExclusions);
	changes.removeExclusions = std::move(removeExclusions);
	changes.peersToBeResolved = std::move(peersToBeResolved);

	return changes;
}

void FilterUtils::applyChanges(const ApplyChanges &changes) {
	if (!changes.newFilters.empty()) {
		for (const auto &filter : changes.newFilters) {
			AyuDatabase::addRegexFilter(filter);
		}
	}

	if (!changes.removeFiltersById.empty()) {
		for (const auto &id : changes.removeFiltersById) {
			const auto byteArray = id.toUtf8();
			const auto filterId = std::vector<char>(byteArray.constData(), byteArray.constData() + byteArray.size());
			AyuDatabase::deleteExclusionsByFilterId(filterId);
		}
	}

	if (!changes.filtersOverrides.empty()) {
		for (const auto &filter : changes.filtersOverrides) {
			AyuDatabase::updateRegexFilter(filter);
		}
	}

	if (!changes.newExclusions.empty()) {
		for (const auto &exclusion : changes.newExclusions) {
			AyuDatabase::addRegexExclusion(exclusion);
		}
	}

	if (!changes.removeExclusions.empty()) {
		for (const auto &exclusion : changes.removeExclusions) {
			AyuDatabase::deleteExclusion(exclusion.dialogId, exclusion.filterId);
		}
	}

	if (!changes.peersToBeResolved.empty()) {
		resolveAllChats(changes.peersToBeResolved);
	}

	FiltersCacheController::rebuildCache();
	crl::on_main([]
	{
		FiltersCacheController::fireUpdate();
	});
}
