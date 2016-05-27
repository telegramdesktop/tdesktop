#include "stdafx.h"
#include "itsbitrix24.h"
#include <memory>

ITSBitrix24::ITSBitrix24() :
	_tcpServer(this),
	_callbackPort(0),
	_defaultGroupId(0){
	
	QObject::connect(&_tcpServer, SIGNAL(newConnection()), this, SLOT(onServerConnection()));
}

ITSBitrix24::~ITSBitrix24() {

	foreach(int i, _clients.keys()) {
		QTextStream os(_clients[i]);
		_clients[i]->close();
		_clients.remove(i);
	}
	_tcpServer.close();
}

void ITSBitrix24::setPortalCallbackPort(int32 bitrix24CallbackPort) {
	_callbackPort = bitrix24CallbackPort;	
}

void ITSBitrix24::setDefaultGroupId(int32 defaultGroupId) {
	_defaultGroupId = defaultGroupId;
}

void ITSBitrix24::onServerConnection(){

	QTcpSocket* clientSocket = _tcpServer.nextPendingConnection();
	int idusersocs = clientSocket->socketDescriptor();
	_clients[idusersocs] = clientSocket;
	connect(_clients[idusersocs], SIGNAL(readyRead()), this, SLOT(onClientReadyRead()));
}

void ITSBitrix24::onClientReadyRead() {

	QTcpSocket* clientSocket = (QTcpSocket*)sender();
	int idusersocs = clientSocket->socketDescriptor();
	QTextStream os(clientSocket);
	//os.setAutoDetectUnicode(true);
	os.setCodec("UTF-8");

    QFile callbackPageFile(":/pages/pages/callback.html");

    if (callbackPageFile.open(QIODevice::ReadOnly))
    {
       QString callbackPageString = QString(callbackPageFile.readAll()).arg(this->_portalUrl);
       os << callbackPageString;
    }

	QString request = clientSocket->readAll();
	int32 start = request.indexOf("GET /?");
	if (start != -1) {
		QString queryString = request.mid(start + 6);
		if (!queryString.isEmpty()) {
			QStringList queryStringSegments = queryString.split("&");			
			foreach (QString queryStringSegment, queryStringSegments)
			{
				QStringList param = queryStringSegment.split("=");
				if (param.count() == 2 && param[0] == "code") {
					QString _newAutorizeCode = param[1];
					if (_prevAutorizeCode.isEmpty()) {
						_prevAutorizeCode = _newAutorizeCode;
						getAccessToken(_prevAutorizeCode);
					}
					else if (_prevAutorizeCode != _newAutorizeCode) {
						_prevAutorizeCode = _newAutorizeCode;
						getAccessToken(_prevAutorizeCode);
					}					
				}
			}
		}		
	}	

	clientSocket->close();
	_clients.remove(idusersocs);
	//clientSocket->deleteLater();
}

void ITSBitrix24::registerBitrix24Portal(QString portalUrl, QString clientId, QString clientSecret) {

	_portalUrl = portalUrl; _clientId = clientId; _clientSecret = clientSecret;
	
	while (_portalUrl.endsWith("/")) {
		_portalUrl.remove(_portalUrl.length() - 1, 1);
		if (_portalUrl.length() == 0) break;
	}

	if (_callbackPort <= 0) {
		emit registerBitrix24PortalFinished(false, "Callback port not valid.");
		return;
	}

	if (_tcpServer.isListening()) _tcpServer.close();

	if (!_tcpServer.listen(QHostAddress::Any, _callbackPort)) {
		emit registerBitrix24PortalFinished(false, QObject::tr("Unable to start the server: %1.").arg(_tcpServer.errorString()));
		return;
	}
			
	QUrl autorizeUrl = QUrl::fromUserInput(QString(portalUrl += "/oauth/authorize/?client_id=%1&response_type=code").arg(clientId));
	if (autorizeUrl == QUrl()) {
		emit registerBitrix24PortalFinished(false, "Autorize url invalid");
		return;
	}	
	_prevAutorizeCode.clear();
	QDesktopServices::openUrl(autorizeUrl);
}

