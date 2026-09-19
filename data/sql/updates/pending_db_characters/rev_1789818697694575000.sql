-- General durable mail-ID reservation. A reference may outlive its mail row.
-- Writers advance the watermark in the same transaction as the durable reference.
CREATE TABLE IF NOT EXISTS `mail_id_high_water` (
    `id` TINYINT UNSIGNED NOT NULL COMMENT 'Singleton key: 1',
    `value` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One-time compatibility import from the former module-specific reservation source.
-- Fresh core-only databases need not have that table. Never lower or delete an existing reservation.
SELECT IF(
    EXISTS (SELECT 1 FROM `information_schema`.`tables`
        WHERE `table_schema` = DATABASE() AND `table_name` = 'playerbots_guild_mail_receipt'),
    CONCAT('INSERT INTO `mail_id_high_water` (`id`, `value`) SELECT 1, COALESCE(MAX(`DeliveryMailID`), 0) ',
        'FROM `playerbots_guild_mail_receipt` ',
        'ON DUPLICATE KEY UPDATE `value` = GREATEST(`value`, VALUES(`value`))'),
    'SELECT 1'
) INTO @mail_id_import;
PREPARE mail_id_import_statement FROM @mail_id_import;
EXECUTE mail_id_import_statement;
DEALLOCATE PREPARE mail_id_import_statement;
SET @mail_id_import = NULL;
