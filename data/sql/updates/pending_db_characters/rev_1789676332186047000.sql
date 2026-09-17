-- Durable attachment custody for the playerbots guild contribution outbox.
-- Schema only: legacy guild mail processing does not write receipts yet.
-- Keep item_instance intact while held; item entry/count cannot reconstruct an item.
-- Acquiring custody and removing the source mail_items link must share one transaction.
-- Release HeldItemGUID only in the transaction that consumes/returns the item and marks delivery.
-- Source identifiers are historical, not unique: native mail/item IDs may be reused after restart.
-- Never reset ReceiptID or cascade-delete receipts when a character or guild is deleted.
CREATE TABLE IF NOT EXISTS `playerbots_guild_mail_receipt` (
    `ReceiptID` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `MailID` INT UNSIGNED NOT NULL,
    `SourceItemGUID` INT UNSIGNED NOT NULL,
    `HeldItemGUID` INT UNSIGNED DEFAULT NULL COMMENT 'Unique live custody; NULL only after finalization',
    `Sender` INT UNSIGNED NOT NULL,
    `Receiver` INT UNSIGNED NOT NULL,
    `GuildID` INT UNSIGNED NOT NULL,
    `ItemEntry` INT UNSIGNED NOT NULL,
    `ItemCount` INT UNSIGNED NOT NULL,
    `EventTime` INT UNSIGNED NOT NULL,
    `State` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 held, 1 resolved, 2 delivered, 3 quarantined',
    `TaskID` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Exact task selected by the contribution ledger',
    `AcceptedCount` INT UNSIGNED NOT NULL DEFAULT 0,
    `PaymentCopper` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `DeliveryMailID` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Historical return/payment mail, if any',
    `UpdatedAt` BIGINT UNSIGNED NOT NULL COMMENT 'Unix seconds, for diagnosis rather than lease ownership',
    PRIMARY KEY (`ReceiptID`),
    UNIQUE KEY `uq_held_item` (`HeldItemGUID`),
    KEY `idx_state_receipt` (`State`, `ReceiptID`),
    KEY `idx_sender` (`Sender`),
    KEY `idx_receiver` (`Receiver`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
