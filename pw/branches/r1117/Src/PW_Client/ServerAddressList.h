#pragma once

#include "System/nstring.h"
#include "System/nvector.h"

// Singleton storing server addresses for failover.
// Populated from base64url-encoded JSON "params" command-line argument.
// ChooseBestByPing() picks the endpoint with lowest UDP RDP-INIT RTT.
class ServerAddressList
{
public:
  // RDP login endpoint port and mux — одинаковы для main и proxy.
  static const int kGamePort      = 35001;
  static const int kLoginMux      = 10;
  // Maximum endpoints supported — связывает фиксированные буферы в .cpp
  // (batch probe, thread-local address buffers).
  static const int kMaxEntries    = 4;

  // Контракт с Castle: JSON-ключи в base64url-JSON -params argv. Общие константы
  // чтобы при смене контракта не было рассинхрона между Game.cpp и renderer'ом.
  static const char* const kParamsJsonKeyAddresses; // "serverAddresses"
  static const char* const kParamsJsonKeyMain;      // "main"
  static const char* const kParamsJsonKeyProxy;     // "proxy"

  struct Entry
  {
    nstl::string name;         // "main" or "proxy" — используется только в логах
    nstl::string host;         // bare hostname or IP, без порта
    bool         responsive;   // последнее состояние probe: true если endpoint ответил
  };

  static ServerAddressList& Instance()
  {
    static ServerAddressList inst;
    return inst;
  }

  void Clear()
  {
    entries.clear();
    currentIndex = 0;
  }

  // Добавляет endpoint. Пустой hostname игнорируется.
  void Add( const char* name, const char* bareHost );

  bool HasAlternatives() const { return entries.size() > 1; }

  const char* GetCurrentName() const
  {
    return entries.empty() ? "" : entries[currentIndex].name.c_str();
  }

  // Полный адрес "host:port@mux" для индекса. Возвращает thread-local буфер
  // (stable в пределах одного endpoint'а на поток, перезаписывается при повторном вызове).
  const char* GetEntryAddress( int index ) const;

  // Полный адрес текущего выбранного endpoint'а.
  const char* GetCurrentAddress() const { return GetEntryAddress( currentIndex ); }

  // Полный адрес первой *responsive* альтернативы. Если alt не ответил на probe —
  // возвращает пустую строку (чтобы не конфигурировать failover на мёртвый endpoint).
  const char* GetResponsiveAlternativeAddress() const;

  void SetCurrent( int index )
  {
    if ( index >= 0 && index < entries.size() )
      currentIndex = index;
  }

  // Probe каждый endpoint UDP RDP-INIT пакетами, выбрать лучший avg RTT.
  // Возвращает индекс лучшего endpoint'а (current не меняется), -1 если никто не ответил.
  // Побочный эффект: обновляет Entry::responsive для каждого endpoint'а.
  int ChooseBestByPing();

private:
  ServerAddressList() : currentIndex(0) {}
  ServerAddressList(const ServerAddressList&);
  ServerAddressList& operator=(const ServerAddressList&);

  nstl::vector<Entry> entries;
  int                 currentIndex;
};
