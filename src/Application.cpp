
#include <iostream>

#include "../database/SqliteDatabase.h"

#include "Application.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"
#include "BitcoinUtil.h"
#include "key.h"
#include "utils.h"
#include "TaggedCache.h"
#include "boost/filesystem.hpp"

Application* theApp = NULL;

DatabaseCon::DatabaseCon(const std::string& strName, const char *initStrings[], int initCount)
{
	boost::filesystem::path	pPath	= theConfig.DATA_DIR / strName;

	mDatabase = new SqliteDatabase(pPath.string().c_str());
	mDatabase->connect();
	for(int i = 0; i < initCount; ++i)
		mDatabase->executeSQL(initStrings[i], true);
}

DatabaseCon::~DatabaseCon()
{
	mDatabase->disconnect();
	delete mDatabase;
}

Application::Application() :
	mUNL(mIOService),
	mNetOps(mIOService, &mMasterLedger), mNodeCache(16384, 600),
	mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL), mHashNodeDB(NULL), mNetNodeDB(NULL),
	mConnectionPool(mIOService), mPeerDoor(NULL), mRPCDoor(NULL)
{
	RAND_bytes(mNonce256.begin(), mNonce256.size());
	RAND_bytes(reinterpret_cast<unsigned char *>(&mNonceST), sizeof(mNonceST));
}

extern const char *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[], *NetNodeDBInit[];
extern int TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount, NetNodeDBCount;

void Application::stop()
{
	mIOService.stop();

	std::cerr << "Stopped: " << mIOService.stopped() << std::endl;
}

void Application::run()
{
	assert(mTxnDB==NULL);

	//
	// Construct databases.
	//
	mTxnDB		= new DatabaseCon("transaction.db", TxnDBInit, TxnDBCount);
	mLedgerDB	= new DatabaseCon("ledger.db", LedgerDBInit, LedgerDBCount);
	mWalletDB	= new DatabaseCon("wallet.db", WalletDBInit, WalletDBCount);
	mHashNodeDB	= new DatabaseCon("hashnode.db", HashNodeDBInit, HashNodeDBCount);
	mNetNodeDB	= new DatabaseCon("netnode.db", NetNodeDBInit, NetNodeDBCount);

	//
	// Begin validation and ip maintenance.
	// - Wallet maintains local information: including identity and network connection persistency information.
	//
	mWallet.start();

	//
	// Set up UNL.
	//
	getUNL().nodeBootstrap();

	//
	// Allow peer connections.
	//
	if (!theConfig.PEER_IP.empty() && theConfig.PEER_PORT)
	{
		mPeerDoor = new PeerDoor(mIOService);
	}
	else
	{
		std::cerr << "Peer interface: disabled" << std::endl;
	}

	//
	// Allow RPC connections.
	//
	if (!theConfig.RPC_IP.empty() && theConfig.RPC_PORT)
	{
		mRPCDoor = new RPCDoor(mIOService);
	}
	else
	{
		std::cerr << "RPC interface: disabled" << std::endl;
	}

	//
	// Begin connecting to network.
	//
	mConnectionPool.start();

	// New stuff.
	NewcoinAddress	rootSeedMaster		= NewcoinAddress::createSeedGeneric("masterpassphrase");
	NewcoinAddress	rootGeneratorMaster	= NewcoinAddress::createGeneratorPublic(rootSeedMaster);
	NewcoinAddress	rootAddress			= NewcoinAddress::createAccountPublic(rootGeneratorMaster, 0);

	// Print enough information to be able to claim root account.
	std::cerr << "Root master seed: " << rootSeedMaster.humanSeed() << std::endl;
	std::cerr << "Root account: " << rootAddress.humanAccountID() << std::endl;

	Ledger::pointer firstLedger = boost::make_shared<Ledger>(rootAddress, SYSTEM_CURRENCY_START);
	assert(!!firstLedger->getAccountState(rootAddress));
	firstLedger->updateHash();
	firstLedger->setClosed();
	firstLedger->setAccepted();
	mMasterLedger.pushLedger(firstLedger);

	Ledger::pointer secondLedger = boost::make_shared<Ledger>(true, boost::ref(*firstLedger));
	mMasterLedger.pushLedger(secondLedger);
	assert(!!secondLedger->getAccountState(rootAddress));
	// temporary

	mNetOps.setStateTimer(0);

	mIOService.run(); // This blocks

	std::cout << "Done." << std::endl;
}

Application::~Application()
{
	delete mTxnDB;
	delete mLedgerDB;
	delete mWalletDB;
	delete mHashNodeDB;
	delete mNetNodeDB;
}
// vim:ts=4
