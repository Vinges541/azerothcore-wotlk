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

#ifndef _TRANSACTION_H
#define _TRANSACTION_H

#include "DatabaseEnvFwd.h"
#include "Define.h"
#include "SQLOperation.h"
#include "StringFormat.h"
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

/*! Transactions, high level class. */
class AC_DATABASE_API TransactionBase
{
    friend class TransactionTask;
    friend class MySQLConnection;

    template <typename T>
    friend class DatabaseWorkerPool;

public:
    TransactionBase()  = default;
    virtual ~TransactionBase() { Cleanup(); }

    void Append(std::string_view sql);

    template<typename... Args>
    void Append(std::string_view sql, Args&&... args)
    {
        Append(Acore::StringFormat(sql, std::forward<Args>(args)...));
    }

    [[nodiscard]] std::size_t GetSize() const { return m_queries.size(); }

    // Retain non-gameplay resources through asynchronous execution/retries and transaction destruction.
    // Attach before dispatch. This is NOT a commit-success callback; do not retain Player/Item pointers here.
    void KeepAlive(std::shared_ptr<void> resource)
    {
        if (resource)
            m_resources.emplace_back(std::move(resource));
    }

protected:
    void AppendPreparedStatement(PreparedStatementBase* statement, std::optional<uint64> expectedAffectedRows = {});
    void Cleanup();
    std::vector<SQLElementData> m_queries;

private:
    bool _cleanedUp{false};
    std::vector<std::shared_ptr<void>> m_resources;
};

template<typename T>
class Transaction : public TransactionBase
{
public:
    using TransactionBase::Append;

    void Append(PreparedStatement<T>* statement)
    {
        AppendPreparedStatement(statement);
    }

    // DML only. A mismatched affected-row count aborts and rolls back the entire transaction.
    // Use for conditional writes whose no-op must not allow dependent statements/hooks to commit.
    void AppendWithAffectedRows(PreparedStatement<T>* statement, uint64 expectedAffectedRows)
    {
        AppendPreparedStatement(statement, expectedAffectedRows);
    }
};

/*! Low level class*/
class AC_DATABASE_API TransactionTask : public SQLOperation
{
    template <class T>
    friend class DatabaseWorkerPool;

    friend class DatabaseWorker;
    friend class TransactionCallback;

public:
    TransactionTask(std::shared_ptr<TransactionBase> trans) : m_trans(std::move(trans)) { }
    ~TransactionTask() override = default;

protected:
    bool Execute() override;
    int TryExecute();
    void CleanupOnFailure();

    std::shared_ptr<TransactionBase> m_trans;
    static std::mutex _deadlockLock;
};

class AC_DATABASE_API TransactionWithResultTask : public TransactionTask
{
public:
    TransactionWithResultTask(std::shared_ptr<TransactionBase> trans) : TransactionTask(trans) { }

    TransactionFuture GetFuture() { return m_result.get_future(); }

protected:
    bool Execute() override;

    TransactionPromise m_result;
};

class AC_DATABASE_API TransactionCallback
{
public:
    TransactionCallback(TransactionFuture&& future) : m_future(std::move(future)) { }
    TransactionCallback(TransactionCallback&&) = default;

    TransactionCallback& operator=(TransactionCallback&&) = default;

    void AfterComplete(std::function<void(bool)> callback) &
    {
        m_callback = std::move(callback);
    }

    bool InvokeIfReady();

    TransactionFuture m_future;
    std::function<void(bool)> m_callback;
};

#endif
