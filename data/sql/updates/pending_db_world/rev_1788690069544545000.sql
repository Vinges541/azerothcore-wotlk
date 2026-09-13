-- Optional mod-world-war field orders. No permanent spawns or changes to stock NPCs.
-- Enabled by WorldWar.Content.Enabled; acceptance and credit are gated by the module.
DELETE FROM `quest_template` WHERE `ID` IN (910001, 910002);
INSERT INTO `quest_template` (`ID`, `QuestType`, `QuestLevel`, `MinLevel`, `QuestSortID`, `QuestInfoID`, `Flags`, `RewardHonor`, `AllowableRaces`, `LogTitle`, `LogDescription`, `QuestDescription`, `QuestCompletionLog`, `RequiredNpcOrGo1`, `RequiredNpcOrGoCount1`, `ObjectiveText1`) VALUES
(910001, 2, 55, 55, 45, 62, 4096, 20, 1101, 'War Orders: Break the Defilers', 'Defeat 5 enemy campaign defenders. Return to a friendly war camp and use .worldwar orders.', 'Our supply lines cannot hold while enemy garrisons threaten the roads. Strike their campaign defenders during the contested phase, then report to any camp we control.', 'Report at a friendly campaign camp using .worldwar orders.', 15128, 5, 'Enemy campaign defenders defeated'),
(910002, 2, 55, 55, 45, 62, 4096, 20, 690, 'War Orders: Break the League', 'Defeat 5 enemy campaign defenders. Return to a friendly war camp and use .worldwar orders.', 'Our supply lines cannot hold while enemy garrisons threaten the roads. Strike their campaign defenders during the contested phase, then report to any camp we control.', 'Report at a friendly campaign camp using .worldwar orders.', 15130, 5, 'Enemy campaign defenders defeated');

DELETE FROM `quest_template_addon` WHERE `ID` IN (910001, 910002);
INSERT INTO `quest_template_addon` (`ID`, `SpecialFlags`) VALUES
(910001, 1),
(910002, 1);

-- Dedicated level-55 convoy templates; the original horse and kodo remain untouched.
DELETE FROM `creature_template` WHERE `entry` IN (910010, 910011);
INSERT INTO `creature_template` (`entry`, `name`, `minlevel`, `maxlevel`, `faction`, `unit_class`, `type`, `AIName`, `HealthModifier`) VALUES
(910010, 'Alliance War Supply Horse', 55, 55, 84, 1, 1, 'SmartAI', 1),
(910011, 'Horde War Supply Kodo', 55, 55, 83, 1, 1, 'SmartAI', 1);

DELETE FROM `creature_template_model` WHERE `CreatureID` = 910010;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 910010, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild` FROM `creature_template_model` WHERE `CreatureID` = 5525;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 910011;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 910011, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild` FROM `creature_template_model` WHERE `CreatureID` = 10636;
