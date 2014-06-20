
#include "Console.h"
#include "Core.h"
#include "DataDefs.h"
#include "Export.h"
#include "PluginManager.h"
#include "modules/EventManager.h"
#include "modules/Maps.h"

#include "df/creature_raw.h"
#include "df/creature_raw_flags.h"
#include "df/profession.h"
#include "df/unit.h"
#include "df/world.h"

#include <map>
#include <vector>

using namespace DFHack;
using namespace std;

static int32_t howOften = 10000;
static int32_t popcap = 100;
static int32_t pregtime = 200000;
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

command_result petcapRemover (color_ostream &out, std::vector <std::string> & parameters);

DFHACK_PLUGIN("petcapRemover");

DFhackCExport command_result plugin_init ( color_ostream &out, std::vector <PluginCommand> &commands)
{
    commands.push_back(PluginCommand(
        "petcapRemover",
        "Removes the pet population cap by causing pregnancies.",
        petcapRemover,
        false, //allow non-interactive use
        "petcapRemover\n"
        " does pregnancies now and schedules the next check\n"
        "petcapRemover every n\n"
        " set how often in ticks the plugin checks for possible pregnancies\n"
        "petcapRemover cap n\n"
        " sets the new cap to n. if n = 0, no cap. Caps between 1 and 50 effectively don't do anything because normal DF pregnancies will continue to happen below that cap.\n"
        "petcapRemover pregtime n\n"
        " sets the pregnancy duration to n ticks. Natural pregnancies are 300000 ticks for the current race and 200000 ticks for everyone else.\n"
    ));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}

bool impregnate(df::unit* female, df::unit* male);
void impregnateMany() {
    map<int32_t, vector<int32_t> > males;
    map<int32_t, vector<int32_t> > females;
    map<int32_t, int32_t> popcount;
    auto units = df::global::world->units.all;
    for ( size_t a = 0; a < units.size(); a++ ) {
        df::unit* unit = units[a];
        if ( unit->flags1.bits.dead || unit->flags1.bits.active_invader || unit->flags2.bits.underworld || unit->flags2.bits.visitor_uninvited || unit->flags2.bits.visitor )
            continue;
        popcount[unit->race]++;
        if ( unit->relations.pregnancy_timer ) {
            //already pregnant
            //for player convenience and population stability, count the fetus toward the population cap
            popcount[unit->race]++;
            continue;
        }
        if ( unit->flags1.bits.caged )
            continue;
        int32_t race = unit->race;
        df::creature_raw* creatureRaw = df::global::world->raws.creatures[race];
        //must have PET or PET_EXOTIC
        if ( !(creatureRaw->flags.is_set(df::enums::creature_raw_flags::PET) || creatureRaw->flags.is_set(df::enums::creature_raw_flags::PET_EXOTIC) ) )
            continue;
        //check for adulthood
        if ( unit->profession == df::enums::profession::CHILD || unit->profession == df::enums::profession::BABY )
            continue;
        if ( unit->sex == 1 )
            males[unit->race].push_back(a);
        else
            females[unit->race].push_back(a);
    }
    
    for ( auto i = females.begin(); i != females.end(); i++ ) {
        int32_t race = i->first;
        vector<int32_t>& femalesList = i->second;
        for ( size_t a = 0; a < femalesList.size(); a++ ) {
            if ( popcap > 0 && popcount[race] >= popcap )
                break;
            vector<int32_t> compatibles;
            df::coord pos1 = units[femalesList[a]]->pos;
            
            if ( males.find(i->first) == males.end() )
                continue;
            
            vector<int32_t>& malesList = males[i->first];
            for ( size_t b = 0; b < malesList.size(); b++ ) {
                df::coord pos2 = units[malesList[b]]->pos;
                if ( Maps::canWalkBetween(pos1,pos2) )
                    compatibles.push_back(malesList[b]);
            }
            if ( compatibles.empty() )
                continue;
            
            size_t maleIndex = (size_t)(compatibles.size()*((float)rand() / (1+(float)RAND_MAX)));
            if ( impregnate(units[femalesList[a]], units[compatibles[maleIndex]]) )
                popcount[race]++;
        }
    }
}

bool impregnate(df::unit* female, df::unit* male) {
    if ( !female || !male )
        return false;
    if ( female->relations.pregnancy_timer )
        return false;
    
    female->relations.pregnancy_timer = pregtime; //300000 for dwarves
    return true;
}

void tickHandler(color_ostream& out, void* data) {
    if ( !is_enabled )
        return;
    CoreSuspender suspend;
    impregnateMany();
    
    EventManager::unregisterAll(plugin_self);
    EventManager::EventHandler handle(tickHandler, howOften);
    EventManager::registerTick(handle, howOften, plugin_self);
}

command_result petcapRemover (color_ostream &out, std::vector <std::string> & parameters)
{
    CoreSuspender suspend;
    
    for ( size_t a = 0; a < parameters.size(); a++ ) {
        if ( parameters[a] == "every" ) {
            if ( a+1 >= parameters.size() )
                return CR_WRONG_USAGE;
            int32_t old = howOften;
            howOften = atoi(parameters[a+1].c_str());
            if (howOften < -1) {
                howOften = old;
                return CR_WRONG_USAGE;
            }
            a++;
            continue;
        } else if ( parameters[a] == "cap" ) {
            if ( a+1 >= parameters.size() )
                return CR_WRONG_USAGE;
            int32_t old = popcap;
            popcap = atoi(parameters[a+1].c_str());
            if ( popcap < 0 ) {
                popcap = old;
                return CR_WRONG_USAGE;
            }
            a++;
            continue;
        } else if ( parameters[a] == "pregtime" ) {
            if ( a+1 >= parameters.size() )
                return CR_WRONG_USAGE;
            int32_t old = pregtime;
            pregtime = atoi(parameters[a+1].c_str());
            if ( pregtime <= 0 ) {
                pregtime = old;
                return CR_WRONG_USAGE;
            }
            a++;
            continue;
        }
        out.print("%s, line %d: invalid argument: %s\n", __FILE__, __LINE__, parameters[a].c_str());
        return CR_WRONG_USAGE;
    }
    
    if ( howOften < 0 ) {
        is_enabled = false;
        return CR_OK;
    }
    
    is_enabled = true;
    EventManager::unregisterAll(plugin_self);
    EventManager::EventHandler handle(tickHandler, howOften);
    EventManager::registerTick(handle, howOften, plugin_self);
    out.print("petcapRemover: howOften = every %d ticks, popcap per species = %d, preg time = %d ticks.\n", howOften, popcap, pregtime);
    
    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable)
{
    if (enable != is_enabled)
    {
        is_enabled = enable;
        if ( !is_enabled ) {
            EventManager::unregisterAll(plugin_self);
        }
    }

    return CR_OK;
}

