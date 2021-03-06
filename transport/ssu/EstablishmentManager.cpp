#include "EstablishmentManager.h"

#include <boost/bind.hpp>

#include "../UDPTransport.h"

#include "PacketBuilder.h"

namespace i2pcpp {
	namespace SSU {
		EstablishmentManager::EstablishmentManager(UDPTransport &transport, Botan::DSA_PrivateKey const &privKey, RouterIdentity const &ri) :
			m_transport(transport),
			m_privKey(privKey),
			m_identity(ri),
			m_log(boost::log::keywords::channel = "EM") {}

		EstablishmentStatePtr EstablishmentManager::createState(Endpoint const &ep)
		{
			std::lock_guard<std::mutex> lock(m_stateTableMutex);

			auto es = std::make_shared<EstablishmentState>(m_privKey, m_identity, ep);
			m_stateTable[ep] = es;

			std::shared_ptr<boost::asio::deadline_timer> timer(new boost::asio::deadline_timer(m_transport.m_ios, boost::posix_time::time_duration(0, 0, 10)));
			timer->async_wait(boost::bind(&EstablishmentManager::timeoutCallback, this, boost::asio::placeholders::error, es));
			m_stateTimers[ep] = timer;

			return es;
		}

		void EstablishmentManager::createState(Endpoint const &ep, RouterIdentity const &ri)
		{
			std::lock_guard<std::mutex> lock(m_stateTableMutex);

			auto es = std::make_shared<EstablishmentState>(m_privKey, m_identity, ep, ri);
			m_stateTable[ep] = es;

			sendRequest(es);

			std::shared_ptr<boost::asio::deadline_timer> timer(new boost::asio::deadline_timer(m_transport.m_ios, boost::posix_time::time_duration(0, 0, 10)));
			timer->async_wait(boost::bind(&EstablishmentManager::timeoutCallback, this, boost::asio::placeholders::error, es));
			m_stateTimers[ep] = timer;
		}

		bool EstablishmentManager::stateExists(Endpoint const &ep) const
		{
			return (m_stateTable.count(ep) > 0);
		}

		void EstablishmentManager::post(EstablishmentStatePtr const &es)
		{
			m_transport.post(boost::bind(&EstablishmentManager::stateChanged, this, es));
		}

		void EstablishmentManager::stateChanged(EstablishmentStatePtr es)
		{
			const Endpoint &ep = es->getTheirEndpoint();
			I2P_LOG_SCOPED_EP(m_log, ep);

			switch(es->getState())
			{
				case EstablishmentState::REQUEST_SENT:
					I2P_LOG(m_log, debug) << "sent session request";
					break;

				case EstablishmentState::REQUEST_RECEIVED:
					I2P_LOG(m_log, debug) << "received session request";
					processRequest(es);
					break;

				case EstablishmentState::CREATED_SENT:
					I2P_LOG(m_log, debug) << "sent session created";
					break;

				case EstablishmentState::CREATED_RECEIVED:
					I2P_LOG(m_log, debug) << "received session created";
					processCreated(es);
					break;

				case EstablishmentState::CONFIRMED_SENT:
					{
						const RouterHash &rh = es->getTheirIdentity().getHash();
						I2P_LOG_SCOPED_RH(m_log, rh);
						I2P_LOG(m_log, debug) << "sent session confirmed";
						m_transport.post(boost::bind(boost::ref(m_transport.m_establishedSignal), rh, (es->getDirection() == EstablishmentState::INBOUND)));
						delState(ep);
					}
					break;

				case EstablishmentState::CONFIRMED_RECEIVED:
					I2P_LOG(m_log, debug) << "received session confirmed";
					processConfirmed(es);
					break;

				case EstablishmentState::UNKNOWN:
				case EstablishmentState::FAILURE:
					I2P_LOG(m_log, error) << "establishment failed";
					m_transport.post(boost::bind(boost::ref(m_transport.m_failureSignal), es->getTheirIdentity().getHash()));
					delState(ep);
					break;
			}
		}

		EstablishmentStatePtr EstablishmentManager::getState(Endpoint const &ep) const
		{
			std::lock_guard<std::mutex> lock(m_stateTableMutex);

			EstablishmentStatePtr es;

			auto itr = m_stateTable.find(ep);
			if(itr != m_stateTable.end())
				es = itr->second;

			return es;
		}

