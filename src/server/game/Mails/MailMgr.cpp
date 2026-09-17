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

#include "MailMgr.h"
#include "CharacterCache.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Timer.h"
#include <map>
#include <limits>

MailMgr* MailMgr::instance()
{
    static MailMgr instance;
    return &instance;
}

std::shared_ptr<void> MailMgr::BeginMailboxLoad(ObjectGuid character)
{
    if (!character.IsPlayer())
        return {};
    auto state = mailboxAccess;
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->mutations.contains(character))
            return {};
        auto& readers = state->loads[character];
        if (readers == std::numeric_limits<uint32>::max())
            return {};
        ++readers;
    }
    return std::shared_ptr<void>(state.get(), [state, character](void*)
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto it = state->loads.find(character);
        if (it != state->loads.end() && !--it->second)
            state->loads.erase(it);
    });
}

std::shared_ptr<void> MailMgr::BeginMailboxMaintenance()
{
    auto state = mailboxAccess;
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (!state->mutations.empty() || state->maintenance == std::numeric_limits<uint32>::max())
            return {};
        ++state->maintenance;
    }
    return std::shared_ptr<void>(state.get(), [state](void*)
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        --state->maintenance;
    });
}

uint64 MailMgr::BeginMailboxMutation(ObjectGuid first, ObjectGuid second)
{
    if (!first.IsPlayer() || !second.IsPlayer() || first == second)
        return 0;
    auto& state = *mailboxAccess;
    std::lock_guard<std::mutex> guard(state.mutex);
    // At most 512 simultaneous two-character operations; live tokens are never evicted.
    if (state.maintenance || state.mutations.size() >= 1024 ||
        state.serial == std::numeric_limits<uint64>::max() ||
        state.loads.contains(first) || state.loads.contains(second) ||
        state.mutations.contains(first) || state.mutations.contains(second))
        return 0;
    uint64 token = ++state.serial;
    state.mutations.emplace(first, token);
    state.mutations.emplace(second, token);
    return token;
}

bool MailMgr::EndMailboxMutation(ObjectGuid first, ObjectGuid second, uint64 token)
{
    if (!token || first == second)
        return false;
    auto& state = *mailboxAccess;
    std::lock_guard<std::mutex> guard(state.mutex);
    auto a = state.mutations.find(first);
    auto b = state.mutations.find(second);
    if (a == state.mutations.end() || b == state.mutations.end() || a->second != token || b->second != token)
        return false;
    state.mutations.erase(a);
    state.mutations.erase(b);
    return true;
}

void MailMgr::OnMailSent(ObjectGuid::LowType receiverLow)
{
    sCharacterCache->IncreaseCharacterMailCount(ObjectGuid(HighGuid::Player, receiverLow));
}

void MailMgr::OnMailDeleted(ObjectGuid::LowType receiverLow)
{
    sCharacterCache->DecreaseCharacterMailCount(ObjectGuid(HighGuid::Player, receiverLow));
}

void MailMgr::OnMailReturned(ObjectGuid::LowType oldReceiverLow, ObjectGuid::LowType newReceiverLow)
{
    sCharacterCache->DecreaseCharacterMailCount(ObjectGuid(HighGuid::Player, oldReceiverLow));
    sCharacterCache->IncreaseCharacterMailCount(ObjectGuid(HighGuid::Player, newReceiverLow));
}

void MailMgr::LoadMailCounts()
{
    QueryResult result = CharacterDatabase.Query("SELECT receiver, COUNT(receiver) FROM mail GROUP BY receiver");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        sCharacterCache->UpdateCharacterMailCount(ObjectGuid(HighGuid::Player, fields[0].Get<uint32>()), static_cast<int32>(fields[1].Get<uint64>()), true);
    } while (result->NextRow());
}

void MailMgr::RecountMailCount(ObjectGuid::LowType receiverLow)
{
    int32 count = 0;
    if (QueryResult result = CharacterDatabase.Query("SELECT COUNT(*) FROM mail WHERE receiver = {}", receiverLow))
        count = static_cast<int32>((*result)[0].Get<uint64>());

    sCharacterCache->UpdateCharacterMailCount(ObjectGuid(HighGuid::Player, receiverLow), count, true);
}

