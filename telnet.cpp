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

#include <winsock2.h>
#include <windows.h>

#include <stdio.h>

#include "utils.h"
#include "telnet.h"
///////////////////////////////////////////////////////////////
enum {
  cdWILL = 251,
  cdWONT = 252,
  cdDO   = 253,
  cdDONT = 254,
  cdIAC  = 255,
};

static const char *code2name(unsigned code)
{
  switch (code) {
    case cdWILL:
      return "WILL";
      break;
    case cdWONT:
      return "WONT";
      break;
    case cdDO:
      return "DO";
      break;
    case cdDONT:
      return "DONT";
      break;
  }
  return "UNKNOWN";
}
///////////////////////////////////////////////////////////////
enum {
  opEcho        = 1,
};
///////////////////////////////////////////////////////////////
enum {
  stData,
  stCode,
  stOption,
};
///////////////////////////////////////////////////////////////
TelnetProtocol::TelnetProtocol(int _thresholdSend, int _thresholdWrite)
  : Protocol(_thresholdSend, _thresholdWrite),
    state(stData)
{
  options[opEcho].remoteOptionState = OptionState::osNo;
}

int TelnetProtocol::Write(const void *pBuf, int count)
{
  for (int i = 0 ; i < count ; i++) {
    BYTE ch = ((const BYTE *)pBuf)[i];

    switch (state) {
      case stData:
        if (ch == cdIAC)
          state = stCode;
        else
          WriteRaw(&ch, 1);
        break;
      case stCode:
        switch (ch) {
          case cdIAC:
            WriteRaw(&ch, 1);
            break;
          case cdWILL:
          case cdWONT:
          case cdDO:
          case cdDONT:
            code = ch;
            state = stOption;
            break;
          default:
            printf("RECV: unknown code %u\n", (unsigned)ch);
            state = stData;
        }
        break;
      case stOption:
        printf("RECV: %s %u\n", code2name(code), (unsigned)ch);
        switch (code) {
          case cdWILL:
            switch (options[ch].remoteOptionState) {
              case OptionState::osCant:
                SendOption(cdDONT, ch);
                break;
              case OptionState::osNo:
                options[ch].remoteOptionState = OptionState::osYes;
                SendOption(cdDO, ch);
                break;
              case OptionState::osYes:
                break;
            }
            break;
          case cdWONT:
            switch (options[ch].remoteOptionState) {
              case OptionState::osCant:
              case OptionState::osNo:
                break;
              case OptionState::osYes:
                options[ch].remoteOptionState = OptionState::osNo;
                SendOption(cdDONT, ch);
                break;
            }
            break;
          case cdDO:
            switch (options[ch].localOptionState) {
              case OptionState::osCant:
                SendOption(cdWONT, ch);
                break;
              case OptionState::osNo:
                options[ch].localOptionState = OptionState::osYes;
                SendOption(cdWILL, ch);
                break;
              case OptionState::osYes:
                break;
            }
            break;
          case cdDONT:
            switch (options[ch].localOptionState) {
              case OptionState::osCant:
              case OptionState::osNo:
                break;
              case OptionState::osYes:
                options[ch].localOptionState = OptionState::osNo;
                SendOption(cdWONT, ch);
                break;
            }
            break;
          default:
            printf("RECV: %u %u (ignore)\n", (unsigned)code, (unsigned)ch);
          };
      state = stData;
      break;
    }
  }

  return count;
}

void TelnetProtocol::SendOption(BYTE code, BYTE option)
{
  BYTE buf[3] = {cdIAC, code, option};

  printf("SEND: %s %u\n", code2name(code), (unsigned)option);

  SendRaw(buf, sizeof(buf));
}
///////////////////////////////////////////////////////////////
