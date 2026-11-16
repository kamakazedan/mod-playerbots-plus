#include "PlayerbotsPlusHivemind.h"
#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusUtil.h"

#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Log.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "PlayerbotAI.h"

#include <fmt/core.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <utility>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    // Channel::IsOn is private: whether a bot is in a channel is asked the
    // way mod-ollama-chat asks it (OllamaIsOnChannel), through a pointer to
    // the member taken in an explicit instantiation, which access rules
    // leave be.
    using ChannelIsOnFn = bool (Channel::*)(ObjectGuid) const;
    ChannelIsOnFn ChannelIsOnPtr();

    template <ChannelIsOnFn Fn>
    struct ChannelIsOnAccess
    {
        friend ChannelIsOnFn ChannelIsOnPtr() { return Fn; }
    };

    template struct ChannelIsOnAccess<&Channel::IsOn>;

    bool IsOn(Channel const* channel, ObjectGuid guid)
    {
        return channel && (channel->*ChannelIsOnPtr())(guid);
    }

    struct EmoteName
    {
        char const* name;
        uint32_t    id;
    };

    // Every text emote the game has, by its name (TextEmotes, lower case).
    const EmoteName kEmotes[] = {
        { "agree", TEXT_EMOTE_AGREE }, { "amaze", TEXT_EMOTE_AMAZE }, { "angry", TEXT_EMOTE_ANGRY },
        { "apologize", TEXT_EMOTE_APOLOGIZE }, { "applaud", TEXT_EMOTE_APPLAUD }, { "bashful", TEXT_EMOTE_BASHFUL },
        { "beckon", TEXT_EMOTE_BECKON }, { "beg", TEXT_EMOTE_BEG }, { "bite", TEXT_EMOTE_BITE },
        { "bleed", TEXT_EMOTE_BLEED }, { "blink", TEXT_EMOTE_BLINK }, { "blush", TEXT_EMOTE_BLUSH },
        { "bonk", TEXT_EMOTE_BONK }, { "bored", TEXT_EMOTE_BORED }, { "bounce", TEXT_EMOTE_BOUNCE },
        { "brb", TEXT_EMOTE_BRB }, { "bow", TEXT_EMOTE_BOW }, { "burp", TEXT_EMOTE_BURP }, { "bye", TEXT_EMOTE_BYE },
        { "cackle", TEXT_EMOTE_CACKLE }, { "cheer", TEXT_EMOTE_CHEER }, { "chicken", TEXT_EMOTE_CHICKEN },
        { "chuckle", TEXT_EMOTE_CHUCKLE }, { "clap", TEXT_EMOTE_CLAP }, { "confused", TEXT_EMOTE_CONFUSED },
        { "congratulate", TEXT_EMOTE_CONGRATULATE }, { "cough", TEXT_EMOTE_COUGH }, { "cower", TEXT_EMOTE_COWER },
        { "crack", TEXT_EMOTE_CRACK }, { "cringe", TEXT_EMOTE_CRINGE }, { "cry", TEXT_EMOTE_CRY },
        { "curious", TEXT_EMOTE_CURIOUS }, { "curtsey", TEXT_EMOTE_CURTSEY }, { "dance", TEXT_EMOTE_DANCE },
        { "drink", TEXT_EMOTE_DRINK }, { "drool", TEXT_EMOTE_DROOL }, { "eat", TEXT_EMOTE_EAT },
        { "eye", TEXT_EMOTE_EYE }, { "fart", TEXT_EMOTE_FART }, { "fidget", TEXT_EMOTE_FIDGET },
        { "flex", TEXT_EMOTE_FLEX }, { "frown", TEXT_EMOTE_FROWN }, { "gasp", TEXT_EMOTE_GASP },
        { "gaze", TEXT_EMOTE_GAZE }, { "giggle", TEXT_EMOTE_GIGGLE }, { "glare", TEXT_EMOTE_GLARE },
        { "gloat", TEXT_EMOTE_GLOAT }, { "greet", TEXT_EMOTE_GREET }, { "grin", TEXT_EMOTE_GRIN },
        { "groan", TEXT_EMOTE_GROAN }, { "grovel", TEXT_EMOTE_GROVEL }, { "guffaw", TEXT_EMOTE_GUFFAW },
        { "hail", TEXT_EMOTE_HAIL }, { "happy", TEXT_EMOTE_HAPPY }, { "hello", TEXT_EMOTE_HELLO },
        { "hug", TEXT_EMOTE_HUG }, { "hungry", TEXT_EMOTE_HUNGRY }, { "kiss", TEXT_EMOTE_KISS },
        { "kneel", TEXT_EMOTE_KNEEL }, { "laugh", TEXT_EMOTE_LAUGH }, { "laydown", TEXT_EMOTE_LAYDOWN },
        { "message", TEXT_EMOTE_MESSAGE }, { "moan", TEXT_EMOTE_MOAN }, { "moon", TEXT_EMOTE_MOON },
        { "mourn", TEXT_EMOTE_MOURN }, { "no", TEXT_EMOTE_NO }, { "nod", TEXT_EMOTE_NOD },
        { "nosepick", TEXT_EMOTE_NOSEPICK }, { "panic", TEXT_EMOTE_PANIC }, { "peer", TEXT_EMOTE_PEER },
        { "plead", TEXT_EMOTE_PLEAD }, { "point", TEXT_EMOTE_POINT }, { "poke", TEXT_EMOTE_POKE },
        { "pray", TEXT_EMOTE_PRAY }, { "roar", TEXT_EMOTE_ROAR }, { "rofl", TEXT_EMOTE_ROFL },
        { "rude", TEXT_EMOTE_RUDE }, { "salute", TEXT_EMOTE_SALUTE }, { "scratch", TEXT_EMOTE_SCRATCH },
        { "sexy", TEXT_EMOTE_SEXY }, { "shake", TEXT_EMOTE_SHAKE }, { "shout", TEXT_EMOTE_SHOUT },
        { "shrug", TEXT_EMOTE_SHRUG }, { "shy", TEXT_EMOTE_SHY }, { "sigh", TEXT_EMOTE_SIGH },
        { "sit", TEXT_EMOTE_SIT }, { "sleep", TEXT_EMOTE_SLEEP }, { "snarl", TEXT_EMOTE_SNARL },
        { "spit", TEXT_EMOTE_SPIT }, { "stare", TEXT_EMOTE_STARE }, { "surprised", TEXT_EMOTE_SURPRISED },
        { "surrender", TEXT_EMOTE_SURRENDER }, { "talk", TEXT_EMOTE_TALK }, { "talkex", TEXT_EMOTE_TALKEX },
        { "talkq", TEXT_EMOTE_TALKQ }, { "tap", TEXT_EMOTE_TAP }, { "thank", TEXT_EMOTE_THANK },
        { "threaten", TEXT_EMOTE_THREATEN }, { "tired", TEXT_EMOTE_TIRED }, { "victory", TEXT_EMOTE_VICTORY },
        { "wave", TEXT_EMOTE_WAVE }, { "welcome", TEXT_EMOTE_WELCOME }, { "whine", TEXT_EMOTE_WHINE },
        { "whistle", TEXT_EMOTE_WHISTLE }, { "work", TEXT_EMOTE_WORK }, { "yawn", TEXT_EMOTE_YAWN },
        { "boggle", TEXT_EMOTE_BOGGLE }, { "calm", TEXT_EMOTE_CALM }, { "cold", TEXT_EMOTE_COLD },
        { "comfort", TEXT_EMOTE_COMFORT }, { "cuddle", TEXT_EMOTE_CUDDLE }, { "duck", TEXT_EMOTE_DUCK },
        { "insult", TEXT_EMOTE_INSULT }, { "introduce", TEXT_EMOTE_INTRODUCE }, { "jk", TEXT_EMOTE_JK },
        { "lick", TEXT_EMOTE_LICK }, { "listen", TEXT_EMOTE_LISTEN }, { "lost", TEXT_EMOTE_LOST },
        { "mock", TEXT_EMOTE_MOCK }, { "ponder", TEXT_EMOTE_PONDER }, { "pounce", TEXT_EMOTE_POUNCE },
        { "praise", TEXT_EMOTE_PRAISE }, { "purr", TEXT_EMOTE_PURR }, { "puzzle", TEXT_EMOTE_PUZZLE },
        { "raise", TEXT_EMOTE_RAISE }, { "ready", TEXT_EMOTE_READY }, { "shimmy", TEXT_EMOTE_SHIMMY },
        { "shiver", TEXT_EMOTE_SHIVER }, { "shoo", TEXT_EMOTE_SHOO }, { "slap", TEXT_EMOTE_SLAP },
        { "smirk", TEXT_EMOTE_SMIRK }, { "sniff", TEXT_EMOTE_SNIFF }, { "snub", TEXT_EMOTE_SNUB },
        { "soothe", TEXT_EMOTE_SOOTHE }, { "stink", TEXT_EMOTE_STINK }, { "taunt", TEXT_EMOTE_TAUNT },
        { "tease", TEXT_EMOTE_TEASE }, { "thirsty", TEXT_EMOTE_THIRSTY }, { "veto", TEXT_EMOTE_VETO },
        { "snicker", TEXT_EMOTE_SNICKER }, { "stand", TEXT_EMOTE_STAND }, { "tickle", TEXT_EMOTE_TICKLE },
        { "violin", TEXT_EMOTE_VIOLIN }, { "smile", TEXT_EMOTE_SMILE }, { "rasp", TEXT_EMOTE_RASP },
        { "pity", TEXT_EMOTE_PITY }, { "growl", TEXT_EMOTE_GROWL }, { "bark", TEXT_EMOTE_BARK },
        { "scared", TEXT_EMOTE_SCARED }, { "flop", TEXT_EMOTE_FLOP }, { "love", TEXT_EMOTE_LOVE },
        { "moo", TEXT_EMOTE_MOO }, { "commend", TEXT_EMOTE_COMMEND }, { "train", TEXT_EMOTE_TRAIN },
        { "helpme", TEXT_EMOTE_HELPME }, { "incoming", TEXT_EMOTE_INCOMING }, { "charge", TEXT_EMOTE_CHARGE },
        { "flee", TEXT_EMOTE_FLEE }, { "attackmytarget", TEXT_EMOTE_ATTACKMYTARGET }, { "oom", TEXT_EMOTE_OOM },
        { "follow", TEXT_EMOTE_FOLLOW }, { "wait", TEXT_EMOTE_WAIT }, { "healme", TEXT_EMOTE_HEALME },
        { "openfire", TEXT_EMOTE_OPENFIRE }, { "flirt", TEXT_EMOTE_FLIRT }, { "joke", TEXT_EMOTE_JOKE },
        { "golfclap", TEXT_EMOTE_GOLFCLAP }, { "wink", TEXT_EMOTE_WINK }, { "pat", TEXT_EMOTE_PAT },
        { "serious", TEXT_EMOTE_SERIOUS }, { "mountspecial", TEXT_EMOTE_MOUNT_SPECIAL },
        { "goodluck", TEXT_EMOTE_GOODLUCK }, { "blame", TEXT_EMOTE_BLAME }, { "blank", TEXT_EMOTE_BLANK },
        { "brandish", TEXT_EMOTE_BRANDISH }, { "breath", TEXT_EMOTE_BREATH }, { "disagree", TEXT_EMOTE_DISAGREE },
        { "doubt", TEXT_EMOTE_DOUBT }, { "embarrass", TEXT_EMOTE_EMBARRASS }, { "encourage", TEXT_EMOTE_ENCOURAGE },
        { "enemy", TEXT_EMOTE_ENEMY }, { "eyebrow", TEXT_EMOTE_EYEBROW }, { "toast", TEXT_EMOTE_TOAST },
        { "fail", TEXT_EMOTE_FAIL }, { "highfive", TEXT_EMOTE_HIGHFIVE }, { "absent", TEXT_EMOTE_ABSENT },
        { "arm", TEXT_EMOTE_ARM }, { "awe", TEXT_EMOTE_AWE }, { "backpack", TEXT_EMOTE_BACKPACK },
        { "badfeeling", TEXT_EMOTE_BADFEELING }, { "challenge", TEXT_EMOTE_CHALLENGE }, { "chug", TEXT_EMOTE_CHUG },
        { "ding", TEXT_EMOTE_DING }, { "facepalm", TEXT_EMOTE_FACEPALM }, { "faint", TEXT_EMOTE_FAINT },
        { "go", TEXT_EMOTE_GO }, { "going", TEXT_EMOTE_GOING }, { "glower", TEXT_EMOTE_GLOWER },
        { "headache", TEXT_EMOTE_HEADACHE }, { "hiccup", TEXT_EMOTE_HICCUP }, { "hiss", TEXT_EMOTE_HISS },
        { "holdhand", TEXT_EMOTE_HOLDHAND }, { "hurry", TEXT_EMOTE_HURRY }, { "idea", TEXT_EMOTE_IDEA },
        { "jealous", TEXT_EMOTE_JEALOUS }, { "luck", TEXT_EMOTE_LUCK }, { "map", TEXT_EMOTE_MAP },
        { "mercy", TEXT_EMOTE_MERCY }, { "mutter", TEXT_EMOTE_MUTTER }, { "nervous", TEXT_EMOTE_NERVOUS },
        { "offer", TEXT_EMOTE_OFFER }, { "pet", TEXT_EMOTE_PET }, { "pinch", TEXT_EMOTE_PINCH },
        { "proud", TEXT_EMOTE_PROUD }, { "promise", TEXT_EMOTE_PROMISE }, { "pulse", TEXT_EMOTE_PULSE },
        { "punch", TEXT_EMOTE_PUNCH }, { "pout", TEXT_EMOTE_POUT }, { "regret", TEXT_EMOTE_REGRET },
        { "revenge", TEXT_EMOTE_REVENGE }, { "rolleyes", TEXT_EMOTE_ROLLEYES }, { "ruffle", TEXT_EMOTE_RUFFLE },
        { "sad", TEXT_EMOTE_SAD }, { "scoff", TEXT_EMOTE_SCOFF }, { "scold", TEXT_EMOTE_SCOLD },
        { "scowl", TEXT_EMOTE_SCOWL }, { "search", TEXT_EMOTE_SEARCH }, { "shakefist", TEXT_EMOTE_SHAKEFIST },
        { "shifty", TEXT_EMOTE_SHIFTY }, { "shudder", TEXT_EMOTE_SHUDDER }, { "signal", TEXT_EMOTE_SIGNAL },
        { "silence", TEXT_EMOTE_SILENCE }, { "sing", TEXT_EMOTE_SING }, { "smack", TEXT_EMOTE_SMACK },
        { "sneak", TEXT_EMOTE_SNEAK }, { "sneeze", TEXT_EMOTE_SNEEZE }, { "snort", TEXT_EMOTE_SNORT },
        { "squeal", TEXT_EMOTE_SQUEAL }, { "stopattack", TEXT_EMOTE_STOPATTACK },
        { "suspicious", TEXT_EMOTE_SUSPICIOUS }, { "think", TEXT_EMOTE_THINK }, { "truce", TEXT_EMOTE_TRUCE },
        { "twiddle", TEXT_EMOTE_TWIDDLE }, { "warn", TEXT_EMOTE_WARN }, { "snap", TEXT_EMOTE_SNAP },
        { "charm", TEXT_EMOTE_CHARM }, { "coverears", TEXT_EMOTE_COVEREARS }, { "crossarms", TEXT_EMOTE_CROSSARMS },
        { "look", TEXT_EMOTE_LOOK }, { "object", TEXT_EMOTE_OBJECT }, { "sweat", TEXT_EMOTE_SWEAT },
        { "yw", TEXT_EMOTE_YW },
    };

    // What players type for some of them besides their names.
    const EmoteName kEmoteAliases[] = {
        { "lol", TEXT_EMOTE_LAUGH }, { "hi", TEXT_EMOTE_HELLO }, { "ty", TEXT_EMOTE_THANK },
        { "thanks", TEXT_EMOTE_THANK }, { "grats", TEXT_EMOTE_CONGRATULATE }, { "gratz", TEXT_EMOTE_CONGRATULATE },
        { "congrats", TEXT_EMOTE_CONGRATULATE }, { "goodbye", TEXT_EMOTE_BYE }, { "farewell", TEXT_EMOTE_BYE },
        { "sorry", TEXT_EMOTE_APOLOGIZE }, { "lay", TEXT_EMOTE_LAYDOWN }, { "lie", TEXT_EMOTE_LAYDOWN },
        { "liedown", TEXT_EMOTE_LAYDOWN }, { "inc", TEXT_EMOTE_INCOMING }, { "rdy", TEXT_EMOTE_READY },
        { "attacktarget", TEXT_EMOTE_ATTACKMYTARGET },
    };

    // At most this many waiting their turn: a GM pressing the command again
    // and again is not a queue that takes minutes to get through.
    constexpr size_t kMostWaiting = 5;

    struct Job
    {
        PbpHivemindKind       kind = PbpHivemindKind::Say;
        std::string           text;        // what they say; the emote's name, for an emote
        uint32_t              emote = 0;
        ObjectGuid            by;          // who asked, told how it went (empty: the console)
        std::vector<uint64_t> bots;        // those nearest a real player first
        size_t                next   = 0;
        uint32_t              went   = 0;
        uint32_t              missed = 0;
    };
    std::deque<Job> g_jobs;

    // A dance, or a sit, a kneel or lying down, the hivemind left a bot in.
    struct Held
    {
        uint64_t          guid  = 0;
        Clock::time_point until{};
        bool              dance = false;
        uint8_t           stand = 0;   // not a dance: the stand state it was put in
    };
    std::vector<Held> g_held;

    PbpHivemindStats g_stats;

    // Every bot online, those nearest a real player first: someone sees the
    // bots about them do it at once, wherever the rest are on the list.
    std::vector<uint64_t> EveryBot()
    {
        std::vector<Player*> real, bots;
        for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
            if (p && p->IsInWorld())
                (PbpIsBotPlayer(p) ? bots : real).push_back(p);
        std::vector<std::pair<float, uint64_t>> order;
        order.reserve(bots.size());
        for (Player* bot : bots)
        {
            float nearest = std::numeric_limits<float>::max();
            for (Player* p : real)
                if (p->GetMapId() == bot->GetMapId() && p->GetInstanceId() == bot->GetInstanceId())
                    nearest = std::min(nearest, bot->GetExactDistSq(p));
            order.emplace_back(nearest, bot->GetGUID().GetRawValue());
        }
        std::sort(order.begin(), order.end());
        std::vector<uint64_t> out;
        out.reserve(order.size());
        for (auto const& [distance, guid] : order)
            out.push_back(guid);
        return out;
    }

    // Its zone's General, playerbots' way (PlayerbotAI::SayToChannel: the
    // channel's id, and the zone's name in its name), once a batch for each
    // side and zone.
    Channel* GeneralOf(Player* bot, std::map<std::pair<uint32_t, uint32_t>, Channel*>& known)
    {
        const std::pair<uint32_t, uint32_t> key{ uint32_t(bot->GetTeamId()), bot->GetZoneId() };
        if (auto it = known.find(key); it != known.end())
            return it->second;
        Channel* found = nullptr;
        const std::string zone = PbpZoneName(bot);
        ChannelMgr* mgr = ChannelMgr::forTeam(bot->GetTeamId());
        if (mgr && !zone.empty())
            for (auto const& [name, channel] : mgr->GetChannels())
                if (channel && channel->GetChannelId() == uint32_t(ChatChannelId::GENERAL) &&
                    channel->GetName().find(zone) != std::string::npos)
                {
                    found = channel;
                    break;
                }
        known[key] = found;
        return found;
    }

    bool Moving(Player* bot)
    {
        return bot->isMoving() || (bot->movespline && !bot->movespline->Finalized());
    }

    // Somewhere a person would dance, or sit down: on its feet and standing
    // still, out of a fight, the water and the air, and the hivemind's
    // dances and sits not switched off (StateSeconds 0).
    bool CanStay(Player* bot)
    {
        return g_HivemindStateSeconds && !bot->IsMounted() && !bot->IsInCombat() && !Moving(bot) &&
               !bot->IsInFlight() && !bot->GetVehicle() && !bot->IsInWater() && !bot->IsFlying();
    }

    // The stand state an emote's animation is (sitting, lying down,
    // kneeling): the game leaves that to a player's client, which a bot has
    // not. UNIT_STAND_STATE_STAND for any other.
    uint8_t StandFor(uint32_t anim)
    {
        switch (anim)
        {
            case EMOTE_STATE_SIT:
                return UNIT_STAND_STATE_SIT;
            case EMOTE_STATE_SLEEP:
                return UNIT_STAND_STATE_SLEEP;
            case EMOTE_STATE_KNEEL:
                return UNIT_STAND_STATE_KNEEL;
            default:
                return UNIT_STAND_STATE_STAND;
        }
    }

    // The emote, the way a player's client asks for it (what playerbots'
    // PlayerbotAI::PlayEmote does): the animation, the "Bot laughs." line
    // to those about and the rest are the game's. At nobody.
    bool Emote(Player* bot, uint32_t emote)
    {
        EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(emote);
        if (!em || !bot->IsAlive() || !bot->GetSession())
            return false;
        const bool wasDancing = bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE;
        WorldPacket data(CMSG_TEXT_EMOTE, 4 + 4 + 8);
        data << uint32(emote);
        data << uint32(0);   // the variant
        data << ObjectGuid::Empty;
        bot->GetSession()->HandleTextEmoteOpcode(data);

        const Clock::time_point until = Clock::now() + std::chrono::seconds(g_HivemindStateSeconds);
        const uint64_t guid = bot->GetGUID().GetRawValue();
        if (em->textid == EMOTE_STATE_DANCE)
        {
            // The game set it dancing: until it moves, or its time is up. On
            // a mount or on the move, the words alone.
            if (!wasDancing && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
            {
                if (CanStay(bot))
                    g_held.push_back({ guid, until, true, 0 });
                else
                    bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
            }
        }
        else if (const uint8_t stand = StandFor(em->textid); stand != UNIT_STAND_STATE_STAND)
        {
            // One already sitting (on a bench, the ground) stays as it is.
            if (bot->getStandState() == UNIT_STAND_STATE_STAND && CanStay(bot))
            {
                bot->SetStandState(stand);
                g_held.push_back({ guid, until, false, stand });
            }
        }
        else if (emote == TEXT_EMOTE_STAND && bot->IsSitState() && !bot->IsInCombat())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        return true;
    }

    // One bot's turn. False: it could not (dead, in no General).
    bool Go(Job const& job, Player* bot, std::map<std::pair<uint32_t, uint32_t>, Channel*>& generals)
    {
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai)
            return false;
        PlayerbotsPlus::OwnLine own;   // never muted (PbpMute)
        switch (job.kind)
        {
            case PbpHivemindKind::Say:
                // (A ghost is heard by nobody living.)
                return bot->IsAlive() && ai->Say(job.text);
            case PbpHivemindKind::Yell:
                return bot->IsAlive() && ai->Yell(job.text);
            case PbpHivemindKind::General:
            {
                Channel* general = GeneralOf(bot, generals);
                if (!general || !IsOn(general, bot->GetGUID()))
                    return false;   // a dungeon or a battleground has none
                general->Say(bot->GetGUID(), job.text, LANG_UNIVERSAL);
                return true;
            }
            case PbpHivemindKind::Emote:
                return Emote(bot, job.emote);
        }
        return false;
    }

    std::string Did(Job const& job)
    {
        switch (job.kind)
        {
            case PbpHivemindKind::Say:
                return fmt::format("said \"{}\"", job.text);
            case PbpHivemindKind::Yell:
                return fmt::format("yelled \"{}\"", job.text);
            case PbpHivemindKind::General:
                return fmt::format("put \"{}\" in their zone's General", job.text);
            case PbpHivemindKind::Emote:
                return "did /" + job.text;
        }
        return {};
    }

    // Everyone has had a turn: in the log, and to whoever asked.
    void Finish(Job const& job)
    {
        std::string missed;
        if (job.missed)
            missed = fmt::format(" ({} could not: {})", job.missed,
                                 job.kind == PbpHivemindKind::General
                                     ? "in no General -- a dungeon, a battleground -- or gone"
                                     : "dead, or gone");
        const std::string line =
            fmt::format("Hivemind: {} of {} bots {}{}.", job.went, job.bots.size(), Did(job), missed);
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {}", line);
        if (Player* gm = job.by ? ObjectAccessor::FindConnectedPlayer(job.by) : nullptr)
            if (WorldSession* session = gm->GetSession())
                ChatHandler(session).SendSysMessage(line);
    }

    // A dance or a sit the hivemind left on ends once the bot moves (as a
    // player's client ends it) or its time is up, and at once with `all`.
    // One that someone else ended or changed since is theirs.
    void Release(const Clock::time_point now, bool all)
    {
        std::erase_if(g_held, [now, all](Held const& h)
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(h.guid));
            if (!bot || !bot->IsInWorld())
                return true;
            const bool over = all || now >= h.until || Moving(bot) || !bot->IsAlive() || bot->IsInCombat() ||
                              bot->IsMounted();
            if (h.dance)
            {
                if (bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) != EMOTE_STATE_DANCE)
                    return true;
                if (over)
                    bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
                return over;
            }
            if (bot->getStandState() != h.stand)
                return true;
            if (over)
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            return over;
        });
    }
}

