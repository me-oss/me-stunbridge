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

#ifndef RELAY_H_
#define RELAY_H_
int relay_compute_sessionkey(char* randomnumber, char* dest_ipHexStr, char* sessionkey);
int relay_start_session(char* str);
int relay_stop_session(char* stream_id);
int relay_is_running();

#endif /* MLSCRC_H_ */
