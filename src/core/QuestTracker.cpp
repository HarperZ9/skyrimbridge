#include "QuestTracker.h"
#include <RE/Skyrim.h>

namespace SB::QuestTracker
{
    QuestData Update()
    {
        QuestData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        // ── Quest progress summary ────────────────────────────────────
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
            return data;

        int activeCount = 0;
        int completedCount = 0;
        int activeObjectives = 0;
        float mainQuestStage = 0.f;

        auto& quests = dataHandler->GetFormArray<RE::TESQuest>();
        for (auto* quest : quests) {
            if (!quest)
                continue;

            bool isActive = quest->IsActive();
            bool isCompleted = quest->IsCompleted();

            if (isCompleted) {
                completedCount++;
            }

            if (isActive) {
                activeCount++;

                // Count displayed objectives for active quests
                for (auto& obj : quest->objectives) {
                    if (obj &&
                        obj->state.get() == RE::QUEST_OBJECTIVE_STATE::kDisplayed)
                        activeObjectives++;
                }

                // Track main quest stage
                if (quest->GetType() == RE::QUEST_DATA::Type::kMainQuest) {
                    float stage = static_cast<float>(quest->GetCurrentStageID());
                    if (stage > mainQuestStage)
                        mainQuestStage = stage;
                }
            }
        }

        data.Progress.x = mainQuestStage;
        data.Progress.y = static_cast<float>(completedCount);
        data.Progress.z = static_cast<float>(activeCount);
        data.Progress.w = static_cast<float>(activeObjectives);

        return data;
    }
}
