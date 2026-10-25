#include <arpa/inet.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <unordered_map>
#include <vector>

extern "C" {
#include "edi_parser.h"
}
#include "buffer_unpack.h"

namespace {

struct Filters {
	bool hasSrcIp = false;
	bool hasDstIp = false;
	bool hasSrcPort = false;
	bool hasDstPort = false;
	uint32_t srcIp = 0;
	uint32_t dstIp = 0;
	uint16_t srcPort = 0;
	uint16_t dstPort = 0;
	bool hasMis = false;
	uint8_t mis = 0;
	bool allowBadUdpChecksum = false;
};

struct Stats {
	size_t pftPackets = 0;
	size_t invalidUdpChecksums = 0;
	size_t invalidPackets = 0;
};

struct BbframeStats {
	size_t bbframes = 0;
	size_t gsePackets = 0;
	size_t invalidGsePackets = 0;
	size_t completedGsePdus = 0;
};

struct GseFragment {
	size_t totalLength = 0;
	std::vector<unsigned char> data;
};

enum class PacketStatus {
	Invalid,
	Incomplete,
	Valid
};

bool parseIpv4Address(const char* text, uint32_t& address)
{
	in_addr parsed;
	if(inet_pton(AF_INET, text, &parsed) != 1)
		return false;
	address = parsed.s_addr;
	return true;
}

uint16_t read16be(const unsigned char* data)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

bool hasValidChecksum(const unsigned char* data, size_t length)
{
	uint32_t sum = 0;
	for(size_t i = 0; i < length; i += 2) {
		const uint16_t word = static_cast<uint16_t>(
			(static_cast<uint16_t>(data[i]) << 8) |
			(i + 1 < length ? data[i + 1] : 0));
		sum += word;
	}
	while(sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return sum == 0xffff;
}

uint8_t crc8DvbS2(const unsigned char* data, size_t length)
{
	uint8_t crc = 0;
	for(size_t i = 0; i < length; ++i) {
		crc ^= data[i];
		for(unsigned bit = 0; bit < 8; ++bit)
			crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xd5)
				: static_cast<uint8_t>(crc << 1);
	}
	return crc;
}

uint32_t crc32Mpeg2(const unsigned char* data, size_t length)
{
	uint32_t crc = 0;
	for(size_t i = 0; i < length; ++i) {
		crc ^= static_cast<uint32_t>(data[i]) << 24;
		for(unsigned bit = 0; bit < 8; ++bit)
			crc = (crc & 0x80000000)
				? (crc << 1) ^ 0x04c11db7
				: crc << 1;
	}
	return crc;
}

bool hasValidUdpChecksum(const unsigned char* ip, const unsigned char* udp,
	size_t udpLength)
{
	if(udp[6] == 0 && udp[7] == 0)
		return true;

	uint32_t sum = 0;
	for(size_t i = 12; i < 20; i += 2)
		sum += read16be(ip + i);
	sum += ip[9];
	sum += static_cast<uint16_t>(udpLength);
	for(size_t i = 0; i < udpLength; i += 2)
		sum += static_cast<uint16_t>(
			(static_cast<uint16_t>(udp[i]) << 8) |
			(i + 1 < udpLength ? udp[i + 1] : 0));
	while(sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return sum == 0xffff;
}

bool isPftPacket(const unsigned char* packet, size_t packetLength)
{
	if(packetLength < 14 || packet[0] != 'P' || packet[1] != 'F')
		return false;

	const uint32_t index = (static_cast<uint32_t>(packet[4]) << 16) |
		(static_cast<uint32_t>(packet[5]) << 8) | packet[6];
	const uint32_t count = (static_cast<uint32_t>(packet[7]) << 16) |
		(static_cast<uint32_t>(packet[8]) << 8) | packet[9];
	const uint32_t flags = packet[10];
	const size_t payloadLength = ((flags & 0x3f) << 8) | packet[11];
	const size_t headerLength = 14 + ((flags & 0x80) ? 2 : 0) +
		((flags & 0x40) ? 4 : 0);

	return count > 0 && index < count && payloadLength > 0 &&
		static_cast<uint64_t>(count) * payloadLength <= 16 * 1024 * 1024 &&
		packetLength >= headerLength + payloadLength &&
		checkCRC(packet, headerLength);
}

bool matchesFilters(const Filters& filters, const unsigned char* ip,
	const unsigned char* udp)
{
	uint32_t srcIp;
	uint32_t dstIp;
	memcpy(&srcIp, ip + 12, sizeof(srcIp));
	memcpy(&dstIp, ip + 16, sizeof(dstIp));
	return (!filters.hasSrcIp || srcIp == filters.srcIp) &&
		(!filters.hasDstIp || dstIp == filters.dstIp) &&
		(!filters.hasSrcPort || read16be(udp) == filters.srcPort) &&
		(!filters.hasDstPort || read16be(udp + 2) == filters.dstPort);
}

PacketStatus inspectIpv4Packet(const unsigned char* ip, size_t available,
	size_t& ipLength, size_t& udpLength, const unsigned char*& udp,
	const unsigned char*& pft, size_t& pftLength)
{
	if(available < 20 || (ip[0] >> 4) != 4)
		return PacketStatus::Invalid;

	const size_t ipHeaderLength = (ip[0] & 0x0f) * 4;
	ipLength = read16be(ip + 2);
	if(ipHeaderLength < 20 || ipLength < ipHeaderLength + 8)
		return PacketStatus::Invalid;
	if(available < ipLength)
		return PacketStatus::Incomplete;
	if(ip[9] != 17 || !hasValidChecksum(ip, ipHeaderLength))
		return PacketStatus::Invalid;

	const uint16_t fragment = read16be(ip + 6);
	if((fragment & 0x3fff) != 0)
		return PacketStatus::Invalid;

	udp = ip + ipHeaderLength;
	udpLength = read16be(udp + 4);
	if(udpLength < 8 || udpLength > ipLength - ipHeaderLength)
		return PacketStatus::Invalid;

	pft = udp + 8;
	pftLength = udpLength - 8;
	if(!isPftPacket(pft, pftLength))
		return PacketStatus::Invalid;
	return PacketStatus::Valid;
}

void processPftPacket(const unsigned char* ip, const unsigned char* udp,
	size_t udpLength, const unsigned char* pft, size_t pftLength,
	const Filters& filters, edi_handler_t* edi, Stats& stats)
{
	if(!matchesFilters(filters, ip, udp))
		return;
	if(!filters.allowBadUdpChecksum && !hasValidUdpChecksum(ip, udp, udpLength)) {
		++stats.invalidUdpChecksums;
		return;
	}
	HandleEDIPacket(edi, const_cast<unsigned char*>(pft), pftLength);
	++stats.pftPackets;
}

void processIpv4Pdu(const unsigned char* ip, size_t available,
	const Filters& filters, edi_handler_t* edi, Stats& stats)
{
	size_t ipLength = 0;
	size_t udpLength = 0;
	size_t pftLength = 0;
	const unsigned char* udp = nullptr;
	const unsigned char* pft = nullptr;
	const PacketStatus status = inspectIpv4Packet(ip, available, ipLength,
		udpLength, udp, pft, pftLength);
	if(status != PacketStatus::Valid) {
		++stats.invalidPackets;
		return;
	}
	processPftPacket(ip, udp, udpLength, pft, pftLength, filters, edi, stats);
}

void processPcapRecord(const unsigned char* record, size_t recordLength,
	uint32_t linkType, const Filters& filters, edi_handler_t* edi, Stats& stats)
{
	size_t ipOffset = 0;
	if(linkType == 1) {
		if(recordLength < 14)
			return;
		uint16_t etherType = read16be(record + 12);
		ipOffset = 14;
		if(etherType == 0x8100 || etherType == 0x88a8) {
			if(recordLength < 18)
				return;
			etherType = read16be(record + 16);
			ipOffset = 18;
		}
		if(etherType != 0x0800)
			return;
	}
	else if(linkType != 101) {
		return;
	}

	if(ipOffset >= recordLength)
		return;
	const unsigned char* ip = record + ipOffset;
	processIpv4Pdu(ip, recordLength - ipOffset, filters, edi, stats);
}

uint16_t read16(const unsigned char* data, bool littleEndian)
{
	return littleEndian
		? static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8))
		: read16be(data);
}