void MailMgr::DeleteEmptyExpiredMail(uint32 mailId, ObjectGuid::LowType receiverLow)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
    stmt->SetData(0, mailId);
    CharacterDatabase.Execute(stmt);

    OnMailDeleted(receiverLow);
}

bool MailMgr::ReturnOrDeleteOldMails(bool serverUp)
{
    auto maintenanceLease = BeginMailboxMaintenance();
    if (!maintenanceLease)
    {
        LOG_DEBUG("server.mail", "Deferring expired-mail cleanup while mailbox custody is active");
        return false;
    }
    uint32 oldMSTime = getMSTime();

    time_t curTime = GameTime::GetGameTime().count();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL);
    stmt->SetData(0, uint32(curTime));
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return true;

    std::map<uint32 /*messageId*/, MailItemInfoVec> itemsCache;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL_ITEMS);
    stmt->SetData(0, uint32(curTime));
    if (PreparedQueryResult items = CharacterDatabase.Query(stmt))
    {
        MailItemInfo item;
        do
        {
            Field* fields = items->Fetch();
            item.item_guid = fields[0].Get<uint32>();
            item.item_template = fields[1].Get<uint32>();
            uint32 mailId = fields[2].Get<uint32>();
            itemsCache[mailId].push_back(item);
        } while (items->NextRow());
    }

    uint32 deletedCount = 0;
    uint32 returnedCount = 0;
    do
    {
        Field* fields = result->Fetch();
        Mail* m = new Mail;
        m->messageID      = fields[0].Get<uint32>();
        m->messageType    = fields[1].Get<uint8>();
        m->sender         = fields[2].Get<uint32>();
        m->receiver       = fields[3].Get<uint32>();
        bool has_items    = fields[4].Get<bool>();
        m->expire_time    = time_t(fields[5].Get<uint32>());
        m->deliver_time   = time_t(0);
        m->stationery     = fields[6].Get<uint8>();
        m->checked        = fields[7].Get<uint8>();
        m->mailTemplateId = fields[8].Get<int16>();

        Player* player = nullptr;
        if (serverUp)
            player = ObjectAccessor::FindPlayerByLowGUID(m->receiver);

        if (player) // don't modify mails of a logged in player
        {
            delete m;
            continue;
        }

        // Keep each mail's correlated writes atomic
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        trans->KeepAlive(maintenanceLease);

        // Delete or return mail
        if (has_items)
        {
            // read items from cache
            m->items.swap(itemsCache[m->messageID]);

            // If it is mail from non-player, or if it's already return mail, it shouldn't be returned, but deleted
            if (!m->IsSentByPlayer() || m->IsSentByGM() || (m->IsCODPayment() || m->IsReturnedMail()))
            {
                for (auto const& mailedItem : m->items)
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
                    stmt->SetData(0, mailedItem.item_guid);
                    trans->Append(stmt);
                }

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM_BY_ID);
                stmt->SetData(0, m->messageID);
                trans->Append(stmt);
            }
            else
            {
                // Mail will be returned
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_RETURNED);
                stmt->SetData(0, m->receiver);
                stmt->SetData(1, m->sender);
                stmt->SetData(2, uint32(curTime + 30 * DAY));
                stmt->SetData(3, uint32(curTime));
                stmt->SetData(4, uint8(MAIL_CHECK_MASK_RETURNED));
                stmt->SetData(5, m->messageID);
                trans->Append(stmt);
                for (auto const& mailedItem : m->items)
                {
                    // Update receiver in mail items for its proper delivery, and in instance_item for avoid lost item at sender delete
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_ITEM_RECEIVER);
                    stmt->SetData(0, m->sender);
                    stmt->SetData(1, mailedItem.item_guid);
                    trans->Append(stmt);

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_OWNER);
                    stmt->SetData(0, m->sender);
                    stmt->SetData(1, mailedItem.item_guid);
                    trans->Append(stmt);
                }

                CharacterDatabase.CommitTransaction(trans);

                OnMailReturned(m->receiver, m->sender);

                delete m;
                ++returnedCount;
                continue;
            }
        }

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
        stmt->SetData(0, m->messageID);
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);

        OnMailDeleted(m->receiver);

        delete m;
        ++deletedCount;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Processed {} expired mails: {} deleted and {} returned in {} ms", deletedCount + returnedCount, deletedCount, returnedCount, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
    return true;
}
