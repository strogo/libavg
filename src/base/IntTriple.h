//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2008 Ulrich von Zadow
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

#ifndef _IntTriple_H_
#define _IntTriple_H_	

#include "../api.h"

#include <ostream>
#include <istream>
#include <vector>



// Fix for non-C99 win compilers up to MSVC++2008
#if defined _MSC_VER && _MSC_VER <= 1500
#define isinf(x) (!_finite(x))
#define isnan(x) (_isnan(x))
#endif

namespace avg {

class IntTriple
{
public:
    int x;
    int y;
    int z; 

  	IntTriple();
    IntTriple(int X,int Y, int Z);
  	IntTriple(const IntTriple & p);
    IntTriple(const std::vector<int>& v);
    ~IntTriple();
  
};

std::ostream& operator<<( std::ostream& os, const IntTriple &p);
std::istream& operator>>(std::istream& is, IntTriple& p);


}

#endif