void ITSBitrix24::getAccessToken(QString code) {

	QString getAccessTokenUrl = QString("%1/oauth/token/?").arg(_portalUrl);
	getAccessTokenUrl = getAccessTokenUrl.append("grant_type=authorization_code&");
	getAccessTokenUrl = getAccessTokenUrl.append(QString("client_id=%1&").arg(_clientId));
	getAccessTokenUrl = getAccessTokenUrl.append(QString("client_secret=%1&").arg(_clientSecret));
	getAccessTokenUrl = getAccessTokenUrl.append(QString("scope=%1&").arg("task,crm,disk,user,entity,sonet_group,lists"));
	getAccessTokenUrl = getAccessTokenUrl.append("code=" + code);
	
	QNetworkRequest getAccessTokenRequest(getAccessTokenUrl);
	QNetworkReply *_getAccessTokenReply = _networkAccessManager.get(getAccessTokenRequest);

	_getAccessTokenReply->setProperty("tokenRefreshing", QVariant(false));
	
	connect(_getAccessTokenReply, SIGNAL(finished()), this, SLOT(onGetAccessTokenFinished()));
	connect(_getAccessTokenReply, SIGNAL(sslErrors(QList<QSslError>)), _getAccessTokenReply, SLOT(ignoreSslErrors()));

}

void ITSBitrix24::refreshAccessToken() {
	QString refreshTokenUrl = QString("%1/oauth/token/?").arg(_portalUrl);
	refreshTokenUrl = refreshTokenUrl.append("grant_type=refresh_token&");
	refreshTokenUrl = refreshTokenUrl.append(QString("client_id=%1&").arg(_clientId));
	refreshTokenUrl = refreshTokenUrl.append(QString("client_secret=%1&").arg(_clientSecret));
	refreshTokenUrl = refreshTokenUrl.append(QString("refresh_token=%1").arg(_refreshToken));

	QNetworkRequest refreshTokenRequest(refreshTokenUrl);
	QNetworkReply *refreshTokenReply = _networkAccessManager.get(refreshTokenRequest);

	refreshTokenReply->setProperty("tokenRefreshing", QVariant(true));

	connect(refreshTokenReply, SIGNAL(finished()), this, SLOT(onGetAccessTokenFinished()));
	connect(refreshTokenReply, SIGNAL(sslErrors(QList<QSslError>)), refreshTokenReply, SLOT(ignoreSslErrors()));
}

void ITSBitrix24::onGetAccessTokenFinished() {

	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (reply->error() == QNetworkReply::NoError) {
		QString strReply = reply->readAll();

		QJsonDocument jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
		QJsonObject tokenJsonObject = jsonResponse.object();

		if (!tokenJsonObject.contains("access_token")) {
			emit registerBitrix24PortalFinished(false, "\"access_token\" not found");			
			reply->deleteLater();
			return;
		}

		if (!tokenJsonObject.contains("refresh_token")) {
			emit registerBitrix24PortalFinished(false, "\"refresh_token\" not found");
			reply->deleteLater();
			return;
		}

		_accessToken = tokenJsonObject.value("access_token").toString();
		_refreshToken = tokenJsonObject.value("refresh_token").toString();
		_accessTokenReceived = QDateTime::currentDateTime();
				
		bool tokenRefreshing = reply->property("tokenRefreshing").toBool();

		if (tokenRefreshing) {
			saveConfigToLocalStorage();
			emit accessTokenRefreshed();
		}
		else {
			getCurrentUserInfo();					
		}		

	}
	else {
		QString dd = reply->errorString();
		emit registerBitrix24PortalFinished(false, QObject::tr("Get access token failed: %1.").arg(reply->errorString()));
	}

	reply->deleteLater();
}

void ITSBitrix24::getCurrentUserInfo() {

	QString getCurrentUserInfoUrl = QString("%1/rest/user.current.json?auth=%2").arg(_portalUrl, _accessToken);	
	QNetworkRequest getCurrentUserRequest(getCurrentUserInfoUrl);
	QNetworkReply *getCurrentUserReply = _networkAccessManager.get(getCurrentUserRequest);
		
	connect(getCurrentUserReply, SIGNAL(finished()), this, SLOT(onGetCurrentUserInfoFinished()));
	connect(getCurrentUserReply, SIGNAL(sslErrors(QList<QSslError>)), getCurrentUserReply, SLOT(ignoreSslErrors()));
}


