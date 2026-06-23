#include "gns/Narrator.h"

namespace gns {

// ---- TemplateNarrationProvider ----------------------------------------------

std::string TemplateNarrationProvider::narrate(const DmContext& ctx) {
    // Authored player text first, then each fact on its own line. dmText is
    // intentionally never included -- it is DM-only.
    std::string out = ctx.areaPlayerText;
    for (const auto& f : ctx.facts) {
        if (!out.empty()) out += '\n';
        out += f;
    }
    return out;
}

std::string TemplateNarrationProvider::speakNpc(const DmContext&, const std::string& npc) {
    const std::string who = npc.empty() ? "The figure" : npc;
    return who + " regards the party in silence.";
}

// ---- Narrator ---------------------------------------------------------------

Narrator::Narrator() : provider_(&default_) {}
Narrator::Narrator(INarrationProvider& provider) : provider_(&provider) {}

std::string Narrator::describeAreaEntry(const Area& area) const {
    return describeAreaEntry(area, {});
}

std::string Narrator::describeAreaEntry(const Area& area,
                                        const std::vector<std::string>& facts) const {
    DmContext ctx;
    ctx.areaName = area.name;
    ctx.areaPlayerText = area.playerText;
    ctx.areaDmText = area.dmText;
    ctx.facts = facts;
    return provider_->narrate(ctx);
}

std::string Narrator::describe(const std::vector<std::string>& facts) const {
    DmContext ctx;
    ctx.facts = facts;
    return provider_->narrate(ctx);
}

std::string Narrator::speak(const std::string& npc, const Area& area) const {
    DmContext ctx;
    ctx.areaName = area.name;
    ctx.areaPlayerText = area.playerText;
    ctx.areaDmText = area.dmText;
    return provider_->speakNpc(ctx, npc);
}

// ---- fact formatters --------------------------------------------------------

std::string factFor(const AttackResult& r, const std::string& attacker,
                    const std::string& target) {
    if (!r.hit)
        return attacker + " misses " + target + ".";
    return attacker + " hits " + target + " for " + std::to_string(r.damage) + " damage.";
}

} // namespace gns
