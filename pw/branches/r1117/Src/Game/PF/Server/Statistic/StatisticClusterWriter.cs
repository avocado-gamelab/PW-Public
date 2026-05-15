using System;
using System.Linq;
using BusinessLogic.Game;
using KontagentLib;
using StatisticService.ThriftImpls;
using log4net;


namespace StatisticService.RPC
{
  public class StatisticClusterWriter
  {
    private static ILog _log = LogManager.GetLogger(typeof(StatisticClusterWriter));

    private ISessionWriter writer;
    private ISessionEventWriter eventwriter;
    private IKontagentManager kontagentManager;
    private static RabbitMqPublisher _rabbitMq = new RabbitMqPublisher();

    public StatisticClusterWriter(IUnmanagedRpcCallbacks unrpc, ISessionWriter writer, ISessionEventWriter eventwriter, IKontagentManager km)
    {
      this.writer = writer;
      this.eventwriter = eventwriter;
      this.kontagentManager = km;

      unrpc.Register(new RemoteCallHandler<SessionStartEvent>(LogSessionStartAndPublish));
      unrpc.Register(new RemoteCallHandler<SessionResultEvent>(LogSessionResultsAndPublish));
      unrpc.Register(new RemoteCallHandler<MMakingCanceled>(LogMMakingCancelledAndPublish));
      unrpc.Register(new RemoteCallHandler<MMakingGame>(LogMMakingGameAndPublish));
      unrpc.Register(new RemoteCallHandler<UserDisconnectEvent>(LogUserDisconnectedAndPublish));
      unrpc.Register(new RemoteCallHandler<UserStatusEvent>(LogUserStatusAndPublish));
      unrpc.Register(new RemoteCallHandler<UserCheatEvent>(LogUserCheatedAndPublish));
      unrpc.Register(new RemoteCallHandler<ExceedingStepTimeInfoServer>(LogGSLagAndPublish));
      unrpc.Register(new RemoteCallHandler<ReconnectAttemptInfo>(LogReconnectAndPublish));
      unrpc.Register(new RemoteCallHandler<SessionTrafficInfo>(LogTrafficAndPublish));
    }

    /// <summary>
    /// Publishes event to RabbitMQ. Called from StatisticWriter/StatisticDebugWriter for their events.
    /// </summary>
    public static void PublishToRabbitMQ<T>(T info, ICallContext callCtx)
    {
      _rabbitMq.Publish(info);
    }

    // Wrappers: original handler + RabbitMQ publish

    private void LogSessionStartAndPublish(SessionStartEvent info, ICallContext callCtx)
    {
      LogSessionStart(info, callCtx);
      _rabbitMq.Publish(info);
    }

    private void LogSessionResultsAndPublish(SessionResultEvent info, ICallContext callCtx)
    {
      LogSessionResults(info, callCtx);
      _rabbitMq.Publish(info);
    }

    private void LogMMakingCancelledAndPublish(MMakingCanceled info, ICallContext callCtx)
    {
      LogMMakingCancelled(info, callCtx);
      _rabbitMq.Publish(info);
    }

    private void LogMMakingGameAndPublish(MMakingGame game, ICallContext callCtx)
    {
      LogMMakingGame(game, callCtx);
      _rabbitMq.Publish(game);
    }

    private void LogUserDisconnectedAndPublish(UserDisconnectEvent evt, ICallContext callCtx)
    {
      LogUserDisconnected(evt, callCtx);
      _rabbitMq.Publish(evt);
    }

    private void LogUserStatusAndPublish(UserStatusEvent evt, ICallContext callCtx)
    {
      LogUserStatus(evt, callCtx);
      _rabbitMq.Publish(evt);
    }

    private void LogUserCheatedAndPublish(UserCheatEvent evt, ICallContext callCtx)
    {
      LogUserCheated(evt, callCtx);
      _rabbitMq.Publish(evt);
    }

    private void LogGSLagAndPublish(ExceedingStepTimeInfoServer info, ICallContext callCtx)
    {
      LogGSLag(info, callCtx);
      _rabbitMq.Publish(info);
    }

    private void LogReconnectAndPublish(ReconnectAttemptInfo info, ICallContext callCtx)
    {
      LogReconnect(info, callCtx);
      _rabbitMq.Publish(info);
    }

    private void LogTrafficAndPublish(SessionTrafficInfo info, ICallContext callCtx)
    {
      LogTraffic(info, callCtx);
      _rabbitMq.Publish(info);
    }

    // Original handlers

    //remote
    public void LogSessionStart(SessionStartEvent info, ICallContext callCtx)
    {
      try
      {
        _log.DebugFormat("Session start "  + info);
        writer.SessionStart(info.sessionid, info.map, info.server, info.serverAddr, info.cluster, info.sessionType, callCtx.ToDate(),
                            info.players.Select(p => (SessionStartPlayerInfo)p));

        if (kontagentManager != null)
          foreach(var player in info.players)
            kontagentManager.SessionStart(player.userid, player.faction, info.sessionType, player.heroid);
      }
      catch (Exception ex)
      {
        _log.Error("Failed to write session start " + info, ex);
      }
    }

