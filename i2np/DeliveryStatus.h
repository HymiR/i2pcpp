#ifndef I2NPDELIVERYSTATUS_H
#define I2NPDELIVERYSTATUS_H

#include "Message.h"

#include "../datatypes/Date.h"

namespace i2pcpp {
	namespace I2NP {
		class DeliveryStatus : public Message {
			public:
				DeliveryStatus();
				DeliveryStatus(uint32_t msgId, Date timestamp);

				Message::Type getType() const;

			protected:
				ByteArray getBytes() const;
				bool parse(ByteArrayConstItr &begin, ByteArrayConstItr end);

			private:
				uint32_t m_msgId;
				Date m_timestamp;
		};
	}
}

#endif
