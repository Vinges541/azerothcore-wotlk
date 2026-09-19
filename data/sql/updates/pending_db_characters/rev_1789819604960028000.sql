-- The guild-mail schema now belongs to mod-playerbots, not the core.
-- Preserve historical journal rows when the module is absent on the first upgraded start.
-- Names/hashes are unchanged; no new entry is created and no schema is marked applied prematurely.
UPDATE `updates` SET `state` = 'MODULE'
WHERE `name` IN ('rev_1789676332186047000.sql', 'rev_1789679944721894000.sql');
