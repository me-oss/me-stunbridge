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

#ifndef STUNBRIDGE_H_
#define STUNBRIDGE_H_

#define MAX_UDT_DATA_SESSION 1
int camera_is_available();
int udt_start_session(char* dest_ip, int dest_port,int local_port, char* stream_id, char* randomstr);
int udt_stop_session(char* stream_id);
typedef enum {VAAPPLET,AAPPLET,PASSTHROUGH_COMMAND,CLOSESESSION_COMMAND} answer_t;
#endif /* MLSCRC_H_ */
