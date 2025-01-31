//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

//==============================================================================
/*
    2017-06-26  ajochems        Refactored for casinocoin
*/
//==============================================================================

#ifndef CASINOCOIN_APP_LEDGER_LEDGERMASTER_H_INCLUDED
#define CASINOCOIN_APP_LEDGER_LEDGERMASTER_H_INCLUDED

#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/ledger/AbstractFetchPackContainer.h>
#include <casinocoin/app/ledger/Ledger.h>
#include <casinocoin/app/ledger/LedgerCleaner.h>
#include <casinocoin/app/ledger/LedgerHistory.h>
#include <casinocoin/app/ledger/LedgerHolder.h>
#include <casinocoin/app/misc/CanonicalTXSet.h>
#include <casinocoin/basics/chrono.h>
#include <casinocoin/basics/RangeSet.h>
#include <casinocoin/basics/ScopedLock.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/protocol/CasinocoinLedgerHash.h>
#include <casinocoin/protocol/STValidation.h>
#include <casinocoin/beast/insight/Collector.h>
#include <casinocoin/core/Stoppable.h>
#include <casinocoin/beast/utility/PropertyStream.h>
#include <mutex>

#include "casinocoin.pb.h"

namespace casinocoin {

class Peer;
class Transaction;

struct LedgerReplay
{
    std::map< int, std::shared_ptr<STTx const> > txns_;
    NetClock::time_point closeTime_;
    int closeFlags_;
    std::shared_ptr<Ledger const> prevLedger_;
};

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster
    : public Stoppable
    , public AbstractFetchPackContainer
{
public:
    explicit
    LedgerMaster(Application& app, Stopwatch& stopwatch,
        Stoppable& parent,
            beast::insight::Collector::ptr const& collector,
                beast::Journal journal);

    virtual ~LedgerMaster () = default;

    LedgerIndex getCurrentLedgerIndex ();
    LedgerIndex getValidLedgerIndex ();

    bool isCompatible (
        ReadView const&,
        beast::Journal::Stream,
        char const* reason);

    std::recursive_mutex& peekMutex ();

    // The current ledger is the ledger we believe new transactions should go in
    std::shared_ptr<ReadView const>
    getCurrentLedger();

    // The finalized ledger is the last closed/accepted ledger
    std::shared_ptr<Ledger const>
    getClosedLedger()
    {
        return mClosedLedger.get();
    }

    // The validated ledger is the last fully validated ledger
    std::shared_ptr<Ledger const>
    getValidatedLedger ()
    {
        return mValidLedger.get();
    }

    // The Rules are in the last fully validated ledger if there is one.
    Rules getValidatedRules();

    // This is the last ledger we published to clients and can lag the validated
    // ledger
    std::shared_ptr<ReadView const>
    getPublishedLedger();

    std::chrono::seconds getPublishedLedgerAge ();
    std::chrono::seconds getValidatedLedgerAge ();
    bool isCaughtUp(std::string& reason);

    std::uint32_t getEarliestFetch ();

    bool storeLedger (std::shared_ptr<Ledger const> ledger);

    void setFullLedger (
        std::shared_ptr<Ledger const> const& ledger,
            bool isSynchronous, bool isCurrent);

    void switchLCL (std::shared_ptr<Ledger const> const& lastClosed);

    void failedSave(std::uint32_t seq, uint256 const& hash);

    std::string getCompleteLedgers ();

    void addLostLedgerToRangeSet (std::uint32_t seq);

    /** Apply held transactions to the open ledger
        This is normally called as we close the ledger.
        The open ledger remains open to handle new transactions
        until a new open ledger is built.
    */
    void applyHeldTransactions ();

    /** Get all the transactions held for a particular account.
        This is normally called when a transaction for that
        account is successfully applied to the open
        ledger so those transactions can be resubmitted without
        waiting for ledger close.
    */
    std::vector<std::shared_ptr<STTx const>>
    pruneHeldTransactions(AccountID const& account,
        std::uint32_t const seq);

    /** Get a ledger's hash by sequence number using the cache
    */
    uint256 getHashBySeq (std::uint32_t index);

    /** Walk to a ledger's hash using the skip list */
    boost::optional<LedgerHash> walkHashBySeq (std::uint32_t index);

    /** Walk the chain of ledger hashes to determine the hash of the
        ledger with the specified index. The referenceLedger is used as
        the base of the chain and should be fully validated and must not
        precede the target index. This function may throw if nodes
        from the reference ledger or any prior ledger are not present
        in the node store.
    */
    boost::optional<LedgerHash> walkHashBySeq (
        std::uint32_t index,
        std::shared_ptr<ReadView const> const& referenceLedger);

    std::shared_ptr<Ledger const>
    getLedgerBySeq (std::uint32_t index);

    std::shared_ptr<Ledger const>
    getLedgerByHash (uint256 const& hash);

    void setLedgerRangePresent (
        std::uint32_t minV, std::uint32_t maxV);

    boost::optional<LedgerHash> getLedgerHash(
        std::uint32_t desiredSeq,
        std::shared_ptr<ReadView const> const& knownGoodLedger);

    boost::optional <NetClock::time_point> getCloseTimeBySeq (
        LedgerIndex ledgerIndex);

    boost::optional <NetClock::time_point> getCloseTimeByHash (
        LedgerHash const& ledgerHash);

    void addHeldTransaction (std::shared_ptr<Transaction> const& trans);
    void fixMismatch (ReadView const& ledger);

    bool haveLedger (std::uint32_t seq);
    void clearLedger (std::uint32_t seq);
    bool getValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal);
    bool getFullValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal);

    void tune (int size, int age);
    void sweep ();
    float getCacheHitRate ();

    void checkAccept (std::shared_ptr<Ledger const> const& ledger);
    void checkAccept (uint256 const& hash, std::uint32_t seq);
    void consensusBuilt (std::shared_ptr<Ledger const> const& ledger, Json::Value consensus);

    LedgerIndex getBuildingLedger ();
    void setBuildingLedger (LedgerIndex index);

    void tryAdvance ();
    void newPathRequest ();
    bool isNewPathRequest ();
    void newOrderBookDB ();

    bool fixIndex (
        LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);
    void doLedgerCleaner(Json::Value const& parameters);

    beast::PropertyStream::Source& getPropertySource ();

    void clearPriorLedgers (LedgerIndex seq);

    void clearLedgerCachePrior (LedgerIndex seq);

    // ledger replay
    void takeReplay (std::unique_ptr<LedgerReplay> replay);
    std::unique_ptr<LedgerReplay> releaseReplay ();

    // Fetch Packs
    void gotFetchPack (
        bool progress,
        std::uint32_t seq);

    void addFetchPack (
        uint256 const& hash,
        std::shared_ptr<Blob>& data);

    boost::optional<Blob>
    getFetchPack (uint256 const& hash) override;

    void makeFetchPack (
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        std::uint32_t uUptime);

    std::size_t getFetchPackCacheSize () const;