    //remote
    public void LogSessionResults(SessionResultEvent info, ICallContext callCtx)
    {
      try
      {
        _log.DebugFormat("Session results " + info);
        writer.SessionResult(info.sessionid, info.result, info.clientData.sideWon, info.clientData.surrenderVote, callCtx.ToDate(),
          info.clientData.players.Select(p =>
          new SessionResultPlayerInfo
          {
            UserId = p.userid,
            FinalLevel = p.scoring.finalLevel,
            Leaver = info.serverPlayersInfo.Single(i => i.userid == p.userid).finishStatus != (int)EGameFinishClientState.FinishedGame,
            Score = p.scoring.score,
            Kills = p.scoring.kills,
            Deaths = p.scoring.deaths,
            Assists = p.scoring.assists,
            EnemyCreepsKilled = p.scoring.enemyCreepsKilled,
            NeutralCreepsKilled = p.scoring.neutralCreepsKilled
          }
          ));
      }
      catch (Exception ex)
      {
        _log.Error("Failed to write session results " + info, ex);
      }
    }

    //remote
    public void LogMMakingCancelled(MMakingCanceled info, ICallContext callCtx)
    {
      try
      {
        _log.Debug("Matchmaking cancelled " + info.member + ", map " + info.map);
        writer.MMakingCancelled((MMakingPlayerInfo)info.member, callCtx.ToDate());
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex );
      }
    }

    //remote
    public void LogMMakingGame(MMakingGame game, ICallContext callCtx)
    {
      try
      {
        _log.Debug("Matchmaking session has created " + game);
        writer.MMakingSession(game.persistentId, game.status, -1, game.basket, game.map, callCtx.ToDate(), game.members.Select(m => (MMakingPlayerInfo)m));
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    //'reason' is Peered::Status from C++ PeeredTypes.h
    //remote
    public void LogUserDisconnected(UserDisconnectEvent evt, ICallContext callCtx)
    {
      try
      {
        _log.DebugFormat("User dropped: {0}, {1}", evt.ctx, evt.reason);
        eventwriter.Disconnect(evt.ctx.sessionid, evt.ctx.userid, evt.reason, callCtx.ToDate());
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    //'status' is Peered::Status from C++ PeeredTypes.h
    //'reason' is Peered::EDisconnectReason::Enum from C++ Peered.h
    //remote
    public void LogUserStatus(UserStatusEvent evt, ICallContext callCtx)
    {
      try
      {
        _log.DebugFormat("User status: {0}, {1}, {2}, {3}", evt.ctx, evt.status, evt.step, evt.reason);
        //TODO...
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    // 'cheatType' is lobby::ECheatType from C++ ISessionHybridLink.h
    //remote
    public void LogUserCheated(UserCheatEvent evt, ICallContext callCtx)
    {
      try
      {
        _log.DebugFormat("User cheated: {0}, {1}, {2}", evt.ctx, evt.cheatType, evt.clientIp);
        eventwriter.Cheat(evt.ctx.sessionid, evt.ctx.userid, evt.cheatType, evt.clientIp, callCtx.ToDate());
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    //remote
    public void LogGSLag(ExceedingStepTimeInfoServer info, ICallContext callCtx)
    {
      try
      {
        _log.Debug("GS lag: " + info);
        eventwriter.ExceedingServerStepTime(info.sessionId, info.currentStep,
                                            EntitiesConvertor.FromUnixTimestamp(info.startTime, callCtx.transmissionTimestamp),
                                            EntitiesConvertor.FromUnixTimestamp(info.finishTime, callCtx.transmissionTimestamp),
                                            info.stepCount, info.stepTimeMin, info.stepTimeMax, info.stepTimeAvg);
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    //remote
    public void LogReconnect(ReconnectAttemptInfo info, ICallContext callCtx)
    {
      try
      {
        _log.Debug("Reconnect: " + info);
        eventwriter.Reconnect(info.sessionId, info.userId, info.currentStep,
                              ((ReconnectType) info.reconnectType).ToString().ToLower(),
                              ((ReconnectResult) info.resultCode).ToString().ToLower(),
                              callCtx.ToDate());
      }
      catch (Exception ex)
      {
        _log.Error("EXCEPTIONARE:", ex);
      }
    }

    //remote
    public void LogTraffic(SessionTrafficInfo info, ICallContext callCtx)
    {
      try
      {
        _log.Debug("Traffic: " + info);
        eventwriter.GSTraffic(info.sessionId, info.totalIn, info.totalOut, info.avgOutPerStep, info.maxOutPerStep, info.totalInReconnect,
                              info.totalOutReconnect, info.avgOutReconnect, info.maxOutReconnect, info.avgCmdPerSecond, info.maxCmdPerSecond);
      }
      catch ( Exception ex )
      {
        _log.Error( "EXCEPTIONARE:", ex );
      }
    }
  }
}
