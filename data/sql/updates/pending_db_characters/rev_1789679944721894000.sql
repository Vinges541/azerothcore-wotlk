-- Keep startup mail-ID recovery indexed even after the original mail has been deleted.
ALTER TABLE `playerbots_guild_mail_receipt`
    ADD KEY `idx_delivery_mail` (`DeliveryMailID`);