private:
    void setValidLedger(
        std::shared_ptr<Ledger const> const& l);
    void setPubLedger(
        std::shared_ptr<Ledger const> const& l);

    void tryFill(
        Job& job,
        std::shared_ptr<Ledger const> ledger);

    void getFetchPack(LedgerHash missingHash, LedgerIndex missingIndex);
    boost::optional<LedgerHash> getLedgerHashForHistory(LedgerIndex index);
    std::size_t getNeededValidations();
    void advanceThread();
    // Try to publish ledgers, acquire missing ledgers
    void doAdvance();
    bool shouldFetchPack(std::uint32_t seq) const;
    bool shouldAcquire(
        std::uint32_t const currentLedger,
        std::uint32_t const ledgerHistory,
        std::uint32_t const ledgerHistoryIndex,
        std::uint32_t const candidateLedger) const;

    std::vector<std::shared_ptr<Ledger const>>
    findNewLedgersToPublish();

    void updatePaths(Job& job);
    void newPFWork(const char *name);

private:
    using ScopedLockType = std::lock_guard <std::recursive_mutex>;
    using ScopedUnlockType = GenericScopedUnlock <std::recursive_mutex>;

    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mutable m_mutex;

    // The ledger that most recently closed.
    LedgerHolder mClosedLedger;

    // The highest-sequence ledger we have fully accepted.
    LedgerHolder mValidLedger;

    // The last ledger we have published.
    std::shared_ptr<Ledger const> mPubLedger;

    // The last ledger we did pathfinding against.
    std::shared_ptr<Ledger const> mPathLedger;

    // The last ledger we handled fetching history
    std::shared_ptr<Ledger const> mHistLedger;

    // Fully validated ledger, whether or not we have the ledger resident.
    std::pair <uint256, LedgerIndex> mLastValidLedger;

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData;

    std::recursive_mutex mCompleteLock;
    RangeSet mCompleteLedgers;

    std::unique_ptr <detail::LedgerCleaner> mLedgerCleaner;

    uint256 mLastValidateHash;
    std::uint32_t mLastValidateSeq;

    // Publish thread is running.
    bool                        mAdvanceThread;

    // Publish thread has work to do.
    bool                        mAdvanceWork;
    int                         mFillInProgress;

    int     mPathFindThread;    // Pathfinder jobs dispatched
    bool    mPathFindNewRequest;

    std::atomic <std::uint32_t> mPubLedgerClose;
    std::atomic <std::uint32_t> mPubLedgerSeq;
    std::atomic <std::uint32_t> mValidLedgerSign;
    std::atomic <std::uint32_t> mValidLedgerSeq;
    std::atomic <std::uint32_t> mBuildingLedgerSeq;

    // The server is in standalone mode
    bool const standalone_;

    // How many ledgers before the current ledger do we allow peers to request?
    std::uint32_t const fetch_depth_;

    // How much history do we want to keep
    std::uint32_t const ledger_history_;

    int const ledger_fetch_size_;

    TaggedCache<uint256, Blob> fetch_packs_;

    std::uint32_t fetch_seq_;

};

} // casinocoin

#endif