		void EstablishmentManager::delState(const Endpoint &ep)
		{
			std::lock_guard<std::mutex> lock(m_stateTableMutex);

			std::shared_ptr<boost::asio::deadline_timer> timer = m_stateTimers[ep];
			if(timer) {
				timer->cancel();
				m_stateTimers.erase(ep);
			}

			m_stateTable.erase(ep);
		}

		void EstablishmentManager::timeoutCallback(const boost::system::error_code& e, EstablishmentStatePtr es)
		{
			if(!e) {
				I2P_LOG_SCOPED_EP(m_log, es->getTheirEndpoint());
				I2P_LOG(m_log, debug) << "establishment timed out";

				es->setState(EstablishmentState::FAILURE);
				post(es);
			}
		}

		void EstablishmentManager::sendRequest(EstablishmentStatePtr const &state)
		{
			PacketPtr p = PacketBuilder::buildSessionRequest(state);
			p->encrypt(state->getSessionKey(), state->getMacKey());

			m_transport.sendPacket(p);

			state->setState(EstablishmentState::REQUEST_SENT);
			post(state);
		}

		void EstablishmentManager::processRequest(EstablishmentStatePtr const &state)
		{
			state->calculateDHSecret();

			PacketPtr p = PacketBuilder::buildSessionCreated(state);
			p->encrypt(state->getIV(), state->getSessionKey(), state->getMacKey());

			const ByteArray& dhSecret = state->getDHSecret();
			SessionKey newKey(dhSecret), newMacKey;

			state->setSessionKey(newKey);

			copy(dhSecret.begin() + 32, dhSecret.begin() + 32 + 32, newMacKey.begin());
			state->setMacKey(newMacKey);

			m_transport.sendPacket(p);

			state->setState(EstablishmentState::CREATED_SENT);
			post(state);
		}

		void EstablishmentManager::processCreated(EstablishmentStatePtr const &state)
		{
			state->calculateDHSecret();

			if(!state->verifyCreationSignature()) {
				I2P_LOG(m_log, error) << "creation signature verification failed";
				state->setState(EstablishmentState::FAILURE);
				return;
			}

			const ByteArray& dhSecret = state->getDHSecret();
			SessionKey newKey(dhSecret), newMacKey;

			state->setSessionKey(newKey);

			copy(dhSecret.begin() + 32, dhSecret.begin() + 32 + 32, newMacKey.begin());
			state->setMacKey(newMacKey);

			Endpoint ep = state->getTheirEndpoint();
			auto ps = std::make_shared<PeerState>(m_transport.m_ios, ep, state->getTheirIdentity());
			ps->setCurrentSessionKey(state->getSessionKey());
			ps->setCurrentMacKey(state->getMacKey());
			m_transport.m_peers.addRemotePeer(ps);

			PacketPtr p = PacketBuilder::buildSessionConfirmed(state);
			p->encrypt(state->getSessionKey(), state->getMacKey());

			m_transport.sendPacket(p);

			state->setState(EstablishmentState::CONFIRMED_SENT);
			post(state);
		}

		void EstablishmentManager::processConfirmed(EstablishmentStatePtr const &state)
		{
			I2P_LOG_RH(m_log, state->getTheirIdentity().getHash());

			if(!state->verifyConfirmationSignature()) {
				I2P_LOG(m_log, error) << "confirmation signature verification failed";
				state->setState(EstablishmentState::FAILURE);
				post(state);

				return;
			} else
				I2P_LOG(m_log, debug) << "confirmation signature verification succeeded";

			Endpoint ep = state->getTheirEndpoint();
			auto ps = std::make_shared<PeerState>(m_transport.m_ios, ep, state->getTheirIdentity());
			ps->setCurrentSessionKey(state->getSessionKey());
			ps->setCurrentMacKey(state->getMacKey());
			m_transport.m_peers.addRemotePeer(ps);

			delState(ep);

			m_transport.post(boost::bind(boost::ref(m_transport.m_establishedSignal), state->getTheirIdentity().getHash(), (state->getDirection() == EstablishmentState::INBOUND)));
		}
	}
}