void ITSBitrix24::onGetCurrentUserInfoFinished() {

	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (reply->error() == QNetworkReply::NoError) {
		QString strReply = reply->readAll();

		QJsonDocument jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
		QJsonObject responseJsonObject = jsonResponse.object();

		if (!responseJsonObject.contains("result")) {
			emit registerBitrix24PortalFinished(false, "Get current user info failed. \"result\" not valid.");
			reply->deleteLater();
			return;
		}

		QJsonObject resultJsonObject = responseJsonObject.value("result").toObject();
		if (!resultJsonObject.contains("ID")) {
			emit registerBitrix24PortalFinished(false, "Get current user info failed. \"ID\" not valid.");
			reply->deleteLater();
			return;
		}
		
		bool idIsValid = false;
		int32 id = resultJsonObject.value("ID").toString().toInt(&idIsValid);
		if (!idIsValid || id <= 0) {
			emit registerBitrix24PortalFinished(false, "Get current user info failed. \"ID\" not valid.");
			reply->deleteLater();
			return;
		}

		_currentUserId = id;
		emit registerBitrix24PortalFinished(true, "");

	}
	else {
		emit registerBitrix24PortalFinished(false, QObject::tr("Get current user info failed. Error: %1.").arg(reply->errorString()));
	}

	reply->deleteLater();

}


QString ITSBitrix24::saveConfigToLocalStorage() {	
	
	if (QUrl::fromUserInput(_portalUrl) == QUrl()) return QString("Portal url not valid.");	
	if (_clientId.isEmpty()) return QString("Client id not valid.");
	if (_clientSecret.isEmpty()) return QString("Client id not valid.");
	if (_accessToken.isEmpty()) return QString("Access token not valid.");	
	if (_refreshToken.isEmpty()) return QString("Refresh token not valid.");
	if (!_accessTokenReceived.isValid()) return QString("Access token received date token not valid.");
	if (_callbackPort <= 0) return QString("Prtal callback port not valid.");
	if (_defaultGroupId <= 0) return QString("Default group id not valid.");
	if (_currentUserId <= 0) return QString("Current user id not valid.");

	cSetBitrix24PortalURL(_portalUrl);
	cSetBitrix24PortalClientId(_clientId);
	cSetBitrix24PortalClientSecret(_clientSecret);
	cSetBitrix24PortalAccessToken(_accessToken);
	cSetBitrix24PortalRefreshToken(_refreshToken);
	cSetBitrix24PortalAccessTokenReceived(_accessTokenReceived);
	cSetBitrix24PortalCallbackPort(_callbackPort);
	cSetBitrix24PortalDefaultGroupId(_defaultGroupId);
	cSetBitrix24PortalUserId(_currentUserId);

	Local::writeUserSettings();

	return "";
}

QString ITSBitrix24::loadConfigFromLocalStorage() {

	QString
		portalUrl = cBitrix24PortalURL(),
		clientId = cBitrix24PortalClientId(),
		clientSecret = cBitrix24PortalClientSecret(),
		accessToken = cBitrix24PortalAccessToken(),
		refreshToken = cBitrix24PortalRefreshToken();

	int32
		callbackPort = cBitrix24PortalCallbackPort(),
		defaultGroupId = cBitrix24PortalDefaultGroupId(),
		userId = cBitrix24PortalUserId();

	QDateTime accessTokenReceived = cBitrix24PortalAccessTokenReceived();

	if (QUrl::fromUserInput(portalUrl) == QUrl()) return QString("Portal url not valid.");
	if (clientId.isEmpty()) return QString("Client id not valid.");
	if (clientSecret.isEmpty()) return QString("Client id not valid.");
	if (accessToken.isEmpty()) return QString("Access token not valid.");
	if (refreshToken.isEmpty()) return QString("Refresh token not valid.");
	if (!accessTokenReceived.isValid()) return QString("Access token received date token not valid.");
	if (callbackPort <= 0) return QString("Prtal callback port not valid.");
	if (defaultGroupId <= 0) return QString("Default group id port not valid.");
	if (userId <= 0) return QString("Current user id not valid.");

	_portalUrl = portalUrl;
	_clientId = clientId;
	_clientSecret = clientSecret;
	_accessToken = accessToken;
	_refreshToken = refreshToken;
	_callbackPort = callbackPort;
	_accessTokenReceived = accessTokenReceived;
	_defaultGroupId = defaultGroupId;
	_currentUserId = userId;

	return "";
}

