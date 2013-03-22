#include "UDPTransport.h"

#include <boost/exception/all.hpp>

namespace i2pcpp {
	UDPTransport::UDPTransport(Botan::DSA_PrivateKey const &privKey, RouterIdentity const &ri) :
		m_socket(m_ios),
		m_packetHandler(*this, ri.getHash()),
		m_establishmentManager(*this, privKey, ri),
		m_log(boost::log::keywords::channel = "SSU") {}

	UDPTransport::~UDPTransport()
	{
		shutdown();
	}

	void UDPTransport::start(Endpoint const &ep)
	{
		try {
			if(ep.getUDPEndpoint().address().is_v4())
				m_socket.open(boost::asio::ip::udp::v4());
			else if(ep.getUDPEndpoint().address().is_v6())
				m_socket.open(boost::asio::ip::udp::v6());

			m_socket.bind(ep.getUDPEndpoint());

			m_socket.async_receive_from(
					boost::asio::buffer(m_receiveBuf.data(), m_receiveBuf.size()),
					m_senderEndpoint,
					boost::bind(
						&UDPTransport::dataReceived,
						this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred
						)
					);

			m_serviceThread = std::thread([&](){
				while(1) {
					try {
						m_ios.run();
						break;
					} catch(std::exception &e) {
						// TODO Handle exception
					}
				}
			});
		} catch(boost::system::system_error &e) {
			shutdown();
			throw;
		}
	}

	void UDPTransport::connect(RouterInfo const &ri)
	{
		// TODO Try all the addresses if it times out
		for(auto a: ri) {
			if(a.getTransport() == "SSU") {
				const Mapping& m = a.getOptions();
				m_establishmentManager.createState(Endpoint(m.getValue("host"), stoi(m.getValue("port"))), ri.getIdentity());
				break;
			}
		}
	}

	void UDPTransport::send(RouterHash const &rh, ByteArray const &msg)
	{
	}

	void UDPTransport::disconnect(RouterHash const &rh)
	{
	}

	void UDPTransport::shutdown()
	{
		m_ios.stop();
		if(m_serviceThread.joinable()) m_serviceThread.join();
	}

	void UDPTransport::sendPacket(SSU::PacketPtr const &p)
	{
		ByteArray& pdata = p->getData();
		Endpoint ep = p->getEndpoint();

		m_socket.async_send_to(
				boost::asio::buffer(pdata.data(), pdata.size()),
				ep.getUDPEndpoint(),
				boost::bind(
					&UDPTransport::dataSent,
					this,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred,
					ep.getUDPEndpoint()
					)
				);
	}

	void UDPTransport::dataReceived(const boost::system::error_code& e, size_t n)
	{
		if(!e && n > 0) {
			BOOST_LOG_SEV(m_log, debug) << "received " << n << " bytes from " << m_senderEndpoint;
			
			auto p = std::make_shared<SSU::Packet>(Endpoint(m_senderEndpoint), m_receiveBuf.data(), n);
			m_ios.post(boost::bind(&SSU::PacketHandler::packetReceived, &m_packetHandler, p, m_peers.getRemotePeer(m_senderEndpoint)));

			m_socket.async_receive_from(
					boost::asio::buffer(m_receiveBuf.data(), m_receiveBuf.size()),
					m_senderEndpoint,
					boost::bind(
						&UDPTransport::dataReceived,
						this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred
						)
					);
		}
	}

	void UDPTransport::dataSent(const boost::system::error_code& e, size_t n, boost::asio::ip::udp::endpoint ep)
	{
		BOOST_LOG_SEV(m_log, debug) << "sent " << n << " bytes to " << ep;
	}

	SSU::EstablishmentManager& UDPTransport::getEstablisher()
	{
		return m_establishmentManager;
	}

	i2p_logger_mt& UDPTransport::getLogger()
	{
		return m_log;
	}
}
