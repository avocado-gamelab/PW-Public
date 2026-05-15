#ifndef NEW_LOBBYGAMECLIENT_H_INCLUDED
#define NEW_LOBBYGAMECLIENT_H_INCLUDED

#include "LobbyClientBase.h"
#include "Core/SessionRunner.h"
#include "Core/GameTypes.h"
#include <Server/AppFramework/Interfaces.h>
#include "PW_Client/GameStatistics.h"
#include "System/Histogram.h"
#include "PF_GameLogic/ReplayInfo.h"


namespace NCore
{
  _interface IWorldBase;
  class ITransceiver;
  class ICommandScheduler;
  class ServerNode;
  class ReplayWriter;
}



namespace NWorld
{
  class IMapCollection;
  class IMapLoader;
  class MapDescriptionLoader;
}



namespace NDb
{
  enum EFaction;
  struct AdvMapDescription;
}



namespace Peered
{
  struct BriefClientInfo;
  struct SHybridPongResult;
}


namespace lobby
{

class FastReconnectCtx;

namespace EGameState
{
  enum Enum
  {
    Startup,
    ShowingLoadingScreen,
    Connecting,
    StartingGame,
    LoadingMap,
    MapLoadCooldown,
    WaitingOtherClients,
    Running,
    Finished
  };
}

//-------------------------------------------------------------------
// ѕродолжатель дела ClientBase, но на поприще старта игровой сессии
//-------------------------------------------------------------------
// ¬ообще этот код можно было оставить пр€мо в ClientBase.
// ‘ункционал разделен на два класса по двум причинам:
//  1. Ќекоторые вещи отсюда не нужны в GameTestClient
//  2. ¬ ClientBase уже и так много кода

//Updated: ¬се-таки нужно склеить ClientBase и GameClient в один класс.
//‘ункционал из GameClient, ненужный в GameTestclient, перенести
//в производный "PW"-класс

class GameClient : public NCore::ISessionRunner, public BaseObjectMT
{  
  NI_DECLARE_REFCOUNT_CLASS_2( GameClient, NCore::ISessionRunner, BaseObjectMT );

public:
  typedef map<int, int> TUsersLoadingStatus; //userId -> loadingProgress

  GameClient( ClientBase * _client, NWorld::IMapCollection * _mapCollection, FastReconnectCtx * _fastReconnectCtx, const bool _isSpectator, const bool _isTutorial, const bool _isSingle, const int _ratingMin, const int _ratingMax );

  ClientBase * Client() { return client; }
  Transport::TClientId ClientId() { return clientId; }

  bool IsThisGameServerNode( rpc::INode * node );

  void SetGameStatistics( StatisticService::GameStatistics * stat ) { gameStatistics = stat; }
  StatisticService::GameStatistics * GameStatistics() { return gameStatistics; }

  int Poll( float transceiverDeltaTime ); // returns number of commands in buffer

  EGameState::Enum GameState() const { return gameState; }
  const wstring & GetNickname() const { return nickname; }
  const TUsersLoadingStatus & UsersLoadingProgress() const { return loadingProgress; }

  virtual bool PopClientStatusChange( wstring & nickname, Peered::BriefClientInfo & info );

  Transport::Ping GetHybridServerTransportPing();
  Transport::Ping GetLobbyServerTransportPing();

  bool IsFastReconnectEnabled() const;

  void OnMulticastData(const vector<byte> & data);

protected:
  //NCore::ISessionRunner
  virtual void ProcessAsyncMessage( IObjectBase *pMsg );
  virtual void ProcessAsyncStatus( NCore::ClientStatus & status ) {};

  //IClientNotify
  virtual void OnStatusChange( EClientStatus::Enum oldStatus, EClientStatus::Enum newStatus ) {}
  virtual void OnLobbyDataChange() {}
  virtual void OnPlayerDropped( int clientId ) { loadingProgress[clientId] = -1; }

  NCore::ICommandScheduler * Scheduler() const { return scheduler; }
  NCore::ITransceiver * Transceiver() const { return transceiver; }
  NCore::ServerNode * ServerNode() const { return hybridServerNode; }
  NCore::ReplayWriter * ReplayWriter() const { return replayWriter; }

