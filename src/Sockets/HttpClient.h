/* Copyright 2013-2017 Sathya Laufer
 *
 * libhomegear-base is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * libhomegear-base is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libhomegear-base.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU Lesser General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
*/

#ifndef HTTPCLIENT_H_
#define HTTPCLIENT_H_

#include "../Exception.h"
#include "../Managers/FileDescriptorManager.h"
#include "../Encoding/Http.h"
#include "TcpSocket.h"

namespace BaseLib
{
/**
 * Exception class for the HTTP client.
 *
 * @see HttpClient
 */
class HttpClientException : public Exception
{
private:
	int32_t _responseCode = -1;
public:
	HttpClientException(std::string message) : Exception(message) {}
	HttpClientException(std::string message, int32_t responseCode) : Exception(message), _responseCode(responseCode) {}

	int32_t responseCode() { return _responseCode; }
};

/**
 * This class provides a basic HTTP client. The class is thread safe.
 *
 * @see HTTPClientException
 */
class HttpClient
{
public:
	/**
	 * Constructor
	 *
	 * @param baseLib The common base library object.
	 * @param hostname The hostname of the client to connect to without "http://".
	 * @param port (default 80) The port to connect to.
	 * @param keepAlive (default true) Keep the socket open after each request.
	 * @param useSSL (default false) Set to "true" to use "https".
	 * @param caFile (default "") Path to the certificate authority file of the certificate authority which signed the server certificate.
	 * @param verifyCertificate (default true) Set to "true" to verify the server certificate (highly recommended).
	 * @param certPath (default "") Path to the PEM encoded client certificate
	 * @param keyPath (default "") Path to the PEM encoded client keyfile
	 */
	HttpClient(BaseLib::SharedObjects* baseLib, std::string hostname, int32_t port = 80, bool keepAlive = true, bool useSSL = false, std::string caFile = "", bool verifyCertificate = true, std::string certPath = "", std::string keyPath = "");

	/**
	 * Constructor. ...Data or ...File are chosen automatically. Just leave the one you don't want to use empty.
	 *
	 * @param baseLib The common base library object.
	 * @param hostname The hostname of the client to connect to without "http://".
	 * @param port (default 80) The port to connect to.
	 * @param keepAlive (default true) Keep the socket open after each request.
	 * @param useSSL (default false) Set to "true" to use "https".
	 * @param verifyCertificate (default true) Set to "true" to verify the server certificate (highly recommended).
	 * @param caFile (default "") Path to the certificate authority file of the certificate authority which signed the server certificate.
	 * @param caData The PEM-encoded CA certificate (not the path) used to sign the server certificate.
	 * @param certPath (default "") Path to the PEM encoded client certificate
	 * @param clientCertData The PEM-encoded client certificate (not the path).
	 * @param keyPath (default "") Path to the PEM encoded client keyfile
	 * @param clientKeyData The PEM-encoded client key (not the path).
	 */
	HttpClient(BaseLib::SharedObjects* baseLib, std::string hostname, int32_t port, bool keepAlive, bool useSSL, bool verifyCertificate, std::string caFile, std::string caData, std::string certPath, std::string certData, std::string keyPath, std::string keyData);

	/**
	 * Destructor
	 */
	virtual ~HttpClient();

	/**
	 * Sets the socket timeout.
	 * @param value The timeout in milliseconds.
	 */
	void setTimeout(uint32_t value);

	/**
	 * Returns "true" if the socket is connected, otherwise "false".
	 * @return "true" if the socket is connected, otherwise "false".
	 */
	bool connected() { return _socket && _socket->connected(); }

	/**
	 * Closes the socket.
	 */
	void disconnect() { if(_socket) _socket->close(); }

	/**
	 * Enables storage of raw content. The raw content can be retrieved with getRawContent().
	 * @param value Set to true to enable.
	 */
	void enableRawContent(bool value) { _keepRawContent = value; }

	/**
	 * Returns the raw content. Only available when enableRawContent() was set to true.
	 * @return The raw respones as received from the HTTP server.
	 */
	std::vector<char>& getRawContent() { return _rawContent; }

	/**
	 * Returns the IP address of the HTTP server.
	 * @return The IP address.
	 */
	std::string getIpAddress() { return _socket ? _socket->getIpAddress() : ""; }

	/*
	 * Sends an HTTP request and returns the response.
	 *
	 * @param[in] request The HTTP request including the full header.
	 * @param[out] response The HTTP response without the header.
	 */
	void sendRequest(const std::string& request, std::string& response, bool responseIsHeaderOnly = false);

	/*
	 * Sends an HTTP request and returns the http object.
	 *
	 * @param[in] request The HTTP request including the full header.
	 * @param[out] response The HTTP response.
	 */
	void sendRequest(const std::string& request, Http& response, bool responseIsHeaderOnly = false);

	/*
	 * Sends an HTTP GET request and returns the response. This method can be used to download files.
	 *
	 * @param[in] url The path of the file to get.
	 * @param[out] data The data returned.
	 */
	void get(const std::string& path, std::string& data);

	/*
	 * Sends an HTTP GET request and returns the response. This method can be used to download files.
	 *
	 * @param[in] url The path of the file to get.
	 * @param[out] data The data returned.
	 */
	void get(const std::string& path, Http& data);

	/*
	 * Sends an HTTP POST request and returns the response.
	 *
	 * @param[in] url The path to post to.
	 * @param[in] dataIn The POST data.
	 * @param[out] dataOut The data returned.
	 */
	void post(const std::string& path, std::string& dataIn, std::string& dataOut);

	/*
	 * Sends an HTTP POST request and returns the response.
	 *
	 * @param[in] url The path to post to.
	 * @param[in] dataIn The POST data.
	 * @param[out] dataOut The data returned.
	 */
	void post(const std::string& path, std::string& dataIn, Http& dataOut);
protected:
	/**
	 * The common base library object.
	 */
	BaseLib::SharedObjects* _bl = nullptr;

	/**
	 * Protects _socket to only allow one operation at a time.
	 *
	 * @see _socket
	 */
	std::mutex _socketMutex;

	/**
	 * The socket object.
	 */
	std::unique_ptr<TcpSocket> _socket;

	/**
	 * The hostname of the HTTP server.
	 */
	std::string _hostname = "";

	/**
	 * The port the HTTP server listens on.
	 */
	int32_t _port = 80;

	/**
	 * Stores the information if the socket connection should be kept open after each request.
	 */
	bool _keepAlive = true;

	/**
	 * When true, the raw content is stored
	 */
	bool _keepRawContent = false;

	/**
	 * Stores the raw response
	 */
	std::vector<char> _rawContent;
};

}
#endif
