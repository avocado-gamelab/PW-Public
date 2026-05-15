#include "stdafx.h"
#include "ServerAddressList.h"
#include "Network/RUDP/RdpProto.h"
#include "Client/NetworkStatusOverlay.h"

const char* const ServerAddressList::kParamsJsonKeyAddresses = "serverAddresses";
const char* const ServerAddressList::kParamsJsonKeyMain      = "main";
const char* const ServerAddressList::kParamsJsonKeyProxy     = "proxy";

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

namespace
{

const int kProbeCount     = 3;
const int kProbeTimeoutMs = 500;
const int kAddrBufSize    = 256;
const int kRecvBufSize    = 128;
// Размер probe-датаграммы — Header (8 байт) + padding до ~1200 байт. Это не fragments
// и больше типичного NAT MTU (~576), но меньше Ethernet (1500) — то есть проба реально
// проверяет, что путь до endpoint'а держит full-size UDP, как настоящий login handshake.
// Маленькие RDP probe (~8 байт) проходили даже там, где CONNECT-датаграмма дропается
// по MTU/Path-MTU-Discovery/firewall-фильтру по размеру, давая false positive выбор
// non-handshake-able endpoint'а. Сервер payload HandshakeInit игнорирует
// (RdpLogic::ParsePacket: для HandshakeInit data не сохраняется), так что лишние
// байты безопасны.
const int kProbeSize      = 1200;
// Пакетный burst — вместо одиночной датаграммы за раунд шлём несколько подряд,
// чтобы ловить rate-limit/stateful-firewall, который пропускает первые N пакетов
// и дропает остальные. Наблюдаемый реальный сценарий (Prime World.zip лог 15:00):
// одиночный probe=3/3 ok, реальный login dg_sent=18 dg_recv=1 loss=100%.
// 5 пакетов × 3 раунда = 15 probe на endpoint — матчит реальный login burst.
const int kBurstPerRound  = 5;
// Endpoint считается "responsive" только если >= 60% пакетов дошли. Меньше —
// сеть режет поток, и реальный login с 15+ пакетами почти наверняка не пройдёт.
const int kMinReplyRatioPct = 60;


bool ResolveHost( const char* host, int port, sockaddr_in & out )
{
  struct addrinfo hints;
  memset( &hints, 0, sizeof(hints) );
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  char portStr[16];
  _snprintf( portStr, sizeof(portStr), "%d", port );

  struct addrinfo* res = NULL;
  if ( getaddrinfo( host, portStr, &hints, &res ) != 0 || res == NULL )
    return false;

  memcpy( &out, res->ai_addr, sizeof(sockaddr_in) );
  freeaddrinfo( res );
  return true;
}

// Отправляет burst из kBurstPerRound HandshakeInit параллельно всем endpoints и ждёт
// ответов общим select() до timeoutMs. Валидирует source address и тип ответного пакета.
// Заполняет rttMs[i] временем первого ответа (-1 если не было) и replyCount[i] —
// сколько пакетов из burst дошло обратно (0..kBurstPerRound). Полуоткрытые handshake
// на стороне сервера протухают сами.
void ProbeEndpointsRound( const sockaddr_in* addrs, int count, int timeoutMs, int* rttMs, int* replyCount )
{
  SOCKET socks[ServerAddressList::kMaxEntries];
  DWORD  sentAt[ServerAddressList::kMaxEntries];
  for ( int i = 0; i < ServerAddressList::kMaxEntries; ++i )
  {
    socks[i] = INVALID_SOCKET;
  }

  NI_VERIFY( count <= ServerAddressList::kMaxEntries, "too many probe endpoints", count = ServerAddressList::kMaxEntries; );

  int numOpened = 0;
  for ( int i = 0; i < count; ++i )
  {
    rttMs[i]      = -1;
    replyCount[i] = 0;

    socks[i] = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( socks[i] == INVALID_SOCKET )
    {
      WarningTrace( "RDP probe: socket() failed for endpoint %d, err=%d", i, WSAGetLastError() );
      continue;
    }

    u_long nonblocking = 1;
    ioctlsocket( socks[i], FIONBIO, &nonblocking );

    // connect() на UDP-сокете фильтрует recvfrom по source address — любой трафик
    // с другого хоста не будет принят. Исключает spoof-probe-ответы.
    if ( connect( socks[i], (const sockaddr*)&addrs[i], sizeof(sockaddr_in) ) != 0 )
    {
      int err = WSAGetLastError();
      if ( err != WSAEWOULDBLOCK )
      {
        WarningTrace( "RDP probe: connect() failed for endpoint %d, err=%d", i, err );
        closesocket( socks[i] );
        socks[i] = INVALID_SOCKET;
        continue;
      }
    }

    // Burst из kBurstPerRound пакетов. Каждый с собственным srcMux — сервер
    // трактует их как независимые probe, отвечает на каждый (полуоткрытые handshake
    // протухают отдельно). Это ловит selective drop/rate-limit.
    NI_STATIC_ASSERT( kProbeSize >= (int)sizeof(ni_udp::proto::Header), probe_size_too_small );
    char probeBuf[kProbeSize];
    sentAt[i] = GetTickCount();
    int sentTotal = 0;
    for ( int b = 0; b < kBurstPerRound; ++b )
    {
      unsigned short srcMux = (unsigned short)( ( GetTickCount() & 0xFFFE ) | 1 )
                            ^ (unsigned short)( ( i * kBurstPerRound + b ) * 2654435761u );
      srcMux |= 1; // keep odd
      ni_udp::proto::Header hdr(
        ni_udp::proto::EPktType::HandshakeInit,
        srcMux,
        (unsigned short)ServerAddressList::kLoginMux,
        /*seq*/ 0 );
      memcpy( probeBuf, &hdr, sizeof(hdr) );
      int sent = send( socks[i], probeBuf, kProbeSize, 0 );
      if ( sent < 0 )
      {
        int err = WSAGetLastError();
        if ( err != WSAEWOULDBLOCK )
        {
          WarningTrace( "RDP probe: send() failed for endpoint %d burst %d, err=%d", i, b, err );
          break;
        }
      }
      ++sentTotal;
    }

    if ( sentTotal == 0 )
    {
      closesocket( socks[i] );
      socks[i] = INVALID_SOCKET;
      continue;
    }
    ++numOpened;
  }

  const DWORD deadline = GetTickCount() + (DWORD)timeoutMs;

  while ( true )
  {
    const DWORD now = GetTickCount();
    if ( now >= deadline ) break;

    fd_set readSet;
    FD_ZERO( &readSet );
    SOCKET maxFd = 0;
    int waitingCount = 0;
    for ( int i = 0; i < count; ++i )
    {
      if ( socks[i] != INVALID_SOCKET && replyCount[i] < kBurstPerRound )
      {
        FD_SET( socks[i], &readSet );
        if ( socks[i] > maxFd ) maxFd = socks[i];
        ++waitingCount;
      }
    }
    if ( waitingCount == 0 ) break;

    const DWORD remaining = deadline - now;
    timeval tv;
    tv.tv_sec  = remaining / 1000;
    tv.tv_usec = ( remaining % 1000 ) * 1000;

    int sel = select( (int)maxFd + 1, &readSet, NULL, NULL, &tv );
    if ( sel <= 0 ) break;

    for ( int i = 0; i < count; ++i )
    {
      if ( socks[i] == INVALID_SOCKET ) continue;
      if ( !FD_ISSET( socks[i], &readSet ) ) continue;

      // Сосём все доступные датаграммы non-blocking — сервер может ответить
      // на несколько probe подряд, важно не пропустить count.
      while ( true )
      {
        char buf[kRecvBufSize];
        int n = recv( socks[i], buf, sizeof(buf), 0 );
        if ( n < 0 )
        {
          // WSAEWOULDBLOCK — больше нет датаграмм, выходим в select снова.
          break;
        }
        if ( n < (int)sizeof(ni_udp::proto::Header) ) continue;
        const ni_udp::proto::Header* resp = (const ni_udp::proto::Header*)buf;
        const unsigned t = resp->type;
        if ( t != ni_udp::proto::EPktType::HandshakeInitAck &&
             t != ni_udp::proto::EPktType::HandshakeRefused &&
             t != ni_udp::proto::EPktType::RetryHandshake )
          continue;

        if ( replyCount[i] == 0 )
          rttMs[i] = (int)( GetTickCount() - sentAt[i] );
        ++replyCount[i];
      }
    }
  }

  for ( int i = 0; i < count; ++i )
    if ( socks[i] != INVALID_SOCKET ) closesocket( socks[i] );
}

} // namespace


