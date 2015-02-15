//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2014 Ulrich von Zadow
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//  Current versions can be found at www.libavg.de
//

#ifndef _ProfilingZoneID_H_ 
#define _ProfilingZoneID_H_

#include "../api.h"

#include <string>

namespace avg {

class ThreadProfiler;

class AVG_API ProfilingZoneID
{
public:
    ProfilingZoneID(const std::string& sName);
    ~ProfilingZoneID();
    
    const std::string& getName() const;
    ThreadProfiler* getProfiler();

private:
    std::string m_sName;
    ThreadProfiler* m_pProfiler;
};

}

#endif
