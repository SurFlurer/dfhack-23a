/*
https://github.com/peterix/dfhack
Copyright (c) 2009-2012 Petr Mrázek (peterix@gmail.com)

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


#include "Internal.h"
#include <string>
#include <vector>
#include <map>
#include <cstring>
using namespace std;

#include "modules/World.h"
#include "MemAccess.h"
#include "VersionInfo.h"
#include "Types.h"
#include "Error.h"
#include "ModuleFactory.h"
#include "Core.h"

#include "modules/Maps.h"

#include "MiscUtils.h"

#include "VTableInterpose.h"

#include "DataDefs.h"
#include "df/world.h"
#include "df/historical_figure.h"
#include "df/map_block.h"

using namespace DFHack;
using namespace df::enums;

using df::global::world;

static int next_persistent_id = 0;
static std::multimap<std::string, int> persistent_index;
typedef std::pair<std::string, int> T_persistent_item;

bool World::ReadPauseState()
{
    return DF_GLOBAL_VALUE(pause_state, false);
}

void World::SetPauseState(bool paused)
{
    bool dummy;
    DF_GLOBAL_VALUE(pause_state, dummy) = paused;
}

uint32_t World::ReadCurrentYear()
{
    return DF_GLOBAL_VALUE(cur_year, 0);
}

uint32_t World::ReadCurrentTick()
{
    return DF_GLOBAL_VALUE(cur_year_tick, 0);
}

bool World::ReadGameMode(t_gamemodes& rd)
{
    if(df::global::gamemode && df::global::gametype)
    {
        rd.g_mode = (DFHack::GameMode)*df::global::gamemode;
        rd.g_type = (DFHack::GameType)*df::global::gametype;
        return true;
    }
    return false;
}
bool World::WriteGameMode(const t_gamemodes & wr)
{
    if(df::global::gamemode && df::global::gametype)
    {
        *df::global::gamemode = wr.g_mode;
        *df::global::gametype = wr.g_type;
        return true;
    }
    return false;
}

/*
FIXME: Japa said that he had to do this with the time stuff he got from here
       Investigate.

    currentYear = Wold->ReadCurrentYear();
    currentTick = Wold->ReadCurrentTick();
    currentMonth = (currentTick+9)/33600;
    currentDay = ((currentTick+9)%33600)/1200;
    currentHour = ((currentTick+9)-(((currentMonth*28)+currentDay)*1200))/50;
    currentTickRel = (currentTick+9)-(((((currentMonth*28)+currentDay)*24)+currentHour)*50);
    */

// FIX'D according to this:
/*
World::ReadCurrentMonth and World::ReadCurrentDay
« Sent to: peterix on: June 04, 2010, 04:44:30 »
« You have forwarded or responded to this message. »

Shouldn't these be /28 and %28 instead of 24?  There're 28 days in a DF month.
Using 28 and doing the calculation on the value stored at the memory location
specified by memory.xml gets me the current month/date.
*/
uint32_t World::ReadCurrentMonth()
{
    return ReadCurrentTick() / 1200 / 28;
}

uint32_t World::ReadCurrentDay()
{
    return ((ReadCurrentTick() / 1200) % 28) + 1;
}

uint8_t World::ReadCurrentWeather()
{
    if (df::global::current_weather)
        return (*df::global::current_weather);
    return 0;
}

void World::SetCurrentWeather(uint8_t weather)
{
    if (df::global::current_weather)
        *df::global::current_weather = (df::weather_type)weather;
}

string World::ReadWorldFolder()
{
    return stl_sprintf("region%i", world->world_data.save_folder_number);
}

static PersistentDataItem dataFromHFig(df::historical_figure *hfig)
{
    return PersistentDataItem(hfig->id, hfig->name.first_name, &hfig->name.nickname, hfig->name.parts);
}

void World::ClearPersistentCache()
{
    next_persistent_id = 0;
    persistent_index.clear();
}