uint32_t PbpHivemind_EmoteId(std::string name)
{
    std::string key;
    for (char c : name)
        if (c != '/' && c != '_' && !std::isspace(static_cast<unsigned char>(c)))
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (EmoteName const& e : kEmotes)
        if (key == e.name)
            return e.id;
    for (EmoteName const& e : kEmoteAliases)
        if (key == e.name)
            return e.id;
    return 0;
}

std::string PbpHivemind_SomeEmotes()
{
    return "laugh, dance, cheer, wave, bow, salute, applaud, roar, flex, cry, hug, kiss, chicken, train, sit, kneel, "
           "sleep";
}

uint32_t PbpHivemind_Start(PbpHivemindKind kind, std::string const& text, uint32_t emote, ObjectGuid by,
                           std::string& why)
{
    if (!g_Pbp.enable || !g_HivemindEnable)
    {
        why = "it is switched off (PlayerbotsPlus.Hivemind.Enable)";
        return 0;
    }
    if (g_jobs.size() >= kMostWaiting)
    {
        why = fmt::format("the bots are still on the last {} -- in a moment", g_jobs.size());
        return 0;
    }
    Job job;
    job.kind  = kind;
    job.text  = text;
    job.emote = emote;
    job.by    = by;
    job.bots  = EveryBot();
    if (job.bots.empty())
    {
        why = "there are no bots online";
        return 0;
    }
    ++g_stats.commands;
    const uint32_t bots = static_cast<uint32_t>(job.bots.size());
    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Hivemind: {} bots to {} ({}).", bots,
             kind == PbpHivemindKind::Emote ? "/" + text : fmt::format("\"{}\"", text),
             kind == PbpHivemindKind::Say      ? "/say"
             : kind == PbpHivemindKind::Yell    ? "/yell"
             : kind == PbpHivemindKind::General ? "General"
                                                : "an emote");
    g_jobs.push_back(std::move(job));
    return bots;
}