uint32_t read32(const unsigned char* data, bool littleEndian)
{
	if(littleEndian)
		return static_cast<uint32_t>(data[0]) |
			(static_cast<uint32_t>(data[1]) << 8) |
			(static_cast<uint32_t>(data[2]) << 16) |
			(static_cast<uint32_t>(data[3]) << 24);
	return (static_cast<uint32_t>(data[0]) << 24) |
		(static_cast<uint32_t>(data[1]) << 16) |
		(static_cast<uint32_t>(data[2]) << 8) | data[3];
}

bool readExact(std::istream& input, unsigned char* data, size_t length)
{
	input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(length));
	return static_cast<size_t>(input.gcount()) == length;
}

bool isPcapMagic(const unsigned char* data, size_t length)
{
	if(length < 4)
		return false;
	return (data[0] == 0xa1 && data[1] == 0xb2 &&
			((data[2] == 0xc3 && data[3] == 0xd4) ||
			 (data[2] == 0x3c && data[3] == 0x4d))) ||
		(data[0] == 0xd4 && data[1] == 0xc3 &&
			data[2] == 0xb2 && data[3] == 0xa1) ||
		(data[0] == 0x4d && data[1] == 0x3c &&
			data[2] == 0xb2 && data[3] == 0xa1);
}

bool isValidBbframeHeader(const unsigned char* data, size_t length)
{
	if(length < 10 || crc8DvbS2(data, 9) != data[9])
		return false;
	const size_t upl = read16be(data + 2);
	const size_t dfl = read16be(data + 4);
	return dfl > 0 && dfl <= 58112 && (dfl & 7) == 0 &&
		(upl & 7) == 0;
}

