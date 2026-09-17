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

#ifdef MOD_PLAYERBOTS

#include "PlayerbotsDatabase.h"
#include "MySQLPreparedStatement.h"

void PlayerbotsDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_PLAYERBOTS_STATEMENTS);

    PrepareStatement(PLAYERBOTS_SEL_CUSTOM_STRATEGY_BY_OWNER, "SELECT DISTINCT name FROM playerbots_custom_strategy WHERE owner = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME, "SELECT idx, action_line FROM playerbots_custom_strategy WHERE owner = ? AND name = ? ORDER BY idx", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME_AND_IDX, "SELECT action_line FROM playerbots_custom_strategy WHERE owner = ? AND name = ? AND idx = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_DEL_CUSTOM_STRATEGY, "DELETE FROM playerbots_custom_strategy WHERE name = ? AND owner = ? AND idx = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_UPD_CUSTOM_STRATEGY, "UPDATE playerbots_custom_strategy SET action_line = ? WHERE name = ? AND owner = ? AND idx = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_INS_CUSTOM_STRATEGY, "INSERT INTO playerbots_custom_strategy (name, owner, idx, action_line) VALUES (?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_DB_STORE, "SELECT `key`,`value` FROM `playerbots_db_store` WHERE `guid` = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_DEL_DB_STORE, "DELETE FROM `playerbots_db_store` WHERE `guid` = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_INS_DB_STORE, "INSERT INTO `playerbots_db_store` (`guid`, `key`, `value`) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_ENCHANTS, "SELECT class, spec, spellid, slotid FROM playerbots_enchants", CONNECTION_SYNCH);

    PrepareStatement(PLAYERBOTS_SEL_EQUIP_CACHE, "SELECT clazz, lvl, slot, quality, item FROM playerbots_equip_cache", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_EQUIP_CACHE, "INSERT INTO playerbots_equip_cache (clazz, lvl, slot, quality, item) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASKS_BY_VALUE, "SELECT `value`, `time`, validIn FROM playerbots_guild_tasks WHERE `value` = ? AND guildid = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASK_ITEM_EXPIRY,
        "SELECT COALESCE(MAX(CAST(`time` AS UNSIGNED) + validIn), 0) FROM playerbots_guild_tasks "
        "WHERE `value` = ? AND guildid = ? AND `type` = 'itemTask'", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASKS_BY_OWNER,
        "SELECT `value`, `time`, validIn, guildid, id, `data` FROM playerbots_guild_tasks "
        "WHERE owner = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_UPD_GUILD_KILL_TASK_COMPLETE,
        "UPDATE playerbots_guild_tasks AS task "
        "JOIN playerbots_guild_tasks AS active_task ON active_task.owner = task.owner "
        "AND active_task.guildid = task.guildid AND active_task.`type` = 'activeTask' "
        "JOIN playerbots_guild_tasks AS reward ON reward.owner = task.owner "
        "AND reward.guildid = task.guildid AND reward.`type` = 'reward' "
        "SET task.`data` = 'kill-complete-v1', reward.`value` = 1, reward.`time` = ?, reward.validIn = ? "
        "WHERE task.id = ? AND task.owner = ? AND task.guildid = ? AND task.`type` = 'killTask' "
        "AND task.`value` = ? AND COALESCE(task.`data`, '') = '' AND active_task.`value` = 2 "
        "AND CAST(task.`time` AS UNSIGNED) + task.validIn > ? "
        "AND CAST(active_task.`time` AS UNSIGNED) + active_task.validIn > ?", CONNECTION_ASYNC);
    // Retries preserve the first durable payload. Callers must read it back before applying a claim:
    // a successful duplicate-key write is not confirmation that the supplied payload was stored.
    PrepareStatement(PLAYERBOTS_INS_GUILD_KILL_CLAIM,
        "INSERT INTO `playerbots_guild_kill_claim` "
        "(`TaskID`, `Owner`, `GuildID`, `CreatureEntry`, `EventTime`, `RewardDelay`) VALUES (?, ?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE `TaskID` = `TaskID`", CONNECTION_ASYNC);
    // A successful missing-row read returns a sentinel with NULL TaskID, unlike a failed query.
    PrepareStatement(PLAYERBOTS_SEL_GUILD_KILL_CLAIM,
        "SELECT `claim`.`TaskID`, `claim`.`Owner`, `claim`.`GuildID`, `claim`.`CreatureEntry`, "
        "`claim`.`EventTime`, `claim`.`RewardDelay` FROM (SELECT 1) AS `seed` "
        "LEFT JOIN `playerbots_guild_kill_claim` AS `claim` ON `claim`.`TaskID` = ?", CONNECTION_ASYNC);
    // Keyset pagination bounds both work and result size. Advance only after admitting the page;
    // NULL TaskID is an explicit end-of-scan sentinel, not a claim with record ID zero.
    PrepareStatement(PLAYERBOTS_SEL_GUILD_KILL_CLAIM_PAGE,
        "SELECT `claim`.`TaskID`, `claim`.`Owner`, `claim`.`GuildID`, `claim`.`CreatureEntry`, "
        "`claim`.`EventTime`, `claim`.`RewardDelay` FROM (SELECT 1) AS `seed` LEFT JOIN "
        "(SELECT `TaskID`, `Owner`, `GuildID`, `CreatureEntry`, `EventTime`, `RewardDelay` "
        "FROM `playerbots_guild_kill_claim` WHERE `TaskID` > ? ORDER BY `TaskID` LIMIT 64) AS `claim` "
        "ON 1 = 1 ORDER BY `claim`.`TaskID`", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_UPD_GUILD_KILL_CLAIM_APPLY,
        "UPDATE `playerbots_guild_tasks` AS `task` "
        "JOIN `playerbots_guild_kill_claim` AS `claim` ON `claim`.`TaskID` = `task`.`id` "
        "AND `claim`.`Owner` = `task`.`owner` AND `claim`.`GuildID` = `task`.`guildid` "
        "AND `claim`.`CreatureEntry` = `task`.`value` "
        "JOIN `playerbots_guild_tasks` AS `active` ON `active`.`owner` = `task`.`owner` "
        "AND `active`.`guildid` = `task`.`guildid` AND `active`.`type` = 'activeTask' "
        "JOIN `playerbots_guild_tasks` AS `reward` ON `reward`.`owner` = `task`.`owner` "
        "AND `reward`.`guildid` = `task`.`guildid` AND `reward`.`type` = 'reward' "
        "SET `task`.`data` = 'kill-complete-v1', `reward`.`value` = 1, "
        "`reward`.`time` = `claim`.`EventTime`, `reward`.`validIn` = `claim`.`RewardDelay` "
        "WHERE `claim`.`TaskID` = ? AND `claim`.`Owner` = ? AND `claim`.`GuildID` = ? "
        "AND `claim`.`CreatureEntry` = ? AND `claim`.`EventTime` = ? AND `claim`.`RewardDelay` = ? "
        "AND `task`.`type` = 'killTask' AND COALESCE(`task`.`data`, '') = '' AND `active`.`value` = 2 "
        "AND `task`.`time` <= `claim`.`EventTime` AND `active`.`time` <= `claim`.`EventTime` "
        "AND CAST(`task`.`time` AS UNSIGNED) + `task`.`validIn` > `claim`.`EventTime` "
        "AND CAST(`active`.`time` AS UNSIGNED) + `active`.`validIn` > `claim`.`EventTime`", CONNECTION_ASYNC);
    // Run in the same transaction after APPLY. Missing reward/active rows alone do not retire evidence.
    // Retirement means reconciled (applied, already applied or stale), not that mail was delivered.
    PrepareStatement(PLAYERBOTS_DEL_GUILD_KILL_CLAIM_RETIRED,
        "DELETE `claim` FROM `playerbots_guild_kill_claim` AS `claim` "
        "LEFT JOIN `playerbots_guild_tasks` AS `task` ON `task`.`id` = `claim`.`TaskID` "
        "WHERE `claim`.`TaskID` = ? AND `claim`.`Owner` = ? AND `claim`.`GuildID` = ? "
        "AND `claim`.`CreatureEntry` = ? AND `claim`.`EventTime` = ? AND `claim`.`RewardDelay` = ? "
        "AND (`task`.`id` IS NULL OR `task`.`owner` <> `claim`.`Owner` "
        "OR `task`.`guildid` <> `claim`.`GuildID` OR `task`.`type` <> 'killTask' "
        "OR `task`.`value` <> `claim`.`CreatureEntry` OR `task`.`time` > `claim`.`EventTime` "
        "OR CAST(`task`.`time` AS UNSIGNED) + `task`.`validIn` <= `claim`.`EventTime` "
        "OR `task`.`data` = 'kill-complete-v1')", CONNECTION_ASYNC);
    // Pre-claim lookup uses event time, not delayed callback time. NULL id is the explicit end-of-page sentinel.
    PrepareStatement(PLAYERBOTS_SEL_GUILD_KILL_TASK_PAGE,
        "SELECT `task`.`id`, `task`.`guildid` FROM (SELECT 1) AS `seed` LEFT JOIN "
        "(SELECT `id`, `guildid` FROM `playerbots_guild_tasks` WHERE `owner` = ? AND `type` = 'killTask' "
        "AND `value` = ? AND `time` <= ? AND CAST(`time` AS UNSIGNED) + `validIn` > ? "
        "AND COALESCE(`data`, '') = '' AND `id` > ? ORDER BY `id` LIMIT 16) AS `task` "
        "ON 1 = 1 ORDER BY `task`.`id`", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASKS_BY_OWNER_AND_TYPE, "SELECT `value`, `time`, validIn FROM playerbots_guild_tasks WHERE owner = ? AND guildid = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASKS_BY_OWNER_DISTINCT, "SELECT DISTINCT guildid FROM playerbots_guild_tasks WHERE owner = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_GUILD_TASKS_BY_OWNER_ORDERED, "SELECT `value`, `time`, validIn, guildid FROM playerbots_guild_tasks WHERE owner = ? AND type = ? ORDER BY guildid", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_DEL_GUILD_TASKS, "DELETE FROM playerbots_guild_tasks WHERE owner = ? AND guildid = ? AND `type` = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_ALL_GUILD_TASKS, "DELETE FROM playerbots_guild_tasks", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_INS_GUILD_TASKS, "INSERT INTO playerbots_guild_tasks (owner, guildid, `time`, validIn, `type`, `value`) VALUES (?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_RANDOM_BOTS_VALUE, "SELECT value FROM playerbots_random_bots WHERE event = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BOT, "SELECT `bot` FROM playerbots_random_bots WHERE event = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_OWNER_AND_EVENT, "SELECT bot FROM playerbots_random_bots WHERE owner = ? AND event = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_OWNER_AND_BOT, "SELECT `event`, `value`, `time`, validIn, `data` FROM playerbots_random_bots WHERE owner = ? AND bot = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_EVENT_AND_VALUE, "SELECT bot FROM playerbots_random_bots WHERE event = ? AND value = ?", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_RANDOM_BOTS, "INSERT INTO playerbots_random_bots (owner, bot, `time`, validIn, event, `value`, `data`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_RANDOM_BOTS, "DELETE FROM playerbots_random_bots", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_RANDOM_BOTS_BY_OWNER, "DELETE FROM playerbots_random_bots WHERE owner = ? AND bot = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_RANDOM_BOTS_BY_OWNER_AND_EVENT, "DELETE FROM playerbots_random_bots WHERE owner = ? AND bot = ? AND event = ?", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_UPD_RANDOM_BOTS, "UPDATE playerbots_random_bots SET validIn = ? WHERE event = ? AND bot = ?", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_RARITY_CACHE, "SELECT item, rarity FROM playerbots_rarity_cache", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_RARITY_CACHE, "INSERT INTO playerbots_rarity_cache (item, rarity) VALUES (?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_RNDITEM_CACHE, "SELECT lvl, type, item FROM playerbots_rnditem_cache", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_RNDITEM_CACHE, "INSERT INTO playerbots_rnditem_cache (lvl, type, item) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_SPEECH, "SELECT name, text, type FROM playerbots_speech", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_SPEECH_PROBABILITY, "SELECT name, probability FROM playerbots_speech_probability", CONNECTION_SYNCH);

    PrepareStatement(PLAYERBOTS_SEL_TELE_CACHE, "SELECT map_id, x, y, z, level FROM playerbots_tele_cache", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_TELE_CACHE, "INSERT INTO playerbots_tele_cache (level, map_id, x, y, z) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_TRAVELNODE, "SELECT id, name, map_id, x, y, z, linked FROM playerbots_travelnode", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_TRAVELNODE, "INSERT INTO `playerbots_travelnode` (`id`, `name`, `map_id`, `x`, `y`, `z`, `linked`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_TRAVELNODE, "DELETE FROM playerbots_travelnode", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_TRAVELNODE_LINK, "SELECT node_id, to_node_id,type,object,distance,swim_distance, extra_cost,calculated, max_creature_0,max_creature_1,max_creature_2 FROM playerbots_travelnode_link", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_TRAVELNODE_LINK, "INSERT INTO `playerbots_travelnode_link` (`node_id`, `to_node_id`,`type`,`object`,`distance`,`swim_distance`, `extra_cost`,`calculated`, `max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_TRAVELNODE_LINK, "DELETE FROM playerbots_travelnode_link", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_TRAVELNODE_PATH, "SELECT node_id, to_node_id, nr, map_id, x, y, z FROM playerbots_travelnode_path order by node_id, to_node_id, nr", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_INS_TRAVELNODE_PATH, "INSERT INTO `playerbots_travelnode_path` (`node_id`, `to_node_id`, `nr`, `map_id`, `x`, `y`, `z`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_TRAVELNODE_PATH, "DELETE FROM playerbots_travelnode_path", CONNECTION_ASYNC);

    PrepareStatement(PLAYERBOTS_SEL_TEXT, "SELECT `name`, `text`, `say_type`, `reply_type`, `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`, `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8` FROM `ai_playerbot_texts`", CONNECTION_SYNCH);
    PrepareStatement(
        PLAYERBOTS_SEL_DUNGEON_SUGGESTION,
        "SELECT"
        "   d.`name`, "
        "   d.`difficulty`, "
        "   d.`min_level`, "
        "   d.`max_level`, "
        "   a.`abbrevation`, "
        "   s.`strategy` "
        "FROM playerbots_dungeon_suggestion_definition d "
        "LEFT OUTER JOIN playerbots_dungeon_suggestion_abbrevation a "
        "   ON d.slug = a.definition_slug "
        "LEFT OUTER JOIN playerbots_dungeon_suggestion_strategy s "
        "   ON d.slug = s.definition_slug "
        "   AND d.difficulty = s.difficulty "
        "WHERE d.expansion <= ?;",
        CONNECTION_SYNCH
    );

    PrepareStatement(PLAYERBOTS_SEL_WEIGHTSCALES, "SELECT id, name, class FROM playerbots_weightscales", CONNECTION_SYNCH);
    PrepareStatement(PLAYERBOTS_SEL_WEIGHTSCALE_DATA, "SELECT id, field, val FROM playerbots_weightscale_data", CONNECTION_SYNCH);

    PrepareStatement(PLAYERBOTS_INS_EQUIP_CACHE_NEW, "INSERT INTO playerbots_item_info_cache (id, quality, slot, source, sourceId, team, faction, factionRepRank, minLevel, "
            "scale_1, scale_2, scale_3, scale_4, scale_5, scale_6, scale_7, scale_8, scale_9, scale_10, scale_11, scale_12, scale_13, scale_14, scale_15, "
            "scale_16, scale_17, scale_18, scale_19, scale_20, scale_21, scale_22, scale_23, scale_24, scale_25, scale_26, scale_27, scale_28, scale_29, scale_30, scale_31, scale_32) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(PLAYERBOTS_DEL_EQUIP_CACHE_NEW, "DELETE FROM playerbots_item_info_cache WHERE id = ?", CONNECTION_ASYNC);

    // Always returns 32 rows, including missing profiles: an empty result is a failed read, not new identities.
    PrepareStatement(PLAYERBOTS_SEL_AUTONOMOUS_PROFILES,
        "SELECT ids.guid, p.version, p.revision, p.payload FROM ("
        "SELECT CAST(? AS UNSIGNED) AS guid UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED) "
        "UNION ALL SELECT CAST(? AS UNSIGNED) UNION ALL SELECT CAST(? AS UNSIGNED)"
        ") ids LEFT JOIN playerbots_autonomous_profile p ON p.guid = ids.guid", CONNECTION_SYNCH);
    // Upgrade codec only with a current revision; delayed old-codec writes must never downgrade a biography.
    // Payload/time precede version and revision assignments, so their guards see the original row.
    PrepareStatement(PLAYERBOTS_INS_AUTONOMOUS_PROFILE,
        "INSERT INTO playerbots_autonomous_profile (guid, version, revision, updated_at, payload) "
        "VALUES (?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE "
        "payload = IF(version <= VALUES(version) AND revision <= VALUES(revision), VALUES(payload), payload), "
        "updated_at = IF(version <= VALUES(version) AND revision <= VALUES(revision), VALUES(updated_at), updated_at), "
        "version = IF(version <= VALUES(version) AND revision <= VALUES(revision), VALUES(version), version), "
        "revision = IF(version = VALUES(version), GREATEST(revision, VALUES(revision)), revision)", CONNECTION_ASYNC);
}

PlayerbotsDatabaseConnection::PlayerbotsDatabaseConnection(MySQLConnectionInfo& connInfo) : MySQLConnection(connInfo)
{
}

PlayerbotsDatabaseConnection::PlayerbotsDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo) : MySQLConnection(q, connInfo)
{
}

PlayerbotsDatabaseConnection::~PlayerbotsDatabaseConnection()
{
}

#endif
