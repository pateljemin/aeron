/*
 * Copyright 2014 - 2017 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <functional>

#include <gtest/gtest.h>
#include <arpa/inet.h>

extern "C"
{
#include "uri/aeron_uri.h"
#include "util/aeron_netutil.h"
#include "util/aeron_error.h"
}

class UriTest : public testing::Test
{
protected:
    aeron_uri_t m_uri;
};

TEST_F(UriTest, shouldNotParseInvalidUriScheme)
{
    EXPECT_EQ(aeron_uri_parse("aaron", &m_uri), -1);
    EXPECT_EQ(aeron_uri_parse("aeron:", &m_uri), -1);
    EXPECT_EQ(aeron_uri_parse("aron:", &m_uri), -1);
    EXPECT_EQ(aeron_uri_parse(":aeron", &m_uri), -1);
}

TEST_F(UriTest, shouldNotParseUnknownUriTransport)
{
    EXPECT_EQ(aeron_uri_parse("aeron:tcp", &m_uri), -1);
    EXPECT_EQ(aeron_uri_parse("aeron:sctp", &m_uri), -1);
    EXPECT_EQ(aeron_uri_parse("aeron:udp", &m_uri), -1);
}

TEST_F(UriTest, shouldParseKnownUriTransportWithoutParams)
{
    EXPECT_EQ(aeron_uri_parse("aeron:ipc", &m_uri), 0);
    ASSERT_EQ(m_uri.type, AERON_URI_IPC);
    EXPECT_EQ(m_uri.params.ipc.additional_params.length, 0u);

    EXPECT_EQ(aeron_uri_parse("aeron:udp?", &m_uri), 0);
    EXPECT_EQ(m_uri.type, AERON_URI_UDP);

    EXPECT_EQ(aeron_uri_parse("aeron:ipc?", &m_uri), 0);
    EXPECT_EQ(m_uri.type, AERON_URI_IPC);
    EXPECT_EQ(m_uri.params.ipc.additional_params.length, 0u);
}

TEST_F(UriTest, shouldParseWithSingleParam)
{
    EXPECT_EQ(aeron_uri_parse("aeron:udp?endpoint=224.10.9.8", &m_uri), 0);
    ASSERT_EQ(m_uri.type, AERON_URI_UDP);
    EXPECT_EQ(std::string(m_uri.params.udp.endpoint_key), "224.10.9.8");
    EXPECT_EQ(m_uri.params.udp.additional_params.length, 0u);

    EXPECT_EQ(aeron_uri_parse("aeron:udp?add|ress=224.10.9.8", &m_uri), 0);
    ASSERT_EQ(m_uri.type, AERON_URI_UDP);
    ASSERT_EQ(m_uri.params.udp.additional_params.length, 1u);
    EXPECT_EQ(std::string(m_uri.params.udp.additional_params.array[0].key), "add|ress");
    EXPECT_EQ(std::string(m_uri.params.udp.additional_params.array[0].value), "224.10.9.8");

    EXPECT_EQ(aeron_uri_parse("aeron:udp?endpoint=224.1=0.9.8", &m_uri), 0);
    ASSERT_EQ(m_uri.type, AERON_URI_UDP);
    EXPECT_EQ(std::string(m_uri.params.udp.endpoint_key), "224.1=0.9.8");
    EXPECT_EQ(m_uri.params.udp.additional_params.length, 0u);
}

TEST_F(UriTest, shouldParseWithMultipleParams)
{
    EXPECT_EQ(aeron_uri_parse("aeron:udp?endpoint=224.10.9.8|port=4567|interface=192.168.0.3|ttl=16", &m_uri), 0);
    ASSERT_EQ(m_uri.type, AERON_URI_UDP);
    EXPECT_EQ(std::string(m_uri.params.udp.endpoint_key), "224.10.9.8");
    EXPECT_EQ(std::string(m_uri.params.udp.interface_key), "192.168.0.3");
    EXPECT_EQ(std::string(m_uri.params.udp.ttl_key), "16");
    EXPECT_EQ(m_uri.params.udp.additional_params.length, 1u);
    EXPECT_EQ(std::string(m_uri.params.udp.additional_params.array[0].key), "port");
    EXPECT_EQ(std::string(m_uri.params.udp.additional_params.array[0].value), "4567");
}

/*
 * WARNING: single threaded only due to global resolver func usage
 */

class UriResolverTest : public testing::Test
{
public:
    UriResolverTest() :
        addr_in((struct sockaddr_in *)&m_addr),
        addr_in6((struct sockaddr_in6 *)&m_addr),
        m_prefixlen(0),
        m_resolver_func([](const char *, struct addrinfo *, struct addrinfo **){ return -1; })
    {
        aeron_uri_hostname_resolver(UriResolverTest::resolver_func, this);
    }

    static int resolver_func(void *clientd, const char *host, struct addrinfo *hints, struct addrinfo **info)
    {
        UriResolverTest *t = (UriResolverTest *)clientd;

        return (*t).m_resolver_func(host, hints, info);
    }

protected:
    aeron_uri_t m_uri;
    struct sockaddr_storage m_addr;
    struct sockaddr_in *addr_in;
    struct sockaddr_in6 *addr_in6;
    size_t m_prefixlen;
    std::function<int(const char *, struct addrinfo *, struct addrinfo **)> m_resolver_func;
};

