-- General durable item custody. Native saves/deletes must respect every owner's guard.
-- Only the owning protocol may release its exact token in its terminal transaction.
CREATE TABLE IF NOT EXISTS `item_custody` (
    `ItemGUID` INT UNSIGNED NOT NULL,
    `Owner` VARBINARY(64) NOT NULL COMMENT 'Stable protocol namespace',
    `Token` BIGINT UNSIGNED NOT NULL COMMENT 'Durable identity inside the owning protocol',
    PRIMARY KEY (`ItemGUID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Compatibility import remains effective when the old module is not loaded.
-- Do not replace a conflicting owner: all native writes stay fenced and module recovery fails closed.
-- Replaying this migration never releases a guard, including quarantined/missing-item receipts.
SELECT IF(
    EXISTS (SELECT 1 FROM `information_schema`.`tables`
        WHERE `table_schema` = DATABASE() AND `table_name` = 'playerbots_guild_mail_receipt'),
    CONCAT('INSERT INTO `item_custody` (`ItemGUID`, `Owner`, `Token`) ',
        'SELECT `HeldItemGUID`, ''playerbots.guild-mail'', `ReceiptID` ',
        'FROM `playerbots_guild_mail_receipt` WHERE `HeldItemGUID` IS NOT NULL ',
        'ON DUPLICATE KEY UPDATE `ItemGUID` = VALUES(`ItemGUID`)'),
    'SELECT 1'
) INTO @item_custody_import;
PREPARE item_custody_import_statement FROM @item_custody_import;
EXECUTE item_custody_import_statement;
DEALLOCATE PREPARE item_custody_import_statement;
SET @item_custody_import = NULL;
