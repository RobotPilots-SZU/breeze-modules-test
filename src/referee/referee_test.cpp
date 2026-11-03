// NOLINTBEGIN(readability-magic-numbers)
#include <zephyr/ztest.h>

#include <breeze/referee/module.h>

#include <etl/span.h>
#include <ramen.hpp>
#include <robotpilots/algorithm/crc.hpp>

#include <array>
#include <cstdint>
#include <cstring>

namespace
{

using breeze::models::DamageSource;
using breeze::models::DartTarget;
using breeze::models::GameMode;
using breeze::models::GameStage;
using breeze::models::messages::DamageEvent;
using breeze::models::messages::DartInfo;
using breeze::models::messages::GameState;
using breeze::referee::module;

constexpr std::size_t FRAME_HEADER_SIZE = 5;
constexpr std::size_t COMMAND_SIZE = 2;
constexpr std::size_t FRAME_TAIL_SIZE = 2;
constexpr std::uint8_t START_OF_FRAME = 0xa5;

struct __attribute__((packed)) GameStatusPacket {
	static constexpr std::uint16_t ID = 0x0001;
	std::uint8_t GameType: 4;
	std::uint8_t GameProgress: 4;
	std::uint16_t StageRemainTime;
	std::uint64_t SyncTimeStamp;
};

struct __attribute__((packed)) DartInfoPacket {
	static constexpr std::uint16_t ID = 0x0105;
	std::uint8_t DartRemainingTime;
	std::uint16_t DartInfo;
};

struct __attribute__((packed)) HurtDataPacket {
	static constexpr std::uint16_t ID = 0x0206;
	std::uint8_t ArmorId: 4;
	std::uint8_t HpDeductionReason: 4;
};

static_assert(sizeof(GameStatusPacket) == 11);
static_assert(sizeof(DartInfoPacket) == 3);
static_assert(sizeof(HurtDataPacket) == 1);

template <typename Payload>
std::array<std::uint8_t, FRAME_HEADER_SIZE + COMMAND_SIZE + sizeof(Payload) + FRAME_TAIL_SIZE>
MakeFrame(const Payload &payload)
{
	constexpr auto FRAME_SIZE =
		FRAME_HEADER_SIZE + COMMAND_SIZE + sizeof(Payload) + FRAME_TAIL_SIZE;
	std::array<std::uint8_t, FRAME_SIZE> frame{};
	const auto dataLength = static_cast<std::uint16_t>(sizeof(Payload));

	frame[0] = START_OF_FRAME;
	frame[1] = static_cast<std::uint8_t>(dataLength & 0xffu);
	frame[2] = static_cast<std::uint8_t>(dataLength >> 8u);
	frame[3] = 1;
	frame[4] = robotpilots::algorithm::Crc8Calculate({frame.data(), FRAME_HEADER_SIZE - 1});
	frame[5] = static_cast<std::uint8_t>(Payload::ID & 0xffu);
	frame[6] = static_cast<std::uint8_t>(Payload::ID >> 8u);
	std::memcpy(frame.data() + FRAME_HEADER_SIZE + COMMAND_SIZE, &payload, sizeof(payload));

	const auto crc = robotpilots::algorithm::Crc16Calculate(
		{frame.data(), FRAME_SIZE - FRAME_TAIL_SIZE});
	std::memcpy(frame.data() + FRAME_SIZE - FRAME_TAIL_SIZE, &crc, sizeof(crc));
	return frame;
}

ZTEST(referee, game_status)
{
	GameStatusPacket packet{
		.GameType = 1,
		.GameProgress = 4,
		.StageRemainTime = 123,
		.SyncTimeStamp = 0,
	};
	const auto frame = MakeFrame(packet);

	module.InReferee({frame.data(), frame.size()});

	GameState result{};
	module.Out.GameState(result);
	zassert_equal(result.Mode, GameMode::UC, "game mode mismatch");
	zassert_equal(result.Stage, GameStage::InMatch, "game stage mismatch");
	zassert_equal(result.Remain.Value, 123, "remaining time mismatch");
}

ZTEST(referee, dart_info)
{
	DartInfoPacket packet{
		.DartRemainingTime = 17,
		.DartInfo = static_cast<std::uint16_t>((2u << 6u) | (5u << 3u)),
	};
	const auto frame = MakeFrame(packet);

	module.InReferee({frame.data(), frame.size()});

	DartInfo result{};
	module.Out.DartInfo(result);
	zassert_equal(result.RemainingTime.Value, 17, "dart remaining time mismatch");
	zassert_equal(result.TargetSelected, DartTarget::BaseFixed, "dart target mismatch");
	zassert_equal(result.HitCount, 5, "dart hit count mismatch");
}

ZTEST(referee, damage_event)
{
	DamageEvent result{};
	bool received = false;
	ramen::Pushable<DamageEvent> observer = [&](const DamageEvent &event) {
		result = event;
		received = true;
	};
	module.Out.DamageEvent >> observer;

	HurtDataPacket packet{
		.ArmorId = 3,
		.HpDeductionReason = 4,
	};
	const auto frame = MakeFrame(packet);
	module.InReferee({frame.data(), frame.size()});

	zassert_true(received, "damage event was not emitted");
	zassert_equal(result.ArmorId, 3, "damage armor id mismatch");
	zassert_equal(result.Source, DamageSource::OverHeat, "damage source mismatch");
}

} // namespace

ZTEST_SUITE(referee, NULL, NULL, NULL, NULL, NULL);

// NOLINTEND(readability-magic-numbers)
