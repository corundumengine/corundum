// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// The built-in dialogue events (sound, quest, item, codex, location, container, shop,
// reputation and encounter) and the dispatcher that runs each queued event through them or
// through the game's on_event hook. A start_encounter event only queues its fight;
// start_pending_encounter starts it once the dialogue that asked for it has closed.

namespace corundum {

struct Engine;

} // namespace corundum

namespace corundum::gameplay {

class Gameplay;

} // namespace corundum::gameplay

namespace corundum::gameplay::dialogue {

struct EventAction;

} // namespace corundum::gameplay::dialogue

namespace corundum::gameplay::quest {

class Runner;

} // namespace corundum::gameplay::quest

namespace corundum::gameplay {

void dispatch_dialogue_event(Gameplay &gameplay, Engine &engine, quest::Runner &quest_runner,
                             const dialogue::EventAction &event) noexcept;

void start_pending_encounter(Engine &engine, Gameplay &gameplay);

} // namespace corundum::gameplay