bool startsWithIpv4Packet(const unsigned char* data, size_t length)
{
	if(length < 28 || (data[0] >> 4) != 4 || data[9] != 17)
		return false;
	const size_t headerLength = (data[0] & 0x0f) * 4;
	return headerLength >= 20 && length >= headerLength + 8 &&
		read16be(data + 2) >= headerLength + 8;
}

enum class InputFormat {
	Unknown,
	Pcap,
	PrefixedBbframe,
	Bbframe,
	Ethernet,
	RawIpv4
};

InputFormat detectInputFormat(const unsigned char* data, size_t length,
	bool forceBbframe)
{
	if(forceBbframe)
		return InputFormat::Bbframe;
	if(isPcapMagic(data, length))
		return InputFormat::Pcap;
	if(length >= 15 && read16be(data + 12) == 0x0800 &&
		startsWithIpv4Packet(data + 14, length - 14))
		return InputFormat::Ethernet;
	if(length >= 19 &&
		(read16be(data + 12) == 0x8100 || read16be(data + 12) == 0x88a8) &&
		read16be(data + 16) == 0x0800 &&
		startsWithIpv4Packet(data + 18, length - 18))
		return InputFormat::Ethernet;
	for(size_t offset = 0; offset + 11 <= length; ++offset) {
		if(data[offset] == 0xb8 &&
			isValidBbframeHeader(data + offset + 1, length - offset - 1))
			return InputFormat::PrefixedBbframe;
	}
	for(size_t offset = 0; offset + 10 <= length; ++offset) {
		if(isValidBbframeHeader(data + offset, length - offset))
			return InputFormat::Bbframe;
	}
	if(startsWithIpv4Packet(data, length))
		return InputFormat::RawIpv4;
	return InputFormat::Unknown;
}