  const NCore::MapStartInfo & GetMapStartInfo() const { return mapStartInfo; }

  virtual void ShowLoadingScreen() {}
  virtual void HideLoadingScreen() {}
  virtual void OnPlayerInfoLoaded() {}
  virtual void CreateAdventureScreen() {}
  virtual void SendTeam() {};
  virtual void StartMapLoading( NCore::IWorldBase * world ) {}
  virtual void NotifyChatMuted() {}

  void OnCombatScreenStarted( NCore::IWorldBase * world, const NGameX::ReplayInfo & _replayInfos );
  void SetMapLoadingProgress( int progress );
  void OnMapLoaded();
  void OnVictory( const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo );
  void SetTimeScale(float timescale);

  void SetLastWorldRequestStep(int _lastWorldRequestStep);

  StrongMT<NWorld::IMapCollection> GetMapCollection() { return mapCollection; }
  StrongMT<NWorld::IMapLoader> GetMapLoader() { return mapLoader; }
  bool IsSpectator() const { return isSpectator; }
  bool IsTutorial() const { return isTutorial; }
  bool IsSingle() const { return isSingle; }
  int GetRatingMin() const { return ratingMin; }
  int GetRatingMax() const { return ratingMax; }
  NWorld::MapDescriptionLoader * MapDescription() const { return mapDescription; }

protected:
  NCore::MapStartInfo                 mapStartInfo;

private:
  EGameState::Enum                    gameState;
  StrongMT<ClientBase>                client;
  WeakMT<Transport::IChannel>         lobbyServerChannel;
  Transport::TClientId                clientId;
  StrongMT<NCore::ServerNode>         hybridServerNode;
  bool                                hybridStarted;
  wstring                             nickname;

  StrongMT<NWorld::IMapCollection>    mapCollection;
  StrongMT<NWorld::IMapLoader>        mapLoader;

  Strong<NCore::ITransceiver>         transceiver;
  StrongMT<NCore::ICommandScheduler>  scheduler;

  StrongMT<StatisticService::GameStatistics>  gameStatistics;

  int                                 currentMapLoadProgress;
  timer::Time                         nextMapLoadProgressTime;

  TUsersLoadingStatus                 loadingProgress;

  int                                 framesSkipped;
  timer::Time                         timeToSkip;

  timer::Time                         timeLastPingRequest; // врем€, когда последний раз запрашивали пинг (получили ответ или нет - неважно)
  timer::Time                         timeLastUnansweredPing; // врем€, когда последний раз запрашивали пинг, на который еще Ќ≈ получен ответ (при ответе сбрасываем в 0)

  AppFramework::InstanceStatistics    stats;
  int                                 lastStep;

  int                                 openedFileCount;

  AppFramework::InstanceStatistics    noDataStats;
  int                                 noDataStep;

  int                                 lastDebugStep;
  timer::Time                         nextChannelDbgInfo;

  timer::Time                         lastPollTime;
  timer::Time                         nextFrameTimeDump;
  CustomHistogram                     frameTimeHistogram;

  StatisticService::PingStatisticsContainer  pingStatistics;

  StrongMT<FastReconnectCtx>          fastReconnectCtx;
  const bool                          isSpectator;
  const bool                          isTutorial;
  const bool                          isSingle;
  const int                           ratingMin;
  const int                           ratingMax;

  Strong<NWorld::MapDescriptionLoader>  mapDescription;

  StrongMT<NCore::ReplayWriter>       replayWriter;

  void UpdateNoDataStatistics(int step, bool noData);
  void UpdatePingStatistics();
  void EnterGameServer();
  void StartGame();
  void PrepareMap();
  void PrepareReplayWriter();
  void StartingGame();
  
  void RunSession();
  void OnHybridPing( Peered::SHybridPongResult& time_step, int context, rpc::CallStatus status );
  void RequestHybridPing();
  void SendMyProgressIfNeeded();
  void StartStepStatistics(int step);
  void StopStepStatistics(int step);

  void DisplayDebugInfo();
};

} //namespace lobby

#endif //NEW_LOBBYGAMECLIENT_H_INCLUDED
