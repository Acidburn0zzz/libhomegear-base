/* Copyright 2013-2016 Sathya Laufer
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

#ifndef UDPSOCKETOPERATIONS_H_
#define UDPSOCKETOPERATIONS_H_

#include "SocketExceptions.h"
#include "../Managers/FileDescriptorManager.h"

namespace BaseLib
{

class Obj;

class UdpSocket
{
public:
	UdpSocket(BaseLib::Obj* baseLib);
	UdpSocket(BaseLib::Obj* baseLib, std::string hostname, std::string port);
	virtual ~UdpSocket();

	void setReadTimeout(int64_t timeout) { _readTimeout = timeout; }
	void setAutoConnect(bool autoConnect) { _autoConnect = autoConnect; }
	void setHostname(std::string hostname) { close(); _hostname = hostname; }
	void setPort(std::string port) { close(); _port = port; }
	std::string getListenIp() { return _listenIp; }
	int32_t getListenPort() { return _listenPort; }

	bool isOpen();
	int32_t proofread(char* buffer, int32_t bufferSize, std::string& senderIp);
	int32_t proofwrite(const std::shared_ptr<std::vector<char>> data);
	int32_t proofwrite(const std::vector<char>& data);
	int32_t proofwrite(const std::string& data);
	int32_t proofwrite(const char* buffer, int32_t bytesToWrite);
	void open();
	void close();
protected:
	BaseLib::Obj* _bl = nullptr;
	int64_t _readTimeout = 15000000;
	bool _autoConnect = true;
	std::string _hostname;
	std::string _port;
	std::string _listenIp;
	int32_t _listenPort = -1;
	struct addrinfo* _serverInfo = nullptr;
	std::mutex _readMutex;
	std::mutex _writeMutex;

	std::shared_ptr<FileDescriptor> _socketDescriptor;

	void getSocketDescriptor();
	void getConnection();
	void autoConnect();
};

typedef std::shared_ptr<BaseLib::UdpSocket> PUdpSocket;

}

#endif
