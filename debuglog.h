/*
* Copyright (C) 2014 Unyphi. All rights reserved.
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DEBUGLOG_H_
#define DEBUGLOG_H_

#include <syslog.h>

void startlog(char* name);
void printlog(const char* format, ...);
#ifndef SIMULATION
#undef printf
#define printf printlog
#endif

#endif /* MLSCRC_H_ */
