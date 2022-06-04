#include "lib/globals.h"
#include "lib/decoders/decoders.h"
#include "aeslanier.h"
#include "lib/crc.h"
#include "lib/fluxmap.h"
#include "lib/decoders/fluxmapreader.h"
#include "lib/sector.h"
#include "lib/bytes.h"
#include "fmt/format.h"
#include <string.h>

static const FluxPattern SECTOR_PATTERN(32, AESLANIER_RECORD_SEPARATOR);

/* This is actually M2FM, rather than MFM, but it our MFM/FM decoder copes fine with it. */

static Bytes reverse_bits(const Bytes& input)
{
    Bytes output;
    ByteWriter bw(output);

    for (uint8_t b : input)
        bw.write_8(reverse_bits(b));
    return output;
}

class AesLanierDecoder : public AbstractDecoder
{
public:
	AesLanierDecoder(const DecoderProto& config):
		AbstractDecoder(config)
	{}

    nanoseconds_t advanceToNextRecord() override
	{
		return seekToPattern(SECTOR_PATTERN);
	}

    void decodeSectorRecord() override
	{
		/* Skip ID mark (we know it's a AESLANIER_RECORD_SEPARATOR). */

		readRawBits(16);

		const auto& rawbits = readRawBits(AESLANIER_RECORD_SIZE*16);
		const auto& bytes = decodeFmMfm(rawbits).slice(0, AESLANIER_RECORD_SIZE);
		const auto& reversed = reverse_bits(bytes);

		_sector->logicalTrack = reversed[1];
		_sector->logicalSide = 0;
		_sector->logicalSector = reversed[2];

		/* Check header 'checksum' (which seems far too simple to mean much). */

		{
			uint8_t wanted = reversed[3];
			uint8_t got = reversed[1] + reversed[2];
			if (wanted != got)
				return;
		}

		/* Check data checksum, which also includes the header and is
			* significantly better. */

		_sector->data = reversed.slice(1, AESLANIER_SECTOR_LENGTH);
		uint16_t wanted = reversed.reader().seek(0x101).read_le16();
		uint16_t got = crc16ref(MODBUS_POLY_REF, _sector->data);
		_sector->status = (wanted == got) ? Sector::OK : Sector::BAD_CHECKSUM;
	}
};

std::unique_ptr<AbstractDecoder> createAesLanierDecoder(const DecoderProto& config)
{
	return std::unique_ptr<AbstractDecoder>(new AesLanierDecoder(config));
}


