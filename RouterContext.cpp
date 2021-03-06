#include "RouterContext.h"

#include <botan/auto_rng.h>

namespace i2pcpp {
	RouterContext::RouterContext(std::string const &dbFile, boost::asio::io_service &ios) :
		m_db(dbFile),
		m_inMsgDispatcher(ios, *this),
		m_outMsgDispatcher(*this),
		m_signals(ios),
		m_tunnelManager(ios, *this),
		m_profileManager(*this),
		m_peerManager(ios, *this),
		m_searchManager(ios, *this)
	{
		// Load the private keys from the database
		Botan::AutoSeeded_RNG rng;

		std::string encryptingKeyPEM = m_db.getConfigValue("private_encryption_key");
		Botan::DataSource_Memory dsm((unsigned char *)encryptingKeyPEM.data(), encryptingKeyPEM.size());
		m_encryptionKey = dynamic_cast<Botan::ElGamal_PrivateKey *>(Botan::PKCS8::load_key(dsm, rng, (std::string)""));

		std::string signingKeyPEM = m_db.getConfigValue("private_signing_key");
		Botan::DataSource_Memory dsm2((unsigned char *)signingKeyPEM.data(), signingKeyPEM.size());
		m_signingKey = dynamic_cast<Botan::DSA_PrivateKey *>(Botan::PKCS8::load_key(dsm2, rng, (std::string)""));

		Botan::BigInt encryptionKeyPublic, signingKeyPublic;
		encryptionKeyPublic = m_encryptionKey->get_y();
		signingKeyPublic = m_signingKey->get_y();

		ByteArray encryptionKeyBytes = Botan::BigInt::encode(encryptionKeyPublic), signingKeyBytes = Botan::BigInt::encode(signingKeyPublic);
		m_identity = RouterIdentity(encryptionKeyBytes, signingKeyBytes, Certificate());

		// Populate the DHT
		m_dht = std::make_shared<DHT::Kademlia>(m_identity.getHash());
		std::forward_list<RouterHash> hashes = m_db.getAllHashes();
		for(auto h: hashes)
			m_dht->insert(DHT::Kademlia::makeKey(h), h);
	}

	RouterContext::~RouterContext()
	{
		if(m_encryptionKey) delete m_encryptionKey;
		if(m_signingKey) delete m_signingKey;
	}

	const Botan::ElGamal_PrivateKey *RouterContext::getEncryptionKey() const
	{
		return m_encryptionKey;
	}

	const Botan::DSA_PrivateKey *RouterContext::getSigningKey() const
	{
		return m_signingKey;
	}

	const RouterIdentity& RouterContext::getIdentity() const
	{
		return m_identity;
	}

	Database& RouterContext::getDatabase()
	{
		return m_db;
	}

	InboundMessageDispatcher& RouterContext::getInMsgDisp()
	{
		return m_inMsgDispatcher;
	}

	OutboundMessageDispatcher& RouterContext::getOutMsgDisp()
	{
		return m_outMsgDispatcher;
	}

	Signals& RouterContext::getSignals()
	{
		return m_signals;
	}

	TunnelManager& RouterContext::getTunnelManager()
	{
		return m_tunnelManager;
	}

	ProfileManager& RouterContext::getProfileManager()
	{
		return m_profileManager;
	}

	PeerManager& RouterContext::getPeerManager()
	{
		return m_peerManager;
	}

	DHT::KademliaPtr RouterContext::getDHT()
	{
		return m_dht;
	}

	DHT::SearchManager& RouterContext::getSearchManager()
	{
		return m_searchManager;
	}
}