void ITSBitrix24::loadAdminsListFromBitrix24Portal() {

	auto connection = std::make_shared<QMetaObject::Connection>();
	*connection = connect(this, &ITSBitrix24::accessTokenRefreshed, [this, connection]() {

		QString createTaskUrl = QString("%1/rest/disk.folder.getchildren").arg(_portalUrl);

		QUrlQuery createTaskData;
		createTaskData.addQueryItem("auth", _accessToken);
		createTaskData.addQueryItem("id", "1");

		QNetworkRequest createTaskRequest(createTaskUrl);
		createTaskRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

		QNetworkReply *createTaskReply = _networkAccessManager.post(createTaskRequest, createTaskData.toString(QUrl::FullyEncoded).toUtf8());
		connect(createTaskReply, SIGNAL(finished()), this, SLOT(onAdminsListLoaded()));
		connect(createTaskReply, SIGNAL(sslErrors(QList<QSslError>)), createTaskReply, SLOT(ignoreSslErrors()));
		
		QObject::disconnect(*connection);		

	});

	refreshAccessToken();	
}

void ITSBitrix24::onAdminsListLoaded() {

	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (reply->error() == QNetworkReply::NoError) {
		QString strReply = reply->readAll();

		QJsonDocument jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
		QJsonObject tokenJsonObject = jsonResponse.object();

		if (!tokenJsonObject.contains("result")) {
			emit adminsListFromBitrix24PortalLoaded(false, QStringList());
			reply->deleteLater();
			return;
		}

		if (!tokenJsonObject.value("result").isArray()) {
			emit adminsListFromBitrix24PortalLoaded(false, QStringList());
			reply->deleteLater();
			return;
		}

		QJsonArray jsonRootDirectoryItems = tokenJsonObject.value("result").toArray();
		foreach(QJsonValue jsonRootDirectoryItem, jsonRootDirectoryItems)
		{
			QJsonObject jsonDirectoryItem = jsonRootDirectoryItem.toObject();
			if (jsonDirectoryItem.value("NAME").toString() == "adminsList.txt" && jsonDirectoryItem.value("TYPE").toString() == "file") {
				QString downloadUrl = jsonDirectoryItem.value("DOWNLOAD_URL").toString();

				QNetworkRequest getAdminsListRequest(downloadUrl);
				QNetworkReply* reply = _networkAccessManager.get(getAdminsListRequest);
				connect(reply, &QNetworkReply::finished, [this, reply]() {
					
					if (reply->error() == QNetworkReply::NoError)
					{
						gAdminsList.clear();
						QString strReply(reply->readAll());

						if (strReply.size() == 0) {
							emit adminsListFromBitrix24PortalLoaded(false, QStringList());
							reply->deleteLater();
							return;
						}

						QStringList names = strReply.split("\n");

						cSetAdminsList(names);
						Local::writeUserSettings();

						emit adminsListFromBitrix24PortalLoaded(true, names);
						reply->deleteLater();
						return;
					}

					emit adminsListFromBitrix24PortalLoaded(false, QStringList());
					reply->deleteLater();
				
				});
				connect(reply, SIGNAL(sslErrors(QList<QSslError>)), reply, SLOT(ignoreSslErrors()));
			}		
		}
	}
	else {
		QString dd = reply->errorString();
		emit registerBitrix24PortalFinished(false, QObject::tr("Get access token failed: %1.").arg(reply->errorString()));
	}

	reply->deleteLater();	
}

