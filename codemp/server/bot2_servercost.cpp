/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Opt-in native research timing. All file output happens after recording.
#include "server.h"
#include "bot2_servercost.h"
#include <chrono>

void Bot2_ServerCostFrame(bool ending, int gameTime) {
    static cvar_t *recording=NULL;
    static bool started=false,used=false,overflow=false;
    static int beganGameTime=0,count=0;
    static std::chrono::steady_clock::time_point began;
    static struct {int time,advance;double us;} samples[4096];
    if(!recording)recording=Cvar_Get("bot_moveCostRecording","0",0);
    if(!ending) {
        started=recording->integer!=0;
        if(started){beganGameTime=gameTime;began=std::chrono::steady_clock::now();used=true;}
        else if(used && count) {
            fileHandle_t file=0;char line[256];
            FS_FOpenFileByMode("server_movement_cost.csv",&file,FS_READ);
            if(file){FS_FCloseFile(file);Com_Printf("[SERVERCOST] REFUSED existing capture\n");}
            else {
                FS_FOpenFileByMode("server_movement_cost.csv",&file,FS_WRITE);
                if(file) {
                    const char *header="level_time,simulation_advance_ms,server_frame_us\n";
                    FS_Write(header,strlen(header),file);
                    for(int i=0;i<count;i++) {
                        Com_sprintf(line,sizeof(line),"%d,%d,%.3f\n",samples[i].time,samples[i].advance,samples[i].us);
                        FS_Write(line,strlen(line),file);
                    }
                    FS_FCloseFile(file);
                    Com_Printf("[SERVERCOST] DONE samples=%d overflow=%d scope=SV_Frame_including_native_movement_and_snapshot_work\n",count,overflow?1:0);
                } else Com_Printf("[SERVERCOST] REFUSED capture open failed\n");
            }
            count=0;used=false;overflow=false;
        }
    } else if(started && gameTime>beganGameTime) {
        double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count();
        if(count<(int)ARRAY_LEN(samples)) {
            samples[count].time=gameTime;samples[count].advance=gameTime-beganGameTime;samples[count++].us=us;
        } else overflow=true;
        started=false;
    }
}