static bool BuildPersistentCache()
{
    if (next_persistent_id)
        return true;
    if (!Core::getInstance().isWorldLoaded())
        return false;

    stl::vector<df::historical_figure*> &hfvec = df::historical_figure::get_vector();

    // Determine the next entry id as min(-100, lowest_id-1)
    next_persistent_id = -100;

    if (hfvec.size() > 0 && hfvec[0]->id <= -100)
        next_persistent_id = hfvec[0]->id-1;

    // Add the entries to the lookup table
    persistent_index.clear();

    for (size_t i = 0; i < hfvec.size() && hfvec[i]->id <= -100; i++)
    {
        if (!hfvec[i]->name.has_name || hfvec[i]->name.first_name.empty())
            continue;

        persistent_index.insert(T_persistent_item(hfvec[i]->name.first_name, -hfvec[i]->id));
    }

    return true;
}

PersistentDataItem World::AddPersistentData(const std::string &key)
{
    if (!BuildPersistentCache() || key.empty())
        return PersistentDataItem();

    stl::vector<df::historical_figure*> &hfvec = df::historical_figure::get_vector();

    df::historical_figure *hfig = new df::historical_figure();
    hfig->id = next_persistent_id;
    hfig->name.has_name = true;
    hfig->name.first_name = key;
    memset(hfig->name.parts, 0xFF, sizeof(hfig->name.parts));

    if (!hfvec.empty())
        hfig->id = std::min(hfig->id, hfvec[0]->id-1);
    next_persistent_id = hfig->id-1;

    hfvec.insert(hfvec.begin(), hfig);

    persistent_index.insert(T_persistent_item(key, -hfig->id));

    return dataFromHFig(hfig);
}

PersistentDataItem World::GetPersistentData(const std::string &key)
{
    if (!BuildPersistentCache())
        return PersistentDataItem();

    auto it = persistent_index.find(key);
    if (it != persistent_index.end())
        return GetPersistentData(it->second);

    return PersistentDataItem();
}

PersistentDataItem World::GetPersistentData(int entry_id)
{
    if (entry_id < 100)
        return PersistentDataItem();

    auto hfig = df::historical_figure::find(-entry_id);
    if (hfig && hfig->name.has_name)
        return dataFromHFig(hfig);

    return PersistentDataItem();
}

PersistentDataItem World::GetPersistentData(const std::string &key, bool *added)
{
    if (added) *added = false;

    PersistentDataItem rv = GetPersistentData(key);

    if (!rv.isValid())
    {
        if (added) *added = true;
        rv = AddPersistentData(key);
    }

    return rv;
}

void World::GetPersistentData(std::vector<PersistentDataItem> *vec, const std::string &key, bool prefix)
{
    vec->clear();

    if (!BuildPersistentCache())
        return;

    auto eqrange = persistent_index.equal_range(key);

    if (prefix)
    {
        if (key.empty())
        {
            eqrange.first = persistent_index.begin();
            eqrange.second = persistent_index.end();
        }
        else
        {
            std::string bound = key;
            if (bound[bound.size()-1] != '/')
                bound += "/";
            eqrange.first = persistent_index.lower_bound(bound);

            bound[bound.size()-1]++;
            eqrange.second = persistent_index.lower_bound(bound);
        }
    }

    for (auto it = eqrange.first; it != eqrange.second; ++it)
    {
        auto hfig = df::historical_figure::find(-it->second);
        if (hfig && hfig->name.has_name)
            vec->push_back(dataFromHFig(hfig));
    }
}

bool World::DeletePersistentData(const PersistentDataItem &item)
{
    int id = item.raw_id();
    if (id > -100)
        return false;
    if (!BuildPersistentCache())
        return false;

    stl::vector<df::historical_figure*> &hfvec = df::historical_figure::get_vector();

    auto eqrange = persistent_index.equal_range(item.key());

    for (auto it2 = eqrange.first; it2 != eqrange.second; )
    {
        auto it = it2; ++it2;

        if (it->second != -id)
            continue;

        persistent_index.erase(it);

        int idx = binsearch_index(hfvec, id);

        if (idx >= 0) {
            delete hfvec[idx];
            hfvec.erase(hfvec.begin()+idx);
        }

        return true;
    }

    return false;
}

df::tile_bitmask *World::getPersistentTilemask(const PersistentDataItem &item, df::map_block *block, bool create)
{
    // this cannot work
    return NULL;
}

bool World::deletePersistentTilemask(const PersistentDataItem &item, df::map_block *block)
{
    return false;
}
