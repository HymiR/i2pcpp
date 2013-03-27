#include "VariableTunnelBuild.h"

namespace i2pcpp {
	namespace I2NP {
		ByteArray VariableTunnelBuild::getBytes() const
		{
			ByteArray b;

			b.insert(b.end(), m_buildRecords.size());
			for(auto& r: m_buildRecords) {
				const ByteArray&& recordBytes = r.getBytes();
				b.insert(b.end(), recordBytes.cbegin(), recordBytes.cend());
			}

			return b;
		}

		bool VariableTunnelBuild::parse(ByteArrayConstItr &begin, ByteArrayConstItr end)
		{
			unsigned char size = *dataItr++;

			for(int i = 0; i < size; i++)
				m_buildRecords.emplace_back(begin);

			return true;
		}
	}
}