/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"

class StorageImageLocation;
class WebFileLocation;
class mtpFileLoader final
	: public FileLoader
	, public RPCSender
	, public Storage::Downloader {
public:
	mtpFileLoader(
		const StorageFileLocation &location,
		Data::FileOrigin origin,
		LocationType type,
		const QString &toFile,
		int32 size,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		const WebFileLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		const GeoPointLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	Data::FileOrigin fileOrigin() const override;

	uint64 objId() const override;

	void stop() override {
		rpcInvalidate();
	}
	void refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current);

	~mtpFileLoader();

private:
	struct RequestData {
		int offset = 0;
		int dcIndex = 0;

		inline bool operator<(const RequestData &other) const {
			return offset < other.offset;
		}
	};
	struct CdnFileHash {
		CdnFileHash(int limit, QByteArray hash) : limit(limit), hash(hash) {
		}
		int limit = 0;
		QByteArray hash;
	};
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	void startLoading() override;
	void cancelRequests() override;

	void makeRequest(const RequestData &requestData);

	MTP::DcId dcId() const override;
	bool readyToRequest() const override;
	void loadPart(int dcIndex) override;
	void normalPartLoaded(const MTPupload_File &result, mtpRequestId requestId);
	void webPartLoaded(const MTPupload_WebFile &result, mtpRequestId requestId);
	void cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId);
	void reuploadDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);

	void partLoaded(int offset, bytes::const_span buffer);
	bool feedPart(int offset, bytes::const_span buffer);

	bool partFailed(const RPCError &error, mtpRequestId requestId);
	bool normalPartFailed(QByteArray fileReference, const RPCError &error, mtpRequestId requestId);
	bool cdnPartFailed(const RPCError &error, mtpRequestId requestId);

	mtpRequestId sendRequest(const RequestData &requestData);
	void placeSentRequest(
		mtpRequestId requestId,
		const RequestData &requestData);
	[[nodiscard]] RequestData finishSentRequest(mtpRequestId requestId);
	void switchToCDN(
		const RequestData &requestData,
		const MTPDupload_fileCdnRedirect &redirect);
	void addCdnHashes(const QVector<MTPFileHash> &hashes);
	void changeCDNParams(
		const RequestData &requestData,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes);

	enum class CheckCdnHashResult {
		NoHash,
		Invalid,
		Good,
	};
	CheckCdnHashResult checkCdnFileHash(int offset, bytes::const_span buffer);

	const not_null<Storage::DownloadManager*> _downloader;
	const MTP::DcId _dcId = 0;
	std::map<mtpRequestId, RequestData> _sentRequests;

	bool _lastComplete = false;
	int32 _nextRequestOffset = 0;

	base::variant<
		StorageFileLocation,
		WebFileLocation,
		GeoPointLocation> _location;
	Data::FileOrigin _origin;

	MTP::DcId _cdnDcId = 0;
	QByteArray _cdnToken;
	QByteArray _cdnEncryptionKey;
	QByteArray _cdnEncryptionIV;
	base::flat_map<int, CdnFileHash> _cdnFileHashes;
	base::flat_map<RequestData, QByteArray> _cdnUncheckedParts;
	mtpRequestId _cdnHashesRequestId = 0;

};