TEST_F(UriResolverTest, shouldResolveIpv4DottedDecimalAndPort)
{
    char buffer[AERON_MAX_PATH];

    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("127.0.0.1:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("192.168.1.20:55", &m_addr), 0) << aeron_errmsg();
    EXPECT_EQ(m_addr.ss_family, AF_INET);
    EXPECT_EQ(addr_in->sin_family, AF_INET);
    EXPECT_STREQ(inet_ntop(AF_INET, &addr_in->sin_addr, buffer, sizeof(buffer)), "192.168.1.20");
    EXPECT_EQ(addr_in->sin_port, htons(55));
}

TEST_F(UriResolverTest, shouldResolveIpv4MulticastDottedDecimalAndPort)
{
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("223.255.255.255:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("224.0.0.0:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("239.255.255.255:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("240.0.0.0:1234", &m_addr), 0) << aeron_errmsg();
}

TEST_F(UriResolverTest, shouldResolveIpv6AndPort)
{
    char buffer[AERON_MAX_PATH];

    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("[::1]:1234", &m_addr), 0) << aeron_errmsg();
    EXPECT_EQ(m_addr.ss_family, AF_INET6);
    EXPECT_EQ(addr_in6->sin6_family, AF_INET6);
    EXPECT_STREQ(inet_ntop(AF_INET6, &addr_in6->sin6_addr, buffer, sizeof(buffer)), "::1");
    EXPECT_EQ(addr_in->sin_port, htons(1234));

    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("[::1%eth0]:1234", &m_addr), 0) << aeron_errmsg();
    EXPECT_EQ(m_addr.ss_family, AF_INET6);
    EXPECT_EQ(addr_in6->sin6_family, AF_INET6);
    EXPECT_STREQ(inet_ntop(AF_INET6, &addr_in6->sin6_addr, buffer, sizeof(buffer)), "::1");
    EXPECT_EQ(addr_in->sin_port, htons(1234));

    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("[::1%12~_.-34]:1234", &m_addr), 0) << aeron_errmsg();
}

TEST_F(UriResolverTest, shouldResolveIpv6MulticastAndPort)
{
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve(
        "[FEFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF]:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("[FF00::]:1234", &m_addr), 0) << aeron_errmsg();
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve(
        "[FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF]:1234", &m_addr), 0) << aeron_errmsg();
}

TEST_F(UriResolverTest, shouldResolveLocalhost)
{
    ASSERT_EQ(aeron_host_and_port_parse_and_resolve("localhost:1234", &m_addr), 0) << aeron_errmsg();
}

TEST_F(UriResolverTest, shouldNotResolveInvalidPort)
{
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("192.168.1.20:aa", &m_addr), -1);
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("192.168.1.20", &m_addr), -1);
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("192.168.1.20:", &m_addr), -1);
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("[::1]:aa", &m_addr), -1);
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("[::1]", &m_addr), -1);
    EXPECT_EQ(aeron_host_and_port_parse_and_resolve("[::1]:", &m_addr), -1);
}

TEST_F(UriResolverTest, shouldResolveIpv4Interface)
{
    char buffer[AERON_MAX_PATH];

    ASSERT_EQ(aeron_interface_parse_and_resolve("192.168.1.20", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 32u);
    EXPECT_EQ(m_addr.ss_family, AF_INET);
    EXPECT_EQ(addr_in->sin_family, AF_INET);
    EXPECT_STREQ(inet_ntop(AF_INET, &addr_in->sin_addr, buffer, sizeof(buffer)), "192.168.1.20");

    ASSERT_EQ(aeron_interface_parse_and_resolve("192.168.1.20/24", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 24u);

    ASSERT_EQ(aeron_interface_parse_and_resolve("192.168.1.20:1234", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 32u);

    ASSERT_EQ(aeron_interface_parse_and_resolve("192.168.1.20:1234/24", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 24u);

    ASSERT_EQ(aeron_interface_parse_and_resolve("0.0.0.0/0", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 0u);
    EXPECT_EQ(m_addr.ss_family, AF_INET);
    EXPECT_EQ(addr_in->sin_family, AF_INET);
    EXPECT_STREQ(inet_ntop(AF_INET, &addr_in->sin_addr, buffer, sizeof(buffer)), "0.0.0.0");
}

TEST_F(UriResolverTest, shouldResolveIpv6Interface)
{
    char buffer[AERON_MAX_PATH];

    ASSERT_EQ(aeron_interface_parse_and_resolve("[::1]", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 128u);
    EXPECT_EQ(m_addr.ss_family, AF_INET6);
    EXPECT_EQ(addr_in->sin_family, AF_INET6);
    EXPECT_STREQ(inet_ntop(AF_INET6, &addr_in6->sin6_addr, buffer, sizeof(buffer)), "::1");

    ASSERT_EQ(aeron_interface_parse_and_resolve("[::1]/48", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 48u);

    ASSERT_EQ(aeron_interface_parse_and_resolve("[::1]:1234", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 128u);

    ASSERT_EQ(aeron_interface_parse_and_resolve("[::1]:1234/48", &m_addr, &m_prefixlen), 0) << aeron_errmsg();
    EXPECT_EQ(m_prefixlen, 48u);
}