/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Homegear.  If not, see
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

#include "../BaseLib.h"
#include "HttpServer.h"

namespace BaseLib
{

HttpServer::HttpServer(BaseLib::SharedObjects* baseLib, std::string listenAddress, uint16_t port, bool useSsl, std::string certFile, std::string certData, std::string keyFile, std::string keyData, std::string dhParamFile, std::string dhParamData)
{
	_bl = baseLib;
	_listenAddress = listenAddress;
	_port = port;
	_useSsl = useSsl;
	_certFile = certFile;
	_certData = certData;
	_keyFile = keyFile;
	_keyData = keyData;
	_dhParamFile = dhParamFile;
	_dhParamData = dhParamData;

	_stopped = true;
	_stopServer = true;
}

HttpServer::~HttpServer()
{
	stop();
}

void HttpServer::start()
{
	stop();
	_stopServer = false;

	_bl->threadManager.start(_mainThread, true, &HttpServer::mainThread, this);
	_stopped = false;
}

void HttpServer::stop()
{
	if(_stopped) return;
	_stopped = true;
	_stopServer = true;
	_bl->threadManager.join(_mainThread);
	_stateMutex.lock();
	for(std::map<int32_t, std::shared_ptr<Client>>::iterator i = _clients.begin(); i != _clients.end(); ++i)
	{
		closeClientConnection(i->second);
	}
	_stateMutex.unlock();
	while(_clients.size() > 0)
	{
		collectGarbage();
		if(_clients.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

uint32_t HttpServer::connectionCount()
{
	try
	{
		_stateMutex.lock();
		uint32_t connectionCount = _clients.size();
		_stateMutex.unlock();
		return connectionCount;
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _stateMutex.unlock();
    return 0;
}

void HttpServer::registerMethod(std::string methodName, std::shared_ptr<BaseLib::Rpc::RpcMethod> method)
{
	try
	{
		if(_rpcMethods->find(methodName) != _rpcMethods->end())
		{
			_out.printWarning("Warning: Could not register RPC method \"" + methodName + "\", because a method with this name already exists.");
			return;
		}
		_rpcMethods->insert(std::pair<std::string, std::shared_ptr<BaseLib::Rpc::RpcMethod>>(methodName, method));
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::closeClientConnection(std::shared_ptr<Client> client)
{
	try
	{
		if(!client) return;
		GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
		client->closed = true;
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::mainThread()
{
	try
	{
		_socket = std::make_shared<TcpSocket>(_bl, _useSsl, _certFile, _certData, _keyFile, _keyData, _dhParamFile, _dhParamData);
		std::string boundAddress;
		_socket->bindSocket(_listenAddress, std::to_string(_port), boundAddress);

		std::string clientAddress;
		std::string clientPort;
		while(!_stopServer)
		{
			try
			{
				if(!_socket->connected())
				{
					if(_stopServer) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(5000));
					_socket = std::make_shared<TcpSocket>(_bl, _useSsl, _certFile, _certData, _keyFile, _keyData, _dhParamFile, _dhParamData);
					_socket->bindSocket(_listenAddress, std::to_string(_port), boundAddress);
					continue;
				}
				std::shared_ptr<BaseLib::FileDescriptor> clientFileDescriptor = _socket->waitForConnection(clientAddress, clientPort);
				if(!clientFileDescriptor || clientFileDescriptor->descriptor == -1) continue;
				std::shared_ptr<Client> client(new Client());
				{
					std::lock_guard<std::mutex> stateGuard(_stateMutex);
					client->id = _currentClientID++;
					if(client->id == -1) client->id = _currentClientID++; //-1 is not allowed
					client->socketDescriptor = clientFileDescriptor;
					while(_clients.find(client->id) != _clients.end())
					{
						_out.printError("Error: Client id was used twice. This shouldn't happen. Please report this error to the developer.");
						_currentClientID++;
						client->id++;
					}
					_clients[client->id] = client;
					_out.printInfo("Info: RPC server client id for client number " + std::to_string(client->socketDescriptor->id) + " is: " + std::to_string(client->id));
				}

				try
				{
					if(_info->ssl)
					{
						getSSLSocketDescriptor(client);
						if(!client->socketDescriptor->tlsSession)
						{
							//Remove client from _clients again. Socket is already closed.
							closeClientConnection(client);
							continue;
						}
					}
					client->socket = std::shared_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(GD::bl.get(), client->socketDescriptor));
					client->socket->setReadTimeout(100000);
					client->socket->setWriteTimeout(15000000);
					client->address = address;
					client->port = port;

#ifdef CCU2
					if(client->address == "127.0.0.1")
					{
						client->clientType = BaseLib::RpcClientType::ccu2;
						_out.printInfo("Info: Client type set to \"CCU2\".");
					}
#endif

					GD::bl->threadManager.start(client->readThread, false, _threadPriority, _threadPolicy, &HttpServer::readClient, this, client);
				}
				catch(const std::exception& ex)
				{
					closeClientConnection(client);
					_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}
				catch(BaseLib::Exception& ex)
				{
					closeClientConnection(client);
					_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
				}
				catch(...)
				{
					closeClientConnection(client);
					_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
				}
			}
			catch(const std::exception& ex)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    GD::bl->fileDescriptorManager.shutdown(_serverFileDescriptor);
}

bool HttpServer::clientValid(std::shared_ptr<Client>& client)
{
	try
	{
		if(client->socketDescriptor->descriptor < 0) return false;
		return true;
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _stateMutex.unlock();
    return false;
}

void HttpServer::sendRPCResponseToClient(std::shared_ptr<Client> client, std::vector<char>& data, bool keepAlive)
{
	try
	{
		if(_stopped) return;
		if(!clientValid(client)) return;
		if(data.empty()) return;
		bool error = false;
		try
		{
			//Sleep a tiny little bit. Some clients like the linux version of IP-Symcon don't accept responses too fast.
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			if(!keepAlive || client->rpcType != BaseLib::RpcType::binary) std::this_thread::sleep_for(std::chrono::milliseconds(20)); //Add additional time for XMLRPC requests. Otherwise clients might not receive response.
			client->socket->proofwrite(data);
		}
		catch(BaseLib::SocketDataLimitException& ex)
		{
			_out.printWarning("Warning: " + ex.what());
		}
		catch(const BaseLib::SocketOperationException& ex)
		{
			_out.printError("Error: " + ex.what());
			error = true;
		}
		if(!keepAlive || error) closeClientConnection(client);
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::analyzeRPC(std::shared_ptr<Client> client, std::vector<char>& packet, PacketType::Enum packetType, bool keepAlive)
{
	try
	{
		if(_stopped) return;

		PacketType::Enum responseType = PacketType::Enum::xmlResponse;
		if(packetType == PacketType::Enum::binaryRequest) responseType = PacketType::Enum::binaryResponse;
		else if(packetType == PacketType::Enum::xmlRequest) responseType = PacketType::Enum::xmlResponse;
		else if(packetType == PacketType::Enum::jsonRequest) responseType = PacketType::Enum::jsonResponse;
		else if(packetType == PacketType::Enum::webSocketRequest) responseType = PacketType::Enum::webSocketResponse;

		std::string methodName;
		int32_t messageId = 0;
		std::shared_ptr<std::vector<BaseLib::PVariable>> parameters;
		if(packetType == PacketType::Enum::binaryRequest)
		{
			if(client->rpcType == BaseLib::RpcType::unknown) client->rpcType = BaseLib::RpcType::binary;
			if(client->clientType == BaseLib::RpcClientType::ccu2) parameters = _rpcDecoderAnsi->decodeRequest(packet, methodName);
			else parameters = _rpcDecoder->decodeRequest(packet, methodName);
		}
		else if(packetType == PacketType::Enum::xmlRequest)
		{
			if(client->rpcType == BaseLib::RpcType::unknown) client->rpcType = BaseLib::RpcType::xml;
			parameters = _xmlRpcDecoder->decodeRequest(packet, methodName);
		}
		else if(packetType == PacketType::Enum::jsonRequest || packetType == PacketType::Enum::webSocketRequest)
		{
			if(client->rpcType == BaseLib::RpcType::unknown) client->rpcType = BaseLib::RpcType::json;
			BaseLib::PVariable result = _jsonDecoder->decode(packet);
			if(result->type == BaseLib::VariableType::tStruct)
			{
				if(result->structValue->find("user") != result->structValue->end())
				{
					_out.printWarning("Warning: WebSocket auth packet received but auth is disabled for WebSockets. Closing connection.");
					closeClientConnection(client);
					return;
				}
				if(result->structValue->find("id") != result->structValue->end()) messageId = result->structValue->at("id")->integerValue;
				if(result->structValue->find("method") == result->structValue->end())
				{
					_out.printWarning("Warning: Could not decode JSON RPC packet:\n" + result->print(false, false));
					sendRPCResponseToClient(client, BaseLib::Variable::createError(-32500, "Could not decode RPC packet. \"method\" not found in JSON."), messageId, responseType, keepAlive);
					return;
				}
				methodName = result->structValue->at("method")->stringValue;
				if(result->structValue->find("params") != result->structValue->end()) parameters = result->structValue->at("params")->arrayValue;
				else parameters.reset(new std::vector<BaseLib::PVariable>());
			}
		}
		if(!parameters)
		{
			_out.printWarning("Warning: Could not decode RPC packet. Parameters are empty.");
			sendRPCResponseToClient(client, BaseLib::Variable::createError(-32500, "Could not decode RPC packet. Parameters are empty."), messageId, responseType, keepAlive);
			return;
		}

		if(!parameters->empty() && parameters->at(0)->errorStruct)
		{
			sendRPCResponseToClient(client, parameters->at(0), messageId, responseType, keepAlive);
			return;
		}
		callMethod(client, methodName, parameters, messageId, responseType, keepAlive);
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::sendRPCResponseToClient(std::shared_ptr<Client> client, BaseLib::PVariable variable, int32_t messageId, PacketType::Enum responseType, bool keepAlive)
{
	try
	{
		if(_stopped) return;
		std::vector<char> data;
		if(responseType == PacketType::Enum::xmlResponse)
		{
			_xmlRpcEncoder->encodeResponse(variable, data);
			data.push_back('\r');
			data.push_back('\n');
			std::string header = getHttpResponseHeader("text/xml", data.size() + 21, !keepAlive);
			header.append("<?xml version=\"1.0\"?>");
			data.insert(data.begin(), header.begin(), header.end());
			if(GD::bl->debugLevel >= 5)
			{
				_out.printDebug("Response packet: " + std::string(&data.at(0), data.size()));
			}
		}
		else if(responseType == PacketType::Enum::binaryResponse)
		{
			_rpcEncoder->encodeResponse(variable, data);
			if(GD::bl->debugLevel >= 5)
			{
				_out.printDebug("Response binary:");
				_out.printBinary(data);
			}
		}
		else if(responseType == PacketType::Enum::jsonResponse)
		{
			_jsonEncoder->encodeResponse(variable, messageId, data);
			data.push_back('\r');
			data.push_back('\n');
			std::string header = getHttpResponseHeader("application/json", data.size(), !keepAlive);
			data.insert(data.begin(), header.begin(), header.end());
			if(GD::bl->debugLevel >= 5)
			{
				_out.printDebug("Response packet: " + std::string(&data.at(0), data.size()));
			}
		}
		else if(responseType == PacketType::Enum::webSocketResponse)
		{
			std::vector<char> json;
			_jsonEncoder->encodeResponse(variable, messageId, json);
			BaseLib::WebSocket::encode(json, BaseLib::WebSocket::Header::Opcode::text, data);
			if(GD::bl->debugLevel >= 5)
			{
				_out.printDebug("Response WebSocket packet: ");
				_out.printBinary(data);
			}
		}
		sendRPCResponseToClient(client, data, keepAlive);
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

BaseLib::PVariable HttpServer::callMethod(std::string& methodName, BaseLib::PVariable& parameters)
{
	try
	{
		if(!parameters) parameters = BaseLib::PVariable(new BaseLib::Variable(BaseLib::VariableType::tArray));
		if(_stopped || GD::bl->shuttingDown) return BaseLib::Variable::createError(100000, "Server is stopped.");
		if(_rpcMethods->find(methodName) == _rpcMethods->end())
		{
			BaseLib::PVariable result = GD::ipcServer->callRpcMethod(methodName, parameters->arrayValue);
			return result;
		}
		_lifetick1Mutex.lock();
		_lifetick1.second = false;
		_lifetick1.first = BaseLib::HelperFunctions::getTime();
		_lifetick1Mutex.unlock();
		if(GD::bl->debugLevel >= 4)
		{
			_out.printInfo("Info: RPC Method called: " + methodName + " Parameters:");
			for(std::vector<BaseLib::PVariable>::iterator i = parameters->arrayValue->begin(); i != parameters->arrayValue->end(); ++i)
			{
				(*i)->print(true, false);
			}
		}
		BaseLib::PVariable ret = _rpcMethods->at(methodName)->invoke(_dummyClientInfo, parameters->arrayValue);
		if(GD::bl->debugLevel >= 5)
		{
			_out.printDebug("Response: ");
			ret->print(true, false);
		}
		_lifetick1Mutex.lock();
		_lifetick1.second = true;
		_lifetick1Mutex.unlock();
		return ret;
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return BaseLib::Variable::createError(-32500, ": Unknown application error.");
}

void HttpServer::callMethod(std::shared_ptr<Client> client, std::string methodName, std::shared_ptr<std::vector<BaseLib::PVariable>> parameters, int32_t messageId, PacketType::Enum responseType, bool keepAlive)
{
	try
	{
		if(_stopped || GD::bl->shuttingDown) return;

		if(methodName == "setClientType" && parameters->size() > 0)
		{
			if(parameters->at(0)->integerValue == 1)
			{
				_out.printInfo("Info: Type of client " + std::to_string(client->id) + " set to addon.");
				client->addon = true;
				BaseLib::PVariable ret(new BaseLib::Variable());
				sendRPCResponseToClient(client, ret, messageId, responseType, keepAlive);
			}
			return;
		}

		if(_rpcMethods->find(methodName) == _rpcMethods->end())
		{
			BaseLib::PVariable result = GD::ipcServer->callRpcMethod(methodName, parameters);
			sendRPCResponseToClient(client, result, messageId, responseType, keepAlive);
			return;
		}
		_lifetick2Mutex.lock();
		_lifetick2.first = BaseLib::HelperFunctions::getTime();
		_lifetick2.second = false;
		_lifetick2Mutex.unlock();
		if(GD::bl->debugLevel >= 4)
		{
			_out.printInfo("Info: Client number " + std::to_string(client->socketDescriptor->id) + (client->clientType == BaseLib::RpcClientType::ccu2 ? " (CCU2)" : "") + (client->clientType == BaseLib::RpcClientType::ipsymcon ? " (IP-Symcon)" : "") + " is calling RPC method: " + methodName + " (" + std::to_string((int32_t)(client->rpcType)) + ") Parameters:");
			for(std::vector<BaseLib::PVariable>::iterator i = parameters->begin(); i != parameters->end(); ++i)
			{
				(*i)->print(true, false);
			}
		}
		BaseLib::PVariable ret = _rpcMethods->at(methodName)->invoke(client, parameters);
		if(GD::bl->debugLevel >= 5)
		{
			_out.printDebug("Response: ");
			ret->print(true, false);
		}
		sendRPCResponseToClient(client, ret, messageId, responseType, keepAlive);
		_lifetick2Mutex.lock();
		_lifetick2.second = true;
		_lifetick2Mutex.unlock();
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::string HttpServer::getHttpResponseHeader(std::string contentType, uint32_t contentLength, bool closeConnection)
{
	std::string header;
	header.append("HTTP/1.1 200 OK\r\n");
	header.append("Connection: ");
	header.append(closeConnection ? "close\r\n" : "Keep-Alive\r\n");
	header.append("Content-Type: " + contentType + "\r\n");
	header.append("Content-Length: ").append(std::to_string(contentLength)).append("\r\n\r\n");
	return header;
}

void HttpServer::analyzeRPCResponse(std::shared_ptr<Client> client, std::vector<char>& packet, PacketType::Enum packetType, bool keepAlive)
{
	try
	{
		if(_stopped) return;
		BaseLib::PVariable response;
		if(packetType == PacketType::Enum::binaryResponse)
		{
			if(client->clientType == BaseLib::RpcClientType::ccu2) response = _rpcDecoderAnsi->decodeResponse(packet);
			else response = _rpcDecoder->decodeResponse(packet);
		}
		else if(packetType == PacketType::Enum::xmlResponse) response = _xmlRpcDecoder->decodeResponse(packet);
		if(!response) return;
		if(GD::bl->debugLevel >= 3)
		{
			_out.printWarning("Warning: RPC server received RPC response. This shouldn't happen. Response data: ");
			response->print(true, false);
		}
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::packetReceived(std::shared_ptr<Client> client, std::vector<char>& packet, PacketType::Enum packetType, bool keepAlive)
{
	try
	{
		if(packetType == PacketType::Enum::binaryRequest || packetType == PacketType::Enum::xmlRequest || packetType == PacketType::Enum::jsonRequest || packetType == PacketType::Enum::webSocketRequest) analyzeRPC(client, packet, packetType, keepAlive);
		else if(packetType == PacketType::Enum::binaryResponse || packetType == PacketType::Enum::xmlResponse) analyzeRPCResponse(client, packet, packetType, keepAlive);
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

const std::vector<BaseLib::PRpcClientInfo> HttpServer::getClientInfo()
{
	std::vector<BaseLib::PRpcClientInfo> clients;
	_stateMutex.lock();
	try
	{
		for(std::map<int32_t, std::shared_ptr<Client>>::const_iterator i = _clients.begin(); i != _clients.end(); i++)
		{
			if(i->second->closed) continue;
			clients.push_back(i->second);
		}
	}
	catch(const std::exception& ex)
	{
		_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	_stateMutex.unlock();
	return clients;
}

BaseLib::PEventHandler HttpServer::addWebserverEventHandler(BaseLib::Rpc::IWebserverEventSink* eventHandler)
{
	if(_webServer) return _webServer->addEventHandler(eventHandler);
	return BaseLib::PEventHandler();
}

void HttpServer::removeWebserverEventHandler(BaseLib::PEventHandler eventHandler)
{
	if(_webServer) _webServer->removeEventHandler(eventHandler);
}

void HttpServer::collectGarbage()
{
	_garbageCollectionMutex.lock();
	try
	{
		_lastGargabeCollection = GD::bl->hf.getTime();
		std::vector<std::shared_ptr<Client>> clientsToRemove;
		_stateMutex.lock();
		try
		{
			for(std::map<int32_t, std::shared_ptr<Client>>::iterator i = _clients.begin(); i != _clients.end(); ++i)
			{
				if(i->second->closed) clientsToRemove.push_back(i->second);
			}
		}
		catch(const std::exception& ex)
		{
			_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
		}
		catch(...)
		{
			_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
		}
		_stateMutex.unlock();
		for(std::vector<std::shared_ptr<Client>>::iterator i = clientsToRemove.begin(); i != clientsToRemove.end(); ++i)
		{
			_out.printDebug("Debug: Joining read thread of client " + std::to_string((*i)->id));
			GD::bl->threadManager.join((*i)->readThread);
			_stateMutex.lock();
			try
			{
				_clients.erase((*i)->id);
			}
			catch(const std::exception& ex)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
			_stateMutex.unlock();
			_out.printDebug("Debug: Client " + std::to_string((*i)->id) + " removed.");
		}
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _garbageCollectionMutex.unlock();
}

void HttpServer::handleConnectionUpgrade(std::shared_ptr<Client> client, BaseLib::Http& http)
{
	try
	{
		if(http.getHeader().fields.find("upgrade") != http.getHeader().fields.end() && BaseLib::HelperFunctions::toLower(http.getHeader().fields["upgrade"]) == "websocket")
		{
			if(http.getHeader().fields.find("sec-websocket-protocol") == http.getHeader().fields.end() && (http.getHeader().path.empty() || http.getHeader().path == "/"))
			{
				closeClientConnection(client);
				_out.printError("Error: No websocket protocol specified.");
				return;
			}
			if(http.getHeader().fields.find("sec-websocket-key") == http.getHeader().fields.end())
			{
				closeClientConnection(client);
				_out.printError("Error: No websocket key specified.");
				return;
			}
			std::string protocol = http.getHeader().fields["sec-websocket-protocol"];
			BaseLib::HelperFunctions::toLower(protocol);
			std::string websocketKey = http.getHeader().fields["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			std::vector<char> data(&websocketKey[0], &websocketKey[0] + websocketKey.size());
			std::vector<char> sha1;
			BaseLib::Security::Hash::sha1(data, sha1);
			std::string websocketAccept;
			BaseLib::Base64::encode(sha1, websocketAccept);
			std::string pathProtocol;
			int32_t pos = http.getHeader().path.find('/', 1);
			if(http.getHeader().path.size() == 7 || pos == 7) pathProtocol = http.getHeader().path.substr(1, 6);
			else if(http.getHeader().path.size() == 11 || pos == 11) pathProtocol = http.getHeader().path.substr(1, 10);
			if(pathProtocol == "client" || pathProtocol == "server")
			{
				//path starts with "/client/" or "/server/". Both are not part of the client id.
				if(http.getHeader().path.size() > 8) client->webSocketClientId = http.getHeader().path.substr(8);
			}
			else if(pathProtocol == "nodeclient" || pathProtocol == "nodeserver")
			{
				//path starts with "/nodeclient/" or "/nodeserver/". Both are not part of the client id.
				if(http.getHeader().path.size() > 12) client->webSocketClientId = http.getHeader().path.substr(12);
			}
			else if(http.getHeader().path.size() > 1)
			{
				pathProtocol.clear();
				//Full path is client id.
				client->webSocketClientId = http.getHeader().path.substr(1);
			}
			BaseLib::HelperFunctions::toLower(client->webSocketClientId);

			if(protocol == "server" || pathProtocol == "server" || protocol == "nodeserver" || pathProtocol == "nodeserver")
			{
				client->rpcType = BaseLib::RpcType::websocket;
				client->initJsonMode = true;
				client->initKeepAlive = true;
				client->initNewFormat = true;
				client->initSubscribePeers = true;
				std::string header;
				header.reserve(133 + websocketAccept.size());
				header.append("HTTP/1.1 101 Switching Protocols\r\n");
				header.append("Connection: Upgrade\r\n");
				header.append("Upgrade: websocket\r\n");
				header.append("Sec-WebSocket-Accept: ").append(websocketAccept).append("\r\n");
				if(!protocol.empty()) header.append("Sec-WebSocket-Protocol: " + protocol + "\r\n");
				header.append("\r\n");
				std::vector<char> data(&header[0], &header[0] + header.size());
				sendRPCResponseToClient(client, data, true);
			}
			else if(protocol == "client" || pathProtocol == "client" || protocol == "nodeclient" || pathProtocol == "nodeclient")
			{
				client->rpcType = BaseLib::RpcType::websocket;
				client->webSocketClient = true;
				if(protocol == "nodeclient" || pathProtocol == "nodeclient") client->nodeClient = true;
				std::string header;
				header.reserve(133 + websocketAccept.size());
				header.append("HTTP/1.1 101 Switching Protocols\r\n");
				header.append("Connection: Upgrade\r\n");
				header.append("Upgrade: websocket\r\n");
				header.append("Sec-WebSocket-Accept: ").append(websocketAccept).append("\r\n");
				if(!protocol.empty()) header.append("Sec-WebSocket-Protocol: " + protocol + "\r\n");
				header.append("\r\n");
				std::vector<char> data(&header[0], &header[0] + header.size());
				sendRPCResponseToClient(client, data, true);
				if(_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::none)
				{
					_out.printInfo("Info: Transferring client number " + std::to_string(client->id) + " to rpc client.");
					GD::rpcClient->addWebSocketServer(client->socket, client->webSocketClientId, client->address, client->nodeClient);
					client->socketDescriptor.reset(new BaseLib::FileDescriptor());
					client->socket.reset(new BaseLib::TcpSocket(GD::bl.get()));
					client->closed = true;
				}
			}
			else
			{
				closeClientConnection(client);
				_out.printError("Error: Unknown websocket protocol. Known protocols are \"server\" and \"client\".");
			}
		}
		else
		{
			closeClientConnection(client);
			_out.printError("Error: Connection upgrade type not supported: " + http.getHeader().fields["upgrade"]);
		}
	}
	catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HttpServer::readClient(std::shared_ptr<Client> client)
{
	try
	{
		if(!client) return;
		int32_t bufferMax = 1024;
		char buffer[bufferMax + 1];
		//Make sure the buffer is null terminated.
		buffer[bufferMax] = '\0';
		int32_t processedBytes = 0;
		int32_t bytesRead = 0;
		PacketType::Enum packetType = PacketType::binaryRequest;
		BaseLib::Rpc::BinaryRpc binaryRpc(GD::bl.get());
		BaseLib::Http http;
		BaseLib::WebSocket webSocket;

		_out.printDebug("Listening for incoming packets from client number " + std::to_string(client->socketDescriptor->id) + ".");
		while(!_stopServer)
		{
			try
			{
				bytesRead = client->socket->proofread(buffer, bufferMax);
				buffer[bufferMax] = 0; //Even though it shouldn't matter, make sure there is a null termination.
				//Some clients send only one byte in the first packet
				if(bytesRead == 1 && !binaryRpc.processingStarted() && !http.headerProcessingStarted() && !webSocket.dataProcessingStarted()) bytesRead += client->socket->proofread(&buffer[1], bufferMax - 1);
			}
			catch(const BaseLib::SocketTimeOutException& ex)
			{
				continue;
			}
			catch(const BaseLib::SocketClosedException& ex)
			{
				if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: " + ex.what());
				break;
			}
			catch(const BaseLib::SocketOperationException& ex)
			{
				_out.printError(ex.what());
				break;
			}

			if(!clientValid(client)) break;

			if(GD::bl->debugLevel >= 5)
			{
				std::vector<uint8_t> rawPacket(buffer, buffer + bytesRead);
				_out.printDebug("Debug: Packet received: " + BaseLib::HelperFunctions::getHexString(rawPacket));
			}

			if(binaryRpc.processingStarted() || (!binaryRpc.processingStarted() && !http.headerProcessingStarted() && !webSocket.dataProcessingStarted() && !strncmp(&buffer[0], "Bin", 3)))
			{
				if(!_info->xmlrpcServer) continue;

				try
				{
					processedBytes = 0;
					while(processedBytes < bytesRead)
					{
						processedBytes += binaryRpc.process(&buffer[processedBytes], bytesRead - processedBytes);
						if(binaryRpc.isFinished())
						{
							std::shared_ptr<BaseLib::Rpc::RpcHeader> header = _rpcDecoder->decodeHeader(binaryRpc.getData());
							if(_info->authType == BaseLib::Rpc::ServerInfo::Info::AuthType::basic)
							{
								if(!client->auth.initialized()) client->auth = Auth(client->socket, _info->validUsers);
								try
								{
									if(!client->auth.basicServer(header))
									{
										_out.printError("Error: Authorization failed. Closing connection.");
										break;
									}
									else _out.printDebug("Client successfully authorized using basic authentication.");
								}
								catch(AuthException& ex)
								{
									_out.printError("Error: Authorization failed. Closing connection. Error was: " + ex.what());
									break;
								}
							}

							packetType = (binaryRpc.getType() == BaseLib::Rpc::BinaryRpc::Type::request) ? PacketType::Enum::binaryRequest : PacketType::Enum::binaryResponse;

							packetReceived(client, binaryRpc.getData(), packetType, true);
							binaryRpc.reset();
							if(client->socketDescriptor->descriptor == -1)
							{
								if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: Connection to client number " + std::to_string(client->socketDescriptor->id) + " closed.");
								break;
							}
						}
					}
				}
				catch(BaseLib::Rpc::BinaryRpcException& ex)
				{
					_out.printError("Error processing binary RPC packet. Closing connection. Error was: " + ex.what());
					binaryRpc.reset();
					break;
				}
				continue;
			}
			else if(!binaryRpc.processingStarted() && !http.headerProcessingStarted() && !webSocket.dataProcessingStarted())
			{
				if(!strncmp(buffer, "GET ", 4) || !strncmp(buffer, "HEAD ", 5))
				{
					buffer[bytesRead] = '\0';
					packetType = PacketType::Enum::xmlRequest;

					if(!_info->redirectTo.empty())
					{
						std::vector<char> data;
						std::vector<std::string> additionalHeaders({std::string("Location: ") + _info->redirectTo});
						_webServer->getError(301, "Moved Permanently", "The document has moved <a href=\"" + _info->redirectTo + "\">here</a>.", data, additionalHeaders);
						sendRPCResponseToClient(client, data, false);
						continue;
					}
					if(!_info->webServer)
					{
						std::vector<char> data;
						_webServer->getError(400, "Bad Request", "Your client sent a request that this server could not understand.", data);
						sendRPCResponseToClient(client, data, false);
						continue;
					}

					try
					{
						http.reset();
						http.process(buffer, bytesRead);
					}
					catch(BaseLib::HttpException& ex)
					{
						_out.printError("XML RPC Server: Could not process HTTP packet: " + ex.what() + " Buffer: " + std::string(buffer, bytesRead));
						std::vector<char> data;
						_webServer->getError(400, "Bad Request", "Your client sent a request that this server could not understand.", data);
						sendRPCResponseToClient(client, data, false);
					}
				}
				else if(!strncmp(buffer, "POST", 4) || !strncmp(buffer, "HTTP/1.", 7))
				{
					if(bytesRead < 8) continue;
					buffer[bytesRead] = '\0';
					packetType = (!strncmp(buffer, "POST", 4)) ? PacketType::Enum::xmlRequest : PacketType::Enum::xmlResponse;

					try
					{
						http.reset();
						http.process(buffer, bytesRead);
					}
					catch(BaseLib::HttpException& ex)
					{
						_out.printError("XML RPC Server: Could not process HTTP packet: " + ex.what() + " Buffer: " + std::string(buffer, bytesRead));
					}
				}
				else if(client->rpcType == BaseLib::RpcType::websocket)
				{
					packetType = PacketType::Enum::webSocketRequest;
					webSocket.reset();
					webSocket.process(buffer, bytesRead);
				}
			}
			else if(http.headerProcessingStarted() || webSocket.dataProcessingStarted())
			{
				buffer[bytesRead] = '\0';
				if(client->rpcType == BaseLib::RpcType::websocket) webSocket.process(buffer, bytesRead);
				else
				{
					try
					{
						http.process(buffer, bytesRead);
					}
					catch(BaseLib::HttpException& ex)
					{
						_out.printError("XML RPC Server: Could not process HTTP packet: " + ex.what() + " Buffer: " + std::string(buffer, bytesRead));
						http.reset();
						std::vector<char> data;
						_webServer->getError(400, "Bad Request", "Your client sent a request that the server couldn't understand..", data);
						sendRPCResponseToClient(client, data, false);
					}

					if(http.getContentSize() > 10485760)
					{
						http.reset();
						std::vector<char> data;
						_webServer->getError(400, "Bad Request", "Your client sent a request larger than 10 MiB.", data);
						sendRPCResponseToClient(client, data, false);
					}
				}
			}
			else
			{
				_out.printError("Error: Uninterpretable packet received. Closing connection. Packet was: " + std::string(buffer, bytesRead));
				break;
			}
			if(client->rpcType == BaseLib::RpcType::websocket && webSocket.isFinished())
			{
				if(webSocket.getHeader().close)
				{
					std::vector<char> response;
					webSocket.encode(webSocket.getContent(), BaseLib::WebSocket::Header::Opcode::close, response);
					sendRPCResponseToClient(client, response, false);
					closeClientConnection(client);
				}
				else if((_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::basic || _info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::session) && !client->webSocketAuthorized)
				{
					if(!client->auth.initialized()) client->auth = Auth(client->socket, _info->validUsers);
					try
					{
						if(_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::basic && !client->auth.basicServer(webSocket))
						{
							_out.printError("Error: Basic authentication failed for host " + client->address + ". Closing connection.");
							std::vector <char> output;
							BaseLib::WebSocket::encodeClose(output);
							sendRPCResponseToClient(client, output, false);
							break;
						}
						else if(_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::session && !client->auth.sessionServer(webSocket))
						{
							_out.printError("Error: Session authentication failed for host " + client->address + ". Closing connection.");
							std::vector <char> output;
							BaseLib::WebSocket::encodeClose(output);
							sendRPCResponseToClient(client, output, false);
							break;
						}
						else
						{
							client->webSocketAuthorized = true;
							if(_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::basic) _out.printInfo(std::string("Client ") + (client->webSocketClient ? "(direction browser => Homegear)" : "(direction Homegear => browser)") + " successfully authorized using basic authentication.");
							else if(_info->websocketAuthType == BaseLib::Rpc::ServerInfo::Info::AuthType::session) _out.printInfo(std::string("Client ") + (client->webSocketClient ? "(direction browser => Homegear)" : "(direction Homegear => browser)") + " successfully authorized using session authentication.");
							if(client->webSocketClient)
							{
								_out.printInfo("Info: Transferring client number " + std::to_string(client->id) + " to rpc client.");
								GD::rpcClient->addWebSocketServer(client->socket, client->webSocketClientId, client->address, client->nodeClient);
								client->socketDescriptor.reset(new BaseLib::FileDescriptor());
								client->socket.reset(new BaseLib::TcpSocket(GD::bl.get()));
								client->closed = true;
								break;
							}
						}
					}
					catch(AuthException& ex)
					{
						_out.printError("Error: Authorization failed for host " + http.getHeader().host + ". Closing connection. Error was: " + ex.what());
						break;
					}
				}
				else if(webSocket.getHeader().opcode == BaseLib::WebSocket::Header::Opcode::ping)
				{
					std::vector<char> response;
					webSocket.encode(webSocket.getContent(), BaseLib::WebSocket::Header::Opcode::pong, response);
					sendRPCResponseToClient(client, response, false);
				}
				else
				{
					packetReceived(client, webSocket.getContent(), packetType, true);
				}
				webSocket.reset();
			}
			else if(http.isFinished())
			{
				if(_info->webSocket && (http.getHeader().connection & BaseLib::Http::Connection::upgrade))
				{
					//Do this before basic auth, because currently basic auth is not supported by websockets. Authorization takes place after the upgrade.
					handleConnectionUpgrade(client, http);
					if(client->closed) break; //No auth and client transferred.
					http.reset();
					continue;
				}

				if(_info->authType == BaseLib::Rpc::ServerInfo::Info::AuthType::basic)
				{
					if(!client->auth.initialized()) client->auth = Auth(client->socket, _info->validUsers);
					try
					{
						if(!client->auth.basicServer(http))
						{
							_out.printError("Error: Authorization failed for host " + http.getHeader().host + ". Closing connection.");
							break;
						}
						else _out.printInfo("Info: Client successfully authorized using basic authentication.");
					}
					catch(AuthException& ex)
					{
						_out.printError("Error: Authorization failed for host " + http.getHeader().host + ". Closing connection. Error was: " + ex.what());
						break;
					}
				}
				if(_info->webServer && (!_info->xmlrpcServer || http.getHeader().method != "POST" || (!http.getHeader().contentType.empty() && http.getHeader().contentType != "text/xml")) && (!_info->jsonrpcServer || http.getHeader().method != "POST" || (!http.getHeader().contentType.empty() && http.getHeader().contentType != "application/json") || http.getHeader().path == "/flows/flows"))
				{

					http.getHeader().remoteAddress = client->address;
					http.getHeader().remotePort = client->port;
					if(http.getHeader().method == "POST") _webServer->post(http, client->socket);
					else if(http.getHeader().method == "GET" || http.getHeader().method == "HEAD") _webServer->get(http, client->socket);
				}
				else if(http.getContentSize() > 0 && (_info->xmlrpcServer || _info->jsonrpcServer))
				{
					if(http.getHeader().contentType == "application/json" || http.getContent().at(0) == '{') packetType = PacketType::jsonRequest;
					packetReceived(client, http.getContent(), packetType, http.getHeader().connection & BaseLib::Http::Connection::Enum::keepAlive);
				}
				http.reset();
				if(client->socketDescriptor->descriptor == -1)
				{
					if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: Connection to client number " + std::to_string(client->socketDescriptor->id) + " closed.");
					break;
				}
			}
		}
		if(client->rpcType == BaseLib::RpcType::websocket) //Send close packet
		{
			std::vector<char> payload;
			std::vector<char> response;
			BaseLib::WebSocket::encode(payload, BaseLib::WebSocket::Header::Opcode::close, response);
			sendRPCResponseToClient(client, response, false);
		}
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    //This point is only reached, when stopServer is true, the socket is closed or an error occured
	closeClientConnection(client);
}

std::shared_ptr<BaseLib::FileDescriptor> HttpServer::getClientSocketDescriptor(std::string& address, int32_t& port)
{
	std::shared_ptr<BaseLib::FileDescriptor> fileDescriptor;
	try
	{
		bool tooManyConnections = false;
		{
			//Don't lock _stateMutex => no synchronisation needed
			if(_clients.size() > GD::bl->settings.rpcServerMaxConnections())
			{
				collectGarbage();
				if(_clients.size() > GD::bl->settings.rpcServerMaxConnections())
				{
					_out.printError("Error: There are too many clients connected to me. Closing incoming connection. You can increase the number of allowed connections in main.conf.");
					tooManyConnections = true;
				}
			}
		}

		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
		fd_set readFileDescriptor;
		int32_t nfds = 0;
		FD_ZERO(&readFileDescriptor);
		{
			auto fileDescriptorGuard = GD::bl->fileDescriptorManager.getLock();
			fileDescriptorGuard.lock();
			nfds = _serverFileDescriptor->descriptor + 1;
			if(nfds <= 0)
			{
				fileDescriptorGuard.unlock();
				GD::out.printError("Error: Server file descriptor is invalid.");
				return fileDescriptor;
			}
			FD_SET(_serverFileDescriptor->descriptor, &readFileDescriptor);
		}
		if(!select(nfds, &readFileDescriptor, NULL, NULL, &timeout))
		{
			if(GD::bl->hf.getTime() - _lastGargabeCollection > 60000 || _clients.size() > GD::bl->settings.rpcServerMaxConnections() * 100 / 112) collectGarbage();
			return fileDescriptor;
		}

		struct sockaddr_storage clientInfo;
		socklen_t addressSize = sizeof(addressSize);
		fileDescriptor = GD::bl->fileDescriptorManager.add(accept(_serverFileDescriptor->descriptor, (struct sockaddr *) &clientInfo, &addressSize));
		if(!fileDescriptor) return fileDescriptor;
		if(tooManyConnections)
		{
			GD::bl->fileDescriptorManager.shutdown(fileDescriptor);
			return std::shared_ptr<BaseLib::FileDescriptor>();
		}

		getpeername(fileDescriptor->descriptor, (struct sockaddr*)&clientInfo, &addressSize);

		char ipString[INET6_ADDRSTRLEN];
		if (clientInfo.ss_family == AF_INET) {
			struct sockaddr_in *s = (struct sockaddr_in *)&clientInfo;
			port = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipString, sizeof(ipString));
		} else { // AF_INET6
			struct sockaddr_in6 *s = (struct sockaddr_in6 *)&clientInfo;
			port = ntohs(s->sin6_port);
			inet_ntop(AF_INET6, &s->sin6_addr, ipString, sizeof(ipString));
		}
		address = std::string(&ipString[0]);

		_out.printInfo("Info: Connection from " + address + ":" + std::to_string(port) + " accepted. Client number: " + std::to_string(fileDescriptor->id));
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return fileDescriptor;
}

void HttpServer::getSSLSocketDescriptor(std::shared_ptr<Client> client)
{
	try
	{
		if(!_tlsPriorityCache)
		{
			_out.printError("Error: Could not initiate TLS connection. _tlsPriorityCache is nullptr.");
			return;
		}
		if(!_x509Cred)
		{
			_out.printError("Error: Could not initiate TLS connection. _x509Cred is nullptr.");
			return;
		}
		int32_t result = 0;
		if((result = gnutls_init(&client->socketDescriptor->tlsSession, GNUTLS_SERVER)) != GNUTLS_E_SUCCESS)
		{
			_out.printError("Error: Could not initialize TLS session: " + std::string(gnutls_strerror(result)));
			client->socketDescriptor->tlsSession = nullptr;
			return;
		}
		if(!client->socketDescriptor->tlsSession)
		{
			_out.printError("Error: Client TLS session is nullptr.");
			return;
		}
		if((result = gnutls_priority_set(client->socketDescriptor->tlsSession, _tlsPriorityCache)) != GNUTLS_E_SUCCESS)
		{
			_out.printError("Error: Could not set cipher priority on TLS session: " + std::string(gnutls_strerror(result)));
			GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
			return;
		}
		if((result = gnutls_credentials_set(client->socketDescriptor->tlsSession, GNUTLS_CRD_CERTIFICATE, _x509Cred)) != GNUTLS_E_SUCCESS)
		{
			_out.printError("Error: Could not set x509 credentials on TLS session: " + std::string(gnutls_strerror(result)));
			GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
			return;
		}
		gnutls_certificate_server_set_request(client->socketDescriptor->tlsSession, GNUTLS_CERT_IGNORE);
		if(!client->socketDescriptor || client->socketDescriptor->descriptor == -1)
		{
			_out.printError("Error setting TLS socket descriptor: Provided socket descriptor is invalid.");
			GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
			return;
		}
		gnutls_transport_set_ptr(client->socketDescriptor->tlsSession, (gnutls_transport_ptr_t)(uintptr_t)client->socketDescriptor->descriptor);
		do
		{
			result = gnutls_handshake(client->socketDescriptor->tlsSession);
        } while (result < 0 && gnutls_error_is_fatal(result) == 0);
		if(result < 0)
		{
			_out.printWarning("Warning: TLS handshake has failed: " + std::string(gnutls_strerror(result)));
			GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
			return;
		}
		return;
	}
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    GD::bl->fileDescriptorManager.shutdown(client->socketDescriptor);
}

void HttpServer::getSocketDescriptor()
{
	try
	{
		if(_info->socketDescriptor)
		{
			_serverFileDescriptor = _info->socketDescriptor;
			return;
		}

		addrinfo hostInfo;
		addrinfo *serverInfo = nullptr;

		int32_t yes = 1;

		memset(&hostInfo, 0, sizeof(hostInfo));

		hostInfo.ai_family = AF_UNSPEC;
		hostInfo.ai_socktype = SOCK_STREAM;
		hostInfo.ai_flags = AI_PASSIVE;
		char buffer[100];
		std::string port = std::to_string(_info->port);
		int32_t result;
		if((result = getaddrinfo(_info->interface.c_str(), port.c_str(), &hostInfo, &serverInfo)) != 0)
		{
			_out.printCritical("Error: Could not get address information: " + std::string(gai_strerror(result)));
			return;
		}

		bool bound = false;
		int32_t error = 0;
		for(struct addrinfo *info = serverInfo; info != 0; info = info->ai_next)
		{
			_serverFileDescriptor = GD::bl->fileDescriptorManager.add(socket(info->ai_family, info->ai_socktype, info->ai_protocol));
			if(_serverFileDescriptor->descriptor == -1) continue;
			if(!(fcntl(_serverFileDescriptor->descriptor, F_GETFL) & O_NONBLOCK))
			{
				if(fcntl(_serverFileDescriptor->descriptor, F_SETFL, fcntl(_serverFileDescriptor->descriptor, F_GETFL) | O_NONBLOCK) < 0) throw BaseLib::Exception("Error: Could not set socket options.");
			}
			if(setsockopt(_serverFileDescriptor->descriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int32_t)) == -1) throw BaseLib::Exception("Error: Could not set socket options.");
			if(bind(_serverFileDescriptor->descriptor, info->ai_addr, info->ai_addrlen) == -1)
			{
				error = errno;
				continue;
			}
			switch (info->ai_family)
			{
				case AF_INET:
					inet_ntop (info->ai_family, &((struct sockaddr_in *) info->ai_addr)->sin_addr, buffer, 100);
					_info->address = std::string(buffer);
					break;
				case AF_INET6:
					inet_ntop (info->ai_family, &((struct sockaddr_in6 *) info->ai_addr)->sin6_addr, buffer, 100);
					_info->address = std::string(buffer);
					break;
			}
			_out.printInfo("Info: RPC Server started listening on address " + _info->address + " and port " + port);
			bound = true;
			break;
		}
		freeaddrinfo(serverInfo);
		if(!bound)
		{
			GD::bl->fileDescriptorManager.shutdown(_serverFileDescriptor);
			_out.printCritical("Error: Server could not start listening on port " + port + ": " + std::string(strerror(error)));
			return;
		}
		if(_serverFileDescriptor->descriptor == -1 || !bound || listen(_serverFileDescriptor->descriptor, _backlog) == -1)
		{
			GD::bl->fileDescriptorManager.shutdown(_serverFileDescriptor);
			_out.printCritical("Error: Server could not start listening on port " + port + ": " + std::string(strerror(errno)));
			return;
		}
		if(_info->address == "0.0.0.0" || _info->address == "::") _info->address = BaseLib::Net::getMyIpAddress();
    }
    catch(const std::exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

}