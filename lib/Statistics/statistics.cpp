////////////////////////////////////////////////////////////////////////////////
/// @brief statistics basics
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2012-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "statistics.h"

#include "BasicsC/locks.h"

#ifdef TRI_HAVE_MACOS_MEM_STATS
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

using namespace triagens::basics;
using namespace std;

#ifdef TRI_USE_SPIN_LOCK_STATISTICS
#define STATISTICS_TYPE TRI_spin_t
#define STATISTICS_INIT TRI_InitSpin
#define STATISTICS_DESTROY TRI_DestroySpin
#define STATISTICS_LOCK TRI_LockSpin
#define STATISTICS_UNLOCK TRI_UnlockSpin
#else
#define STATISTICS_TYPE TRI_mutex_t
#define STATISTICS_INIT TRI_InitMutex
#define STATISTICS_DESTROY TRI_DestroyMutex
#define STATISTICS_LOCK TRI_LockMutex
#define STATISTICS_UNLOCK TRI_UnlockMutex
#endif

// -----------------------------------------------------------------------------
// --SECTION--                              private request statistics variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief lock for lists
////////////////////////////////////////////////////////////////////////////////

static STATISTICS_TYPE RequestListLock;

////////////////////////////////////////////////////////////////////////////////
/// @brief free list
////////////////////////////////////////////////////////////////////////////////

static TRI_statistics_list_t RequestFreeList;

// -----------------------------------------------------------------------------
// --SECTION--                               public request statistics functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief gets a new statistics block
////////////////////////////////////////////////////////////////////////////////

