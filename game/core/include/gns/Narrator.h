#pragma once
#include "gns/Module.h"   // gns::Area
#include "gns/Rules.h"    // gns::AttackResult (fact formatters)

#include <string>
#include <vector>

// Narrator: turns game events into the text the player reads (milestone M4).
//
// The Narrator is the presentation layer of the procedural DM. It assembles a
// DmContext from authored area text + the rule outcomes the engine already
// decided, then hands it to an INarrationProvider for rendering. Two providers:
//
//   * TemplateNarrationProvider (default) -- authored playerText + formatted
//     facts, deterministic, offline, zero dependencies. The permanent floor.
//   * a local llama.cpp provider (later slice) -- rewrites/voices the same
//     context. It only renders facts the engine decided; it never adjudicates.
//
// dmText is carried in the context to seed a future model but is DM-only: the
// template provider never shows it to the player.

namespace gns {

// Everything a provider needs to render one beat of narration. Assembled by the
// Narrator from authored data (Area) + runtime facts.
struct DmContext {
    std::string areaName;
    std::string areaPlayerText;
    std::string areaDmText;          // DM-only; never shown to the player
    std::string situation;           // short runtime summary, e.g. "3 orcs, hostile"
    std::vector<std::string> facts;  // rule outcomes already decided by the engine
};

// The seam every narration provider implements. The local model and the template
// renderer are interchangeable behind this interface.
class INarrationProvider {
public:
    virtual ~INarrationProvider() = default;
    virtual std::string narrate(const DmContext& ctx) = 0;                       // area/event prose
    virtual std::string speakNpc(const DmContext& ctx, const std::string& npc) = 0;  // one NPC line
};

// Always-on, offline, deterministic provider: authored playerText followed by the
// formatted facts. Never emits dmText.
class TemplateNarrationProvider : public INarrationProvider {
public:
    std::string narrate(const DmContext& ctx) override;
    std::string speakNpc(const DmContext& ctx, const std::string& npc) override;
};

// Thin presentation service the engine drives. Defaults to the built-in template
// provider; a local provider drops in via the injecting constructor. Non-copyable
// (it holds a pointer into its own default provider), which is fine -- the engine
// constructs one and keeps it.
class Narrator {
public:
    Narrator();                                    // uses the built-in template provider
    explicit Narrator(INarrationProvider& provider);
    Narrator(const Narrator&) = delete;
    Narrator& operator=(const Narrator&) = delete;

    // Narrate entering an area, optionally folding in rule-outcome facts.
    std::string describeAreaEntry(const Area& area) const;
    std::string describeAreaEntry(const Area& area,
                                  const std::vector<std::string>& facts) const;
    // Narrate a bare list of rule outcomes (e.g. a resolved combat round).
    std::string describe(const std::vector<std::string>& facts) const;
    // Voice an NPC line in the context of an area.
    std::string speak(const std::string& npc, const Area& area) const;

    INarrationProvider& provider() const { return *provider_; }

private:
    TemplateNarrationProvider default_;
    INarrationProvider* provider_;
};

// Format an existing rules result as a one-line fact for DmContext::facts. More
// formatters (wandering / treasure / turn-undead) land with their producing
// slices; AttackResult is here now to establish the pattern.
std::string factFor(const AttackResult& r, const std::string& attacker,
                    const std::string& target);

} // namespace gns