const char* inputFormatName(InputFormat format)
{
	switch(format) {
	case InputFormat::Pcap: return "pcap";
	case InputFormat::PrefixedBbframe: return "0xB8-prefixed DVB-S2 BBFRAME";
	case InputFormat::Bbframe: return "DVB-S2 BBFRAME";
	case InputFormat::Ethernet: return "raw Ethernet";
	case InputFormat::RawIpv4: return "raw IPv4";
	case InputFormat::Unknown: return "unknown";
	}
	return "unknown";
}

bool processPcap(std::istream& input, const unsigned char* pcapHeader,
	const Filters& filters, edi_handler_t* edi, Stats& stats)
{
	bool littleEndian;
	switch((static_cast<uint32_t>(pcapHeader[0]) << 24) |
		(static_cast<uint32_t>(pcapHeader[1]) << 16) |
		(static_cast<uint32_t>(pcapHeader[2]) << 8) | pcapHeader[3]) {
	case 0xa1b2c3d4:
	case 0xa1b23c4d:
		littleEndian = false;
		break;
	case 0xd4c3b2a1:
	case 0x4d3cb2a1:
		littleEndian = true;
		break;
	default:
		fprintf(stderr, "Invalid pcap magic\n");
		return false;
	}

	const unsigned char* header = pcapHeader + 4;
	const uint16_t major = read16(header, littleEndian);
	const uint16_t minor = read16(header + 2, littleEndian);
	const uint32_t snapLength = read32(header + 12, littleEndian);
	const uint32_t linkType = read32(header + 16, littleEndian);
	if(major != 2 || minor != 4 || snapLength == 0 ||
		(linkType != 1 && linkType != 101)) {
		fprintf(stderr, "Unsupported pcap version or link type\n");
		return false;
	}

	unsigned char recordHeader[16];
	char firstRecordByte;
	while(input.get(firstRecordByte)) {
		recordHeader[0] = static_cast<unsigned char>(firstRecordByte);
		if(!readExact(input, recordHeader + 1, sizeof(recordHeader) - 1)) {
			fprintf(stderr, "Truncated pcap record header\n");
			return false;
		}
		const uint32_t capturedLength = read32(recordHeader + 8, littleEndian);
		if(capturedLength == 0 || capturedLength > snapLength ||
			capturedLength > 1024 * 1024) {
			fprintf(stderr, "Invalid pcap record length: %u\n", capturedLength);
			return false;
		}
		std::vector<unsigned char> record(capturedLength);
		if(!readExact(input, record.data(), capturedLength)) {
			fprintf(stderr, "Truncated pcap record\n");
			return false;
		}
		processPcapRecord(record.data(), record.size(), linkType, filters, edi,
			stats);
	}
	if(!input.eof()) {
		fprintf(stderr, "Error reading pcap input\n");
		return false;
	}
	return true;
}

PacketStatus inspectStreamPacket(const unsigned char* data, size_t available,
	size_t& packetOffset, size_t& packetLength, const unsigned char*& ip,
	size_t& ipLength, size_t& udpLength, const unsigned char*& udp,
	const unsigned char*& pft, size_t& pftLength)
{
	if(available >= 14 && read16be(data + 12) == 0x0800 &&
		(available < 15 || (data[14] >> 4) == 4)) {
		packetOffset = 14;
	}
	else if(available >= 18 &&
		(read16be(data + 12) == 0x8100 || read16be(data + 12) == 0x88a8) &&
		read16be(data + 16) == 0x0800) {
		packetOffset = 18;
	}
	else if(available >= 1 && (data[0] >> 4) == 4) {
		packetOffset = 0;
	}
	else {
		return PacketStatus::Invalid;
	}

	const PacketStatus status = inspectIpv4Packet(data + packetOffset,
		available - packetOffset, ipLength, udpLength, udp, pft, pftLength);
	if(status != PacketStatus::Valid)
		return status;
	ip = data + packetOffset;
	packetLength = packetOffset + ipLength;
	return PacketStatus::Valid;
}

