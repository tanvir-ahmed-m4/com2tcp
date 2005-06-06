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
 * Revision 1.1  2005/06/06 15:19:02  vfrolov
 * Initial revision
 *
 *
 */

#ifndef _TELNET_H
#define _TELNET_H

///////////////////////////////////////////////////////////////
class TelnetProtocol : public Protocol
{
  public:
    TelnetProtocol(int _thresholdSend = 0, int _thresholdWrite = 0);

    //virtual int Send(const void *pBuf, int count);
    virtual int Write(const void *pBuf, int count);

  protected:
    void SendOption(BYTE code, BYTE option);

    int state;
    int code;

    struct OptionState
    {
      OptionState() : localOptionState(osCant), remoteOptionState(osCant) {}
      enum {osCant, osNo, osYes};
      int localOptionState  : 2;
      int remoteOptionState : 2;
    };

    OptionState options[256];
    DataStream toTelnet;
};
///////////////////////////////////////////////////////////////

#endif  // _TELNET_H