void ServerAddressList::Add( const char* name, const char* bareHost )
{
  if ( !bareHost || bareHost[0] == '\0' )
  {
    WarningTrace( "ServerAddressList::Add: empty host for '%s', entry skipped", name ? name : "?" );
    return;
  }
  if ( (int)entries.size() >= kMaxEntries )
  {
    WarningTrace( "ServerAddressList::Add: max %d entries reached, '%s' skipped", kMaxEntries, name );
    return;
  }
  Entry e;
  e.name = name;
  e.host = bareHost;
  e.responsive = false;
  entries.push_back( e );
}


const char* ServerAddressList::GetEntryAddress( int index ) const
{
  // Thread-local buffer per index. Вызовы на одном потоке (Game.cpp + GameContext::Init).
  static __declspec(thread) char buf[kMaxEntries][kAddrBufSize];
  if ( index < 0 || index >= entries.size() || index >= kMaxEntries )
    return "";

  _snprintf( buf[index], kAddrBufSize, "%s:%d@%d",
    entries[index].host.c_str(), kGamePort, kLoginMux );
  buf[index][kAddrBufSize - 1] = 0;
  return buf[index];
}


const char* ServerAddressList::GetResponsiveAlternativeAddress() const
{
  if ( entries.size() <= 1 ) return "";
  for ( int step = 1; step < entries.size(); ++step )
  {
    int alt = ( currentIndex + step ) % entries.size();
    if ( entries[alt].responsive )
      return GetEntryAddress( alt );
  }
  return "";
}