void processGseDataField(const unsigned char* data, size_t length,
	const Filters& filters, edi_handler_t* edi, Stats& packetStats,
	BbframeStats& bbStats,
	std::unordered_map<uint8_t, GseFragment>& fragments)
{
	size_t offset = 0;
	while(offset + 2 <= length) {
		const uint16_t header = read16be(data + offset);
		const unsigned indicator = header >> 14;
		const unsigned labelType = (header >> 12) & 0x03;
		const size_t gseLength = header & 0x0fff;
		if(indicator == 0 && labelType == 0 && gseLength == 0) {
			++bbStats.gsePackets;
			break;
		}
		if(gseLength > length - offset - 2) {
			++bbStats.invalidGsePackets;
			break;
		}

		const unsigned char* payload = data + offset + 2;
		offset += 2 + gseLength;
		++bbStats.gsePackets;

		if(indicator == 3) {
			const size_t labelLength = labelType == 0 ? 6 : labelType == 1 ? 3 : 0;
			if(gseLength < 2 + labelLength) {
				++bbStats.invalidGsePackets;
				continue;
			}
			if(read16be(payload) == 0x0800) {
				++bbStats.completedGsePdus;
				processIpv4Pdu(payload + 2 + labelLength,
					gseLength - 2 - labelLength, filters, edi, packetStats);
			}
			continue;
		}

		if(indicator == 2) {
			const size_t labelLength = labelType == 0 ? 6 : labelType == 1 ? 3 : 0;
			if(gseLength < 5 + labelLength) {
				++bbStats.invalidGsePackets;
				continue;
			}
			const uint8_t fragId = payload[0];
			const size_t totalLength = read16be(payload + 1);
			const uint16_t protocolType = read16be(payload + 3);
			fragments.erase(fragId);
			if(protocolType != 0x0800 || totalLength == 0)
				continue;

			GseFragment fragment;
			fragment.totalLength = totalLength;
			fragment.data.assign(payload + 5 + labelLength,
				payload + gseLength);
			if(fragment.data.size() <= fragment.totalLength)
				fragments.emplace(fragId, std::move(fragment));
			else
				++bbStats.invalidGsePackets;
			continue;
		}

		if(indicator == 0) {
			if(gseLength < 1) {
				++bbStats.invalidGsePackets;
				continue;
			}
			auto it = fragments.find(payload[0]);
			if(it != fragments.end()) {
				const size_t chunkLength = gseLength - 1;
				if(chunkLength <= it->second.totalLength -
					std::min(it->second.data.size(), it->second.totalLength)) {
					it->second.data.insert(it->second.data.end(), payload + 1,
						payload + gseLength);
				}
				else {
					fragments.erase(it);
					++bbStats.invalidGsePackets;
				}
			}
			continue;
		}

		if(indicator == 1) {
			if(gseLength < 5) {
				++bbStats.invalidGsePackets;
				continue;
			}
			auto it = fragments.find(payload[0]);
			if(it == fragments.end())
				continue;

			const size_t chunkLength = gseLength - 5;
			if(chunkLength > it->second.totalLength -
				std::min(it->second.data.size(), it->second.totalLength)) {
				fragments.erase(it);
				++bbStats.invalidGsePackets;
				continue;
			}
			it->second.data.insert(it->second.data.end(), payload + 1,
				payload + 1 + chunkLength);
			GseFragment fragment = std::move(it->second);
			fragments.erase(it);
			if(fragment.data.size() <= fragment.totalLength) {
				++bbStats.completedGsePdus;
				processIpv4Pdu(fragment.data.data(), fragment.data.size(),
					filters, edi, packetStats);
			}
		}
	}
}