void ITSBitrix24::createTask(QString title, QString description, int32 groupId, bool openCreatedTaskInBrowser) {

	auto connection = std::make_shared<QMetaObject::Connection>();
	*connection = connect(this, &ITSBitrix24::accessTokenRefreshed, [this, title, description, groupId, connection]() {

		QString createTaskUrl = QString("%1/rest/task.item.add.json").arg(_portalUrl);

		QUrlQuery createTaskData;
		createTaskData.addQueryItem("auth", _accessToken);
		createTaskData.addQueryItem("fields[TITLE]", title.toUtf8());
		createTaskData.addQueryItem("fields[DESCRIPTION]", description.toUtf8());
		createTaskData.addQueryItem("fields[RESPONSIBLE_ID]", "1");
		createTaskData.addQueryItem("fields[GROUP_ID]", QString::number(groupId));

		QNetworkRequest createTaskRequest(createTaskUrl);
		createTaskRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

		QNetworkReply *createTaskReply = _networkAccessManager.post(createTaskRequest, createTaskData.toString(QUrl::FullyEncoded).toUtf8());
		connect(createTaskReply, SIGNAL(finished()), this, SLOT(onCreateTaskFinished()));
		connect(createTaskReply, SIGNAL(sslErrors(QList<QSslError>)), createTaskReply, SLOT(ignoreSslErrors()));
		
		QObject::disconnect(*connection);
	});

	if (_defaultGroupId <= 0) {
		emit createTaskFinished(false, "Default group id no defined.");
		return;
	} 

	refreshAccessToken();

}

void ITSBitrix24::createTask(QString title, QString description, bool openCreatedTaskInBrowser) {

	auto connection = std::make_shared<QMetaObject::Connection>();
	int32 defaultGroupId = this->_defaultGroupId;
	*connection = connect(this, &ITSBitrix24::accessTokenRefreshed, [this, title, description, defaultGroupId, connection]() {

		QString createTaskUrl = QString("%1/rest/task.item.add.json").arg(_portalUrl);

		QUrlQuery createTaskData;
		createTaskData.addQueryItem("auth", _accessToken);
		createTaskData.addQueryItem("fields[TITLE]", title.toUtf8());
		createTaskData.addQueryItem("fields[DESCRIPTION]", description.toUtf8());
		createTaskData.addQueryItem("fields[RESPONSIBLE_ID]", QString::number(_currentUserId));
		createTaskData.addQueryItem("fields[GROUP_ID]", QString::number(defaultGroupId));

		QNetworkRequest createTaskRequest(createTaskUrl);
		createTaskRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

		QNetworkReply *createTaskReply = _networkAccessManager.post(createTaskRequest, createTaskData.toString(QUrl::FullyEncoded).toUtf8());
		connect(createTaskReply, SIGNAL(finished()), this, SLOT(onCreateTaskFinished()));
		connect(createTaskReply, SIGNAL(sslErrors(QList<QSslError>)), createTaskReply, SLOT(ignoreSslErrors()));

		QObject::disconnect(*connection);
	});

	refreshAccessToken();

}

void ITSBitrix24::onCreateTaskFinished() {

	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (reply->error() == QNetworkReply::NoError) {
		QString strReply = reply->readAll();

		QJsonDocument jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
		QJsonObject jsonResult = jsonResponse.object();

		if (!jsonResult.contains("result")) {
			emit createTaskFinished(false, QString("Response not \"constaint\" result.").arg(reply->errorString()));
			reply->deleteLater();
			return;
		}		
		emit createTaskFinished(true, QString::number(jsonResult.value("result").toInt()));
	}
	else {
		emit createTaskFinished(false, QString("Error description: %1").arg(reply->errorString()));
	}
	
	reply->deleteLater();
}

bool ITSBitrix24::checkBitrix24RegData() {
	
	QString
		portalUrl = cBitrix24PortalURL(),
		clientId = cBitrix24PortalClientId(),
		clientSecret = cBitrix24PortalClientSecret(),
		accessToken = cBitrix24PortalAccessToken(),
		refreshToken = cBitrix24PortalRefreshToken();

	int32
		defaultGroupId = cBitrix24PortalDefaultGroupId(),
		userId = cBitrix24PortalUserId();

	if (QUrl::fromUserInput(portalUrl) == QUrl()) return false;
	if (clientId.isEmpty()) return false;
	if (clientSecret.isEmpty()) return false;
	if (accessToken.isEmpty()) return false;
	if (refreshToken.isEmpty()) return false;
	if (defaultGroupId <= 0) return false;
	if (userId <= 0) return false;

	return true;
}