TRI_request_statistics_t* TRI_AcquireRequestStatistics () {
  TRI_request_statistics_t* statistics = NULL;

  STATISTICS_LOCK(&RequestListLock);

  if (RequestFreeList._first != NULL) {
    statistics = (TRI_request_statistics_t*) RequestFreeList._first;
    RequestFreeList._first = RequestFreeList._first->_next;
  }

  STATISTICS_UNLOCK(&RequestListLock);

  return statistics;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief releases a statistics block
////////////////////////////////////////////////////////////////////////////////

void TRI_ReleaseRequestStatistics (TRI_request_statistics_t* statistics) {
  STATISTICS_LOCK(&RequestListLock);

  TotalRequests.incCounter();
  if (statistics->_async) {
    AsyncRequests.incCounter();
  }

  MethodRequests[(int) statistics->_requestType].incCounter();

  // check the request was completely received and transmitted
  if (statistics->_readStart != 0.0 && statistics->_writeEnd != 0.0) {
    double totalTime = statistics->_writeEnd - statistics->_readStart;
    TotalTimeDistribution->addFigure(totalTime);

    double requestTime = statistics->_requestEnd - statistics->_requestStart;
    RequestTimeDistribution->addFigure(requestTime);

    if (statistics->_queueStart != 0.0 && statistics->_queueEnd != 0.0) {
      double queueTime = statistics->_queueEnd - statistics->_queueStart;
      QueueTimeDistribution->addFigure(queueTime);
    }

    BytesSentDistribution->addFigure(statistics->_sentBytes);
    BytesReceivedDistribution->addFigure(statistics->_receivedBytes);
  }

  // clear statistics and put back an the free list
  memset(statistics, 0, sizeof(TRI_request_statistics_t));
  statistics->_requestType = triagens::rest::HttpRequest::HTTP_REQUEST_ILLEGAL;

  if (RequestFreeList._first == NULL) {
    RequestFreeList._first = (TRI_statistics_entry_t*) statistics;
    RequestFreeList._last = (TRI_statistics_entry_t*) statistics;
  }
  else {
    RequestFreeList._last->_next = (TRI_statistics_entry_t*) statistics;
    RequestFreeList._last = (TRI_statistics_entry_t*) statistics;
  }

  RequestFreeList._last->_next = 0;

  STATISTICS_UNLOCK(&RequestListLock);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fills the current statistics
////////////////////////////////////////////////////////////////////////////////

void TRI_FillRequestStatistics (StatisticsDistribution& totalTime,
                                StatisticsDistribution& requestTime,
                                StatisticsDistribution& queueTime,
                                StatisticsDistribution& bytesSent,
                                StatisticsDistribution& bytesReceived) {
  STATISTICS_LOCK(&RequestListLock);

  totalTime = *TotalTimeDistribution;
  requestTime = *RequestTimeDistribution;
  queueTime = *QueueTimeDistribution;
  bytesSent = *BytesSentDistribution;
  bytesReceived = *BytesReceivedDistribution;

  STATISTICS_UNLOCK(&RequestListLock);
}

// -----------------------------------------------------------------------------
// --SECTION--                           private connection statistics variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief lock for lists
////////////////////////////////////////////////////////////////////////////////

static STATISTICS_TYPE ConnectionListLock;

////////////////////////////////////////////////////////////////////////////////
/// @brief free list
////////////////////////////////////////////////////////////////////////////////

static TRI_statistics_list_t ConnectionFreeList;

// -----------------------------------------------------------------------------
// --SECTION--                            public connection statistics functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief gets a new statistics block
////////////////////////////////////////////////////////////////////////////////

TRI_connection_statistics_t* TRI_AcquireConnectionStatistics () {
  TRI_connection_statistics_t* statistics = 0;

  STATISTICS_LOCK(&ConnectionListLock);

  if (ConnectionFreeList._first != NULL) {
    statistics = (TRI_connection_statistics_t*) ConnectionFreeList._first;
    ConnectionFreeList._first = ConnectionFreeList._first->_next;
  }

  STATISTICS_UNLOCK(&ConnectionListLock);

  return statistics;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief releases a statistics block
////////////////////////////////////////////////////////////////////////////////

void TRI_ReleaseConnectionStatistics (TRI_connection_statistics_t* statistics) {
  STATISTICS_LOCK(&ConnectionListLock);

  if (statistics->_http) {
    if (statistics->_connStart != 0) {
      if (statistics->_connEnd == 0) {
        HttpConnections.incCounter();
      }
      else {
        HttpConnections.decCounter();

        double totalTime = statistics->_connEnd - statistics->_connStart;
        ConnectionTimeDistribution->addFigure(totalTime);
      }
    }
  }

  // clear statistics and put back an the free list
  memset(statistics, 0, sizeof(TRI_connection_statistics_t));

  if (ConnectionFreeList._first == NULL) {
    ConnectionFreeList._first = (TRI_statistics_entry_t*) statistics;
    ConnectionFreeList._last = (TRI_statistics_entry_t*) statistics;
  }
  else {
    ConnectionFreeList._last->_next = (TRI_statistics_entry_t*) statistics;
    ConnectionFreeList._last = (TRI_statistics_entry_t*) statistics;
  }

  ConnectionFreeList._last->_next = 0;

  STATISTICS_UNLOCK(&ConnectionListLock);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fills the current statistics
////////////////////////////////////////////////////////////////////////////////

void TRI_FillConnectionStatistics (StatisticsCounter& httpConnections,
                                   StatisticsCounter& totalRequests,
                                   vector<StatisticsCounter>& methodRequests,
                                   StatisticsCounter& asyncRequests,
                                   StatisticsDistribution& connectionTime) {
  STATISTICS_LOCK(&ConnectionListLock);

  httpConnections = HttpConnections;
  totalRequests   = TotalRequests;
  methodRequests  = MethodRequests;
  asyncRequests   = AsyncRequests;
  connectionTime  = *ConnectionTimeDistribution;

  STATISTICS_UNLOCK(&ConnectionListLock);
}

// -----------------------------------------------------------------------------
// --SECTION--                                public server statistics functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief gets the global server statistics
////////////////////////////////////////////////////////////////////////////////

TRI_server_statistics_t TRI_GetServerStatistics () {
  TRI_server_statistics_t server;

  server._startTime = ServerStatistics._startTime;
  server._uptime    = TRI_microtime() - server._startTime;

  return server;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief gets the physical memory
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_HAVE_MACOS_MEM_STATS

uint64_t TRI_GetPhysicalMemory () {
  int mib[2];
  int64_t physicalMemory;
  size_t length;

  // Get the Physical memory size
  mib[0] = CTL_HW;
  mib[1] = HW_MEMSIZE;
  length = sizeof(int64_t);
  sysctl(mib, 2, &physicalMemory, &length, NULL, 0);

  return (uint64_t) physicalMemory;
}

#else

uint64_t TRI_GetPhysicalMemory () {
  return 0;
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief fills a linked list
////////////////////////////////////////////////////////////////////////////////

static void FillStatisticsList (TRI_statistics_list_t* list, size_t element, size_t count) {
  size_t i;
  TRI_statistics_entry_t* entry;

  entry = (TRI_statistics_entry_t*) TRI_Allocate(TRI_CORE_MEM_ZONE, element, true);

  list->_first = entry;
  list->_last = entry;

  for (i = 1;  i < count;  ++i) {
    entry = (TRI_statistics_entry_t*) TRI_Allocate(TRI_CORE_MEM_ZONE, element, true);

    list->_last->_next = entry;
    list->_last = entry;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a linked list
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyStatisticsList (TRI_statistics_list_t* list) {
  TRI_statistics_entry_t* entry = list->_first;
  while (entry != NULL) {
    TRI_statistics_entry_t* next = entry->_next;
    TRI_Free(TRI_CORE_MEM_ZONE, entry);
    entry = next;
  }

  list->_first = list->_last = NULL;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   public variable
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief statistics enabled flags
////////////////////////////////////////////////////////////////////////////////

bool TRI_ENABLE_STATISTICS = true;

////////////////////////////////////////////////////////////////////////////////
/// @brief number of http connections
////////////////////////////////////////////////////////////////////////////////

StatisticsCounter HttpConnections;

////////////////////////////////////////////////////////////////////////////////
/// @brief total number of requests
////////////////////////////////////////////////////////////////////////////////

StatisticsCounter TotalRequests;

////////////////////////////////////////////////////////////////////////////////
/// @brief number of requests by HTTP method
////////////////////////////////////////////////////////////////////////////////

std::vector<StatisticsCounter> MethodRequests;

////////////////////////////////////////////////////////////////////////////////
/// @brief number of async requests
////////////////////////////////////////////////////////////////////////////////

StatisticsCounter AsyncRequests;

////////////////////////////////////////////////////////////////////////////////
/// @brief connection time distribution vector
////////////////////////////////////////////////////////////////////////////////

StatisticsVector ConnectionTimeDistributionVector;

////////////////////////////////////////////////////////////////////////////////
/// @brief total time distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* ConnectionTimeDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief request time distribution vector
////////////////////////////////////////////////////////////////////////////////

StatisticsVector RequestTimeDistributionVector;

////////////////////////////////////////////////////////////////////////////////
/// @brief total time distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* TotalTimeDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief request time distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* RequestTimeDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief queue time distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* QueueTimeDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief bytes sent distribution vector
////////////////////////////////////////////////////////////////////////////////

StatisticsVector BytesSentDistributionVector;

////////////////////////////////////////////////////////////////////////////////
/// @brief bytes sent distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* BytesSentDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief bytes received distribution vector
////////////////////////////////////////////////////////////////////////////////

StatisticsVector BytesReceivedDistributionVector;

////////////////////////////////////////////////////////////////////////////////
/// @brief bytes received distribution
////////////////////////////////////////////////////////////////////////////////

StatisticsDistribution* BytesReceivedDistribution;

////////////////////////////////////////////////////////////////////////////////
/// @brief global server statistics
////////////////////////////////////////////////////////////////////////////////

TRI_server_statistics_t ServerStatistics;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief gets the current wallclock time
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_HIRES_FIGURES

double TRI_StatisticsTime () {
  struct timespec tp;

  clock_gettime(CLOCK_REALTIME, &tp);

  return tp.tv_sec + (tp.tv_nsec / 1000000000.0);
}

#else

double TRI_StatisticsTime () {
  return TRI_microtime();
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                             module initialisation
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief module init function
////////////////////////////////////////////////////////////////////////////////

void TRI_InitialiseStatistics () {
  ServerStatistics._startTime = TRI_microtime();

#if TRI_ENABLE_FIGURES

  static size_t const QUEUE_SIZE = 1000;

  // .............................................................................
  // sets up the statistics
  // .............................................................................

  ConnectionTimeDistributionVector << (0.1) << (1.0) << (60.0);

  BytesSentDistributionVector << (250) << (1000) << (2 * 1000) << (5 * 1000) << (10 * 1000);
  BytesReceivedDistributionVector << (250) << (1000) << (2 * 1000) << (5 * 1000) << (10 * 1000);

#ifdef TRI_ENABLE_HIRES_FIGURES
  RequestTimeDistributionVector << (0.0001) << (0.05) << (0.1) << (0.2) << (0.5) << (1.0);
#else
  RequestTimeDistributionVector << (0.01) << (0.05) << (0.1) << (0.2) << (0.5) << (1.0);
#endif

  ConnectionTimeDistribution = new StatisticsDistribution(ConnectionTimeDistributionVector);
  TotalTimeDistribution = new StatisticsDistribution(RequestTimeDistributionVector);
  RequestTimeDistribution = new StatisticsDistribution(RequestTimeDistributionVector);
  QueueTimeDistribution = new StatisticsDistribution(RequestTimeDistributionVector);
  BytesSentDistribution = new StatisticsDistribution(BytesSentDistributionVector);
  BytesReceivedDistribution = new StatisticsDistribution(BytesReceivedDistributionVector);

  // initialise counters for all HTTP request types
  MethodRequests.clear();

  for (int i = 0; i < ((int) triagens::rest::HttpRequest::HTTP_REQUEST_ILLEGAL) + 1; ++i) {
    StatisticsCounter c;
    MethodRequests.push_back(c);
  }

  // .............................................................................
  // generate the request statistics queue
  // .............................................................................

  RequestFreeList._first = RequestFreeList._last = NULL;

  FillStatisticsList(&RequestFreeList, sizeof(TRI_request_statistics_t), QUEUE_SIZE);

  STATISTICS_INIT(&RequestListLock);

  // .............................................................................
  // generate the connection statistics queue
  // .............................................................................

  ConnectionFreeList._first = ConnectionFreeList._last = NULL;

  FillStatisticsList(&ConnectionFreeList, sizeof(TRI_connection_statistics_t), QUEUE_SIZE);

  STATISTICS_INIT(&ConnectionListLock);
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shut down statistics
////////////////////////////////////////////////////////////////////////////////

void TRI_ShutdownStatistics (void) {
#if TRI_ENABLE_FIGURES
  delete ConnectionTimeDistribution;
  delete TotalTimeDistribution;
  delete RequestTimeDistribution;
  delete QueueTimeDistribution;
  delete BytesSentDistribution;
  delete BytesReceivedDistribution;

  TRI_DestroyStatisticsList(&RequestFreeList);
  TRI_DestroyStatisticsList(&ConnectionFreeList);
#endif
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