bool processBbframeStream(std::istream& input, const unsigned char* prefix,
	size_t prefixLength, const Filters& filters, edi_handler_t* edi,
	Stats& packetStats, BbframeStats& bbStats, bool syncBytePrefix)
{
	std::vector<unsigned char> buffer(prefix, prefix + prefixLength);
	size_t scan = 0;
	bool eof = false;
	bool readError = false;
	char chunk[8192];
	std::unordered_map<uint8_t, GseFragment> fragments;

	auto ensureAvailable = [&](size_t length) {
		while(buffer.size() - scan < length && !eof) {
			input.read(chunk, sizeof(chunk));
			const std::streamsize bytesRead = input.gcount();
			if(bytesRead > 0)
				buffer.insert(buffer.end(), chunk, chunk + bytesRead);
			if(input.bad()) {
				readError = true;
				return false;
			}
			if(input.eof())
				eof = true;
			else if(input.fail()) {
				readError = true;
				return false;
			}
		}
		return buffer.size() - scan >= length;
	};

	while(ensureAvailable(10)) {
		const size_t headerOffset = syncBytePrefix ? 1 : 0;
		if(syncBytePrefix && buffer[scan] != 0xb8) {
			++scan;
			continue;
		}
		if(!ensureAvailable(headerOffset + 10))
			break;
		const unsigned char* header = buffer.data() + scan + headerOffset;
		const size_t upl = read16be(header + 2);
		const size_t dfl = read16be(header + 4);
		if(crc8DvbS2(header, 9) != header[9] ||
			dfl == 0 || dfl > 58112 || (dfl & 7) != 0 || (upl & 7) != 0) {
			++scan;
		}
		else {
			const size_t dataLength = dfl / 8;
			const size_t frameLength = headerOffset + 10 + dataLength;
			if(!ensureAvailable(frameLength)) {
				if(!readError)
					fprintf(stderr, "Truncated DVB-S2 BBFRAME at end of input\n");
				break;
			}

			header = buffer.data() + scan + headerOffset;
			const unsigned char* dataField = header + 10;
			size_t usableLength = dataLength;
			if(dataLength >= 4) {
				const uint32_t crcValue = (static_cast<uint32_t>(
					dataField[dataLength - 4]) << 24) |
					(static_cast<uint32_t>(dataField[dataLength - 3]) << 16) |
					(static_cast<uint32_t>(dataField[dataLength - 2]) << 8) |
					dataField[dataLength - 1];
				if(crc32Mpeg2(header, 10 + dataLength - 4) == crcValue)
					usableLength -= 4;
			}

			if(!filters.hasMis || header[1] == filters.mis) {
				processGseDataField(dataField, usableLength, filters, edi,
					packetStats, bbStats, fragments);
				++bbStats.bbframes;
			}
			scan += frameLength;
		}

		if(scan >= 8192) {
			buffer.erase(buffer.begin(), buffer.begin() +
				static_cast<std::ptrdiff_t>(scan));
			scan = 0;
		}
	}

	if(scan > 0)
		buffer.erase(buffer.begin(), buffer.begin() +
			static_cast<std::ptrdiff_t>(scan));
	if(readError)
		fprintf(stderr, "Error reading DVB-S2 BBFRAME input\n");
	return !readError && bbStats.bbframes > 0;
}

bool processRawStream(std::istream& input, const unsigned char* prefix,
	size_t prefixLength, const Filters& filters, edi_handler_t* edi,
	Stats& stats)
{
	std::vector<unsigned char> buffer(prefix, prefix + prefixLength);
	char chunk[8192];
	size_t scan = 0;
	while(true) {
		input.read(chunk, sizeof(chunk));
		const std::streamsize bytesRead = input.gcount();
		if(bytesRead > 0)
			buffer.insert(buffer.end(), chunk, chunk + bytesRead);

		while(scan + 42 <= buffer.size()) {
			size_t packetOffset = 0;
			size_t packetLength = 0;
			size_t ipLength = 0;
			size_t udpLength = 0;
			size_t pftLength = 0;
			const unsigned char* ip = nullptr;
			const unsigned char* udp = nullptr;
			const unsigned char* pft = nullptr;
			const PacketStatus status = inspectStreamPacket(buffer.data() + scan,
				buffer.size() - scan, packetOffset, packetLength, ip, ipLength,
				udpLength, udp, pft, pftLength);
			if(status == PacketStatus::Incomplete)
				break;
			if(status == PacketStatus::Invalid) {
				++scan;
				continue;
			}

			processPftPacket(ip, udp, udpLength, pft, pftLength, filters, edi,
				stats);
			scan += packetLength;
		}

		if(scan > 0) {
			buffer.erase(buffer.begin(), buffer.begin() +
				static_cast<std::ptrdiff_t>(scan));
			scan = 0;
		}
		if(input.eof())
			break;
		if(input.bad()) {
			fprintf(stderr, "Error reading input stream\n");
			return false;
		}
	}
	return true;
}

