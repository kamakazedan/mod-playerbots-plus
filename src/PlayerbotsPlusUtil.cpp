#include "PlayerbotsPlusUtil.h"

#include "DBCStructure.h"
#include "Group.h"
#include "Player.h"
#include "WorldSession.h"

#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"


bool PbpIsBotPlayer(Player* player)
{
    if (!player)
        return false;

    // Right from the session's construction, unlike the AI lookup below.
    if (WorldSession* session = player->GetSession())
        if (session->IsHeadless())
            return true;

    PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    return ai && ai->IsBotAI();
}

bool PbpGroupHasRealPlayer(Player* who)
{
    Group* group = who ? who->GetGroup() : nullptr;
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (PbpIsRealPlayer(ref->GetSource()))
            return true;
    return false;
}

PlayerbotAI* PbpAiOf(Player* player)
{
    return player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
}

std::string PbpZoneName(Player* bot)
{
    PlayerbotAI* ai = PbpAiOf(bot);
    if (AreaTableEntry const* zone = ai ? ai->GetCurrentZone() : nullptr)
        return PlayerbotAI::GetLocalizedAreaName(zone);
    return {};
}
