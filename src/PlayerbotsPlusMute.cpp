#include "PlayerbotsPlusMute.h"
#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusText.h"
#include "PlayerbotsPlusUtil.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace
{
    // Who is a bot: by guid for what they say, by name for playerbots' red
    // error lines to a master, which come from nobody ("Name|cfff00000: I
    // can't do this right now").
    struct Bots
    {
        std::unordered_set<uint64_t>    guids;
        std::unordered_set<std::string> names;
    };

    // What the packet hook reads. Replaced whole, never changed in place.
    struct Snapshot
    {
        bool                     enable = false;
        std::vector<std::string> patterns;
        Bots                     bots;
    };


    std::mutex                      g_mutex;
    std::shared_ptr<const Snapshot> g_snapshot = std::make_shared<Snapshot>();
    std::atomic<bool>               g_enabled{ false };   // fast path: most packets are not chat at all
    std::atomic<uint64_t>           g_dropped{ 0 };

    // Lines a module is sending for a bot on this thread, in its own words
    // (PlayerbotsPlus::OwnLine).
    thread_local int t_ownLines = 0;

    // Kept on the world thread, published with the bots.
    bool                     g_enable = false;
    std::vector<std::string> g_patterns;

    std::shared_ptr<const Snapshot> Current()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_snapshot;
    }

    void Publish(Bots bots)
    {
        auto next = std::make_shared<Snapshot>();
        next->enable   = g_enable && !g_patterns.empty();
        next->patterns = g_patterns;
        next->bots     = std::move(bots);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_snapshot = std::move(next);
        }
        g_enabled.store(g_enable && !g_patterns.empty(), std::memory_order_relaxed);
    }

    Bots OnlineBots()
    {
        Bots bots;
        for (auto const& entry : ObjectAccessor::GetPlayers())
            if (Player* p = entry.second)
                if (PbpIsBotPlayer(p))
                {
                    bots.guids.insert(p->GetGUID().GetRawValue());
                    bots.names.insert(p->GetName());
                }
        return bots;
    }

    // "Name, Name|cfff00000: text" from bots only: the text. Empty otherwise.
    std::string ErrorText(std::string const& line, Bots const& bots)
    {
        std::vector<std::string> names;
        std::string text;
        if (!PbpSplitErrorLine(line, names, text))
            return {};
        for (std::string const& name : names)
            if (!bots.names.contains(name))
                return {};
        return text;
    }
}

void PbpMute_Publish(bool enable, std::vector<std::string> patterns)
{
    g_enable = enable;
    g_patterns.clear();
    for (std::string& p : patterns)
    {
        const size_t b = p.find_first_not_of(" \t");
        if (b == std::string::npos)
            continue;
        const size_t e = p.find_last_not_of(" \t");
        g_patterns.push_back(p.substr(b, e - b + 1));
    }
    Publish(OnlineBots());
    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Muted bot lines: {} ({} pattern(s)).",
             g_enable ? "on" : "off", g_patterns.size());
}

void PbpMute_Update(uint32_t /*diff*/)
{
    if (!g_enable)
        return;
    using Clock = std::chrono::steady_clock;
    static Clock::time_point next{};
    const Clock::time_point now = Clock::now();
    if (now < next)
        return;
    next = now + std::chrono::seconds(2);
    Publish(OnlineBots());
}

uint64_t PbpMute_GetDropped()
{
    return g_dropped.load(std::memory_order_relaxed);
}

void PbpMute_OwnLineBegin()
{
    ++t_ownLines;
}

void PbpMute_OwnLineEnd()
{
    --t_ownLines;
}

bool PbpMuteScript::CanPacketSend(WorldSession* session, WorldPacket const& packet)
{
    // Every packet to every client comes through here, from any thread: say
    // yes to all of them, and look no further than the opcode unless muting
    // is on.
    if (packet.GetOpcode() != SMSG_MESSAGECHAT || !g_enabled.load(std::memory_order_relaxed) || !session ||
        t_ownLines > 0)
        return true;

    // type u8, language i32, sender u64, flags u32, receiver u64, length u32,
    // text: the layout ChatHandler::BuildChatPacket writes for these types.
    constexpr size_t kTextAt = 1 + 4 + 8 + 4 + 8 + 4;
    const size_t size = packet.size();
    if (size < kTextAt + 1)
        return true;

    const uint8 type = packet.read<uint8>(0);
    const bool system = type == CHAT_MSG_SYSTEM;
    if (!system && type != CHAT_MSG_WHISPER && type != CHAT_MSG_PARTY && type != CHAT_MSG_PARTY_LEADER &&
        type != CHAT_MSG_RAID && type != CHAT_MSG_RAID_LEADER)
        return true;
    if (packet.read<uint32>(1) == uint32(LANG_ADDON))
        return true;

    const uint32 length = packet.read<uint32>(kTextAt - 4);   // includes the closing NUL
    if (length < 2 || kTextAt + length > size)
        return true;

    const std::shared_ptr<const Snapshot> snap = Current();
    if (!snap->enable || (!system && !snap->bots.guids.contains(packet.read<uint64>(5))))
        return true;

    const char* text = reinterpret_cast<const char*>(packet.contents() + kTextAt);
    std::string line(text, strnlen(text, length - 1));
    if (system)
    {
        line = ErrorText(line, snap->bots);
        if (line.empty())
            return true;   // a system message of any other kind
    }
    for (const std::string& pattern : snap->patterns)
    {
        if (PbpGlobMatch(pattern, line))
        {
            g_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    return true;
}
