#pragma once
#include "pspecific.h"

#ifdef Q_OS_WIN
#include <QtNetwork\qtcpserver.h>
#else
#include <qtcpserver.h>
#endif

#include "localstorage.h"

class ITSBitrix24 : public QObject {
	Q_OBJECT

public:
	static ITSBitrix24& Instance()
	{
		static ITSBitrix24 s;
		return s;
	}

	void registerBitrix24Portal(QString portalUrl, QString clientId, QString clientSecret);
	void loadAdminsListFromBitrix24Portal();
	void createTask(QString title, QString description, int32 groupId, bool openCreatedTaskInBrowser);
	void createTask(QString title, QString description, bool openCreatedTaskInBrowser);
	void setPortalCallbackPort(int32 bitrix24CallbackPort);
	void setDefaultGroupId(int32 defaultGroupId);
	bool checkBitrix24RegData();

	QString saveConfigToLocalStorage();
	QString loadConfigFromLocalStorage();
	
	QString getPortalUrl() { return _portalUrl; }
	QString getClientId() { return _clientId; }
	QString getClientSecret() { return _clientSecret; }
	int32 getPortalCallbackPort() { return _callbackPort; }
	int32 getDefaultGroupId() { return _defaultGroupId; }
	int32 getCurrentUserId() { return _currentUserId; }

	//QString restSyncRequest(QString type, QUrl restUrl, );

signals:

	void registerBitrix24PortalFinished(bool success, QString errorDescription = "");
	void createTaskFinished(bool success, QString errorDescription = "");
	void accessTokenRefreshed();
	void adminsListFromBitrix24PortalLoaded(bool success, QStringList adminsList);

public slots:	

private slots :

	void onServerConnection();
	void onClientReadyRead();
	void onGetAccessTokenFinished();
	void onCreateTaskFinished();
	void onAdminsListLoaded();
	void onGetCurrentUserInfoFinished();

private:

	ITSBitrix24();
	~ITSBitrix24();

	ITSBitrix24(ITSBitrix24 const&) = delete;
	ITSBitrix24& operator= (ITSBitrix24 const&) = delete;

	void getAccessToken(QString code);
	void refreshAccessToken();
	void getCurrentUserInfo();

	QTcpServer _tcpServer;
	QNetworkAccessManager _networkAccessManager;
	QMap<int, QTcpSocket *> _clients;
	int32 _callbackPort, _defaultGroupId, _currentUserId;

	QString _portalUrl, _clientId, _clientSecret, _prevAutorizeCode, _accessToken, _refreshToken;
	QDateTime _accessTokenReceived;
};
