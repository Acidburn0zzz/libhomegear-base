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

#include "Net.h"
#include "HelperFunctions.h"

#include <asm/types.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <iostream>

namespace BaseLib
{
int32_t Net::readNlSocket(int32_t sockFd, std::vector<char>& buffer, uint32_t messageIndex, uint32_t pid)
{
	struct nlmsghdr* nlHeader = nullptr;
	int32_t readLength = 0;
	uint32_t messageLength = 0;
	do
	{
		if(messageLength >= buffer.size())
		{
			buffer.resize(buffer.size() + messageLength + 8192, 0);
		}
		if((readLength = recv(sockFd, buffer.data() + messageLength, buffer.size() - messageLength, 0)) < 0) throw NetException("Read from socket failed: " + std::string(strerror(errno)));
		nlHeader = (struct nlmsghdr *)(buffer.data() + messageLength);

		if((0 == NLMSG_OK(nlHeader, (uint32_t)readLength)) || (NLMSG_ERROR == nlHeader->nlmsg_type)) throw NetException("Error in received packet: " + std::string(strerror(errno)));
		if (NLMSG_DONE == nlHeader->nlmsg_type) break;

		messageLength += readLength;

		if ((nlHeader->nlmsg_flags & NLM_F_MULTI) == 0) break;
	} while((nlHeader->nlmsg_seq != messageIndex) || (nlHeader->nlmsg_pid != pid));
	return messageLength;
}

bool Net::isIp(std::string ipAddress)
{
	struct sockaddr_in sa;
	struct sockaddr_in6 sa6;
	if(inet_pton(AF_INET, ipAddress.c_str(), &(sa.sin_addr)) != 1 && inet_pton(AF_INET6, ipAddress.c_str(), &(sa6.sin6_addr)) != 1) return false;
	return true;
}

std::string Net::resolveHostname(std::string& hostname)
{
	struct addrinfo* serverInfo = nullptr;
	struct addrinfo hostInfo;
	memset(&hostInfo, 0, sizeof hostInfo);

	hostInfo.ai_family = AF_UNSPEC;
	hostInfo.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(hostname.c_str(), nullptr, &hostInfo, &serverInfo) != 0)
	{
		freeaddrinfo(serverInfo);
		throw NetException("Could not get address information: " + std::string(strerror(errno)));
	}

	char ipStringBuffer[INET6_ADDRSTRLEN];
	if (serverInfo->ai_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)serverInfo->ai_addr;
		inet_ntop(AF_INET, &s->sin_addr, ipStringBuffer, sizeof(ipStringBuffer));
	} else { // AF_INET6
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)serverInfo->ai_addr;
		inet_ntop(AF_INET6, &s->sin6_addr, ipStringBuffer, sizeof(ipStringBuffer));
	}
	std::string ipAddress(&ipStringBuffer[0]);
	freeaddrinfo(serverInfo);
	return ipAddress;
}

