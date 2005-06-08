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
 * Revision 1.2  2005/06/08 07:40:23  vfrolov
 * Added missing DataStream::busy initialization
 *
 * Revision 1.1  2005/06/06 15:19:02  vfrolov
 * Initial revision
 *
 *
 */

#ifndef _UTILS_H
#define _UTILS_H

///////////////////////////////////////////////////////////////
class ChunkStream
{
  public:
    ChunkStream() : first(0), last(0) {}

    int write(const void *pBuf, int count);
    int read(void *pBuf, int count);

  private:
    char data[256];
    int first;
    int last;

    ChunkStream *pNext;

  friend class ChunkStreamQ;
};
///////////////////////////////////////////////////////////////
class ChunkStreamQ
{
  public:
    ChunkStreamQ() : pFirst(NULL), pLast(NULL) {}

    void toQueue(ChunkStream *pChunk);
    ChunkStream *fromQueue();

    ChunkStream *first() { return pFirst; }
    ChunkStream *last() { return pLast; }

  private:
    ChunkStream *pFirst;
    ChunkStream *pLast;
};
///////////////////////////////////////////////////////////////
class DataStream
{
  public:
    DataStream(int _threshold = 0)
      : busy(0), threshold(_threshold), eof(FALSE) {}
    ~DataStream() { DataStream::Clean(); }

    int PutData(const void *pBuf, int count);
    int GetData(void *pBuf, int count);
    void PutEof() { eof = TRUE; }
    BOOL isFull() const { return threshold && threshold < busy; }

  protected:
    void Clean();

  private:
    ChunkStreamQ bufQ;
    int busy;

    int threshold;
    BOOL eof;
};
///////////////////////////////////////////////////////////////
class Protocol
{
  public:
    Protocol(int _thresholdSend = 0, int _thresholdWrite = 0)
      : streamSendRead(_thresholdSend), streamWriteRecv(_thresholdWrite) {}

    virtual int Send(const void *pBuf, int count);
    int SendRaw(const void *pBuf, int count) { return streamSendRead.PutData(pBuf, count); }
    void SendEof() { streamSendRead.PutEof(); }
    BOOL isSendFull() const { return streamSendRead.isFull(); }
    int Read(void *pBuf, int count) { return streamSendRead.GetData(pBuf, count); }

    virtual int Write(const void *pBuf, int count);
    int WriteRaw(const void *pBuf, int count) { return streamWriteRecv.PutData(pBuf, count); }
    void WriteEof() { streamWriteRecv.PutEof(); }
    BOOL isWriteFull() const { return streamWriteRecv.isFull(); }
    int Recv(void *pBuf, int count) { return streamWriteRecv.GetData(pBuf, count); }

  private:
    DataStream streamSendRead;
    DataStream streamWriteRecv;
};
///////////////////////////////////////////////////////////////

#endif  // _UTILS_H
