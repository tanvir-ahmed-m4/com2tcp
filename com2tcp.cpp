/*
 * $Id$
 *
 * Copyright (c) 2005 Vyacheslav Frolov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * $Log$
 * Revision 1.4  2005/06/07 10:06:37  vfrolov
 * Added ability to use port names
 *
 * Revision 1.3  2005/06/06 15:20:46  vfrolov
 * Implemented --telnet option
 *
 * Revision 1.2  2005/05/30 12:17:32  vfrolov
 * Fixed resolving problem
 *
 * Revision 1.1  2005/05/30 10:02:33  vfrolov
 * Initial revision
 *
 *
 */

#include <winsock2.h>
#include <windows.h>

#include <stdio.h>

#include "utils.h"
#include "telnet.h"

///////////////////////////////////////////////////////////////
static void TraceLastError(const char *pFmt, ...)
{
  DWORD err = GetLastError();
  va_list va;
  va_start(va, pFmt);
  vfprintf(stderr, pFmt, va);
  va_end(va);

  fprintf(stderr, " ERROR %s (%lu)\n", strerror(err), (unsigned long)err);
}
///////////////////////////////////////////////////////////////
static BOOL myGetCommState(HANDLE hC0C, DCB *dcb)
{
  dcb->DCBlength = sizeof(*dcb);

  if (!GetCommState(hC0C, dcb)) {
    TraceLastError("GetCommState()");
    return FALSE;
  }
  return TRUE;
}

static BOOL mySetCommState(HANDLE hC0C, DCB *dcb)
{
  if (!SetCommState(hC0C, dcb)) {
    TraceLastError("SetCommState()");
    return FALSE;
  }
  return TRUE;
}
///////////////////////////////////////////////////////////////
static void CloseEvents(int num, HANDLE *hEvents)
{
  for (int i = 0 ; i < num ; i++) {
    if (hEvents[i]) {
      if (!::CloseHandle(hEvents[i])) {
        TraceLastError("CloseEvents(): CloseHandle()");
      }
      hEvents[i] = NULL;
    }
  }
}