Net::RouteInfoList Net::getRoutes()
{
	RouteInfoList routeInfo;
	struct nlmsghdr* nlMessage = nullptr;
	std::shared_ptr<RouteInfo> info;
	uint32_t messageIndex = 0;
	int32_t nlBufferLength = 8192;
	int32_t socketDescriptor = 0;
	int32_t length = 0;
	std::vector<char> nlBuffer(nlBufferLength, 0);

	if((socketDescriptor = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0) throw NetException("Could not create socket: " + std::string(strerror(errno)));

	nlMessage = (struct nlmsghdr*)(nlBuffer.data());

	nlMessage->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	nlMessage->nlmsg_type = RTM_GETROUTE;

	nlMessage->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
	nlMessage->nlmsg_seq = messageIndex++;
	nlMessage->nlmsg_pid = getpid();

	if(send(socketDescriptor, nlMessage, nlMessage->nlmsg_len, 0) < 0)
	{
		close(socketDescriptor);
		throw NetException("Write to socket failed: " + std::string(strerror(errno)));
	}
	nlMessage = nullptr; //readNlSocket might change buffer address

	length = readNlSocket(socketDescriptor, nlBuffer, messageIndex, getpid());
	if(length < 0)
	{
		close(socketDescriptor);
		throw NetException("Read from socket failed: " + std::string(strerror(errno)));
	}
	nlMessage = (struct nlmsghdr*)(nlBuffer.data());

	struct rtmsg* routeMessage = nullptr;
    struct rtattr* routeAttribute = nullptr;
    int32_t routeLength = 0;
    char interfaceNameBuffer[IF_NAMESIZE + 1];
	for(; NLMSG_OK(nlMessage, (uint32_t)length); nlMessage = NLMSG_NEXT(nlMessage, length))
	{
		info.reset(new RouteInfo());
        routeMessage = (struct rtmsg *)NLMSG_DATA(nlMessage);
		if((routeMessage->rtm_family != AF_INET) || (routeMessage->rtm_table != RT_TABLE_MAIN)) continue;

		routeAttribute = (struct rtattr *)RTM_RTA(routeMessage);
		routeLength = RTM_PAYLOAD(nlMessage);

		for (; RTA_OK(routeAttribute, routeLength); routeAttribute = RTA_NEXT(routeAttribute, routeLength))
		{
			switch(routeAttribute->rta_type)
			{
				case RTA_OIF:
					if(if_indextoname(*(uint32_t*)RTA_DATA(routeAttribute), interfaceNameBuffer))
					{
						interfaceNameBuffer[IF_NAMESIZE] = 0;
						info->interfaceName = std::string(interfaceNameBuffer);
					}
					break;
				case RTA_GATEWAY:
					info->gateway = *(uint32_t*)RTA_DATA(routeAttribute);
					break;
				case RTA_PREFSRC:
					info->sourceAddress = *(uint32_t*)RTA_DATA(routeAttribute);
					break;
				case RTA_DST:
					info->destinationAddress = *(uint32_t*)RTA_DATA(routeAttribute);
					break;
			}
        }

		routeInfo.push_back(info);
	}
	close(socketDescriptor);
	return routeInfo;
}

std::string Net::getMyIpAddress(std::string interfaceName)
{
	std::string address;

	if(interfaceName.empty())
	{
		RouteInfoList list = getRoutes();
		for(RouteInfoList::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			if((*i)->destinationAddress == 0)
			{
				address = std::string(std::to_string((*i)->sourceAddress & 0xFF) + '.' + std::to_string(((*i)->sourceAddress >> 8) & 0xFF) + '.' + std::to_string(((*i)->sourceAddress >> 16) & 0xFF) + '.' + std::to_string((*i)->sourceAddress >> 24));
				if(address.compare(0, 3, "10.") == 0 || address.compare(0, 4, "172.") == 0 || address.compare(0, 8, "192.168.") == 0) break;
				else address.clear();
			}
		}
	}

	if(address.empty()) //Alternative method
	{
		char buffer[101];
		buffer[100] = 0;
		bool addressFound = false;
		ifaddrs* interfaces = nullptr;
		if(getifaddrs(&interfaces) != 0) throw NetException("Could not get address information: " + std::string(strerror(errno)));
		for(ifaddrs* info = interfaces; info != 0; info = info->ifa_next)
		{
			if (info->ifa_addr == NULL) continue;
			switch (info->ifa_addr->sa_family)
			{
				case AF_INET:
					inet_ntop (info->ifa_addr->sa_family, &((struct sockaddr_in *)info->ifa_addr)->sin_addr, buffer, 100);
					address = std::string(buffer);
					if(!interfaceName.empty())
					{
						std::string currentInterfaceName(info->ifa_name);
						if(currentInterfaceName == interfaceName) addressFound = true;
					}
					else if(address.compare(0, 3, "10.") == 0 || address.compare(0, 4, "172.") == 0 || address.compare(0, 8, "192.168.") == 0) addressFound = true;
					break;
			}
			if(addressFound) break;
		}
		freeifaddrs(interfaces);
		if(!addressFound) throw NetException("No IP address could be found.");
	}
	return address;
}

std::string Net::getMyIp6Address(std::string interfaceName)
{
	std::string address;

	if(interfaceName.empty())
	{
		RouteInfoList list = getRoutes();
		for(RouteInfoList::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			if((*i)->destinationAddress == 0)
			{
				//TODO: Implement
			}
		}
	}

	if(address.empty()) //Alternative method
	{
		char buffer[101];
		buffer[100] = 0;
		bool addressFound = false;
		ifaddrs* interfaces = nullptr;
		if(getifaddrs(&interfaces) != 0) throw NetException("Could not get address information: " + std::string(strerror(errno)));
		for(ifaddrs* info = interfaces; info != 0; info = info->ifa_next)
		{
			if (info->ifa_addr == NULL) continue;
			switch (info->ifa_addr->sa_family)
			{
				case AF_INET6:
					inet_ntop (info->ifa_addr->sa_family, &((struct sockaddr_in6 *)info->ifa_addr)->sin6_addr, buffer, 100);
					address = std::string(buffer);
					if(!interfaceName.empty())
					{
						std::string currentInterfaceName(info->ifa_name);
						if(currentInterfaceName == interfaceName) addressFound = true;
					}
					else if(address.compare(0, 3, "::1") != 0 && address.compare(0, 4, "fe80") != 0) addressFound = true;
					break;
			}
			if(addressFound) break;
		}
		freeifaddrs(interfaces);
		if(!addressFound) return getMyIpAddress(interfaceName);
	}
	return address;
}

}
