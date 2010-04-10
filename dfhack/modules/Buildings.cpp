/*
www.sourceforge.net/projects/dfhack
Copyright (c) 2009 Petr Mrázek (peterix), Kenneth Ferland (Impaler[WrG]), dorf

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#include "DFCommonInternal.h"
#include "../private/APIPrivate.h"
#include "modules/Translation.h"
#include "DFMemInfo.h"
#include "DFProcess.h"
#include "DFVector.h"
#include "DFTypes.h"
#include "modules/Buildings.h"

using namespace DFHack;

//raw
struct t_building_df40d
{
    uint32_t vtable;
    uint32_t x1;
    uint32_t y1;
    uint32_t centerx;
    uint32_t x2;
    uint32_t y2;
    uint32_t centery;
    uint32_t z;
    uint32_t height;
    t_matglossPair material;
    // not complete
};

struct Buildings::Private
{
    uint32_t buildings_vector;
    // translation
    DfVector * p_bld;
    
    APIPrivate *d;
    bool Inited;
    bool Started;
};

Buildings::Buildings(APIPrivate * d_)
{
    d = new Private;
    d->d = d_;
    d->Inited = d->Started = false;
    memory_info * mem = d->d->offset_descriptor;
    d->buildings_vector = mem->getAddress ("buildings_vector");
    d->Inited = true;
}

Buildings::~Buildings()
{
    if(d->Started)
        Finish();
    delete d;
}

bool Buildings::Start(uint32_t & numbuildings)
{
    d->p_bld = new DfVector (g_pProcess, d->buildings_vector, 4);
    numbuildings = d->p_bld->getSize();
    d->Started = true;
    return true;
}

bool Buildings::Read (const uint32_t index, t_building & building)
{
    if(!d->Started)
        return false;
    t_building_df40d bld_40d;

    // read pointer from vector at position
    uint32_t temp = * (uint32_t *) d->p_bld->at (index);
    //d->p_bld->read(index,(uint8_t *)&temp);

    //read building from memory
    g_pProcess->read (temp, sizeof (t_building_df40d), (uint8_t *) &bld_40d);

    // transform
    int32_t type = -1;
    d->d->offset_descriptor->resolveObjectToClassID (temp, type);
    building.origin = temp;
    building.vtable = bld_40d.vtable;
    building.x1 = bld_40d.x1;
    building.x2 = bld_40d.x2;
    building.y1 = bld_40d.y1;
    building.y2 = bld_40d.y2;
    building.z = bld_40d.z;
    building.material = bld_40d.material;
    building.type = type;
    return true;
}

bool Buildings::Finish()
{
    if(d->p_bld)
    {
        delete d->p_bld;
        d->p_bld = NULL;
    }
    d->Started = false;
}