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
 * Revision 1.1  2005/05/30 10:02:33  vfrolov
 * Initial revision
 *
 *
 */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

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
static void InOut(HANDLE hC0C, SOCKET hSock)
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

  char cbufRead[1024];
  DWORD cbufReadDone = 0;
  BOOL waitingRead = FALSE;
  DWORD cbufSendDone = 0;
  BOOL waitingSend = FALSE;
  DWORD sent;

  char cbufRecv[1];
  DWORD cbufRecvDone = 0;
  BOOL waitingRecv = FALSE;
  DWORD cbufWriteDone = 0;
  BOOL waitingWrite = FALSE;
  DWORD written;

  BOOL waitingStat = FALSE;

  while (!stop) {
    if (!waitingRead && !waitingSend) {
      if (cbufSendDone < cbufReadDone) {
        if (!WriteFile((HANDLE)hSock, cbufRead + cbufSendDone, cbufReadDone - cbufSendDone, &sent, &overlaps[EVENT_SENT])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hSock)");
            break;
          }
        }
        waitingSend = TRUE;
      } else {
        if (!ReadFile(hC0C, cbufRead, sizeof(cbufRead), &cbufReadDone, &overlaps[EVENT_READ])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): ReadFile(hC0C)");
            break;
          }
        }
        waitingRead = TRUE;
      }
    }

    if (!waitingRecv && !waitingWrite) {
      if (cbufWriteDone < cbufRecvDone) {
        if (!WriteFile(hC0C, cbufRecv + cbufWriteDone, cbufRecvDone - cbufWriteDone, &written, &overlaps[EVENT_WRITTEN])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): WriteFile(hC0C)");
            break;
          }
        }
        waitingWrite = TRUE;
      } else {
        if (!ReadFile((HANDLE)hSock, cbufRecv, sizeof(cbufRecv), &cbufRecvDone, &overlaps[EVENT_RECEIVED])) {
          if (::GetLastError() != ERROR_IO_PENDING) {
            TraceLastError("InOut(): ReadFile(hSock)");
            break;
          }
        }
        waitingRecv = TRUE;
      }
    }

    if (!waitingStat) {
      DWORD maskStat;

      if (!WaitCommEvent(hC0C, &maskStat, &overlaps[EVENT_STAT])) {
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
      DWORD undef;

      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_READ:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_READ], &cbufReadDone, FALSE)) {
          TraceLastError("InOut(): GetOverlappedResult(EVENT_READ)");
          stop = TRUE;
          break;
        }
        ResetEvent(hEvents[EVENT_READ]);
        waitingRead = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_SENT:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_SENT], &sent, FALSE)) {
          TraceLastError("InOut(): GetOverlappedResult(EVENT_SENT)");
          stop = TRUE;
          break;
        }
        ResetEvent(hEvents[EVENT_SENT]);
        cbufSendDone += sent;
        if (cbufSendDone >= cbufReadDone)
          cbufSendDone = cbufReadDone = 0;
        waitingSend = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_RECEIVED:
        if (!GetOverlappedResult((HANDLE)hSock, &overlaps[EVENT_RECEIVED], &cbufRecvDone, FALSE)) {
          TraceLastError("InOut(): GetOverlappedResult(EVENT_RECEIVED)");
          stop = TRUE;
          break;
        }
        ResetEvent(hEvents[EVENT_RECEIVED]);
        if (!cbufRecvDone) {
          printf("Received EOF\n");
          break;
        }
        waitingRecv = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_WRITTEN:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_WRITTEN], &written, FALSE)) {
          TraceLastError("InOut(): GetOverlappedResult(EVENT_WRITTEN)");
          stop = TRUE;
          break;
        }
        ResetEvent(hEvents[EVENT_WRITTEN]);
        cbufWriteDone += written;
        if (cbufWriteDone >= cbufRecvDone)
          cbufWriteDone = cbufRecvDone = 0;
        waitingWrite = FALSE;
        break;
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &undef, FALSE)) {
          TraceLastError("InOut(): GetOverlappedResult(EVENT_STAT)");
          stop = TRUE;
          break;
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

  BOOL waitingStat = FALSE;

  while (!fault) {
    if (!waitingStat) {
      DWORD maskStat;

      if (!WaitCommEvent(hC0C, &maskStat, &overlaps[EVENT_STAT])) {
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
      DWORD undef;

      switch (WaitForMultipleObjects(EVENT_NUM, hEvents, FALSE, 5000)) {
      case WAIT_OBJECT_0 + EVENT_STAT:
        if (!GetOverlappedResult(hC0C, &overlaps[EVENT_STAT], &undef, FALSE)) {
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

  dcb.fOutxCtsFlow = TRUE;
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
  struct sockaddr_in sn;

  memset(&sn, 0, sizeof(sn));
  sn.sin_family = AF_INET;
  sn.sin_port = htons((u_short)atoi(pPort));

  unsigned long addr = inet_addr(pAddr);
  const struct hostent *pHostEnt = (addr == INADDR_NONE) ?
                                                 gethostbyname(pAddr) :
                                                 gethostbyaddr((const char *)&addr, 4, AF_INET);

  if (!pHostEnt) {
    TraceLastError("Connect(): gethostbyname(\"%s\")", pAddr);
    return INVALID_SOCKET;
  }

  memcpy(&sn.sin_addr, pHostEnt->h_addr, pHostEnt->h_length);

  const struct protoent *pProtoEnt;
  
  pProtoEnt = getprotobyname("tcp");

  if (!pProtoEnt) {
    TraceLastError("Connect(): getprotobyname(\"tcp\")");
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
int main(int argc, char* argv[])
{
  if (argc != 4) {
    printf("Usage:\n");
    printf("    %s \\\\.\\<com port> <host addr> <host port>\n", argv[0]);
    return 1;
  }

  HANDLE hC0C = OpenC0C(argv[1]);

  if (hC0C == INVALID_HANDLE_VALUE) {
    return 2;
  }

  WSADATA wsaData;

  WSAStartup(MAKEWORD(1, 1), &wsaData);

  while (WaitComReady(hC0C)) {
    SOCKET hSock = Connect(argv[2], argv[3]);

    if (hSock == INVALID_SOCKET)
      break;

    InOut(hC0C, hSock);
    Disconnect(hSock);
  }

  CloseHandle(hC0C);
  WSACleanup();
  return 0;
}
///////////////////////////////////////////////////////////////