void printUsage(const char* program)
{
	fprintf(stderr,
		"Usage: %s [--src-ip IP] [--dst-ip IP] [--src-port PORT] "
		"[--dst-port PORT] [--mis ISI] [--allow-bad-udp-checksum] "
		"[--bbframe]\n"
		"Input format is auto-detected: pcap, raw IP/Ethernet, or BBFRAME. "
		"--bbframe forces unprefixed BBFRAME input.\n",
		program);
}

bool parsePort(const char* text, uint16_t& port)
{
	char* end = nullptr;
	const long value = strtol(text, &end, 10);
	if(!end || *end != '\0' || value < 0 || value > 65535)
		return false;
	port = static_cast<uint16_t>(value);
	return true;
}

bool parseMis(const char* text, uint8_t& mis)
{
	char* end = nullptr;
	const long value = strtol(text, &end, 10);
	if(!end || *end != '\0' || value < 0 || value > 255)
		return false;
	mis = static_cast<uint8_t>(value);
	return true;
}

} // namespace

static bool outputError = false;

static void writeEti(void* privData, void* etiData, int etiLength)
{
	FILE* output = static_cast<FILE*>(privData);
	if(fwrite(etiData, 1, etiLength, output) != static_cast<size_t>(etiLength)) {
		fprintf(stderr, "Error writing ETI output\n");
		outputError = true;
	}
}