void PbpHivemind_Update(uint32_t /*diff*/)
{
    const auto now = Clock::now();
    const bool on = g_Pbp.enable && g_HivemindEnable;
    static Clock::time_point nextRelease{};
    if (!g_held.empty() && (!on || now >= nextRelease))
    {
        nextRelease = now + std::chrono::milliseconds(500);
        Release(now, !on);
    }
    if (!on)
    {
        g_jobs.clear();
        return;
    }
    if (g_jobs.empty())
        return;

    Job& job = g_jobs.front();
    const size_t batch = g_HivemindBotsPerTick ? g_HivemindBotsPerTick : job.bots.size();
    std::map<std::pair<uint32_t, uint32_t>, Channel*> generals;   // this batch's, by side and zone
    for (size_t n = 0; n < batch && job.next < job.bots.size(); ++n)
    {
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(job.bots[job.next++]));
        if (bot && bot->IsInWorld() && !bot->IsBeingTeleported() && Go(job, bot, generals))
        {
            ++job.went;
            ++g_stats.lines;
        }
        else
        {
            ++job.missed;
            ++g_stats.missed;
        }
    }
    if (job.next >= job.bots.size())
    {
        Finish(job);
        g_jobs.pop_front();
    }
}

PbpHivemindStats PbpHivemind_GetStats()
{
    PbpHivemindStats s = g_stats;
    for (Job const& job : g_jobs)
        s.waiting += static_cast<uint32_t>(job.bots.size() - job.next);
    return s;
}
