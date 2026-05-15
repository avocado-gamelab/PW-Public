#include "stdafx.h"
#include "NetworkStatusOverlay.h"

#include "UI/Resolution.h"
#include "UI/SkinStyles.h"
#include "UI/FontRender.h"
#include "UI/FontStyle.h"
#include "UI/TextComponent.h"
#include <stdio.h>
#include <stdarg.h>

namespace NMainLoop
{

NetworkStatusOverlay::NetworkStatusOverlay()
  : head( 0 )
  , count( 0 )
  , enabled( true )
{
}


NetworkStatusOverlay & NetworkStatusOverlay::Instance()
{
  static NetworkStatusOverlay inst;
  return inst;
}


void NetworkStatusOverlay::Log( const char * fmt, ... )
{
  char buf[256];
  va_list args;
  va_start( args, fmt );
  _vsnprintf( buf, sizeof(buf), fmt, args );
  buf[sizeof(buf) - 1] = 0;
  va_end( args );

  // char → wchar_t через MultiByteToWideChar: корректно сохранит UTF-8 русский.
  wchar_t wbuf[256];
  int w = MultiByteToWideChar( CP_UTF8, 0, buf, -1, wbuf, 256 );
  if ( w <= 0 )
  {
    // Fallback: ANSI
    MultiByteToWideChar( CP_ACP, 0, buf, -1, wbuf, 256 );
  }
  wbuf[255] = 0;

  threading::MutexLock lock( mutex );
  lines[head] = wbuf;
  head = ( head + 1 ) % kMaxLines;
  if ( count < kMaxLines ) ++count;

  // Дублируем в обычный trace — чтобы попадало в текстовый лог.
  MessageTrace( "[NetOverlay] %s", buf );
}


void NetworkStatusOverlay::Clear()
{
  threading::MutexLock lock( mutex );
  count = 0;
  head = 0;
  for ( int i = 0; i < kMaxLines; ++i )
    lines[i].clear();
}


void NetworkStatusOverlay::SetEnabled( bool e )
{
  threading::MutexLock lock( mutex );
  enabled = e;
}


// Прямая отрисовка одной строки через UI::TextComponent.
// OverrideFontStyle с GetDebugFontStyle ставит шрифт без DB-style (uiFontStyle==NULL),
// поэтому TextComponent::Render идёт по ветке `else` (TextComponent.cpp:82-86),
// где используется наш SetDefaultColor — цвет применяется, шрифт маленький.
static void DrawLine(
  UI::TextComponent & tc,
  const wchar_t * text,
  int x, int y,
  int fontSize,
  const Render::Color & color )
{
  UI::Point res = UI::GetUIScreenResolution();
  if ( x < 0 || y < 0 || x > res.x || y > res.y )
    return;

  UI::IFontStyle * style = UI::GetFontRenderer()->GetDebugFontStyle( fontSize );
  if ( style )
    tc.OverrideFontStyle( style );

  tc.SetDefaultColor( color );
  tc.SetText( nstl::wstring( text ) );

  const float width  = tc.GetTextWidth();
  const float height = tc.GetTextHeight();

  // Draw rect — tight, определяет позицию baseline. CropRect — расширенный
  // вниз и вверх, чтобы descender'ы (р, у, g) и ascender'ы не обрезались.
  // Horizontal padding — для italic/bold свесов и margin'а.
  const int padX       = 4;
  const int cropUp     = 2;
  const int cropDown   = 5;
  UI::Rect drawR( x - padX, y, x + (int)width + padX, y + (int)height );
  UI::Rect cropR( x - padX, y - cropUp,
                  x + (int)width + padX, y + (int)height + cropDown );
  tc.SetDrawRect( drawR );
  tc.SetCropRect( cropR );
  tc.Render();
}


void NetworkStatusOverlay::Draw()
{
  // Снимок строк под lock — render снаружи, чтобы потоки-логгеры (RDP probe /
  // LoginClient) не блокировались на GPU-рендере.
  nstl::wstring snapshot[kMaxLines];
  int snapCount = 0;
  {
    threading::MutexLock lock( mutex );
    if ( !enabled || count == 0 )
      return;
    const int tail = ( head - count + kMaxLines ) % kMaxLines;
    for ( int i = 0; i < count; ++i )
      snapshot[i] = lines[ ( tail + i ) % kMaxLines ];
    snapCount = count;
  }

  const int fontSize = 10;
  const int xLeft    = 20;
  const int lineH    = 17;
  UI::Point res = UI::GetUIScreenResolution();
  const int headerY = res.y - ( kMaxLines + 2 ) * lineH;

  textComponent.EnableWordWrap( false );
  textComponent.SetStretchText( false );
  textComponent.SetVAlign( NDb::UITEXTVALIGN_TOP );
  textComponent.SetHAlign( NDb::UITEXTHALIGN_LEFT );

  const Render::Color colorHdr ( 255, 140, 0,   255 ); // #FF8C00 ярко-оранжевый
  const Render::Color colorBody( 255, 217, 50,  255 ); // #FFD933 жёлтый

  DrawLine( textComponent, L"Network status:", xLeft, headerY, fontSize, colorHdr );

  int y = headerY + lineH;
  for ( int i = 0; i < snapCount; ++i )
  {
    if ( !snapshot[i].empty() )
      DrawLine( textComponent, snapshot[i].c_str(), xLeft, y, fontSize, colorBody );
    y += lineH;
  }
}

} // namespace NMainLoop