int main(int argc, char** argv)
{
	Filters filters;
	bool bbframeInput = false;
	static option options[] = {
		{"src-ip", required_argument, nullptr, 's'},
		{"dst-ip", required_argument, nullptr, 'd'},
		{"src-port", required_argument, nullptr, 'S'},
		{"dst-port", required_argument, nullptr, 'D'},
		{"mis", required_argument, nullptr, 'm'},
		{"allow-bad-udp-checksum", no_argument, nullptr, 'b'},
		{"bbframe", no_argument, nullptr, 'B'},
		{"help", no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};

	int option;
	while((option = getopt_long_only(argc, argv, "", options, nullptr)) != -1) {
		switch(option) {
		case 's':
			if(!parseIpv4Address(optarg, filters.srcIp)) {
				fprintf(stderr, "Invalid source IP: %s\n", optarg);
				return 1;
			}
			filters.hasSrcIp = true;
			break;
		case 'd':
			if(!parseIpv4Address(optarg, filters.dstIp)) {
				fprintf(stderr, "Invalid destination IP: %s\n", optarg);
				return 1;
			}
			filters.hasDstIp = true;
			break;
		case 'S':
			if(!parsePort(optarg, filters.srcPort)) {
				fprintf(stderr, "Invalid source port: %s\n", optarg);
				return 1;
			}
			filters.hasSrcPort = true;
			break;
		case 'D':
			if(!parsePort(optarg, filters.dstPort)) {
				fprintf(stderr, "Invalid destination port: %s\n", optarg);
				return 1;
			}
			filters.hasDstPort = true;
			break;
		case 'm':
			if(!parseMis(optarg, filters.mis)) {
				fprintf(stderr, "Invalid MIS/ISI: %s\n", optarg);
				return 1;
			}
			filters.hasMis = true;
			break;
		case 'b':
			filters.allowBadUdpChecksum = true;
			break;
		case 'B':
			bbframeInput = true;
			break;
		case 'h':
			printUsage(argv[0]);
			return 0;
		default:
			printUsage(argv[0]);
			return 1;
		}
	}
	if(optind != argc) {
		printUsage(argv[0]);
		return 1;
	}

	edi_handler_t* edi = initEDIHandle(ETI_FMT_RAW, writeEti, stdout);
	if(!edi) {
		fprintf(stderr, "Could not initialize EDI parser\n");
		return 1;
	}

	unsigned char prefix[4096];
	std::streamsize firstRead = std::cin.rdbuf()->sgetn(
		reinterpret_cast<char*>(prefix), 24);
	bool inputOk = true;
	if(firstRead == 0) {
		fprintf(stderr, "No input received\n");
		inputOk = false;
	}
	else {
		InputFormat format = detectInputFormat(prefix,
			static_cast<size_t>(firstRead), bbframeInput);
		if(format == InputFormat::Unknown && !bbframeInput) {
			firstRead += std::cin.rdbuf()->sgetn(
				reinterpret_cast<char*>(prefix + firstRead),
				sizeof(prefix) - static_cast<size_t>(firstRead));
			format = detectInputFormat(prefix, static_cast<size_t>(firstRead),
				false);
		}
		if(format == InputFormat::Unknown) {
			fprintf(stderr, "Could not detect input format\n");
			inputOk = false;
		}
		else {
			fprintf(stderr, "%s input format: %s\n",
				bbframeInput ? "Forced" : "Detected",
				inputFormatName(format));
			if(format == InputFormat::Pcap) {
				Stats stats;
				inputOk = firstRead == 24
					? processPcap(std::cin, prefix, filters, edi, stats)
					: false;
				if(firstRead != 24)
					fprintf(stderr, "Truncated pcap global header\n");
				if(stats.invalidUdpChecksums > 0)
					fprintf(stderr, "Skipped %zu matching UDP packet(s) with invalid checksum\n",
						stats.invalidUdpChecksums);
				fprintf(stderr, "Processed %zu EDI/PFT UDP packet(s)\n",
					stats.pftPackets);
				if(stats.pftPackets == 0)
					inputOk = false;
			}
			else if(format == InputFormat::Bbframe ||
				format == InputFormat::PrefixedBbframe) {
				Stats packetStats;
				BbframeStats bbStats;
				const bool syncBytePrefix =
					format == InputFormat::PrefixedBbframe;
				inputOk = processBbframeStream(std::cin, prefix,
					static_cast<size_t>(firstRead), filters, edi, packetStats,
					bbStats, syncBytePrefix);
				fprintf(stderr, "Processed %zu %s(s), %zu GSE packet(s), "
					"%zu complete IP PDU(s)\n", bbStats.bbframes,
					syncBytePrefix ? "0xB8-prefixed BBFRAME" : "DVB-S2 BBFRAME",
					bbStats.gsePackets, bbStats.completedGsePdus);
				if(bbStats.invalidGsePackets > 0)
					fprintf(stderr, "Skipped %zu invalid/truncated GSE packet(s)\n",
						bbStats.invalidGsePackets);
				if(packetStats.invalidUdpChecksums > 0)
					fprintf(stderr, "Skipped %zu matching UDP packet(s) with invalid checksum\n",
						packetStats.invalidUdpChecksums);
				fprintf(stderr, "Processed %zu EDI/PFT UDP packet(s)\n",
					packetStats.pftPackets);
				if(packetStats.pftPackets == 0)
					inputOk = false;
			}
			else {
				Stats stats;
				inputOk = processRawStream(std::cin, prefix,
					static_cast<size_t>(firstRead), filters, edi, stats);
				if(stats.invalidUdpChecksums > 0)
					fprintf(stderr, "Skipped %zu matching UDP packet(s) with invalid checksum\n",
						stats.invalidUdpChecksums);
				fprintf(stderr, "Processed %zu EDI/PFT UDP packet(s)\n",
					stats.pftPackets);
				if(stats.pftPackets == 0)
					inputOk = false;
			}
		}
	}

	closeEDIHandle(edi);
	if(fflush(stdout) != 0) {
		fprintf(stderr, "Error flushing ETI output\n");
		outputError = true;
	}
	return inputOk && !outputError ? 0 : 1;
}
