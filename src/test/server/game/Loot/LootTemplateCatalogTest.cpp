/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LootMgr.h"
#include "gtest/gtest.h"
#include <array>
#include <limits>

namespace
{
    LootStoreItem* Row(uint32 item, int32 reference = 0, uint8 group = 0, float chance = 100.0f)
    {
        return new LootStoreItem(item, reference, chance, false, LOOT_MODE_DEFAULT, group, 1, 1);
    }
}

TEST(LootTemplateCatalog, SortedUniquePlainAndGroupedCandidates)
{
    LootTemplate loot;
    loot.AddEntry(Row(30));
    loot.AddEntry(Row(10, 0, 1, 10.0f));
    loot.AddEntry(Row(20, 0, 1, 0.0f));
    loot.AddEntry(Row(30, 0, 3)); // Includes a sparse, empty second group.
    std::vector<uint32> result{999};
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_EQ(result, (std::vector<uint32>{10, 20, 30}));
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_EQ(result, (std::vector<uint32>{10, 20, 30}));
}

TEST(LootTemplateCatalog, ExcludesQuestConditionalAndOtherLootModes)
{
    LootTemplate loot;
    Condition condition{};
    auto* quest = Row(10);
    quest->needs_quest = true;
    loot.AddEntry(quest);
    auto* conditional = Row(20, 0, 1);
    conditional->conditions.push_back(&condition);
    loot.AddEntry(conditional);
    auto* otherMode = Row(30, 0, 2);
    otherMode->lootmode = LOOT_MODE_HARD_MODE_1;
    loot.AddEntry(otherMode);
    loot.AddEntry(Row(40, 0, 0, 0.0f));
    loot.AddEntry(Row(50));
    std::vector<uint32> result;
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_EQ(result, (std::vector<uint32>{50}));
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}, LOOT_MODE_HARD_MODE_1));
    EXPECT_EQ(result, (std::vector<uint32>{30}));
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}, 0));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, FollowsPlainAndGroupedReferencesWithoutLeakingReferenceIds)
{
    LootTemplate loot;
    LootTemplate child;
    LootTemplate leaf;
    loot.AddEntry(Row(999, 1));
    loot.AddEntry(Row(998, -2, 3, 0.0f));
    child.AddEntry(Row(10));
    child.AddEntry(Row(0, 2, 1));
    leaf.AddEntry(Row(20, 0, 2));
    std::vector<uint32> result;
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {{1, &child}, {2, &leaf}}));
    EXPECT_EQ(result, (std::vector<uint32>{10, 20}));
}

TEST(LootTemplateCatalog, ConditionalReferenceDoesNotExposeChildren)
{
    LootTemplate loot;
    LootTemplate child;
    Condition condition{};
    auto* reference = Row(0, 1);
    reference->conditions.push_back(&condition);
    loot.AddEntry(reference);
    child.AddEntry(Row(10));
    std::vector<uint32> result;
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {{1, &child}}));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, MissingAndNullReferencesDiscardPartialCatalog)
{
    LootTemplate loot;
    loot.AddEntry(Row(10));
    loot.AddEntry(Row(0, 1));
    std::vector<uint32> result{999};
    EXPECT_FALSE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(loot.CollectUnconditionalItemIds(result, {{1, nullptr}}));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, ReferenceCycleDiscardsPartialCatalog)
{
    LootTemplate loot;
    LootTemplate child;
    loot.AddEntry(Row(10));
    loot.AddEntry(Row(0, 1));
    child.AddEntry(Row(0, 2, 1));
    std::vector<uint32> result;
    EXPECT_FALSE(loot.CollectUnconditionalItemIds(result, {{1, &child}, {2, &loot}}));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, MinimumSignedReferenceCannotOverflow)
{
    LootTemplate loot;
    loot.AddEntry(Row(0, std::numeric_limits<int32>::min()));
    std::vector<uint32> result;
    EXPECT_FALSE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, BoundedWorkDiscardsPartialCatalog)
{
    LootTemplate loot;
    for (uint32 item = 1; item < 4096; ++item)
        loot.AddEntry(Row(item));
    std::vector<uint32> result;
    ASSERT_TRUE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_EQ(result.size(), 4095u);
    loot.AddEntry(Row(4096));
    EXPECT_FALSE(loot.CollectUnconditionalItemIds(result, {}));
    EXPECT_TRUE(result.empty());
}

TEST(LootTemplateCatalog, BoundedReferenceDepthDiscardsPartialCatalog)
{
    std::array<LootTemplate, 33> templates;
    LootTemplateMap references;
    for (uint32 index = 1; index < templates.size(); ++index)
    {
        references[index] = &templates[index];
        templates[index - 1].AddEntry(Row(0, index));
    }
    templates.back().AddEntry(Row(10));
    std::vector<uint32> result;
    EXPECT_FALSE(templates.front().CollectUnconditionalItemIds(result, references));
    EXPECT_TRUE(result.empty());
    ASSERT_TRUE(templates[1].CollectUnconditionalItemIds(result, references));
    EXPECT_EQ(result, (std::vector<uint32>{10}));
}

TEST(LootTemplateCatalog, MissingStoreEntryClearsOutput)
{
    LootStore store("catalog-test", "entry", false);
    std::vector<uint32> result{999};
    EXPECT_FALSE(store.CollectUnconditionalItemIds(1, result));
    EXPECT_TRUE(result.empty());
}
