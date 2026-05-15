#pragma once

#include "System/nstring.h"
#include "System/nvector.h"
#include "System/SyncPrimitives.h"
#include "UI/TextComponent.h"

namespace NMainLoop
{

// On-screen overlay для debug-вывода событий подключения (RDP probe, LoginClient
// state, auto-fallback). Видим игроку в shipping-билде — помогает пользователю
// прислать скрин при проблемах подключения, а нам — диагностировать без доступа
// к файлу логов.
//
// Singleton. Thread-safe (мессаги приходят из rdp/login потоков, render на main).
// Автоматически скрывается при статусе "game" (игрок в лобби, чат доступен).
class NetworkStatusOverlay
{
public:
  static NetworkStatusOverlay & Instance();

  // Добавить строку в буфер. printf-style. Можно вызывать из любого потока.
  // Длина каждой строки ограничена 255 символами.
  void Log( const char * fmt, ... );

  // Вывести накопленные строки в левом верхнем углу. Должен вызываться каждый
  // кадр — Render::DebugRenderer опустошает queue на каждом frame.
  void Draw();

  // Скрыть overlay (после успешного входа в лобби).
  void Clear();

  // Принудительное включение/выключение. Включён по умолчанию.
  void SetEnabled( bool e );

private:
  NetworkStatusOverlay();
  NetworkStatusOverlay( const NetworkStatusOverlay& );
  NetworkStatusOverlay& operator=( const NetworkStatusOverlay& );

  static const int kMaxLines = 15;

  threading::Mutex    mutex;
  nstl::wstring       lines[kMaxLines];
  int                 head;   // next write index
  int                 count;  // total stored (<=kMaxLines)
  bool                enabled;
  UI::TextComponent   textComponent;
};

} // namespace NMainLoop