static BOOL PrepareEvents(int num, HANDLE *hEvents, OVERLAPPED *overlaps)
{
  memset(hEvents, 0, num * sizeof(HANDLE));
  memset(overlaps, 0, num * sizeof(OVERLAPPED));

  for (int i = 0 ; i < num ; i++) {
    overlaps[i].hEvent = hEvents[i] = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvents[i]) {
      TraceLastError("PrepareEvents(): CreateEvent()");
      CloseEvents(i, hEvents);
      return FALSE;
    }
  }
  return TRUE;
}
///////////////////////////////////////////////////////////////
static void InOut(HANDLE hC0C, SOCKET hSock, Protocol &protocol)
{
  printf("InOut() START\n");

  BOOL stop = FALSE;

  enum {
    EVENT_READ,
    EVENT_SENT,
    EVENT_RECEIVED,
    EVENT_WRITTEN,
    EVENT_STAT,
    EVENT_NUM
  };

  HANDLE hEvents[EVENT_NUM];
  OVERLAPPED overlaps[EVENT_NUM];

  if (!PrepareEvents(EVENT_NUM, hEvents, overlaps))
    stop = TRUE;

  if (!SetCommMask(hC0C, EV_DSR)) {
    TraceLastError("InOut(): SetCommMask()");
    stop = TRUE;
  }

  DWORD not_used;

  BYTE cbufRead[64];
  BOOL waitingRead = FALSE;

  BYTE cbufSend[64];
  int cbufSendSize = 0;
  int cbufSendDone = 0;
  BOOL waitingSend = FALSE;

  BYTE cbufRecv[64];
  BOOL waitingRecv = FALSE;

  BYTE cbufWrite[64];
  int cbufWriteSize = 0;
  int cbufWriteDone = 0;
  BOOL waitingWrite = FALSE;

  BOOL waitingStat = FALSE;

  while (!stop) {
    if (!waitingSend) {
      if (!cbufSendSize) {
        cbufSendSize = protocol.Read(cbufSend, sizeof(cbufSend));
        if (cbufSendSize < 0)
          break;
      }

      DWORD num = cbufSendSize - cbufSendDone;

      if (num) {
        if (!WriteFile((HANDLE)hSock, cbufSend + cbufSendDone, num, &not_used, &overlaps[EVENT_SENT])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hSock)");
            break;
          }
        }
        waitingSend = TRUE;
      }
    }

    if (!waitingRead && !protocol.isSendFull()) {
      if (!ReadFile(hC0C, cbufRead, sizeof(cbufRead), &not_used, &overlaps[EVENT_READ])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): ReadFile(hC0C)");
          break;
        }
      }
      waitingRead = TRUE;
    }

    if (!waitingWrite) {
      if (!cbufWriteSize) {
        cbufWriteSize = protocol.Recv(cbufWrite, sizeof(cbufWrite));
        if (cbufWriteSize < 0)
          break;
      }

      DWORD num = cbufWriteSize - cbufWriteDone;

      if (num) {
        if (!WriteFile(hC0C, cbufWrite + cbufWriteDone, num, &not_used, &overlaps[EVENT_WRITTEN])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hC0C)");
            break;
          }
        }
        waitingWrite = TRUE;
      }
    }

    if (!waitingRecv && !protocol.isWriteFull()) {
      if (!ReadFile((HANDLE)hSock, cbufRecv, sizeof(cbufRecv), &not_used, &overlaps[EVENT_RECEIVED])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): ReadFile(hSock)");
          break;
        }
      }
      waitingRecv = TRUE;
    }

    if (!waitingStat) {
      if (!WaitCommEvent(hC0C, &not_used, &overlaps[EVENT_STAT])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("InOut(): WaitCommEvent()");
          break;
        }
      }
      waitingStat = TRUE;

      DWORD stat;

      if (!GetCommModemStatus(hC0C, &stat)) {
        TraceLastError("InOut(): GetCommModemStatus()");
        break;
      }

      if (!(stat & MS_DSR_ON)) {
        printf("DSR is OFF\n");
        break;
      }
    }

    if ((waitingRead || waitingSend) && (waitingRecv || waitingWrite) && waitingStat) {
      DWORD done;

      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_READ:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_READ], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_READ)");
            stop = TRUE;
            break;
          }
        }
        ResetEvent(hEvents[EVENT_READ]);
        waitingRead = FALSE;
        protocol.Send(cbufRead, done);
        break;
      case WAIT_OBJECT_0 + EVENT_SENT:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_SENT], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_SENT)");
            stop = TRUE;
            break;
          }
          done = 0;
        }
        ResetEvent(hEvents[EVENT_SENT]);
        cbufSendDone += done;
        if (cbufSendDone >= cbufSendSize)
          cbufSendDone = cbufSendSize = 0;
        waitingSend = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_RECEIVED:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_RECEIVED], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_RECEIVED)");
            stop = TRUE;
            break;
          }
          done = 0;
        } else if (!done) {
          ResetEvent(hEvents[EVENT_RECEIVED]);
          printf("Received EOF\n");
          break;
        }
        ResetEvent(hEvents[EVENT_RECEIVED]);
        waitingRecv = FALSE;
        protocol.Write(cbufRecv, done);
        break;
      case WAIT_OBJECT_0 + EVENT_WRITTEN:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_WRITTEN], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_WRITTEN)");
            stop = TRUE;
            break;
          }
          done = 0;
        }
        ResetEvent(hEvents[EVENT_WRITTEN]);
        cbufWriteDone += done;
        if (cbufWriteDone >= cbufWriteSize)
          cbufWriteDone = cbufWriteSize = 0;
        waitingWrite = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &done, FALSE)) {
          if (::GetLastError() != ERROR_OPERATION_ABORTED) {
            TraceLastError("InOut(): GetOverlappedResult(EVENT_STAT)");
            stop = TRUE;
            break;
          }
        }
        waitingStat = FALSE;
        break;
      case WAIT_TIMEOUT:
        break;                       
      default:
        TraceLastError("InOut(): WaitForMultipleObjects()");
        stop = TRUE;
      }
    }
  }

  CancelIo(hC0C);
  CancelIo((HANDLE)hSock);

  CloseEvents(EVENT_NUM, hEvents);

  printf("InOut() - STOP\n");
}
///////////////////////////////////////////////////////////////
static BOOL WaitComReady(HANDLE hC0C)
{
  enum {
    EVENT_STAT,
    EVENT_NUM
  };

  HANDLE hEvents[EVENT_NUM];
  OVERLAPPED overlaps[EVENT_NUM];

  if (!PrepareEvents(EVENT_NUM, hEvents, overlaps))
    return FALSE;

  BOOL fault = FALSE;

  if (!SetCommMask(hC0C, EV_DSR)) {
    TraceLastError("WaitComReady(): SetCommMask()");
    fault = TRUE;
  }

  DWORD not_used;
  BOOL waitingStat = FALSE;

  while (!fault) {
    if (!waitingStat) {
      if (!WaitCommEvent(hC0C, &not_used, &overlaps[EVENT_STAT])) {
        if (::GetLastError() != ERROR_IO_PENDING) {
          TraceLastError("WaitComReady(): WaitCommEvent()");
          fault = TRUE;
          break;
        }
      }
      waitingStat = TRUE;

      DWORD stat;

      if (!GetCommModemStatus(hC0C, &stat)) {
        TraceLastError("WaitComReady(): GetCommModemStatus()");
        fault = TRUE;
        break;
      }

      if (stat & MS_DSR_ON) {
        printf("DSR is ON\n");

        Sleep(1000);

        if (!GetCommModemStatus(hC0C, &stat)) {
          TraceLastError("WaitComReady(): GetCommModemStatus()");
          fault = TRUE;
          break;
        }

        if (stat & MS_DSR_ON)
          break;                // OK if DSR is still ON

        printf("DSR is OFF\n");
      }
    }

    if (waitingStat) {
      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &not_used, FALSE)) {
          TraceLastError("WaitComReady(): GetOverlappedResult(EVENT_STAT)");
          fault = TRUE;
        }
        waitingStat = FALSE;
        break;
      case WAIT_TIMEOUT:
        break;                       
      default:
        TraceLastError("WaitComReady(): WaitForMultipleObjects()");
        fault = TRUE;
      }
    }
  }

  CancelIo(hC0C);

  CloseEvents(EVENT_NUM, hEvents);

  printf("WaitComReady() - %s\n", fault ? "FAIL" : "OK");

  return !fault;
}
///////////////////////////////////////////////////////////////
static HANDLE OpenC0C(const char *pPath)
{
  HANDLE hC0C = CreateFile(pPath,
                    GENERIC_READ|GENERIC_WRITE,
                    0,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    NULL);

  if (hC0C == INVALID_HANDLE_VALUE) {
    TraceLastError("OpenC0C(): CreateFile(\"%s\")", pPath);
    return INVALID_HANDLE_VALUE;
  }

  DCB dcb;

  if (!myGetCommState(hC0C, &dcb)) {
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  dcb.BaudRate = CBR_19200;
  dcb.ByteSize = 8;
  dcb.Parity   = NOPARITY;
  dcb.StopBits = ONESTOPBIT;

  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = TRUE;
  dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = TRUE;
  dcb.XonChar = 0x11;
  dcb.XoffChar = 0x13;
  dcb.XonLim = 100;
  dcb.XoffLim = 100;
  dcb.fParity = FALSE;
  dcb.fNull = FALSE;

  if (!mySetCommState(hC0C, &dcb)) {
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  COMMTIMEOUTS timeouts;

  if (!GetCommTimeouts(hC0C, &timeouts)) {
    TraceLastError("OpenC0C(): GetCommTimeouts()");
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = MAXDWORD - 1;
  timeouts.ReadIntervalTimeout = MAXDWORD;

  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;

  if (!SetCommTimeouts(hC0C, &timeouts)) {
    TraceLastError("OpenC0C(): SetCommTimeouts()");
    CloseHandle(hC0C);
    return INVALID_HANDLE_VALUE;
  }

  printf("OpenC0C(\"%s\") - OK\n", pPath);

  return hC0C;
}
///////////////////////////////////////////////////////////////
static SOCKET Connect(const char *pAddr, const char *pPort)
{
  const char *pProtoName = "tcp";
  struct sockaddr_in sn;

  memset(&sn, 0, sizeof(sn));
  sn.sin_family = AF_INET;

  struct servent *pServEnt;

  pServEnt = getservbyname(pPort, pProtoName);

  sn.sin_port = pServEnt ? pServEnt->s_port : htons((u_short)atoi(pPort));

  sn.sin_addr.S_un.S_addr = inet_addr(pAddr);

  if (sn.sin_addr.S_un.S_addr == INADDR_NONE) {
    const struct hostent *pHostEnt = gethostbyname(pAddr);

    if (!pHostEnt) {
      TraceLastError("Connect(): gethostbyname(\"%s\")", pAddr);
      return INVALID_SOCKET;
    }

    memcpy(&sn.sin_addr, pHostEnt->h_addr, pHostEnt->h_length);
  }

  const struct protoent *pProtoEnt;
  
  pProtoEnt = getprotobyname(pProtoName);

  if (!pProtoEnt) {
    TraceLastError("Connect(): getprotobyname(\"%s\")", pProtoName);
    return INVALID_SOCKET;
  }

  SOCKET hSock = socket(AF_INET, SOCK_STREAM, pProtoEnt->p_proto);

  if (hSock == INVALID_SOCKET) {
    TraceLastError("Connect(): socket()");
    return INVALID_SOCKET;
  }

  if (connect(hSock, (struct sockaddr *)&sn, sizeof(sn)) == SOCKET_ERROR) {
    TraceLastError("Connect(): connect()");
    closesocket(hSock);
    return INVALID_SOCKET;
  }

  printf("Connect(\"%s\", \"%s\") - OK\n", pAddr, pPort);


  return hSock;
}

static void Disconnect(SOCKET hSock)
{
  if (shutdown(hSock, SD_BOTH) != 0)
    TraceLastError("Disconnect(): shutdown()");

  if (closesocket(hSock) != 0)
    TraceLastError("Disconnect(): closesocket()");

  printf("Disconnect() - OK\n");
}
///////////////////////////////////////////////////////////////
static void Usage(const char *pProgName)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "    %s [options] \\\\.\\<com port> <host addr> <host port>\n", pProgName);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "    --telnet              - use Telnet protocol.\n");
  exit(1);
}
///////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
  enum {prNone, prTelnet} protocol = prNone;
  char **pArgs = &argv[1];

  while (argc > 1) {
    if (**pArgs != '-')
      break;

    if (!strcmp(*pArgs, "--telnet")) {
      protocol = prTelnet;
      pArgs++;
      argc--;
    } else {
      fprintf(stderr, "Unknown option %s\n", *pArgs);
      Usage(argv[0]);
    }
  }

  if (argc != 4)
    Usage(argv[0]);

  HANDLE hC0C = OpenC0C(pArgs[0]);

  if (hC0C == INVALID_HANDLE_VALUE) {
    return 2;
  }

  WSADATA wsaData;

  WSAStartup(MAKEWORD(1, 1), &wsaData);

  while (WaitComReady(hC0C)) {
    SOCKET hSock = Connect(pArgs[1], pArgs[2]);

    if (hSock == INVALID_SOCKET)
      break;

    Protocol *pProtocol;

    switch (protocol) {
    case prTelnet:
      pProtocol = new TelnetProtocol(10, 10);
      break;
    default:
      pProtocol = new Protocol(10, 10);
    };

    InOut(hC0C, hSock, *pProtocol);

    delete pProtocol;

    Disconnect(hSock);
  }

  CloseHandle(hC0C);
  WSACleanup();
  return 0;
}
///////////////////////////////////////////////////////////////
