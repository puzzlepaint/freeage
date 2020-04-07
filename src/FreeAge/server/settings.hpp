// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QByteArray>

#include "FreeAge/common/free_age.hpp"

struct ServerSettings {
  /// Time point at which the server was started. This serves as the reference time point
  /// for the server time, which the clients attempt to synchronize to.
  TimePoint serverStartTime;
  
  /// Token with which the server was started. This is used by the host
  /// to authorize itself when making the TCP connection to the server.
  QByteArray hostToken;
  
  /// State of the setting whether additional players may connect to the server.
  bool allowNewConnections = true;
  
  /// Whether accepting new connections is paused on the QTcpServer. This may be true
  /// even if allowNewConnections is true, which happens when the host readied up while
  /// not unchecking the allowNewConnections setting.
  bool acceptingConnectionsPaused = false;
  
  /// The map size chosen by the host.
  u16 mapSize = 50;  // TODO: Make sure that this is the same default as on the client (or make the host send a settings update right at the start)
};
