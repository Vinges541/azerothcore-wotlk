/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "GossipDef.h"
#include <gtest/gtest.h>

TEST(GossipLearningHint, AnnotatesOnlyExactUncodedOption)
{
    GossipMenu menu;
    menu.AddMenuItem(0, 0, "learn", 50, 1001, "", 0);
    menu.AddMenuItem(1, 0, "unlearn", 51, 1001, "", 0);
    menu.SetSpellLearning(50, 1001, {123, 200000, true});
    EXPECT_EQ(menu.GetItem(0)->SpellLearning.Spell, 123u);
    EXPECT_EQ(menu.GetItem(0)->SpellLearning.Cost, 200000u);
    EXPECT_TRUE(menu.GetItem(0)->SpellLearning.Confirmation);
    EXPECT_EQ(menu.GetItem(1)->SpellLearning.Spell, 0u);
    EXPECT_EQ(menu.GetItem(0)->BoxMoney, 0u); // Native client popup is not changed by server-only metadata.
    menu.AddMenuItem(2, 0, "code", 50, 1002, "", 0, true);
    menu.SetSpellLearning(50, 1002, {123, 0, false});
    EXPECT_EQ(menu.GetItem(2)->SpellLearning.Spell, 0u);
}

TEST(GossipLearningHint, ClearsOnReplacementAmbiguityAndReset)
{
    GossipMenu menu;
    menu.AddMenuItem(0, 0, "learn", 50, 1001, "", 0);
    menu.SetSpellLearning(50, 1001, {123, 0, false});
    menu.AddMenuItem(0, 0, "different", 51, 1002, "", 0);
    EXPECT_EQ(menu.GetItem(0)->SpellLearning.Spell, 0u);
    menu.AddMenuItem(1, 0, "learn", 50, 1001, "", 0);
    menu.SetSpellLearning(50, 1001, {123, 0, false});
    menu.AddMenuItem(2, 0, "ambiguous", 50, 1001, "", 0);
    menu.SetSpellLearning(50, 1001, {123, 0, false});
    EXPECT_EQ(menu.GetItem(1)->SpellLearning.Spell, 0u);
    EXPECT_EQ(menu.GetItem(2)->SpellLearning.Spell, 0u);
    menu.SetSpellLearning(50, 9999, {123, 0, false});
    EXPECT_EQ(menu.GetMenuItemCount(), 3u);
    menu.ClearMenu();
    EXPECT_TRUE(menu.Empty());
}