int ServerAddressList::ChooseBestByPing()
{
  if ( entries.size() < 2 )
    return -1;

  // Резолв DNS один раз перед probe-раундами.
  const int N = entries.size();
  sockaddr_in addrs[kMaxEntries];
  bool        resolved[kMaxEntries];
  for ( int i = 0; i < N; ++i )
  {
    resolved[i] = ResolveHost( entries[i].host.c_str(), kGamePort, addrs[i] );
    if ( !resolved[i] )
      WarningTrace( "RDP probe: DNS resolution failed for %s", entries[i].host.c_str() );
  }

  int totalRtt   [kMaxEntries] = {0};
  int okRounds   [kMaxEntries] = {0};
  int totalReply [kMaxEntries] = {0};

  for ( int round = 0; round < kProbeCount; ++round )
  {
    sockaddr_in batchAddrs[kMaxEntries];
    int         batchIdx  [kMaxEntries];
    int         batchN = 0;
    for ( int i = 0; i < N; ++i )
    {
      if ( resolved[i] )
      {
        batchAddrs[batchN] = addrs[i];
        batchIdx  [batchN] = i;
        ++batchN;
      }
    }

    int rtts   [kMaxEntries];
    int replies[kMaxEntries];
    ProbeEndpointsRound( batchAddrs, batchN, kProbeTimeoutMs, rtts, replies );

    for ( int b = 0; b < batchN; ++b )
    {
      int i = batchIdx[b];
      MessageTrace( "RDP probe %s try %d: rtt=%d ms, replies=%d/%d",
        entries[i].host.c_str(), round + 1, rtts[b], replies[b], kBurstPerRound );
      if ( rtts[b] >= 0 )
      {
        totalRtt[i] += rtts[b];
        okRounds[i] += 1;
      }
      totalReply[i] += replies[b];
    }
  }

  const int totalSentPerEndpoint = kProbeCount * kBurstPerRound;
  const int minReply = ( totalSentPerEndpoint * kMinReplyRatioPct + 99 ) / 100;

  int best = -1;
  int bestAvg = 0x7FFFFFFF;

  for ( int i = 0; i < N; ++i )
  {
    const bool healthy = ( totalReply[i] >= minReply );
    entries[i].responsive = healthy;

    if ( okRounds[i] == 0 )
    {
      WarningTrace( "RDP probe %s: no response (all %d tries timed out)",
        entries[i].host.c_str(), kProbeCount );
      NMainLoop::NetworkStatusOverlay::Instance().Log(
        "Probe %s: NO RESPONSE", entries[i].name.c_str() );
      continue;
    }

    int avg = totalRtt[i] / okRounds[i];
    MessageTrace( "RDP probe %s: avg=%d ms (%d/%d rounds ok, %d/%d replies, healthy=%d)",
      entries[i].host.c_str(), avg, okRounds[i], kProbeCount,
      totalReply[i], totalSentPerEndpoint, healthy ? 1 : 0 );
    NMainLoop::NetworkStatusOverlay::Instance().Log(
      "Probe %s: %d ms, %d/%d replies%s",
      entries[i].name.c_str(), avg, totalReply[i], totalSentPerEndpoint,
      healthy ? "" : " (LOSSY)" );

    if ( !healthy )
    {
      WarningTrace( "RDP probe %s: dropping from selection — burst loss %d%% exceeds threshold (replies %d/%d)",
        entries[i].host.c_str(),
        100 - ( totalReply[i] * 100 / totalSentPerEndpoint ),
        totalReply[i], totalSentPerEndpoint );
      continue;
    }

    if ( avg < bestAvg )
    {
      bestAvg = avg;
      best    = i;
    }
  }

  return best;
}
