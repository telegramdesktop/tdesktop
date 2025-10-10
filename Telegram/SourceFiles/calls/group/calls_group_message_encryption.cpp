/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_message_encryption.h"

#include <QtCore/QJsonValue>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Calls::Group {
namespace {

//[[nodiscard]] MTPJSONValue String(const QByteArray &value) {
//	return MTP_jsonString(MTP_bytes(value));
//}
//
//[[nodiscard]] MTPJSONValue Int(int value) {
//	return MTP_jsonNumber(MTP_double(value));
//}
//
//[[nodiscard]] MTPJSONObjectValue Value(
//		const QByteArray &name,
//		const MTPJSONValue &value) {
//	return MTP_jsonObjectValue(MTP_bytes(name), value);
//}
//
//[[nodiscard]] MTPJSONValue Object(
//		const QByteArray &cons,
//		QVector<MTPJSONObjectValue> &&values) {
//	values.insert(values.begin(), Value("_", String(cons)));
//	return MTP_jsonObject(MTP_vector<MTPJSONObjectValue>(std::move(values)));
//}
//
//[[nodiscard]] MTPJSONValue Array(QVector<MTPJSONValue> &&values) {
//	return MTP_jsonArray(MTP_vector<MTPJSONValue>(std::move(values)));
//}
//
//template <typename MTPD>
//[[nodiscard]] MTPJSONValue SimpleEntity(
//		const QByteArray &name,
//		const MTPD &data) {
//	return Object(name, {
//		Value("offset", Int(data.voffset().v)),
//		Value("length", Int(data.vlength().v)),
//	});
//}
//
//[[nodiscard]] MTPJSONValue Entity(const MTPMessageEntity &entity) {
//	return entity.match([](const MTPDmessageEntityBold &data) {
//		return SimpleEntity("messageEntityBold", data);
//	}, [](const MTPDmessageEntityItalic &data) {
//		return SimpleEntity("messageEntityItalic", data);
//	}, [](const MTPDmessageEntityUnderline &data) {
//		return SimpleEntity("messageEntityUnderline", data);
//	}, [](const MTPDmessageEntityStrike &data) {
//		return SimpleEntity("messageEntityStrike", data);
//	}, [](const MTPDmessageEntitySpoiler &data) {
//		return SimpleEntity("messageEntitySpoiler", data);
//	}, [](const MTPDmessageEntityCustomEmoji &data) {
//		return Object("messageEntityCustomEmoji", {
//			Value("offset", Int(data.voffset().v)),
//			Value("length", Int(data.vlength().v)),
//			Value(
//				"document_id",
//				String(QByteArray::number(int64(data.vdocument_id().v)))),
//		});
//	}, [](const auto &data) {
//		return MTP_jsonNull();
//	});
//}
//
//[[nodiscard]] QVector<MTPJSONValue> Entities(
//		const QVector<MTPMessageEntity> &list) {
//	auto result = QVector<MTPJSONValue>();
//	result.reserve(list.size());
//	for (const auto &entity : list) {
//		if (const auto e = Entity(entity); e.type() != mtpc_jsonNull) {
//			result.push_back(e);
//		}
//	}
//	return result;
//}
//
//[[nodiscard]] QByteArray Serialize(const MTPJSONValue &value) {
//	auto counter = ::tl::details::LengthCounter();
//	value.write(counter);
//	auto buffer = mtpBuffer();
//	buffer.reserve(counter.length);
//	value.write(buffer);
//	return QByteArray(
//		reinterpret_cast<const char*>(buffer.constData()),
//		buffer.size() * sizeof(buffer.front()));
//}

[[nodiscard]] QJsonValue String(const QByteArray &value) {
	return QJsonValue(QString::fromUtf8(value));
}

[[nodiscard]] QJsonValue Int(int value) {
	return QJsonValue(double(value));
}

struct JsonObjectValue {
	const char *name = nullptr;
	QJsonValue value;
};

[[nodiscard]] JsonObjectValue Value(
		const char *name,
		const QJsonValue &value) {
	return JsonObjectValue{ name, value };
}

[[nodiscard]] QJsonValue Object(
		const char *cons,
		QVector<JsonObjectValue> &&values) {
	auto result = QJsonObject();
	result.insert("_", cons);
	for (const auto &value : values) {
		result.insert(value.name, value.value);
	}
	return result;
}

[[nodiscard]] QJsonValue Array(QVector<QJsonValue> &&values) {
	auto result = QJsonArray();
	for (const auto &value : values) {
		result.push_back(value);
	}
	return result;
}

template <typename MTPD>
[[nodiscard]] QJsonValue SimpleEntity(
		const char *name,
		const MTPD &data) {
	return Object(name, {
		Value("offset", Int(data.voffset().v)),
		Value("length", Int(data.vlength().v)),
	});
}

[[nodiscard]] QJsonValue Entity(const MTPMessageEntity &entity) {
	return entity.match([](const MTPDmessageEntityBold &data) {
		return SimpleEntity("messageEntityBold", data);
	}, [](const MTPDmessageEntityItalic &data) {
		return SimpleEntity("messageEntityItalic", data);
	}, [](const MTPDmessageEntityUnderline &data) {
		return SimpleEntity("messageEntityUnderline", data);
	}, [](const MTPDmessageEntityStrike &data) {
		return SimpleEntity("messageEntityStrike", data);
	}, [](const MTPDmessageEntitySpoiler &data) {
		return SimpleEntity("messageEntitySpoiler", data);
	}, [](const MTPDmessageEntityCustomEmoji &data) {
		return Object("messageEntityCustomEmoji", {
			Value("offset", Int(data.voffset().v)),
			Value("length", Int(data.vlength().v)),
			Value(
				"document_id",
				String(QByteArray::number(int64(data.vdocument_id().v)))),
		});
	}, [](const auto &data) {
		return QJsonValue(QJsonValue::Null);
	});
}

[[nodiscard]] QVector<QJsonValue> Entities(
		const QVector<MTPMessageEntity> &list) {
	auto result = QVector<QJsonValue>();
	result.reserve(list.size());
	for (const auto &entity : list) {
		if (const auto e = Entity(entity); !e.isNull()) {
			result.push_back(e);
		}
	}
	return result;
}

[[nodiscard]] QByteArray Serialize(const QJsonValue &value) {
	return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

[[nodiscard]] std::optional<QJsonValue> GetValue(
		const QJsonObject &object,
		const char *name) {
	const auto i = object.find(name);
	return (i != object.end()) ? *i : std::optional<QJsonValue>();
}

[[nodiscard]] std::optional<int> GetInt(
		const QJsonObject &object,
		const char *name) {
	if (const auto maybeValue = GetValue(object, name)) {
		if (maybeValue->isDouble()) {
			return int(base::SafeRound(maybeValue->toDouble()));
		} else if (maybeValue->isString()) {
			auto ok = false;
			const auto result = maybeValue->toString().toInt(&ok);
			return ok ? result : std::optional<int>();
		}
	}
	return {};
}

[[nodiscard]] std::optional<uint64> GetLong(
		const QJsonObject &object,
		const char *name) {
	if (const auto maybeValue = GetValue(object, name)) {
		if (maybeValue->isDouble()) {
			const auto value = maybeValue->toDouble();
			return (value >= 0.)
				? uint64(base::SafeRound(value))
				: std::optional<uint64>();
		} else if (maybeValue->isString()) {
			auto ok = false;
			const auto result = maybeValue->toString().toLongLong(&ok);
			return ok ? uint64(result) : std::optional<uint64>();
		}
	}
	return {};
}


[[nodiscard]] std::optional<QString> GetString(
		const QJsonObject &object,
		const char *name) {
	const auto maybeValue = GetValue(object, name);
	return (maybeValue && maybeValue->isString())
		? maybeValue->toString()
		: std::optional<QString>();
}


[[nodiscard]] std::optional<QString> GetCons(const QJsonObject &object) {
	return GetString(object, "_");
}

[[nodiscard]] bool Unsupported(
		const QJsonObject &object,
		const QString &cons = QString()) {
	const auto maybeMinLayer = GetInt(object, "_min_layer");
	const auto layer = int(MTP::details::kCurrentLayer);
	if (maybeMinLayer.value_or(layer) > layer) {
		LOG(("E2E Error: _min_layer too large: %1 > %2").arg(*maybeMinLayer).arg(layer));
		return true;
	} else if (!cons.isEmpty() && GetCons(object) != cons) {
		LOG(("E2E Error: Expected %1 here.").arg(cons));
		return true;
	}
	return false;
}

[[nodiscard]] std::optional<MTPMessageEntity> GetEntity(
		const QString &text,
		const QJsonObject &object) {
	const auto cons = GetCons(object).value_or(QString());
	const auto offset = GetInt(object, "offset").value_or(-1);
	const auto length = GetInt(object, "length").value_or(0);
	if (Unsupported(object)
		|| (offset < 0)
		|| (length <= 0)
		|| (offset >= text.size())
		|| (length > text.size())
		|| (offset + length > text.size())) {
		return {};
	}
	const auto simple = [&](const auto &make) {
		return make(MTP_int(offset), MTP_int(length));
	};
	if (cons == "messageEntityBold") {
		return simple(MTP_messageEntityBold);
	} else if (cons == "messageEntityItalic") {
		return simple(MTP_messageEntityItalic);
	} else if (cons == "messageEntityUnderline") {
		return simple(MTP_messageEntityUnderline);
	} else if (cons == "messageEntityStrike") {
		return simple(MTP_messageEntityStrike);
	} else if (cons == "messageEntitySpoiler") {
		return simple(MTP_messageEntitySpoiler);
	} else if (cons == "messageEntityCustomEmoji") {
		const auto maybeDocumentId = GetLong(object, "document_id");
		if (const auto documentId = maybeDocumentId.value_or(0)) {
			return MTP_messageEntityCustomEmoji(
				MTP_int(offset),
				MTP_int(length),
				MTP_long(documentId));
		}
	}
	return {};
}

[[nodiscard]] QVector<MTPMessageEntity> GetEntities(
		const QString &text,
		const QJsonArray &list) {
	auto result = QVector<MTPMessageEntity>();
	result.reserve(list.size());
	for (const auto &entry : list) {
		if (const auto entity = GetEntity(text, entry.toObject())) {
			result.push_back(*entity);
		}
	}
	return result;
}

} // namespace

QByteArray SerializeMessage(const PreparedMessage &data) {
	return Serialize(Object("groupCallMessage", {
		Value(
			"random_id",
			String(QByteArray::number(int64(data.randomId)))),
		Value(
			"message",
			Object("textWithEntities", {
				Value("text", String(data.message.data().vtext().v)),
				Value(
					"entities",
					Array(Entities(data.message.data().ventities().v))),
			})),
	}));
}

std::optional<PreparedMessage> DeserializeMessage(
		const QByteArray &data) {
	auto error = QJsonParseError();
	auto document = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError
		|| !document.isObject()) {
		LOG(("E2E Error: Bad json in Calls::Group::DeserializeMessage."));
		return {};
	}
	const auto groupCallMessage = document.object();
	if (Unsupported(groupCallMessage, "groupCallMessage")) {
		return {};
	}
	const auto randomId = GetLong(groupCallMessage, "random_id").value_or(0);
	if (!randomId) {
		return {};
	}
	const auto message = groupCallMessage["message"].toObject();
	if (Unsupported(message, "textWithEntities")) {
		return {};
	}
	const auto maybeText = GetString(message, "text");
	if (!maybeText) {
		return {};
	}
	const auto &text = *maybeText;
	const auto maybeEntities = GetValue(message, "entities");
	if (!maybeEntities || !maybeEntities->isArray()) {
		return {};
	}
	const auto entities = GetEntities(text, maybeEntities->toArray());
	return PreparedMessage{
		.randomId = randomId,
		.message = MTP_textWithEntities(
			MTP_string(text),
			MTP_vector<MTPMessageEntity>(entities)),
	};
}

} // namespace Calls::Group
