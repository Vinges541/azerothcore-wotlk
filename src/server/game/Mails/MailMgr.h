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

#ifndef _MAILMGR_H
#define _MAILMGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <map>
#include <memory>
#include <mutex>

/**
 * @brief Owns the mail lifecycle bookkeeping that lives outside a single player
 * session: the per-character mail count mirrored in CharacterCache and the
 * cleanup of expired mail.
 *
 * Every code path that inserts or deletes a row in the characters `mail` table
 * must report it here, otherwise the cached count drifts until the next recount.
 */
class AC_GAME_API MailMgr
{
public:
    static MailMgr* instance();

    // Login holders retain a read lease through SQL execution AND native Player construction.
    // Calls never wait for SQL. A conflicting operation must retry later.
    std::shared_ptr<void> BeginMailboxLoad(ObjectGuid character);

    // Expiry snapshots cover many characters. Hold this before reading and through all resulting SQL writes.
    // Busy custody makes maintenance retry later, without scanning every receipt or blocking a thread.
    std::shared_ptr<void> BeginMailboxMaintenance();

    // Acquire both participants atomically before asynchronous custody work. Zero means busy/invalid.
    // No timeout: an uncertain commit must retain the token through recovery and native reconciliation.
    uint64 BeginMailboxMutation(ObjectGuid first, ObjectGuid second);
    bool EndMailboxMutation(ObjectGuid first, ObjectGuid second, uint64 token);
    bool HasMailboxMutation(ObjectGuid first, ObjectGuid second, uint64 token) const;

    /**
     * @brief Reports a mail row inserted for a character.
     * @param receiverLow Low GUID of the mail receiver
     */
    void OnMailSent(ObjectGuid::LowType receiverLow);

    /**
     * @brief Reports a mail row deleted from a character's mailbox.
     * @param receiverLow Low GUID of the mail receiver
     */
    void OnMailDeleted(ObjectGuid::LowType receiverLow);

    /**
     * @brief Reports a mail row handed to a new receiver (return to sender).
     * @param oldReceiverLow Low GUID of the previous receiver
     * @param newReceiverLow Low GUID of the new receiver
     */
    void OnMailReturned(ObjectGuid::LowType oldReceiverLow, ObjectGuid::LowType newReceiverLow);

    /**
     * @brief Recounts the mail of all characters from the database.
     * Called once at startup after the character cache is filled.
     */
    void LoadMailCounts();

    /**
     * @brief Recounts one character's mail from the database, overwriting the
     * cached value.
     * @param receiverLow Low GUID of the character to recount
     */
    void RecountMailCount(ObjectGuid::LowType receiverLow);

    /**
     * @brief Deletes an expired mail row that has no items, money or COD
     * attached. Used at login for mail that would otherwise stay invisible in
     * the DB until ReturnOrDeleteOldMails catches the receiver offline.
     * @param mailId Id of the mail row to delete
     * @param receiverLow Low GUID of the mail receiver
     */
    void DeleteEmptyExpiredMail(uint32 mailId, ObjectGuid::LowType receiverLow);

    /**
     * @brief Returns expired mail with items to the sender and deletes the rest.
     * @param serverUp When true, receivers that are currently online are skipped.
     * @return False when custody deferred the attempt; the world scheduler should retry soon.
     */
    bool ReturnOrDeleteOldMails(bool serverUp);

private:
    struct MailboxAccess
    {
        std::mutex mutex;
        std::map<ObjectGuid, uint32> loads;
        std::map<ObjectGuid, uint64> mutations;
        uint64 serial = 0;
        uint32 maintenance = 0;
    };
    // Query holders can outlive singleton destruction during shutdown; leases own this state, not MailMgr.
    std::shared_ptr<MailboxAccess> mailboxAccess = std::make_shared<MailboxAccess>();
};

#define sMailMgr MailMgr::instance()

#endif
